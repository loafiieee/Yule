-- lib/servers.lua
-- Fetches a server list from a URL over HTTP/1.0 TCP, pings each entry,
-- and tracks the currently selected server.
--
-- Server list JSON format (served at the configured update URL):
--   [ { "name": "US East", "host": "us-east.example.com", "port": 7878, "region": "us-east" }, ... ]

servers = servers or {}

-- Full URL for the server list, e.g. "https://loafiieee.com/servers.json"
local UPDATE_URL = config.get("server_list_url", "")


-- ── state ────────────────────────────────────────────────────────────────
local list         = {}   -- current known server list
local selected_idx = 1    -- index into list
local pings        = {}   -- ping_ms per index, nil = not measured
local http_handle  = nil  -- mod.http handle for the in-flight server list fetch
local fetch_status = "idle"  -- "idle"|"fetching"|"ok"|"error"|"no_http"
local fetch_error  = nil     -- last error string when fetch_status == "error"

local ping_socks   = {}   -- { sock, idx, sent_tick } per in-flight ping
local PING_TIMEOUT = 180  -- ticks (~3 s)

-- ── helpers ──────────────────────────────────────────────────────────────
local function gtick()
    return (mod and mod.game and mod.game.tick_count and mod.game.tick_count()) or 0
end

local function parse_server_list(text)
    if type(text) ~= "string" then return nil end
    -- Strip UTF-8 BOM if present
    if text:sub(1, 3) == "\xEF\xBB\xBF" then text = text:sub(4) end
    local ok, decoded = pcall(function() return json.decode(text) end)
    if not ok or type(decoded) ~= "table" then return nil end
    local out = {}
    for _, v in ipairs(decoded) do
        if type(v) == "table" and type(v.host) == "string" and v.host ~= "" then
            out[#out + 1] = {
                name   = tostring(v.name   or v.host),
                host   = tostring(v.host),
                port   = tonumber(v.port)   or 7878,
                region = tostring(v.region or ""),
            }
        end
    end
    return #out > 0 and out or nil
end

local function apply_list(new_list)
    -- Preserve selected server by host:port if possible
    local old_host = list[selected_idx] and list[selected_idx].host
    local old_port = list[selected_idx] and list[selected_idx].port
    list  = new_list
    pings = {}
    -- Try to keep the same server selected
    selected_idx = 1
    if old_host then
        for i, s in ipairs(list) do
            if s.host == old_host and s.port == old_port then
                selected_idx = i
                break
            end
        end
    end
end

local function abort_fetch()
    if http_handle ~= nil then
        if mod.http then mod.http.cancel(http_handle) end
        http_handle = nil
    end
end

local function start_ping(idx)
    if not list[idx] then return end
    local s, _ = mod.net.connect(list[idx].host, list[idx].port)
    if not s then return end
    ping_socks[#ping_socks + 1] = { sock = s, idx = idx, sent = gtick() }
end

-- ── public API ───────────────────────────────────────────────────────────
function servers.get_list()         return list end
function servers.get_selected_idx() return selected_idx end
function servers.get_selected()     return list[selected_idx] end
function servers.get_ping(idx)      return pings[idx] end
function servers.is_fetching()      return http_handle ~= nil end
function servers.get_fetch_status() return fetch_status, fetch_error end

function servers.select(idx)
    if list[idx] then
        selected_idx = idx
    end
end

-- Call this when a connection to the selected server is successfully initiated,
-- so it's restored as the default next session.
function servers.mark_connected()
    local sv = list[selected_idx]
    if sv then
        storage.set("last_server_host", sv.host)
        storage.set("last_server_port", sv.port)
        storage.set("last_server_name", sv.name)
    end
end

-- Called once when the hub opens — starts the HTTP fetch and re-pings all servers.
function servers.refresh()
    -- Abort any in-flight work
    abort_fetch()
    for _, p in ipairs(ping_socks) do mod.net.close(p.sock) end
    ping_socks = {}

    if UPDATE_URL == "" or not mod.http then
        fetch_status = "no_http"
        fetch_error  = UPDATE_URL == "" and "server_list_url not set in config" or "mod.http unavailable (rebuild DLL)"
        mod.warn("[servers] " .. fetch_error)
        for i = 1, #list do start_ping(i) end
        return
    end

    local handle, err = mod.http.get(UPDATE_URL)
    if not handle then
        fetch_status = "error"
        fetch_error  = tostring(err)
        mod.warn("[servers] http.get failed: " .. fetch_error)
        for i = 1, #list do start_ping(i) end
        return
    end
    fetch_status = "fetching"
    fetch_error  = nil
    http_handle  = handle
end

-- Must be called every frame while the hub is open.
function servers.update()
    -- ── HTTP fetch poll ──────────────────────────────────────────────────
    if http_handle ~= nil then
        local status, body = mod.http.poll(http_handle)
        if status == "done" then
            http_handle = nil
            mod.log(string.format("[servers] body len=%d first=%q", #(body or ""), (body or ""):sub(1, 60)))
            local parsed = parse_server_list(body)
            if parsed then
                apply_list(parsed)
                fetch_status = "ok"
                fetch_error  = nil
                mod.log(string.format("[servers] fetched %d servers", #list))
            else
                fetch_status = "error"
                fetch_error  = "bad JSON: " .. (body or ""):sub(1, 40)
                mod.warn("[servers] could not parse server list: " .. tostring(fetch_error))
            end
            for i = 1, #list do start_ping(i) end
        elseif status == "error" then
            fetch_status = "error"
            fetch_error  = tostring(body)
            mod.warn("[servers] fetch failed: " .. fetch_error)
            http_handle = nil
            for i = 1, #list do start_ping(i) end
        end
        -- "pending" → do nothing, check again next frame
    end

    -- ── Ping state machine ───────────────────────────────────────────────
    local now  = gtick()
    local keep = {}
    for _, p in ipairs(ping_socks) do
        local r = mod.net.check(p.sock)
        if r == "connected" then
            -- TCP handshake completed — that's our RTT measurement
            local rtt = (now - p.sent) * (1000.0 / 60.0)
            pings[p.idx] = math.max(1, math.floor(rtt + 0.5))
            mod.net.close(p.sock)
        elseif r == "failed" or (now - p.sent) > PING_TIMEOUT then
            pings[p.idx] = nil   -- unreachable / timed out
            mod.net.close(p.sock)
        else
            keep[#keep + 1] = p
        end
    end
    ping_socks = keep
end

-- On first load, seed the list with the last-used server from storage so the
-- picker and get_selected() work immediately before the fetch completes.
do
    local h = storage.get("last_server_host")
    local p = storage.get("last_server_port")
    local n = storage.get("last_server_name")
    if type(h) == "string" and h ~= "" then
        apply_list({ { name = tostring(n or h), host = h, port = tonumber(p) or 7878, region = "" } })
    end
end
