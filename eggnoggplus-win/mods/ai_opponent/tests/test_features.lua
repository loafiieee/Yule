local F = dofile("lib/features.lua")

local function fake_ctx()
  return {
    snap = {
      player = { x=100, y=50, vx=1, vy=0, facing=1, has_sword=true, grounded=true,
                 wall_left=false, wall_right=false, state_id=0, cmd_bits=0 },
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

-- direction sensitivity: enemy to the right -> dx positive; flip -> negative
local ctx = fake_ctx()
ctx.snap.enemy.x = 20
local v2 = F.extract(ctx)
check(v[1] > 0 and v2[1] < 0, "dx sign follows enemy side")

-- enemy attack bit visible
local ctx_a = fake_ctx()
ctx_a.snap.enemy.cmd_bits = 0
local va = F.extract(ctx_a)
check(v[19] == 1 and va[19] == 0, "enemy attack bit feature")

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
