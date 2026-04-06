-- mods/online/main.lua - Eggnogg+ Online
--
-- Minimal online relay:
--   - both peers launch the same match,
--   - each peer samples/sends local movement from the frame side,
--   - each peer applies the latest local+remote movement on the gameplay tick.

local SERVER_HOST = config.get("server_host", "127.0.0.1")
local SERVER_PORT = config.get("server_port", 7878)

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

-- HUD toggle (F3)
local hud_visible = true
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

    if reason then
        pending_reopen_hub     = true
        pending_reopen_message = reason
        if current_state == "game" and mod.ui.goto_main_menu then
            mod.ui.goto_main_menu()
        end
    end
    mod.log("[main] match ended: " .. tostring(reason or "clean"))
end

-- ── lifecycle ─────────────────────────────────────────────────────────────
mod.on_load(function()
    if hub.ensure_state then hub.ensure_state() end
    mod.log(string.format("Eggnogg+ Online loaded  server=%s:%d", SERVER_HOST, SERVER_PORT))
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
        pending_start_match = false
        if sync.start(role, hub.get_authority_role(), hub.get_match_seed()) then
            match_active = true
            mod.log(string.format("[main] relay match started  role=%d  map=%s",
                role, tostring(hub.get_map_key())))
        else
            end_match("Failed to initialize online sync.")
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

    sync.apply_pending()
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
            if ensure_match_map_selected() and prime_match_start() and dispatch_start(start_ptr) then
                pending_start_cooldown = 6
                return
            end
        end

        local clicked = mod.ui.native_button("online_open_btn", "ONLINE", 1.0, 4.5, 3.0, 6.0)
        if base_x and base_y and base_w and base_h then
            mod.ui.native_resize("online_open_btn", base_w, base_h)
            mod.ui.native_set_pos("online_open_btn", base_x, base_y + base_h + GAP)
        end
        if clicked then
            hub.open(SERVER_HOST, SERVER_PORT)
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
        if pre_ptr and ensure_match_map_selected() and prime_match_start() and dispatch_start(pre_ptr) then
            pending_start_cooldown = 6
            return
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
        if hud_visible then
            draw_netgraph()
        end
    end

    -- State-change transitions
    if sname ~= prev_state then

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

    -- Block F5 (local restart) during a match — would break sync
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

-- ── netgraph HUD ──────────────────────────────────────────────────────────
function draw_netgraph()
    local W, H = mod.ui.screen_size()
    W = W or 640
    H = H or 480
    local st = sync.stats()

    local role_str = "P" .. tostring((tonumber(st.role) or 0) + 1)
    local mode_str = st.is_authority and "AUTH" or "SYNC"
    mod.ui.text_at("ONLINE  " .. role_str .. "  " .. mode_str, 12, 8, 0.9, 0.2, 1.0, 0.4)

    local ping_str = string.format("ping %3d ms", math.floor(st.ping_ms + 0.5))
    mod.ui.text_at(ping_str, 12, 24, 0.7, 0.8, 0.9, 1.0)

    local source_str = (st.local_source == nil) and "src ?" or ("src p" .. tostring(st.local_source + 1))
    local detail = string.format(
        "%-6s  rem %-3d  frm %-5d  tick %-5d  dly %-2d",
        source_str,
        tonumber(st.remote_cmd) or 0,
        tonumber(st.current_state_seq) or 0,
        tonumber(st.native_tick) or 0,
        tonumber(st.resend_every) or 0)
    mod.ui.text_at(detail, 12, 38, 0.65, 0.9, 0.9, 0.9)

    local line2 = string.format(
        "in %-4d  rin %-4d  snp out=%-4d in=%-4d  seed %-10u",
        tonumber(st.inputs_sent) or 0,
        tonumber(st.inputs_recv) or 0,
        tonumber(st.snapshots_sent) or 0,
        tonumber(st.snapshots_applied) or 0,
        tonumber(st.rng_seed) or 0)
    mod.ui.text_at(line2, 12, 52, 0.6, 0.8, 0.8, 0.8)

    mod.ui.text_at(string.format("wait %-18s", tostring(st.waiting_reason or "?")), W - 8, 8, 0.65, 1.0, 1.0, 0.0)
    if st.error then
        mod.ui.text_at(string.format("err %s", tostring(st.error)), W - 8, 22, 0.6, 1.0, 0.4, 0.4)
    end
    mod.ui.text_at("F3 hud", 12, H - 12, 0.5, 0.5, 0.5, 0.5)
end
