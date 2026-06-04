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
local CHARACTER_MANIFEST_PATH = "characters/manifest.json"
local CHARACTER_PACK_PATHS = { "characters/knight/character.json" }
local CHARACTER_CACHE_DIR = CACHE_DIR .. "/characters"
local CHARACTER_TARGET_W = 16
local CHARACTER_TARGET_H = 16
local CHARACTER_MAX_BYTES = 1048576
local HAT_CELL_W = 32
local HAT_CELL_H = 32
local BUNDLED_HATS_SHA256 = "3bbc38cdc3b540ea337e75a4800d84c47c5c199e456cfca5312c1bf662ca0245"
ONLINE_COSMETICS_ENABLED = false

local selected_player = storage.get("selected_player", "p1")
local online_color_source = storage.get("online_color_source", "p1")
local profile = {
  p1 = {
    hat = storage.get("p1_hat", "none"),
    character = storage.get("p1_character", "default"),
  },
  p2 = {
    hat = storage.get("p2_hat", "none"),
    character = storage.get("p2_character", "default"),
  },
  online = {
    hat = storage.get("online_hat", storage.get("p1_hat", "none")),
    character = storage.get("online_character", storage.get("p1_character", "default")),
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
local characters = {
  {
    id = "default",
    name = "Default",
    builtin = true,
    color = {0.16, 0.18, 0.21, 1.0},
  },
}
local character_by_id = { default = characters[1] }
local remote_characters_by_sha = {}
local remote_asset_revision_applied = nil
local valid_character_id
local custom_hat_follow_offset
local custom_sword_idle_offset
local character_render_hidden = {}
local character_choice_state = {}
local character_import = {
  defs = {},
  message = nil,
  message_until = 0.0,
}

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

function character_import.read_abs(path)
  local f, err = io.open(path, "rb")
  if not f then return nil, err or "open failed" end
  local data = f:read("*a")
  f:close()
  return data
end

function character_import.dirname(path)
  path = tostring(path or ""):gsub("\\", "/")
  return path:match("^(.*)/[^/]*$") or ""
end

function character_import.join(base, rel)
  base = tostring(base or ""):gsub("\\", "/")
  rel = tostring(rel or ""):gsub("\\", "/")
  if base:sub(-1) == "/" then return base .. rel end
  return base .. "/" .. rel
end

function character_import.ext(path)
  return tostring(path or ""):match("%.([%w]+)$")
end

function character_import.set_message(message)
  character_import.message = tostring(message or "")
  character_import.message_until = os.clock() + 5.0
end

local function ensure_cache_dir()
  if not mod.get_path then return false, "mod.get_path unavailable" end
  local dir = mod.get_path(CACHE_DIR)
  dir = tostring(dir or ""):gsub('"', "")
  if dir == "" then return false, "cache path unavailable" end
  os.execute('mkdir "' .. dir .. '" >nul 2>nul')
  return true
end

local function ensure_cache_subdir(rel_dir)
  local ok, err = ensure_cache_dir()
  if not ok then return false, err end
  if not mod.get_path then return false, "mod.get_path unavailable" end
  local dir = mod.get_path(rel_dir)
  dir = tostring(dir or ""):gsub('"', "")
  if dir == "" then return false, "cache path unavailable" end
  os.execute('mkdir "' .. dir .. '" >nul 2>nul')
  return true
end

local function write_binary_file(rel_path, data)
  local rel_dir = tostring(rel_path or ""):match("^(.*)/[^/]+$")
  local ok, err
  if rel_dir and rel_dir ~= "" then
    ok, err = ensure_cache_subdir(rel_dir)
  else
    ok, err = ensure_cache_dir()
  end
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

local function online_color_source_id()
  online_color_source = online_color_source == "p2" and "p2" or "p1"
  return online_color_source
end

local function toggle_online_color_source()
  online_color_source = online_color_source_id() == "p1" and "p2" or "p1"
  storage.set("online_color_source", online_color_source)
end

local function selected_player_colours(player_id)
  local source_id = player_id == "online" and online_color_source_id() or (player_id == "p2" and "p2" or "p1")
  local player_index = player_index_from_id(source_id)
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
         rgba_or_fallback(clothing, fallback_clothing[source_id] or fallback_clothing.p1)
end

local online_colour_restore = nil
local colour_indices_applied = false

local function player_colour_index_getter()
  return game.player_color_index or game.player_colour_index
end

local function player_colour_index_setter()
  return game.set_player_color_index or game.set_player_colour_index
end

local function player_colour_getter()
  return game.player_color or game.player_colour
end

local function player_render_colour_setter()
  return game.set_player_render_colors or game.set_player_render_colours
end

local function refresh_player_render_colours(player_index)
  local colour_getter = player_colour_getter()
  local render_setter = player_render_colour_setter()
  if not colour_getter or not render_setter then return end
  local skin = colour_getter(player_index, 0)
  local clothing = colour_getter(player_index, 1)
  render_setter(player_index, skin, clothing)
end

local function read_player_colour_indices(player_id)
  local source_id = player_id == "p2" and "p2" or "p1"
  local getter = player_colour_index_getter()
  local player_index = player_index_from_id(source_id)
  if getter then
    return {
      skin = math.floor(tonumber(getter(player_index, 0)) or 0),
      clothing = math.floor(tonumber(getter(player_index, 1)) or 0),
    }
  end
  return { skin = 0, clothing = 0 }
end

local function selected_player_colour_indices(player_id)
  local source_id = player_id == "online" and online_color_source_id() or (player_id == "p2" and "p2" or "p1")
  if online_colour_restore and online_colour_restore[source_id] then
    return clone_table(online_colour_restore[source_id])
  end
  return read_player_colour_indices(source_id)
end

local function clamped_saved_hat_id(player_id)
  local current = profile[player_id] and profile[player_id].hat or "none"
  local stored = storage.get(player_id .. "_hat", current)
  local stored_id = valid_hat_id(stored)
  if stored_id ~= "none" then return stored_id end
  return valid_hat_id(current)
end

profile.p1.hat = clamped_saved_hat_id("p1")
profile.p2.hat = clamped_saved_hat_id("p2")
profile.online.hat = clamped_saved_hat_id("online")
selected_player = (selected_player == "p2" or selected_player == "online") and selected_player or "p1"
online_color_source = online_color_source == "p2" and "p2" or "p1"

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

local function save_character(player_id, character_id)
  player_id = profile_id(player_id)
  character_id = valid_character_id(character_id)
  profile[player_id].character = character_id
  storage.set(player_id .. "_character", character_id)
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
  required_sha256 = nil,
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

local function clean_rel_path(value)
  if type(value) ~= "string" then return nil end
  local path = value:gsub("\\", "/"):gsub("[%c]", "")
  if path == "" or #path > 180 then return nil end
  if path:sub(1, 1) == "/" or path:match("^%a:") or path:find("%.%.", 1, true) then return nil end
  return path
end

local function rebuild_character_index()
  character_by_id = {}
  for i = 1, #characters do
    character_by_id[characters[i].id] = characters[i]
  end
end

function valid_character_id(id)
  return character_by_id[id] and id or "default"
end

local function character_asset_id(prefix, id, sha)
  local clean = clean_id(id) or "custom"
  local suffix = lower_sha256(sha or "") or string.rep("0", 64)
  local max_clean = 63 - #prefix - 1 - 12
  if max_clean < 8 then max_clean = 8 end
  clean = clean:sub(1, max_clean)
  return prefix .. clean .. "_" .. suffix:sub(1, 12)
end

function character_import.normalize_hex_color(value)
  if type(value) == "number" then
    local n = clamp(math.floor(value), 0, 0xFFFFFF)
    return string.format("#%06X", n)
  end

  if type(value) == "table" then
    local r = tonumber(value.r or value[1])
    local g = tonumber(value.g or value[2])
    local b = tonumber(value.b or value[3])
    if not r or not g or not b then return nil end
    if r <= 1.0 and g <= 1.0 and b <= 1.0 then
      r, g, b = r * 255.0, g * 255.0, b * 255.0
    end
    return string.format("#%02X%02X%02X",
                         clamp(math.floor(r + 0.5), 0, 255),
                         clamp(math.floor(g + 0.5), 0, 255),
                         clamp(math.floor(b + 0.5), 0, 255))
  end

  if type(value) ~= "string" then return nil end
  local hex = value:gsub("^%s+", ""):gsub("%s+$", "")
  hex = hex:gsub("^#", ""):gsub("^0x", ""):gsub("^0X", "")
  if #hex == 3 and hex:match("^[0-9a-fA-F]+$") then
    hex = hex:sub(1, 1) .. hex:sub(1, 1) ..
          hex:sub(2, 2) .. hex:sub(2, 2) ..
          hex:sub(3, 3) .. hex:sub(3, 3)
  end
  if #hex ~= 6 or not hex:match("^[0-9a-fA-F]+$") then return nil end
  return "#" .. hex:upper()
end

function character_import.append_mask_color(out, color)
  color = character_import.normalize_hex_color(color)
  if not color then return false end
  for i = 1, #out do
    if out[i] == color then return true end
  end
  if #out >= 128 then return false end
  out[#out + 1] = color
  return true
end

function character_import.append_mask_colors(out, value)
  if value == nil then return end
  if type(value) == "table" and (value.colors ~= nil or value.colours ~= nil) then
    value = value.colors or value.colours
  end
  if type(value) == "table" and #value > 0 then
    for i = 1, #value do
      character_import.append_mask_colors(out, value[i])
    end
    return
  end
  character_import.append_mask_color(out, value)
end

function character_import.normalize_color_masks(src)
  local input = type(src) == "table" and
                (src.color_masks or src.colour_masks or src.tint_masks or
                 src.palette or src.recolor or src.recolour) or nil
  if type(input) ~= "table" then return nil end

  local masks = {
    skin = {},
    clothing = {},
  }

  character_import.append_mask_colors(masks.skin, input.skin or input.body)
  character_import.append_mask_colors(masks.clothing, input.clothing or input.clothes or input.armor or input.armour)

  for i = 1, #input do
    local entry = input[i]
    if type(entry) == "table" then
      local target = tostring(entry.target or entry.layer or entry.slot or ""):lower()
      local colors = entry.colors or entry.colours or entry.color or entry.colour or entry.from
      if target == "skin" or target == "body" then
        character_import.append_mask_colors(masks.skin, colors)
      elseif target == "clothing" or target == "clothes" or target == "armor" or target == "armour" then
        character_import.append_mask_colors(masks.clothing, colors)
      end
    end
  end

  if #masks.skin == 0 then masks.skin = nil end
  if #masks.clothing == 0 then masks.clothing = nil end
  if not masks.skin and not masks.clothing then return nil end
  return masks
end

function character_import.color_mask_key(masks)
  if type(masks) ~= "table" then return nil end
  local parts = {}
  for _, layer in ipairs({"skin", "clothing"}) do
    local colors = masks[layer]
    if type(colors) == "table" and #colors > 0 then
      parts[#parts + 1] = layer .. ":" .. table.concat(colors, ",")
    end
  end
  if #parts == 0 then return nil end
  return sha256_hex(table.concat(parts, ";")) or table.concat(parts, "_"):gsub("[^%w_%-%.]", "_"):sub(1, 64)
end

local function normalize_frame_list(value)
  local frames = {}
  if type(value) ~= "table" then return frames end
  for i = 1, #value do
    local frame = tonumber(value[i])
    if frame then
      frames[#frames + 1] = clamp(math.floor(frame), 0, 4095)
      if #frames >= 240 then break end
    end
  end
  return frames
end

local function normalize_frame_sequence(value)
  if type(value) ~= "table" then return {}, nil end

  if type(value.choose) == "table" then
    local choices = {}
    for i = 1, #value.choose do
      local entry = value.choose[i]
      local frames
      if type(entry) == "table" then
        frames = normalize_frame_list(entry)
      else
        local frame = tonumber(entry)
        frames = frame and {clamp(math.floor(frame), 0, 4095)} or {}
      end
      if #frames > 0 then
        choices[#choices + 1] = frames
        if #choices >= 64 then break end
      end
    end
    if #choices > 0 then return choices[1], choices end
    return {}, nil
  end

  return normalize_frame_list(value), nil
end

local function normalize_animations(src)
  local animations = {}
  local default_fps = clamp(tonumber(src.fps or src.frame_rate) or 10.0, 1.0, 60.0)
  local source_anims = type(src.animations) == "table" and src.animations or nil
  if source_anims then
    for name, anim in pairs(source_anims) do
      local clean_anim = clean_id(tostring(name or ""))
      if clean_anim and type(anim) == "table" then
        local frames, choices = normalize_frame_sequence(anim.frames or anim)
        if #frames > 0 then
          animations[clean_anim] = {
            frames = frames,
            choices = choices,
            fps = clamp(tonumber(anim.fps or anim.frame_rate) or default_fps, 1.0, 60.0),
            loop = anim.loop ~= false,
          }
        end
      end
    end
  end
  if not animations.idle then
    local frames, choices = normalize_frame_sequence(src.frames)
    animations.idle = {
      frames = #frames > 0 and frames or {0},
      choices = choices,
      fps = default_fps,
      loop = true,
    }
  end
  return animations
end

local function normalize_frame_map(src, animations)
  local out = {}
  local frame_map = type(src.frame_map) == "table" and src.frame_map or nil
  if not frame_map then return out end
  for frame, anim in pairs(frame_map) do
    local frame_index = tonumber(frame)
    local anim_id = clean_id(tostring(anim or ""))
    if frame_index and anim_id and animations[anim_id] then
      out[tostring(math.floor(frame_index))] = anim_id
    end
  end
  return out
end

character_import.anchor_anim_names = {"idle", "run", "walk", "jump", "fall", "duck", "crouch", "prone", "stun", "dead", "eggnogg"}

function character_import.normalize_anchor_frame_entry(value)
  if type(value) ~= "table" then return nil end
  local x = tonumber(value.x or value[1]) or 0.0
  local y = tonumber(value.y or value[2]) or 0.0
  return {
    x = clamp(x, -16.0, 16.0),
    y = clamp(y, -16.0, 16.0),
    bob_x = clamp(tonumber(value.bob_x) or 0.0, -8.0, 8.0),
    bob_y = clamp(tonumber(value.bob_y) or 0.0, -8.0, 8.0),
  }
end

function character_import.normalize_anchor_frame_list(value)
  if type(value) ~= "table" or #value == 0 then return nil end
  local frames = {}
  for i = 1, #value do
    local frame = character_import.normalize_anchor_frame_entry(value[i])
    if frame then
      frames[#frames + 1] = frame
      if #frames >= 240 then break end
    end
  end
  return #frames > 0 and frames or nil
end

function character_import.normalize_anchor_frame_map(value)
  if type(value) ~= "table" or #value > 0 then return nil end
  local out = {}
  local count = 0
  for frame, entry in pairs(value) do
    local frame_index = tonumber(frame)
    local motion = character_import.normalize_anchor_frame_entry(entry)
    if frame_index and motion then
      out[tostring(math.floor(frame_index))] = motion
      count = count + 1
      if count >= 240 then break end
    end
  end
  return count > 0 and out or nil
end

function character_import.normalize_anchor_motion(input, fallback)
  fallback = fallback or {}
  if type(input) ~= "table" then return nil end

  local frames = character_import.normalize_anchor_frame_list(input.frames or input.frame_offsets or input.offsets)
  local frame_map = character_import.normalize_anchor_frame_map(input.frame_map or input.by_frame or input.frames_by_index)
  if not frame_map and type(input.frames) == "table" then
    frame_map = character_import.normalize_anchor_frame_map(input.frames)
  end

  return {
    x = clamp(tonumber(input.x) or 0.0, -16.0, 16.0),
    y = clamp(tonumber(input.y) or 0.0, -16.0, 16.0),
    bob_x = clamp(tonumber(input.bob_x or input.idle_bob_x) or 0.0, -8.0, 8.0),
    bob_y = clamp(tonumber(input.bob_y or input.idle_bob_y) or 0.0, -8.0, 8.0),
    fps = clamp(tonumber(input.fps) or fallback.fps or 12.0, 1.0, 60.0),
    phase = tonumber(input.phase) or fallback.phase or 0.0,
    frames = frames,
    frame_map = frame_map,
  }
end

function character_import.normalize_anchor_table(input)
  if type(input) ~= "table" then return nil end

  local out = character_import.normalize_anchor_motion(input, { fps = 12.0, phase = 0.0 })
  if not out then return nil end

  for _, name in ipairs(character_import.anchor_anim_names) do
    local motion = character_import.normalize_anchor_motion(input[name], out)
    if motion then
      out[name] = motion
    end
  end

  return out
end

function character_import.normalize_hat_anchor(src)
  local input = type(src.hat_anchor) == "table" and src.hat_anchor or nil
  if not input and type(src.head_anchor) == "table" then input = src.head_anchor end
  return character_import.normalize_anchor_table(input)
end

function character_import.normalize_sword_anchor(src)
  local input = type(src.sword_anchor) == "table" and src.sword_anchor or nil
  if not input and type(src.sword_idle) == "table" then input = src.sword_idle end
  return character_import.normalize_anchor_table(input)
end

local function normalize_character_definition(src, source, cached_path)
  if type(src) ~= "table" then return nil, "character is not an object" end
  local id = clean_id(src.id)
  if not id or id == "default" then return nil, "invalid character id" end
  local sheet_path = cached_path or clean_rel_path(src.sheet or src.spritesheet or src.path)
  if not sheet_path then return nil, "missing character sheet" end

  local cell_w = clamp(math.floor(tonumber(src.cell_w or src.frame_w or src.cellWidth) or 16), 1, 512)
  local cell_h = clamp(math.floor(tonumber(src.cell_h or src.frame_h or src.cellHeight) or 16), 1, 512)
  local target_w = clamp(tonumber(src.target_w or src.width or CHARACTER_TARGET_W) or CHARACTER_TARGET_W, 4.0, 32.0)
  local target_h = clamp(tonumber(src.target_h or src.height or CHARACTER_TARGET_H) or CHARACTER_TARGET_H, 4.0, 32.0)
  local padding = clamp(math.floor(tonumber(src.padding) or 0), 0, 64)
  local sheet_sha = lower_sha256(src.sheet_sha256 or src.sha256 or src.asset_sha256)
  local sheet_data = nil

  if source == "local" then
    sheet_data = read_binary_file(sheet_path)
    if not sheet_data then return nil, "missing character sheet" end
    if #sheet_data > CHARACTER_MAX_BYTES then return nil, "character sheet is too large" end
    sheet_sha = sha256_hex(sheet_data)
  elseif not sheet_sha then
    return nil, "remote character missing sheet checksum"
  end
  if not sheet_sha then return nil, "character sheet checksum unavailable" end

  local animations = normalize_animations(src)
  local color_masks = character_import.normalize_color_masks(src)
  local color_mask_key = character_import.color_mask_key(color_masks)
  local asset_key = color_mask_key and (sha256_hex(sheet_sha .. ":" .. color_mask_key) or sheet_sha) or sheet_sha
  return {
    id = id,
    name = clean_name(src.name, id),
    sheet = sheet_path,
    sheet_sha256 = sheet_sha,
    sheet_bytes = sheet_data,
    source = source or "local",
    asset_key = asset_key,
    asset_id = character_asset_id(source == "remote" and "remote_character_" or "character_", id, asset_key),
    cell_w = cell_w,
    cell_h = cell_h,
    padding = padding,
    target_w = target_w,
    target_h = target_h,
    offset_x = clamp(tonumber(src.offset_x or src.x) or 0.0, -16.0, 16.0),
    offset_y = clamp(tonumber(src.offset_y or src.y) or 0.0, -16.0, 16.0),
    fps = clamp(tonumber(src.fps or src.frame_rate) or 10.0, 1.0, 60.0),
    animations = animations,
    frame_map = normalize_frame_map(src, animations),
    hat_anchor = character_import.normalize_hat_anchor(src),
    sword_anchor = character_import.normalize_sword_anchor(src),
    color_masks = color_masks,
    color_mask_key = color_mask_key,
    allowed_online = src.allowed_online ~= false,
    color = {0.14, 0.18, 0.24, 1.0},
  }
end

local function character_entries_from_document(src)
  if type(src) ~= "table" then return {} end
  if type(src.characters) == "table" then return src.characters end
  return { src }
end

local function character_entry_with_sheet_base(src, base_dir)
  if type(src) ~= "table" then return src end
  if type(base_dir) ~= "string" or base_dir == "" then return src end
  local sheet_rel = clean_rel_path(src.sheet or src.spritesheet or src.path)
  if not sheet_rel then return src end

  local out = clone_table(src)
  out.sheet = character_import.join(base_dir, sheet_rel)
  out.spritesheet = nil
  out.path = nil
  return out
end

local function add_or_replace_character(character)
  if not character or not character.id then return false end
  for i = 1, #characters do
    if characters[i].id == character.id then
      characters[i] = character
      rebuild_character_index()
      return true
    end
  end
  characters[#characters + 1] = character
  rebuild_character_index()
  return true
end

local function load_character_catalog()
  characters = { characters[1] }
  rebuild_character_index()
  character_import.defs = {}

  local body = read_binary_file(CHARACTER_MANIFEST_PATH)
  if body then
    local manifest = json_decode(body)
    if type(manifest) == "table" and math.floor(tonumber(manifest.schema) or 0) == 1 then
      local list = character_entries_from_document(manifest)
      for i = 1, #list do
        local character = normalize_character_definition(list[i], "local")
        if character then
          add_or_replace_character(character)
        end
      end
    end
  end

  for p = 1, #CHARACTER_PACK_PATHS do
    local bundled_json = CHARACTER_PACK_PATHS[p]
    local bundled_body = read_binary_file(bundled_json)
    local bundled_manifest = type(bundled_body) == "string" and json_decode(bundled_body) or nil
    local bundled_base = character_import.dirname(bundled_json)
    local list = character_entries_from_document(bundled_manifest)
    for i = 1, #list do
      local src = character_entry_with_sheet_base(list[i], bundled_base)
      local character = normalize_character_definition(src, "local")
      if character then
        add_or_replace_character(character)
      end
    end
  end

  local imported_body = storage.get("imported_characters_json", "")
  local imported_manifest = type(imported_body) == "string" and json_decode(imported_body) or nil
  local imported_list = character_entries_from_document(imported_manifest)
  for i = 1, #imported_list do
    local character = normalize_character_definition(imported_list[i], "local")
    if character then
      add_or_replace_character(character)
      character_import.defs[#character_import.defs + 1] = clone_table(imported_list[i])
    end
  end
  return true
end

local function clamped_saved_character_id(player_id)
  local current = profile[player_id] and profile[player_id].character or "default"
  local stored = storage.get(player_id .. "_character", current)
  local stored_id = valid_character_id(stored)
  if stored_id ~= "default" then return stored_id end
  return valid_character_id(current)
end

load_character_catalog()
profile.p1.character = clamped_saved_character_id("p1")
profile.p2.character = clamped_saved_character_id("p2")
profile.online.character = clamped_saved_character_id("online")

local function clone_default_hats()
  local cloned = {}
  for i = 1, #default_hats do
    cloned[i] = clone_table(default_hats[i])
  end
  return cloned
end

local function clamp_profiles_to_catalog()
  profile.p1.hat = clamped_saved_hat_id("p1")
  profile.p2.hat = clamped_saved_hat_id("p2")
  profile.online.hat = clamped_saved_hat_id("online")
  profile.p1.character = clamped_saved_character_id("p1")
  profile.p2.character = clamped_saved_character_id("p2")
  profile.online.character = clamped_saved_character_id("online")
  for _, match_profile in pairs(match_profiles) do
    match_profile.hat = valid_hat_id(match_profile.hat)
    match_profile.character = valid_character_id(match_profile.character)
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

local function mark_catalog_verified(pending, message)
  apply_verified_catalog(pending)
  cosmetics_net.status = "verified"
  cosmetics_net.message = message or "official hats verified"
  cosmetics_net.verified = true
  cosmetics_net.checked_at = os.clock()
  cosmetics_net.retry_at = os.clock() + 900.0
  cosmetics_net.required_sha256 = nil
  cosmetics_net.manifest_handle = nil
  cosmetics_net.asset_handle = nil
  cosmetics_net.pending = nil
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

local function cached_asset_matches(expected_sha)
  expected_sha = lower_sha256(expected_sha)
  if not expected_sha then return false, "missing expected checksum" end

  local data, read_err = read_binary_file(CACHE_HAT_PATH)
  if not data then return false, "cached hats missing: " .. tostring(read_err or "open failed") end

  local actual_sha, sha_err = sha256_hex(data)
  if not actual_sha then return false, "checksum unavailable: " .. tostring(sha_err) end
  if actual_sha ~= expected_sha then return false, "cached hats checksum mismatch" end
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
  if cosmetics_net.required_sha256 and pending.asset_sha256 ~= cosmetics_net.required_sha256 then
    schedule_cosmetics_retry("manifest hats checksum does not match online profile", 60.0)
    return
  end

  if cached_asset_matches(pending.asset_sha256) then
    mark_catalog_verified(pending, "official hats verified from cache")
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

  mark_catalog_verified(pending, "official hats verified")
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

local function request_hat_asset_sha(expected_sha)
  expected_sha = lower_sha256(expected_sha)
  if not expected_sha or expected_sha == hat_asset_sha256 then return true end

  cosmetics_net.required_sha256 = expected_sha
  if cosmetics_net.status ~= "fetch_manifest" and cosmetics_net.status ~= "fetch_asset" then
    cosmetics_net.status = "error"
    cosmetics_net.message = "fetching missing official hats"
    cosmetics_net.retry_at = 0.0
    start_manifest_fetch(true)
  end
  return false
end

local function json_escape_string(value)
  value = tostring(value or "")
  local escaped = value:gsub('[%z\1-\31\\"]', function(c)
    if c == '"' then return '\\"' end
    if c == "\\" then return "\\\\" end
    if c == "\b" then return "\\b" end
    if c == "\f" then return "\\f" end
    if c == "\n" then return "\\n" end
    if c == "\r" then return "\\r" end
    if c == "\t" then return "\\t" end
    return string.format("\\u%04x", c:byte())
  end)
  return '"' .. escaped .. '"'
end

local function json_number(value, fallback)
  local n = tonumber(value) or fallback or 0.0
  n = clamp(n, 0.0, 1.0)
  return string.format("%.4f", n)
end

local function json_integer(value, fallback)
  return tostring(math.floor(tonumber(value) or fallback or 0))
end

local function sorted_keys(t)
  local keys = {}
  if type(t) ~= "table" then return keys end
  for k in pairs(t) do
    keys[#keys + 1] = tostring(k)
  end
  table.sort(keys)
  return keys
end

local function json_number_raw(value, fallback)
  local n = tonumber(value) or fallback or 0.0
  return string.format("%.4f", n)
end

local function json_frame_array(frames)
  local out = {}
  frames = type(frames) == "table" and frames or {}
  for i = 1, #frames do
    out[#out + 1] = json_integer(frames[i], 0)
  end
  return "[" .. table.concat(out, ",") .. "]"
end

local function json_animation_frames(anim)
  if type(anim) == "table" and type(anim.choices) == "table" and #anim.choices > 0 then
    local out = {}
    for i = 1, #anim.choices do
      local frames = anim.choices[i]
      if type(frames) == "table" and #frames == 1 then
        out[#out + 1] = json_integer(frames[1], 0)
      else
        out[#out + 1] = json_frame_array(frames)
      end
    end
    return '{"choose":[' .. table.concat(out, ",") .. "]}"
  end
  return json_frame_array(anim and anim.frames)
end

function character_import.json_color_masks(masks)
  if type(masks) ~= "table" then return "null" end
  local parts = {}
  for _, layer in ipairs({"skin", "clothing"}) do
    local colors = masks[layer]
    if type(colors) == "table" and #colors > 0 then
      local color_parts = {}
      for i = 1, #colors do
        color_parts[#color_parts + 1] = json_escape_string(colors[i])
      end
      parts[#parts + 1] = json_escape_string(layer) .. ":[" .. table.concat(color_parts, ",") .. "]"
    end
  end
  if #parts == 0 then return "null" end
  return "{" .. table.concat(parts, ",") .. "}"
end

function character_import.json_anchor_frame_entry(frame)
  if type(frame) ~= "table" then return "null" end
  return "{" ..
    '"x":' .. json_number_raw(frame.x, 0.0) .. "," ..
    '"y":' .. json_number_raw(frame.y, 0.0) .. "," ..
    '"bob_x":' .. json_number_raw(frame.bob_x, 0.0) .. "," ..
    '"bob_y":' .. json_number_raw(frame.bob_y, 0.0) ..
  "}"
end

function character_import.json_anchor_frames(frames)
  if type(frames) ~= "table" or #frames == 0 then return nil end
  local parts = {}
  for i = 1, #frames do
    parts[#parts + 1] = character_import.json_anchor_frame_entry(frames[i])
  end
  return "[" .. table.concat(parts, ",") .. "]"
end

function character_import.json_anchor_frame_map(frame_map)
  if type(frame_map) ~= "table" then return nil end
  local parts = {}
  for _, frame in ipairs(sorted_keys(frame_map)) do
    parts[#parts + 1] = json_escape_string(frame) .. ":" .. character_import.json_anchor_frame_entry(frame_map[frame])
  end
  if #parts == 0 then return nil end
  return "{" .. table.concat(parts, ",") .. "}"
end

function character_import.json_anchor_motion(anchor, fallback)
  if type(anchor) ~= "table" then return "null" end
  fallback = fallback or {}
  local parts = {
    '"x":' .. json_number_raw(anchor.x, 0.0),
    '"y":' .. json_number_raw(anchor.y, 0.0),
    '"bob_x":' .. json_number_raw(anchor.bob_x, 0.0),
    '"bob_y":' .. json_number_raw(anchor.bob_y, 0.0),
    '"fps":' .. json_number_raw(anchor.fps, fallback.fps or 12.0),
    '"phase":' .. json_number_raw(anchor.phase, fallback.phase or 0.0),
  }
  local frames = character_import.json_anchor_frames(anchor.frames)
  if frames then parts[#parts + 1] = '"frames":' .. frames end
  local frame_map = character_import.json_anchor_frame_map(anchor.frame_map)
  if frame_map then parts[#parts + 1] = '"frame_map":' .. frame_map end
  return "{" .. table.concat(parts, ",") .. "}"
end

function character_import.json_hat_anchor(anchor)
  if type(anchor) ~= "table" then return "null" end

  local parts = {
    '"x":' .. json_number_raw(anchor.x, 0.0),
    '"y":' .. json_number_raw(anchor.y, 0.0),
    '"bob_x":' .. json_number_raw(anchor.bob_x, 0.0),
    '"bob_y":' .. json_number_raw(anchor.bob_y, 0.0),
    '"fps":' .. json_number_raw(anchor.fps, 12.0),
    '"phase":' .. json_number_raw(anchor.phase, 0.0),
  }
  local frames = character_import.json_anchor_frames(anchor.frames)
  if frames then parts[#parts + 1] = '"frames":' .. frames end
  local frame_map = character_import.json_anchor_frame_map(anchor.frame_map)
  if frame_map then parts[#parts + 1] = '"frame_map":' .. frame_map end

  for _, name in ipairs(sorted_keys(anchor)) do
    local motion = anchor[name]
    if type(motion) == "table" and
       name ~= "frames" and name ~= "frame_map" and
       name ~= "x" and name ~= "y" and
       name ~= "bob_x" and name ~= "bob_y" and
       name ~= "fps" and name ~= "phase" then
      parts[#parts + 1] = json_escape_string(name) .. ":" .. character_import.json_anchor_motion(motion, anchor)
    end
  end

  return "{" .. table.concat(parts, ",") .. "}"
end

local function json_rgba_array(rgba, fallback)
  rgba = rgba_or_fallback(rgba, fallback or fallback_skin)
  return "[" ..
    json_number(rgba[1], 1.0) .. "," ..
    json_number(rgba[2], 1.0) .. "," ..
    json_number(rgba[3], 1.0) .. "," ..
    json_number(rgba[4], 1.0) .. "]"
end

local function build_online_profile()
  local colour_indices = selected_player_colour_indices("online")
  local character = character_by_id[valid_character_id(profile.online.character)]
  return {
    schema = 1,
    manifest_url = MANIFEST_URL,
    hats_sha256 = hat_asset_sha256,
    verified = cosmetics_net.verified,
    hat = valid_online_hat_id(profile.online.hat),
    character = character and not character.builtin and character.allowed_online ~= false and character or nil,
    color_source = online_color_source_id(),
    color_indices = colour_indices,
  }
end

local function character_meta_json(character)
  if not character or character.builtin then
    return '{"id":"default"}'
  end

  local anim_parts = {}
  for _, name in ipairs(sorted_keys(character.animations)) do
    local anim = character.animations[name]
    if anim and anim.frames and #anim.frames > 0 then
      anim_parts[#anim_parts + 1] = json_escape_string(name) .. ":{" ..
        '"frames":' .. json_animation_frames(anim) .. "," ..
        '"fps":' .. json_number_raw(anim.fps, character.fps or 10.0) .. "," ..
        '"loop":' .. (anim.loop ~= false and "true" or "false") ..
      "}"
    end
  end

  local map_parts = {}
  for _, frame in ipairs(sorted_keys(character.frame_map)) do
    local anim_id = character.frame_map[frame]
    if anim_id then
      map_parts[#map_parts + 1] = json_escape_string(frame) .. ":" .. json_escape_string(anim_id)
    end
  end

  return "{" ..
    '"id":' .. json_escape_string(character.id) .. "," ..
    '"name":' .. json_escape_string(character.name) .. "," ..
    '"sheet_sha256":' .. json_escape_string(character.sheet_sha256) .. "," ..
    '"cell_w":' .. json_integer(character.cell_w, 16) .. "," ..
    '"cell_h":' .. json_integer(character.cell_h, 16) .. "," ..
    '"padding":' .. json_integer(character.padding, 0) .. "," ..
    '"target_w":' .. json_number_raw(character.target_w, CHARACTER_TARGET_W) .. "," ..
    '"target_h":' .. json_number_raw(character.target_h, CHARACTER_TARGET_H) .. "," ..
    '"offset_x":' .. json_number_raw(character.offset_x, 0.0) .. "," ..
    '"offset_y":' .. json_number_raw(character.offset_y, 0.0) .. "," ..
    '"fps":' .. json_number_raw(character.fps, 10.0) .. "," ..
    '"animations":{' .. table.concat(anim_parts, ",") .. "}," ..
    '"frame_map":{' .. table.concat(map_parts, ",") .. "}," ..
    '"color_masks":' .. character_import.json_color_masks(character.color_masks) .. "," ..
    '"hat_anchor":' .. character_import.json_hat_anchor(character.hat_anchor) .. "," ..
    '"sword_anchor":' .. character_import.json_hat_anchor(character.sword_anchor) ..
  "}"
end

local function build_online_profile_json()
  local data = build_online_profile()
  return "{" ..
    '"schema":1,' ..
    '"manifest_url":' .. json_escape_string(data.manifest_url) .. "," ..
    '"hats_sha256":' .. json_escape_string(data.hats_sha256) .. "," ..
    '"verified":' .. (data.verified and "true" or "false") .. "," ..
    '"hat":' .. json_escape_string(data.hat) .. "," ..
    '"character":' .. character_meta_json(data.character) .. "," ..
    '"color_source":' .. json_escape_string(data.color_source) .. "," ..
    '"color_indices":{' ..
      '"skin":' .. json_integer(data.color_indices and data.color_indices.skin, 0) .. "," ..
      '"clothing":' .. json_integer(data.color_indices and data.color_indices.clothing, 0) ..
    "}" ..
  "}"
end

function character_import.definition_json(character)
  if not character or character.builtin then return nil end

  local anim_parts = {}
  for _, name in ipairs(sorted_keys(character.animations)) do
    local anim = character.animations[name]
    if anim and anim.frames and #anim.frames > 0 then
      anim_parts[#anim_parts + 1] = json_escape_string(name) .. ":{" ..
        '"frames":' .. json_animation_frames(anim) .. "," ..
        '"fps":' .. json_number_raw(anim.fps, character.fps or 10.0) .. "," ..
        '"loop":' .. (anim.loop ~= false and "true" or "false") ..
      "}"
    end
  end

  local map_parts = {}
  for _, frame in ipairs(sorted_keys(character.frame_map)) do
    local anim_id = character.frame_map[frame]
    if anim_id then
      map_parts[#map_parts + 1] = json_escape_string(frame) .. ":" .. json_escape_string(anim_id)
    end
  end

  return "{" ..
    '"id":' .. json_escape_string(character.id) .. "," ..
    '"name":' .. json_escape_string(character.name) .. "," ..
    '"sheet":' .. json_escape_string(character.sheet) .. "," ..
    '"sheet_sha256":' .. json_escape_string(character.sheet_sha256) .. "," ..
    '"cell_w":' .. json_integer(character.cell_w, 16) .. "," ..
    '"cell_h":' .. json_integer(character.cell_h, 16) .. "," ..
    '"padding":' .. json_integer(character.padding, 0) .. "," ..
    '"target_w":' .. json_number_raw(character.target_w, CHARACTER_TARGET_W) .. "," ..
    '"target_h":' .. json_number_raw(character.target_h, CHARACTER_TARGET_H) .. "," ..
    '"offset_x":' .. json_number_raw(character.offset_x, 0.0) .. "," ..
    '"offset_y":' .. json_number_raw(character.offset_y, 0.0) .. "," ..
    '"fps":' .. json_number_raw(character.fps, 10.0) .. "," ..
    '"animations":{' .. table.concat(anim_parts, ",") .. "}," ..
    '"frame_map":{' .. table.concat(map_parts, ",") .. "}," ..
    '"color_masks":' .. character_import.json_color_masks(character.color_masks) .. "," ..
    '"hat_anchor":' .. character_import.json_hat_anchor(character.hat_anchor) .. "," ..
    '"sword_anchor":' .. character_import.json_hat_anchor(character.sword_anchor) ..
  "}"
end

function character_import.save()
  local parts = {}
  for i = 1, #character_import.defs do
    local json = character_import.definition_json(character_import.defs[i])
    if json then parts[#parts + 1] = json end
  end
  storage.set("imported_characters_json", '{"schema":1,"characters":[' .. table.concat(parts, ",") .. "]}")
end

function character_import.remember(character)
  local stored = clone_table(character)
  stored.sheet_bytes = nil
  stored.sheet_info = nil
  stored.color_layer_info = nil
  stored.mask_paths = nil
  stored.mask_error = nil
  stored.asset_error = nil
  stored.last_asset_try = nil
  stored.source = nil
  stored.asset_id = nil
  local replaced = false
  for i = 1, #character_import.defs do
    if character_import.defs[i].id == stored.id then
      character_import.defs[i] = stored
      replaced = true
      break
    end
  end
  if not replaced then
    character_import.defs[#character_import.defs + 1] = stored
  end
  character_import.save()
end

function character_import.import_json(json_path)
  local package_dir = character_import.dirname(json_path)
  local body, read_err = character_import.read_abs(json_path)
  if not body then return false, "could not read character.json: " .. tostring(read_err) end
  local src, parse_err = json_decode(body)
  if type(src) ~= "table" then return false, "character.json parse failed: " .. tostring(parse_err) end
  local entries = character_entries_from_document(src)
  src = entries[1]
  if type(src) ~= "table" then return false, "character.json does not define a character" end

  local sheet_rel = clean_rel_path(src.sheet or src.spritesheet or src.path)
  if not sheet_rel then return false, "character.json needs a relative sheet path" end
  local sheet_data, sheet_err = character_import.read_abs(character_import.join(package_dir, sheet_rel))
  if not sheet_data then return false, "could not read sheet: " .. tostring(sheet_err) end
  if #sheet_data > CHARACTER_MAX_BYTES then return false, "sheet is larger than 1 MiB" end

  local sha = sha256_hex(sheet_data)
  if not sha then return false, "could not checksum sheet" end
  local cache_path = CHARACTER_CACHE_DIR .. "/" .. sha .. ".png"
  local ok, write_err = write_binary_file(cache_path, sheet_data)
  if not ok then return false, "could not cache sheet: " .. tostring(write_err) end

  local imported = clone_table(src)
  imported.sheet = cache_path
  imported.sheet_sha256 = sha
  imported.id = clean_id(imported.id) or ("custom_" .. sha:sub(1, 12))
  imported.name = clean_name(imported.name, imported.id)

  local character, err = normalize_character_definition(imported, "local")
  if not character then return false, tostring(err or "invalid character") end
  add_or_replace_character(character)
  character_import.remember(character)
  save_character(selected_player, character.id)
  return true, "imported " .. character.name
end

function character_import.import_folder(folder_path)
  local fs = mod.fs
  if not fs or not fs.find_file then
    return false, "file picker API unavailable"
  end
  local json_path = fs.find_file(folder_path, "character.json")
  if type(json_path) ~= "string" then
    return false, "package needs character.json"
  end
  return character_import.import_json(json_path)
end

function character_import.ps_quote(value)
  return "'" .. tostring(value or ""):gsub("'", "''") .. "'"
end

function character_import.import_zip(zip_path)
  if not mod.get_path then return false, "mod.get_path unavailable" end
  local stamp = tostring(math.floor(os.clock() * 1000)) .. "_" .. tostring(math.random(1000, 9999))
  local rel_dir = CACHE_DIR .. "/character_import_" .. stamp
  local abs_dir = mod.get_path(rel_dir)
  local ok, mkdir_err = ensure_cache_subdir(rel_dir)
  if not ok then return false, tostring(mkdir_err) end

  local command = 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Force -LiteralPath ' ..
                  character_import.ps_quote(zip_path) .. ' -DestinationPath ' .. character_import.ps_quote(abs_dir) .. '"'
  local result = os.execute(command)
  if result ~= true and result ~= 0 then
    return false, "zip extraction failed"
  end
  return character_import.import_folder(abs_dir)
end

function character_import.import_file(file_path)
  local ext = character_import.ext(file_path)
  if ext and ext:lower() == "zip" then
    return character_import.import_zip(file_path)
  end
  if ext and ext:lower() == "json" then
    return character_import.import_json(file_path)
  end
  return false, "select a .zip package or character.json"
end

function character_import.choose_folder()
  if not mod.fs or not mod.fs.pick_folder then
    character_import.set_message("folder picker unavailable")
    return
  end
  local folder = mod.fs.pick_folder("Import character folder")
  if type(folder) ~= "string" then
    character_import.set_message("import cancelled")
    return
  end
  local ok, message = character_import.import_folder(folder)
  character_import.set_message(message)
end

function character_import.choose_file()
  if not mod.fs or not mod.fs.pick_character_file then
    character_import.set_message("file picker unavailable")
    return
  end
  local path = mod.fs.pick_character_file("Import character zip or json")
  if type(path) ~= "string" then
    character_import.set_message("import cancelled")
    return
  end
  local ok, message = character_import.import_file(path)
  character_import.set_message(message)
end

local function normalize_rgba(value, fallback)
  local rgba = rgba_or_fallback(value, fallback)
  return {
    clamp(tonumber(rgba[1]) or fallback[1], 0.0, 1.0),
    clamp(tonumber(rgba[2]) or fallback[2], 0.0, 1.0),
    clamp(tonumber(rgba[3]) or fallback[3], 0.0, 1.0),
    clamp(tonumber(rgba[4]) or fallback[4], 0.0, 1.0),
  }
end

local function normalize_profile_colors(data)
  local colors = type(data) == "table" and data.colors or nil
  if type(colors) ~= "table" then return nil end
  return {
    skin_rgba = normalize_rgba(colors.skin_rgba, fallback_skin),
    clothing_rgba = normalize_rgba(colors.clothing_rgba, fallback_clothing.p1),
  }
end

local function normalize_profile_colour_indices(data)
  if type(data) ~= "table" then return nil end
  local indices = type(data.color_indices) == "table" and data.color_indices or data
  local skin = tonumber(indices.skin or indices.skin_index or indices[1])
  local clothing = tonumber(indices.clothing or indices.clothing_index or indices[2])
  if not skin or not clothing then return nil end
  return {
    skin = math.floor(skin),
    clothing = math.floor(clothing),
  }
end

local function remote_character_cache_path(sha)
  return CHARACTER_CACHE_DIR .. "/" .. tostring(sha or "") .. ".png"
end

local function cached_character_asset_matches(sha)
  sha = lower_sha256(sha)
  if not sha then return false end
  local data = read_binary_file(remote_character_cache_path(sha))
  if not data then return false end
  local actual = sha256_hex(data)
  return actual == sha
end

local function poll_remote_character_asset(expected_sha)
  expected_sha = lower_sha256(expected_sha)
  local online = mod.online
  if not expected_sha or not online or not online.remote_cosmetic_asset then return false end

  local asset_id, data, revision = online.remote_cosmetic_asset()
  if type(asset_id) ~= "string" or type(data) ~= "string" then return false end
  if lower_sha256(asset_id) ~= expected_sha then return false end
  if #data > CHARACTER_MAX_BYTES then return false end

  local actual = sha256_hex(data)
  if actual ~= expected_sha then return false end
  local ok = write_binary_file(remote_character_cache_path(expected_sha), data)
  if not ok then return false end
  if online.mark_cosmetic_asset_applied and revision and remote_asset_revision_applied ~= revision then
    online.mark_cosmetic_asset_applied(revision)
    remote_asset_revision_applied = revision
  end
  return true
end

local function incoming_character_meta(data)
  if type(data) ~= "table" then return nil end
  local character = data.character or data.char
  if type(data.cosmetics) == "table" and data.cosmetics.character then
    character = data.cosmetics.character
  end
  if type(character) ~= "table" then return nil end
  if character.id == "default" or character.id == "none" then return nil end
  return character
end

local function ensure_character_from_profile_meta(meta)
  if type(meta) ~= "table" then return "default", true end
  local sha = lower_sha256(meta.sheet_sha256 or meta.sha256 or meta.asset_sha256)
  if not sha then return "default", true end
  local incoming_masks = character_import.normalize_color_masks(meta)
  local incoming_mask_key = character_import.color_mask_key(incoming_masks)

  local local_id = valid_character_id(meta.id)
  local local_character = character_by_id[local_id]
  if local_character and not local_character.builtin and
     local_character.sheet_sha256 == sha and
     local_character.color_mask_key == incoming_mask_key then
    return local_id, true
  end

  if not cached_character_asset_matches(sha) and not poll_remote_character_asset(sha) then
    return nil, false
  end

  local remote_key = sha .. ":" .. tostring(incoming_mask_key or "")
  local existing_id = remote_characters_by_sha[remote_key]
  if existing_id and character_by_id[existing_id] then
    return existing_id, true
  end

  local remote_meta = clone_table(meta)
  remote_meta.id = "remote_" .. sha:sub(1, 12)
  remote_meta.sheet_sha256 = sha
  local character = normalize_character_definition(remote_meta, "remote", remote_character_cache_path(sha))
  if not character then return nil, false end
  character.color = {0.18, 0.14, 0.20, 1.0}
  add_or_replace_character(character)
  remote_characters_by_sha[remote_key] = character.id
  return character.id, true
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
  if incoming_sha and not request_hat_asset_sha(incoming_sha) then
    return false
  end

  local character_id = "default"
  local character_meta = incoming_character_meta(data)
  if character_meta then
    local ok
    character_id, ok = ensure_character_from_profile_meta(character_meta)
    if not ok then return false end
  end

  match_profiles[slot] = {
    hat = hat_id,
    character = valid_character_id(character_id),
    hats_sha256 = incoming_sha,
    color_indices = normalize_profile_colour_indices(data),
  }
  return true
end

local function apply_match_profile_json(slot, profile_json)
  if type(profile_json) ~= "string" or profile_json == "" then return false end
  local data = json_decode(profile_json)
  if type(data) ~= "table" then return false end
  return apply_match_profile(slot, data)
end

local function capture_online_colour_restore()
  if online_colour_restore then return end
  online_colour_restore = {
    p1 = read_player_colour_indices("p1"),
    p2 = read_player_colour_indices("p2"),
  }
end

local function restore_match_colour_indices()
  local setter = player_colour_index_setter()
  if setter and online_colour_restore then
    for _, slot in ipairs({"p1", "p2"}) do
      local player_index = player_index_from_id(slot)
      local indices = online_colour_restore[slot]
      if indices then
        setter(player_index, 0, indices.skin)
        setter(player_index, 1, indices.clothing)
        refresh_player_render_colours(player_index)
      end
    end
  end
  online_colour_restore = nil
  colour_indices_applied = false
end

local function apply_match_colour_indices()
  local setter = player_colour_index_setter()
  if not setter then return end
  capture_online_colour_restore()
  for slot, match_profile in pairs(match_profiles) do
    if type(match_profile) == "table" and type(match_profile.color_indices) == "table" then
      local player_index = player_index_from_id(slot)
      setter(player_index, 0, match_profile.color_indices.skin)
      setter(player_index, 1, match_profile.color_indices.clothing)
      refresh_player_render_colours(player_index)
      colour_indices_applied = true
    end
  end
end

local function gameplay_hat_for(slot)
  local match_profile = match_profiles[slot]
  if match_profile and match_profile.hat then
    return valid_hat_id(match_profile.hat)
  end
  return profile[slot] and valid_hat_id(profile[slot].hat) or "none"
end

local function gameplay_character_for(slot)
  local match_profile = match_profiles[slot]
  if match_profile and match_profile.character then
    return valid_character_id(match_profile.character)
  end
  return profile[slot] and valid_character_id(profile[slot].character) or "default"
end

local function should_skip_gameplay_cosmetic_overlays()
  local state_name = ui.state_name and ui.state_name() or nil
  if not state_name or state_name == "unknown" or state_name == "game" then
    return false
  end
  return true
end

local function set_player_vanilla_visible(slot, visible)
  local player_index = player_index_from_id(slot)
  local body_setter = game.set_player_body_hidden
  if not body_setter then return false end

  body_setter(player_index, not visible)
  if visible then
    refresh_player_render_colours(player_index)
    character_render_hidden[slot] = false
    return true
  end

  refresh_player_render_colours(player_index)
  character_render_hidden[slot] = true
  return true
end

local function set_sword_idle_offset(slot, x, y)
  local setter = game.set_player_sword_idle_offset
  if not setter then return end
  setter(player_index_from_id(slot), tonumber(x) or 0.0, tonumber(y) or 0.0)
end

local function apply_character_render_visibility()
  if should_skip_gameplay_cosmetic_overlays() then
    for _, slot in ipairs({"p1", "p2"}) do
      set_sword_idle_offset(slot, 0.0, 0.0)
      if character_render_hidden[slot] then
        set_player_vanilla_visible(slot, true)
      end
    end
    return
  end

  local snap = nil
  local tick = nil
  if game.set_player_sword_idle_offset and game.snapshot then
    snap = game.snapshot(0, false)
    if snap and not snap.error then
      tick = snap.native_tick or snap.tick
    end
  end

  for _, slot in ipairs({"p1", "p2"}) do
    local character = character_by_id[gameplay_character_for(slot)]
    local hide_vanilla = character and not character.builtin and character.sheet_info
    if hide_vanilla then
      set_player_vanilla_visible(slot, false)
      if custom_sword_idle_offset then
        local player = snap and (slot == "p2" and snap.enemy or snap.player) or nil
        local x, y = custom_sword_idle_offset(character, player, { owner_slot = slot, tick = tick or os.clock() * 60.0 })
        set_sword_idle_offset(slot, x, y)
      else
        set_sword_idle_offset(slot, 0.0, 0.0)
      end
    elseif character_render_hidden[slot] then
      set_player_vanilla_visible(slot, true)
      set_sword_idle_offset(slot, 0.0, 0.0)
    else
      set_sword_idle_offset(slot, 0.0, 0.0)
    end
  end
end

local online_sync_state = {
  active = false,
  last_local_json = nil,
  last_remote_revision = nil,
  last_asset_id = nil,
  asset_check_started = false,
}

local function slot_from_player_index(index)
  return tonumber(index) == 1 and "p2" or "p1"
end

local function online_is_active()
  local online = mod.online
  if not online or not online.status then return false end
  local status = online.status()
  return type(status) == "table" and status.active and true or false
end

local function sync_local_character_asset()
  local online = mod.online
  if not online or not online.set_cosmetic_asset then return end
  local character = character_by_id[valid_character_id(profile.online.character)]
  if character and not character.builtin and character.allowed_online ~= false and character.sheet_sha256 and character.sheet_bytes then
    if online_sync_state.last_asset_id ~= character.sheet_sha256 then
      local ok = online.set_cosmetic_asset(character.sheet_sha256, character.sheet_bytes)
      if ok then online_sync_state.last_asset_id = character.sheet_sha256 end
    end
  elseif online_sync_state.last_asset_id ~= "" then
    online.set_cosmetic_asset("", "")
    online_sync_state.last_asset_id = ""
  end
end

local function sync_online_profiles()
  local online = mod.online
  if not online or not online.status then return end

  local status = online.status()
  if type(status) ~= "table" or not status.active then
    if online_sync_state.active then
      restore_match_colour_indices()
      match_profiles = {}
      apply_character_render_visibility()
      online_sync_state.active = false
      online_sync_state.last_local_json = nil
      online_sync_state.last_remote_revision = nil
      online_sync_state.last_asset_id = nil
      online_sync_state.asset_check_started = false
    end
    return
  end

  online_sync_state.active = true
  if not ONLINE_COSMETICS_ENABLED then
    restore_match_colour_indices()
    match_profiles = {}
    apply_character_render_visibility()
    if online.set_cosmetic_profile and online_sync_state.last_local_json ~= "" then
      online.set_cosmetic_profile("")
      online_sync_state.last_local_json = ""
    end
    if online.set_cosmetic_asset and online_sync_state.last_asset_id ~= "" then
      online.set_cosmetic_asset("", "")
      online_sync_state.last_asset_id = ""
    end
    online_sync_state.last_remote_revision = nil
    online_sync_state.asset_check_started = false
    return
  end
  if not online_sync_state.asset_check_started then
    start_manifest_fetch(true)
    online_sync_state.asset_check_started = true
  end
  local local_slot = slot_from_player_index(status.local_player)
  local remote_slot = slot_from_player_index(status.remote_player)

  sync_local_character_asset()
  local local_profile = build_online_profile()
  apply_match_profile(local_slot, local_profile)

  if online.set_cosmetic_profile then
    local local_json = build_online_profile_json()
    if local_json ~= online_sync_state.last_local_json then
      local ok = online.set_cosmetic_profile(local_json)
      if ok then online_sync_state.last_local_json = local_json end
    end
  end

  if online.remote_cosmetic_profile then
    local remote_json, revision = online.remote_cosmetic_profile()
    if remote_json and revision ~= online_sync_state.last_remote_revision then
      if apply_match_profile_json(remote_slot, remote_json) then
        online_sync_state.last_remote_revision = revision
        if online.mark_cosmetic_profile_applied then
          online.mark_cosmetic_profile_applied(revision)
        end
      end
    end
  end
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
        cosmetics_net.verified = false
        cosmetics_net.status = "error"
        cosmetics_net.message = "hats missing; fetching official hats"
        cosmetics_net.retry_at = 0.0
        start_manifest_fetch(true)
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

  local facing = tonumber(player.facing) or 1
  return x * (facing < 0 and -1 or 1), y
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
  local follow_x, follow_y
  if custom_hat_follow_offset then
    follow_x, follow_y = custom_hat_follow_offset(player, opts)
  end
  if follow_x == nil then
    follow_x, follow_y = head_follow_offset(player, opts)
  end
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

local function ensure_character_asset(character)
  if not character or character.builtin then return false end
  if character.sheet_info then return true end
  local now = os.clock()
  if character.last_asset_try and now - character.last_asset_try < 0.35 then
    return false
  end
  character.last_asset_try = now

  local sheet_path = character.sheet
  local mask_layers = nil

  if character.color_masks then
    if character.mask_paths then
      mask_layers = character.mask_paths.layers
      sheet_path = character.mask_paths.base or sheet_path
    elseif mod.assets.build_color_masks then
      local mask_key = tostring(character.asset_key or character.sheet_sha256 or character.id):sub(1, 32)
      local paths, mask_err = mod.assets.build_color_masks(character.sheet, {
        out_dir = CHARACTER_CACHE_DIR .. "/masks",
        key = mask_key,
        masks = character.color_masks,
      })
      if paths and paths.base then
        character.mask_paths = paths
        mask_layers = paths.layers
        sheet_path = paths.base
        character.mask_error = nil
      else
        character.mask_error = mask_err or "failed to build color masks"
      end
    else
      character.mask_error = "mod.assets.build_color_masks unavailable"
    end
  end

  local batch_started = false
  if type(mask_layers) == "table" and mod.assets.begin_batch and mod.assets.end_batch then
    local ok = mod.assets.begin_batch()
    batch_started = ok and true or false
  end

  local sheet, err = mod.assets.load_spritesheet(character.asset_id, sheet_path, {
    cell_w = character.cell_w,
    cell_h = character.cell_h,
    padding = character.padding or 0,
  })
  if sheet then
    local layer_info = {}
    local layer_ok = true
    if type(mask_layers) == "table" then
      for _, layer in ipairs({"skin", "clothing"}) do
        local layer_path = mask_layers[layer]
        if type(layer_path) == "string" then
          local layer_asset_id = character_asset_id("character_" .. layer .. "_", character.id, character.asset_key or character.sheet_sha256)
          local layer_sheet, layer_err = mod.assets.load_spritesheet(layer_asset_id, layer_path, {
            cell_w = character.cell_w,
            cell_h = character.cell_h,
            padding = character.padding or 0,
          })
          if not layer_sheet then
            layer_sheet = mod.assets.info and mod.assets.info(layer_asset_id) or nil
          end
          if layer_sheet and layer_sheet.base_id then
            layer_info[layer] = {
              asset_id = layer_asset_id,
              sheet = layer_sheet,
            }
          else
            character.asset_error = layer_err or ("failed to pack " .. layer .. " color mask")
            layer_ok = false
            break
          end
        end
      end
    end
    if layer_ok then
      if batch_started then
        local batch_ok, batch_err = mod.assets.end_batch()
        batch_started = false
        if not batch_ok then
          character.asset_error = batch_err or "failed to pack character assets"
          return false
        end
        sheet = mod.assets.info and mod.assets.info(character.asset_id) or sheet
        if not sheet or not sheet.count or sheet.count <= 0 then
          character.asset_error = "failed to pack character spritesheet"
          return false
        end
        for _, layer in ipairs({"skin", "clothing"}) do
          local info_entry = layer_info[layer]
          if info_entry then
            local refreshed = mod.assets.info and mod.assets.info(info_entry.asset_id) or nil
            if not refreshed or not refreshed.count or refreshed.count <= 0 then
              character.asset_error = "failed to pack " .. layer .. " color mask"
              return false
            end
            info_entry.sheet = refreshed
          end
        end
      end
      character.sheet_info = sheet
      character.color_layer_info = layer_info
      character.asset_error = nil
      return true
    end
  end

  if batch_started and mod.assets.cancel_batch then
    mod.assets.cancel_batch()
  end

  if type(mask_layers) == "table" then
    return false
  end

  local info = mod.assets.info and mod.assets.info(character.asset_id) or nil
  if info and info.base_id then
    character.sheet_info = info
    character.asset_error = nil
    return true
  end
  character.asset_error = err
  return false
end

local function character_has_anim(character, name)
  return character and character.animations and character.animations[name] and true or false
end

local function character_animation_for_player(character, player)
  local frame = math.floor(tonumber(player.sprite_index or player.sprite_frame) or 0)
  local mapped = character.frame_map and character.frame_map[tostring(frame)] or nil
  if mapped and character_has_anim(character, mapped) then return mapped end

  if is_eggnogg_pose(player) and character_has_anim(character, "eggnogg") then return "eggnogg" end
  if is_dead_pose(player) and character_has_anim(character, "dead") then return "dead" end

  local anim_ptr = math.floor(tonumber(player.anim_ptr) or 0)
  local state_id = math.floor(tonumber(player.state_id) or 0)
  if (anim_ptr == anim_ptrs.stun or state_id == 4) and character_has_anim(character, "stun") then
    return "stun"
  end
  if duck_pose_anchor(player, frame) then
    if prone_pose_frames[frame] and character_has_anim(character, "prone") then return "prone" end
    if character_has_anim(character, "duck") then return "duck" end
    if character_has_anim(character, "crouch") then return "crouch" end
  end

  local vx = math.abs(tonumber(player.vx) or 0.0)
  local vy = tonumber(player.vy) or 0.0
  if not player.grounded then
    if vy < -0.15 and character_has_anim(character, "jump") then return "jump" end
    if vy >= -0.15 and character_has_anim(character, "fall") then return "fall" end
  end
  if vx > 0.35 then
    if character_has_anim(character, "run") then return "run" end
    if character_has_anim(character, "walk") then return "walk" end
  end
  return character_has_anim(character, "idle") and "idle" or next(character.animations)
end

local character_frame_context

function character_import.anchor_frame_motion(anchor, motion, context)
  if not context then return nil end
  local index = math.floor(tonumber(context.frame_index) or 0)
  local frame = context.frame
  if motion and type(motion.frames) == "table" and #motion.frames > 0 and index > 0 then
    return motion.frames[((index - 1) % #motion.frames) + 1]
  end
  if motion and type(motion.frame_map) == "table" and frame ~= nil then
    return motion.frame_map[tostring(frame)]
  end
  if anchor and type(anchor.frames) == "table" and #anchor.frames > 0 and index > 0 then
    return anchor.frames[((index - 1) % #anchor.frames) + 1]
  end
  if anchor and type(anchor.frame_map) == "table" and frame ~= nil then
    return anchor.frame_map[tostring(frame)]
  end
  return nil
end

function character_import.anchor_offset_for_player(character, anchor, player, opts)
  if type(anchor) ~= "table" then return nil end
  local context = character_frame_context and character_frame_context(character, player, opts) or nil
  local anim_name = context and context.anim_name or (player and character_animation_for_player(character, player)) or "idle"
  local motion = type(anchor[anim_name]) == "table" and anchor[anim_name] or nil
  local frame_motion = character_import.anchor_frame_motion(anchor, motion, context)
  local x = tonumber(anchor.x) or 0.0
  local y = tonumber(anchor.y) or 0.0

  if motion then
    x = x + (tonumber(motion.x) or 0.0)
    y = y + (tonumber(motion.y) or 0.0)
  end
  if frame_motion then
    x = x + (tonumber(frame_motion.x) or 0.0)
    y = y + (tonumber(frame_motion.y) or 0.0)
  end

  local bob_x = (tonumber(anchor.bob_x) or 0.0) +
                (tonumber(motion and motion.bob_x) or 0.0) +
                (tonumber(frame_motion and frame_motion.bob_x) or 0.0)
  local bob_y = (tonumber(anchor.bob_y) or 0.0) +
                (tonumber(motion and motion.bob_y) or 0.0) +
                (tonumber(frame_motion and frame_motion.bob_y) or 0.0)
  if bob_x ~= 0.0 or bob_y ~= 0.0 then
    local tick = tonumber(opts and opts.tick) or os.clock() * 60.0
    local fps = tonumber((motion and motion.fps) or anchor.fps) or 12.0
    local phase = tonumber((motion and motion.phase) or anchor.phase) or 0.0
    local wave = math.sin((tick / 60.0) * fps * math.pi * 2.0 + phase)
    x = x + bob_x * wave
    y = y + bob_y * wave
  end

  local facing = tonumber(player and player.facing) or 1
  return x * (facing < 0 and -1 or 1), y
end

function custom_hat_follow_offset(player, opts)
  opts = opts or {}
  local owner_slot = opts.owner_slot
  local profile_slot = owner_slot and profile[owner_slot] or nil
  if not profile_slot then return nil end

  local character_id = opts.screen and profile_slot.character or gameplay_character_for(owner_slot)
  local character = character_by_id[valid_character_id(character_id)]
  if not character or character.builtin then return nil end
  return character_import.anchor_offset_for_player(character, character.hat_anchor, player, opts)
end

function custom_sword_idle_offset(character, player, opts)
  opts = opts or {}
  if not character or character.builtin then return 0.0, 0.0 end
  local x, y = character_import.anchor_offset_for_player(character, character.sword_anchor, player, opts)
  return x or 0.0, y or 0.0
end

local function chosen_animation_frames(character, anim_name, anim, player, opts)
  if type(anim.choices) ~= "table" or #anim.choices == 0 then
    return anim.frames
  end

  local owner = opts and opts.owner_slot or tostring(player and player.index or "?")
  local key = owner .. ":" .. tostring(character.id)
  local state = character_choice_state[key]
  if not state or state.anim_name ~= anim_name then
    local seed = math.floor(tonumber(opts and opts.tick) or os.clock() * 60.0)
    seed = seed + math.floor((tonumber(player and player.x) or 0.0) * 17.0)
    seed = seed + math.floor((tonumber(player and player.y) or 0.0) * 31.0)
    seed = seed + #owner * 13 + #anim_name * 7
    local index = (math.abs(seed) % #anim.choices) + 1
    state = { anim_name = anim_name, choice = index }
    character_choice_state[key] = state
  end

  return anim.choices[state.choice] or anim.frames
end

character_frame_context = function(character, player, opts)
  local anim_name = character_animation_for_player(character, player)
  local anim = character.animations and character.animations[anim_name] or nil
  if not anim or not anim.frames or #anim.frames == 0 then
    return { anim_name = anim_name or "idle", frame = 0, frame_index = 1, anim = anim, frames = {0} }
  end
  local frames = chosen_animation_frames(character, anim_name, anim, player, opts)
  if not frames or #frames == 0 then
    return { anim_name = anim_name or "idle", frame = 0, frame_index = 1, anim = anim, frames = {0} }
  end
  local tick = tonumber(opts and opts.tick) or os.clock() * 60.0
  local fps = tonumber(anim.fps or character.fps) or 10.0
  local index = math.floor((tick / 60.0) * fps)
  if anim.loop == false then
    index = math.min(index, #frames - 1)
  else
    index = index % #frames
  end
  local frame_index = index + 1
  return {
    anim_name = anim_name or "idle",
    frame = frames[frame_index] or frames[1] or 0,
    frame_index = frame_index,
    anim = anim,
    frames = frames,
  }
end

local function character_frame_for(character, player, opts)
  local context = character_frame_context(character, player, opts)
  return context and context.frame or 0
end

local function draw_custom_character(character_id, player, opts)
  local character = character_by_id[valid_character_id(character_id)]
  if not character or character.builtin or not player then return false end
  if not ensure_character_asset(character) then return false end

  local frame = character_frame_for(character, player, opts)
  local sprite = mod.assets.sprite_id(character.asset_id, frame)
  if not sprite then return false end

  local facing = tonumber(player.facing) or 1
  local draw_x
  local draw_y
  local scale_x
  local scale_y

  if opts and opts.screen then
    local body_scale = tonumber(opts.body_scale) or 1.0
    draw_x = (tonumber(opts.x) or 0.0) + (character.offset_x or 0.0) * body_scale
    draw_y = (tonumber(opts.y) or 0.0) + (character.offset_y or 0.0) * body_scale
    scale_x = body_scale * ((character.target_w or CHARACTER_TARGET_W) / character.cell_w)
    scale_y = body_scale * ((character.target_h or CHARACTER_TARGET_H) / character.cell_h)
  else
    local sx, sy, world_scale = world_to_screen((tonumber(player.x) or 0.0) + (character.offset_x or 0.0),
                                                (tonumber(player.y) or 0.0) + (character.offset_y or 0.0),
                                                opts and opts.camera or nil)
    draw_x = sx
    draw_y = sy
    scale_x = world_scale * ((character.target_w or CHARACTER_TARGET_W) / character.cell_w)
    scale_y = world_scale * ((character.target_h or CHARACTER_TARGET_H) / character.cell_h)
  end

  local draw_opts = {
    scale_x = scale_x,
    scale_y = scale_y,
    flip = facing < 0,
    tint = {1.0, 1.0, 1.0, 1.0},
  }

  local drawn = ui.draw_sprite(sprite, draw_x, draw_y, draw_opts)
  if drawn and character.color_layer_info then
    local owner_slot = opts and opts.owner_slot or "p1"
    local skin_tint, clothing_tint = selected_player_colours(owner_slot)
    for _, layer in ipairs({"skin", "clothing"}) do
      local layer_info = character.color_layer_info[layer]
      if layer_info then
        local layer_sprite = mod.assets.sprite_id(layer_info.asset_id, frame)
        if layer_sprite then
          local tint = layer == "skin" and skin_tint or clothing_tint
          draw_opts.tint = tint
          ui.draw_sprite(layer_sprite, draw_x, draw_y, draw_opts)
        end
      end
    end
  end
  return drawn
end

local function character_items()
  local rendered = {}
  for i = 1, #characters do
    local character = characters[i]
    local item = {
      id = character.id,
      label = character.name,
      color = character.color,
    }
    if not character.builtin and ensure_character_asset(character) then
      local idle = character.animations and character.animations.idle
      local frame = idle and idle.frames and idle.frames[1] or 0
      local sprite = mod.assets.sprite_id(character.asset_id, frame)
      if sprite then
        local fit = math.min(42.0 / character.cell_w, 44.0 / character.cell_h)
        item.sprite = sprite
        item.sprite_opts = {
          scale_x = fit,
          scale_y = fit,
        }
      end
    end
    rendered[#rendered + 1] = item
  end
  return rendered
end

local function draw_preview_player(cx, cy, body_scale, player_id)
  local clock = os.clock()
  local bounce = math.sin(clock * 3.2) * 2.0
  local frame = 0
  local base = ui.sprite_id and ui.sprite_id("sprites", frame)
  local clothing_layer = ui.sprite_id and ui.sprite_id("sprites", 128 + frame)
  local skin_tint, clothing_tint = selected_player_colours(player_id)
  local preview_player = {
    index = player_index_from_id(player_id),
    sprite_index = frame,
    facing = 1,
    vx = math.sin(clock * 2.1) * 0.8,
    vy = math.cos(clock * 2.5) * 0.5,
    grounded = true,
    state_id = 1,
  }

  local custom_drawn = draw_custom_character(profile[player_id].character, preview_player, {
    screen = true,
    owner_slot = player_id,
    x = cx,
    y = cy + bounce,
    body_scale = body_scale,
    tick = clock * 60.0,
  })

  if not custom_drawn then
    if base then
      ui.draw_sprite(base, cx, cy + bounce, { scale = body_scale, tint = skin_tint })
    end
    if clothing_layer then
      ui.draw_sprite(clothing_layer, cx, cy + bounce, { scale = body_scale, tint = clothing_tint })
    end
  end

  draw_hat(profile[player_id].hat, preview_player, {
    screen = true,
    owner_slot = player_id,
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

  local import_y = bottom - 132
  local import_button_w = math.max(96, (left_w - 58) * 0.5)
  if ui.button_at("official_cosmetics_import_zip", "IMPORT ZIP", margin + 24, import_y, import_button_w, 28) then
    character_import.choose_file()
  end
  if ui.button_at("official_cosmetics_import_folder", "IMPORT FOLDER", margin + 32 + import_button_w, import_y, import_button_w, 28) then
    character_import.choose_folder()
  end
  if character_import.message and os.clock() < character_import.message_until then
    ui.text_at(character_import.message, margin + 24, bottom - 104, 0.68, 0.78, 0.82, 0.86)
  end

  if selected_player == "online" then
    local color_label = online_color_source_id() == "p2" and "COLORS: P2" or "COLORS: P1"
    if ui.button_at("official_cosmetics_online_color_source", color_label, margin + 24, bottom - 82, 132, 28) then
      toggle_online_color_source()
    end
  end

  local active_hat = hat_by_id[profile[selected_player].hat] or hat_by_id.none
  local active_character = character_by_id[profile[selected_player].character] or character_by_id.default
  ui.text_at("Character: " .. active_character.name, margin + 24, bottom - 56, 0.82, 0.82, 0.85, 0.88)
  ui.text_at("Hat: " .. active_hat.name, margin + 24, bottom - 34, 0.82, 0.82, 0.85, 0.88)

  local cols = right_w > 620 and 4 or (right_w > 440 and 3 or 2)
  local cell_w = math.max(126, (right_w - 36 - (cols - 1) * 10) / cols)
  ui.text_at("CHARACTERS", right_x + 18, top + 31, 1.0, 0.94, 0.95, 0.96)
  local character_grid_y = top + header_h + 18
  local character_cell_h = 82
  local new_character = select(1, ui.item_grid("official_cosmetics_characters_" .. selected_player,
                                               character_items(),
                                               profile[selected_player].character,
                                               {
                                                 x = right_x + 18,
                                                 y = character_grid_y,
                                                 cols = cols,
                                                 cell_w = cell_w,
                                                 cell_h = character_cell_h,
                                                 gap = 10,
                                                 text_scale = 0.70,
                                               }))
  if new_character ~= profile[selected_player].character then
    save_character(selected_player, new_character)
  end

  local character_rows = math.max(1, math.ceil(#characters / cols))
  local hats_title_y = character_grid_y + character_rows * character_cell_h + math.max(character_rows - 1, 0) * 10 + 36
  ui.text_at("HATS", right_x + 18, hats_title_y, 1.0, 0.94, 0.95, 0.96)
  local grid_y = hats_title_y + 18
  local cell_h = 86
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

local function draw_gameplay_characters()
  if should_skip_gameplay_cosmetic_overlays() then return end
  if online_is_active() and not ONLINE_COSMETICS_ENABLED then return end

  local p1_character = gameplay_character_for("p1")
  local p2_character = gameplay_character_for("p2")
  if p1_character == "default" and p2_character == "default" then
    return
  end

  local snap = game.snapshot(0, false)
  if not snap or snap.error or not snap.player then return end
  local cam = game.camera and game.camera() or nil
  local tick = snap.native_tick or snap.tick or os.clock() * 60.0

  ui.begin_overlay()
  if snap.player then
    draw_custom_character(p1_character, snap.player, { camera = cam, tick = tick, owner_slot = "p1" })
  end
  if snap.enemy then
    draw_custom_character(p2_character, snap.enemy, { camera = cam, tick = tick, owner_slot = "p2" })
  end
  ui.end_overlay()
end

local function draw_gameplay_hats()
  if should_skip_gameplay_cosmetic_overlays() then return end
  if online_is_active() and not ONLINE_COSMETICS_ENABLED then return end

  local p1_hat = gameplay_hat_for("p1")
  local p2_hat = gameplay_hat_for("p2")
  if p1_hat == "none" and p2_hat == "none" then
    return
  end
  if not ensure_assets() then return end

  local snap = game.snapshot(0, false)
  if not snap or snap.error or not snap.player then return end
  local cam = game.camera and game.camera() or nil
  local tick = snap.native_tick or snap.tick or os.clock() * 60.0
  local online = online_is_active()

  ui.begin_overlay()
  local p0 = snap.player
  local p1 = snap.enemy
  if p0 then
    local opts = { camera = cam, tick = tick, online = online, owner_slot = "p1" }
    draw_hat(p1_hat, p0, opts)
  end
  if p1 then
    local opts = { camera = cam, tick = tick, online = online, owner_slot = "p2" }
    draw_hat(p2_hat, p1, opts)
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
        characters = #characters,
        online_character = valid_character_id(profile.online.character),
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
        online_character = valid_character_id(profile.online.character),
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

start_manifest_fetch(true)

mod.on_frame(function()
  poll_cosmetics_server()
  sync_online_profiles()
  apply_character_render_visibility()
  draw_main_button()
  draw_gameplay_characters()
  draw_gameplay_hats()
end)

mod.on_tick(function()
  if ONLINE_COSMETICS_ENABLED and online_is_active() then
    poll_cosmetics_server()
    sync_online_profiles()
    apply_match_colour_indices()
  elseif colour_indices_applied then
    restore_match_colour_indices()
  end
  apply_character_render_visibility()
end)

mod.on_tick_post(function()
  apply_character_render_visibility()
end)
