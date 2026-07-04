-- Discrete action set -> native command bitmasks, canonicalized to the goal
-- direction: "forward" always means "toward my goal end of the map", so one
-- policy plays either side. A.mask(i, goal_dir) resolves fwd/back to RIGHT/LEFT.
-- JUMP 0x01, ATTACK 0x02, RIGHT 0x04, LEFT 0x08, UP 0x10, DOWN 0x20.
local A = {}
local J, AT, R, L, U, D = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
-- fwd/back are resolved per goal_dir at mask time
A.LIST = {
  { name = "idle",         base = 0,      fwd = false, back = false },
  { name = "back",         base = 0,      fwd = false, back = true  },
  { name = "forward",      base = 0,      fwd = true,  back = false },
  { name = "jump",         base = J,      fwd = false, back = false },
  { name = "jump_back",    base = J,      fwd = false, back = true  },
  { name = "jump_forward", base = J,      fwd = true,  back = false },
  { name = "attack",       base = AT,     fwd = false, back = false },
  { name = "attack_back",  base = AT,     fwd = false, back = true  },
  { name = "attack_fwd",   base = AT,     fwd = true,  back = false },
  { name = "up",           base = U,      fwd = false, back = false },
  { name = "down",         base = D,      fwd = false, back = false },
  { name = "down_jump",    base = D + J,  fwd = false, back = false },  -- crouch (sword pickup)
  { name = "slide_fwd",    base = D + J,  fwd = true,  back = false },  -- slide: passes through hitboxes
  { name = "slide_back",   base = D + J,  fwd = false, back = true  },
}
A.COUNT = #A.LIST

function A.mask(i, goal_dir)
  local e = A.LIST[i]
  if not e then return 0 end
  local dir = (goal_dir or 1) >= 0 and 1 or -1
  local m = e.base
  local fwd_bit = (dir > 0) and R or L
  local back_bit = (dir > 0) and L or R
  if e.fwd then m = m + fwd_bit end
  if e.back then m = m + back_bit end
  return m
end

return A
