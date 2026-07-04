-- Game-facing bot runtime: builds the sensing context from live game state and
-- drives one player. Arbiter: the scripted heuristic handles navigation, sword
-- recovery and unarmed play; the NN policy (when provided) drives fight-context
-- ticks only. Loaded with mod.dofile("bot.lua"); deps injected via init().
local Bot = { _d = nil }

function Bot.init(deps)  -- deps = { NN=, A=, F=, Policy=, H= }
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
  grid.ok = true
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

function Bot.build_ctx(ai_player)
  local snap = mod.game.snapshot(ai_player, false)
  if not snap or not snap.in_game or not snap.player or not snap.enemy then return nil end
  local p = snap.player
  local ns = mod.game.native_state()
  local dirx = (p.facing or 1) >= 0 and 1 or -1
  local px, py = p.x, p.y
  local room = p.room_index or 0
  grid_refresh(room)
  local nav_r = nav_dir(px, py, 1)
  local nav_l = nav_dir(px, py, -1)
  local nav_f = (dirx > 0) and nav_r or nav_l
  local lead = ns.leader or -1
  local leader = 0
  if lead == ai_player then leader = 1 elseif lead == (1 - ai_player) then leader = -1 end
  return {
    snap = snap,
    my_room = room,
    enemy_room = snap.enemy.room_index or 0,
    goal_dir = (ai_player == 0) and 1 or -1,   -- P0 pushes right (trainer logs verify)
    leader = leader,
    nav = { [1] = nav_r, [-1] = nav_l },
    -- probes feed the NN features (facing-relative, mirror-stable)
    probes = {
      ahead_near = solid_px(px + dirx * 12, py),
      ahead_far = solid_px(px + dirx * 28, py),
      hazard_ahead = 0,
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

function Bot.reset_scaffold()
  stuck[0].x, stuck[0].t, stuck[0].hold = 0, 0, 0
  stuck[1].x, stuck[1].t, stuck[1].hold = 0, 0, 0
  heur_mem[0], heur_mem[1] = nil, nil
  engage[0] = { min_dist = nil, t = 0, force = 0, progress = false }
  engage[1] = { min_dist = nil, t = 0, force = 0, progress = false }
  grid.room, grid.ok = -1, false
end

function Bot.last(ai_player)
  return last_info[ai_player] or {}
end

-- stuck scaffold (last line of defense): if x hasn't moved >2px in 60 ticks
-- while trying to move, HOLD jump for 10 ticks (a single-tick tap is a useless
-- micro-hop that never clears anything)
function Bot.scaffold(ai_player, ctx, mask)
  local s = stuck[ai_player]
  if s.hold > 0 then
    s.hold = s.hold - 1
    if mask % 2 == 0 then mask = mask + 1 end   -- OR in JUMP
    return mask
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
    if mask % 2 == 0 then mask = mask + 1 end
  end
  return mask
end

-- Compute this tick's command mask for ai_player without applying it.
-- policy: NN policy that drives fight-context ticks, or nil for pure heuristic.
function Bot.decide_mask(ai_player, policy)
  local d = Bot._d
  local ctx = Bot.build_ctx(ai_player)
  if not ctx then return nil, nil end
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
