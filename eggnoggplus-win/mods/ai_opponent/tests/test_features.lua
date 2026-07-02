local F = dofile("lib/features.lua")

local function fake_ctx()
  return {
    snap = {
      player = { x=100, y=50, vx=1, vy=0, facing=1, has_sword=true, grounded=true,
                 wall_left=false, wall_right=true, state_id=0, cmd_bits=0 },
      enemy  = { x=180, y=50, vx=-1, vy=0, facing=-1, has_sword=true, grounded=true,
                 wall_left=false, wall_right=false, state_id=0, cmd_bits=2 },
      nearest_sword_x = 140, nearest_sword_y = 50,
    },
    my_room = 2, enemy_room = 2, goal_dir = 1, leader = -1,
    probes = { ahead_near=0, ahead_far=1, hazard_ahead=0, ground_front=1, gap_below=0, above=0 },
  }
end

local v = F.extract(fake_ctx())
check(#v == F.N_INPUTS, "vector width == N_INPUTS")
for i = 1, #v do
  check(type(v[i]) == "number", "numeric " .. i)
  check(v[i] >= -1.001 and v[i] <= 1.001, "bounded [-1,1] at " .. i .. " = " .. tostring(v[i]))
end

-- direction sensitivity: enemy toward goal -> positive; behind -> negative
local ctx = fake_ctx()
ctx.snap.enemy.x = 20
local v2 = F.extract(ctx)
check(v[1] > 0 and v2[1] < 0, "goal-relative dx sign")

-- MIRROR INVARIANCE: a fully x-mirrored world with goal_dir=-1 must produce the
-- exact same features (this is what lets one net play both sides).
local function mirror_ctx(c)
  local m = fake_ctx()
  local cx = 0
  m.goal_dir = -c.goal_dir
  m.leader = c.leader
  m.my_room = -c.my_room
  m.enemy_room = -c.enemy_room
  local function flip(dst, src)
    dst.x = cx - src.x
    dst.vx = -(src.vx or 0)
    dst.facing = -(src.facing or 1)
    dst.wall_left, dst.wall_right = src.wall_right, src.wall_left
    dst.y, dst.vy = src.y, src.vy
    dst.has_sword, dst.grounded = src.has_sword, src.grounded
    dst.state_id, dst.cmd_bits = src.state_id, src.cmd_bits
  end
  flip(m.snap.player, c.snap.player)
  flip(m.snap.enemy, c.snap.enemy)
  if c.snap.nearest_sword_x then
    m.snap.nearest_sword_x = cx - c.snap.nearest_sword_x
    m.snap.nearest_sword_y = c.snap.nearest_sword_y
  else
    m.snap.nearest_sword_x, m.snap.nearest_sword_y = nil, nil
  end
  m.probes = c.probes  -- facing-relative, mirror-stable
  return m
end

local base = fake_ctx()
local vm = F.extract(mirror_ctx(base))
local vb = F.extract(base)
for i = 1, F.N_INPUTS do
  check(math.abs(vb[i] - vm[i]) < 1e-9, "mirror-invariant feature " .. i ..
        " (" .. tostring(vb[i]) .. " vs " .. tostring(vm[i]) .. ")")
end

-- enemy attack bit visible
local ctx_a = fake_ctx()
ctx_a.snap.enemy.cmd_bits = 0
local va = F.extract(ctx_a)
check(v[19] == 1 and va[19] == 0, "enemy attack bit feature")

-- range feature is unsigned distance
check(math.abs(v[20] - 80/200) < 1e-9, "range feature |dx|/200")

-- missing sword entity handled
local ctx3 = fake_ctx()
ctx3.snap.nearest_sword_x, ctx3.snap.nearest_sword_y = nil, nil
local v3 = F.extract(ctx3)
check(#v3 == F.N_INPUTS, "handles missing sword")
check(v3[23] == 0, "sword-present flag off")

-- extreme values stay clamped
local ctx4 = fake_ctx()
ctx4.snap.enemy.x = 1e6; ctx4.snap.player.vx = 1e6
local v4 = F.extract(ctx4)
for i = 1, #v4 do check(v4[i] >= -1.001 and v4[i] <= 1.001, "clamped " .. i) end

-- dying state flags (state 8/9)
local ctx5 = fake_ctx()
ctx5.snap.enemy.state_id = 8
local v5 = F.extract(ctx5)
check(v5[18] == 1, "enemy dying flag")
check(v5[17] == 0, "own dying flag off")
