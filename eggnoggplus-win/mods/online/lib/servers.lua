-- lib/servers.lua
-- Fetches a server list from a URL over HTTP/1.0 TCP, pings each entry,
-- and tracks the currently selected server.
--
-- Server list JSON format (served at the configured update URL):
--   [ { "name": "US East", "host": "us-east.example.com", "port": 7878, "region": "us-east" }, ... ]

servers = servers or {}

local UPDATE_URL  = config.get("server_list_url", "")   -- e.g. "mysite.com/servers.json"
local UPDATE_PORT = config.get("server_list_port", 80)
local UPDATE_PATH = config.get("server_list_path", "/servers.json")

-- Built-in fallback — always usable even if the fetch fails
local FALLBACK = {
    { name = "Local",  host = config.get("server_host", "127.0.0.1"), port = config.get("server_port", 7878), region = "local" },
}

-- ── state ────────────────────────────────────────────────────────────────
local list           = {}          -- current known server list
local selected_idx   = 1           -- index into list
local pings          = {}          -- ping_ms per index, nil = not measured
local fetch_sock     = nil         -- TCP socket used for HTTP fetch
local fetch_buf      = ""
local fetch_state    = "idle"      -- idle | connecting | headers | body | done | error
local fetch_headers_done = false
local body_buf       = ""

local ping_socks     = {}          -- { sock, idx, sent_tick } per in-flight ping
local PING_TIMEOUT   = 180         -- ticks (~3 s)

-- ── helpers ──────────────────────────────────────────────────────────────
local function gtick()
    return (mod and mod.game and mod.game.tick_count and mod.game.tick_count()) or 0
end

local function parse_server_list(text)
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
    if fetch_sock then
        mod.net.close(fetch_sock)
        fetch_sock = nil
    end
    fetch_state = "idle"
    fetch_buf   = ""
    body_buf    = ""
    fetch_headers_done = false
end

local function start_ping(idx)
    if not list[idx] then return end
    local s, _ = mod.net.connect(list[idx].host, list[idx].port)
    if not s then return end
    ping_socks[#ping_socks + 1] = { sock = s, idx = idx, sent = gtick() }
end

-- ── public API ───────────────────────────────────────────────────────────
function servers.get_list()  return list end
function servers.get_selected_idx() return selected_idx end
function servers.get_selected() return list[selected_idx] or FALLBACK[1] end
function servers.get_ping(idx) return pings[idx] end

function servers.select(idx)
    if list[idx] then
        selected_idx = idx
        storage.set("selected_server_host", list[idx].host)
        storage.set("selected_server_port", list[idx].port)
    end
end

-- Called once when the hub opens — starts the HTTP fetch and re-pings all servers.
function servers.refresh()
    -- Abort any in-flight work
    abort_fetch()
    for _, p in ipairs(ping_socks) do mod.net.close(p.sock) end
    ping_socks = {}

    -- Start with fallback while we wait for the fetch
    if #list == 0 then apply_list(FALLBACK) end

    if UPDATE_URL == "" then
        -- No fetch URL configured; just ping the fallback list
        for i = 1, #list do start_ping(i) end
        return
    end

    -- Open TCP connection for HTTP/1.0 GET
    local s, err = mod.net.connect(UPDATE_URL, UPDATE_PORT)
    if not s then
        mod.warn("[servers] fetch connect failed: " .. tostring(err))
        for i = 1, #list do start_ping(i) end
        return
    end
    fetch_sock   = s
    fetch_state  = "connecting"
    fetch_buf    = ""
    body_buf     = ""
    fetch_headers_done = false
end

-- Must be called every frame while the hub is open.
function servers.update()
    -- ── HTTP fetch state machine ─────────────────────────────────────────
    if fetch_state == "connecting" then
        local r = mod.net.check(fetch_sock)
        if r == "connected" then
            local req = "GET " .. UPDATE_PATH .. " HTTP/1.0\r\nHost: " .. UPDATE_URL .. "\r\nConnection: close\r\n\r\n"
            mod.net.send(fetch_sock, req)
            fetch_state = "headers"
        elseif r == "failed" then
            mod.warn("[servers] fetch TCP connect failed")
            abort_fetch()
            for i = 1, #list do start_ping(i) end
        end

    elseif fetch_state == "headers" or fetch_state == "body" then
        while true do
            local chunk = mod.net.recv(fetch_sock)
            if chunk == false then
                -- Connection closed — if we have a body, try to parse it
                if body_buf ~= "" then
                    local parsed = parse_server_list(body_buf)
                    if parsed then
                        apply_list(parsed)
                        mod.log(string.format("[servers] fetched %d servers", #list))
                    else
                        mod.warn("[servers] fetch: could not parse server list")
                    end
                end
                abort_fetch()
                fetch_state = "done"
                for i = 1, #list do start_ping(i) end
                break
            end
            if chunk == nil then break end
            fetch_buf = fetch_buf .. chunk

            if not fetch_headers_done then
                -- Scan for end of headers
                local hend = fetch_buf:find("\r\n\r\n", 1, true)
                        or fetch_buf:find("\n\n", 1, true)
                if hend then
                    local header_block = fetch_buf:sub(1, hend)
                    -- Check HTTP status line
                    local code = header_block:match("HTTP/%S+ (%d+)")
                    if code ~= "200" then
                        mod.warn("[servers] fetch HTTP status: " .. tostring(code))
                        abort_fetch()
                        for i = 1, #list do start_ping(i) end
                        break
                    end
                    -- Find where body starts (skip the blank line)
                    local body_start = fetch_buf:find("\r\n\r\n", 1, true)
                    if body_start then
                        body_buf = fetch_buf:sub(body_start + 4)
                    else
                        body_start = fetch_buf:find("\n\n", 1, true)
                        body_buf = fetch_buf:sub(body_start + 2)
                    end
                    fetch_buf = ""
                    fetch_headers_done = true
                    fetch_state = "body"
                end
            else
                body_buf = body_buf .. fetch_buf
                fetch_buf = ""
            end
        end
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

-- Restore previously selected server from storage on first load
do
    local saved_host = storage.get("selected_server_host")
    local saved_port = storage.get("selected_server_port")
    apply_list(FALLBACK)
    -- Override fallback host/port if storage has something
    if type(saved_host) == "string" and saved_host ~= "" then
        FALLBACK[1].host = saved_host
        FALLBACK[1].port = type(saved_port) == "number" and saved_port or FALLBACK[1].port
        apply_list(FALLBACK)
    end
end
