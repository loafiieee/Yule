local NN = dofile("lib/nn.lua")
local Policy = dofile("lib/policy.lua")
Policy.init(NN)

local net = NN.new({4, 8, 5}, 11)
local p = Policy.new(net, { react_delay = 0, epsilon = 0, seed = 1 })
local a1 = Policy.decide(p, {1, 0, 0, 0})
check(a1 >= 1 and a1 <= 5, "action in range")
check(Policy.decide(p, {1, 0, 0, 0}) == a1, "deterministic with eps=0")

-- reaction delay: with delay=3, early decisions keep using the OLDEST queued features
local pd = Policy.new(net, { react_delay = 3, epsilon = 0, seed = 1 })
local first = Policy.decide(pd, {1, 0, 0, 0})
local second = Policy.decide(pd, {0, 1, 0, 0})  -- still sees {1,0,0,0}
check(second == first, "delayed observations")

-- after the fifo fills, old features age out
local pd2 = Policy.new(net, { react_delay = 2, epsilon = 0, seed = 1 })
local base = Policy.decide(pd2, {1, 0, 0, 0})
Policy.decide(pd2, {0, 0, 0, 1})
Policy.decide(pd2, {0, 0, 0, 1})
local aged = Policy.decide(pd2, {0, 0, 0, 1})
local direct = Policy.decide(Policy.new(net, { react_delay = 0, epsilon = 0, seed = 1 }), {0, 0, 0, 1})
check(aged == direct, "fifo ages out to newer features")

-- epsilon=1 gives random actions across full range eventually
local pr = Policy.new(net, { react_delay = 0, epsilon = 1, seed = 2 })
local seen = {}
for i = 1, 200 do
  local a = Policy.decide(pr, {0, 0, 0, 0})
  check(a >= 1 and a <= 5, "random action in range " .. i)
  seen[a] = true
end
local n = 0; for _ in pairs(seen) do n = n + 1 end
check(n >= 3, "epsilon explores")

Policy.reset(pd)
check(#pd.fifo == 0, "reset clears fifo")
