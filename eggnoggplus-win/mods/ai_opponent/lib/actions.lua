-- Discrete action set -> native command bitmasks.
-- JUMP 0x01, ATTACK 0x02, RIGHT 0x04, LEFT 0x08, UP 0x10, DOWN 0x20.
local A = {}
local J, AT, R, L, U, D = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
A.LIST = {
  { name = "idle",         mask = 0 },
  { name = "left",         mask = L },
  { name = "right",        mask = R },
  { name = "jump",         mask = J },
  { name = "jump_left",    mask = J + L },
  { name = "jump_right",   mask = J + R },
  { name = "attack",       mask = AT },
  { name = "attack_left",  mask = AT + L },
  { name = "attack_right", mask = AT + R },
  { name = "up",           mask = U },
  { name = "down",         mask = D },
  { name = "down_jump",    mask = D + J },
}
A.COUNT = #A.LIST
function A.mask(i)
  local e = A.LIST[i]
  return e and e.mask or 0
end
return A
