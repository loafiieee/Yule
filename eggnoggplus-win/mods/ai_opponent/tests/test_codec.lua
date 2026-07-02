local Codec = dofile("lib/codec.lua")

-- fake storage backed by a table (same get/set/delete contract as mod storage)
local kv = {}
local storage = {
  get = function(k, d) local v = kv[k]; if v == nil then return d end; return v end,
  set = function(k, v) kv[k] = v; return true end,
  delete = function(k) kv[k] = nil; return true end,
}

local long = string.rep("abcdefghij", 1000) .. "END"   -- 10,003 chars, forces chunking
check(Codec.store(storage, "ck", long), "store ok")
check(Codec.load(storage, "ck") == long, "roundtrip long")
check(Codec.load(storage, "missing") == nil, "missing -> nil")
check(Codec.store(storage, "ck", "short"), "overwrite shrinks")
check(Codec.load(storage, "ck") == "short", "roundtrip short after shrink")

-- stale chunk keys beyond the new count must be gone
check(kv["ck#2"] == nil, "stale chunks deleted")

-- empty string roundtrip
check(Codec.store(storage, "e", ""), "store empty")
check(Codec.load(storage, "e") == "", "roundtrip empty")
