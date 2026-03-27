-- lib/sync.lua - authoritative online sync.
-- P1 is the host authority. Both clients send local input to the server.
-- The host simulates the match and streams snapshots to P2.
-- P2 predicts locally for responsiveness but continuously corrects to host state.

sync = sync or {}

local active = false
local local_idx = 0
local remote_idx = 1
local is_host = false
local map_selector = nil

local local_source = nil
local send_seq = 0
local recv_input_seq = -1
local last_remote_cmd = 0

local latest_snapshot = nil
local latest_snapshot_tick = -1
local applied_snapshot_tick = -1
local waiting_for_first_snapshot = false
local host_snapshot_tick = -1

local function choose_local_source(raw0, raw1)
    if raw0 ~= 0 and raw1 == 0 then
        return 0
    elseif raw1 ~= 0 and raw0 == 0 then
        return 1
    elseif local_source ~= nil then
        return local_source
    else
        return local_idx
    end
end

function sync.prepare(role, sel)
    local_idx = tonumber(role) or 0
    if local_idx < 0 then local_idx = 0 end
    if local_idx > 1 then local_idx = 1 end
    remote_idx = 1 - local_idx
    is_host = (local_idx == 0)
    map_selector = sel

    local_source = nil
    send_seq = 0
    recv_input_seq = -1
    last_remote_cmd = 0
    latest_snapshot = nil
    latest_snapshot_tick = -1
    applied_snapshot_tick = -1
    waiting_for_first_snapshot = false
    host_snapshot_tick = -1
    active = false

    mod.log(string.format("[sync] prepared role=%d host=%s remote=%d map_sel=%s",
        local_idx, tostring(is_host), remote_idx, tostring(sel)))
end

function sync.apply_map()
    if map_selector ~= nil and mod.game.set_map_selector then
        mod.game.set_map_selector(map_selector)
        mod.log("[sync] map selector set to " .. tostring(map_selector))
    end
end

function sync.start()
    if active then return end
    send_seq = 0
    recv_input_seq = -1
    last_remote_cmd = 0
    latest_snapshot = nil
    latest_snapshot_tick = -1
    applied_snapshot_tick = -1
    waiting_for_first_snapshot = (not is_host) and (mod.game.apply_snapshot ~= nil)
    host_snapshot_tick = -1
    active = true
    mod.log("[sync] started" .. (waiting_for_first_snapshot and " (waiting for host snapshot)" or ""))
end

function sync.stop()
    active = false
    local_source = nil
    send_seq = 0
    recv_input_seq = -1
    last_remote_cmd = 0
    latest_snapshot = nil
    latest_snapshot_tick = -1
    applied_snapshot_tick = -1
    waiting_for_first_snapshot = false
    host_snapshot_tick = -1
    if mod and mod.game then
        mod.game.input_clear(0)
        mod.game.input_clear(1)
    end
    mod.log("[sync] stopped")
end

function sync.is_active()
    return active
end

function sync.on_remote_input(tick, cmd)
    local seq = tonumber(tick) or 0
    if seq >= recv_input_seq then
        recv_input_seq = seq
        last_remote_cmd = tonumber(cmd) or 0
    end
end

function sync.on_snapshot(tick, snap)
    if is_host then return end
    local seq = tonumber(tick) or (snap and tonumber(snap.tick)) or 0
    if seq < latest_snapshot_tick then return end
    latest_snapshot_tick = seq
    latest_snapshot = snap
end

local function apply_latest_snapshot_if_needed()
    if is_host or not latest_snapshot or not mod.game.apply_snapshot then
        return
    end
    if latest_snapshot_tick <= applied_snapshot_tick then
        return
    end
    local ok = mod.game.apply_snapshot(latest_snapshot)
    if ok then
        applied_snapshot_tick = latest_snapshot_tick
        waiting_for_first_snapshot = false
    end
end

function sync.tick()
    if not active then return end

    local raw0 = mod.game.poll_cmds_raw(0) or 0
    local raw1 = mod.game.poll_cmds_raw(1) or 0
    local picked = choose_local_source(raw0, raw1)
    if local_source ~= picked then
        local_source = picked
        mod.log(string.format("[sync] local input source=%d for role=%d", local_source, local_idx))
    end

    local raw = (local_source == 1) and raw1 or raw0
    send_seq = send_seq + 1
    proto.send({ type = "input", tick = send_seq, cmd = raw })

    if is_host then
        mod.game.set_input(local_idx, raw, 1, true)
        mod.game.set_input(remote_idx, last_remote_cmd, 1, true)
    else
        if waiting_for_first_snapshot then
            mod.game.set_input(0, 0, 1, true)
            mod.game.set_input(1, 0, 1, true)
        else
            mod.game.set_input(local_idx, raw, 1, true)
            mod.game.set_input(remote_idx, last_remote_cmd, 1, true)
        end
    end
end

function sync.frame()
    if not active then return end

    apply_latest_snapshot_if_needed()

    if is_host then
        local snap = mod.game.snapshot(0, false)
        if snap and snap.in_game then
            host_snapshot_tick = tonumber(snap.tick) or host_snapshot_tick
            proto.send({ type = "snapshot", tick = host_snapshot_tick, snap = snap })
        end
    end
end

function sync.draw_hud()
    if not active then return end
    local W, _ = mod.ui.screen_size()
    local role_str = is_host and "HOST/P1" or "CLIENT/P2"
    local src_str = (local_source == nil) and "?" or tostring(local_source + 1)
    local wait_str = waiting_for_first_snapshot and " WAIT_SYNC" or ""
    local snap_str = is_host and (" snap" .. tostring(host_snapshot_tick)) or (" snap" .. tostring(applied_snapshot_tick))
    local info = role_str .. " src" .. src_str .. " tx" .. tostring(send_seq) .. " rx" .. tostring(recv_input_seq) .. snap_str .. wait_str
    mod.ui.text_at(info, W - 8, 8, 0.75, 1, 1, 0)
end
