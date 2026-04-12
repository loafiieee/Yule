-- lib/sync.lua
-- Online multiplayer sync: rollback netcode with input prediction.
--
-- Model:
--   - Both peers sample local raw input on each gameplay tick.
--   - Both peers block native input for BOTH player slots and inject deterministic
--     scheduled input for the current gameplay tick.
--   - Missing remote input is predicted from the last resolved remote command.
--   - A full pre-tick snapshot is stored for every simulated frame in a rolling
--     history window.
--   - When a late remote input differs from our prediction, we roll back to the
--     stored pre-tick snapshot for that frame, replay forward with confirmed
--     inputs plus prediction for the remaining unknown frames, and return to the
--     live pre-tick state before the native engine advances.
--   - Authority periodically sends a full correction snapshot for a historical
--     pre-tick frame. The non-authority peer uses that only as a desync repair
--     anchor; gameplay is driven by rollback/prediction, not by continuous snap
--     correction.
--   - If a correction arrives outside the rollback window, we apply it directly
--     and preserve prev_x/prev_y where possible to soften the visible pop.

sync_rollback = sync_rollback or {}
local bit = bit or require("bit")

local ALL_CMD_MASK = bit.bor(0x01, 0x02, 0x04, 0x08, 0x10, 0x20)
local U32_MOD      = 4294967296

local HISTORY_FRAMES      = config.get("rollback_history_frames", 120)
local CORRECTION_INTERVAL = config.get("correction_interval", 30)
local PING_EVERY          = 60
local PING_HISTORY        = 8
local TIMEOUT_TICKS       = 600

-- ── state ────────────────────────────────────────────────────────────────
local active         = false
local live           = false
local local_role     = 0
local local_slot_idx = 0
local remote_slot_idx= 1
local authority_role = 0
local is_authority   = false
local last_error     = nil
local waiting_reason = "idle"
local match_seed     = 0

local next_frame               = 0 -- next gameplay frame to simulate
local last_sent_cmd            = 0
local last_remote_cmd          = 0
local last_remote_role         = nil
local last_confirmed_remote    = 0
local highest_remote_frame     = -1
local highest_confirmed_frame  = -1
local ticks_since_recv         = 0

local state_history            = {} -- pre-tick world snapshot for frame F
local local_input_history      = {}
local remote_input_history     = {}
local predicted_remote_history = {}
local sent_correction_frames   = {}

local rollback_target_frame    = nil
local rollback_target_snapshot = nil
local rollback_reason          = nil

local ping_seq     = 0
local ping_sent_at = {}
local ping_samples = {}
local current_ping = 0

local stat_inputs_sent        = 0
local stat_inputs_recv        = 0
local stat_rollbacks          = 0
local stat_prediction_misses  = 0
local stat_corrections_sent   = 0
local stat_corrections_recv   = 0
local stat_corrections_applied= 0
local stat_emergency_applies  = 0
local stat_resimulated_frames = 0

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

local function clamp_history_frame(frame)
    local min_frame = next_frame - HISTORY_FRAMES
    if min_frame < 0 then min_frame = 0 end
    if frame < min_frame then return min_frame end
    return frame
end

local function history_floor()
    local min_frame = next_frame - HISTORY_FRAMES
    if min_frame < 0 then min_frame = 0 end
    return min_frame
end

local function prune_history()
    local floor = history_floor()
    for k in pairs(state_history) do if k < floor then state_history[k] = nil end end
    for k in pairs(local_input_history) do if k < floor then local_input_history[k] = nil end end
    for k in pairs(remote_input_history) do if k < floor then remote_input_history[k] = nil end end
    for k in pairs(predicted_remote_history) do if k < floor then predicted_remote_history[k] = nil end end
    for k in pairs(sent_correction_frames) do if k < floor then sent_correction_frames[k] = nil end end
end

local function pack_snapshot_for_history(snap)
    if not snap or type(snap) ~= "table" then return nil end

    -- Remove large legacy convenience duplicates that do not participate in restore.
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
    snap.error = nil
    snap.room_width = nil
    snap.room_height = nil
    snap.tick = nil
    snap.in_game = nil
    return snap
end

local function canonicalize_entities_in_place(ents)
    if type(ents) ~= "table" then return end
    table.sort(ents, function(a, b)
        local a_slot = tonumber(a and a.slot) or -1
        local b_slot = tonumber(b and b.slot) or -1
        if a_slot ~= b_slot then return a_slot < b_slot end

        local a_type = tonumber(a and a.type) or -1
        local b_type = tonumber(b and b.type) or -1
        if a_type ~= b_type then return a_type < b_type end

        local a_room = tonumber(a and a.room_index) or -1
        local b_room = tonumber(b and b.room_index) or -1
        if a_room ~= b_room then return a_room < b_room end

        local a_x = tonumber(a and a.x) or 0
        local b_x = tonumber(b and b.x) or 0
        if a_x ~= b_x then return a_x < b_x end

        local a_y = tonumber(a and a.y) or 0
        local b_y = tonumber(b and b.y) or 0
        return a_y < b_y
    end)
end

local function canonicalize_snapshot_in_place(snap)
    if not snap or type(snap) ~= "table" then return snap end

    local player_index = qround(snap.player_index or 0) % 2
    local enemy_index = qround(snap.enemy_index or (1 - player_index)) % 2
    if player_index == enemy_index then
        enemy_index = 1 - player_index
    end

    if player_index ~= 0 or enemy_index ~= 1 then
        if player_index == 1 and enemy_index == 0 then
            snap.player, snap.enemy = snap.enemy, snap.player
        end
        snap.player_index = 0
        snap.enemy_index = 1
    end

    canonicalize_entities_in_place(snap.entities)
    return snap
end

local function capture_world_snapshot()
    if not (mod.game and mod.game.snapshot) then return nil end
    local snap = mod.game.snapshot(local_slot_idx, false)
    canonicalize_snapshot_in_place(snap)
    return pack_snapshot_for_history(snap)
end

local function append_scaled(parts, v, scale)
    parts[#parts + 1] = tostring(qround((tonumber(v) or 0) * (scale or 1)))
end

local function append_player(parts, p)
    if not p then
        parts[#parts + 1] = "p:nil"
        return
    end
    parts[#parts + 1] = "p"
    append_scaled(parts, p.x, 1000)
    append_scaled(parts, p.y, 1000)
    append_scaled(parts, p.prev_x, 1000)
    append_scaled(parts, p.prev_y, 1000)
    append_scaled(parts, p.vx, 1000)
    append_scaled(parts, p.vy, 1000)
    append_scaled(parts, p.state_id, 1)
    append_scaled(parts, p.state_timer, 1)
    append_scaled(parts, p.facing, 1)
    append_scaled(parts, p.room_index, 1)
    append_scaled(parts, p.jump_buffer, 1)
    append_scaled(parts, p.attack_buffer, 1)
    append_scaled(parts, p.collision_flags, 1)
    append_scaled(parts, p.prev_collision_flags, 1)
    append_scaled(parts, p.has_sword and 1 or 0, 1)
end

local function append_entities(parts, ents)
    if type(ents) ~= "table" then
        parts[#parts + 1] = "e:nil"
        return
    end
    parts[#parts + 1] = "e"
    for i = 1, #ents do
        local e = ents[i]
        parts[#parts + 1] = tostring(qround(e and e.slot or -1))
        parts[#parts + 1] = tostring(qround(e and e.type or -1))
        parts[#parts + 1] = tostring((e and e.active) and 1 or 0)
        append_scaled(parts, e and e.room_index or -1, 1)
        append_scaled(parts, e and e.x or 0, 1000)
        append_scaled(parts, e and e.y or 0, 1000)
        append_scaled(parts, e and e.prev_x or 0, 1000)
        append_scaled(parts, e and e.prev_y or 0, 1000)
        append_scaled(parts, e and e.vx or 0, 1000)
        append_scaled(parts, e and e.vy or 0, 1000)
        append_scaled(parts, e and e.state_id or 0, 1)
        append_scaled(parts, e and e.flags or 0, 1)
    end
end

local function snapshot_hash(snap)
    if not snap or type(snap) ~= "table" then return 0 end

    canonicalize_snapshot_in_place(snap)

    local parts = {}
    append_scaled(parts, snap.player_index or 0, 1)
    append_scaled(parts, snap.enemy_index or 1, 1)
    append_scaled(parts, snap.room_index or 0, 1)
    append_scaled(parts, snap.start_countdown or 0, 1)
    append_scaled(parts, snap.end_countdown or 0, 1)
    append_scaled(parts, snap.native_tick or 0, 1)
    append_scaled(parts, snap.rng_seed or 0, 1)
    append_scaled(parts, snap.game_level or 0, 1)
    append_scaled(parts, snap.leader_index or -1, 1)
    append_player(parts, snap.player)
    append_player(parts, snap.enemy)
    append_entities(parts, snap.entities)

    local s = table.concat(parts, "|")
    local h = 5381
    for i = 1, #s do
        h = (h * 33 + s:byte(i)) % U32_MOD
    end
    return h
end

local function read_local_raw_slot(player_index)
    if mod.game and mod.game.poll_cmds_raw then
        local raw = bit.band(tonumber(mod.game.poll_cmds_raw(player_index, 2)) or 0, ALL_CMD_MASK)
        if raw ~= 0 then return raw end
        raw = bit.band(tonumber(mod.game.poll_cmds_raw(player_index, 1)) or 0, ALL_CMD_MASK)
        if raw ~= 0 then return raw end
        return 0
    end
    if mod.game and mod.game.poll_cmds then
        local eff = bit.band(tonumber(mod.game.poll_cmds(player_index, 2)) or 0, ALL_CMD_MASK)
        if eff ~= 0 then return eff end
        eff = bit.band(tonumber(mod.game.poll_cmds(player_index, 1)) or 0, ALL_CMD_MASK)
        if eff ~= 0 then return eff end
    end
    return 0
end

local function set_control_slot(slot)
    slot = math.max(0, math.min(1, tonumber(slot) or 0))
    local_slot_idx = slot
    remote_slot_idx = 1 - slot
end

local function schedule_resim_input_pair(local_cmd, remote_cmd)
    if not (mod.game and mod.game.set_input) then return false end
    mod.game.set_input(local_slot_idx, local_cmd, 1, true)
    mod.game.set_input(remote_slot_idx, remote_cmd, 1, true)
    return true
end

local function schedule_live_remote_input(remote_cmd)
    if not (mod.game and mod.game.set_input) then return false end
    mod.game.set_input(remote_slot_idx, remote_cmd, 2, true)
    return true
end

local function clear_inputs()
    if mod and mod.game and mod.game.input_clear then
        mod.game.input_clear(0)
        mod.game.input_clear(1)
    end
end

local function set_live_input_blocking()
    if mod and mod.game and mod.game.block_raw_input then
        mod.game.block_raw_input(local_slot_idx, false)
        mod.game.block_raw_input(remote_slot_idx, true)
    end
end

local function read_local_cmd()
    return read_local_raw_slot(local_slot_idx)
end

local function expected_remote_role()
    return 1 - local_role
end

local function looks_like_local_echo(frame, cmd)
    local local_cmd = local_input_history[frame]
    if local_cmd == nil then return false end
    return bit.band(tonumber(local_cmd) or 0, ALL_CMD_MASK) == bit.band(tonumber(cmd) or 0, ALL_CMD_MASK)
end

local function normalize_remote_role(msg_role, frame, cmd)
    local expected = expected_remote_role()
    if msg_role == nil then
        return expected, true
    end
    if msg_role == expected then
        return expected, true
    end
    if msg_role == local_role then
        if looks_like_local_echo(frame, cmd) then
            vlog(string.format("dropped echoed local input role=%d frame=%d cmd=%d", msg_role, frame, cmd))
            return expected, false
        end
        vlog(string.format("accepting mislabeled remote input role=%d->%d frame=%d cmd=%d", msg_role, expected, frame, cmd))
        return expected, true
    end
    vlog(string.format("accepting unexpected remote role=%s as role=%d frame=%d cmd=%d", tostring(msg_role), expected, frame, cmd))
    return expected, true
end

local function set_resim_input_blocking()
    if mod and mod.game and mod.game.block_raw_input then
        mod.game.block_raw_input(0, true)
        mod.game.block_raw_input(1, true)
    end
end

local function resolved_remote_cmd(frame)
    local confirmed = remote_input_history[frame]
    if confirmed ~= nil then return confirmed, true end
    local predicted = predicted_remote_history[frame]
    if predicted ~= nil then return predicted, false end
    return nil, false
end

local function last_resolved_remote_before(frame)
    for f = frame - 1, history_floor(), -1 do
        local cmd = remote_input_history[f]
        if cmd ~= nil then return cmd end
        cmd = predicted_remote_history[f]
        if cmd ~= nil then return cmd end
    end
    return last_confirmed_remote or 0
end

local function predict_remote_cmd(frame)
    local confirmed = remote_input_history[frame]
    if confirmed ~= nil then
        predicted_remote_history[frame] = confirmed
        return confirmed, false
    end
    local predicted = predicted_remote_history[frame]
    if predicted ~= nil then return predicted, true end
    predicted = last_resolved_remote_before(frame)
    predicted_remote_history[frame] = predicted
    return predicted, true
end

local function ensure_preframe_snapshot(frame)
    if state_history[frame] ~= nil then return state_history[frame] end
    local snap = capture_world_snapshot()
    if snap then
        state_history[frame] = snap
    end
    return snap
end

local function request_rollback(frame, base_snapshot, reason)
    frame = tonumber(frame)
    if not frame then return end
    frame = qround(frame)
    if frame < 0 then frame = 0 end

    if rollback_target_frame == nil or frame < rollback_target_frame then
        rollback_target_frame = frame
        rollback_target_snapshot = base_snapshot
        rollback_reason = reason
    elseif rollback_target_frame == frame and base_snapshot ~= nil then
        rollback_target_snapshot = base_snapshot
        rollback_reason = reason
    end
end

local function maybe_send_ping(frame)
    if (frame % PING_EVERY) ~= 0 then return end
    ping_seq = ping_seq + 1
    ping_sent_at[ping_seq] = gtick()
    proto.send({ type = "ping", seq = ping_seq })
end

local function maybe_send_correction(frame)
    if not is_authority then return end
    if CORRECTION_INTERVAL <= 0 then return end
    if (frame % CORRECTION_INTERVAL) ~= 0 then return end
    if sent_correction_frames[frame] then return end

    local snap = state_history[frame] or ensure_preframe_snapshot(frame)
    if not snap then return end

    canonicalize_snapshot_in_place(snap)
    snap._kind = "correction"
    snap._history_frame = frame
    snap._hash = snapshot_hash(snap)

    if proto.send({
        type = "snapshot",
        seq  = frame,
        data = snap,
    }) then
        sent_correction_frames[frame] = true
        stat_corrections_sent = stat_corrections_sent + 1
    end
end

local function overwrite_prev_from_current(target, current)
    if not target or not current then return end

    if target.player and current.player then
        target.player.prev_x = current.player.x
        target.player.prev_y = current.player.y
    end
    if target.enemy and current.enemy then
        target.enemy.prev_x = current.enemy.x
        target.enemy.prev_y = current.enemy.y
    end

    if type(target.entities) == "table" and type(current.entities) == "table" then
        local by_slot = {}
        for i = 1, #current.entities do
            local e = current.entities[i]
            if e and e.slot ~= nil then by_slot[e.slot] = e end
        end
        for i = 1, #target.entities do
            local e = target.entities[i]
            local cur = e and by_slot[e.slot or -1] or nil
            if e and cur then
                e.prev_x = cur.x
                e.prev_y = cur.y
            end
        end
    end
end

local function apply_snapshot_direct(snap)
    if not snap or type(snap) ~= "table" then return false end
    if not (mod.game and mod.game.apply_snapshot) then return false end

    canonicalize_snapshot_in_place(snap)
    local current = capture_world_snapshot()
    overwrite_prev_from_current(snap, current)
    clear_inputs()
    set_resim_input_blocking()
    local ok = mod.game.apply_snapshot(snap)
    set_live_input_blocking()
    if ok then
        state_history[next_frame] = capture_world_snapshot()
        stat_emergency_applies = stat_emergency_applies + 1
    end
    return ok
end

local MAX_RESIM_FRAMES = config.get("max_resim_frames", 30)

local function rollback_and_resim(frame, base_snapshot, reason)
    if not (mod.game and mod.game.apply_snapshot and mod.game.simulate_ticks) then
        last_error = "Rollback APIs are unavailable."
        mod.warn("[sync] " .. last_error)
        return false
    end

    local floor = history_floor()
    if frame < floor then
        if base_snapshot and apply_snapshot_direct(base_snapshot) then
            stat_corrections_applied = stat_corrections_applied + 1
            vlog(string.format("emergency direct correction applied frame=%d reason=%s", frame, tostring(reason)))
            return true
        end
        last_error = string.format("Rollback window exceeded (frame=%d floor=%d).", frame, floor)
        mod.warn("[sync] " .. last_error)
        return false
    end

    local base = base_snapshot or state_history[frame]
    if not base then
        last_error = string.format("Missing rollback snapshot for frame %d.", frame)
        mod.warn("[sync] " .. last_error)
        return false
    end

    -- Cap the number of frames we resim per rollback to prevent CPU starvation
    -- at high ping.  When the gap exceeds the cap, skip ahead to only resim the
    -- most recent MAX_RESIM_FRAMES, accepting a visual pop.
    local resim_count = next_frame - frame
    if resim_count > MAX_RESIM_FRAMES then
        local new_frame = next_frame - MAX_RESIM_FRAMES
        vlog(string.format("resim cap: clamping rollback from frame %d to %d (gap %d > max %d)",
            frame, new_frame, resim_count, MAX_RESIM_FRAMES))
        -- Prefer the history snapshot closer to the present if available
        if state_history[new_frame] then
            base = state_history[new_frame]
        end
        frame = new_frame
    end

    canonicalize_snapshot_in_place(base)

    -- Capture a safety snapshot of the current live state BEFORE we touch
    -- anything.  If the resim fails mid-way (e.g. state transition during
    -- combat), we restore this so the game is never left in a half-resimmed
    -- corrupted state.
    local safety_snapshot = capture_world_snapshot()

    clear_inputs()
    set_resim_input_blocking()
    if not mod.game.apply_snapshot(base) then
        set_live_input_blocking()
        -- Restore safety snapshot so the game isn't left corrupted
        if safety_snapshot then mod.game.apply_snapshot(safety_snapshot) end
        last_error = string.format("Failed to restore snapshot for frame %d.", frame)
        mod.warn("[sync] " .. last_error)
        return false
    end

    state_history[frame] = base

    local resimulated = 0
    for f = frame, next_frame - 1 do
        local local_cmd = local_input_history[f] or 0
        local remote_cmd, _ = predict_remote_cmd(f)
        last_remote_cmd = remote_cmd

        if not schedule_resim_input_pair(local_cmd, remote_cmd) then
            set_live_input_blocking()
            if safety_snapshot then
                clear_inputs()
                mod.game.apply_snapshot(safety_snapshot)
            end
            last_error = "Failed to schedule rollback inputs."
            mod.warn("[sync] " .. last_error)
            return false
        end

        local ok, ran = mod.game.simulate_ticks(1)
        if not ok or ran ~= 1 then
            -- Resim failed (likely a state transition during combat).
            -- Restore the pre-rollback state so the game isn't corrupted.
            set_live_input_blocking()
            if safety_snapshot then
                clear_inputs()
                mod.game.apply_snapshot(safety_snapshot)
                vlog(string.format("resim failed at frame %d, restored safety snapshot", f))
            end
            -- This is recoverable — don't propagate as a fatal sync error.
            -- The next correction or input will trigger a fresh rollback.
            stat_rollbacks = stat_rollbacks + 1
            stat_resimulated_frames = stat_resimulated_frames + resimulated
            return true
        end

        resimulated = resimulated + 1
        state_history[f + 1] = capture_world_snapshot()
    end

    set_live_input_blocking()

    stat_rollbacks = stat_rollbacks + 1
    stat_resimulated_frames = stat_resimulated_frames + resimulated
    if reason == "authority_correction" then
        stat_corrections_applied = stat_corrections_applied + 1
    end

    vlog(string.format("rollback frame=%d -> %d resim=%d reason=%s", frame, next_frame, resimulated, tostring(reason)))
    return true
end

local function process_pending_rollback()
    if rollback_target_frame == nil then return true end

    local frame  = rollback_target_frame
    local base   = rollback_target_snapshot
    local reason = rollback_reason

    rollback_target_frame = nil
    rollback_target_snapshot = nil
    rollback_reason = nil

    return rollback_and_resim(frame, base, reason)
end

local function handle_correction_snapshot(snap)
    if not snap or type(snap) ~= "table" then return end
    if is_authority then return end

    canonicalize_snapshot_in_place(snap)

    local frame = qround(snap._history_frame or snap.seq or -1)
    if frame < 0 then return end
    if frame > next_frame then return end

    stat_corrections_recv = stat_corrections_recv + 1

    local local_snap = nil
    if frame == next_frame then
        local_snap = ensure_preframe_snapshot(frame)
    else
        local_snap = state_history[frame]
    end

    if local_snap then
        local local_hash = snapshot_hash(local_snap)
        local remote_hash = tonumber(snap._hash) or snapshot_hash(snap)
        if local_hash == remote_hash then
            return
        end
    end

    request_rollback(frame, snap, "authority_correction")
end

-- ── lifecycle ────────────────────────────────────────────────────────────
local function reset_state()
    active = false; live = false
    local_role = 0; set_control_slot(0)
    authority_role = 0; is_authority = false
    last_error = nil; waiting_reason = "idle"; match_seed = 0

    next_frame = 0
    last_sent_cmd = 0
    last_remote_cmd = 0
    last_remote_role = nil
    last_confirmed_remote = 0
    highest_remote_frame = -1
    highest_confirmed_frame = -1
    ticks_since_recv = 0

    state_history = {}
    local_input_history = {}
    remote_input_history = {}
    predicted_remote_history = {}
    sent_correction_frames = {}

    rollback_target_frame = nil
    rollback_target_snapshot = nil
    rollback_reason = nil

    ping_seq = 0; ping_sent_at = {}; ping_samples = {}; current_ping = 0

    stat_inputs_sent = 0
    stat_inputs_recv = 0
    stat_rollbacks = 0
    stat_prediction_misses = 0
    stat_corrections_sent = 0
    stat_corrections_recv = 0
    stat_corrections_applied = 0
    stat_emergency_applies = 0
    stat_resimulated_frames = 0
end

function sync_rollback.start(local_player_index, auth_role, seed)
    reset_state()

    local_role     = math.max(0, math.min(1, tonumber(local_player_index) or 0))
    set_control_slot(local_role)
    authority_role = tonumber(auth_role) or 0
    is_authority   = (local_role == authority_role)
    match_seed     = tonumber(seed) or 0

    if proto.get_state and proto.get_state() ~= "connected" then
        last_error = "Not connected."
        mod.warn("[sync] " .. last_error)
        return false
    end

    if not (mod.game and mod.game.set_input and mod.game.input_clear and mod.game.block_raw_input
        and mod.game.snapshot and mod.game.apply_snapshot and mod.game.simulate_ticks) then
        last_error = "Required rollback APIs are unavailable."
        mod.warn("[sync] " .. last_error)
        return false
    end

    clear_inputs()
    set_live_input_blocking()

    if match_seed ~= 0 and mod.game.set_rng_seed then
        mod.game.set_rng_seed(match_seed)
        vlog(string.format("set RNG seed to %u", match_seed))
    end

    state_history[0] = capture_world_snapshot()
    if not state_history[0] then
        last_error = "Failed to capture initial rollback snapshot."
        mod.warn("[sync] " .. last_error)
        clear_inputs()
        mod.game.block_raw_input(0, false)
        mod.game.block_raw_input(1, false)
        return false
    end

    active = true
    live   = true
    waiting_reason = "live"
    mod.log(string.format(
        "[sync] started  role=%d  slot=%d  remote_slot=%d  authority=%d  is_auth=%s  seed=%u  history=%d",
        local_role, local_slot_idx, remote_slot_idx, authority_role, tostring(is_authority), match_seed, HISTORY_FRAMES))
    return true
end

function sync_rollback.stop()
    clear_inputs()
    if mod and mod.game and mod.game.block_raw_input then
        mod.game.block_raw_input(0, false)
        mod.game.block_raw_input(1, false)
    end
    if active then
        mod.log(string.format(
            "[sync] stopped sent=%d recv=%d rb=%d miss=%d corr=%d/%d apply=%d ping=%.0fms err=%s",
            stat_inputs_sent,
            stat_inputs_recv,
            stat_rollbacks,
            stat_prediction_misses,
            stat_corrections_sent,
            stat_corrections_recv,
            stat_corrections_applied,
            current_ping,
            tostring(last_error)))
    end
    reset_state()
end

function sync_rollback.is_active() return active end
function sync_rollback.get_error()  return last_error end

-- ── per-tick ─────────────────────────────────────────────────────────────
function sync_rollback.apply_pending()
    -- Rollback/resimulation must happen before we schedule the current tick.
    if not process_pending_rollback() then
        return false
    end
    return true
end

function sync_rollback.tick()
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

    local frame = next_frame
    local preframe = ensure_preframe_snapshot(frame)
    if not preframe then
        last_error = string.format("Failed to capture pre-frame snapshot %d.", frame)
        mod.warn("[sync] " .. last_error)
        return "sync_error"
    end

    maybe_send_ping(frame)
    maybe_send_correction(frame)

    local local_cmd = read_local_cmd()
    local remote_cmd, _ = predict_remote_cmd(frame)

    local_input_history[frame] = local_cmd
    last_sent_cmd = local_cmd
    last_remote_cmd = remote_cmd

    if not schedule_live_remote_input(remote_cmd) then
        last_error = "Failed to inject live remote input."
        mod.warn("[sync] " .. last_error)
        return "sync_error"
    end

    if not proto.send({
        type  = "input",
        seq   = frame,
        frame = frame,
        cmd   = local_cmd,
    }) then
        last_error = "Failed to send input."
        mod.warn("[sync] " .. last_error)
        return "sync_error"
    end

    stat_inputs_sent = stat_inputs_sent + 1
    next_frame = frame + 1
    prune_history()
    return nil
end

-- ── per-frame ────────────────────────────────────────────────────────────
function sync_rollback.frame()
    if not active or not live then return nil end
    return nil
end

-- ── message handler ──────────────────────────────────────────────────────
function sync_rollback.on_message(msg)
    if not active or not msg then return nil end
    local t = msg.type

    if t == "remote_input" then
        ticks_since_recv = 0

        local frame = qround(msg.frame or msg.seq or 0)
        local cmd   = bit.band(tonumber(msg.cmd) or 0, ALL_CMD_MASK)
        local msg_role = tonumber(msg.role)
        local normalized_role, accept = normalize_remote_role(msg_role, frame, cmd)
        if not accept then
            return nil
        end

        remote_input_history[frame] = cmd
        last_remote_role = normalized_role
        last_confirmed_remote = cmd
        if frame > highest_confirmed_frame then highest_confirmed_frame = frame end
        if frame > highest_remote_frame then highest_remote_frame = frame end
        stat_inputs_recv = stat_inputs_recv + 1

        local predicted = predicted_remote_history[frame]
        if predicted ~= nil and predicted ~= cmd and frame < next_frame then
            stat_prediction_misses = stat_prediction_misses + 1
            request_rollback(frame, nil, "remote_input")
        else
            predicted_remote_history[frame] = cmd
        end

        last_remote_cmd = cmd
        return nil
    end

    if t == "remote_snapshot" then
        ticks_since_recv = 0
        handle_correction_snapshot(msg.data)
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
function sync_rollback.stats()
    return {
        role                 = local_role,
        authority_role       = authority_role,
        is_authority         = is_authority,
        local_source         = local_slot_idx,
        local_cmd            = last_sent_cmd,
        remote_cmd           = last_remote_cmd,
        remote_role          = last_remote_role,
        ping_ms              = current_ping,
        sync_started         = live,
        waiting_for_first_snapshot = false,
        waiting_reason       = waiting_reason,
        ticks_no_recv        = ticks_since_recv,
        inputs_sent          = stat_inputs_sent,
        inputs_recv          = stat_inputs_recv,
        snapshots_sent       = stat_corrections_sent,
        snapshots_recv       = stat_corrections_recv,
        snapshots_applied    = stat_corrections_applied,
        restores             = stat_rollbacks,
        latest_snapshot_seq  = highest_remote_frame,
        applied_snapshot_seq = highest_confirmed_frame,
        current_state_seq    = next_frame,
        resend_every         = CORRECTION_INTERVAL,
        error                = last_error,
        input_delay          = 0,
        native_tick          = (mod.game and mod.game.native_tick and mod.game.native_tick()) or gtick(),
        rng_seed             = match_seed,
        hashes_sent          = 0,
        rollbacks            = stat_rollbacks,
        prediction_misses    = stat_prediction_misses,
        corrections_sent     = stat_corrections_sent,
        corrections_recv     = stat_corrections_recv,
        corrections_applied  = stat_corrections_applied,
        emergency_applies    = stat_emergency_applies,
        resimulated_frames   = stat_resimulated_frames,
        high_ping_mode       = current_ping >= 120,
    }
end

return sync_rollback
