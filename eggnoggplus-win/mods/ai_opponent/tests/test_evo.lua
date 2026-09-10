local NN = dofile("lib/nn.lua")
local EVO = dofile("lib/evo.lua")
EVO.init(NN)

local pop = EVO.new(8, {4, 6, 3}, 5)
check(#pop.nets == 8 and pop.gen == 0, "population init")

-- fitness = -index so net 1 is best; elites must survive verbatim
local fit = {}
for i = 1, 8 do fit[i] = -i end
local best_serial = NN.serialize(pop.nets[1])
local next_pop = EVO.next_gen(pop, fit, { elites = 2, mut_rate = 0.1, mut_scale = 0.2 }, NN.rng_new(9))
check(next_pop.gen == 1, "generation increments")
check(#next_pop.nets == 8, "size preserved")
check(NN.serialize(next_pop.nets[1]) == best_serial, "elite #1 preserved")

-- toy convergence: evolve weights toward output[1] high on fixed input
local pop2 = EVO.new(16, {2, 4, 2}, 1)
local function score(net) local o = NN.forward(net, {1, -1}); return o[1] - o[2] end
local best0
for g = 1, 30 do
  local f = {}
  for i = 1, 16 do f[i] = score(pop2.nets[i]) end
  if g == 1 then best0 = math.max(unpack(f)) end
  pop2 = EVO.next_gen(pop2, f, { elites = 2, mut_rate = 0.3, mut_scale = 0.3 }, NN.rng_new(g))
end
local fin = {}
for i = 1, 16 do fin[i] = score(pop2.nets[i]) end
check(math.max(unpack(fin)) > best0, "fitness improves over 30 generations")
