-- Feedforward MLP on a flat weight array. Pure Lua 5.1 / LuaJIT.
-- Loaded with dofile (tests) or mod.dofile (in game); returns its table.
local NN = {}

function NN.rng_new(seed)
  local s = (seed or 42) % 2147483648
  if s <= 0 then s = s + 2147483647 end
  return function()
    s = (1103515245 * s + 12345) % 2147483648
    return s / 2147483648
  end
end

function NN.param_count(sizes)
  local n = 0
  for i = 2, #sizes do n = n + sizes[i-1] * sizes[i] + sizes[i] end
  return n
end

function NN.new(sizes, seed)
  local net = { sizes = {}, w = {} }
  for i = 1, #sizes do net.sizes[i] = sizes[i] end
  local rand = NN.rng_new(seed)
  for i = 1, NN.param_count(sizes) do net.w[i] = (rand() * 2 - 1) * 0.5 end
  return net
end

function NN.forward(net, x)
  local sizes, w = net.sizes, net.w
  local a = x
  local k = 0
  for layer = 2, #sizes do
    local nin, nout = sizes[layer-1], sizes[layer]
    local out = {}
    for j = 1, nout do
      local sum = 0
      local base = k + (j - 1) * nin
      for i = 1, nin do sum = sum + w[base + i] * a[i] end
      out[j] = sum
    end
    k = k + nin * nout
    for j = 1, nout do out[j] = out[j] + w[k + j] end
    k = k + nout
    if layer < #sizes then
      for j = 1, nout do
        local t = out[j]
        out[j] = t > 0 and t or 0.01 * t   -- leaky ReLU
      end
    end
    a = out
  end
  return a
end

function NN.argmax(v)
  local bi, bv = 1, v[1]
  for i = 2, #v do if v[i] > bv then bv, bi = v[i], i end end
  return bi
end

function NN.copy(net)
  local c = { sizes = {}, w = {} }
  for i = 1, #net.sizes do c.sizes[i] = net.sizes[i] end
  for i = 1, #net.w do c.w[i] = net.w[i] end
  return c
end

function NN.mutate(net, rate, scale, rand)
  local m = NN.copy(net)
  for i = 1, #m.w do
    if rand() < rate then m.w[i] = m.w[i] + (rand() * 2 - 1) * scale end
  end
  return m
end

function NN.crossover(a, b, rand)
  local c = NN.copy(a)
  for i = 1, #c.w do
    if rand() < 0.5 then c.w[i] = b.w[i] end
  end
  return c
end

function NN.serialize(net)
  local ws = {}
  for i = 1, #net.w do ws[i] = string.format("%.9g", net.w[i]) end
  return "nn1|" .. table.concat(net.sizes, ",") .. "|" .. table.concat(ws, " ")
end

function NN.deserialize(s)
  if type(s) ~= "string" then return nil, "not a string" end
  local tag, sizes_s, w_s = s:match("^(nn1)|([%d,]+)|(.+)$")
  if not tag then return nil, "bad format" end
  local net = { sizes = {}, w = {} }
  for num in sizes_s:gmatch("%d+") do net.sizes[#net.sizes + 1] = tonumber(num) end
  if #net.sizes < 2 then return nil, "bad sizes" end
  for num in w_s:gmatch("%S+") do
    local v = tonumber(num)
    if not v then return nil, "bad weight" end
    net.w[#net.w + 1] = v
  end
  if #net.w ~= NN.param_count(net.sizes) then return nil, "weight count mismatch" end
  return net
end

return NN
