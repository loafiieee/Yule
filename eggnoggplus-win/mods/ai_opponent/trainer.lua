-- Self-play neuroevolution trainer. Runs inside a TRAIN AI match: every gameplay
-- tick it fast-forwards up to train_ticks_per_frame sim ticks (set both players'
-- inputs -> simulate_ticks(1)), scores episodes, and evolves the population.
local Trainer = {}
local d = nil

local POP_N = 32
local EPISODE_TICKS = 1800
local SAVE_TOP_N = 8   -- resumable population seeds kept in storage
local MUT = { elites = 4, mut_rate = 0.15, mut_scale = 0.25 }

local st = nil  -- training session state (nil when idle)

function Trainer.init(deps) d = deps end

local function load_seed_nets()
  local nets = {}
  local dump = d.Codec.load(storage, "trainer_pop")
  if dump then
    for chunk in dump:gmatch("([^\n]+)") do
      local net = d.NN.deserialize(chunk)
      if net then nets[#nets + 1] = net end
    end
  end
  return nets
end

local function fresh_session()
  local gen = tonumber(storage.get("trainer_gen", 0)) or 0
  local seeds = load_seed_nets()
  local pop
  if #seeds >= 2 then
    -- rebuild a full population from saved seeds: keep seeds, fill with mutants
    local rand = d.NN.rng_new(os.time() % 100000 + 3)
    pop = { nets = {}, gen = gen, sizes = d.SIZES }
    for i = 1, math.min(#seeds, POP_N) do pop.nets[i] = seeds[i] end
    while #pop.nets < POP_N do
      local base = seeds[1 + (#pop.nets % #seeds)]
      pop.nets[#pop.nets + 1] = d.NN.mutate(base, 0.3, 0.3, rand)
    end
    mod.log("trainer: resumed population from storage (gen " .. gen .. ")")
  else
    pop = d.EVO.new(POP_N, d.SIZES, os.time() % 100000)
    mod.log("trainer: fresh random population")
  end
  return {
    pop = pop,
    rand = d.NN.rng_new(os.time() % 100000 + 17),
    base_blob = nil,
    fitness = {},
    pair_i = 1,          -- genome under evaluation (plays as player 0)
    ep = nil,            -- per-episode accumulators
    paused = false,
    best_fit = nil, mean_fit = nil,
    ticks_done = 0,
    last_mask0 = 0, last_mask1 = 0,
    goal_logged = false,
  }
end

local function pick_opponent(s)
  local j = 1 + math.floor(s.rand() * #s.pop.nets)
  if j > #s.pop.nets then j = #s.pop.nets end
  if j == s.pair_i then j = (j % #s.pop.nets) + 1 end
  return j
end

local function begin_episode(s)
  if s.base_blob then mod.game.apply_full_state_blob(s.base_blob) end
  d.Bot.reset_scaffold()
  local oi = pick_opponent(s)
  s.ep = {
    a = d.Policy.new(s.pop.nets[s.pair_i], { react_delay = 0, epsilon = 0.05, seed = s.pair_i * 31 + s.pop.gen }),
    b = d.Policy.new(s.pop.nets[oi], { react_delay = 0, epsilon = 0.05, seed = oi * 37 + s.pop.gen }),
    fit_a = 0,
    tick = 0,
    was_dying = { [0] = false, [1] = false },
    start_x = nil, start_room = nil,
    prev_x = nil, prev_dist = nil,
  }
end

local function is_dying(state_id) return state_id == 8 or state_id == 9 end

-- One sim tick of the current episode. Returns true when the episode ended.
local function episode_step(s)
  local e = s.ep
  local mask0, ctx0 = d.Bot.decide_mask(0, e.a)
  local mask1, ctx1 = d.Bot.decide_mask(1, e.b)
  if not mask0 or not mask1 then return true end  -- lost gameplay state
  if not e.start_x then
    e.start_x = ctx0.snap.player.x
    e.start_room = ctx0.my_room
  end
  s.last_mask0, s.last_mask1 = mask0, mask1
  mod.game.set_input(0, mask0, 1, true)
  mod.game.set_input(1, mask1, 1, true)
  mod.game.simulate_ticks(1)
  e.tick = e.tick + 1

  local s0 = mod.game.snapshot(0, false)
  if not s0 or not s0.in_game or not s0.player or not s0.enemy then return true end
  local ns = mod.game.native_state()
  local p_dying = is_dying(s0.player.state_id or 0)
  local o_dying = is_dying(s0.enemy.state_id or 0)
  local ended = false
  if o_dying and not e.was_dying[1] then
    e.fit_a = e.fit_a + 100
    ended = true
    if not s.goal_logged then
      s.goal_logged = true
      mod.log(string.format("trainer: first kill sample p0.x=%.0f p1.x=%.0f leader=%d",
                            s0.player.x, s0.enemy.x, ns.leader or -1))
    end
  end
  if p_dying and not e.was_dying[0] then e.fit_a = e.fit_a - 100; ended = true end
  e.was_dying[0], e.was_dying[1] = p_dying, o_dying
  if (ns.leader or -1) == 0 then e.fit_a = e.fit_a + 0.05 end
  e.fit_a = e.fit_a - 0.01
  -- per-tick goal progress: rewards moving toward the goal, PUNISHES running the
  -- wrong way symmetrically (player 0 goal assumed +x; see first-kill log)
  if e.prev_x then
    local dx = s0.player.x - e.prev_x
    if dx > 8 then dx = 8 elseif dx < -8 then dx = -8 end  -- teleport/respawn guard
    e.fit_a = e.fit_a + 0.05 * dx
  end
  e.prev_x = s0.player.x
  -- engagement: closing distance to the opponent in the same room is rewarded,
  -- backing away from the fight costs the same amount
  local room_now = s0.player.room_index or e.start_room or 0
  local enemy_room = s0.enemy.room_index or room_now
  if room_now == enemy_room then
    local dist = math.abs(s0.enemy.x - s0.player.x)
    if e.prev_dist then
      local closing = e.prev_dist - dist
      if closing > 8 then closing = 8 elseif closing < -8 then closing = -8 end
      e.fit_a = e.fit_a + 0.02 * closing
    end
    e.prev_dist = dist
  else
    e.prev_dist = nil
  end
  if math.abs(room_now - (e.start_room or room_now)) >= 2 then ended = true end
  if e.tick >= EPISODE_TICKS then ended = true end
  if ended then
    e.fit_a = e.fit_a + 30 * (room_now - (e.start_room or room_now))
  end
  return ended
end

function Trainer.save_checkpoints(s)
  local fit = s.fitness
  local order = {}
  for i = 1, #s.pop.nets do order[i] = i end
  table.sort(order, function(x, y) return (fit[x] or 0) > (fit[y] or 0) end)
  local best = s.pop.nets[order[1]]
  d.Codec.store(storage, "ckpt_hard", d.NN.serialize(best))
  if s.pop.gen == 10 then d.Codec.store(storage, "ckpt_easy", d.NN.serialize(best)) end
  if s.pop.gen == 40 then d.Codec.store(storage, "ckpt_normal", d.NN.serialize(best)) end
  local dump = {}
  for i = 1, math.min(SAVE_TOP_N, #order) do
    dump[i] = d.NN.serialize(s.pop.nets[order[i]])
  end
  d.Codec.store(storage, "trainer_pop", table.concat(dump, "\n"))
  storage.set("trainer_gen", tostring(s.pop.gen))
  storage.save()
end

local function finish_episode(s)
  s.fitness[s.pair_i] = (s.fitness[s.pair_i] or 0) + s.ep.fit_a
  s.pair_i = s.pair_i + 1
  if s.pair_i > #s.pop.nets then
    local best, sum = nil, 0
    for i = 1, #s.pop.nets do
      local f = s.fitness[i] or 0
      if not best or f > best then best = f end
      sum = sum + f
    end
    s.best_fit, s.mean_fit = best, sum / #s.pop.nets
    Trainer.save_checkpoints(s)
    mod.log(string.format("trainer: gen %d done  best %.1f  mean %.1f",
                          s.pop.gen, s.best_fit, s.mean_fit))
    s.pop = d.EVO.next_gen(s.pop, s.fitness, MUT, s.rand)
    storage.set("trainer_gen", tostring(s.pop.gen))
    s.fitness = {}
    s.pair_i = 1
  end
  begin_episode(s)
end

function Trainer.tick()
  if not st then st = fresh_session() end
  if st.paused then
    -- keep the visible (native) tick harmless while paused
    mod.game.set_input(0, 0, 1, true)
    mod.game.set_input(1, 0, 1, true)
    return
  end
  if not st.base_blob then
    local blob = mod.game.full_state_blob()
    if not blob then return end
    st.base_blob = blob
    begin_episode(st)
  end
  local budget = tonumber(config.get("train_ticks_per_frame", 120)) or 120
  if budget < 1 then budget = 1 end
  if budget > 600 then budget = 600 end
  for _ = 1, budget do
    if episode_step(st) then finish_episode(st) end
    st.ticks_done = st.ticks_done + 1
  end
  -- the native (visible) tick after on_tick returns: hold the last masks so the
  -- rendered frame continues the current episode instead of reading real input
  mod.game.set_input(0, st.last_mask0, 1, true)
  mod.game.set_input(1, st.last_mask1, 1, true)
end

function Trainer.overlay()
  if not st then return end
  mod.ui.begin_overlay()
  mod.ui.rect(20, 20, 320, 128, { color = { 0.03, 0.04, 0.06, 0.85 } })
  mod.ui.text_at("TRAIN AI  gen " .. tostring(st.pop.gen), 32, 34, 1.0, 1.0, 0.8, 0.6)
  mod.ui.text_at(string.format("genome %d/%d  ep tick %d", st.pair_i, #st.pop.nets,
                               st.ep and st.ep.tick or 0), 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("best %.1f  mean %.1f", st.best_fit or 0, st.mean_fit or 0),
                 32, 74, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at("sim ticks: " .. tostring(st.ticks_done), 32, 92, 0.9, 0.7, 0.7, 0.7)
  if mod.ui.button_at("train_pause", st.paused and "RESUME" or "PAUSE", 32, 108, 90, 26) then
    st.paused = not st.paused
  end
  if mod.ui.button_at("train_save", "SAVE", 130, 108, 70, 26) then
    if next(st.fitness) then Trainer.save_checkpoints(st) end
  end
  mod.ui.end_overlay()
end

-- Called when the TRAIN match ends (back to menu): persist progress and drop the
-- session so the next TRAIN run captures a fresh base state for its map.
function Trainer.match_ended()
  if st and next(st.fitness) then Trainer.save_checkpoints(st) end
  st = nil
end

function Trainer.shutdown()
  Trainer.match_ended()
end

return Trainer
