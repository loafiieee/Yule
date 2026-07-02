-- Chunked string storage: values kept under CHUNK chars to stay friendly to the
-- line-based storage backend. Layout: key.."#n" = chunk count, key.."#i" = chunk i.
local Codec = {}
local CHUNK = 2000

function Codec.store(storage, key, str)
  local old_n = tonumber(storage.get(key .. "#n", 0)) or 0
  local n = math.ceil(#str / CHUNK)
  if n == 0 then n = 1 end
  for i = 1, n do
    local part = str:sub((i - 1) * CHUNK + 1, i * CHUNK)
    if not storage.set(key .. "#" .. i, part) then return false end
  end
  for i = n + 1, old_n do storage.delete(key .. "#" .. i) end
  return storage.set(key .. "#n", tostring(n)) and true or false
end

function Codec.load(storage, key)
  local n = tonumber(storage.get(key .. "#n"))
  if not n or n < 1 then return nil end
  local parts = {}
  for i = 1, n do
    local part = storage.get(key .. "#" .. i)
    if type(part) ~= "string" then return nil end
    parts[i] = part
  end
  return table.concat(parts)
end

return Codec
