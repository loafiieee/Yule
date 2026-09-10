local NN = dofile("lib/nn.lua")

check(NN.param_count({3, 4, 2}) == 3*4+4 + 4*2+2, "param_count")

local net = NN.new({4, 8, 3}, 123)
check(#net.w == NN.param_count({4, 8, 3}), "new allocates all params")

local out = NN.forward(net, {0.1, -0.5, 1.0, 0.0})
check(#out == 3, "forward output width")
local out2 = NN.forward(net, {0.1, -0.5, 1.0, 0.0})
for i = 1, 3 do check(out[i] == out2[i], "forward deterministic " .. i) end

check(NN.argmax({0.1, 5.0, -2.0}) == 2, "argmax")

local s = NN.serialize(net)
local net2, err = NN.deserialize(s)
check(net2 ~= nil, "deserialize ok: " .. tostring(err))
if net2 then
  local o1, o3 = NN.forward(net, {1,1,1,1}), NN.forward(net2, {1,1,1,1})
  for i = 1, 3 do check(math.abs(o1[i]-o3[i]) < 1e-6, "roundtrip forward " .. i) end
end
check(NN.deserialize("garbage") == nil, "deserialize rejects garbage")

local rand = NN.rng_new(7)
local mut = NN.mutate(net, 1.0, 0.1, rand)
check(mut ~= net and mut.w ~= net.w, "mutate returns copy")
local diff = 0
for i = 1, #net.w do if mut.w[i] ~= net.w[i] then diff = diff + 1 end end
check(diff > #net.w * 0.9, "mutate rate=1 changes ~all weights")

local a, b = NN.new({2,2}, 1), NN.new({2,2}, 2)
local c = NN.crossover(a, b, NN.rng_new(3))
local from_a, from_b = 0, 0
for i = 1, #c.w do
  if c.w[i] == a.w[i] then from_a = from_a + 1 end
  if c.w[i] == b.w[i] then from_b = from_b + 1 end
end
check(from_a > 0 and from_b > 0, "crossover mixes parents")

-- rng determinism: same seed -> same stream
local r1, r2 = NN.rng_new(55), NN.rng_new(55)
for i = 1, 10 do check(r1() == r2(), "rng deterministic " .. i) end
