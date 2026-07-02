-- Self-play neuroevolution trainer. Runs inside a TRAIN AI match: every gameplay
-- tick it fast-forwards up to train_ticks_per_frame sim ticks (set both players'
-- inputs -> simulate_ticks(1)), scores episodes, and evolves the population.
--
-- Fitness design notes (anti-exploit):
-- * Kill +120 vs death -60: an even fight is +30 EV, so avoiding combat loses
--   to engaging (cowardice was the dominant strategy under symmetric +/-100).
-- * Episodes CONTINUE through kills (respawns and all) and end only on a map
--   score, a 2-room push, or timeout - so post-kill play (advancing, handling
--   the respawned blocker) is actually trained.
-- * Each genome plays 2 episodes per generation, once per side; features and
--   actions are goal-mirrored, so experience transfers exactly to either side.
-- * Sword-loss (-5) and pickup (+5) cancel over throw/re-pick cycles, so
--   rearming can't be farmed; disarming the enemy up close pays +10.
-- * Per-tick deltas are clamped +-8px so respawn teleports can't be farmed
--   (or unfairly punished) through the progress/closing terms.
local Trainer = {}
local d = nil

local POP_N = 32
local EPISODE_TICKS = 1800
local EPISODES_PER_GENOME = 2   -- one per side
local SAVE_TOP_N = 8            -- resumable population seeds kept in storage
local MUT = { elites = 4, mut_rate = 0.15, mut_scale = 0.25 }

-- v2 keys: feature/action semantics changed (goal-mirroring), old nets are
-- incompatible garbage - never resume them.
local POP_KEY = "trainer_pop2"
local GEN_KEY = "trainer_gen2"
Trainer.CKPT_KEYS = { easy = "ckpt2_easy", normal = "ckpt2_normal", hard = "ckpt2_hard" }

local R = {
  KILL = 120, DEATH = -60,
  LEADER_TICK = 0.02, TIME_TICK = -0.01,
  PROGRESS = 0.05, CLOSING = 0.02, DELTA_CLAMP = 8,
  DISARM = 10, DISARM_RANGE = 48,
  SWORD_LOST = -5, SWORD_GAINED = 5,
  ROOM = 30, SCORE_WIN = 300, SCORE_LOSS = -150,
}

local st = nil  -- training session state (nil when idle)

function Trainer.init(deps) d = deps end

local function load_seed_nets()
  local nets = {}
  local dump = d.Codec.load(storage, POP_KEY)
  if dump then
    for chunk in dump:gmatch("([^\n]+)") do
      local net = d.NN.deserialize(chunk)
      if net then nets[#nets + 1] = net end
    end
  end
  return nets
end

local function fresh_session()
  local gen = tonumber(storage.get(GEN_KEY, 0)) or 0
  local seeds = load_seed_nets()
  local pop
  if #seeds >= 2 then
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
    pair_i = 1,          -- genome under evaluation
    ep_num = 1,          -- 1..EPISODES_PER_GENOME for that genome
    ep = nil,
    paused = false,
    best_fit = nil, mean_fit = nil,
    ticks_done = 0, kills_seen = 0,
    last_mask = { [0] = 0, [1] = 0 },
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
  local gp = (s.ep_num % 2 == 1) and 0 or 1   -- side the genome plays this episode
  s.ep = {
    gp = gp,
    goal = (gp == 0) and 1 or -1,
    a = d.Policy.new(s.pop.nets[s.pair_i], { react_delay = 0, epsilon = 0.05, seed = s.pair_i * 31 + s.pop.gen * 7 + s.ep_num }),
    b = d.Policy.new(s.pop.nets[oi], { react_delay = 0, epsilon = 0.05, seed = oi * 37 + s.pop.gen * 11 + s.ep_num }),
    fit = 0,
    tick = 0,
    was_dying = { me = false, enemy = false },
    had_sword = { me = nil, enemy = nil },
    start_room = nil, prev_x = nil, prev_dist = nil,
    start_score_me = nil, start_score_enemy = nil,
  }
end

local function is_dying(state_id) return state_id == 8 or state_id == 9 end

-- One sim tick of the current episode. Returns true when the episode ended.
local function episode_step(s)
  local e = s.ep
  local gp = e.gp
  -- genome drives player gp; sparring opponent drives the other side
  local mask_me, ctx_me = d.Bot.decide_mask(gp, e.a)
  local mask_op = d.Bot.decide_mask(1 - gp, e.b)
  if not mask_me or not mask_op then return true end  -- lost gameplay state
  s.last_mask[gp], s.last_mask[1 - gp] = mask_me, mask_op
  mod.game.set_input(gp, mask_me, 1, true)
  mod.game.set_input(1 - gp, mask_op, 1, true)
  mod.game.simulate_ticks(1)
  e.tick = e.tick + 1

  local snap = mod.game.snapshot(gp, false)
  if not snap or not snap.in_game or not snap.player or not snap.enemy then return true end
  local me, en = snap.player, snap.enemy
  local ns = mod.game.native_state()

  if not e.start_room then
    e.start_room = me.room_index or 0
    e.start_score_me = (gp == 0) and (ns.score_p0 or 0) or (ns.score_p1 or 0)
    e.start_score_enemy = (gp == 0) and (ns.score_p1 or 0) or (ns.score_p0 or 0)
  end

  local ended = false

  -- kills / deaths (edges); episode CONTINUES so post-kill play is learned
  local me_dying, en_dying = is_dying(me.state_id or 0), is_dying(en.state_id or 0)
  if en_dying and not e.was_dying.enemy then
    e.fit = e.fit + R.KILL
    s.kills_seen = s.kills_seen + 1
    if not s.goal_logged then
      s.goal_logged = true
      mod.log(string.format("trainer: first kill sample gp=%d me.x=%.0f enemy.x=%.0f leader=%d",
                            gp, me.x, en.x, ns.leader or -1))
    end
  end
  if me_dying and not e.was_dying.me then e.fit = e.fit + R.DEATH end
  e.was_dying.me, e.was_dying.enemy = me_dying, en_dying

  -- disarm / rearm economy (balanced against throw+pickup farming)
  local me_sword, en_sword = me.has_sword and true or false, en.has_sword and true or false
  if e.had_sword.me ~= nil then
    if e.had_sword.me and not me_sword then e.fit = e.fit + R.SWORD_LOST end
    if (not e.had_sword.me) and me_sword then e.fit = e.fit + R.SWORD_GAINED end
    if e.had_sword.enemy and not en_sword and math.abs(en.x - me.x) < R.DISARM_RANGE then
      e.fit = e.fit + R.DISARM
    end
  end
  e.had_sword.me, e.had_sword.enemy = me_sword, en_sword

  -- leader time + time pressure
  if (ns.leader or -1) == gp then e.fit = e.fit + R.LEADER_TICK end
  e.fit = e.fit + R.TIME_TICK

  -- per-tick goal progress (wrong-way running punished; respawn teleports clamped)
  if e.prev_x then
    local dx = (me.x - e.prev_x) * e.goal
    if dx > R.DELTA_CLAMP then dx = R.DELTA_CLAMP elseif dx < -R.DELTA_CLAMP then dx = -R.DELTA_CLAMP end
    e.fit = e.fit + R.PROGRESS * dx
  end
  e.prev_x = me.x

  -- engagement: closing on the opponent in the same room
  local room_now = me.room_index or e.start_room or 0
  local enemy_room = en.room_index or room_now
  if room_now == enemy_room then
    local dist = math.abs(en.x - me.x)
    if e.prev_dist then
      local closing = e.prev_dist - dist
      if closing > R.DELTA_CLAMP then closing = R.DELTA_CLAMP
      elseif closing < -R.DELTA_CLAMP then closing = -R.DELTA_CLAMP end
      e.fit = e.fit + R.CLOSING * closing
    end
    e.prev_dist = dist
  else
    e.prev_dist = nil
  end

  -- terminal conditions: map score (real win/loss), a 2-room push, or timeout
  local score_me = (gp == 0) and (ns.score_p0 or 0) or (ns.score_p1 or 0)
  local score_en = (gp == 0) and (ns.score_p1 or 0) or (ns.score_p0 or 0)
  if e.start_score_me and score_me > e.start_score_me then
    e.fit = e.fit + R.SCORE_WIN
    ended = true
  elseif e.start_score_enemy and score_en > e.start_score_enemy then
    e.fit = e.fit + R.SCORE_LOSS
    ended = true
  end
  local room_prog = (room_now - (e.start_room or room_now)) * e.goal
  if math.abs(room_now - (e.start_room or room_now)) >= 2 then ended = true end
  if e.tick >= EPISODE_TICKS then ended = true end
  if ended then
    e.fit = e.fit + R.ROOM * room_prog
  end
  return ended
end

function Trainer.save_checkpoints(s)
  local fit = s.fitness
  local order = {}
  for i = 1, #s.pop.nets do order[i] = i end
  table.sort(order, function(x, y) return (fit[x] or 0) > (fit[y] or 0) end)
  local best = s.pop.nets[order[1]]
  d.Codec.store(storage, Trainer.CKPT_KEYS.hard, d.NN.serialize(best))
  if s.pop.gen == 30 then d.Codec.store(storage, Trainer.CKPT_KEYS.easy, d.NN.serialize(best)) end
  if s.pop.gen == 300 then d.Codec.store(storage, Trainer.CKPT_KEYS.normal, d.NN.serialize(best)) end
  local dump = {}
  for i = 1, math.min(SAVE_TOP_N, #order) do
    dump[i] = d.NN.serialize(s.pop.nets[order[i]])
  end
  d.Codec.store(storage, POP_KEY, table.concat(dump, "\n"))
  storage.set(GEN_KEY, tostring(s.pop.gen))
  storage.save()
end

local function finish_episode(s)
  s.fitness[s.pair_i] = (s.fitness[s.pair_i] or 0) + s.ep.fit
  s.ep_num = s.ep_num + 1
  if s.ep_num > EPISODES_PER_GENOME then
    s.ep_num = 1
    s.pair_i = s.pair_i + 1
  end
  if s.pair_i > #s.pop.nets then
    local best, sum = nil, 0
    for i = 1, #s.pop.nets do
      local f = s.fitness[i] or 0
      if not best or f > best then best = f end
      sum = sum + f
    end
    s.best_fit, s.mean_fit = best, sum / #s.pop.nets
    Trainer.save_checkpoints(s)
    mod.log(string.format("trainer: gen %d done  best %.1f  mean %.1f  kills so far %d",
                          s.pop.gen, s.best_fit, s.mean_fit, s.kills_seen))
    s.pop = d.EVO.next_gen(s.pop, s.fitness, MUT, s.rand)
    storage.set(GEN_KEY, tostring(s.pop.gen))
    s.fitness = {}
    s.pair_i = 1
    s.ep_num = 1
  end
  begin_episode(s)
end

function Trainer.tick()
  if not st then st = fresh_session() end
  if st.paused then
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
  -- hold the last masks through the visible native tick after on_tick returns
  mod.game.set_input(0, st.last_mask[0], 1, true)
  mod.game.set_input(1, st.last_mask[1], 1, true)
end

function Trainer.overlay()
  if not st then return end
  mod.ui.begin_overlay()
  mod.ui.rect(20, 20, 330, 146, { color = { 0.03, 0.04, 0.06, 0.85 } })
  mod.ui.text_at("TRAIN AI  gen " .. tostring(st.pop.gen), 32, 34, 1.0, 1.0, 0.8, 0.6)
  mod.ui.text_at(string.format("genome %d/%d  side %d  ep tick %d",
                               st.pair_i, #st.pop.nets, st.ep and st.ep.gp or 0,
                               st.ep and st.ep.tick or 0), 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("best %.1f  mean %.1f", st.best_fit or 0, st.mean_fit or 0),
                 32, 74, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("sim ticks %d   kills %d", st.ticks_done, st.kills_seen),
                 32, 92, 0.9, 0.7, 0.7, 0.7)
  if mod.ui.button_at("train_pause", st.paused and "RESUME" or "PAUSE", 32, 112, 90, 26) then
    st.paused = not st.paused
  end
  if mod.ui.button_at("train_save", "SAVE", 130, 112, 70, 26) then
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
