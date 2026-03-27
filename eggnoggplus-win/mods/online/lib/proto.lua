-- lib/proto.lua  –  newline-delimited JSON framing over mod.net TCP socket.
-- Sets the global `proto` table.
--
-- Protocol:  each message is json.encode(t) .. "\n"
-- Call proto.update() once per frame to drain incoming bytes.
-- Call proto.poll() to get the next parsed incoming message, or nil.

proto = proto or {}

local sock     = nil   -- mod.net slot handle (integer)
local recv_buf = ""    -- accumulated receive bytes (may span many frames)
local msg_q    = {}    -- queue of decoded incoming message tables
local state    = "disconnected"  -- "disconnected" | "connecting" | "connected"

-- ── Public API ──────────────────────────────────────────────────────────────

--- Begin connecting.  Does NOT block.
function proto.connect(host, port)
    if sock ~= nil then proto.disconnect() end
    mod.log("[proto] connecting to " .. host .. ":" .. tostring(port))
    local s, err = mod.net.connect(host, port)
    if not s then
        mod.warn("[proto] connect failed: " .. tostring(err))
        state = "disconnected"
        return false
    end
    sock  = s
    state = "connecting"
    return true
end

--- Cleanly close the connection.
function proto.disconnect()
    if sock ~= nil then mod.net.close(sock) end
    sock      = nil
    recv_buf  = ""
    msg_q     = {}
    state     = "disconnected"
    mod.log("[proto] disconnected")
end

--- Send a table as a JSON line.
--- Returns true on success, false if not connected or send failed.
function proto.send(t)
    if state ~= "connected" then return false end
    local line = json.encode(t) .. "\n"
    local ok, _ = mod.net.send(sock, line)
    if not ok then
        mod.warn("[proto] send failed; disconnecting")
        proto.disconnect()
        return false
    end
    return true
end

--- Returns the next received message table, or nil if the queue is empty.
function proto.poll()
    if #msg_q == 0 then return nil end
    return table.remove(msg_q, 1)
end

--- Current connection state string.
function proto.get_state() return state end

--- Call once per frame to advance the connection state and drain incoming data.
function proto.update()
    if state == "disconnected" then return end

    -- Finish async connect handshake
    if state == "connecting" then
        local r = mod.net.check(sock)
        if r == "connected" then
            state = "connected"
            mod.log("[proto] connected")
        elseif r == "failed" then
            mod.warn("[proto] async connect failed")
            proto.disconnect()
        end
        return -- wait until next frame to recv
    end

    -- state == "connected": drain all available bytes
    while true do
        local chunk = mod.net.recv(sock)
        if chunk == false then
            -- error / remote close
            mod.warn("[proto] recv error; disconnecting")
            proto.disconnect()
            break
        end
        if chunk == nil then break end  -- no more data right now
        recv_buf = recv_buf .. chunk

        -- Parse complete lines out of the buffer
        while true do
            local nl = recv_buf:find("\n", 1, true)
            if not nl then break end
            local line = recv_buf:sub(1, nl - 1)
            recv_buf   = recv_buf:sub(nl + 1)
            line = line:match("^%s*(.-)%s*$") -- trim CR/LF/spaces
            if #line > 0 then
                local t, err = json.decode(line)
                if t then
                    msg_q[#msg_q + 1] = t
                else
                    mod.warn("[proto] JSON decode failed: " .. tostring(err) ..
                             " raw=" .. line:sub(1, 80))
                end
            end
        end
    end
end
