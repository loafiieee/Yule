-- Human-in-the-loop trainer ("TRAIN VS ME"): the human plays normally, in real
-- time, while the population rotates through the AI seat in fixed time slices.
--
-- Fitness is exactly four unambiguous ledger events (native player_die detour,
-- see mod.game.combat_ledger - goal dives are pre-classified and never count
-- as deaths):
--   human died   +120      ai died      -60
--   ai scored    +300      human scored -100
-- Match end hops to a random map immediately (online-flow recipe), so a
-- session never falls back to the menu. No sword/disarm shaping: throws and
-- pickups are indistinguishable from disarms and only add noise.
--
-- Shares the population storage with the self-play trainer, so the two modes
-- interleave: bootstrap on self-play speed, then refine against the human.
local HT = {}
local d = nil

local POP_N = 12                -- small population: human time is expensive
local MUT = { elites = 2, mut_rate = 0.20, mut_scale = 0.25 }
local POP_KEY = "trainer_pop2"  -- shared with trainer.lua
local GEN_KEY = "htrainer_gen"

local R = { KILL = 120, DEATH = -60, SCORE = 300, OPP_SCORE = -100 }

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
    fit = 0,
    -- ledger baselines (captured on first tick, resynced after map hops)
    led = nil,
    -- session scoreboard
    ai_kills = 0, human_kills = 0,
    ai_points = 0, human_points = 0,
    gens_done = 0,
  }
end

-- ledger values from the AI player's perspective
local function led_view(ai_player)
  local led = mod.game.combat_ledger()
  if ai_player == 0 then
    return { d_ai = led.deaths0, d_hu = led.deaths1,
             s_ai = led.scores0, s_hu = led.scores1,
             ends = led.match_ends, winner = led.last_winner }
  end
  return { d_ai = led.deaths1, d_hu = led.deaths0,
           s_ai = led.scores1, s_hu = led.scores0,
           ends = led.match_ends, winner = led.last_winner }
end

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
    mod.log(string.format("htrainer: human-gen %d done (kills you %d : %d ai, points you %d : %d ai)",
                          s.pop.gen, s.human_kills, s.ai_kills, s.human_points, s.ai_points))
    s.pop = d.EVO.next_gen(s.pop, s.fitness, MUT, s.rand)
    storage.set(GEN_KEY, tostring(s.pop.gen))
    s.fitness = {}
    s.i = 1
    s.gens_done = s.gens_done + 1
  end
end

function HT.tick(ai_player)
  if not st then st = fresh_session() end
  local s = st
  local slice_ticks = tonumber(config.get("human_slice_ticks", 1200)) or 1200
  if slice_ticks < 300 then slice_ticks = 300 end

  if not s.policy then
    s.policy = d.Policy.new(s.pop.nets[s.i],
                            { react_delay = 0, epsilon = 0.03, seed = s.pop.gen * 31 + s.i })
    s.slice_t = 0
    s.fit = 0
    s.pot = nil
    d.Bot.reset_scaffold()
  end

  local snap = mod.game.snapshot(ai_player, false)
  if not (snap and snap.in_game and snap.player and snap.enemy) then return end
  local led = led_view(ai_player)
  if not s.led then s.led = led end

  -- MATCH END FIRST: on the end tick all ledger deltas are dive artifacts
  -- (the winning dive briefly registers as a death because the engine sets
  -- scores/countdown outside player_die) - award the map result, hop to a
  -- random map immediately, and skip everything else this tick
  if led.ends > s.led.ends or (snap.end_countdown or 0) > 0 then
    local winner = (led.ends > s.led.ends and led.winner and led.winner >= 0)
                   and led.winner or snap.leader_index
    if winner == ai_player then
      s.fit = s.fit + R.SCORE
      s.ai_points = s.ai_points + 1
    elseif winner ~= nil then
      s.fit = s.fit + R.OPP_SCORE
      s.human_points = s.human_points + 1
    end
    mod.game.start_match(d.pick_map(s.rand))
    s.led = led_view(ai_player)   -- resync across the hop (drops dive deltas)
    s.pot = nil
    d.Bot.reset_scaffold()
    if s.policy then d.Policy.reset(s.policy) end
    s.slice_t = s.slice_t + 1
    return
  end

  -- scoring dives (points modes: score and respawn, match continues)
  local ai_dove = led.s_ai > s.led.s_ai
  if ai_dove then
    local n = led.s_ai - s.led.s_ai
    s.fit = s.fit + R.SCORE * n
    s.ai_points = s.ai_points + n
  end
  if led.s_hu > s.led.s_hu then
    local n = led.s_hu - s.led.s_hu
    s.fit = s.fit + R.OPP_SCORE * n
    s.human_points = s.human_points + n
  end

  -- real kills / deaths
  local ai_died = led.d_ai > s.led.d_ai
  if led.d_hu > s.led.d_hu then
    local n = led.d_hu - s.led.d_hu
    s.fit = s.fit + R.KILL * n
    s.ai_kills = s.ai_kills + n
  end
  if ai_died then
    local n = led.d_ai - s.led.d_ai
    s.fit = s.fit + R.DEATH * n
    s.human_kills = s.human_kills + n
  end

  s.led = led

  -- potential shaping: reward per-tick progress toward winning (camping = 0).
  -- Goal side is engine-fixed per player index (player+0x9C from player_new:
  -- P0 pushes RIGHT, P1 pushes LEFT) - never positional.
  local lead = snap.leader_index
  local goal = (ai_player == 0) and 1 or -1
  local obs = { x = snap.player.x, goal = goal,
                has_sword = snap.player.has_sword and true or false,
                enemy_has_sword = snap.enemy.has_sword and true or false,
                is_leader = (lead == ai_player),
                dist = math.abs(snap.enemy.x - snap.player.x) }
  if ai_died or ai_dove or (s.pot and s.pot.goal ~= goal) then
    s.pot = nil                    -- teleport or goal flip: rebaseline
  else
    s.fit = s.fit + d.RW.delta(s.pot, obs)
    s.pot = obs
  end

  d.Bot.drive(ai_player, s.policy)

  s.slice_t = s.slice_t + 1
  if s.slice_t >= slice_ticks then next_fighter(s) end
end

function HT.overlay()
  if not st then return end
  local s = st
  local slice_ticks = tonumber(config.get("human_slice_ticks", 1200)) or 1200
  local secs_left = math.max(0, math.floor((slice_ticks - s.slice_t) / 60))
  mod.ui.begin_overlay()
  mod.ui.rect(20, 20, 344, 110, { color = { 0.03, 0.04, 0.06, 0.80 } })
  mod.ui.text_at("TRAIN VS ME  human-gen " .. tostring(s.pop.gen), 32, 34, 1.0, 1.0, 0.75, 0.45)
  mod.ui.text_at(string.format("fighter %d/%d  next in %ds  this one: %+.0f",
                               s.i, #s.pop.nets, secs_left, s.fit), 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("kills YOU %d : %d AI   points YOU %d : %d AI",
                               s.human_kills, s.ai_kills, s.human_points, s.ai_points),
                 32, 74, 0.95, 0.6, 1.0, 0.7)
  mod.ui.text_at("keep fighting - every kill teaches it", 32, 92, 0.8, 0.6, 0.6, 0.6)
  mod.ui.end_overlay()
end

function HT.match_ended()
  if st and next(st.fitness) then save_progress(st) end
  st = nil
end

return HT
