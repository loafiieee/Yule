-- Inference wrapper: reaction latency (feature FIFO) + epsilon action noise.
-- Call Policy.init(NN) once before use (dependency injection; no dofile here so the
-- same file loads under both standalone luajit and the mod sandbox).
local Policy = { _NN = nil }

function Policy.init(NN) Policy._NN = NN end

function Policy.new(net, opts)
  opts = opts or {}
  return {
    net = net,
    delay = opts.react_delay or 0,
    eps = opts.epsilon or 0,
    rand = Policy._NN.rng_new(opts.seed or 1),
    fifo = {},
    n_actions = net.sizes[#net.sizes],
  }
end

function Policy.reset(p)
  p.fifo = {}
end

function Policy.decide(p, feats)
  local NN = Policy._NN
  p.fifo[#p.fifo + 1] = feats
  while #p.fifo > p.delay + 1 do table.remove(p.fifo, 1) end
  local use = p.fifo[1]
  if p.eps > 0 and p.rand() < p.eps then
    return 1 + math.floor(p.rand() * p.n_actions)
  end
  return NN.argmax(NN.forward(p.net, use))
end

return Policy
