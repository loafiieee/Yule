-- Game-facing bot runtime: builds the feature context from live game state and
-- drives one player via set_input. Loaded with mod.dofile("bot.lua"); deps injected.
local Bot = { _d = nil }

function Bot.init(deps)  -- deps = { NN=, A=, F=, Policy= }
  Bot._d = deps
end

-- Solid-geometry probe (world pixels). Hazard ids are not queryable per-pixel in the
-- current framework API, so hazard_ahead stays 0 for v1; deaths teach hazard
-- avoidance through the trainer's fitness signal instead.
local function solid(x, y)
  return mod.game.is_solid(x, y) and 1 or 0
end

function Bot.build_ctx(ai_player)
  local snap = mod.game.snapshot(ai_player, false)
  if not snap or not snap.in_game or not snap.player or not snap.enemy then return nil end
  local p = snap.player
  local ns = mod.game.native_state()
  local dirx = (p.facing or 1) >= 0 and 1 or -1
  local px, py = p.x, p.y
  local g_front = solid(px + dirx * 12, py + 14)
  local below_front = solid(px + dirx * 12, py + 30)
  local lead = ns.leader or -1
  local leader = 0
  if lead == ai_player then leader = 1 elseif lead == (1 - ai_player) then leader = -1 end
  return {
    snap = snap,
    my_room = p.room_index or 0,
    enemy_room = snap.enemy.room_index or 0,
    goal_dir = (ai_player == 0) and 1 or -1,   -- P0 pushes right (trainer logs verify this)
    leader = leader,
    probes = {
      ahead_near = solid(px + dirx * 12, py),
      ahead_far = solid(px + dirx * 28, py),
      hazard_ahead = 0,
      ground_front = g_front,
      gap_below = (g_front == 0 and below_front == 0) and 1 or 0,
      above = solid(px, py - 16),
    },
    tick = mod.game.tick_count(),
  }
end

-- stuck scaffold: if x hasn't moved >2px in 90 ticks while trying to move, force a jump
local stuck = { [0] = { x = 0, t = 0 }, [1] = { x = 0, t = 0 } }

function Bot.reset_scaffold()
  stuck[0].x, stuck[0].t = 0, 0
  stuck[1].x, stuck[1].t = 0, 0
end

function Bot.scaffold(ai_player, ctx, mask)
  local s = stuck[ai_player]
  local x = ctx.snap.player.x
  local moving = (mask % 16) >= 4   -- LEFT (0x08) or RIGHT (0x04) bit set
  if moving and math.abs(x - s.x) < 2 then
    s.t = s.t + 1
  else
    s.x, s.t = x, 0
  end
  if s.t > 90 then
    s.t = 0
    if mask % 2 == 0 then mask = mask + 1 end   -- OR in JUMP if not set
  end
  return mask
end

-- Compute this tick's command mask for ai_player without applying it.
function Bot.decide_mask(ai_player, policy)
  local d = Bot._d
  local ctx = Bot.build_ctx(ai_player)
  if not ctx then return nil, nil end
  local feats = d.F.extract(ctx)
  local action = d.Policy.decide(policy, feats)
  local mask = Bot.scaffold(ai_player, ctx, d.A.mask(action, ctx.goal_dir))
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
