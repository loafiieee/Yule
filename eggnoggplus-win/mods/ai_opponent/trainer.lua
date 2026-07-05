-- Self-play trainer ("TRAIN AI"): champion-vs-challenger duels, fast-forwarded.
--
-- Scheme ((1+1)-style evolution, per user design): keep ONE champion net. Each
-- match, a challenger = mutated copy of the champion fights it in a FULL match
-- (played to an actual map win, like a real game). The winner survives as the
-- new champion and becomes the template for the next mutation. After every
-- match the trainer hops to a random map from the training pool (banned_maps
-- config excludes poor teachers like the eggnog points map).
--
-- Scoring per side = the four unambiguous combat-ledger events (native
-- player_die detour; goal dives pre-classified, never deaths):
--   kill +120   death -60   point scored +300   point conceded -100
-- PLUS continuous potential-based shaping (lib/reward.lua): fighters are
-- rewarded every tick for progress toward winning - territory gained, swords
-- held, leads taken, closing on the enemy when a kill is needed. Camping
-- earns exactly zero while anything that progresses pulls ahead, so selection
-- always has a gradient (the fix for corner-camping lineages). The bot-side
-- engagement scaffold additionally hands control to the scripted fighter
-- whenever the NN refuses to fight, so standoffs cannot stall a match.
-- Matches end ONLY when a player actually wins the map; a 10-minute failsafe
-- logs a warning and decides on score (it should never fire).
--
-- Every BENCH_EVERY matches the champion plays the scripted fighter; the
-- kill+point winrate is the quality meter and auto-captures checkpoints:
--   >= 0.35 (once) easy | >= 0.50 nn_ready | >= 0.55 (once) normal
-- The hard checkpoint is always the current champion.
local Trainer = {}
local d = nil

local FAILSAFE_TICKS = 36000    -- 10 minutes of game time; should never fire
local BENCH_EVERY = 20          -- duels between benchmark series
local BENCH_MATCHES = 2
local MUT_RATE, MUT_SCALE = 0.15, 0.20
local HEAVY_CHANCE, HEAVY_RATE, HEAVY_SCALE = 0.10, 0.40, 0.45
local RECENT_CHAMPS = 8         -- seeds kept for TRAIN VS ME to build on

local POP_KEY = "trainer_pop2"  -- shared with htrainer.lua
Trainer.CKPT_KEYS = { easy = "ckpt2_easy", normal = "ckpt2_normal", hard = "ckpt2_hard" }

local R = { KILL = 120, DEATH = -60, SCORE = 300, OPP_SCORE = -100 }

local st = nil

function Trainer.init(deps) d = deps end

local function load_champion()
  local dump = d.Codec.load(storage, POP_KEY)
  if dump then
    for chunk in dump:gmatch("([^\n]+)") do
      local net = d.NN.deserialize(chunk)
      if net then
        mod.log("trainer: champion resumed from storage")
        return net
      end
    end
  end
  mod.log("trainer: fresh random champion")
  return d.NN.new(d.SIZES, os.time() % 100000)
end

local function fresh_session()
  local champ = load_champion()
  return {
    champ = champ,
    champ_age = 0,                 -- matches survived
    recent = { d.NN.serialize(champ) },
    rand = d.NN.rng_new(os.time() % 100000 + 17),
    matches = tonumber(storage.get("duel_matches", 0)) or 0,
    ch_wins = 0,                   -- challenger takeovers this session
    bench = nil,
    bench_winrate = tonumber(storage.get("bench_winrate", -1)) or -1,
    paused = false,
    ticks_done = 0,
    last_mask = { [0] = 0, [1] = 0 },
    match = nil,
  }
end

local function save_progress(s)
  d.Codec.store(storage, Trainer.CKPT_KEYS.hard, d.NN.serialize(s.champ))
  d.Codec.store(storage, POP_KEY, table.concat(s.recent, "\n"))
  storage.set("duel_matches", tostring(s.matches))
  storage.save()
end

local function remember_champion(s)
  table.insert(s.recent, 1, d.NN.serialize(s.champ))
  while #s.recent > RECENT_CHAMPS do table.remove(s.recent) end
end

local function mutate_challenger(s)
  if s.rand() < HEAVY_CHANCE then
    return d.NN.mutate(s.champ, HEAVY_RATE, HEAVY_SCALE, s.rand)
  end
  return d.NN.mutate(s.champ, MUT_RATE, MUT_SCALE, s.rand)
end

-- begin one match. kind = 'duel' (challenger vs champion) or 'bench'
-- (champion vs the scripted fighter).
local function begin_match(s, kind)
  local led = mod.game.combat_ledger()
  local map = mod.game.map_size()
  local m = {
    kind = kind,
    t = 0,
    led = led,
    mapw = map and map.w or nil,
    score = { [0] = 0, [1] = 0 },
    pot = { [0] = nil, [1] = nil },   -- shaping baselines (nil = rebaseline)
    pol = {},
  }
  if kind == 'bench' then
    m.side = s.bench.n % 2                       -- champion's side
    m.pol[m.side] = d.Policy.new(s.champ, { react_delay = 0, epsilon = 0, seed = 900 + s.bench.n })
    m.pol[1 - m.side] = nil                      -- scripted fighter
  else
    s.challenger = mutate_challenger(s)
    m.side = s.matches % 2                       -- challenger's side
    m.pol[m.side] = d.Policy.new(s.challenger, { react_delay = 0, epsilon = 0.03, seed = s.matches * 7 + 1 })
    m.pol[1 - m.side] = d.Policy.new(s.champ, { react_delay = 0, epsilon = 0.03, seed = s.matches * 7 + 2 })
  end
  d.Bot.reset_scaffold()
  s.match = m
end

-- one fast-forwarded sim tick; returns winner player index, -1 for a
-- score-decided timeout, or nil while the match continues
local function match_step(s)
  local m = s.match
  local mask0 = d.Bot.decide_mask(0, m.pol[0])
  local mask1 = d.Bot.decide_mask(1, m.pol[1])
  if not mask0 or not mask1 then return -1 end
  s.last_mask[0], s.last_mask[1] = mask0, mask1
  mod.game.set_input(0, mask0, 1, true)
  mod.game.set_input(1, mask1, 1, true)
  mod.game.simulate_ticks(1)
  m.t = m.t + 1

  local led = mod.game.combat_ledger()
  local snap = mod.game.snapshot(0, false)
  if not (snap and snap.in_game and snap.player and snap.enemy) then return -1 end

  -- MATCH END FIRST: the winning dive briefly registers as a death (the
  -- engine sets scores/countdown outside player_die), so on the end tick all
  -- ledger deltas are dive artifacts and must be skipped, not scored
  local ledger_end = led.match_ends > m.led.match_ends
  if ledger_end or (snap.end_countdown or 0) > 0 then
    local winner
    if ledger_end and led.last_winner and led.last_winner >= 0 then
      winner = led.last_winner
    else
      winner = snap.leader_index
    end
    m.led = led
    if winner == nil or winner < 0 then
      winner = (m.score[0] >= m.score[1]) and 0 or 1
    end
    return winner
  end

  -- ledger deltas -> per-side scores
  local d0 = led.deaths0 - m.led.deaths0
  local d1 = led.deaths1 - m.led.deaths1
  local s0 = led.scores0 - m.led.scores0
  local s1 = led.scores1 - m.led.scores1
  if d0 > 0 then m.score[0] = m.score[0] + R.DEATH * d0; m.score[1] = m.score[1] + R.KILL * d0 end
  if d1 > 0 then m.score[1] = m.score[1] + R.DEATH * d1; m.score[0] = m.score[0] + R.KILL * d1 end
  if s0 > 0 then m.score[0] = m.score[0] + R.SCORE * s0; m.score[1] = m.score[1] + R.OPP_SCORE * s0 end
  if s1 > 0 then m.score[1] = m.score[1] + R.SCORE * s1; m.score[0] = m.score[0] + R.OPP_SCORE * s1 end
  m.led = led

  -- potential-based shaping: reward per-tick progress toward winning.
  -- Goals point at the NEAREST map end (either eggnog pool wins).
  do
    local lead = snap.leader_index
    local dist = math.abs(snap.enemy.x - snap.player.x)
    local mid = (m.mapw and m.mapw > 1) and (m.mapw * 0.5) or nil
    local function goal_of(x, pi)
      if mid then return (x < mid) and -1 or 1 end
      return (pi == 0) and 1 or -1
    end
    local obs = {
      [0] = { x = snap.player.x, goal = goal_of(snap.player.x, 0),
              has_sword = snap.player.has_sword and true or false,
              enemy_has_sword = snap.enemy.has_sword and true or false,
              is_leader = (lead == 0), dist = dist },
      [1] = { x = snap.enemy.x, goal = goal_of(snap.enemy.x, 1),
              has_sword = snap.enemy.has_sword and true or false,
              enemy_has_sword = snap.player.has_sword and true or false,
              is_leader = (lead == 1), dist = dist },
    }
    local died = { [0] = d0 > 0, [1] = d1 > 0 }
    local dove = { [0] = s0 > 0, [1] = s1 > 0 }
    for pi = 0, 1 do
      if died[pi] or dove[pi] or (m.pot[pi] and m.pot[pi].goal ~= obs[pi].goal) then
        m.pot[pi] = nil   -- teleport or goal flip: rebaseline, no shaped reward
      else
        m.score[pi] = m.score[pi] + d.RW.delta(m.pot[pi], obs[pi])
        m.pot[pi] = obs[pi]
      end
    end
  end
  -- matches end only when someone actually wins; the failsafe should never
  -- fire now that the engagement scaffold makes standoffs impossible
  if m.t >= FAILSAFE_TICKS then
    mod.log("trainer: WARNING - duel hit the 10-minute failsafe, deciding on score")
    if m.score[0] == m.score[1] then return -1 end
    return (m.score[0] > m.score[1]) and 0 or 1
  end
  return nil
end

local function hop_map(s)
  mod.game.start_match(d.pick_map(s.rand))
end

local function finish_benchmark(s)
  local b = s.bench
  local total = b.my + b.op
  local wr = 0.5
  if total > 0 then wr = b.my / total end
  s.bench_winrate = wr
  storage.set("bench_winrate", string.format("%.3f", wr))
  mod.log(string.format("trainer: benchmark after %d duels  winrate vs scripted %.2f (%d-%d)",
                        s.matches, wr, b.my, b.op))
  local champ = d.NN.serialize(s.champ)
  if wr >= 0.35 and storage.get("ckpt_easy_done", "0") ~= "1" then
    d.Codec.store(storage, Trainer.CKPT_KEYS.easy, champ)
    storage.set("ckpt_easy_done", "1")
    mod.log("trainer: EASY checkpoint captured")
  end
  if wr >= 0.50 and storage.get("nn_ready", "0") ~= "1" then
    storage.set("nn_ready", "1")
    mod.log("trainer: champion beats the scripted fighter - NN marked ready for play")
  end
  if wr >= 0.55 and storage.get("ckpt_normal_done", "0") ~= "1" then
    d.Codec.store(storage, Trainer.CKPT_KEYS.normal, champ)
    storage.set("ckpt_normal_done", "1")
    mod.log("trainer: NORMAL checkpoint captured")
  end
  -- map curriculum: proficient on the current pool -> unlock the next map
  if wr >= 0.55 and config.get("curriculum", true) and d.curriculum_size then
    local stage = tonumber(storage.get("map_stage", 1)) or 1
    if stage < d.curriculum_size() then
      storage.set("map_stage", tostring(stage + 1))
      mod.log("trainer: map curriculum advanced - " .. (stage + 1) .. " map(s) unlocked")
    end
  end
  storage.save()
  s.bench = nil
end

local function finish_match(s, winner)
  local m = s.match
  if m.kind == 'bench' then
    -- kills+points decide the benchmark: positive events for each side
    local champ_side = m.side
    s.bench.my = s.bench.my + math.max(0, m.score[champ_side])
    s.bench.op = s.bench.op + math.max(0, m.score[1 - champ_side])
    s.bench.n = s.bench.n + 1
    if s.bench.n >= BENCH_MATCHES then finish_benchmark(s) end
  else
    local ch_side = m.side
    local challenger_won = (winner == ch_side)
    if winner == -1 then challenger_won = false end   -- ties keep the champion
    if challenger_won then
      s.champ = s.challenger
      s.champ_age = 0
      s.ch_wins = s.ch_wins + 1
      remember_champion(s)
    else
      s.champ_age = s.champ_age + 1
    end
    s.matches = s.matches + 1
    save_progress(s)
    if s.matches % BENCH_EVERY == 0 then
      s.bench = { n = 0, my = 0, op = 0 }
    end
  end
  hop_map(s)
  begin_match(s, s.bench and 'bench' or 'duel')
end

function Trainer.tick()
  if not st then st = fresh_session() end
  local s = st
  if s.paused then
    mod.game.set_input(0, 0, 1, true)
    mod.game.set_input(1, 0, 1, true)
    return
  end
  if not s.match then begin_match(s, 'duel') end
  local budget = tonumber(config.get("train_ticks_per_frame", 120)) or 120
  if budget < 1 then budget = 1 end
  if budget > 600 then budget = 600 end
  for _ = 1, budget do
    local winner = match_step(s)
    if winner ~= nil then finish_match(s, winner) end
    s.ticks_done = s.ticks_done + 1
  end
  -- hold the last masks through the visible native tick after on_tick returns
  mod.game.set_input(0, st.last_mask[0], 1, true)
  mod.game.set_input(1, st.last_mask[1], 1, true)
end

function Trainer.overlay()
  if not st then return end
  local s = st
  local m = s.match
  mod.ui.begin_overlay()
  mod.ui.rect(20, 20, 360, 164, { color = { 0.03, 0.04, 0.06, 0.85 } })
  mod.ui.text_at("TRAIN AI  duel " .. tostring(s.matches), 32, 34, 1.0, 1.0, 0.8, 0.6)
  local what = "duel"
  if m and m.kind == 'bench' then what = "BENCHMARK " .. (s.bench and (s.bench.n + 1) or 1) .. "/" .. BENCH_MATCHES end
  mod.ui.text_at(string.format("%s  match tick %d", what, m and m.t or 0),
                 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("champion age %d  takeovers %d  score %.0f : %.0f",
                               s.champ_age, s.ch_wins,
                               m and m.score[0] or 0, m and m.score[1] or 0),
                 32, 74, 0.9, 0.9, 0.9, 0.9)
  local wrtxt = "not yet measured"
  if s.bench_winrate and s.bench_winrate >= 0 then
    wrtxt = string.format("%.0f%%%s", s.bench_winrate * 100,
                          (storage.get("nn_ready", "0") == "1") and "  (NN live in play)" or "")
  end
  mod.ui.text_at("vs scripted: " .. wrtxt, 32, 92, 0.9, 0.6, 1.0, 0.7)
  mod.ui.text_at("sim ticks " .. tostring(s.ticks_done), 32, 110, 0.9, 0.7, 0.7, 0.7)
  if mod.ui.button_at("train_pause", s.paused and "RESUME" or "PAUSE", 32, 128, 90, 26) then
    s.paused = not s.paused
  end
  if mod.ui.button_at("train_save", "SAVE", 130, 128, 70, 26) then
    save_progress(s)
  end
  mod.ui.end_overlay()
end

function Trainer.match_ended()
  if st then save_progress(st) end
  st = nil
end

function Trainer.shutdown()
  Trainer.match_ended()
end

return Trainer
