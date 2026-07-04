local A = dofile("lib/actions.lua")
local J, AT, R, L, U, D = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20

check(A.COUNT == 14, "14 actions")
check(A.mask(1) == 0, "idle mask 0")
check(A.mask(99) == 0, "out of range -> 0")

local seen = {}
for i = 1, A.COUNT do
  local m = A.mask(i, 1)
  check(m ~= nil, "mask defined " .. i)
  check(not (seen[m] and m ~= 0), "masks unique " .. i)
  seen[m] = true
  check(m % 64 == m, "mask uses only low 6 bits " .. i)  -- no MENU bit
end

-- goal-direction mirroring: forward means RIGHT when goal_dir=+1, LEFT when -1
local function has(m, bit) return (math.floor(m / bit) % 2) == 1 end
for i = 1, A.COUNT do
  local e = A.LIST[i]
  local mp, mm = A.mask(i, 1), A.mask(i, -1)
  if e.fwd then
    check(has(mp, R) and not has(mp, L), "fwd -> RIGHT at +1: " .. e.name)
    check(has(mm, L) and not has(mm, R), "fwd -> LEFT at -1: " .. e.name)
  elseif e.back then
    check(has(mp, L) and not has(mp, R), "back -> LEFT at +1: " .. e.name)
    check(has(mm, R) and not has(mm, L), "back -> RIGHT at -1: " .. e.name)
  else
    check(mp == mm, "non-lateral action side-invariant: " .. e.name)
  end
  -- vertical/attack bits never mirrored
  check(has(mp, J) == has(mm, J) and has(mp, AT) == has(mm, AT) and
        has(mp, U) == has(mm, U) and has(mp, D) == has(mm, D),
        "base bits stable: " .. e.name)
end

check(A.mask(3) == A.mask(3, 1), "default goal_dir is +1")
