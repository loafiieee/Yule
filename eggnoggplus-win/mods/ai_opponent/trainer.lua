-- Self-play neuroevolution trainer. Runs inside a TRAIN AI match: every gameplay
-- tick it fast-forwards up to train_ticks_per_frame sim ticks (set both players'
-- inputs -> simulate_ticks(1)), scores episodes, and evolves the population.
--
-- Architecture (hybrid arbiter): the scripted heuristic drives navigation and
-- unarmed play for BOTH fighters; the genome's NN drives only fight-context
-- ticks. Fitness differences therefore concentrate on combat skill.
--
-- Curriculum: half the sparring episodes are against the pure scripted fighter,
-- half against other genomes. Every BENCH_EVERY generations the best genome
-- plays a benchmark series vs the scripted fighter; the kill-based winrate is
-- the objective quality meter, and it auto-captures difficulty checkpoints:
--   winrate >= 0.35 (once) -> ckpt easy
--   winrate >= 0.50        -> nn_ready flag (play mode starts trusting the NN)
--   winrate >= 0.55 (once) -> ckpt normal
--   best-of-gen (always)   -> ckpt hard
--
-- Anti-exploit fitness notes: kill +120 / death -60 (even fights are +EV);
-- episodes continue through kills and end on map score / 2-room push / timeout;
-- sword lose/regain cancels over throw cycles; per-tick deltas clamped so
-- respawn teleports can't be farmed.
local Trainer = {}
local d = nil

local POP_N = 32
local EPISODE_TICKS = 1800
local EPISODES_PER_GENOME = 2   -- one per side
local SAVE_TOP_N = 8            -- resumable population seeds kept in storage
local MUT = { elites = 4, mut_rate = 0.15, mut_scale = 0.25 }
local BENCH_EVERY = 10
local BENCH_EPISODES = 4

-- v2 keys: goal-mirrored feature/action semantics; older nets are incompatible.
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
-- A dying edge only counts as a kill if no map score follows within this many
-- ticks: eggnogg runs the WINNER through the dying state (you win by leaping
-- into the pit), so an immediate score means "win-fall", not a kill. Without
-- this the winner's pit-fall credits the loser +120 (it also inflated the old
-- benchmark winrates).
local PENDING_TICKS = 120

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
    bench = nil,         -- active benchmark series
    bench_winrate = tonumber(storage.get("bench_winrate", -1)) or -1,
    paused = false,
    best_fit = nil, mean_fit = nil,
    ticks_done = 0, kills_seen = 0,
    last_mask = { [0] = 0, [1] = 0 },
    goal_logged = false,
  }
end

local function new_policy(net, seed)
  return d.Policy.new(net, { react_delay = 0, epsilon = 0.05, seed = seed })
end

local function begin_episode(s)
  if s.base_blob then mod.game.apply_full_state_blob(s.base_blob) end
  d.Bot.reset_scaffold()
  local gp, pa, pb
  if s.bench then
    gp = (s.bench.done % 2 == 0) and 0 or 1
    pa = new_policy(s.pop.nets[1], 4242 + s.bench.done)   -- elite (copy of last gen's best)
    pb = nil                                              -- scripted fighter
  else
    gp = (s.ep_num % 2 == 1) and 0 or 1
    pa = new_policy(s.pop.nets[s.pair_i], s.pair_i * 31 + s.pop.gen * 7 + s.ep_num)
    if s.rand() < 0.5 then
      pb = nil                                            -- curriculum: scripted fighter
    else
      local oi = 1 + math.floor(s.rand() * #s.pop.nets)
      if oi > #s.pop.nets then oi = #s.pop.nets end
      if oi == s.pair_i then oi = (oi % #s.pop.nets) + 1 end
      pb = new_policy(s.pop.nets[oi], oi * 37 + s.pop.gen * 11 + s.ep_num)
    end
  end
  s.ep = {
    gp = gp,
    goal = (gp == 0) and 1 or -1,
    a = pa, b = pb,
    fit = 0,
    tick = 0,
    kills_me = 0, kills_op = 0,
    pending = {},   -- deferred kill/death edges (see PENDING_TICKS)
    was_dying = { me = false, enemy = false },
    had_sword = { me = nil, enemy = nil },
    start_room = nil, prev_x = nil, prev_dist = nil,
    start_score_me = nil, start_score_enemy = nil,
  }
end

-- commit deferred edges not followed by a map score (= real kills)
local function ep_commit_pending(s, e, force)
  local keep = {}
  for _, ev in ipairs(e.pending) do
    if force or (e.tick - ev.t) >= PENDING_TICKS then
      if ev.who == 'en' then
        e.fit = e.fit + R.KILL
        e.kills_me = e.kills_me + 1
        s.kills_seen = s.kills_seen + 1
      else
        e.fit = e.fit + R.DEATH
        e.kills_op = e.kills_op + 1
      end
    else
      keep[#keep + 1] = ev
    end
  end
  e.pending = keep
end

local function is_dying(state_id) return state_id == 8 or state_id == 9 end

-- One sim tick of the current episode. Returns true when the episode ended.
local function episode_step(s)
  local e = s.ep
  local gp = e.gp
  local mask_me = d.Bot.decide_mask(gp, e.a)
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

  -- kills / deaths: buffer the edges; they only count as kills if no map
  -- score follows (the winner's pit-fall also looks like dying)
  local me_dying, en_dying = is_dying(me.state_id or 0), is_dying(en.state_id or 0)
  if en_dying and not e.was_dying.enemy then
    e.pending[#e.pending + 1] = { who = 'en', t = e.tick }
    if not s.goal_logged then
      s.goal_logged = true
      mod.log(string.format("trainer: first kill sample gp=%d me.x=%.0f enemy.x=%.0f leader=%d",
                            gp, me.x, en.x, ns.leader or -1))
    end
  end
  if me_dying and not e.was_dying.me then
    e.pending[#e.pending + 1] = { who = 'me', t = e.tick }
  end
  e.was_dying.me, e.was_dying.enemy = me_dying, en_dying
  ep_commit_pending(s, e, false)

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
    e.pending = {}   -- buffered dying edge was the win-fall, not a kill
    e.fit = e.fit + R.SCORE_WIN
    ended = true
  elseif e.start_score_enemy and score_en > e.start_score_enemy then
    e.pending = {}
    e.fit = e.fit + R.SCORE_LOSS
    ended = true
  end
  local room_prog = (room_now - (e.start_room or room_now)) * e.goal
  if math.abs(room_now - (e.start_room or room_now)) >= 2 then ended = true end
  if e.tick >= EPISODE_TICKS then ended = true end
  if ended then
    ep_commit_pending(s, e, true)
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
  local dump = {}
  for i = 1, math.min(SAVE_TOP_N, #order) do
    dump[i] = d.NN.serialize(s.pop.nets[order[i]])
  end
  d.Codec.store(storage, POP_KEY, table.concat(dump, "\n"))
  storage.set(GEN_KEY, tostring(s.pop.gen))
  storage.save()
end

-- benchmark series finished: record winrate, auto-capture checkpoints
local function finish_benchmark(s)
  local b = s.bench
  local total = b.kills_me + b.kills_op
  local wr = 0.5
  if total > 0 then wr = b.kills_me / total end
  s.bench_winrate = wr
  storage.set("bench_winrate", string.format("%.3f", wr))
  mod.log(string.format("trainer: benchmark gen %d  winrate vs scripted %.2f (%d-%d)",
                        s.pop.gen, wr, b.kills_me, b.kills_op))
  local elite = d.NN.serialize(s.pop.nets[1])
  if wr >= 0.35 and storage.get("ckpt_easy_done", "0") ~= "1" then
    d.Codec.store(storage, Trainer.CKPT_KEYS.easy, elite)
    storage.set("ckpt_easy_done", "1")
    mod.log("trainer: EASY checkpoint captured")
  end
  if wr >= 0.50 and storage.get("nn_ready", "0") ~= "1" then
    storage.set("nn_ready", "1")
    mod.log("trainer: NN combat brain now beats the scripted fighter - marked ready for play")
  end
  if wr >= 0.55 and storage.get("ckpt_normal_done", "0") ~= "1" then
    d.Codec.store(storage, Trainer.CKPT_KEYS.normal, elite)
    storage.set("ckpt_normal_done", "1")
    mod.log("trainer: NORMAL checkpoint captured")
  end
  storage.save()
  s.bench = nil
end

local function finish_episode(s)
  local e = s.ep
  if s.bench then
    s.bench.kills_me = s.bench.kills_me + e.kills_me
    s.bench.kills_op = s.bench.kills_op + e.kills_op
    s.bench.done = s.bench.done + 1
    if s.bench.done >= BENCH_EPISODES then finish_benchmark(s) end
    begin_episode(s)
    return
  end
  s.fitness[s.pair_i] = (s.fitness[s.pair_i] or 0) + e.fit
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
    if s.pop.gen % BENCH_EVERY == 0 then
      s.bench = { done = 0, kills_me = 0, kills_op = 0 }
    end
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
  mod.ui.rect(20, 20, 344, 164, { color = { 0.03, 0.04, 0.06, 0.85 } })
  mod.ui.text_at("TRAIN AI  gen " .. tostring(st.pop.gen), 32, 34, 1.0, 1.0, 0.8, 0.6)
  local what
  if st.bench then
    what = "BENCHMARK " .. (st.bench.done + 1) .. "/" .. BENCH_EPISODES
  else
    what = string.format("genome %d/%d side %d", st.pair_i, #st.pop.nets, st.ep and st.ep.gp or 0)
  end
  mod.ui.text_at(what .. "  ep tick " .. tostring(st.ep and st.ep.tick or 0), 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("best %.1f  mean %.1f", st.best_fit or 0, st.mean_fit or 0),
                 32, 74, 0.9, 0.9, 0.9, 0.9)
  local wrtxt = "not yet measured"
  if st.bench_winrate and st.bench_winrate >= 0 then
    wrtxt = string.format("%.0f%%%s", st.bench_winrate * 100,
                          (storage.get("nn_ready", "0") == "1") and "  (NN live in play)" or "")
  end
  mod.ui.text_at("vs scripted: " .. wrtxt, 32, 92, 0.9, 0.6, 1.0, 0.7)
  mod.ui.text_at(string.format("sim ticks %d   kills %d", st.ticks_done, st.kills_seen),
                 32, 110, 0.9, 0.7, 0.7, 0.7)
  if mod.ui.button_at("train_pause", st.paused and "RESUME" or "PAUSE", 32, 128, 90, 26) then
    st.paused = not st.paused
  end
  if mod.ui.button_at("train_save", "SAVE", 130, 128, 70, 26) then
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
