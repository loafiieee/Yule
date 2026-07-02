-- (mu+lambda)-style GA with elitism and tournament selection.
-- Call EVO.init(NN) once before use.
local EVO = { _NN = nil }

function EVO.init(NN) EVO._NN = NN end

function EVO.new(n, sizes, seed)
  local pop = { nets = {}, gen = 0, sizes = sizes }
  for i = 1, n do pop.nets[i] = EVO._NN.new(sizes, (seed or 1) * 7919 + i) end
  return pop
end

local function ranked_indices(fit)
  local idx = {}
  for i = 1, #fit do idx[i] = i end
  table.sort(idx, function(a, b) return fit[a] > fit[b] end)
  return idx
end

local function tournament(fit, rand, k)
  local best = nil
  for _ = 1, k do
    local c = 1 + math.floor(rand() * #fit)
    if c > #fit then c = #fit end
    if not best or fit[c] > fit[best] then best = c end
  end
  return best
end

function EVO.next_gen(pop, fit, cfg, rand)
  local NN = EVO._NN
  local n = #pop.nets
  local elites = cfg.elites or 2
  local order = ranked_indices(fit)
  local out = { nets = {}, gen = pop.gen + 1, sizes = pop.sizes }
  for i = 1, math.min(elites, n) do
    out.nets[i] = NN.copy(pop.nets[order[i]])
  end
  while #out.nets < n do
    local pa = pop.nets[tournament(fit, rand, 3)]
    local pb = pop.nets[tournament(fit, rand, 3)]
    local child = NN.crossover(pa, pb, rand)
    out.nets[#out.nets + 1] = NN.mutate(child, cfg.mut_rate or 0.1, cfg.mut_scale or 0.2, rand)
  end
  return out
end

return EVO
