-- lib/proto.lua - newline-delimited JSON framing over mod.net TCP socket.
-- Verbose diagnostics build.

proto = proto or {}

local sock = nil
local recv_buf = ""
local msg_q = {}
local state = "disconnected"
local verbose = config.get("verbose_logging", true)

local sent_count = 0
local recv_count = 0
local bytes_sent = 0
local bytes_recv = 0
local last_send_tick = -1
local last_recv_tick = -1
local idle_warn_send = -999999
local idle_warn_recv = -999999

local function now_tick()
    if mod and mod.game and mod.game.tick_count then
        return mod.game.tick_count() or 0
    end
    return 0
end

local function vlog(msg)
    if verbose then
        mod.log("[proto] " .. tostring(msg))
    end
end

local function summarize_message(t)
    if not t or type(t) ~= "table" then return tostring(t) end
    local ty = tostring(t.type or "?")
    if ty == "sync_ready" then
        return string.format(
            "sync_ready tile=%s room=%s seed=%s tick=%s delay=%s authority=%s",
            tostring(t.tile_hash), tostring(t.room_index), tostring(t.seed),
            tostring(t.start_tick), tostring(t.input_delay), tostring(t.authority_role))
    elseif ty == "sync_begin" then
        return string.format("sync_begin authority=%s seed=%s tick=%s delay=%s",
            tostring(t.authority_role), tostring(t.seed), tostring(t.start_tick), tostring(t.input_delay))
    elseif ty == "input" then
        return string.format("input frame=%s cmd=%s", tostring(t.frame or t.seq), tostring(t.cmd))
    elseif ty == "remote_input" then
        return string.format("remote_input role=%s frame=%s cmd=%s", tostring(t.role), tostring(t.frame or t.seq), tostring(t.cmd))
    elseif ty == "snapshot" then
        local d = t.data
        if type(d) == "table" and d._kind == "correction" then
            return string.format("snapshot/correction frame=%s hash=%s", tostring(d._history_frame or t.seq), tostring(d._hash))
        end
        return string.format("snapshot seq=%s", tostring(t.seq))
    elseif ty == "remote_snapshot" then
        local d = t.data
        if type(d) == "table" and d._kind == "correction" then
            return string.format("remote_snapshot/correction frame=%s hash=%s", tostring(d._history_frame or t.seq), tostring(d._hash))
        end
        return string.format("remote_snapshot seq=%s", tostring(t.seq))
    elseif ty == "sword_snapshot" or ty == "remote_sword_snapshot" then
        return string.format("%s seq=%s", ty, tostring(t.seq))
    elseif ty == "frame_hash" then
        return string.format("frame_hash frame=%s hash=%s", tostring(t.frame), tostring(t.hash))
    elseif ty == "sync_error" then
        return string.format("sync_error reason=%s", tostring(t.reason))
    elseif ty == "match_found" then
        return string.format("match_found role=%s map_sel=%s map=%s key=%s",
            tostring(t.role), tostring(t.map_sel), tostring(t.map_label), tostring(t.map_key))
    elseif ty == "ping" or ty == "pong" then
        return string.format("%s seq=%s", ty, tostring(t.seq))
    elseif ty == "match_start" then
        return string.format("match_start role=%s map_sel=%s map=%s key=%s",
            tostring(t.role), tostring(t.map_sel), tostring(t.map_label), tostring(t.map_key))
    elseif ty == "auth_ok" or ty == "auth_fail" or ty == "queue_update" or ty == "ready" then
        return ty
    end
    return ty
end

function proto.connect(host, port)
    if sock ~= nil then proto.disconnect() end
    mod.log("[proto] connecting to " .. host .. ":" .. tostring(port))
    local s, err = mod.net.connect(host, port)
    if not s then
        mod.warn("[proto] connect failed: " .. tostring(err))
        state = "disconnected"
        return false
    end
    sock = s
    recv_buf = ""
    msg_q = {}
    state = "connecting"
    sent_count = 0
    recv_count = 0
    bytes_sent = 0
    bytes_recv = 0
    last_send_tick = -1
    last_recv_tick = -1
    idle_warn_send = -999999
    idle_warn_recv = -999999
    return true
end

function proto.disconnect()
    if sock ~= nil then mod.net.close(sock) end
    sock = nil
    recv_buf = ""
    msg_q = {}
    state = "disconnected"
    mod.log(string.format(
        "[proto] disconnected sent=%d recv=%d bytes_out=%d bytes_in=%d",
        sent_count, recv_count, bytes_sent, bytes_recv))
end

function proto.send(t)
    if state ~= "connected" then
        mod.warn("[proto] send dropped while state=" .. tostring(state) .. " type=" .. tostring(t and t.type))
        return false
    end
    local line = json.encode(t) .. "\n"
    local ok, _ = mod.net.send(sock, line)
    if not ok then
        mod.warn("[proto] send failed; disconnecting type=" .. tostring(t and t.type))
        proto.disconnect()
        return false
    end
    sent_count = sent_count + 1
    bytes_sent = bytes_sent + #line
    last_send_tick = now_tick()
    vlog(string.format("tx#%d bytes=%d %s", sent_count, #line, summarize_message(t)))
    return true
end

function proto.poll()
    if #msg_q == 0 then return nil end
    return table.remove(msg_q, 1)
end

function proto.get_state()
    return state
end

function proto.update()
    if state == "disconnected" then return end

    if state == "connecting" then
        local r = mod.net.check(sock)
        if r == "connected" then
            state = "connected"
            mod.log("[proto] connected")
        elseif r == "failed" then
            mod.warn("[proto] async connect failed")
            proto.disconnect()
        end
        return
    end

    while true do
        local chunk = mod.net.recv(sock)
        if chunk == false then
            mod.warn("[proto] recv error; disconnecting")
            proto.disconnect()
            break
        end
        if chunk == nil then break end
        bytes_recv = bytes_recv + #chunk
        recv_buf = recv_buf .. chunk

        while true do
            local nl = recv_buf:find("\n", 1, true)
            if not nl then break end
            local line = recv_buf:sub(1, nl - 1)
            recv_buf = recv_buf:sub(nl + 1)
            line = line:match("^%s*(.-)%s*$")
            if #line > 0 then
                local t, err = json.decode(line)
                if t then
                    recv_count = recv_count + 1
                    last_recv_tick = now_tick()
                    msg_q[#msg_q + 1] = t
                    vlog(string.format("rx#%d bytes=%d %s", recv_count, #line, summarize_message(t)))
                else
                    mod.warn("[proto] JSON decode failed: " .. tostring(err) .. " raw=" .. line:sub(1, 160))
                end
            end
        end
    end

    local tick = now_tick()
    if state == "connected" then
        if last_send_tick >= 0 and (tick - last_send_tick) > 180 and (tick - idle_warn_send) > 180 then
            idle_warn_send = tick
            mod.warn(string.format("[proto] no outbound traffic for %d ticks", tick - last_send_tick))
        end
        if last_recv_tick >= 0 and (tick - last_recv_tick) > 180 and (tick - idle_warn_recv) > 180 then
            idle_warn_recv = tick
            mod.warn(string.format("[proto] no inbound traffic for %d ticks", tick - last_recv_tick))
        end
    end
end
