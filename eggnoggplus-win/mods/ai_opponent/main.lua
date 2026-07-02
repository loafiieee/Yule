-- AI Opponent: singleplayer bot + in-engine self-play trainer.
-- VS AI / TRAIN AI are entered from the main menu's mode-cycling PLAY button.
local NN = mod.dofile("lib/nn.lua")
local A = mod.dofile("lib/actions.lua")
local F = mod.dofile("lib/features.lua")
local Policy = mod.dofile("lib/policy.lua")
local EVO = mod.dofile("lib/evo.lua")
local Codec = mod.dofile("lib/codec.lua")
local Bot = mod.dofile("bot.lua")
local Trainer = mod.dofile("trainer.lua")

Policy.init(NN)
EVO.init(NN)
Bot.init({ NN = NN, A = A, F = F, Policy = Policy })
Trainer.init({ NN = NN, A = A, F = F, Policy = Policy, EVO = EVO, Codec = Codec, Bot = Bot,
               SIZES = { F.N_INPUTS, 32, 16, A.COUNT } })

local SIZES = { F.N_INPUTS, 32, 16, A.COUNT }

local DIFF = {
  easy   = { react_delay = 10, epsilon = 0.10, key = "ckpt_easy" },
  normal = { react_delay = 4,  epsilon = 0.03, key = "ckpt_normal" },
  hard   = { react_delay = 1,  epsilon = 0.00, key = "ckpt_hard" },
}

local play_policy = nil
local active_player = nil

local function config_difficulty()
  local v = config.get("difficulty", "normal")
  if v ~= "easy" and v ~= "normal" and v ~= "hard" then v = "normal" end
  return v
end

local function load_net_for(diff_name)
  local d = DIFF[diff_name] or DIFF.normal
  -- 1) user-trained checkpoint in storage
  local s = Codec.load(storage, d.key)
  if s then
    local net = NN.deserialize(s)
    if net then return net, d end
  end
  -- 2) shipped checkpoint data file
  local ok, shipped = pcall(mod.dofile, "data/weights_" .. diff_name .. ".lua")
  if ok and type(shipped) == "string" then
    local net = NN.deserialize(shipped)
    if net then return net, d end
  end
  -- 3) fresh random net (pre-training fallback)
  mod.warn("no checkpoint for '" .. diff_name .. "'; using fresh random net")
  return NN.new(SIZES, 1337), d
end

mod.game.register_bot_provider()

mod.on_tick(function()
  local m = mod.game.ai_match()
  if not m then
    if active_player then
      mod.game.input_clear(active_player)
      active_player, play_policy = nil, nil
      Bot.reset_scaffold()
    end
    return
  end
  if not mod.ui.is_state("game") then return end
  if m.training then
    Trainer.tick()
    return
  end
  if active_player ~= m.ai_player or not play_policy then
    local diff = config_difficulty()
    local net, d = load_net_for(diff)
    play_policy = Policy.new(net, { react_delay = d.react_delay, epsilon = d.epsilon, seed = 99 })
    active_player = m.ai_player
    Bot.reset_scaffold()
    mod.log("VS AI: driving player " .. tostring(active_player) .. " (" .. diff .. ")")
  end
  Bot.drive(active_player, play_policy)
end)

mod.on_frame(function()
  local m = mod.game.ai_match()
  if m and m.training then Trainer.overlay() end
end)

mod.on_unload(function()
  mod.game.input_clear(0)
  mod.game.input_clear(1)
  Trainer.shutdown()
end)

mod.log("ai_opponent loaded (inputs=" .. F.N_INPUTS .. " actions=" .. A.COUNT .. ")")
