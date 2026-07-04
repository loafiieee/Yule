-- Human-in-the-loop trainer ("TRAIN VS ME"): the human plays normally, in real
-- time, while the population rotates through the AI seat in fixed time slices.
-- Fitness comes from actual kills scored against / conceded to the human - the
-- signal self-play can't provide (a policy that farms the scripted fighter's
-- blind spots gets crushed here and selected out).
--
-- Shares the population storage with the self-play trainer, so the two modes
-- interleave: bootstrap on self-play speed, then refine against the human.
local HT = {}
local d = nil

local POP_N = 12                -- small population: human time is expensive
local MUT = { elites = 2, mut_rate = 0.20, mut_scale = 0.25 }
local POP_KEY = "trainer_pop2"  -- shared with trainer.lua
local GEN_KEY = "htrainer_gen"

local R = { KILL = 120, DEATH = -60, DISARM = 10, DISARM_RANGE = 48,
            MAP_WIN = 300, MAP_LOSS = -100 }
-- A dying edge only counts as a kill if no map score follows within this many
-- ticks: eggnogg's win animation runs the WINNER through the dying state
-- (you win by leaping into the pit), so an immediate score means "win-fall",
-- not a kill.
local PENDING_TICKS = 120

local st = nil

function HT.init(deps) d = deps end

local function load_pop()
  local gen = tonumber(storage.get(GEN_KEY, 0)) or 0
  local nets = {}
  local dump = d.Codec.load(storage, POP_KEY)
  if dump then
    for chunk in dump:gmatch("([^\n]+)") do
      local net = d.NN.deserialize(chunk)
      if net then nets[#nets + 1] = net end
    end
  end
  local pop = { nets = {}, gen = gen, sizes = d.SIZES }
  local rand = d.NN.rng_new(os.time() % 100000 + 11)
  if #nets >= 1 then
    for i = 1, math.min(#nets, POP_N) do pop.nets[i] = nets[i] end
    while #pop.nets < POP_N do
      pop.nets[#pop.nets + 1] = d.NN.mutate(nets[1 + (#pop.nets % #nets)], 0.25, 0.25, rand)
    end
    mod.log("htrainer: population seeded from storage (" .. #nets .. " seeds)")
  else
    pop = d.EVO.new(POP_N, d.SIZES, os.time() % 100000 + 23)
    mod.log("htrainer: fresh random population")
  end
  return pop
end

local function fresh_session()
  return {
    pop = load_pop(),
    rand = d.NN.rng_new(os.time() % 100000 + 29),
    i = 1,
    fitness = {},
    policy = nil,
    slice_t = 0,
    tick = 0,
    fit = 0,
    was = { me = false, en = false },
    had_sword_en = nil,
    pending = {},                    -- deferred kill/death edges
    base_blob = nil,                 -- match state to reset to after map wins
    score_me = nil, score_en = nil,
    ai_kills = 0, human_kills = 0,   -- session scoreboard (real kills only)
    ai_wins = 0, human_wins = 0,     -- map wins
    gens_done = 0,
  }
end

local function is_dying(state_id) return state_id == 8 or state_id == 9 end

local function save_progress(s)
  local order = {}
  for i = 1, #s.pop.nets do order[i] = i end
  table.sort(order, function(a, b) return (s.fitness[a] or 0) > (s.fitness[b] or 0) end)
  local dump = {}
  for i = 1, math.min(8, #order) do
    dump[i] = d.NN.serialize(s.pop.nets[order[i]])
  end
  d.Codec.store(storage, POP_KEY, table.concat(dump, "\n"))
  d.Codec.store(storage, d.CKPT_HARD or "ckpt2_hard", d.NN.serialize(s.pop.nets[order[1]]))
  storage.set(GEN_KEY, tostring(s.pop.gen))
  storage.save()
end

local function next_fighter(s)
  s.fitness[s.i] = s.fit
  s.i = s.i + 1
  s.policy = nil
  if s.i > #s.pop.nets then
    save_progress(s)
    mod.log(string.format("htrainer: human-gen %d done (session score you %d : %d ai)",
                          s.pop.gen, s.human_kills, s.ai_kills))
    s.pop = d.EVO.next_gen(s.pop, s.fitness, MUT, s.rand)
    storage.set(GEN_KEY, tostring(s.pop.gen))
    s.fitness = {}
    s.i = 1
    s.gens_done = s.gens_done + 1
  end
end

-- commit deferred edges that were NOT followed by a map score (= real kills)
local function commit_pending(s, force)
  local keep = {}
  for _, ev in ipairs(s.pending) do
    if force or (s.tick - ev.t) >= PENDING_TICKS then
      if ev.who == 'en' then
        s.fit = s.fit + R.KILL
        s.ai_kills = s.ai_kills + 1
      else
        s.fit = s.fit + R.DEATH
        s.human_kills = s.human_kills + 1
      end
    else
      keep[#keep + 1] = ev
    end
  end
  s.pending = keep
end

-- a map was scored: the buffered dying edge was the winner's pit-fall, not a
-- kill; award win/loss and restart the round in place so training continues
local function map_scored(s, ai_won)
  s.pending = {}
  if ai_won then
    s.fit = s.fit + R.MAP_WIN
    s.ai_wins = s.ai_wins + 1
  else
    s.fit = s.fit + R.MAP_LOSS
    s.human_wins = s.human_wins + 1
  end
  if s.base_blob then
    mod.game.apply_full_state_blob(s.base_blob)
  end
  s.was = { me = false, en = false }
  s.had_sword_en = nil
  s.score_me, s.score_en = nil, nil   -- re-baseline after the reset
  d.Bot.reset_scaffold()
  if s.policy then d.Policy.reset(s.policy) end
end

function HT.tick(ai_player)
  if not st then st = fresh_session() end
  local s = st
  local slice_ticks = tonumber(config.get("human_slice_ticks", 1200)) or 1200
  if slice_ticks < 300 then slice_ticks = 300 end
  s.tick = s.tick + 1

  if not s.base_blob then
    s.base_blob = mod.game.full_state_blob()
  end

  if not s.policy then
    s.policy = d.Policy.new(s.pop.nets[s.i],
                            { react_delay = 0, epsilon = 0.03, seed = s.pop.gen * 31 + s.i })
    s.slice_t = 0
    s.fit = 0
    s.was = { me = false, en = false }
    s.had_sword_en = nil
    s.pending = {}
    d.Bot.reset_scaffold()
  end

  d.Bot.drive(ai_player, s.policy)

  -- map score first: a score converts buffered dying edges into a win-fall
  local ns = mod.game.native_state()
  local score_me = (ai_player == 0) and (ns.score_p0 or 0) or (ns.score_p1 or 0)
  local score_en = (ai_player == 0) and (ns.score_p1 or 0) or (ns.score_p0 or 0)
  if s.score_me == nil then
    s.score_me, s.score_en = score_me, score_en
  elseif score_me > s.score_me then
    map_scored(s, true)
    s.slice_t = s.slice_t + 1
    return
  elseif score_en > s.score_en then
    map_scored(s, false)
    s.slice_t = s.slice_t + 1
    return
  end

  local snap = mod.game.snapshot(ai_player, false)
  if snap and snap.in_game and snap.player and snap.enemy then
    local me, en = snap.player, snap.enemy
    local me_dying, en_dying = is_dying(me.state_id or 0), is_dying(en.state_id or 0)
    if en_dying and not s.was.en then s.pending[#s.pending + 1] = { who = 'en', t = s.tick } end
    if me_dying and not s.was.me then s.pending[#s.pending + 1] = { who = 'me', t = s.tick } end
    s.was.me, s.was.en = me_dying, en_dying
    local en_sword = en.has_sword and true or false
    if s.had_sword_en ~= nil and s.had_sword_en and not en_sword and
       math.abs(en.x - me.x) < R.DISARM_RANGE then
      s.fit = s.fit + R.DISARM
    end
    s.had_sword_en = en_sword
  end

  commit_pending(s, false)

  s.slice_t = s.slice_t + 1
  if s.slice_t >= slice_ticks then
    commit_pending(s, true)
    next_fighter(s)
  end
end

function HT.overlay()
  if not st then return end
  local s = st
  local slice_ticks = tonumber(config.get("human_slice_ticks", 1200)) or 1200
  local secs_left = math.max(0, math.floor((slice_ticks - s.slice_t) / 60))
  mod.ui.begin_overlay()
  mod.ui.rect(20, 20, 330, 110, { color = { 0.03, 0.04, 0.06, 0.80 } })
  mod.ui.text_at("TRAIN VS ME  human-gen " .. tostring(s.pop.gen), 32, 34, 1.0, 1.0, 0.75, 0.45)
  mod.ui.text_at(string.format("fighter %d/%d  next in %ds  this one: %+d",
                               s.i, #s.pop.nets, secs_left, s.fit), 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("kills  YOU %d : %d AI    maps  YOU %d : %d AI",
                               s.human_kills, s.ai_kills, s.human_wins, s.ai_wins),
                 32, 74, 0.95, 0.6, 1.0, 0.7)
  mod.ui.text_at("keep fighting - every kill teaches it", 32, 92, 0.8, 0.6, 0.6, 0.6)
  mod.ui.end_overlay()
end

function HT.match_ended()
  if st and next(st.fitness) then save_progress(st) end
  st = nil
end

return HT
