local RW = dofile("lib/reward.lua")

local function o(over)
  local base = { x = 100, goal = 1, has_sword = true, enemy_has_sword = true,
                 is_leader = false, dist = 150 }
  for k, v in pairs(over or {}) do base[k] = v end
  return base
end

-- camping = exactly zero reward
check(RW.delta(o(), o()) == 0, "camping earns nothing")

-- advancing toward goal is rewarded; retreating is punished symmetrically
local fwd = RW.delta(o(), o({ x = 120 }))
local back = RW.delta(o(), o({ x = 80 }))
check(fwd > 0, "advance rewarded (" .. fwd .. ")")
check(back < 0, "retreat punished (" .. back .. ")")
check(math.abs(fwd + back) < 1e-9, "advance/retreat symmetric")

-- oscillation nets zero (potential-based: can't be farmed)
local a, b = o(), o({ x = 120 })
check(math.abs(RW.delta(a, b) + RW.delta(b, a)) < 1e-9, "oscillation nets zero")

-- mirror: same advance on the other side scores identically
local mfwd = RW.delta(o({ x = -100, goal = -1 }), o({ x = -120, goal = -1 }))
check(math.abs(mfwd - fwd) < 1e-9, "goal-mirrored advance identical")

-- gaining a sword, disarming the enemy, gaining the lead: all positive
check(RW.delta(o({ has_sword = false }), o()) > 0, "gaining sword rewarded")
check(RW.delta(o(), o({ enemy_has_sword = false })) > 0, "enemy disarmed rewarded")
check(RW.delta(o(), o({ is_leader = true })) > 0, "becoming leader rewarded")

-- the hunt: when not leader, closing distance is progress
check(RW.delta(o({ dist = 180 }), o({ dist = 120 })) > 0, "closing rewarded when hunting")
check(RW.delta(o({ dist = 120 }), o({ dist = 180 })) < 0, "fleeing punished when hunting")
-- but when leader, distance doesn't matter (running is the job)
check(RW.delta(o({ is_leader = true, dist = 180 }), o({ is_leader = true, dist = 120 })) == 0,
      "leader ignores distance")

-- teleport clamp
local spike = RW.delta(o(), o({ x = 5000 }))
check(spike <= RW.W.DELTA_CLAMP + 1e-9, "teleport clamped (" .. spike .. ")")

-- nil baseline -> no reward
check(RW.delta(nil, o()) == 0, "nil baseline safe")
