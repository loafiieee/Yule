-- AI Opponent: singleplayer bot + in-engine self-play trainer.
-- VS AI / TRAIN AI are entered from the main menu's mode-cycling PLAY button.
--
-- Hybrid arbiter: a scripted heuristic (lib/heuristic.lua) plays the macro game
-- (navigation, sword recovery, unarmed play) so the bot is competent even fully
-- untrained; the evolved NN takes over fight-context ticks once training has
-- proven it against the scripted fighter (benchmark winrate, see trainer.lua).
local NN = mod.dofile("lib/nn.lua")
local A = mod.dofile("lib/actions.lua")
local F = mod.dofile("lib/features.lua")
local Policy = mod.dofile("lib/policy.lua")
local EVO = mod.dofile("lib/evo.lua")
local Codec = mod.dofile("lib/codec.lua")
local H = mod.dofile("lib/heuristic.lua")
local RW = mod.dofile("lib/reward.lua")
local P = mod.dofile("lib/path.lua")
local Bot = mod.dofile("bot.lua")
local Trainer = mod.dofile("trainer.lua")
local HT = mod.dofile("htrainer.lua")

-- Training map pool. banned_maps (config) is a comma list of selector indices
-- to exclude - default bans vanilla 5 (selector 4, the multi-score eggnog
-- map, a poor fencing teacher).
--
-- Map curriculum (config `curriculum`, default on): training starts on ONE
-- map until the champion is proficient there (benchmark winrate vs the
-- scripted fighter >= 55%), then the next allowed map unlocks, and so on.
-- The unlocked count persists as storage key "map_stage".
local function allowed_maps()
  local total = math.max(1, tonumber(mod.game.map_count()) or 1)
  local banned = {}
  for tok in tostring(config.get("banned_maps", "4") or ""):gmatch("[^,%s]+") do
    local n = tonumber(tok)
    if n then banned[n] = true end
  end
  local list = {}
  for sel = 0, total - 1 do
    if not banned[sel] then list[#list + 1] = sel end
  end
  if #list == 0 then list[1] = 0 end
  return list
end

local function pick_map(rand)
  local list = allowed_maps()
  local n = #list
  if config.get("curriculum", true) then
    local stage = tonumber(storage.get("map_stage", 1)) or 1
    if stage < 1 then stage = 1 end
    if stage < n then n = stage end
  end
  local i = 1 + math.floor(rand() * n)
  if i > n then i = n end
  return list[i]
end

local function curriculum_size()
  return #allowed_maps()
end

Policy.init(NN)
EVO.init(NN)
H.init({ NN = NN })
Bot.init({ NN = NN, A = A, F = F, Policy = Policy, H = H, P = P })
Trainer.init({ NN = NN, A = A, F = F, Policy = Policy, EVO = EVO, Codec = Codec, Bot = Bot, H = H,
               RW = RW, SIZES = { F.N_INPUTS, 32, 16, A.COUNT }, pick_map = pick_map,
               curriculum_size = curriculum_size })
HT.init({ NN = NN, A = A, F = F, Policy = Policy, EVO = EVO, Codec = Codec, Bot = Bot, H = H,
          RW = RW, SIZES = { F.N_INPUTS, 32, 16, A.COUNT }, CKPT_HARD = Trainer.CKPT_KEYS.hard,
          pick_map = pick_map })

local human_train = false   -- set by the TRAIN VS ME menu mode, cleared at match end

local DIFF = {
  easy   = { react_delay = 10, epsilon = 0.10, key = Trainer.CKPT_KEYS.easy,   need_ready = false },
  normal = { react_delay = 4,  epsilon = 0.03, key = Trainer.CKPT_KEYS.normal, need_ready = false },
  hard   = { react_delay = 1,  epsilon = 0.00, key = Trainer.CKPT_KEYS.hard,   need_ready = true },
}

local play_policy = nil        -- nil = pure scripted heuristic
local play_brain = 'heur'
local active_player = nil

local function config_difficulty()
  local v = config.get("difficulty", "normal")
  if v ~= "easy" and v ~= "normal" and v ~= "hard" then v = "normal" end
  return v
end

-- Load the combat brain for a difficulty: a trained checkpoint if one has been
-- earned (storage first, shipped data file second), else nil -> heuristic.
local function load_brain(diff_name)
  local dd = DIFF[diff_name] or DIFF.normal
  if dd.need_ready and storage.get("nn_ready", "0") ~= "1" then
    return nil, dd
  end
  local s = Codec.load(storage, dd.key)
  if s then
    local net = NN.deserialize(s)
    if net then return net, dd end
  end
  local ok, shipped = pcall(mod.dofile, "data/weights_" .. diff_name .. ".lua")
  if ok and type(shipped) == "string" then
    local net = NN.deserialize(shipped)
    if net then return net, dd end
  end
  return nil, dd
end

-- Add our modes to the main menu's mode selector. on_activate receives the
-- index of the player who pressed the button; returning true tells the menu
-- to proceed with the native START flow (map select -> match).
mod.game.register_menu_mode({
  id = "vs_ai",
  label = "VS AI",
  color = { 0.70, 0.45, 1.00 },
  on_activate = function(who)
    human_train = false
    mod.game.arm_ai_match(1 - who, false)
    return true
  end,
})
mod.game.register_menu_mode({
  id = "train_ai",
  label = "TRAIN AI",
  color = { 0.52, 0.32, 0.98 },
  on_activate = function(who)
    human_train = false
    mod.game.arm_ai_match(1 - who, true)
    return true
  end,
})
mod.game.register_menu_mode({
  id = "train_vs_me",
  label = "TRAIN VS ME",
  color = { 0.98, 0.62, 0.24 },
  on_activate = function(who)
    human_train = true
    mod.game.arm_ai_match(1 - who, false)
    return true
  end,
})

mod.on_tick(function()
  local m = mod.game.ai_match()
  if not m then
    if active_player then
      mod.game.input_clear(active_player)
      active_player, play_policy = nil, nil
      Bot.reset_scaffold()
    end
    Trainer.match_ended()
    if human_train then
      HT.match_ended()
      human_train = false
    end
    return
  end
  if not mod.ui.is_state("game") then return end
  if m.training then
    Trainer.tick()
    return
  end
  if human_train then
    HT.tick(m.ai_player)
    return
  end
  if active_player ~= m.ai_player then
    local diff = config_difficulty()
    local net, dd = load_brain(diff)
    if net then
      play_policy = Policy.new(net, { react_delay = dd.react_delay, epsilon = dd.epsilon, seed = 99 })
      play_brain = 'nn+heur'
    else
      play_policy = nil
      play_brain = 'heur'
    end
    active_player = m.ai_player
    Bot.reset_scaffold()
    mod.log("VS AI: driving player " .. tostring(active_player) .. " (" .. diff .. ", brain=" .. play_brain .. ")")
  end
  Bot.drive(active_player, play_policy)
end)

-- draw the routes the AI wants to take (world -> screen via the game camera)
local function draw_paths(players)
  local cam = mod.game.camera()
  if not cam or not cam.w or cam.w <= 1 then return end
  local sw, sh = mod.ui.screen_size()
  if not sw or sw <= 0 then return end
  local scale = math.min(sw / cam.w, sh / cam.h)
  mod.ui.begin_overlay()
  for _, pl in ipairs(players) do
    local pts = Bot.route_points(pl)
    if pts and #pts >= 1 then
      local col = (pl == 0) and { 0.30, 1.00, 0.50, 0.85 } or { 1.00, 0.55, 0.30, 0.85 }
      local px, py
      for _, pt in ipairs(pts) do
        local sx = sw * 0.5 + (pt.x - cam.x) * scale
        local sy = sh * 0.5 + (pt.y - cam.y) * scale
        if px then mod.ui.line(px, py, sx, sy, { line_w = 2, color = col }) end
        px, py = sx, sy
      end
      if px then mod.ui.rect(px - 3, py - 3, 6, 6, { color = col }) end
    end
  end
  mod.ui.end_overlay()
end

mod.on_frame(function()
  local m = mod.game.ai_match()
  if not m then return end
  if config.get("show_paths", true) and mod.ui.is_state("game") then
    if m.training then
      draw_paths({ 0, 1 })
    else
      draw_paths({ m.ai_player })
    end
  end
  if m.training then
    if config.get("show_train_overlay", true) then Trainer.overlay() end
    return
  end
  if human_train then
    if mod.ui.is_state("game") then HT.overlay() end
    return
  end
  if config.get("show_debug_overlay", false) and active_player and mod.ui.is_state("game") then
    local info = Bot.last(active_player)
    mod.ui.begin_overlay()
    mod.ui.rect(20, 20, 250, 44, { color = { 0.03, 0.04, 0.06, 0.75 } })
    mod.ui.text_at(string.format("AI p%d  %s  [%s]", active_player,
                                 tostring(info.mode or '?'), tostring(info.brain or play_brain)),
                   30, 32, 0.9, 0.9, 0.9, 0.9)
    mod.ui.text_at("mask " .. tostring(info.mask or 0), 30, 48, 0.85, 0.7, 0.7, 0.7)
    mod.ui.end_overlay()
  end
end)

mod.on_unload(function()
  mod.game.input_clear(0)
  mod.game.input_clear(1)
  Trainer.shutdown()
  HT.match_ended()
end)

mod.log("ai_opponent loaded (inputs=" .. F.N_INPUTS .. " actions=" .. A.COUNT .. ", hybrid arbiter)")
