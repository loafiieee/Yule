-- Game-facing bot runtime: builds the sensing context from live game state and
-- drives one player. Arbiter: the scripted heuristic handles navigation, sword
-- recovery and unarmed play; the NN policy (when provided) drives fight-context
-- ticks only. Loaded with mod.dofile("bot.lua"); deps injected via init().
local Bot = { _d = nil }

function Bot.init(deps)  -- deps = { NN=, A=, F=, Policy=, H=, P= }
  Bot._d = deps
end

local function solid_px(x, y)
  return mod.game.is_solid(x, y) and 1 or 0
end

-- ------------------------------------------------- room tile grid (cached) ---

local grid = { room = -1, ok = false }
local solid_id = {}   -- tile id -> bool (engine props table via mod.game.tile_solid)

local function tile_is_solid(id)
  if id == nil or id < 0 then return false end
  local v = solid_id[id]
  if v == nil then
    v = mod.game.tile_solid(id) and true or false
    solid_id[id] = v
  end
  return v
end

local LETHAL_IDS = { [5] = true }            -- spikes: instant death on contact/landing
local MINE_ID = 6                            -- mines: SAFE to step on; ~1s fuse then a blast
local POOL_IDS = { [9] = true, [10] = true } -- eggnog goal pools: touching wins

-- Mine fuse tracking (position-based, no engine timer needed): the moment a
-- player's cell becomes a mine cell we mark that mine LIVE and count ticks. It
-- is safe until FUSE_TICKS, then a blast for BLAST_TICKS within BLAST_R cells.
-- This is what makes the AI fuse-aware: cross/step freely, but be gone from the
-- blast before it goes off - and use it as a trap (bait the enemy across it).
local FUSE_TICKS  = 55    -- ~1s fuse (user's estimate; tune here if needed)
local BLAST_TICKS = 12    -- lethal blast lingers a moment after detonation
local BLAST_R     = 1     -- blast reaches ~1 cell around the mine
local armed = {}          -- "room:c:r" -> { t=arm_tick, by=player_idx, room, c, r }
local g_now = 0           -- latest game tick (updated each build_ctx)

local function grid_refresh(room)
  if grid.room == room and grid.ok then return end
  grid.room = room
  grid.ok = false
  local rt = mod.game.room_tiles(room)
  if not rt or not rt.ids or not rt.w or rt.w < 2 or not rt.h or rt.h < 2 then return end
  if not rt.tile_w or rt.tile_w <= 0 or not rt.tile_h or rt.tile_h <= 0 then return end
  grid.w, grid.h, grid.ids = rt.w, rt.h, rt.ids
  grid.tile_w, grid.tile_h = rt.tile_w, rt.tile_h
  grid.origin_x, grid.origin_y = rt.origin_x or 0, rt.origin_y or 0
  -- goal pools in this room (dive target only when on OUR side - see goal_pool)
  grid.pools = {}
  for r = 1, grid.h do
    for c = 1, grid.w do
      local id = grid.ids[(r - 1) * grid.w + c]
      if id and POOL_IDS[id] then
        grid.pools[#grid.pools + 1] = { c = c, r = r }
      end
    end
  end
  grid.ok = true
end

-- spikes etc: landing/standing there kills instantly, so never a graph node
local function lethal_cell(c, r)
  if not grid.ok then return false end
  if c < 1 or c > grid.w or r < 1 or r > grid.h then return false end
  local id = grid.ids[(r - 1) * grid.w + c]
  return (id and LETHAL_IDS[id]) and true or false
end

-- mines: safe to step on (a fuse starts, ~1s, then it blows). Passable but
-- costly - cross quickly, don't linger, prefer to jump over when cheap.
local function mine_cell(c, r)
  if not grid.ok then return false end
  if c < 1 or c > grid.w or r < 1 or r > grid.h then return false end
  return grid.ids[(r - 1) * grid.w + c] == MINE_ID
end

-- col/row are 1-based; cells outside the room count as open (the row below the
-- map is the death pit - the gap probe is what reports that danger)
local function solid_cell(col, row)
  if not grid.ok then return false end
  if col < 1 or col > grid.w or row < 1 or row > grid.h then return false end
  return tile_is_solid(grid.ids[(row - 1) * grid.w + col])
end

local function world_to_cell(x, y)
  local col = math.floor((x - grid.origin_x) / grid.tile_w) + 1
  local row = math.floor((y - grid.origin_y) / grid.tile_h) + 1
  return col, row
end

-- lethal tile (spike/mine) in the next cell or two ahead at foot level? feeds
-- the NN's hazard_ahead probe (which was hardcoded 0 until now) and warns the
-- scripted layer off walking into it.
local function hazard_ahead_of(px, py, dir)
  if not grid.ok then return 0 end
  local col, row = world_to_cell(px, py)
  for step = 1, 2 do
    local c = col + dir * step
    if lethal_cell(c, row) or lethal_cell(c, row + 1)
       or mine_cell(c, row) or mine_cell(c, row + 1) then
      return 1   -- caution: spikes are lethal, mines are a timed risk
    end
  end
  return 0
end

-- a mine (id 6) in our OWN cell or directly under our feet: we are on it. That
-- arms it (a fuse starts), so bail before it blows.
local function on_mine(px, py)
  if not grid.ok then return false end
  local col, row = world_to_cell(px, py)
  for dr = 0, 1 do
    local r = row + dr
    if r >= 1 and r <= grid.h and grid.ids[(r - 1) * grid.w + col] == MINE_ID then
      return true
    end
  end
  return false
end

-- a player standing on a mine cell arms it: record the LIVE mine (once) with
-- the tick and who armed it, so we know when it will blow and can bait with it.
local function arm_mines_at(room, px, py, by)
  if not grid.ok then return end
  local col, row = world_to_cell(px, py)
  for dr = 0, 1 do
    local r = row + dr
    if r >= 1 and r <= grid.h and grid.ids[(r - 1) * grid.w + col] == MINE_ID then
      local k = room .. ":" .. col .. ":" .. r
      if not armed[k] then armed[k] = { t = g_now, by = by, room = room, c = col, r = r } end
    end
  end
end

local function prune_armed()
  for k, m in pairs(armed) do
    if g_now - m.t > FUSE_TICKS + BLAST_TICKS then armed[k] = nil end
  end
end

-- nearest LIVE armed mine whose blast still threatens cell (c,r) in this room,
-- within `reach` cells; returns the mine record or nil
local function armed_threat(room, c, r, reach)
  local best, bestd
  for _, m in pairs(armed) do
    if m.room == room then
      local age = g_now - m.t
      if age >= 0 and age <= FUSE_TICKS + BLAST_TICKS then
        local d = math.abs(m.c - c) + math.abs(m.r - r)
        if d <= (reach or (BLAST_R + 2)) and (not best or d < bestd) then
          best, bestd = m, d
        end
      end
    end
  end
  return best
end

-- for the pathfinder: is cell (c,r) inside a live armed mine's blast right now?
local function armed_blast_cell(c, r)
  if not grid.ok then return false end
  return armed_threat(grid.room, c, r, BLAST_R) ~= nil
end

-- nearest goal pool cell in the current room, or nil
local function nearest_pool(px, py)
  if not grid.ok or not grid.pools or #grid.pools == 0 then return nil end
  local sc, sr = world_to_cell(px, py)
  local best, best_d
  for _, pool in ipairs(grid.pools) do
    local dd = math.abs(pool.c - sc) + math.abs(pool.r - sr)
    if not best or dd < best_d then best, best_d = pool, dd end
  end
  return best
end

-- nav senses for one screen direction: wall = solid ahead at any body height;
-- gap = no ground below the next columns. Pixel probes at multiple heights are
-- ALWAYS merged in (the grid geometry derivation can be wrong on some maps,
-- and missing a wall means never jumping it).
local function nav_dir(px, py, dir)
  local wall = solid_px(px + dir * 14, py - 6) == 1 or
               solid_px(px + dir * 14, py + 8) == 1 or
               solid_px(px + dir * 28, py) == 1
  local gap = solid_px(px + dir * 16, py + 18) == 0 and
              solid_px(px + dir * 16, py + 34) == 0
  if grid.ok then
    local col, row = world_to_cell(px, py)
    for step = 1, 2 do
      if solid_cell(col + dir * step, row) or solid_cell(col + dir * step, row - 1) then
        wall = true
        break
      end
    end
    local ggap = true
    for step = 1, 2 do
      local ground = false
      for below = 1, 4 do
        if solid_cell(col + dir * step, row + below) then ground = true break end
      end
      if ground then ggap = false break end
    end
    gap = gap or ggap
  end
  return { wall = wall, gap = gap }
end

-- ----------------------------------------------------------------- context ---

-- Goal sides are FIXED by the engine, not positional: player_new writes
-- (index & 1) * -2 + 1 to player+0x9C, so P0 always pushes RIGHT and P1
-- always pushes LEFT, for the whole match. Room transitions enforce it
-- (only the _leader may cross a room edge, and only in their 0x9C
-- direction - anyone else gets their position reverted). Running toward
-- the OTHER end is never progress; the old nearest-end heuristic made
-- defenders sprint at the enemy's goal.
local function goal_dir_of(ai_player)
  return (ai_player == 0) and 1 or -1
end

-- nearest pool that actually lies in OUR goal direction (a pool at the
-- enemy's end is their target, not ours - never dive for it)
local function goal_pool(ai_player, px, py)
  local pool = nearest_pool(px, py)
  if not (pool and grid.ok) then return nil end
  local poolx = grid.origin_x + (pool.c - 0.5) * grid.tile_w
  if (poolx - px) * goal_dir_of(ai_player) < 0 then return nil end
  return pool
end

function Bot.build_ctx(ai_player)
  local snap = mod.game.snapshot(ai_player, false)
  if not snap or not snap.in_game or not snap.player or not snap.enemy then return nil end
  local p, en = snap.player, snap.enemy
  local ns = mod.game.native_state()
  local dirx = (p.facing or 1) >= 0 and 1 or -1
  local px, py = p.x, p.y
  local room = p.room_index or 0
  grid_refresh(room)
  -- fuse bookkeeping: advance the clock, arm any mine a player is standing on,
  -- forget mines that have already blown
  g_now = mod.game.tick_count() or (g_now + 1)
  arm_mines_at(room, px, py, ai_player)
  if (en.room_index or room) == room then arm_mines_at(room, en.x, en.y, 1 - ai_player) end
  prune_armed()
  local nav_r = nav_dir(px, py, 1)
  local nav_l = nav_dir(px, py, -1)
  local nav_f = (dirx > 0) and nav_r or nav_l
  local lead = ns.leader or -1
  local leader = 0
  if lead == ai_player then leader = 1 elseif lead == (1 - ai_player) then leader = -1 end
  -- fuse-aware mine picture for the heuristic
  local avoid_dir, threat_by = 0, nil
  if grid.ok then
    local mcol, mrow = world_to_cell(px, py)
    local threat = armed_threat(room, mcol, mrow, BLAST_R + 3)
    if threat then
      threat_by = threat.by
      if threat.c > mcol then avoid_dir = 1 elseif threat.c < mcol then avoid_dir = -1 end
    end
  end
  return {
    snap = snap,
    my_room = room,
    enemy_room = snap.enemy.room_index or 0,
    goal_dir = goal_dir_of(ai_player),
    leader = leader,
    nav = { [1] = nav_r, [-1] = nav_l },
    -- mine picture: on = standing on one (arm+bail); avoid_dir = screen dir of a
    -- LIVE armed blast to stay out of; by_me = we armed it (bait opportunity)
    mine = { on = on_mine(px, py), avoid_dir = avoid_dir,
             by_me = (threat_by == ai_player) },
    -- probes feed the NN features (facing-relative, mirror-stable)
    probes = {
      ahead_near = solid_px(px + dirx * 12, py),
      ahead_far = solid_px(px + dirx * 28, py),
      hazard_ahead = hazard_ahead_of(px, py, dirx),
      ground_front = solid_px(px + dirx * 12, py + 14),
      gap_below = (nav_f.gap and 1 or 0),
      above = solid_px(px, py - 16),
    },
    tick = mod.game.tick_count(),
  }
end

-- ------------------------------------------------------- per-player state ---

local stuck = { [0] = { x = 0, t = 0, hold = 0 }, [1] = { x = 0, t = 0, hold = 0 } }
local heur_mem = { [0] = nil, [1] = nil }
local last_info = { [0] = {}, [1] = {} }
-- engagement scaffold state: the NN decides HOW to fight, never WHETHER.
-- If it makes no fight progress (no closing, no attacks) for ENGAGE_PATIENCE
-- ticks, the scripted fighter takes the stick for ENGAGE_FORCE ticks.
local ENGAGE_PATIENCE, ENGAGE_FORCE = 30, 90
local engage = { [0] = { min_dist = nil, t = 0, force = 0, progress = false },
                 [1] = { min_dist = nil, t = 0, force = 0, progress = false } }

local plan = { [0] = nil, [1] = nil }   -- cached routes (pathfinder output)
-- fight-or-run intent state (open-lane runs with a stall fallback)
local intent_st = { [0] = { run_x = nil, stall = 0, hunt_until = 0 },
                    [1] = { run_x = nil, stall = 0, hunt_until = 0 } }

function Bot.reset_scaffold()
  stuck[0].x, stuck[0].t, stuck[0].hold = 0, 0, 0
  stuck[1].x, stuck[1].t, stuck[1].hold = 0, 0, 0
  heur_mem[0], heur_mem[1] = nil, nil
  engage[0] = { min_dist = nil, t = 0, force = 0, progress = false }
  engage[1] = { min_dist = nil, t = 0, force = 0, progress = false }
  intent_st[0] = { run_x = nil, stall = 0, hunt_until = 0 }
  intent_st[1] = { run_x = nil, stall = 0, hunt_until = 0 }
  plan[0], plan[1] = nil, nil
  armed = {}                       -- forget live mines from the previous match
  grid.room, grid.ok = -1, false
end

-- ------------------------------------------------------- route planning ---
-- All long-distance movement follows an explicit path over the tile grid
-- (lib/path.lua): jumps and drops are plan steps, not probe guesses.

local function grid_obj()
  return { w = grid.w, h = grid.h,
           solid = function(c, r) return solid_cell(c, r) end,
           -- spikes AND live armed-mine blast cells are no-go (routes around);
           -- an idle mine is merely a penalized crossing.
           hazard = function(c, r) return lethal_cell(c, r) or armed_blast_cell(c, r) end,
           softhazard = function(c, r) return mine_cell(c, r) and not armed_blast_cell(c, r) end }
end

local function is_dying_state(sid) return sid == 8 or sid == 9 end

-- Fight or run? RUN only when it can actually win: with the go (the leader
-- advances the active room), with a dead enemy, or when OUR goal pool is in
-- this room (dive for it - even as non-leader, touching our own nog wins).
-- A non-leader cannot advance the room, so without those the only path to
-- the goal is a kill: HUNT and hold ground - every screen the enemy takes
-- is ours to defend. Stalled runs also fall back to hunting.
local function decide_intent(ai_player, ctx)
  local st = intent_st[ai_player]
  local me, en = ctx.snap.player, ctx.snap.enemy
  local en_alive = not is_dying_state(en.state_id or 0)
  local pool = goal_pool(ai_player, me.x, me.y)
  -- WARP: the enemy has the go and has advanced into a room we cannot follow
  -- into (only the leader crosses room edges). Chasing them only shrinks the
  -- catch-up gap and delays the respawn - pure chasing never works. Run our
  -- OWN goal instead to WIDEN the gap and trigger the teleport that snaps us
  -- back beside (often in front of) the leader. (Loser respawns AT leader.x.)
  if ctx.leader == -1 and en_alive and ctx.enemy_room ~= ctx.my_room then
    st.run_x, st.stall, st.hunt_until = nil, 0, 0
    return 'warp'
  end
  if not (ctx.leader == 1 or not en_alive or pool) then
    st.run_x, st.stall, st.hunt_until = nil, 0, 0
    return 'hunt'
  end
  if st.hunt_until > 0 then
    st.hunt_until = st.hunt_until - 1
    return 'hunt'
  end
  -- run, but watch for stalls (blocked step, screen edge...)
  if st.run_x == nil then
    st.run_x, st.stall = me.x, 0
  elseif math.abs(me.x - st.run_x) > 2 then
    st.run_x, st.stall = me.x, 0
  else
    st.stall = st.stall + 1
    if st.stall >= 90 then
      st.run_x, st.stall = nil, 0
      st.hunt_until = 180
      return 'hunt'
    end
  end
  return 'run'
end

local function plan_route(ai_player, ctx)
  if not grid.ok then plan[ai_player] = nil; return nil end
  local d = Bot._d
  local me, en = ctx.snap.player, ctx.snap.enemy
  local g = ctx.goal_dir
  local sc, sr = world_to_cell(me.x, me.y)

  -- movement intent mirrors the heuristic's macro logic
  local kind, gc, gr
  local sx, sy = ctx.snap.nearest_sword_x, ctx.snap.nearest_sword_y
  if ctx.intent == 'run' or ctx.intent == 'warp' then
    kind = 'goal'
    local pool = goal_pool(ai_player, me.x, me.y)
    if pool then
      gc, gr = pool.c, pool.r      -- dive target: our pool itself
    else
      gc, gr = (g > 0) and grid.w or 1, sr   -- otherwise our goal-side edge
    end
  elseif (not me.has_sword) and sx and math.abs(sx - me.x) < 240 and
         math.abs((sy or me.y) - me.y) < 90 then
    kind = 'sword'
    gc, gr = world_to_cell(sx, sy or me.y)
  elseif ctx.enemy_room ~= ctx.my_room then
    kind, gc, gr = 'enemy', (ctx.enemy_room > ctx.my_room) and grid.w or 1, sr
  else
    kind = 'enemy'
    gc, gr = world_to_cell(en.x, en.y)
  end

  local p = plan[ai_player]
  if (not p) or p.kind ~= kind or p.room ~= ctx.my_room or p.age >= 20 or
     math.abs(p.gc - gc) > 1 or math.abs(p.gr - gr) > 1 then
    p = { kind = kind, room = ctx.my_room, gc = gc, gr = gr, age = 0, idx = 1,
          path = d.P.find(grid_obj(), sc, sr, gc, gr) }
    plan[ai_player] = p
  end
  p.age = p.age + 1
  if not p.path then return nil end

  -- advance waypoints we've reached OR OVERSHOT (a jump can land past a
  -- waypoint; steering back to it caused the 1-tile-step oscillation)
  local function md(c, r, wp) return math.abs(c - wp.c) + math.abs(r - wp.r) end
  local wp = p.path[p.idx]
  while wp do
    if wp.c == sc and wp.r == sr then
      p.idx = p.idx + 1
    elseif p.path[p.idx + 1] and md(sc, sr, p.path[p.idx + 1]) < md(sc, sr, wp) then
      p.idx = p.idx + 1
    else
      break
    end
    wp = p.path[p.idx]
  end
  if not wp then return nil end
  local dir = 0
  if wp.c > sc then dir = 1 elseif wp.c < sc then dir = -1 end
  local jump = (wp.r < sr) or (wp.kind == 'jump')
  return { dir = dir,
           jump = (jump and (me.grounded and true or false)),
           climb = (wp.kind == 'climb'),
           kind = kind }
end

-- remaining waypoints in world pixels (for the path overlay)
function Bot.route_points(ai_player)
  local p = plan[ai_player]
  if not p or not p.path or not grid.ok then return nil end
  local pts = {}
  for i = math.max(1, p.idx), #p.path do
    local wp = p.path[i]
    pts[#pts + 1] = { x = grid.origin_x + (wp.c - 0.5) * grid.tile_w,
                      y = grid.origin_y + (wp.r - 0.5) * grid.tile_h }
  end
  if #pts == 0 then return nil end
  return pts
end

function Bot.last(ai_player)
  return last_info[ai_player] or {}
end

-- stuck scaffold (last line of defense): if x hasn't moved >2px in 60 ticks
-- while trying to move, HOLD jump for 10 ticks (a single-tick tap is a useless
-- micro-hop that never clears anything)
function Bot.scaffold(ai_player, ctx, mask)
  local s = stuck[ai_player]
  local function or_jump(m)   -- JUMP is bit 0x02
    if math.floor(m / 2) % 2 == 0 then return m + 2 end
    return m
  end
  if s.hold > 0 then
    s.hold = s.hold - 1
    return or_jump(mask)
  end
  local x = ctx.snap.player.x
  local moving = (mask % 16) >= 4   -- LEFT (0x08) or RIGHT (0x04) bit set
  if moving and math.abs(x - s.x) < 2 then
    s.t = s.t + 1
  else
    s.x, s.t = x, 0
  end
  if s.t > 60 then
    s.t = 0
    s.hold = 10
    mask = or_jump(mask)
  end
  return mask
end

-- Compute this tick's command mask for ai_player without applying it.
-- policy: NN policy that drives fight-context ticks, or nil for pure heuristic.
function Bot.decide_mask(ai_player, policy)
  local d = Bot._d
  local ctx = Bot.build_ctx(ai_player)
  if not ctx then return nil, nil end
  ctx.intent = decide_intent(ai_player, ctx)
  ctx.route = plan_route(ai_player, ctx)
  if not heur_mem[ai_player] then
    heur_mem[ai_player] = d.H.new_mem(ai_player * 7919 + 5)
  end
  local action, mode, brain
  if policy and d.H.is_fight(ctx) then
    local eng = engage[ai_player]
    local dist = math.abs(ctx.snap.enemy.x - ctx.snap.player.x)
    if eng.force > 0 then
      -- engagement scaffold: scripted fighter has the stick until the standoff
      -- is broken (the NN forfeited its turn by refusing to fight)
      eng.force = eng.force - 1
      action, mode = d.H.decide(ctx, heur_mem[ai_player])
      brain = 'heur-forced'
    else
      action = d.Policy.decide(policy, d.F.extract(ctx))
      mode, brain = 'fight', 'nn'
      -- fight-progress watchdog: only NEW record approaches or actual
      -- in-range attacks count as fighting - dancing in and out of an old
      -- distance earns nothing (min_dist persists across windows)
      eng.t = eng.t + 1
      if eng.min_dist == nil or dist < eng.min_dist - 8 then
        eng.min_dist = dist
        eng.progress = true
      end
      if action >= 7 and action <= 9 and dist < 56 then   -- attack family, in range
        eng.progress = true
      end
      if eng.t >= ENGAGE_PATIENCE then
        if not eng.progress then
          eng.force = ENGAGE_FORCE
        end
        eng.t = 0
        eng.progress = false
      end
    end
  else
    engage[ai_player].min_dist = nil
    engage[ai_player].t = 0
    engage[ai_player].progress = false
    action, mode = d.H.decide(ctx, heur_mem[ai_player])
    brain = 'heur'
  end
  local mask = Bot.scaffold(ai_player, ctx, d.A.mask(action, ctx.goal_dir))
  last_info[ai_player] = { mode = mode, brain = brain, action = action, mask = mask }
  return mask, ctx
end

-- Play-mode entry: decide and inject for this gameplay tick.
function Bot.drive(ai_player, policy)
  local mask, ctx = Bot.decide_mask(ai_player, policy)
  if not mask then return nil end
  mod.game.set_input(ai_player, mask, 1, true)
  return ctx
end

return Bot
