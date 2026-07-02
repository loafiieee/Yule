-- Snapshot context -> normalized feature vector, canonicalized to the goal
-- direction: all x-axis quantities are mirrored by ctx.goal_dir so "positive x"
-- always means "toward my goal". One net therefore plays either side.
-- ctx = { snap, my_room, enemy_room, goal_dir, leader, probes = {...} }
local F = {}
F.N_INPUTS = 28

local function clamp(v, lo, hi)
  if v == nil or v ~= v then return 0 end -- nil/NaN guard
  if v < lo then return lo elseif v > hi then return hi end
  return v
end
local function b(x) return x and 1 or 0 end

function F.extract(ctx)
  local p, e = ctx.snap.player, ctx.snap.enemy
  local g = (ctx.goal_dir or 1) >= 0 and 1 or -1
  local o = {}
  -- relative opponent, goal-mirrored (1-4)
  o[1] = clamp(((e.x - p.x) * g) / 200, -1, 1)
  o[2] = clamp((e.y - p.y) / 100, -1, 1)
  o[3] = clamp(((e.vx or 0) * g) / 6, -1, 1)
  o[4] = clamp((e.vy or 0) / 6, -1, 1)
  -- self motion (5-6)
  o[5] = clamp(((p.vx or 0) * g) / 6, -1, 1)
  o[6] = clamp((p.vy or 0) / 6, -1, 1)
  -- facing relative to goal / swords (7-10): +1 = facing my goal
  o[7]  = clamp((p.facing or 1) * g, -1, 1)
  o[8]  = clamp((e.facing or 1) * g, -1, 1)
  o[9]  = b(p.has_sword)
  o[10] = b(e.has_sword)
  -- contact flags (11-14): walls mirrored to back/forward
  -- (explicit branch: the and/or ternary idiom is wrong for false booleans)
  o[11] = b(p.grounded)
  o[12] = b(e.grounded)
  if g > 0 then
    o[13] = b(p.wall_left)   -- back
    o[14] = b(p.wall_right)  -- forward
  else
    o[13] = b(p.wall_right)
    o[14] = b(p.wall_left)
  end
  -- state (15-18): normalized ids + death flags (dying=8, dead-ish=9)
  local ps, es = p.state_id or 0, e.state_id or 0
  o[15] = clamp(ps / 16, 0, 1)
  o[16] = clamp(es / 16, 0, 1)
  o[17] = b(ps == 8 or ps == 9)
  o[18] = b(es == 8 or es == 9)
  -- enemy attack button (19): ATTACK bit 0x02 without bit ops
  o[19] = b(((e.cmd_bits or 0) % 4) >= 2)
  -- range + strategy (20-22)
  o[20] = clamp(math.abs(e.x - p.x) / 200, 0, 1)
  o[21] = clamp(ctx.leader or 0, -1, 1)
  o[22] = clamp((((ctx.my_room or 0) - (ctx.enemy_room or 0)) * g) / 3, -1, 1)
  -- nearest loose sword, goal-mirrored (23-25)
  local sx, sy = ctx.snap.nearest_sword_x, ctx.snap.nearest_sword_y
  if sx and sy then
    o[23] = 1
    o[24] = clamp(((sx - p.x) * g) / 200, -1, 1)
    o[25] = clamp((sy - p.y) / 100, -1, 1)
  else
    o[23], o[24], o[25] = 0, 0, 0
  end
  -- tile probes (26-28): facing-relative, so already side-agnostic given o[7]
  local pr = ctx.probes or {}
  o[26] = clamp((pr.ahead_near or 0) + 0.5 * (pr.ahead_far or 0) - 0.001, -1, 1)
  o[27] = clamp((pr.hazard_ahead or 0) + 0.5 * (pr.gap_below or 0), -1, 1)
  o[28] = clamp((pr.ground_front or 0) - (pr.above or 0), -1, 1)
  return o
end

return F
