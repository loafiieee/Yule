-- mods/online/main.lua  –  Eggnogg+ Online Multiplayer mod entry point.

local SERVER_HOST = config.get("server_host", "127.0.0.1")
local SERVER_PORT = config.get("server_port", 7878)

local START_ACTION  = 0x432440
local GAP           = 10.0

-- ── ONLINE button baseline (vs_ai pattern) ───────────────────────────────────

local baseline_captured     = false
local baseline_from_initial = false
local base_x, base_y, base_w, base_h = nil, nil, nil, nil
local last_start_ptr = nil
local was_in_main = false

local function in_main(st)
    return st == "main" or st == "main_initial"
end

local function capture_baseline(st)
    local ptr = mod.ui.find_button_by_action_ptr(START_ACTION)
    if not ptr then return nil end

    last_start_ptr = ptr
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
    local codes = { 1, 3, 0, 2, 4 }
    for _, code in ipairs(codes) do
        local ok, err = fn(ptr, code)
        if ok == nil then
            mod.warn("start dispatch failed (code=" .. tostring(code) .. "): " .. tostring(err))
        end
        if mod.ui.state_name() ~= before then
            return true
        end
    end

    return false
end

-- ── Sub-modules ──────────────────────────────────────────────────────────────

mod.dofile("lib/json.lua")
mod.dofile("lib/proto.lua")
mod.dofile("lib/sync.lua")
mod.dofile("lib/hub.lua")

-- ── Lifecycle ────────────────────────────────────────────────────────────────

mod.on_load(function()
    if hub.ensure_state then
        hub.ensure_state()
    end
    mod.log("Eggnogg+ Online loaded  server=" ..
            SERVER_HOST .. ":" .. tostring(SERVER_PORT))
end)

mod.on_unload(function()
    sync.stop()
    proto.disconnect()
    mod.log("Eggnogg+ Online unloaded")
end)

-- ── Per-frame ────────────────────────────────────────────────────────────────

local last_tick       = 0
local prev_state_name = ""
local match_started   = false
local pending_start_match = false
local pending_start_cooldown = 0

local function reopen_hub_with_win_message()
    sync.stop()
    if hub.set_post_open_message then
        hub.set_post_open_message("Opponent left the game. You win.")
    end
    hub.open()
end

mod.on_frame(function()
    local tc    = mod.game.tick_count()
    local dt    = math.min((tc - last_tick) / 60.0, 0.1)
    last_tick   = tc

    local sname = mod.ui.state_name()

    if hub.consume_start_request and hub.consume_start_request() then
        pending_start_match = true
        pending_start_cooldown = 0
    end

    if pending_start_cooldown > 0 then
        pending_start_cooldown = pending_start_cooldown - 1
    end

    -- ── ONLINE button on main menu ──────────────────────────────────────────
    if in_main(sname) then
        if not was_in_main then
            baseline_captured     = false
            baseline_from_initial = false
            base_x, base_y, base_w, base_h = nil, nil, nil, nil
        end

        local start_ptr = capture_baseline(sname)

        if pending_start_match and start_ptr and pending_start_cooldown <= 0 then
            sync.apply_map()
            if dispatch_start(start_ptr) then
                pending_start_cooldown = 6
                mod.log("[main] pending match start dispatched from " .. sname)
                return
            end
        end

        local clicked = mod.ui.native_button("online_open_btn", "ONLINE",
                                             1.0, 4.5, 3.0, 6.0)
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

    if pending_start_match and sname == "pregame" and pending_start_cooldown <= 0 then
        sync.apply_map()
        local pre_ptr = mod.ui.find_button_by_action_ptr(START_ACTION)
        if pre_ptr and dispatch_start(pre_ptr) then
            pending_start_cooldown = 6
            mod.log("[main] pending match start dispatched from pregame")
            return
        end
    end

    -- ── Hub draw (only while in the online_hub game state) ─────────────────
    if sname == "online_hub" then
        hub.draw(dt)
    end

    -- ── In-game: drain messages + draw HUD ──────────────────────────────────
    if sync.is_active() then
        proto.update()
        local msg = proto.poll()
        while msg do
            if msg.type == "match_end" then
                reopen_hub_with_win_message()
            elseif msg.type == "remote_input" then
                sync.on_remote_input(
                    tonumber(msg.tick) or 0,
                    tonumber(msg.cmd)  or 0)
            elseif msg.type == "snapshot" then
                sync.on_snapshot(tonumber(msg.tick) or 0, msg.snap)
            end
            msg = proto.poll()
        end

        if proto.get_state() == "disconnected" then
            reopen_hub_with_win_message()
            return
        end

        sync.frame()
        sync.draw_hud()
    end

    -- ── State transition detection ───────────────────────────────────────────
    if sname ~= prev_state_name then
        if sname == "game" and not match_started
                           and hub.get_state() == "in_game" then
            match_started = true
            pending_start_match = false
            sync.start()
            mod.log("[main] game started – sync on")
        end
        if sname == "main" and match_started then
            match_started = false
            if sync.is_active() then
                proto.send({ type = "match_end" })
                sync.stop()
            end
            hub.open()
        end
        prev_state_name = sname
    end
end)

-- ── Per-tick ─────────────────────────────────────────────────────────────────

mod.on_tick(function()
    if sync.is_active() then
        sync.tick()
    end
end)

-- ── Events ───────────────────────────────────────────────────────────────────

mod.on_event(function(e)
    if mod.ui.state_name() == "online_hub" then
        return hub.on_event(e)
    end
    return false
end)
