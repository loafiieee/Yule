local NN = dofile("lib/nn.lua")
local A = dofile("lib/actions.lua")
local H = dofile("lib/heuristic.lua")
H.init({ NN = NN })

local R, L, J, AT, D = 0x04, 0x08, 0x01, 0x02, 0x20
local function has(m, bit) return (math.floor(m / bit) % 2) == 1 end

local function fake_ctx(over)
  local c = {
    snap = {
      player = { x = 100, y = 50, vx = 0, vy = 0, facing = 1, has_sword = true, grounded = true,
                 wall_left = false, wall_right = false, state_id = 0, cmd_bits = 0 },
      enemy  = { x = 300, y = 50, vx = 0, vy = 0, facing = -1, has_sword = true, grounded = true,
                 wall_left = false, wall_right = false, state_id = 0, cmd_bits = 0 },
      nearest_sword_x = nil, nearest_sword_y = nil,
    },
    my_room = 2, enemy_room = 2, goal_dir = 1, leader = 0,
    nav = { [1] = { wall = false, gap = false }, [-1] = { wall = false, gap = false } },
  }
  for k, v in pairs(over or {}) do
    if k == 'player' or k == 'enemy' then
      for k2, v2 in pairs(v) do c.snap[k][k2] = v2 end
    else
      c[k] = v
    end
  end
  return c
end

-- dead -> idle
local m = H.new_mem(1)
local a, mode = H.decide(fake_ctx({ player = { state_id = 8 } }), m)
check(a == 1 and mode == 'dead', 'dying -> idle')

-- fight context detection
check(H.is_fight(fake_ctx({ enemy = { x = 160 } })), 'close same room armed = fight')
check(not H.is_fight(fake_ctx({ enemy = { x = 160 }, enemy_room = 3 })), 'other room = no fight')
check(not H.is_fight(fake_ctx({ enemy = { x = 160 }, player = { has_sword = false } })), 'unarmed = no fight')
check(not H.is_fight(fake_ctx()), 'far = no fight')

-- navigation: enemy far ahead (toward goal) -> chases/advances toward +x
local m2 = H.new_mem(2)
local a2, mode2 = H.decide(fake_ctx(), m2)
check(mode2 == 'navigate', 'far -> navigate (' .. tostring(mode2) .. ')')
check(has(A.mask(a2, 1), R), 'navigates right toward distant enemy')

-- navigation: I'm the leader, enemy behind me -> advance toward goal, not enemy
local m3 = H.new_mem(3)
local a3 = H.decide(fake_ctx({ enemy = { x = 20 }, leader = 1 }), m3)
check(has(A.mask(a3, 1), R), 'leader advances toward goal')

-- wall ahead while navigating -> jump
local m4 = H.new_mem(4)
local a4 = H.decide(fake_ctx({ nav = { [1] = { wall = true, gap = false }, [-1] = { wall = false, gap = false } } }), m4)
local mask4 = A.mask(a4, 1)
check(has(mask4, J) and has(mask4, R), 'wall ahead -> jump forward')

-- unarmed with a sword nearby -> goes for the sword (left of us)
local m5 = H.new_mem(5)
local a5, mode5 = H.decide(fake_ctx({ player = { has_sword = false }, enemy = { x = 400 },
                                      snapx = nil }), m5)
-- place sword left
local ctx5 = fake_ctx({ player = { has_sword = false }, enemy = { x = 400 } })
ctx5.snap.nearest_sword_x, ctx5.snap.nearest_sword_y = 40, 50
local m5b = H.new_mem(5)
local a5b, mode5b = H.decide(ctx5, m5b)
check(mode5b == 'get_sword', 'unarmed seeks sword (' .. tostring(mode5b) .. ')')
check(has(A.mask(a5b, 1), L), 'moves left toward the sword')

-- fight: in range vs idle enemy -> mostly attacks
local atk_n, act_n = 0, 100
for i = 1, act_n do
  local mm = H.new_mem(100 + i)
  local ai = H.decide(fake_ctx({ enemy = { x = 130 } }), mm)   -- adx 30
  if has(A.mask(ai, 1), AT) then atk_n = atk_n + 1 end
end
check(atk_n >= 45, 'attacks often in range (' .. atk_n .. '/100)')

-- fight: enemy attacking in range -> evades (jump/duck) more than half the time
local evade_n = 0
for i = 1, 100 do
  local mm = H.new_mem(300 + i)
  local ai = H.decide(fake_ctx({ enemy = { x = 130, cmd_bits = 2 } }), mm)
  local mk = A.mask(ai, 1)
  if has(mk, J) or has(mk, D) then evade_n = evade_n + 1 end
end
check(evade_n >= 55, 'evades when enemy swings (' .. evade_n .. '/100)')

-- hold mechanics: a jump hold repeats for several ticks
local mh = H.new_mem(7)
local ctxh = fake_ctx({ nav = { [1] = { wall = true, gap = false }, [-1] = { wall = false, gap = false } } })
local ah1 = H.decide(ctxh, mh)
local ah2 = H.decide(fake_ctx(), mh)   -- different ctx, but hold should persist
check(ah1 == ah2, 'hold repeats action across ticks')

-- MIRROR test: fully mirrored world (goal -1) with same rng seed produces the
-- mirrored command mask every tick
local function mirror_ctx(c)
  local mctx = fake_ctx()
  mctx.goal_dir = -c.goal_dir
  mctx.leader = c.leader
  mctx.my_room, mctx.enemy_room = -c.my_room, -c.enemy_room
  local function flip(dst, src)
    for k, v in pairs(src) do dst[k] = v end
    dst.x = -src.x
    dst.vx = -(src.vx or 0)
    dst.facing = -(src.facing or 1)
    dst.wall_left, dst.wall_right = src.wall_right, src.wall_left
  end
  flip(mctx.snap.player, c.snap.player)
  flip(mctx.snap.enemy, c.snap.enemy)
  if c.snap.nearest_sword_x then
    mctx.snap.nearest_sword_x = -c.snap.nearest_sword_x
    mctx.snap.nearest_sword_y = c.snap.nearest_sword_y
  end
  mctx.nav = { [1] = c.nav[-1], [-1] = c.nav[1] }
  return mctx
end
local function mirror_mask(mask)
  local out = mask - (has(mask, R) and R or 0) - (has(mask, L) and L or 0)
  if has(mask, R) then out = out + L end
  if has(mask, L) then out = out + R end
  return out
end
local scenarios = {
  fake_ctx({ enemy = { x = 130 } }),
  fake_ctx({ enemy = { x = 130, cmd_bits = 2 } }),
  fake_ctx({ player = { has_sword = false }, enemy = { x = 150 } }),
  fake_ctx({ leader = 1, enemy = { x = 20 } }),
  fake_ctx({ nav = { [1] = { wall = true, gap = false }, [-1] = { wall = false, gap = false } } }),
}
for si, ctx in ipairs(scenarios) do
  local ma, mb = H.new_mem(900 + si), H.new_mem(900 + si)
  for t = 1, 12 do
    local aa = H.decide(ctx, ma)
    local ab = H.decide(mirror_ctx(ctx), mb)
    local maskA = A.mask(aa, ctx.goal_dir)
    local maskB = A.mask(ab, -ctx.goal_dir)
    check(maskB == mirror_mask(maskA),
          string.format('mirror scenario %d tick %d (%d vs %d)', si, t, maskA, maskB))
  end
end
