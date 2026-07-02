local A = dofile("lib/actions.lua")
check(A.COUNT == 12, "12 actions")
check(A.mask(1) == 0, "idle mask 0")
local seen = {}
for i = 1, A.COUNT do
  local m = A.mask(i)
  check(m ~= nil, "mask defined " .. i)
  check(not (seen[m] and m ~= 0), "masks unique " .. i)
  seen[m] = true
  check(m % 64 == m, "mask uses only low 6 bits " .. i)  -- no MENU bit
end
check(A.mask(99) == 0, "out of range -> 0")
