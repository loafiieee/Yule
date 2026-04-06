-- lib/sync.lua
-- Online multiplayer sync: authority-based snapshot correction.
--
-- Model:
--   - Both players send ALL input bits every frame
--   - Both players inject remote input into the other player slot
--   - Authority (role 0) sends snapshots every 2 ticks to non-auth
--   - Non-auth applies: authority's player position, RNG, countdowns, leader
--   - Non-auth strips: snap.enemy (local player), entities, native_tick, game_level

sync = sync or {}
local bit = bit or require("bit")

local ALL_CMD_MASK = bit.bor(0x01, 0x02, 0x04, 0x08, 0x10, 0x20)

local SNAPSHOT_INTERVAL = 2
local PING_EVERY        = 60
local PING_HISTORY      = 8
local TIMEOUT_TICKS     = 600

-- ── state ────────────────────────────────────────────────────────────────
local active         = false
local live           = false
local local_idx      = 0
local remote_idx     = 1
local authority_role = 0
local is_authority   = false
local last_error     = nil
local waiting_reason = "idle"
local match_seed     = 0

local send_seq          = 0
local last_capture_tick = -1
local last_sent_cmd     = 0
local last_remote_cmd   = 0
local last_remote_role  = nil
local ticks_since_recv  = 0
local tick_count        = 0

local snapshot_seq      = 0
local snapshot_recv_seq = -1
local snapshots_sent    = 0
local snapshots_applied = 0
local pending_snapshot  = nil

local ping_seq     = 0
local ping_sent_at = {}
local ping_samples = {}
local current_ping = 0

local stat_inputs_sent = 0
local stat_inputs_recv = 0

local verbose = config.get("verbose_logging", true)

-- ── helpers ──────────────────────────────────────────────────────────────
local function vlog(msg)
    if verbose then mod.log("[sync] " .. tostring(msg)) end
end

local function gtick()
    return (mod.game and mod.game.tick_count and mod.game.tick_count()) or 0
end

local function qround(v)
    v = tonumber(v) or 0
    if v >= 0 then return math.floor(v + 0.5) end
    return math.ceil(v - 0.5)
end

local function avg_ping()
    if #ping_samples == 0 then return 0 end
    local total = 0
    for _, v in ipairs(ping_samples) do total = total + v end
    return total / #ping_samples
end

-- ── input reading ────────────────────────────────────────────────────────
local function read_slot(player_index)
    if mod.game and mod.game.poll_cmds then
        local eff = bit.band(tonumber(mod.game.poll_cmds(player_index, 2)) or 0, ALL_CMD_MASK)
        if eff ~= 0 then return eff end
    end
    if mod.game and mod.game.poll_cmds_raw then
        local raw = bit.band(tonumber(mod.game.poll_cmds_raw(player_index, 2)) or 0, ALL_CMD_MASK)
        if raw ~= 0 then return raw end
    end
    if mod.game and mod.game.poll_cmds then
        local eff = bit.band(tonumber(mod.game.poll_cmds(player_index, 1)) or 0, ALL_CMD_MASK)
        if eff ~= 0 then return eff end
    end
    if mod.game and mod.game.poll_cmds_raw then
        local raw = bit.band(tonumber(mod.game.poll_cmds_raw(player_index, 1)) or 0, ALL_CMD_MASK)
        if raw ~= 0 then return raw end
    end
    return 0
end

local function read_local_cmd()
    return read_slot(local_idx)
end

-- ── remote input injection ───────────────────────────────────────────────
local function apply_remote_input()
    if not (mod.game and mod.game.set_input) then return false end
    mod.game.set_input(remote_idx, last_remote_cmd, 2, true)
    return true
end

-- ── snapshot: outgoing (authority only) ──────────────────────────────────

local function take_and_send_snapshot()
    if not (mod.game and mod.game.snapshot) then return end

    local snap = mod.game.snapshot(local_idx, false)
    if not snap then return end

    if mod.game.rng_seed then
        snap.rng_seed = mod.game.rng_seed()
    end

    -- Strip convenience duplicates
    snap.player_x = nil;  snap.player_y = nil
    snap.player_vx = nil; snap.player_vy = nil
    snap.player_has_sword = nil; snap.player_facing = nil; snap.player_grounded = nil
    snap.enemy_x = nil;  snap.enemy_y = nil
    snap.enemy_dx = nil; snap.enemy_dy = nil
    snap.enemy_vx = nil; snap.enemy_vy = nil
    snap.enemy_has_sword = nil; snap.enemy_facing = nil; snap.enemy_grounded = nil
    snap.nearest_sword_dx = nil; snap.nearest_sword_dy = nil
    snap.nearest_sword_x = nil;  snap.nearest_sword_y = nil
    snap.tiles_of_current_room = nil
    snap.tick = nil; snap.in_game = nil
    snap.room_width = nil; snap.room_height = nil; snap.error = nil

    -- Strip entities (apply_snapshot deactivates missing slots = destructive)
    snap.entities = nil

    -- Strip enemy (non-auth's player from auth's delayed view — stale)
    snap.enemy = nil

    -- Never send these
    snap.native_tick = nil
    snap.game_level = nil

    -- Strip player convenience booleans
    if snap.player then
        snap.player.dx = nil; snap.player.dy = nil
        snap.player.grounded = nil; snap.player.ceiling = nil
        snap.player.wall_right = nil; snap.player.wall_left = nil
    end

    snapshot_seq = snapshot_seq + 1
    snapshots_sent = snapshots_sent + 1

    proto.send({
        type = "snapshot",
        seq  = snapshot_seq,
        data = snap,
    })
end

-- ── snapshot: apply (non-authority only) ─────────────────────────────────

local function apply_snapshot_data(snap)
    if not snap or type(snap) ~= "table" then return false end
    if not (mod.game and mod.game.apply_snapshot) then return false end

    snap.native_tick = nil
    snap.game_level = nil
    snap.entities = nil
    snap.enemy = nil

    if snap.rng_seed and mod.game.set_rng_seed then
        mod.game.set_rng_seed(snap.rng_seed)
    end

    local ok = mod.game.apply_snapshot(snap)
    if ok then
        snapshots_applied = snapshots_applied + 1
    end
    return ok
end

-- ── lifecycle ────────────────────────────────────────────────────────────
local function reset_state()
    active = false; live = false
    local_idx = 0; remote_idx = 1
    authority_role = 0; is_authority = false
    last_error = nil; waiting_reason = "idle"; match_seed = 0

    send_seq = 0; last_capture_tick = -1
    last_sent_cmd = 0; last_remote_cmd = 0; last_remote_role = nil
    ticks_since_recv = 0; tick_count = 0

    snapshot_seq = 0; snapshot_recv_seq = -1
    snapshots_sent = 0; snapshots_applied = 0; pending_snapshot = nil

    ping_seq = 0; ping_sent_at = {}; ping_samples = {}; current_ping = 0
    stat_inputs_sent = 0; stat_inputs_recv = 0
end

function sync.start(local_player_index, auth_role, seed)
    reset_state()

    local_idx      = math.max(0, math.min(1, tonumber(local_player_index) or 0))
    remote_idx     = 1 - local_idx
    authority_role = tonumber(auth_role) or 0
    is_authority   = (local_idx == authority_role)
    match_seed     = tonumber(seed) or 0

    if proto.get_state and proto.get_state() ~= "connected" then
        last_error = "Not connected."
        mod.warn("[sync] " .. last_error)
        return false
    end

    if not (mod.game and mod.game.set_input and mod.game.input_clear and mod.game.block_raw_input) then
        last_error = "Required relay APIs are unavailable."
        mod.warn("[sync] " .. last_error)
        return false
    end

    mod.game.input_clear(0)
    mod.game.input_clear(1)
    mod.game.block_raw_input(local_idx, false)
    mod.game.block_raw_input(remote_idx, true)

    if match_seed ~= 0 and mod.game.set_rng_seed then
        mod.game.set_rng_seed(match_seed)
        vlog(string.format("set RNG seed to %u", match_seed))
    end

    active = true
    live   = true
    waiting_reason = "live"
    mod.log(string.format(
        "[sync] started  role=%d  remote=%d  authority=%d  is_auth=%s  seed=%u",
        local_idx, remote_idx, authority_role, tostring(is_authority), match_seed))
    return true
end

function sync.stop()
    if mod and mod.game and mod.game.input_clear then
        mod.game.input_clear(0); mod.game.input_clear(1)
    end
    if mod and mod.game and mod.game.block_raw_input then
        mod.game.block_raw_input(0, false); mod.game.block_raw_input(1, false)
    end
    if active then
        mod.log(string.format(
            "[sync] stopped  sent=%d recv=%d snap_out=%d snap_in=%d ping=%.0fms err=%s",
            stat_inputs_sent, stat_inputs_recv, snapshots_sent, snapshots_applied,
            current_ping, tostring(last_error)))
    end
    reset_state()
end

function sync.is_active() return active end
function sync.get_error()  return last_error end

-- ── per-tick ─────────────────────────────────────────────────────────────
function sync.apply_pending()
    if pending_snapshot and not is_authority then
        apply_snapshot_data(pending_snapshot)
        pending_snapshot = nil
    end
end

function sync.tick()
    if not active then return nil end

    ticks_since_recv = ticks_since_recv + 1
    if ticks_since_recv > TIMEOUT_TICKS then
        last_error = "Connection timed out."
        mod.warn("[sync] " .. last_error)
        return "timeout"
    end

    if not live then
        waiting_reason = "waiting"
        return nil
    end

    waiting_reason = "live"
    tick_count = tick_count + 1

    if not apply_remote_input() then
        last_error = "Failed to inject remote input."
        mod.warn("[sync] " .. last_error)
        return "sync_error"
    end

    if is_authority and (tick_count % SNAPSHOT_INTERVAL) == 0 then
        take_and_send_snapshot()
    end

    return nil
end

-- ── per-frame ────────────────────────────────────────────────────────────
function sync.frame()
    if not active or not live then return nil end

    local now = gtick()
    if last_capture_tick == now then return nil end
    last_capture_tick = now

    if (now % PING_EVERY) == 0 then
        ping_seq = ping_seq + 1
        ping_sent_at[ping_seq] = now
        proto.send({ type = "ping", seq = ping_seq })
    end

    last_sent_cmd = read_local_cmd()
    send_seq = send_seq + 1

    if not proto.send({
        type  = "input",
        seq   = send_seq,
        frame = send_seq,
        cmd   = last_sent_cmd,
    }) then
        last_error = "Failed to send input."
        mod.warn("[sync] " .. last_error)
        return "sync_error"
    end

    stat_inputs_sent = stat_inputs_sent + 1
    return nil
end

-- ── message handler ──────────────────────────────────────────────────────
function sync.on_message(msg)
    if not active or not msg then return nil end
    local t = msg.type

    if t == "remote_input" then
        ticks_since_recv = 0
        last_remote_cmd  = bit.band(tonumber(msg.cmd) or 0, ALL_CMD_MASK)
        last_remote_role = tonumber(msg.role)
        stat_inputs_recv = stat_inputs_recv + 1
        return nil
    end

    if t == "remote_snapshot" then
        ticks_since_recv = 0
        local seq = qround(msg.seq or 0)
        if seq > snapshot_recv_seq then
            snapshot_recv_seq = seq
            pending_snapshot  = msg.data
        end
        return nil
    end

    if t == "ping" then
        proto.send({ type = "pong", seq = msg.seq })
        return nil
    end

    if t == "pong" then
        local seq = tonumber(msg.seq)
        if seq and ping_sent_at[seq] then
            local rtt = (gtick() - ping_sent_at[seq]) * (1000.0 / 60.0)
            ping_sent_at[seq] = nil
            ping_samples[#ping_samples + 1] = math.max(0, rtt)
            if #ping_samples > PING_HISTORY then
                table.remove(ping_samples, 1)
            end
            current_ping = avg_ping()
        end
        return nil
    end

    if t == "match_end" then return "match_end" end

    if t == "sync_error" then
        last_error = tostring(msg.reason or "Sync failed.")
        mod.warn("[sync] " .. last_error)
        return "sync_error"
    end

    return nil
end

-- ── stats for HUD ────────────────────────────────────────────────────────
function sync.stats()
    return {
        role              = local_idx,
        authority_role    = authority_role,
        is_authority      = is_authority,
        local_source      = local_idx,
        local_cmd         = last_sent_cmd,
        remote_cmd        = last_remote_cmd,
        remote_role       = last_remote_role,
        ping_ms           = current_ping,
        sync_started      = live,
        waiting_for_first_snapshot = false,
        waiting_reason    = waiting_reason,
        ticks_no_recv     = ticks_since_recv,
        inputs_sent       = stat_inputs_sent,
        inputs_recv       = stat_inputs_recv,
        snapshots_sent    = snapshots_sent,
        snapshots_recv    = snapshot_recv_seq >= 0 and (snapshot_recv_seq + 1) or 0,
        snapshots_applied = snapshots_applied,
        restores          = 0,
        latest_snapshot_seq  = snapshot_recv_seq,
        applied_snapshot_seq = snapshots_applied > 0 and snapshot_recv_seq or -1,
        current_state_seq = send_seq,
        resend_every      = SNAPSHOT_INTERVAL,
        error             = last_error,
        input_delay       = 0,
        native_tick       = gtick(),
        rng_seed          = match_seed,
        hashes_sent       = 0,
    }
end
