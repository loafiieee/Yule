local STATE = "official_cosmetics_hats"
local SDLK_ESCAPE = 27

local ui = mod.ui
local game = mod.game

local SERVER_ORIGIN = "https://loafiieee.com"
local SERVER_BASE = SERVER_ORIGIN .. "/eggnogg"
local COSMETICS_ROOT = "/cosmetics/v1"
local MANIFEST_URL = SERVER_BASE .. COSMETICS_ROOT .. "/manifest.json"
local LOCAL_HAT_ASSET_ID = "hats"
local REMOTE_HAT_ASSET_ID = "official_hats_remote"
local LOCAL_HAT_PATH = "assets/hats.png"
local CACHE_DIR = "cache"
local CACHE_HAT_PATH = CACHE_DIR .. "/hats.png"
local HAT_CELL_W = 32
local HAT_CELL_H = 32
local BUNDLED_HATS_SHA256 = "3bbc38cdc3b540ea337e75a4800d84c47c5c199e456cfca5312c1bf662ca0245"

local selected_player = storage.get("selected_player", "p1")
local profile = {
  p1 = {
    hat = storage.get("p1_hat", "none"),
  },
  p2 = {
    hat = storage.get("p2_hat", "none"),
  },
  online = {
    hat = storage.get("online_hat", storage.get("p1_hat", "none")),
  },
}

local hat_sheet = nil
local hat_asset_id = LOCAL_HAT_ASSET_ID
local hat_asset_path = LOCAL_HAT_PATH
local hat_asset_sha256 = BUNDLED_HATS_SHA256
local asset_error = nil
local last_asset_try = 0
local match_profiles = {}

local hats = {
  {
    id = "none",
    name = "None",
    motion = false,
    sprite_index = nil,
    color = {0.16, 0.18, 0.21, 1.0},
  },
  {
    id = "cap",
    name = "Cap",
    sprite_index = 0,
    motion = true,
    scale = 0.64,
    y = 10.2,
    bob = 0.7,
    tilt = 7.0,
    drag = 0.40,
  },
  {
    id = "crown",
    name = "Crown",
    sprite_index = 1,
    motion = true,
    scale = 0.68,
    y = 10.8,
    bob = 0.45,
    tilt = 5.0,
    drag = 0.25,
  },
  {
    id = "halo",
    name = "Halo",
    sprite_index = 2,
    motion = false,
    scale = 0.72,
    y = 11.4,
    bob = 0.0,
    tilt = 0.0,
    drag = 0.0,
  },
  {
    id = "beanie",
    name = "Beanie",
    sprite_index = 3,
    motion = true,
    scale = 0.66,
    y = 10.4,
    bob = 0.75,
    tilt = 6.0,
    drag = 0.35,
  },
  {
    id = "top_hat",
    name = "Top Hat",
    sprite_index = 4,
    motion = false,
    scale = 0.72,
    y = 10.8,
    bob = 0.0,
    tilt = 0.0,
    drag = 0.0,
  },
  {
    id = "visor",
    name = "Visor",
    sprite_index = 5,
    motion = true,
    scale = 0.62,
    y = 9.6,
    bob = 0.35,
    tilt = 4.0,
    drag = 0.20,
  },
}

local hat_by_id = {}

local function clone_table(t)
  if type(t) ~= "table" then return t end
  local copy = {}
  for k, v in pairs(t) do
    copy[k] = clone_table(v)
  end
  return copy
end

local default_hats = clone_table(hats)

local function rebuild_hat_index()
  hat_by_id = {}
  for i = 1, #hats do
    hat_by_id[hats[i].id] = hats[i]
  end
end

rebuild_hat_index()

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local bitlib = bit
if not bitlib then
  local ok, loaded = pcall(require, "bit")
  if ok then bitlib = loaded end
end

local function read_binary_file(rel_path)
  if not mod.get_path then return nil, "mod.get_path unavailable" end
  local path = mod.get_path(rel_path)
  local f, err = io.open(path, "rb")
  if not f then return nil, err or "open failed" end
  local data = f:read("*a")
  f:close()
  return data
end

local function ensure_cache_dir()
  if not mod.get_path then return false, "mod.get_path unavailable" end
  local dir = mod.get_path(CACHE_DIR)
  dir = tostring(dir or ""):gsub('"', "")
  if dir == "" then return false, "cache path unavailable" end
  os.execute('mkdir "' .. dir .. '" >nul 2>nul')
  return true
end

local function write_binary_file(rel_path, data)
  local ok, err = ensure_cache_dir()
  if not ok then return false, err end
  local path = mod.get_path(rel_path)
  local f, open_err = io.open(path, "wb")
  if not f then return false, open_err or "open failed" end
  f:write(data or "")
  f:close()
  return true
end

local function sha256_hex(data)
  if not bitlib then return nil, "LuaJIT bit library unavailable" end

  local band = bitlib.band
  local bxor = bitlib.bxor
  local bnot = bitlib.bnot
  local rshift = bitlib.rshift
  local ror = bitlib.ror
  local MOD32 = 4294967296
  local K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  }
  local H = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  }

  local function add32(...)
    local sum = 0
    for i = 1, select("#", ...) do
      sum = (sum + (select(i, ...) % MOD32)) % MOD32
    end
    return sum
  end

  local function u32(n)
    return n % MOD32
  end

  local function be32(n)
    n = u32(n)
    return string.char(math.floor(n / 16777216) % 256,
                       math.floor(n / 65536) % 256,
                       math.floor(n / 256) % 256,
                       n % 256)
  end

  data = tostring(data or "")
  local original_len = #data
  local bit_len_hi = math.floor(original_len / 536870912)
  local bit_len_lo = (original_len * 8) % MOD32
  data = data .. string.char(0x80)
  data = data .. string.rep("\0", (56 - (#data % 64)) % 64)
  data = data .. be32(bit_len_hi) .. be32(bit_len_lo)

  for chunk = 1, #data, 64 do
    local w = {}
    for i = 0, 15 do
      local j = chunk + i * 4
      local b1, b2, b3, b4 = data:byte(j, j + 3)
      w[i] = ((b1 or 0) * 16777216 + (b2 or 0) * 65536 + (b3 or 0) * 256 + (b4 or 0)) % MOD32
    end
    for i = 16, 63 do
      local s0 = bxor(bxor(ror(w[i - 15], 7), ror(w[i - 15], 18)), rshift(w[i - 15], 3))
      local s1 = bxor(bxor(ror(w[i - 2], 17), ror(w[i - 2], 19)), rshift(w[i - 2], 10))
      w[i] = add32(w[i - 16], s0, w[i - 7], s1)
    end

    local a, b, c, d, e, f, g, h = H[1], H[2], H[3], H[4], H[5], H[6], H[7], H[8]
    for i = 0, 63 do
      local S1 = bxor(bxor(ror(e, 6), ror(e, 11)), ror(e, 25))
      local ch = bxor(band(e, f), band(bnot(e), g))
      local temp1 = add32(h, S1, ch, K[i + 1], w[i])
      local S0 = bxor(bxor(ror(a, 2), ror(a, 13)), ror(a, 22))
      local maj = bxor(bxor(band(a, b), band(a, c)), band(b, c))
      local temp2 = add32(S0, maj)
      h, g, f, e, d, c, b, a = g, f, e, add32(d, temp1), c, b, a, add32(temp1, temp2)
    end

    H[1] = add32(H[1], a)
    H[2] = add32(H[2], b)
    H[3] = add32(H[3], c)
    H[4] = add32(H[4], d)
    H[5] = add32(H[5], e)
    H[6] = add32(H[6], f)
    H[7] = add32(H[7], g)
    H[8] = add32(H[8], h)
  end

  local out = {}
  for i = 1, 8 do
    out[i] = string.format("%08x", u32(H[i]))
  end
  return table.concat(out)
end

local function json_decode(text)
  text = tostring(text or "")
  local len = #text
  local pos = 1

  local function fail(msg)
    error(msg .. " at byte " .. tostring(pos), 0)
  end

  local function skip_ws()
    while pos <= len do
      local c = text:byte(pos)
      if c ~= 32 and c ~= 9 and c ~= 10 and c ~= 13 then break end
      pos = pos + 1
    end
  end

  local parse_value

  local function parse_string()
    if text:byte(pos) ~= 34 then fail("expected string") end
    pos = pos + 1
    local out = {}
    while pos <= len do
      local c = text:byte(pos)
      if c == 34 then
        pos = pos + 1
        return table.concat(out)
      elseif c == 92 then
        pos = pos + 1
        local esc = text:sub(pos, pos)
        if esc == '"' or esc == "\\" or esc == "/" then
          out[#out + 1] = esc
          pos = pos + 1
        elseif esc == "b" then
          out[#out + 1] = "\b"
          pos = pos + 1
        elseif esc == "f" then
          out[#out + 1] = "\f"
          pos = pos + 1
        elseif esc == "n" then
          out[#out + 1] = "\n"
          pos = pos + 1
        elseif esc == "r" then
          out[#out + 1] = "\r"
          pos = pos + 1
        elseif esc == "t" then
          out[#out + 1] = "\t"
          pos = pos + 1
        elseif esc == "u" then
          local cp = tonumber(text:sub(pos + 1, pos + 4), 16)
          if not cp then fail("invalid unicode escape") end
          out[#out + 1] = cp < 128 and string.char(cp) or "?"
          pos = pos + 5
        else
          fail("invalid escape")
        end
      elseif c < 32 then
        fail("invalid control character")
      else
        out[#out + 1] = string.char(c)
        pos = pos + 1
      end
    end
    fail("unterminated string")
  end

  local function parse_number()
    local start = pos
    if text:sub(pos, pos) == "-" then pos = pos + 1 end
    if text:sub(pos, pos) == "0" then
      pos = pos + 1
    else
      if not text:sub(pos, pos):match("%d") then fail("invalid number") end
      while text:sub(pos, pos):match("%d") do pos = pos + 1 end
    end
    if text:sub(pos, pos) == "." then
      pos = pos + 1
      if not text:sub(pos, pos):match("%d") then fail("invalid number") end
      while text:sub(pos, pos):match("%d") do pos = pos + 1 end
    end
    local e = text:sub(pos, pos)
    if e == "e" or e == "E" then
      pos = pos + 1
      local sign = text:sub(pos, pos)
      if sign == "+" or sign == "-" then pos = pos + 1 end
      if not text:sub(pos, pos):match("%d") then fail("invalid exponent") end
      while text:sub(pos, pos):match("%d") do pos = pos + 1 end
    end
    local n = tonumber(text:sub(start, pos - 1))
    if not n then fail("invalid number") end
    return n
  end

  local function parse_array()
    pos = pos + 1
    local out = {}
    skip_ws()
    if text:sub(pos, pos) == "]" then
      pos = pos + 1
      return out
    end
    while true do
      out[#out + 1] = parse_value()
      skip_ws()
      local c = text:sub(pos, pos)
      if c == "]" then
        pos = pos + 1
        return out
      elseif c == "," then
        pos = pos + 1
      else
        fail("expected ',' or ']'")
      end
    end
  end

  local function parse_object()
    pos = pos + 1
    local out = {}
    skip_ws()
    if text:sub(pos, pos) == "}" then
      pos = pos + 1
      return out
    end
    while true do
      skip_ws()
      local key = parse_string()
      skip_ws()
      if text:sub(pos, pos) ~= ":" then fail("expected ':'") end
      pos = pos + 1
      out[key] = parse_value()
      skip_ws()
      local c = text:sub(pos, pos)
      if c == "}" then
        pos = pos + 1
        return out
      elseif c == "," then
        pos = pos + 1
      else
        fail("expected ',' or '}'")
      end
    end
  end

  function parse_value()
    skip_ws()
    local c = text:sub(pos, pos)
    if c == "{" then return parse_object() end
    if c == "[" then return parse_array() end
    if c == '"' then return parse_string() end
    if c == "-" or c:match("%d") then return parse_number() end
    if text:sub(pos, pos + 3) == "true" then
      pos = pos + 4
      return true
    end
    if text:sub(pos, pos + 4) == "false" then
      pos = pos + 5
      return false
    end
    if text:sub(pos, pos + 3) == "null" then
      pos = pos + 4
      return nil
    end
    fail("unexpected JSON value")
  end

  local ok, value_or_err = pcall(function()
    local value = parse_value()
    skip_ws()
    if pos <= len then fail("unexpected trailing data") end
    return value
  end)
  if not ok then return nil, value_or_err end
  return value_or_err
end

local function valid_hat_id(id)
  return hat_by_id[id] and id or "none"
end

local function valid_online_hat_id(id)
  id = valid_hat_id(id)
  local item = hat_by_id[id]
  if item and item.allowed_online == false then return "none" end
  return id
end

local fallback_skin = {1.0, 1.0, 1.0, 1.0}
local fallback_clothing = {
  p1 = {1.00, 0.46, 0.28, 1.0},
  p2 = {0.34, 0.66, 1.00, 1.0},
}

local function rgba_or_fallback(rgba, fallback)
  if type(rgba) ~= "table" then
    return fallback
  end

  return {
    tonumber(rgba[1] or rgba.r) or fallback[1],
    tonumber(rgba[2] or rgba.g) or fallback[2],
    tonumber(rgba[3] or rgba.b) or fallback[3],
    tonumber(rgba[4] or rgba.a) or fallback[4],
  }
end

local function player_index_from_id(player_id)
  return player_id == "p2" and 1 or 0
end

local function selected_player_colours(player_id)
  local player_index = player_index_from_id(player_id)
  local skin
  local clothing

  if game.player_color then
    skin = game.player_color(player_index, 0)
    clothing = game.player_color(player_index, 1)
  elseif game.player_colour then
    skin = game.player_colour(player_index, 0)
    clothing = game.player_colour(player_index, 1)
  end

  return rgba_or_fallback(skin, fallback_skin),
         rgba_or_fallback(clothing, fallback_clothing[player_id] or fallback_clothing.p1)
end

profile.p1.hat = valid_hat_id(profile.p1.hat)
profile.p2.hat = valid_hat_id(profile.p2.hat)
profile.online.hat = valid_hat_id(profile.online.hat)
selected_player = (selected_player == "p2" or selected_player == "online") and selected_player or "p1"

local function profile_id(id)
  if id == "p2" then return "p2" end
  if id == "online" then return "online" end
  return "p1"
end

local function save_hat(player_id, hat_id)
  player_id = profile_id(player_id)
  hat_id = valid_hat_id(hat_id)
  profile[player_id].hat = hat_id
  storage.set(player_id .. "_hat", hat_id)
end

local function set_selected_player(player_id)
  selected_player = profile_id(player_id)
  storage.set("selected_player", selected_player)
end

local cosmetics_net = {
  status = "idle",
  message = "using bundled hats",
  verified = false,
  retry_at = 0.0,
  manifest_handle = nil,
  asset_handle = nil,
  pending = nil,
  revision = nil,
  checked_at = 0.0,
}

local function resolve_cosmetic_url(url)
  url = tostring(url or "")
  if url:match("^https?://") then return url end
  if url:sub(1, 8) == "/eggnogg" then return SERVER_ORIGIN .. url end
  if url:sub(1, 1) == "/" then return SERVER_BASE .. url end
  return SERVER_BASE .. "/" .. url
end

local function lower_sha256(value)
  if type(value) ~= "string" then return nil end
  local sha = value:lower()
  if #sha == 64 and sha:match("^[0-9a-f]+$") then
    return sha
  end
  return nil
end

local function clean_id(value)
  if type(value) ~= "string" then return nil end
  if #value < 1 or #value > 64 then return nil end
  if not value:match("^[%w_%-%.]+$") then return nil end
  return value
end

local function clean_name(value, fallback)
  value = type(value) == "string" and value or fallback
  value = tostring(value or "Hat")
  value = value:gsub("[%c]", ""):sub(1, 40)
  return value ~= "" and value or "Hat"
end

local function clone_default_hats()
  local cloned = {}
  for i = 1, #default_hats do
    cloned[i] = clone_table(default_hats[i])
  end
  return cloned
end

local function clamp_profiles_to_catalog()
  profile.p1.hat = valid_hat_id(profile.p1.hat)
  profile.p2.hat = valid_hat_id(profile.p2.hat)
  profile.online.hat = valid_hat_id(profile.online.hat)
  for _, match_profile in pairs(match_profiles) do
    match_profile.hat = valid_hat_id(match_profile.hat)
  end
end

local function use_bundled_hats()
  hats = clone_default_hats()
  rebuild_hat_index()
  clamp_profiles_to_catalog()
  hat_asset_id = LOCAL_HAT_ASSET_ID
  hat_asset_path = LOCAL_HAT_PATH
  hat_asset_sha256 = BUNDLED_HATS_SHA256
  hat_sheet = nil
  last_asset_try = 0
end

local function build_catalog_from_manifest(manifest)
  if type(manifest) ~= "table" then return nil, "manifest is not an object" end
  local schema = math.floor(tonumber(manifest.schema) or 0)
  if schema ~= 1 then return nil, "unsupported cosmetics manifest schema" end

  local assets = type(manifest.assets) == "table" and manifest.assets or {}
  local hats_asset = type(assets.hats) == "table" and assets.hats or nil
  if not hats_asset then return nil, "manifest missing assets.hats" end

  local expected_sha = lower_sha256(hats_asset.sha256 or hats_asset.signature)
  if not expected_sha then return nil, "assets.hats.sha256 must be a 64-character hex digest" end

  local cell_w = math.floor(tonumber(hats_asset.cell_w or hats_asset.cellWidth or HAT_CELL_W) or HAT_CELL_W)
  local cell_h = math.floor(tonumber(hats_asset.cell_h or hats_asset.cellHeight or HAT_CELL_H) or HAT_CELL_H)
  if cell_w ~= HAT_CELL_W or cell_h ~= HAT_CELL_H then
    return nil, "remote hats sheet must use 32x32 cells"
  end

  local list = type(manifest.hats) == "table" and manifest.hats or nil
  if not list then return nil, "manifest missing hats list" end

  local catalog = { clone_table(default_hats[1]) }
  local seen = { none = true }
  for i = 1, #list do
    local src = list[i]
    if type(src) == "table" then
      local id = clean_id(src.id)
      local sprite_index = tonumber(src.sprite_index or src.sprite or src.frame)
      if id and id ~= "none" and not seen[id] and sprite_index then
        seen[id] = true
        catalog[#catalog + 1] = {
          id = id,
          name = clean_name(src.name, id),
          sprite_index = math.max(0, math.floor(sprite_index)),
          motion = src.motion == true,
          scale = clamp(tonumber(src.scale) or 0.65, 0.35, 1.25),
          x = clamp(tonumber(src.x or src.offset_x) or 0.0, -18.0, 18.0),
          y = clamp(tonumber(src.y or src.anchor_y) or 10.5, 0.0, 30.0),
          bob = clamp(tonumber(src.bob) or 0.0, 0.0, 4.0),
          tilt = clamp(tonumber(src.tilt) or 0.0, 0.0, 20.0),
          drag = clamp(tonumber(src.drag) or 0.0, 0.0, 1.25),
          allowed_online = src.allowed_online ~= false,
        }
      end
    end
  end

  if #catalog < 2 then return nil, "manifest did not define any valid hats" end
  return {
    asset_url = resolve_cosmetic_url(hats_asset.url or (COSMETICS_ROOT .. "/assets/hats.png")),
    asset_sha256 = expected_sha,
    catalog = catalog,
    revision = tostring(manifest.revision or manifest.version or ""),
  }
end

local function apply_verified_catalog(pending)
  hats = pending.catalog
  rebuild_hat_index()
  clamp_profiles_to_catalog()
  hat_asset_id = REMOTE_HAT_ASSET_ID
  hat_asset_path = CACHE_HAT_PATH
  hat_asset_sha256 = pending.asset_sha256
  hat_sheet = nil
  last_asset_try = 0
  cosmetics_net.revision = pending.revision
end

local function schedule_cosmetics_retry(message, delay)
  cosmetics_net.status = "error"
  cosmetics_net.message = message or "official hats check failed"
  cosmetics_net.verified = false
  cosmetics_net.retry_at = os.clock() + (delay or 60.0)
  cosmetics_net.manifest_handle = nil
  cosmetics_net.asset_handle = nil
  cosmetics_net.pending = nil
  if hat_asset_id ~= REMOTE_HAT_ASSET_ID then
    use_bundled_hats()
  end
end

local function start_manifest_fetch(force)
  if cosmetics_net.status == "fetch_manifest" or cosmetics_net.status == "fetch_asset" then
    return true
  end
  if not force and os.clock() < (cosmetics_net.retry_at or 0.0) then
    return false
  end
  if not mod.http or not mod.http.get or not mod.http.poll then
    schedule_cosmetics_retry("HTTP API unavailable; using bundled hats", 60.0)
    return false
  end

  local handle, err = mod.http.get(MANIFEST_URL)
  if not handle then
    schedule_cosmetics_retry("manifest fetch failed: " .. tostring(err or "network unavailable"), 60.0)
    return false
  end

  cosmetics_net.status = "fetch_manifest"
  cosmetics_net.message = "checking official hats"
  cosmetics_net.manifest_handle = handle
  cosmetics_net.asset_handle = nil
  cosmetics_net.pending = nil
  return true
end

local function finish_manifest_fetch(body)
  local manifest, parse_err = json_decode(body)
  if not manifest then
    schedule_cosmetics_retry("manifest parse failed: " .. tostring(parse_err), 60.0)
    return
  end

  local pending, manifest_err = build_catalog_from_manifest(manifest)
  if not pending then
    schedule_cosmetics_retry("manifest rejected: " .. tostring(manifest_err), 60.0)
    return
  end

  local handle, err = mod.http.get(pending.asset_url)
  if not handle then
    schedule_cosmetics_retry("hat asset fetch failed: " .. tostring(err or "network unavailable"), 60.0)
    return
  end

  cosmetics_net.status = "fetch_asset"
  cosmetics_net.message = "verifying official hats"
  cosmetics_net.pending = pending
  cosmetics_net.asset_handle = handle
end

local function finish_asset_fetch(body)
  local pending = cosmetics_net.pending
  if not pending then
    schedule_cosmetics_retry("missing pending manifest", 60.0)
    return
  end

  local actual_sha, sha_err = sha256_hex(body)
  if not actual_sha then
    schedule_cosmetics_retry("checksum unavailable: " .. tostring(sha_err), 60.0)
    return
  end
  if actual_sha ~= pending.asset_sha256 then
    schedule_cosmetics_retry("hat asset checksum mismatch", 60.0)
    return
  end

  local ok, write_err = write_binary_file(CACHE_HAT_PATH, body)
  if not ok then
    schedule_cosmetics_retry("could not cache hats: " .. tostring(write_err), 60.0)
    return
  end

  apply_verified_catalog(pending)
  cosmetics_net.status = "verified"
  cosmetics_net.message = "official hats verified"
  cosmetics_net.verified = true
  cosmetics_net.checked_at = os.clock()
  cosmetics_net.retry_at = os.clock() + 900.0
  cosmetics_net.manifest_handle = nil
  cosmetics_net.asset_handle = nil
  cosmetics_net.pending = nil
end

local function poll_cosmetics_server()
  if cosmetics_net.status == "idle" or cosmetics_net.status == "error" then
    start_manifest_fetch(false)
  end

  if cosmetics_net.status == "fetch_manifest" and cosmetics_net.manifest_handle then
    local state, body = mod.http.poll(cosmetics_net.manifest_handle)
    if state == "pending" then return end
    cosmetics_net.manifest_handle = nil
    if state == "done" then
      finish_manifest_fetch(body)
    else
      schedule_cosmetics_retry("manifest fetch failed: " .. tostring(body or "network unavailable"), 60.0)
    end
  elseif cosmetics_net.status == "fetch_asset" and cosmetics_net.asset_handle then
    local state, body = mod.http.poll(cosmetics_net.asset_handle)
    if state == "pending" then return end
    cosmetics_net.asset_handle = nil
    if state == "done" then
      finish_asset_fetch(body)
    else
      schedule_cosmetics_retry("hat asset fetch failed: " .. tostring(body or "network unavailable"), 60.0)
    end
  end
end

local function verify_current_remote_asset()
  if hat_asset_id ~= REMOTE_HAT_ASSET_ID or not cosmetics_net.verified then
    return false, "official hats are not server-verified yet"
  end

  local data, read_err = read_binary_file(hat_asset_path)
  if not data then return false, "could not read cached hats: " .. tostring(read_err) end
  local actual_sha, sha_err = sha256_hex(data)
  if not actual_sha then return false, "checksum unavailable: " .. tostring(sha_err) end
  if actual_sha ~= hat_asset_sha256 then
    cosmetics_net.verified = false
    cosmetics_net.status = "error"
    cosmetics_net.message = "cached hats checksum mismatch"
    start_manifest_fetch(true)
    return false, "cached hats checksum mismatch"
  end

  return true
end

local function build_online_profile()
  local skin_tint, clothing_tint = selected_player_colours("online")
  return {
    schema = 1,
    manifest_url = MANIFEST_URL,
    hats_sha256 = hat_asset_sha256,
    verified = cosmetics_net.verified,
    hat = valid_online_hat_id(profile.online.hat),
    colors = {
      skin_rgba = clone_table(skin_tint),
      clothing_rgba = clone_table(clothing_tint),
    },
  }
end

local function apply_match_profile(slot, data)
  slot = profile_id(slot)
  if type(data) ~= "table" then
    match_profiles[slot] = nil
    return true
  end

  local hat_id = data.hat
  if type(data.cosmetics) == "table" and data.cosmetics.hat then
    hat_id = data.cosmetics.hat
  end
  hat_id = valid_online_hat_id(hat_id)

  local incoming_sha = lower_sha256(data.hats_sha256 or data.asset_sha256 or data.sha256)
  if incoming_sha and incoming_sha ~= hat_asset_sha256 then
    hat_id = "none"
  end

  match_profiles[slot] = {
    hat = hat_id,
    hats_sha256 = incoming_sha,
  }
  return true
end

local function gameplay_hat_for(slot)
  local match_profile = match_profiles[slot]
  if match_profile and match_profile.hat then
    return valid_hat_id(match_profile.hat)
  end
  return profile[slot] and valid_hat_id(profile[slot].hat) or "none"
end

local function ensure_assets()
  if hat_sheet then return true end

  local now = os.clock()
  if now - last_asset_try < 0.35 then
    return false
  end
  last_asset_try = now

  if not hat_sheet then
    local sheet, err = mod.assets.load_spritesheet(hat_asset_id, hat_asset_path, {
      cell_w = HAT_CELL_W,
      cell_h = HAT_CELL_H,
    })
    if sheet then
      hat_sheet = sheet
    else
      local info = mod.assets.info and mod.assets.info(hat_asset_id) or nil
      if info and info.base_id then
        hat_sheet = info
      else
        asset_error = err
        return false
      end
    end
  end

  asset_error = nil
  return true
end

local function hat_sprite(hat)
  if not hat or hat.id == "none" or not hat.sprite_index then return nil end
  if not ensure_assets() then return nil end
  return mod.assets.sprite_id(hat_asset_id, hat.sprite_index)
end

local function cosmetic_items(source_items, sprite_fn)
  local rendered_items = {}
  for i = 1, #source_items do
    local h = source_items[i]
    local item = {
      id = h.id,
      label = h.name,
      color = h.color,
    }
    local sprite = sprite_fn(h)
    if sprite then
      item.sprite = sprite
      item.sprite_opts = {
        scale = h.id == "halo" and 1.10 or 1.0,
        angle = h.motion and math.sin(os.clock() * 3.0 + i) * 4.0 or 0.0,
      }
    end
    rendered_items[#rendered_items + 1] = item
  end
  return rendered_items
end

local function hat_items()
  return cosmetic_items(hats, hat_sprite)
end

local function world_to_screen(x, y, cam)
  local sw, sh = ui.screen_size()
  local cw = tonumber(cam and cam.w) or sw
  local ch = tonumber(cam and cam.h) or sh
  if cw <= 1 then cw = sw end
  if ch <= 1 then ch = sh end
  local scale_x = sw / cw
  local scale_y = sh / ch
  local scale = math.min(scale_x, scale_y)
  local sx = sw * 0.5 + (x - (tonumber(cam and cam.x) or 0)) * scale
  local sy = sh * 0.5 + (y - (tonumber(cam and cam.y) or 0)) * scale
  return sx, sy, scale
end

local head_frame_offsets = {
  [0] = {-2.0, 0.0},  [1] = {-1.5, 0.0},  [2] = {-1.5, 0.0},  [3] = {4.5, 1.0},
  [4] = {-1.0, 0.0},  [5] = {1.5, 1.0},   [8] = {-0.5, 0.0},  [9] = {0.0, -1.0},
  [10] = {1.5, 0.0},  [16] = {-2.0, 0.0}, [17] = {1.0, 1.0},  [18] = {-0.5, -1.0},
  [19] = {1.0, 1.0},  [21] = {-0.5, 0.0}, [24] = {0.5, 0.0},  [25] = {1.5, 0.0},
  [26] = {0.5, 0.0},  [29] = {-2.0, 0.0}, [30] = {-2.0, -1.0}, [32] = {1.5, 0.0},
  [40] = {-0.5, -1.0}, [41] = {-0.5, 1.0}, [43] = {-1.5, -1.0}, [44] = {-1.0, -1.0},
  [45] = {1.0, 1.0},  [48] = {-0.5, 0.0}, [49] = {-1.0, 0.0}, [50] = {-2.0, 0.0},
  [51] = {-1.0, 0.0}, [52] = {-2.0, -1.0}, [53] = {-1.5, 0.0}, [54] = {-2.5, 1.0},
  [55] = {-2.0, 0.0}, [56] = {1.0, 1.0},  [59] = {-1.5, -1.0}, [61] = {-0.5, -1.0},
}

local idle_body_x_scale = 0.60
local head_follow_x_bias = 0.25
local head_follow_y_bias = 0.15
local head_bounce_y = 2.0
local follow_state = {}
local hat_live_state = {}
local hat_fall_state = {}
local head_bounce_state = {}

local anim_ptrs = {
  duck = 0x4262b0,
  dead = 0x41f5f0,
  eggnogg = 0x421d50,
  stun = 0x427ca0,
}

local low_pose_offsets = {
  [11] = {1.0, 6.0},  [12] = {1.4, 7.6},  [13] = {1.5, 8.0},
  [14] = {2.0, 11.6}, [15] = {2.0, 11.2}, [31] = {1.5, 11.0},
  [35] = {0.4, head_bounce_y}, [36] = {0.4, head_bounce_y}, [42] = {1.7, 8.2},  [60] = {0.8, 2.2},
  [62] = {0.8, 2.4},  [63] = {1.6, 11.6}, [64] = {1.5, 10.0},
  [65] = {0.8, 2.0},  [66] = {1.0, 7.0},
}

local duck_pose_frames = {
  [11] = true, [12] = true, [13] = true, [14] = true, [15] = true,
  [31] = true, [42] = true, [63] = true, [64] = true,
}

local prone_pose_frames = {
  [14] = true, [15] = true, [31] = true, [60] = true, [62] = true,
  [63] = true, [64] = true, [65] = true, [66] = true,
}

local head_bounce_pose_frames = {
  [35] = true, [36] = true,
}

local function has_event_bit(value, bit_value)
  value = math.floor(tonumber(value) or 0)
  return (math.floor(value / bit_value) % 2) == 1
end

local function has_head_bounce_event(player)
  return has_event_bit(player.pending_event_flags, 0x80) or
         has_event_bit(player.event_flags, 0x80) or
         has_event_bit(player.prev_event_flags, 0x80)
end

local function smooth_follow_offset(key, target_x, target_y)
  if not key then return target_x, target_y end

  local state = follow_state[key]
  if not state then
    state = { x = target_x, y = target_y }
    follow_state[key] = state
    return target_x, target_y
  end

  if math.abs(state.x - target_x) > 8.0 or math.abs(state.y - target_y) > 8.0 then
    state.x = target_x
    state.y = target_y
  else
    state.x = state.x + (target_x - state.x) * 0.45
    state.y = state.y + (target_y - state.y) * 0.45
  end

  return state.x, state.y
end

local function vanilla_sin_degrees(degrees)
  local index = math.floor(degrees * (8192.0 / 360.0) + 0.5) % 8192
  return math.sin(index * (math.pi * 2.0 / 8192.0))
end

local function vanilla_idle_body_offset(player, opts)
  local state_id = math.floor(tonumber(player.state_id) or 0)
  local vx = math.abs(tonumber(player.vx) or 0.0)
  local idle_follow = player.grounded and vx < 0.25

  if not idle_follow then return 0.0, 0.0 end
  if state_id ~= 1 and not (state_id == 10 and player.has_sword) then
    return 0.0, 0.0
  end

  local tick = tonumber(opts and opts.tick) or 0.0
  local player_index = math.floor(tonumber(player.index) or 0)
  local thing_slot = math.floor(tonumber(player.thing_slot or player.slot) or (player_index + 1))
  local wave = vanilla_sin_degrees(thing_slot * 123.0 + tick * 10.0)
  local lift = 1.0 - wave * wave
  lift = lift * lift * lift * 2.0

  return wave * idle_body_x_scale, -lift
end

local function is_idle_follow(player)
  return player.grounded and math.abs(tonumber(player.vx) or 0.0) < 0.25
end

local function should_draw_hat_for_pose(player, opts)
  return true
end

local function is_dead_pose(player)
  local state_id = math.floor(tonumber(player.state_id) or 0)
  local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
  return state_id == 8 or anim_ptr == anim_ptrs.dead
end

local function is_eggnogg_pose(player)
  local state_id = math.floor(tonumber(player.state_id) or 0)
  local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
  return state_id == 9 or anim_ptr == anim_ptrs.eggnogg
end

local function hat_state_key(player, opts)
  local key_prefix = (opts and opts.screen) and "screen" or "world"
  local slot = (opts and (opts.slot or opts.kind)) or "hat"
  return key_prefix .. ":" .. tostring(slot) .. ":" .. tostring(player.index or 0)
end

local function hat_world_solid(x, y)
  if not game.is_solid then return false end
  return game.is_solid(x, y) and true or false
end

local function hat_collides_at(x, y)
  return hat_world_solid(x, y + 2.0) or
         hat_world_solid(x - 1.5, y + 0.7) or
         hat_world_solid(x + 1.5, y + 0.7)
end

local function hat_side_collides_at(x, y)
  return hat_world_solid(x - 1.8, y - 0.4) or
         hat_world_solid(x + 1.8, y - 0.4) or
         hat_world_solid(x - 1.8, y + 0.9) or
         hat_world_solid(x + 1.8, y + 0.9)
end

local function detached_hat_blocked(state, x, y, side_only)
  if not game.is_solid then return false end

  local blocked = side_only and hat_side_collides_at(x, y) or hat_collides_at(x, y)
  if state.ignore_solid then
    if not blocked then
      if not side_only then state.ignore_solid = false end
      return false
    end
    if blocked and (state.step_y or 0.0) < 0.0 then return false end
    state.ignore_solid = false
  end

  return blocked
end

local function move_detached_hat(state)
  state.age = (state.age or 0) + 1
  state.vy = math.min((state.vy or 0.0) + 0.28, 5.5)

  local dx = state.vx or 0.0
  local dy = state.vy or 0.0
  local steps = math.max(1, math.ceil(math.max(math.abs(dx), math.abs(dy)) / 0.45))

  for _ = 1, steps do
    local step_x = dx / steps
    local step_y = dy / steps

    if step_x ~= 0.0 then
      local next_x = state.x + step_x
      state.step_y = 0.0
      if detached_hat_blocked(state, next_x, state.y, true) then
        state.vx = -state.vx * 0.46
        state.spin = (state.spin or 0.0) + state.vx * 3.0
      else
        state.x = next_x
      end
    end

    if step_y ~= 0.0 then
      local next_y = state.y + step_y
      state.step_y = step_y
      if detached_hat_blocked(state, state.x, next_y) then
        if state.vy > 0 then
          local bounce = math.abs(state.vy) * 0.46
          state.vy = bounce > 0.32 and -bounce or 0.0
          state.vx = state.vx * 0.78
          state.spin = clamp((state.spin or 0.0) * 0.28 + state.vx * 1.35, -5.0, 5.0)
          state.roll_frames = 18
        else
          state.vy = math.abs(state.vy) * 0.25
          state.spin = (state.spin or 0.0) * 0.35
        end
        break
      else
        state.y = next_y
      end
    end
  end
  state.step_y = 0.0

  if not game.is_solid and state.y >= state.ground_y then
    state.y = state.ground_y
    local bounce = math.abs(state.vy) * 0.46
    state.vy = bounce > 0.32 and -bounce or 0.0
    state.vx = state.vx * 0.78
    state.spin = clamp((state.spin or 0.0) * 0.28 + state.vx * 1.35, -5.0, 5.0)
    state.roll_frames = 18
  else
    state.vx = state.vx * 0.985
  end

  if (state.roll_frames or 0) > 0 then
    state.roll_frames = state.roll_frames - 1
    state.spin = (state.spin or 0.0) * 0.84
    if math.abs(state.spin) < 0.035 then state.spin = 0.0 end
    state.angle = state.angle + state.spin + state.vx * 1.15
  else
    state.spin = (state.spin or 0.0) * 0.985
    state.angle = state.angle + state.spin + state.vx * 7.0
  end
end

local function detached_hat_pose(key, mode, hat, player, start_x, start_y, start_angle)
  local facing = tonumber(player.facing) or 1
  local flip = facing < 0 and -1 or 1
  local state = hat_fall_state[key]
  local current_vx = tonumber(player.vx) or 0.0
  local current_vy = tonumber(player.vy) or 0.0

  if not state or state.mode ~= mode then
    local live = hat_live_state[key]
    local live_vx = live and live.vx or 0.0
    local live_vy = live and live.vy or 0.0
    local source_vx = math.abs(current_vx) > math.abs(live_vx) and current_vx or live_vx
    local source_vy = math.abs(current_vy) > math.abs(live_vy) and current_vy or live_vy
    local initial_x = live and live.x or start_x
    local initial_y = live and live.y or start_y
    state = {
      mode = mode,
      x = initial_x,
      y = initial_y,
      vx = mode == "sink" and (source_vx * 0.12 + flip * 0.20) or clamp(source_vx * 0.90 + flip * 0.35, -5.5, 5.5),
      vy = mode == "sink" and 0.40 or clamp(source_vy * 0.75 - 1.1, -5.5, 5.5),
      angle = live and live.angle or start_angle or 0.0,
      ground_y = (tonumber(player.y) or 0.0) - 1.0,
      alpha = 1.0,
      spin = clamp(source_vx * 2.8 + flip * 4.5, -18.0, 18.0),
      age = 0,
      ignore_solid = mode ~= "sink" and hat_collides_at(initial_x, initial_y) or false,
      last_player_vx = current_vx,
      last_player_vy = current_vy,
    }
    hat_fall_state[key] = state
  end

  if mode == "sink" then
    state.x = state.x + state.vx
    state.y = state.y + state.vy
    state.vx = state.vx * 0.96
    state.vy = math.min(state.vy + 0.035, 0.90)
    state.angle = state.angle + flip * 2.5
    state.alpha = math.max(0.0, state.alpha - 0.035)
  else
    local previous_player_vx = state.last_player_vx or current_vx
    local previous_player_vy = state.last_player_vy or current_vy
    local impulse_x = current_vx - previous_player_vx
    local impulse_y = current_vy - previous_player_vy
    local impulse_power = impulse_x * impulse_x + impulse_y * impulse_y
    local player_power = current_vx * current_vx + current_vy * current_vy
    local hat_power = (state.vx or 0.0) * (state.vx or 0.0) + (state.vy or 0.0) * (state.vy or 0.0)

    if impulse_power > 0.30 or (player_power > 0.65 and hat_power < player_power * 0.25) then
      local kick_x = impulse_power > 0.30 and impulse_x or current_vx
      local kick_y = impulse_power > 0.30 and impulse_y or current_vy
      state.vx = clamp((state.vx or 0.0) + kick_x * 0.72, -6.0, 6.0)
      state.vy = clamp((state.vy or 0.0) + kick_y * 0.72, -6.0, 6.0)
      state.spin = clamp((state.spin or 0.0) + kick_x * 2.0, -12.0, 12.0)
      state.roll_frames = 0
      state.ignore_solid = kick_y < -0.05 and hat_collides_at(state.x, state.y)
      state.age = 0
    end

    state.last_player_vx = current_vx
    state.last_player_vy = current_vy
    move_detached_hat(state)
  end

  return state.x, state.y, state.angle, state.alpha
end

local function duck_pose_anchor(player, frame)
  local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
  local state_id = math.floor(tonumber(player.state_id) or 0)
  local speed = math.abs(tonumber(player.vx) or 0.0)

  if prone_pose_frames[frame] then
    return {1.7, 8.7}
  end

  if anim_ptr ~= anim_ptrs.duck and state_id ~= 3 and not duck_pose_frames[frame] then
    return nil
  end

  if speed > 0.45 then
    return {1.9, 8.9}
  end
  return {1.8, 8.2}
end

local function clamp_pose_for_anim(player, x, y, low_pose)
  local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
  local state_id = math.floor(tonumber(player.state_id) or 0)

  if anim_ptr == anim_ptrs.stun or state_id == 4 then
    y = math.min(y, low_pose and -5.8 or -6.2)
  elseif anim_ptr == anim_ptrs.duck or state_id == 3 then
    x = math.max(x, low_pose and 1.8 or 1.6)
    y = math.max(y, low_pose and 8.2 or 7.6)
  end

  return x, y
end

local function reset_follow_if_detached(key)
  local fall = hat_fall_state[key]
  if fall then
    follow_state[key] = nil
  end
end

local function head_follow_offset(player, opts)
  local frame = math.floor(tonumber(player.sprite_index or player.sprite_frame) or 0)
  local head_bounce_pose = head_bounce_pose_frames[frame]
  local low_pose = duck_pose_anchor(player, frame) or low_pose_offsets[frame]
  local offset = low_pose or head_frame_offsets[frame]
  local x = offset and (offset[1] or 0.0) or 0.0
  local y = offset and (offset[2] or 0.0) or 0.0
  local vx = math.abs(tonumber(player.vx) or 0.0)
  local idle_follow = player.grounded and vx < 0.25

  local facing = tonumber(player.facing) or 1
  local flip = facing < 0 and -1 or 1
  if not idle_follow and not low_pose then
    y = 0.0
  end
  if not low_pose then
    local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
    if anim_ptr == anim_ptrs.duck then
      y = 4.8
    elseif anim_ptr == anim_ptrs.stun then
      y = 6.0
    end
  end
  x, y = clamp_pose_for_anim(player, x, y, low_pose)
  x = x * flip + head_follow_x_bias * flip
  y = y + head_follow_y_bias

  local key = hat_state_key(player, opts)
  local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
  local state_id = math.floor(tonumber(player.state_id) or 0)
  local stomp_pose = head_bounce_pose or has_head_bounce_event(player)
  local stomp_still_active = anim_ptr == anim_ptrs.stun or state_id == 4 or head_bounce_pose

  if stomp_pose then
    head_bounce_state[key] = 10
  elseif head_bounce_state[key] and head_bounce_state[key] > 0 then
    if stomp_still_active then
      head_bounce_state[key] = math.max(head_bounce_state[key], 2)
    else
      head_bounce_state[key] = head_bounce_state[key] - 1
    end
  end

  if head_bounce_state[key] and head_bounce_state[key] > 0 then
    x = (0.4 * flip) + head_follow_x_bias * flip
    y = head_bounce_y + head_follow_y_bias
    follow_state[key] = { x = x, y = y }
    return x, y
  end

  if head_bounce_pose or anim_ptr == anim_ptrs.stun or state_id == 4 then
    follow_state[key] = { x = x, y = y }
    return x, y
  end

  local smooth_x, smooth_y = smooth_follow_offset(key, x, y)
  local body_x, body_y = vanilla_idle_body_offset(player, opts)
  return smooth_x + body_x, smooth_y + body_y
end

local function draw_hat(hat_id, player, opts)
  local item = hat_by_id[valid_hat_id(hat_id)]
  local sprite = hat_sprite(item)
  if not sprite or not player then return false end

  local source_opts = opts or {}
  opts = {}
  for k, v in pairs(source_opts) do
    opts[k] = v
  end
  opts.kind = "hat"
  opts.slot = "hat"
  if not should_draw_hat_for_pose(player, opts) then return false end

  local facing = tonumber(player.facing) or 1
  local flip = facing < 0 and -1 or 1
  local vx = tonumber(player.vx) or 0
  local vy = tonumber(player.vy) or 0
  local follow_x, follow_y = head_follow_offset(player, opts)
  local idle_follow = is_idle_follow(player)
  local dead_pose = not opts.screen and is_dead_pose(player)
  local eggnogg_pose = not opts.screen and is_eggnogg_pose(player)
  local detached_mode = dead_pose and "fall" or (eggnogg_pose and "sink" or nil)
  local key = hat_state_key(player, opts)
  local drag = 0.0
  local angle = 0.0
  local alpha = 1.0
  local item_x = (item.x or 0.0) * flip
  local item_y = item.y or 14.0
  local item_scale = item.scale or 0.65

  if item.motion and not detached_mode then
    local drag_vx = idle_follow and 0.0 or vx
    drag = clamp(-drag_vx * (item.drag or 0.0), -2.0, 2.0)
    angle = clamp(-drag_vx * (item.tilt or 0.0) + vy * 0.6, -14.0, 14.0)
  end

  local draw_x
  local draw_y
  local draw_scale
  if detached_mode then
    local start_x = (tonumber(player.x) or 0) + follow_x + item_x + drag
    local start_y = (tonumber(player.y) or 0) - item_y + follow_y
    local fall_x, fall_y, fall_angle, fall_alpha = detached_hat_pose(key, detached_mode, item, player, start_x, start_y, angle)
    if fall_alpha and fall_alpha <= 0.02 then return false end
    local sx, sy, world_scale = world_to_screen(fall_x, fall_y, opts.camera)
    draw_x = sx
    draw_y = sy
    draw_scale = world_scale * item_scale * 0.5
    angle = fall_angle
    alpha = fall_alpha or 1.0
  elseif opts.screen then
    draw_x = opts.x + (follow_x + item_x + drag * 0.15) * (opts.body_scale or 1.0)
    draw_y = opts.y - (item_y * (opts.body_scale or 1.0)) + follow_y * (opts.body_scale or 1.0)
    draw_scale = (opts.body_scale or 1.0) * item_scale * 0.5
  else
    local sx, sy, world_scale = world_to_screen((tonumber(player.x) or 0) + follow_x + item_x + drag,
                                                (tonumber(player.y) or 0) - item_y + follow_y,
                                                opts.camera)
    draw_x = sx
    draw_y = sy
    draw_scale = world_scale * item_scale * 0.5
    reset_follow_if_detached(key)
    hat_fall_state[key] = nil
    hat_live_state[key] = {
      x = (tonumber(player.x) or 0) + follow_x + item_x + drag,
      y = (tonumber(player.y) or 0) - item_y + follow_y,
      vx = tonumber(player.vx) or 0.0,
      vy = tonumber(player.vy) or 0.0,
      angle = angle,
    }
  end

  return ui.draw_sprite(sprite, draw_x, draw_y, {
    scale = draw_scale,
    angle = angle,
    flip = facing < 0,
    tint = {1.0, 1.0, 1.0, alpha},
  })
end

local function draw_preview_player(cx, cy, body_scale, player_id)
  local clock = os.clock()
  local bounce = math.sin(clock * 3.2) * 2.0
  local frame = 0
  local base = ui.sprite_id and ui.sprite_id("sprites", frame)
  local clothing_layer = ui.sprite_id and ui.sprite_id("sprites", 128 + frame)
  local skin_tint, clothing_tint = selected_player_colours(player_id)

  if base then
    ui.draw_sprite(base, cx, cy + bounce, { scale = body_scale, tint = skin_tint })
  end
  if clothing_layer then
    ui.draw_sprite(clothing_layer, cx, cy + bounce, { scale = body_scale, tint = clothing_tint })
  end

  draw_hat(profile[player_id].hat, {
    index = player_index_from_id(player_id),
    sprite_index = frame,
    facing = 1,
    vx = math.sin(clock * 2.1) * 0.8,
    vy = math.cos(clock * 2.5) * 0.5,
  }, {
    screen = true,
    x = cx,
    y = cy + bounce,
    body_scale = body_scale,
    tick = clock * 60.0,
  })
end

local function draw_main_button()
  if not ui.is_state("main") then return end
  ensure_assets()

  local sw, sh = ui.screen_size()
  local icon_hat = hat_by_id.cap or hats[2]
  local sprite = hat_sprite(icon_hat)
  local size = math.max(38, math.min(52, sw * 0.04))
  local x = sw - size - 22
  local y = 22

  ui.begin_overlay()
  local clicked
  if sprite then
    clicked = ui.icon_button("official_cosmetics_open", sprite, x, y, size, size, {
      icon_scale = 0.95,
      tooltip = "Cosmetics",
    })
  else
    clicked = ui.icon_button("official_cosmetics_open", "C", x, y, size, size, {
      tooltip = asset_error or "Cosmetics",
    })
  end
  ui.end_overlay()

  if clicked then
    ui.enter_state(STATE)
  end
end

local player_tabs = {
  { id = "p1", label = "PLAYER 1" },
  { id = "p2", label = "PLAYER 2" },
  { id = "online", label = "ONLINE" },
}

local function draw_hats_state()
  ensure_assets()

  local sw, sh = ui.screen_size()
  local margin = math.max(22, math.min(46, sw * 0.035))
  local top = margin
  local bottom = sh - margin
  local left_w = math.max(260, math.min(420, sw * 0.34))
  local gap = math.max(18, sw * 0.018)
  local right_x = margin + left_w + gap
  local right_w = sw - right_x - margin
  local panel_h = bottom - top
  local header_h = 44

  ui.begin_overlay()
  ui.rect(0, 0, sw, sh, { color = {0.045, 0.048, 0.052, 0.96} })
  ui.rect(margin, top, left_w, panel_h, { color = {0.075, 0.082, 0.092, 0.96} })
  ui.border(margin, top, left_w, panel_h, { line_w = 1, color = {0.22, 0.25, 0.30, 1.0} })
  ui.rect(right_x, top, right_w, panel_h, { color = {0.070, 0.076, 0.086, 0.96} })
  ui.border(right_x, top, right_w, panel_h, { line_w = 1, color = {0.22, 0.25, 0.30, 1.0} })

  ui.text_at("COSMETICS", margin + 18, top + 31, 1.1, 0.94, 0.95, 0.96)
  if ui.button_at("official_cosmetics_back", "BACK", sw - margin - 110, top + 8, 110, 30) then
    ui.leave_state()
  end

  selected_player = select(1, ui.segmented("official_cosmetics_player", player_tabs, selected_player, {
    x = margin + 18,
    y = top + header_h + 10,
    w = left_w - 36,
    h = 30,
    text_scale = 0.78,
  }))
  set_selected_player(selected_player)

  local preview_cx = margin + left_w * 0.5
  local preview_cy = top + panel_h * 0.54
  local body_scale = math.max(4.8, math.min(7.0, left_w / 60.0))
  ui.rect(margin + 18, top + 100, left_w - 36, panel_h - 150, { color = {0.10, 0.11, 0.12, 0.82} })
  ui.border(margin + 18, top + 100, left_w - 36, panel_h - 150, { line_w = 1, color = {0.20, 0.23, 0.27, 1.0} })
  draw_preview_player(preview_cx, preview_cy, body_scale, selected_player)

  local active_hat = hat_by_id[profile[selected_player].hat] or hat_by_id.none
  ui.text_at("Hat: " .. active_hat.name, margin + 24, bottom - 34, 0.82, 0.82, 0.85, 0.88)

  ui.text_at("HATS", right_x + 18, top + 31, 1.0, 0.94, 0.95, 0.96)
  local grid_y = top + header_h + 18
  local cols = right_w > 620 and 4 or (right_w > 440 and 3 or 2)
  local cell_w = math.max(126, (right_w - 36 - (cols - 1) * 10) / cols)
  local cell_h = 92
  local new_hat = select(1, ui.item_grid("official_cosmetics_hats_" .. selected_player,
                                         hat_items(),
                                         profile[selected_player].hat,
                                         {
                                           x = right_x + 18,
                                           y = grid_y,
                                           cols = cols,
                                           cell_w = cell_w,
                                           cell_h = cell_h,
                                           gap = 10,
                                           text_scale = 0.70,
                                         }))
  if new_hat ~= profile[selected_player].hat then
    save_hat(selected_player, new_hat)
  end

  ui.end_overlay()
end

local function draw_gameplay_hats()
  local state_name = ui.state_name()
  if state_name == STATE or
     state_name == "options" or
     state_name == "options_paused" or
     state_name == "mods" then
    return
  end
  if not ensure_assets() then return end

  local snap = game.snapshot(0, false)
  if not snap or snap.error or not snap.player then return end
  local cam = game.camera and game.camera() or nil

  ui.begin_overlay()
  local p0 = snap.player
  local p1 = snap.enemy
  if p0 then
    local opts = { camera = cam, tick = snap.native_tick or snap.tick or os.clock() * 60.0 }
    draw_hat(gameplay_hat_for("p1"), p0, opts)
  end
  if p1 then
    local opts = { camera = cam, tick = snap.native_tick or snap.tick or os.clock() * 60.0 }
    draw_hat(gameplay_hat_for("p2"), p1, opts)
  end
  ui.end_overlay()
end

local function validate_for_online()
  poll_cosmetics_server()

  local checking = cosmetics_net.status == "fetch_manifest" or cosmetics_net.status == "fetch_asset"
  if checking then
    return false, cosmetics_net.message
  end

  local now = os.clock()
  if not cosmetics_net.verified or now - (cosmetics_net.checked_at or 0.0) > 15.0 then
    start_manifest_fetch(true)
    return false, "checking official hats"
  end

  local ok, reason = verify_current_remote_asset()
  if not ok then
    start_manifest_fetch(true)
    return false, reason
  end

  return true, "official hats verified"
end

if mod.interop and mod.interop.provide then
  mod.interop.provide("official_cosmetics:api", "1.0.0", {
    status = function()
      return {
        status = cosmetics_net.status,
        message = cosmetics_net.message,
        verified = cosmetics_net.verified,
        manifest_url = MANIFEST_URL,
        hats_sha256 = hat_asset_sha256,
        revision = cosmetics_net.revision,
      }
    end,
    refresh = function()
      return start_manifest_fetch(true)
    end,
    validate_for_online = function()
      local ok, reason = validate_for_online()
      return ok, {
        message = reason,
        status = cosmetics_net.status,
        verified = cosmetics_net.verified,
        manifest_url = MANIFEST_URL,
        hats_sha256 = hat_asset_sha256,
      }
    end,
    get_online_profile = build_online_profile,
    set_match_profile = apply_match_profile,
    clear_match_profiles = function()
      match_profiles = {}
      return true
    end,
  })
end

if storage.schema and storage.schema() < 3 then
  storage.set_schema(3)
end

if ui.define_state then
  ui.define_state(STATE, {
    render = draw_hats_state,
    event = function(e)
      if e.type == "keydown" and e.sym == SDLK_ESCAPE then
        ui.leave_state()
        return true
      end
      return true
    end,
  })
else
  ui.create_state(STATE)
end

mod.on_frame(function()
  poll_cosmetics_server()
  draw_main_button()
  draw_gameplay_hats()
end)
