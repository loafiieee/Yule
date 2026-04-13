-- mods/online/main.lua - Eggnogg+ Online
--
-- Minimal online relay:
--   - both peers launch the same match,
--   - each peer samples/sends local movement from the frame side,
--   - each peer applies the latest local+remote movement on the gameplay tick.

-- Server host/port are now dynamic — resolved at connect time via servers.get_selected().
-- config.server_host / server_port remain as a last-resort fallback for servers.lua.

-- Address of the game's "Start" button (used to launch matches from the hub)
local START_ACTION = 0x432440
local GAP          = 10.0

-- ── baseline tracking (ONLINE button placement) ──────────────────────────
local baseline_captured     = false
local baseline_from_initial = false
local base_x, base_y, base_w, base_h = nil, nil, nil, nil
local was_in_main = false

local function in_main(st)
    return st == "main" or st == "main_initial"
end

local function capture_baseline(st)
    local ptr = mod.ui.find_button_by_action_ptr(START_ACTION)
    if not ptr then return nil end
    local want = (not baseline_captured) or
                 (baseline_from_initial and st == "main")
    if want then
        local x, y, w, h = mod.ui.button_rect_ptr(ptr)
        if x and y and w and h and w > 20.0 and h > 10.0 then
            base_x, base_y, base_w, base_h = x, y, w, h
            baseline_captured     = true
            baseline_from_initial = (st == "main_initial")
            if st == "main" then baseline_from_initial = false end
        end
    end
    return ptr
end

local function dispatch_start(ptr)
    if not ptr then return false end
    local fn = mod.ui.button_invoke_ptr or mod.ui.button_activate_ptr
    if not fn then return false end
    local before = mod.ui.state_name()
    for _, code in ipairs({ 1, 3, 0, 2, 4 }) do
        fn(ptr, code)
        if mod.ui.state_name() ~= before then return true end
    end
    return false
end

-- ── load sub-modules ──────────────────────────────────────────────────────
mod.dofile("lib/json.lua")
mod.dofile("lib/proto.lua")
mod.dofile("lib/map_manifest.lua")
mod.dofile("lib/servers.lua")
mod.dofile("lib/sync.lua")
mod.dofile("lib/hub.lua")

-- ── match state ───────────────────────────────────────────────────────────
local match_active           = false
local pending_start_match    = false
local pending_start_cooldown = 0
local pending_reopen_hub     = false
local pending_reopen_message = nil
local map_selector           = nil
local match_seed             = nil
local match_start_tick       = 0
local match_input_delay      = 1
local pending_sync_start_retries = 0

-- HUD toggle (F3)
local hud_visible = false
local SDLK_F2     = 1073741883
local SDLK_F3     = 1073741884
local SDLK_F5     = 1073741886

local function prime_match_start()
    return true
end

local function ensure_match_map_selected()
    if map_selector == nil then
        return true
    end
    if not (mod.game and mod.game.set_map_selector) then
        return true
    end

    mod.game.set_map_selector(map_selector)

    if mod.game.get_map_selector then
        local want = tonumber(map_selector)
        local got = tonumber(mod.game.get_map_selector())
        if want ~= nil and got ~= nil and got ~= want then
            return false
        end
    end

    return true
end

-- ── end_match ─────────────────────────────────────────────────────────────
local function end_match(reason)
    local current_state = mod.ui.state_name()
    if match_active then
        proto.send({ type = "match_end" })
    end
    sync.stop()

    match_active           = false
    pending_start_match    = false
    pending_start_cooldown = 0
    map_selector           = nil
    match_seed             = nil
    match_start_tick       = 0
    match_input_delay      = 1
    pending_sync_start_retries = 0

    -- Always reopen the hub after a match. When reason is nil it's a clean game-over
    -- (state already transitioned to main); when it's non-nil we need to force the
    -- transition first.
    pending_reopen_hub     = true
    pending_reopen_message = reason or "Match over."
    if reason and current_state == "game" and mod.ui.goto_main_menu then
        mod.ui.goto_main_menu()
    end
    mod.log("[main] match ended: " .. tostring(reason or "clean"))
end

-- ── lifecycle ─────────────────────────────────────────────────────────────
mod.on_load(function()
    if hub.ensure_state then hub.ensure_state() end
    mod.log("Eggnogg+ Online loaded")
end)

mod.on_unload(function()
    if match_active then end_match(nil) end
    proto.disconnect()
    mod.log("Eggnogg+ Online unloaded")
end)

-- ── on_tick: gameplay sync ────────────────────────────────────────────────
-- Input/network handling runs before the engine advances the gameplay tick.
mod.on_tick(function()
    local sname = mod.ui.state_name()

    if sname == "game" and not match_active and pending_start_match and hub.get_state() == "in_game" then
        local role = hub.get_role()
        local ok, started = pcall(sync.start, role, hub.get_authority_role(), hub.get_match_seed())
        if ok and started then
            pending_start_match = false
            pending_sync_start_retries = 0
            match_active = true
            mod.log(string.format("[main] online match started  mode=%s  role=%d  map=%s",
                tostring(sync.mode and sync.mode() or "unknown"), role, tostring(hub.get_map_key())))
        else
            local err = ok and (sync.get_error and sync.get_error()) or tostring(started)
            err = tostring(err or "Failed to initialize online sync.")
            local transient = err:find("snapshot", 1, true) ~= nil or err:find("pre-frame") ~= nil
            if transient and pending_sync_start_retries > 0 then
                pending_sync_start_retries = pending_sync_start_retries - 1
                mod.log(string.format("[main] delaying online sync init; retries_left=%d reason=%s",
                    pending_sync_start_retries, err))
                return
            end
            pending_start_match = false
            end_match(err)
            return
        end
    end

    if not match_active or not sync.is_active() then return end

    -- Pump match traffic on the gameplay tick so queued control messages and
    -- remote input are visible before we schedule local overrides.
    proto.update()
    local msg = proto.poll()
    while msg do
        local status = sync.on_message(msg)
        if status == "match_end" then
            end_match("Opponent disconnected.")
            return
        elseif status == "sync_error" then
            end_match(sync.get_error() or "Match sync failed.")
            return
        end
        msg = proto.poll()
    end

    if proto.get_state() == "disconnected" then
        end_match("Server disconnected.")
        return
    end

    if sync.apply_pending() == false then
        end_match(sync.get_error() or "Match sync failed.")
        return
    end

    local status = sync.tick()
    if status == "timeout" then
        end_match("Connection timed out.")
    elseif status == "sync_error" then
        end_match(sync.get_error() or "Match sync failed.")
    end
end)

-- ── on_frame: network I/O + UI ────────────────────────────────────────────
local last_tick  = 0
local prev_state = ""

mod.on_frame(function()
    local tc    = mod.game.tick_count()
    local dt    = math.min((tc - last_tick) / 60.0, 0.1)
    last_tick   = tc
    local sname = mod.ui.state_name()

    -- Consume match-start requests queued by the hub UI
    if hub.consume_start_request and hub.consume_start_request() then
        pending_start_match    = true
        pending_start_cooldown = 0
        pending_sync_start_retries = 20
        map_selector           = hub.get_map_sel()
        match_seed             = hub.get_match_seed and hub.get_match_seed() or match_seed
        match_start_tick       = hub.get_start_tick and hub.get_start_tick() or 0
        match_input_delay      = hub.get_input_delay and hub.get_input_delay() or 1
    end
    if pending_start_cooldown > 0 then
        pending_start_cooldown = pending_start_cooldown - 1
    end

    -- Main menu: show ONLINE button + handle match launch
    if in_main(sname) then
        if not was_in_main then
            baseline_captured     = false
            baseline_from_initial = false
            base_x, base_y, base_w, base_h = nil, nil, nil, nil
        end

        local start_ptr = capture_baseline(sname)

        if pending_start_match and start_ptr and pending_start_cooldown <= 0 then
            if ensure_match_map_selected() and prime_match_start() then
                -- Pre-seed RNG before the game state starts so game_init()'s mrand() calls
                -- (_game_level map selection, __seed) produce the same values on both clients.
                local seed = match_seed or (hub.get_match_seed and hub.get_match_seed())
                if seed and seed ~= 0 and mod.game.set_rng_seed then
                    mod.game.set_rng_seed(seed)
                end
                if dispatch_start(start_ptr) then
                    pending_start_cooldown = 6
                    return
                end
            end
        end

        local clicked = mod.ui.native_button("online_open_btn", "ONLINE", 1.0, 4.5, 3.0, 6.0)
        if base_x and base_y and base_w and base_h then
            mod.ui.native_resize("online_open_btn", base_w, base_h)
            mod.ui.native_set_pos("online_open_btn", base_x, base_y + base_h + GAP)
        end
        if clicked then
            hub.open()
        end

    else
        if was_in_main then
            baseline_captured     = false
            baseline_from_initial = false
            base_x, base_y, base_w, base_h = nil, nil, nil, nil
        end
    end
    was_in_main = in_main(sname)

    -- Pregame: apply map selector + dispatch start
    if pending_start_match and sname == "pregame" and pending_start_cooldown <= 0 then
        local pre_ptr = mod.ui.find_button_by_action_ptr(START_ACTION)
        if pre_ptr and ensure_match_map_selected() and prime_match_start() then
            -- Pre-seed again here (pregame → game transition is when game_init() runs).
            local seed = match_seed or (hub.get_match_seed and hub.get_match_seed())
            if seed and seed ~= 0 and mod.game.set_rng_seed then
                mod.game.set_rng_seed(seed)
            end
            if dispatch_start(pre_ptr) then
                pending_start_cooldown = 6
                return
            end
        end
    end

    -- Hub UI draw
    if sname == "online_hub" then
        hub.draw(dt)
    end

    -- In-match: sample/send local movement from the frame side, then draw HUD.
    if match_active then
        local status = sync.frame()
        if status == "sync_error" then
            end_match(sync.get_error() or "Match sync failed.")
            return
        end
        if sname == "game" then
            mod.ui.begin_overlay()
            draw_nametags()
            if hud_visible then
                draw_netgraph()
            end
            mod.ui.end_overlay()
        elseif hud_visible then
            draw_netgraph()
        end
    end

    -- State-change transitions
    if sname ~= prev_state then

        -- Leaving game state while match is active: clear any queued tick input overrides.
        -- hooks_finish_game_tick (which decrements the tick override countdown) only runs
        -- during game state, so a stale remote-input override on the remote player slot
        -- persists into the pause menu and causes main_player_poll_cmds to fire a button
        -- click, which opens the mods menu immediately via mods_entry_player_filter_proxy.
        if prev_state == "game" and match_active and mod.game and mod.game.input_clear then
            mod.game.input_clear(0)
            mod.game.input_clear(1)
        end

        -- Returned to main menu from an active match
        if sname == "main" and match_active then
            end_match(nil)
        end

        -- Reopen hub after opponent disconnect
        if sname == "main" and pending_reopen_hub then
            pending_reopen_hub = false
            if hub.set_post_open_message then
                hub.set_post_open_message(pending_reopen_message or "Match ended.")
            end
            pending_reopen_message = nil
            hub.open()
        end

        prev_state = sname
    end
end)

-- ── on_event: keyboard ────────────────────────────────────────────────────
mod.on_event(function(e)
    if mod.ui.state_name() == "online_hub" then
        return hub.on_event(e)
    end

    if not (e and (e.type == "keydown" or e.type == "keyup")) then
        return false
    end

    local sym = tonumber(e.sym)

    -- Block F2/F5 (local restart) during a match — would break sync.
    if match_active and (sym == SDLK_F2 or sym == SDLK_F5) then
        if e.type == "keydown" then
            mod.warn(string.format("[main] key blocked during online match sym=%s", tostring(sym)))
        end
        return true
    end

    -- F3 toggles the netgraph HUD
    if sym == SDLK_F3 and e.type == "keydown" then
        hud_visible = not hud_visible
        return true
    end

    return false
end)

-- ── player name tags ──────────────────────────────────────────────────────
function draw_nametags()
    if not (mod.game and mod.game.snapshot and mod.game.camera) then return end

    local cam = mod.game.camera()
    if not cam then return end

    local W, H = mod.ui.screen_size()
    W = tonumber(W) or 640
    H = tonumber(H) or 480

    local role = hub.get_role and hub.get_role() or 0
    role = math.max(0, math.min(1, tonumber(role) or 0))

    local local_snap = mod.game.snapshot(role, false)
    if not local_snap or not local_snap.player then return end

    local function num(v)
        v = tonumber(v)
        if not v or v ~= v or v == math.huge or v == -math.huge then return nil end
        return v
    end

    local cx = num(cam.x) or 0
    local cy = num(cam.y) or 0
    local view_w = num(cam.w) or W
    local view_h = num(cam.h) or H
    if view_w <= 0 then view_w = W end
    if view_h <= 0 then view_h = H end
    local scale_x = W / view_w
    local scale_y = H / view_h

    local local_x = num(local_snap.player.x)
    local local_y = num(local_snap.player.y)

    -- Scale UI elements relative to a 640x480 reference resolution
    local ui_scale = H / 480

    local TAG_SCALE = 0.65 * ui_scale
    local LOCAL_TAG_OFFSET_Y = 50 * ui_scale

    local function world_to_screen(world_x, world_y)
        if not world_x or not world_y then return nil, nil end
        local sx = (world_x - cx + view_w * 0.5) * scale_x
        local sy = (world_y - cy + view_h * 0.5) * scale_y
        if sx ~= sx or sy ~= sy then return nil, nil end
        return sx, sy
    end

    local function draw_tag_at_screen(sx, sy, text, offset_y, r, g, b)
        if not sx or not sy or not text or text == "" then return end
        local tw = #text * 9 * TAG_SCALE
        local shadow_off = math.max(1, math.floor(ui_scale + 0.5))
        sy = sy - offset_y
        if sx < -tw or sx > W + tw or sy < -20 or sy > H + 20 then
            return
        end
        mod.ui.text_at(text, sx - tw * 0.5 + shadow_off, sy + shadow_off, TAG_SCALE, 0.0, 0.0, 0.0)
        mod.ui.text_at(text, sx - tw * 0.5, sy, TAG_SCALE, r, g, b)
    end

    local sx, sy = world_to_screen(local_x, local_y)
    if sx and sy then
        draw_tag_at_screen(sx, sy, "V", LOCAL_TAG_OFFSET_Y, 0.3, 1.0, 0.3)
    end
end

-- ── netgraph HUD ──────────────────────────────────────────────────────────
function draw_netgraph()
    local W, H = mod.ui.screen_size()
    W = W or 640
    H = H or 480
    local st = sync.stats()

    -- Scale all positions and text sizes relative to 640x480 reference
    local s = H / 480
    local lx = 12 * s   -- left margin
    local rx = W - 8 * s -- right margin

    local role_str = "P" .. tostring((tonumber(st.role) or 0) + 1)
    local mode_str = st.is_authority and "AUTH" or "SYNC"
    local hi_str   = st.high_ping_mode and "  HI-PING" or ""
    mod.ui.text_at("ONLINE  " .. role_str .. "  " .. mode_str .. hi_str, lx, 8 * s, 0.9 * s,
        st.high_ping_mode and 1.0 or 0.2,
        st.high_ping_mode and 0.6 or 1.0,
        0.4)

    local ping_pr = math.floor(st.ping_ms + 0.5)
    local ping_r  = ping_pr >= 100 and 1.0 or (ping_pr >= 60 and 0.85 or 0.55)
    local ping_g  = ping_pr >= 100 and 0.55 or (ping_pr >= 60 and 0.80 or 0.90)
    local ping_str = string.format("ping %3d ms", ping_pr)
    mod.ui.text_at(ping_str, lx, 24 * s, 0.7 * s, ping_r, ping_g, 0.40)

    local source_str = (st.local_source == nil) and "src ?" or ("src p" .. tostring(st.local_source + 1))
    local detail = string.format(
        "%-6s  rem %-3d  frm %-5d  tick %-5d  snp %-2d",
        source_str,
        tonumber(st.remote_cmd) or 0,
        tonumber(st.current_state_seq) or 0,
        tonumber(st.native_tick) or 0,
        tonumber(st.resend_every) or 0)
    mod.ui.text_at(detail, lx, 38 * s, 0.65 * s, 0.9, 0.9, 0.9)

    local line2 = string.format(
        "in %-4d  rin %-4d  rb %-3d miss %-3d  seed %-10u",
        tonumber(st.inputs_sent) or 0,
        tonumber(st.inputs_recv) or 0,
        tonumber(st.rollbacks) or tonumber(st.restores) or 0,
        tonumber(st.prediction_misses) or 0,
        tonumber(st.rng_seed) or 0)
    mod.ui.text_at(line2, lx, 52 * s, 0.6 * s, 0.8, 0.8, 0.8)

    local line3 = string.format(
        "corr s=%-3d r=%-3d a=%-3d  rsim %-4d",
        tonumber(st.corrections_sent) or tonumber(st.snapshots_sent) or 0,
        tonumber(st.corrections_recv) or tonumber(st.snapshots_recv) or 0,
        tonumber(st.corrections_applied) or tonumber(st.snapshots_applied) or 0,
        tonumber(st.resimulated_frames) or 0)
    mod.ui.text_at(line3, lx, 66 * s, 0.58 * s, 0.75, 0.75, 0.75)

    mod.ui.text_at(string.format("wait %-18s", tostring(st.waiting_reason or "?")), rx, 8 * s, 0.65 * s, 1.0, 1.0, 0.0)
    if st.error then
        mod.ui.text_at(string.format("err %s", tostring(st.error)), rx, 22 * s, 0.6 * s, 1.0, 0.4, 0.4)
    end
    mod.ui.text_at("F3 hud", lx, H - 12 * s, 0.5 * s, 0.5, 0.5, 0.5)
end
