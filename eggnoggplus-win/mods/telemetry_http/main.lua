local ok_ffi, ffi = pcall(require, "ffi")
if not ok_ffi then
  mod.error("telemetry_http requires LuaJIT ffi")
  return
end

ffi.cdef[[
typedef unsigned short u_short;
typedef unsigned long u_long;
typedef unsigned int SOCKET;
typedef unsigned int uint32_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef struct in_addr { unsigned long s_addr; } IN_ADDR;
typedef struct sockaddr {
  unsigned short sa_family;
  char sa_data[14];
} SOCKADDR;
typedef struct sockaddr_in {
  short sin_family;
  u_short sin_port;
  IN_ADDR sin_addr;
  char sin_zero[8];
} SOCKADDR_IN;
typedef struct WSAData {
  unsigned short wVersion;
  unsigned short wHighVersion;
  char szDescription[257];
  char szSystemStatus[129];
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
} WSADATA;
int WSAStartup(unsigned short wVersionRequested, WSADATA* lpWSAData);
int WSACleanup(void);
int WSAGetLastError(void);
u_short htons(u_short hostshort);
SOCKET socket(int af, int type, int protocol);
int ioctlsocket(SOCKET s, long cmd, u_long* argp);
int bind(SOCKET s, const SOCKADDR* name, int namelen);
int listen(SOCKET s, int backlog);
SOCKET accept(SOCKET s, SOCKADDR* addr, int* addrlen);
int recv(SOCKET s, char* buf, int len, int flags);
int send(SOCKET s, const char* buf, int len, int flags);
int closesocket(SOCKET s);
]]

local C = ffi.load("Ws2_32")

local AF_INET = 2
local SOCK_STREAM = 1
local IPPROTO_TCP = 6
local FIONBIO = 0x8004667e
local INADDR_LOOPBACK = 0x0100007F
local SOCKET_ERROR = -1
local INVALID_SOCKET = ffi.cast("SOCKET", 0xFFFFFFFF)
local WSAEWOULDBLOCK = 10035

local ADDR_PLAYER_ARRAY = 0x542058
local ADDR_LEADER = 0x541E0C
local ADDR_LOSER = 0x542064

local PLAYER_OFS_STATE = 0x78
local PLAYER_OFS_HAS_SWORD = 0x11
local PLAYER_OFS_GOAL_DIR = 0x9C
local PLAYER_STATE_DEAD = 8

local server = {
  listener = INVALID_SOCKET,
  port = 0,
  wsa_started = false
}

local function is_invalid_socket(sock)
  return sock == nil or sock == INVALID_SOCKET
end

local function close_socket(sock)
  if not is_invalid_socket(sock) then
    C.closesocket(sock)
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function parse_int(v, default)
  if v == nil then return default end
  local n = tonumber(v)
  if n == nil then return default end
  return math.floor(n)
end

local function parse_num(v, default)
  if v == nil then return default end
  local n = tonumber(v)
  if n == nil then return default end
  return n
end

local function safe_u32(addr)
  local ok, value = pcall(function()
    return tonumber(ffi.cast("uint32_t*", addr)[0])
  end)
  if not ok then return nil end
  return value
end

local function safe_i8(addr)
  local ok, value = pcall(function()
    return tonumber(ffi.cast("int8_t*", addr)[0])
  end)
  if not ok then return nil end
  return value
end

local function safe_u8(addr)
  local ok, value = pcall(function()
    return tonumber(ffi.cast("uint8_t*", addr)[0])
  end)
  if not ok then return nil end
  return value
end

local function valid_ptr32(p)
  return p ~= nil and p ~= 0 and p >= 0x10000 and p < 0x80000000
end

local function player_ptr(index)
  local slot_addr = ADDR_PLAYER_ARRAY + (index * 4)
  local p = safe_u32(slot_addr)
  if not valid_ptr32(p) then return nil end
  return p
end

local function player_index_from_ptr(ptr, p0, p1)
  if not valid_ptr32(ptr) then return nil end
  if p0 and ptr == p0 then return 0 end
  if p1 and ptr == p1 then return 1 end
  return nil
end

local function infer_player_meta(s)
  local pidx = clamp(parse_int(s.player_index, 0), 0, 1)
  local eidx = clamp(parse_int(s.enemy_index, (pidx + 1) % 2), 0, 1)
  local p0 = player_ptr(0)
  local p1 = player_ptr(1)

  local pptr = (pidx == 0) and p0 or p1
  local eptr = (eidx == 0) and p0 or p1

  local leader_ptr = safe_u32(ADDR_LEADER) or 0
  local loser_ptr = safe_u32(ADDR_LOSER) or 0
  local leader_idx = player_index_from_ptr(leader_ptr, p0, p1)
  local loser_idx = player_index_from_ptr(loser_ptr, p0, p1)

  if leader_idx ~= nil then
    s.leader_player_index = leader_idx
  end
  if loser_idx ~= nil then
    s.loser_player_index = loser_idx
  end

  local player_state = pptr and safe_i8(pptr + PLAYER_OFS_STATE) or nil
  local enemy_state = eptr and safe_i8(eptr + PLAYER_OFS_STATE) or nil
  local player_goal_dir_raw = pptr and safe_i8(pptr + PLAYER_OFS_GOAL_DIR) or nil
  local enemy_goal_dir_raw = eptr and safe_i8(eptr + PLAYER_OFS_GOAL_DIR) or nil
  if player_state ~= nil then s.player_state = player_state end
  if enemy_state ~= nil then s.enemy_state = enemy_state end
  if player_goal_dir_raw ~= nil then
    if player_goal_dir_raw > 0 then s.player_goal_dir = 1
    elseif player_goal_dir_raw < 0 then s.player_goal_dir = -1
    else s.player_goal_dir = 0 end
  end
  if enemy_goal_dir_raw ~= nil then
    if enemy_goal_dir_raw > 0 then s.enemy_goal_dir = 1
    elseif enemy_goal_dir_raw < 0 then s.enemy_goal_dir = -1
    else s.enemy_goal_dir = 0 end
  end
  s.goal_dir_known = (player_goal_dir_raw ~= nil and enemy_goal_dir_raw ~= nil) and true or false

  local player_dead_from_state = (player_state ~= nil and player_state == PLAYER_STATE_DEAD) or false
  local enemy_dead_from_state = (enemy_state ~= nil and enemy_state == PLAYER_STATE_DEAD) or false
  local player_dead_from_loser = (loser_idx ~= nil and loser_idx == pidx) or false
  local enemy_dead_from_loser = (loser_idx ~= nil and loser_idx == eidx) or false

  s.player_dead = player_dead_from_state or player_dead_from_loser
  s.enemy_dead = enemy_dead_from_state or enemy_dead_from_loser
  s.death_known = (loser_idx ~= nil) or s.player_dead or s.enemy_dead

  local player_go_known = (leader_idx ~= nil)
  local enemy_go_known = (leader_idx ~= nil)
  s.player_has_go_arrow = (leader_idx ~= nil and leader_idx == pidx) or false
  s.enemy_has_go_arrow = (leader_idx ~= nil and leader_idx == eidx) or false
  s.go_arrow_known = player_go_known and enemy_go_known

  local player_has_sword_raw = pptr and safe_u8(pptr + PLAYER_OFS_HAS_SWORD) or nil
  local enemy_has_sword_raw = eptr and safe_u8(eptr + PLAYER_OFS_HAS_SWORD) or nil
  if player_has_sword_raw ~= nil then
    s.player_has_sword = (player_has_sword_raw == 0)
  end
  if enemy_has_sword_raw ~= nil then
    s.enemy_has_sword = (enemy_has_sword_raw == 0)
  end
end

local function json_escape(s)
  return (s
    :gsub("\\", "\\\\")
    :gsub("\"", "\\\"")
    :gsub("\b", "\\b")
    :gsub("\f", "\\f")
    :gsub("\n", "\\n")
    :gsub("\r", "\\r")
    :gsub("\t", "\\t"))
end

local function is_array(tbl)
  local max = 0
  local count = 0
  for k, _ in pairs(tbl) do
    if type(k) ~= "number" or k < 1 or k % 1 ~= 0 then
      return false, 0
    end
    if k > max then max = k end
    count = count + 1
  end
  return max == count, max
end

local function json_encode(v, seen)
  local t = type(v)
  if t == "nil" then
    return "null"
  end
  if t == "boolean" then
    return v and "true" or "false"
  end
  if t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then return "null" end
    return string.format("%.17g", v)
  end
  if t == "string" then
    return "\"" .. json_escape(v) .. "\""
  end
  if t ~= "table" then
    return "\"" .. json_escape(tostring(v)) .. "\""
  end

  seen = seen or {}
  if seen[v] then return "\"<cycle>\"" end
  seen[v] = true

  local is_arr, n = is_array(v)
  local parts = {}
  if is_arr then
    for i = 1, n do
      parts[#parts + 1] = json_encode(v[i], seen)
    end
    seen[v] = nil
    return "[" .. table.concat(parts, ",") .. "]"
  end

  for k, val in pairs(v) do
    if type(k) == "string" then
      parts[#parts + 1] = "\"" .. json_escape(k) .. "\":" .. json_encode(val, seen)
    end
  end
  seen[v] = nil
  return "{" .. table.concat(parts, ",") .. "}"
end

local function url_decode(s)
  if not s then return "" end
  s = s:gsub("+", " ")
  s = s:gsub("%%(%x%x)", function(hex)
    return string.char(tonumber(hex, 16))
  end)
  return s
end

local function parse_query(qs)
  local out = {}
  if not qs or qs == "" then return out end
  for part in qs:gmatch("[^&]+") do
    local k, v = part:match("^([^=]+)=(.*)$")
    if k then out[url_decode(k)] = url_decode(v)
    else out[url_decode(part)] = "" end
  end
  return out
end

local function parse_request_target(req)
  local line = req:match("([^\r\n]+)")
  if not line then return "GET", "/", {} end
  local method, target = line:match("^(%S+)%s+(%S+)")
  if not method or not target then return "GET", "/", {} end
  local path, qs = target:match("^([^?]*)%??(.*)$")
  return method, path or "/", parse_query(qs)
end

local function send_all(sock, data)
  local idx = 1
  while idx <= #data do
    local chunk = data:sub(idx)
    local sent = C.send(sock, chunk, #chunk, 0)
    if sent == SOCKET_ERROR or sent <= 0 then
      return false
    end
    idx = idx + sent
  end
  return true
end

local function send_json(sock, status_line, payload)
  local body = json_encode(payload)
  local response =
    "HTTP/1.1 " .. status_line .. "\r\n" ..
    "Content-Type: application/json\r\n" ..
    "Cache-Control: no-store\r\n" ..
    "Connection: close\r\n" ..
    "Content-Length: " .. tostring(#body) .. "\r\n\r\n" ..
    body
  send_all(sock, response)
end

local SPEED_MIN = 0.05
local SPEED_MAX = 100.0

local function speed_enabled()
  return config.get("speed_enabled", true) and true or false
end

local function speed_multiplier()
  local s = parse_num(config.get("speed_multiplier", 1.0), 1.0)
  return clamp(s, SPEED_MIN, SPEED_MAX)
end

local function speed_effective()
  if speed_enabled() then
    return speed_multiplier()
  end
  return 1.0
end

local function speed_apply_query(query)
  local changed = false

  if query.value ~= nil then
    local requested = clamp(parse_num(query.value, speed_multiplier()), SPEED_MIN, SPEED_MAX)
    config.set("speed_multiplier", requested)
    changed = true
  end

  if query.enabled ~= nil then
    local enabled = parse_int(query.enabled, speed_enabled() and 1 or 0) ~= 0
    config.set("speed_enabled", enabled)
    changed = true
  end

  if query.reset ~= nil and parse_int(query.reset, 0) ~= 0 then
    config.set("speed_multiplier", 1.0)
    changed = true
  end

  return changed
end

local function speed_payload(changed)
  return {
    ok = true,
    changed = changed and true or false,
    speed_enabled = speed_enabled(),
    speed_multiplier = speed_multiplier(),
    sim_time_scale = speed_effective(),
    min = SPEED_MIN,
    max = SPEED_MAX
  }
end

local function ensure_wsa()
  if server.wsa_started then return true end
  local wsadata = ffi.new("WSADATA[1]")
  local rc = C.WSAStartup(0x0202, wsadata)
  if rc ~= 0 then
    mod.error("WSAStartup failed: " .. tostring(rc))
    return false
  end
  server.wsa_started = true
  return true
end

local function stop_server()
  if not is_invalid_socket(server.listener) then
    close_socket(server.listener)
  end
  server.listener = INVALID_SOCKET
  server.port = 0
end

local function start_server(port)
  if not ensure_wsa() then return false end

  local s = C.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
  if s == INVALID_SOCKET then
    mod.error("socket() failed: " .. tostring(C.WSAGetLastError()))
    return false
  end

  local nb = ffi.new("u_long[1]", 1)
  if C.ioctlsocket(s, FIONBIO, nb) ~= 0 then
    mod.error("ioctlsocket(FIONBIO) failed: " .. tostring(C.WSAGetLastError()))
    close_socket(s)
    return false
  end

  local addr = ffi.new("SOCKADDR_IN[1]")
  addr[0].sin_family = AF_INET
  addr[0].sin_port = C.htons(port)
  addr[0].sin_addr.s_addr = INADDR_LOOPBACK
  ffi.fill(addr[0].sin_zero, 8, 0)

  local rc = C.bind(s, ffi.cast("SOCKADDR*", addr), ffi.sizeof(addr[0]))
  if rc ~= 0 then
    mod.error("bind(127.0.0.1:" .. tostring(port) .. ") failed: " .. tostring(C.WSAGetLastError()))
    close_socket(s)
    return false
  end

  rc = C.listen(s, 8)
  if rc ~= 0 then
    mod.error("listen() failed: " .. tostring(C.WSAGetLastError()))
    close_socket(s)
    return false
  end

  stop_server()
  server.listener = s
  server.port = port
  mod.log("telemetry server listening on http://127.0.0.1:" .. tostring(port))
  return true
end

local function make_state_payload()
  local player_index = clamp(parse_int(config.get("player_index", 0), 0), 0, 1)
  local include_tiles = config.get("include_tiles", true) and true or false
  local s = mod.game.snapshot(player_index, include_tiles)
  infer_player_meta(s)
  s.speed_enabled = speed_enabled()
  s.speed_multiplier = speed_multiplier()
  s.sim_time_scale = speed_effective()
  return s
end

local function handle_request(sock, req)
  local method, path, query = parse_request_target(req)

  if method ~= "GET" then
    send_json(sock, "405 Method Not Allowed", { ok = false, error = "GET only" })
    return
  end

  if path == "/" or path == "/state" then
    send_json(sock, "200 OK", make_state_payload())
    return
  end

  if path == "/input" then
    if not config.get("allow_input_override", false) then
      send_json(sock, "403 Forbidden", { ok = false, error = "input override disabled by config" })
      return
    end
    local player = clamp(parse_int(query.player, parse_int(config.get("player_index", 0), 0)), 0, 1)
    local mask = parse_int(query.mask, 0)
    local frames = parse_int(query.frames, 1)
    local replace = parse_int(query.replace, 0) ~= 0
    mod.game.input_override(player, mask, frames, replace)
    send_json(sock, "200 OK", {
      ok = true,
      player = player,
      status = mod.game.input_status(player)
    })
    return
  end

  if path == "/input/clear" then
    local player = clamp(parse_int(query.player, parse_int(config.get("player_index", 0), 0)), 0, 1)
    mod.game.input_clear(player)
    send_json(sock, "200 OK", {
      ok = true,
      player = player,
      status = mod.game.input_status(player)
    })
    return
  end

  if path == "/speed" then
    local changed = speed_apply_query(query)
    send_json(sock, "200 OK", speed_payload(changed))
    return
  end

  send_json(sock, "404 Not Found", { ok = false, error = "unknown endpoint" })
end

local function poll_server()
  if is_invalid_socket(server.listener) then return end

  for _ = 1, 8 do
    local addr = ffi.new("SOCKADDR_IN[1]")
    local addrlen = ffi.new("int[1]", ffi.sizeof(addr[0]))
    local client = C.accept(server.listener, ffi.cast("SOCKADDR*", addr), addrlen)
    if client == INVALID_SOCKET then
      local err = C.WSAGetLastError()
      if err ~= WSAEWOULDBLOCK then
        mod.warn("accept() failed: " .. tostring(err))
      end
      break
    end

    local nb = ffi.new("u_long[1]", 1)
    C.ioctlsocket(client, FIONBIO, nb)

    local buf = ffi.new("char[4096]")
    local n = C.recv(client, buf, 4095, 0)
    if n > 0 then
      local req = ffi.string(buf, n)
      handle_request(client, req)
    else
      if n == SOCKET_ERROR and C.WSAGetLastError() ~= WSAEWOULDBLOCK then
        send_json(client, "400 Bad Request", { ok = false, error = "recv failed" })
      end
    end

    close_socket(client)
  end
end

mod.on_load(function()
  if not mod.game or not mod.game.snapshot then
    mod.error("telemetry_http requires the new mod.game API")
    return
  end

  config.on_action("clear_input_override", function()
    mod.game.input_clear(0)
    mod.game.input_clear(1)
    mod.log("cleared input overrides")
  end)

  config.on_action("reset_speed_multiplier", function()
    config.set("speed_multiplier", 1.0)
    mod.log("reset speed multiplier to 1.0")
  end)
end)

mod.on_event(function(e)
  if e.type == "delta_time" then
    if speed_enabled() then
      e.value = e.value * speed_multiplier()
    end
  end
  return false
end)

mod.on_frame(function()
  if not config.get("enabled", true) then
    stop_server()
    return
  end

  local port = clamp(parse_int(config.get("port", 8765), 8765), 1024, 65535)
  if is_invalid_socket(server.listener) or server.port ~= port then
    start_server(port)
  end

  poll_server()
end)

mod.on_unload(function()
  stop_server()
  if server.wsa_started then
    C.WSACleanup()
    server.wsa_started = false
  end
end)
