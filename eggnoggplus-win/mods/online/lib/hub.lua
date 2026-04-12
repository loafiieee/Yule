-- lib/hub.lua - online hub custom state UI.

hub = hub or {}

local STATE_NAME = "online_hub"

local S_CONNECTING     = "connecting"
local S_SERVER_SELECT  = "server_select"
local S_LOGIN          = "login"
local S_LOGGING_IN     = "logging_in"
local S_HUB            = "hub"
local S_QUEUING        = "queuing"
local S_MATCH_FOUND    = "match_found"
local S_IN_GAME        = "in_game"

local state = S_LOGIN
local username = ""
local status_msg = ""
local queue_count = 0
local cd_timer = 0
local my_role = 0
local my_authority_role = 0
local my_map_sel = 0
local my_map_label = ""
local my_map_key = ""
local my_opponent = ""
local my_match_seed = 0
local my_start_tick = 0
local my_input_delay = 1
local match_ready_sent = false
local focus = 1
local field_user = ""
local field_pass = ""
local remember_me = false
local state_ready = false
local pending_start = false
local post_open_message = nil

local click_pending = false
local click_x = 0
local click_y = 0

-- Which state to return to when ESC is pressed from S_SERVER_SELECT
local server_select_back = S_LOGIN

-- Pending auth: set when LOG IN/REGISTER is clicked while still connecting.
-- Sent automatically once the TCP connection completes.
local pending_auth = nil

-- ── per-server credential helpers ─────────────────────────────────────────
-- Credentials are stored per server host so switching servers loads the right
-- account automatically. Key is sanitized host (dots → underscores).
local function sv_storage_key(sv)
    if not sv then return nil end
    return sv.host:gsub("[^%w]", "_")
end

local function load_server_creds()
    local sv  = servers and servers.get_selected and servers.get_selected()
    local key = sv_storage_key(sv)
    if not key then return end
    local u = storage.get("cu_" .. key)
    local p = storage.get("cp_" .. key)
    if type(u) == "string" and u ~= "" then
        field_user  = u
        field_pass  = type(p) == "string" and p or ""
        remember_me = true
    else
        field_user  = ""
        field_pass  = ""
        remember_me = false
    end
end

local function save_server_creds(sv, user, pass)
    local key = sv_storage_key(sv)
    if not key then return end
    if remember_me then
        storage.set("cu_" .. key, user)
        storage.set("cp_" .. key, pass)
    else
        storage.set("cu_" .. key, nil)
        storage.set("cp_" .. key, nil)
    end
end

local function ui_create_state(name)
    local f = mod.ui.create_state or mod.ui.register_state
    if f then return f(name) end
    return true
end

local function ui_enter_state(name)
    if mod.ui.enter_state then return mod.ui.enter_state(name) end
    if name == STATE_NAME and mod.ui.enter_online_hub then
        mod.ui.enter_online_hub()
        return true
    end
    return false
end

local function ui_leave_state()
    if mod.ui.leave_state then return mod.ui.leave_state() end
    if mod.ui.leave_online_hub then
        mod.ui.leave_online_hub()
        return true
    end
    return false
end

local function ui_scale()
    local W, H = mod.ui.screen_size()
    local s = math.min(W / 1280.0, H / 720.0)
    if s < 1.08 then s = 1.08 end
    if s > 1.55 then s = 1.55 end
    return s
end

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function approx_text_w(text, scale)
    return #tostring(text or "") * 9 * (scale or 1.0)
end

local function draw_text(text, x, y, scale, r, g, b)
    mod.ui.text_at(text, x + 2, y + 2, scale, 0.0, 0.0, 0.0)
    mod.ui.text_at(text, x, y, scale, r, g, b)
end

local function draw_text_center(text, cx, y, scale, r, g, b)
    draw_text(text, cx - approx_text_w(text, scale) * 0.5, y, scale, r, g, b)
end

local function fill_rect(x, y, w, h, r, g, b, a)
    if mod.ui.fill_rect then mod.ui.fill_rect(x, y, w, h, r, g, b, a) end
end

local function stroke_rect(x, y, w, h, line_w, r, g, b, a)
    if mod.ui.stroke_rect then mod.ui.stroke_rect(x, y, w, h, line_w, r, g, b, a) end
end

local function mouse_pos()
    if mod.ui.mouse_pos then return mod.ui.mouse_pos() end
    return 0, 0
end

local function mouse_in_rect(x, y, w, h)
    local mx, my = mouse_pos()
    return mx >= x and my >= y and mx <= (x + w) and my <= (y + h)
end

local function consume_click(x, y, w, h)
    if click_pending and click_x >= x and click_y >= y and click_x <= (x + w) and click_y <= (y + h) then
        click_pending = false
        return true
    end
    return false
end

local function set_state(s)
    state = s
    if s ~= S_MATCH_FOUND then
        cd_timer = 0
        match_ready_sent = false
    end
end

local function status(msg)
    status_msg = msg or ""
    if msg and msg ~= "" then mod.log("[hub] " .. msg) end
end

function hub.ensure_state()
    if not state_ready then
        state_ready = ui_create_state(STATE_NAME) and true or false
    end
    return state_ready
end

function hub.set_post_open_message(msg)
    post_open_message = msg
end

local function begin_login_request(kind)
    if field_user == "" or field_pass == "" then
        status("Enter a username and password")
        return
    end
    local sv = servers and servers.get_selected and servers.get_selected()
    if not sv then
        server_select_back = S_LOGIN
        set_state(S_SERVER_SELECT)
        return
    end
    username = field_user
    save_server_creds(sv, field_user, field_pass)

    local ps = proto.get_state()
    if ps == "connected" then
        proto.send({ type = kind, username = field_user, password = field_pass })
        set_state(S_LOGGING_IN)
        status(kind == "register" and "Registering..." or "Logging in...")
    elseif ps == "disconnected" then
        pending_auth = { kind = kind, user = field_user, pass = field_pass }
        set_state(S_CONNECTING)
        status("Connecting to " .. sv.name .. "...")
        proto.connect(sv.host, sv.port)
        servers.mark_connected()
    elseif ps == "connecting" then
        -- Already connecting (e.g. user clicked twice); just update the pending auth.
        pending_auth = { kind = kind, user = field_user, pass = field_pass }
    end
end

local function start_match()
    pending_start = true
    set_state(S_IN_GAME)
    status("Starting match...")
    hub.close()
end

local function handle_msg(msg)
    local t = msg and msg.type or nil
    if t == "auth_ok" then
        username = msg.username or username
        map_manifest.invalidate()  -- rebuild manifest fresh in case maps changed since last session
        map_manifest.send()
        set_state(S_HUB)
        status("")
    elseif t == "auth_fail" then
        set_state(S_LOGIN)
        status("Login failed: " .. tostring(msg.reason or "?"))
    elseif t == "queue_update" then
        queue_count = tonumber(msg.count) or queue_count
    elseif t == "match_found" then
        my_role = tonumber(msg.role) or 0
        my_authority_role = tonumber(msg.authority_role) or 0
        my_opponent = tostring(msg.opponent or "")
        my_map_key = tostring(msg.map_key or "")
        do
            local resolved_sel = nil
            if my_map_key ~= "" and map_manifest and map_manifest.selector_for_key then
                resolved_sel = map_manifest.selector_for_key(my_map_key)
            end
            my_map_sel = (resolved_sel ~= nil) and resolved_sel or (tonumber(msg.map_sel) or 0)
            if resolved_sel ~= nil and resolved_sel ~= (tonumber(msg.map_sel) or 0) then
                mod.log(string.format("[hub] resolved map_key=%s local_selector=%d server_selector=%s",
                    my_map_key, my_map_sel, tostring(msg.map_sel)))
            end
        end
        my_map_label = tostring(msg.map_label or ("Map " .. tostring(my_map_sel + 1)))
        -- Force map selector immediately so game_init uses the right map.
        if mod.game and mod.game.set_map_selector then
            mod.game.set_map_selector(my_map_sel)
            local got = mod.game.get_map_selector and mod.game.get_map_selector()
            mod.log(string.format("[hub] match_found: set map selector=%d  got=%s  key=%s",
                my_map_sel, tostring(got), my_map_key))
        end
        cd_timer = 3.0
        match_ready_sent = false
        set_state(S_MATCH_FOUND)
        status("Match found. You are player " .. tostring(my_role + 1))
    elseif t == "match_start" then
        my_role = tonumber(msg.role) or my_role
        my_authority_role = tonumber(msg.authority_role) or my_authority_role
        my_map_key = tostring(msg.map_key or my_map_key or "")
        do
            local resolved_sel = nil
            if my_map_key ~= "" and map_manifest and map_manifest.selector_for_key then
                resolved_sel = map_manifest.selector_for_key(my_map_key)
            end
            my_map_sel = (resolved_sel ~= nil) and resolved_sel or (tonumber(msg.map_sel) or my_map_sel)
        end
        my_map_label = tostring(msg.map_label or my_map_label or ("Map " .. tostring((my_map_sel or 0) + 1)))
        my_match_seed = tonumber(msg.seed) or my_match_seed
        my_start_tick = tonumber(msg.start_tick) or my_start_tick
        my_input_delay = tonumber(msg.input_delay) or my_input_delay
        -- Force map selector and RNG seed immediately — before hub.close() / game launch —
        -- so both clients have the same map when game_init() picks the starting room.
        if mod.game and mod.game.set_map_selector then
            mod.game.set_map_selector(my_map_sel)
            local got = mod.game.get_map_selector and mod.game.get_map_selector()
            mod.log(string.format("[hub] match_start: set map selector=%d  got=%s  key=%s",
                my_map_sel, tostring(got), my_map_key))
        end
        if mod.game and mod.game.set_rng_seed and my_match_seed and my_match_seed ~= 0 then
            mod.game.set_rng_seed(my_match_seed)
            mod.log(string.format("[hub] match_start: primed RNG seed=%u", my_match_seed))
        end
        if state == S_MATCH_FOUND then
            start_match()
        end
    elseif t == "match_end" then
        set_state(S_HUB)
        status(post_open_message or "Match over.")
        post_open_message = nil
        hub.open()
    elseif t == "error" then
        status("Server error: " .. tostring(msg.message or "?"))
        if state == S_QUEUING then
            set_state(S_HUB)
        end
    end
end

local function pump_messages()
    proto.update()
    local msg = proto.poll()
    while msg do
        handle_msg(msg)
        msg = proto.poll()
    end

    if state == S_CONNECTING and proto.get_state() == "connected" then
        if pending_auth then
            proto.send({ type = pending_auth.kind, username = pending_auth.user, password = pending_auth.pass })
            set_state(S_LOGGING_IN)
            status(pending_auth.kind == "register" and "Registering..." or "Logging in...")
            pending_auth = nil
        else
            set_state(S_LOGIN)
            status("Connected. Please log in.")
        end
    end

    if proto.get_state() == "disconnected" and state ~= S_CONNECTING and state ~= S_IN_GAME
       and state ~= S_SERVER_SELECT and state ~= S_LOGIN then
        pending_auth = nil
        set_state(S_LOGIN)
        status("Disconnected.")
    end
end

local function panel_layout()
    local W, H = mod.ui.screen_size()
    local s = ui_scale()
    local pad = math.floor(28 * s)
    local panel_w = clamp(math.floor(860 * s), math.floor(W * 0.72), W - pad * 2)
    local panel_h = clamp(math.floor(560 * s), math.floor(H * 0.64), H - pad * 2)

    if state == S_CONNECTING or state == S_LOGGING_IN then
        panel_h = clamp(math.floor(320 * s), math.floor(H * 0.40), H - pad * 2)
    elseif state == S_SERVER_SELECT then
        panel_h = clamp(math.floor(480 * s), math.floor(H * 0.58), H - pad * 2)
    elseif state == S_LOGIN then
        panel_h = clamp(math.floor(640 * s), math.floor(H * 0.72), H - pad * 2)
    elseif state == S_HUB then
        panel_h = clamp(math.floor(520 * s), math.floor(H * 0.60), H - pad * 2)
    elseif state == S_QUEUING then
        panel_h = clamp(math.floor(360 * s), math.floor(H * 0.46), H - pad * 2)
    elseif state == S_MATCH_FOUND then
        panel_h = clamp(math.floor(408 * s), math.floor(H * 0.50), H - pad * 2)
    end

    local px = math.floor((W - panel_w) * 0.5)
    local py = math.floor((H - panel_h) * 0.5)
    local header_h = math.floor(78 * s)
    local footer_h = math.floor(50 * s)
    local inner_pad = math.floor(28 * s)
    local content_x = px + inner_pad
    local content_y = py + header_h + inner_pad
    local content_w = panel_w - inner_pad * 2
    local content_h = panel_h - header_h - footer_h - inner_pad * 2
    return {
        W = W, H = H, s = s,
        x = px, y = py, w = panel_w, h = panel_h,
        header_h = header_h, footer_h = footer_h,
        cx = px + panel_w * 0.5,
        content_x = content_x, content_y = content_y,
        content_w = content_w, content_h = content_h,
    }
end

-- draw_panel(title, subtitle [, hint])
-- hint: nil = show "ESC" dimly, false = no footer text, string = custom text
local function draw_panel(title, subtitle, hint)
    local L = panel_layout()
    local x, y, w, h, s = L.x, L.y, L.w, L.h, L.s
    local t = mod.game.tick_count()
    local pulse = 0.5 + 0.5 * math.sin(t * 0.035)

    -- Dim overlay + CRT scanlines
    fill_rect(0, 0, L.W, L.H, 0.01, 0.02, 0.03, 0.10)
    local scan_step = math.max(3, math.floor(5 * s))
    local scan_h    = math.max(1, math.floor(s))
    for sy = 0, L.H, scan_step do
        fill_rect(0, sy, L.W, scan_h, 0.0, 0.0, 0.0, 0.07)
    end

    fill_rect(x, y, w, h, 0.03, 0.05, 0.08, 0.92)
    fill_rect(x + 3, y + 3, w - 6, h - 6, 0.06, 0.08, 0.12, 0.55)
    fill_rect(x, y, w, L.header_h, 0.10, 0.13, 0.20, 0.96)
    fill_rect(x, y + h - L.footer_h, w, L.footer_h, 0.03, 0.04, 0.06, 0.94)
    fill_rect(L.content_x, L.content_y, L.content_w, L.content_h, 0.04, 0.07, 0.11, 0.48)

    -- Pulsing outer border
    stroke_rect(x, y, w, h, 2.0, 0.40 + pulse * 0.16, 0.50 + pulse * 0.10, 0.66 + pulse * 0.12, 1.0)
    stroke_rect(x + 4, y + 4, w - 8, h - 8, 1.0, 0.16, 0.22, 0.32, 0.92)

    -- Accent line at the bottom edge of the header
    local al_h = math.max(2, math.floor(2 * s))
    fill_rect(x + 4, y + L.header_h - al_h, w - 8, al_h,
        0.32 + pulse * 0.18, 0.44 + pulse * 0.14, 0.70 + pulse * 0.12, 0.88)

    draw_text_center(title, L.cx, y + math.floor(18 * s), 1.90 * s, 0.96, 0.97, 0.99)
    if subtitle and subtitle ~= "" then
        -- Inside the header (header_h = 78*s); title at 18*s, subtitle at 52*s
        draw_text_center(subtitle, L.cx, y + math.floor(52 * s), 1.10 * s, 0.95, 0.82, 0.46)
    end
    if status_msg ~= "" then
        draw_text_center(status_msg, L.cx, y + L.header_h + math.floor(34 * s), 1.04 * s, 0.80, 0.87, 0.96)
    end
    local hint_text = (hint == nil) and "ESC" or (type(hint) == "string" and hint or nil)
    if hint_text then
        draw_text(hint_text, x + math.floor(18 * s), y + h - L.footer_h + math.floor(12 * s), 0.88 * s, 0.40, 0.48, 0.58)
    end
    return L
end

local function button_box(label, x, y, w, h, primary)
    local hovered = mouse_in_rect(x, y, w, h)
    local clicked = consume_click(x, y, w, h)
    local br, bg, bb, ba = 0.10, 0.15, 0.23, 0.90
    local tr, tg, tb = 0.90, 0.93, 0.98

    if primary then
        br, bg, bb, ba = 0.19, 0.13, 0.05, 0.96
        tr, tg, tb = 0.98, 0.88, 0.42
    end
    if hovered then
        br = br + 0.05
        bg = bg + 0.05
        bb = bb + 0.05
    end

    fill_rect(x, y, w, h, br, bg, bb, ba)
    stroke_rect(x, y, w, h, hovered and 2.0 or 1.0, 0.36, 0.45, 0.60, hovered and 1.0 or 0.84)
    draw_text_center(label, x + w * 0.5, y + h * 0.5 - math.floor(12 * ui_scale()), 1.18 * ui_scale(), tr, tg, tb)
    return clicked, hovered
end

local function field_box(label, value, x, y, w, h, focused, masked)
    local clicked = consume_click(x, y, w, h)
    local hovered = mouse_in_rect(x, y, w, h)
    local shown = tostring(value or "")
    if masked then shown = string.rep("*", #shown) end
    if focused then
        local blink = (math.floor(mod.game.tick_count() / 20) % 2 == 0) and "_" or ""
        shown = shown .. blink
    end
    if #shown > 24 then shown = shown:sub(#shown - 23) end

    draw_text(label, x + 2, y - math.floor(32 * ui_scale()), 1.00 * ui_scale(), 0.74, 0.82, 0.94)
    fill_rect(x, y, w, h, focused and 0.10 or 0.05, focused and 0.13 or 0.08, focused and 0.18 or 0.11, hovered and 0.98 or 0.92)
    stroke_rect(x, y, w, h, focused and 2.0 or 1.0,
        focused and 0.98 or 0.36,
        focused and 0.84 or 0.45,
        focused and 0.38 or 0.60,
        focused and 1.0 or 0.86)
    draw_text(shown ~= "" and shown or " ", x + math.floor(18 * ui_scale()), y + h * 0.5 - math.floor(12 * ui_scale()), 1.16 * ui_scale(), 0.92, 0.95, 0.99)
    return clicked, hovered
end

local CURSOR_PIXELS = {
    "YYY.........",
    "OYYY........",
    "OOYYY.......",
    ".OOYYY......",
    "..OOYYY.....",
    "...OOYYY....",
    "....OOY.OOO.",
    ".....O.OYY..",
    "......OYY...",
    "......OO.O..",
    "......O...YO",
    "..........OO",
}

local function draw_cursor_pixels(x, y, px, shadow)
    for row = 1, #CURSOR_PIXELS do
        local line = CURSOR_PIXELS[row]
        for col = 1, #line do
            local c = line:sub(col, col)
            if c ~= "." then
                local r, g, b, a
                if shadow then
                    r, g, b, a = 0.0, 0.0, 0.0, 0.35
                elseif c == "Y" then
                    r, g, b, a = 1.0, 231 / 255, 4 / 255, 1.0
                else
                    r, g, b, a = 154 / 255, 119 / 255, 0.0, 1.0
                end
                fill_rect(x + (col - 1) * px, y + (row - 1) * px, px, px, r, g, b, a)
            end
        end
    end
end

local function draw_mouse_cursor()
    local mx, my = mouse_pos()
    local s = ui_scale()
    local px = math.max(2, math.floor(2.0 * s))
    local ox = math.floor(mx + 8 * s)
    local oy = math.floor(my + 4 * s)
    draw_cursor_pixels(ox + math.max(1, math.floor(px * 0.8)), oy + math.max(1, math.floor(px * 0.8)), px, true)
    draw_cursor_pixels(ox, oy, px, false)
end

local function draw_connecting()
    local sv = servers and servers.get_selected and servers.get_selected()
    local addr = sv and (sv.name .. "  " .. sv.host .. ":" .. tostring(sv.port)) or "..."
    local L = draw_panel("EGGNOGG+ ONLINE", "Connecting to server")
    draw_text_center(addr, L.cx, L.content_y + math.floor(22 * L.s), 1.04 * L.s, 0.76, 0.82, 0.94)
    local bw = math.floor(280 * L.s)
    local bh = math.floor(56 * L.s)
    if button_box("BACK", L.cx - bw * 0.5, L.y + L.h - L.footer_h - bh - math.floor(20 * L.s), bw, bh, false) then
        pending_auth = nil
        proto.disconnect()
        set_state(S_LOGIN)
        status("")
    end
end

local function draw_checkbox(label, x, y, size, checked, s)
    local clicked = consume_click(x, y, size + approx_text_w(label, 0.88 * s), size)
    local hovered = mouse_in_rect(x, y, size + approx_text_w(label, 0.88 * s), size)
    fill_rect(x, y, size, size, 0.06, 0.09, 0.14, 0.92)
    stroke_rect(x, y, size, size, hovered and 2.0 or 1.0,
        hovered and 0.70 or 0.36, hovered and 0.82 or 0.45, hovered and 0.95 or 0.60, 1.0)
    if checked then
        local p = math.floor(size * 0.22)
        fill_rect(x + p, y + p, size - p * 2, size - p * 2, 0.95, 0.83, 0.32, 1.0)
    end
    draw_text(label, x + size + math.floor(8 * s), y + math.floor(size * 0.35), 0.88 * s, 0.74, 0.82, 0.94)
    return clicked
end

local function draw_login()
    local L = draw_panel("EGGNOGG+ ONLINE", "Sign in to queue for matches")
    local fx = L.content_x + math.floor(18 * L.s)
    local fw = L.content_w - math.floor(36 * L.s)
    local field_h = math.floor(64 * L.s)
    local btn_h   = math.floor(58 * L.s)
    local gap     = math.floor(24 * L.s)
    -- Tight top gap so fields + checkbox fit even on small panels
    local y = L.content_y + math.floor(16 * L.s)

    local pass_y = y + field_h + math.floor(32 * L.s) + math.floor(16 * L.s)
    if select(1, field_box("Username", field_user, fx, y, fw, field_h, focus == 1, false)) then focus = 1 end
    if select(1, field_box("Password", field_pass, fx, pass_y, fw, field_h, focus == 2, true)) then focus = 2 end

    -- Buttons anchored to panel bottom; checkbox snug above LOG IN
    local by = L.y + L.h - L.footer_h - btn_h * 2 - gap - math.floor(20 * L.s)
    local cb_size = math.floor(22 * L.s)
    local cb_y    = by - cb_size - math.floor(8 * L.s)
    if draw_checkbox("Remember me", fx, cb_y, cb_size, remember_me, L.s) then
        remember_me = not remember_me
    end

    if button_box("LOG IN", fx, by, fw, btn_h, true) then
        begin_login_request("login")
    end
    if button_box("REGISTER", fx, by + btn_h + gap, fw, btn_h, false) then
        begin_login_request("register")
    end
end

local function draw_logging_in()
    local L = draw_panel("AUTHENTICATING", "Waiting for server")
    draw_text_center("Please wait...", L.cx, L.content_y + math.floor(28 * L.s), 1.20 * L.s, 0.78, 0.84, 0.98)
    local bw = math.floor(280 * L.s)
    local bh = math.floor(56 * L.s)
    if button_box("CANCEL", L.cx - bw * 0.5, L.y + L.h - L.footer_h - bh - math.floor(20 * L.s), bw, bh, false) then
        proto.disconnect()
        set_state(S_LOGIN)
        status("Disconnected.")
    end
end

local function draw_hub_screen()
    local sv = servers.get_selected()
    local subtitle = username ~= "" and ("Logged in as " .. username) or "Connected"
    local L = draw_panel("ONLINE HUB", subtitle)
    local fx = L.content_x + math.floor(18 * L.s)
    local fw = L.content_w - math.floor(36 * L.s)
    local btn_h = math.floor(56 * L.s)
    local gap   = math.floor(20 * L.s)
    local info_y = L.content_y + math.floor(40 * L.s)
    draw_text("Players in queue", fx, info_y, 1.10 * L.s, 0.72, 0.80, 0.92)
    draw_text(tostring(queue_count), fx + fw - math.floor(34 * L.s), info_y - math.floor(4 * L.s), 1.44 * L.s, 0.96, 0.88, 0.42)
    -- Current region indicator
    if sv then
        local ping_ms  = servers.get_ping(servers.get_selected_idx())
        local ping_str = ping_ms and ("  " .. tostring(ping_ms) .. " ms") or ""
        draw_text("Region: " .. sv.name .. ping_str, fx, info_y + math.floor(36 * L.s), 0.90 * L.s, 0.55, 0.68, 0.84)
    end
    local by = L.y + L.h - L.footer_h - btn_h * 2 - gap - math.floor(28 * L.s)
    if button_box("FIND MATCH", fx, by, fw, btn_h, true) then
        map_manifest.send()
        proto.send({ type = "join_queue" })
        set_state(S_QUEUING)
        status("")
    end
    if button_box("DISCONNECT", fx, by + btn_h + gap, fw, btn_h, false) then
        proto.disconnect()
        hub.close()
    end

    -- Small REGION button in the top-right corner of the header
    local rbw = math.floor(100 * L.s)
    local rbh = math.floor(30 * L.s)
    local rbx = L.x + L.w - rbw - math.floor(14 * L.s)
    local rby = L.y + math.floor((L.header_h - rbh) * 0.5)
    local rhov = mouse_in_rect(rbx, rby, rbw, rbh)
    local rclk = consume_click(rbx, rby, rbw, rbh)
    fill_rect(rbx, rby, rbw, rbh, 0.08, 0.12, 0.20, rhov and 0.96 or 0.80)
    stroke_rect(rbx, rby, rbw, rbh, rhov and 2.0 or 1.0, 0.30, 0.40, 0.58, 0.90)
    draw_text_center("REGION", rbx + rbw * 0.5, rby + rbh * 0.5 - math.floor(10 * L.s), 0.80 * L.s, 0.75, 0.83, 0.95)
    if rclk then
        server_select_back = S_HUB
        set_state(S_SERVER_SELECT)
        if servers and servers.refresh then servers.refresh() end
    end
end

local function draw_queueing()
    local L = draw_panel("MATCHMAKING", nil, false)
    local t = mod.game.tick_count()
    local pulse = 0.60 + 0.30 * math.sin(t * 0.08)
    -- Animated ellipsis: 0 → 1 → 2 → 3 → 0 dots, held for 15 ticks each
    local dot_n = math.floor(t / 15) % 4
    local dots  = string.rep(".", dot_n) .. string.rep(" ", 3 - math.min(dot_n, 3))
    draw_text_center("SEARCHING" .. dots, L.cx, L.content_y + math.floor(22 * L.s),
        1.26 * L.s, pulse * 0.68, pulse * 0.82, pulse)
    local qr = queue_count >= 2 and 0.68 or 0.52
    local qg = queue_count >= 2 and 0.96 or 0.64
    local qb = queue_count >= 2 and 0.48 or 0.78
    local qpulse = queue_count >= 2 and (0.85 + 0.15 * math.sin(t * 0.12)) or 1.0
    draw_text_center(tostring(queue_count) .. " in queue", L.cx,
        L.content_y + math.floor(78 * L.s), 1.08 * L.s, qr * qpulse, qg * qpulse, qb * qpulse)
    local bw = math.floor(280 * L.s)
    local bh = math.floor(56 * L.s)
    if button_box("CANCEL", L.cx - bw * 0.5, L.y + L.h - L.footer_h - bh - math.floor(20 * L.s), bw, bh, false) then
        proto.send({ type = "leave_queue" })
        set_state(S_HUB)
        status("Left queue.")
    end
end

local function draw_match_found(dt)
    local L = draw_panel("MATCH FOUND", "Get ready", false)
    local t = mod.game.tick_count()
    cd_timer = math.max(0, cd_timer - dt)

    -- Pulsing highlight bar behind the player label
    local hl_pulse = 0.5 + 0.5 * math.sin(t * 0.09)
    fill_rect(L.content_x, L.content_y + math.floor(4 * L.s),
        L.content_w, math.floor(28 * L.s),
        0.08, 0.22 + hl_pulse * 0.06, 0.10, hl_pulse * 0.40)

    draw_text_center("You are player " .. tostring(my_role + 1), L.cx, L.content_y + math.floor(12 * L.s), 1.14 * L.s, 0.66, 0.96, 0.70)
    draw_text_center(my_map_label ~= "" and my_map_label or ("Map " .. tostring((my_map_sel or 0) + 1)), L.cx, L.content_y + math.floor(66 * L.s), 1.08 * L.s, 0.70, 0.78, 0.94)
    if cd_timer > 0 then
        draw_text_center("Starting in " .. tostring(math.ceil(cd_timer)) .. "...", L.cx, L.content_y + math.floor(126 * L.s), 1.34 * L.s, 0.98, 0.84, 0.42)
    else
        draw_text_center("Waiting for start...", L.cx, L.content_y + math.floor(126 * L.s), 1.34 * L.s, 0.98, 0.84, 0.42)
    end
    if not match_ready_sent then
        proto.send({ type = "ready" })
        match_ready_sent = true
    end
    local bar_x = L.content_x + math.floor(18 * L.s)
    local bar_w = L.content_w - math.floor(36 * L.s)
    local bar_y = L.y + L.h - L.footer_h - math.floor(52 * L.s)
    local bar_h = math.floor(18 * L.s)
    fill_rect(bar_x, bar_y, bar_w, bar_h, 0.09, 0.12, 0.18, 0.92)
    local progress = 1.0 - math.max(0.0, math.min(1.0, cd_timer / 3.0))
    fill_rect(bar_x, bar_y, bar_w * progress, bar_h, 0.95, 0.83, 0.32, 0.95)
    stroke_rect(bar_x, bar_y, bar_w, bar_h, 1.0, 0.36, 0.45, 0.60, 0.84)
end

-- ── server select screen ──────────────────────────────────────────────────
local function draw_server_select()
    local L = draw_panel("SELECT SERVER", "Choose a region to connect to", "ESC")
    local s  = L.s
    local list = servers.get_list()
    local sel  = servers.get_selected_idx()

    local row_h = math.floor(58 * s)
    local fx    = L.content_x + math.floor(10 * s)
    local fw    = L.content_w - math.floor(20 * s)

    -- BEST PING button at top of content area
    local bbw = math.floor(200 * s)
    local bbh = math.floor(40 * s)
    local bbx = L.cx - bbw * 0.5
    local bby = L.content_y + math.floor(6 * s)
    if button_box("BEST PING", bbx, bby, bbw, bbh, false) then
        -- Pick server with lowest measured ping; fall back to first
        local best_i, best_ms = 1, math.huge
        for i = 1, #list do
            local ms = servers.get_ping(i)
            if ms and ms < best_ms then best_ms = ms; best_i = i end
        end
        servers.select(best_i)
        load_server_creds()
        set_state(S_LOGIN)
    end

    -- Server rows below the BEST PING button
    local ry = bby + bbh + math.floor(12 * s)
    for i, sv in ipairs(list) do
        local ping_ms = servers.get_ping(i)
        local is_sel  = (i == sel)
        local row_w   = fw
        local row_x   = fx
        local hov = mouse_in_rect(row_x, ry, row_w, row_h - math.floor(4*s))
        local clk = consume_click(row_x, ry, row_w, row_h - math.floor(4*s))

        local bg_a = is_sel and 0.30 or (hov and 0.18 or 0.08)
        fill_rect(row_x, ry, row_w, row_h - math.floor(4*s), 0.14, 0.24, 0.40, bg_a)
        stroke_rect(row_x, ry, row_w, row_h - math.floor(4*s),
            (is_sel or hov) and 2.0 or 1.0,
            is_sel and 0.60 or 0.30, is_sel and 0.78 or 0.42, is_sel and 0.98 or 0.60,
            is_sel and 1.0 or 0.70)

        local nr = is_sel and 0.98 or (hov and 0.92 or 0.80)
        local ng = is_sel and 0.90 or (hov and 0.88 or 0.80)
        local nb = is_sel and 0.46 or (hov and 0.82 or 0.80)
        draw_text(sv.name, row_x + math.floor(16*s), ry + math.floor(6*s), 1.08 * s, nr, ng, nb)
        draw_text(sv.host .. ":" .. tostring(sv.port), row_x + math.floor(16*s), ry + math.floor(30*s), 0.78 * s, 0.50, 0.60, 0.74)

        -- Ping
        local ping_str, pr, pg, pb
        if ping_ms then
            ping_str = tostring(ping_ms) .. " ms"
            pr = ping_ms < 80 and 0.38 or (ping_ms < 150 and 0.82 or 0.92)
            pg = ping_ms < 80 and 0.92 or (ping_ms < 150 and 0.82 or 0.38)
            pb = 0.40
        else
            local dots = string.rep(".", math.floor(mod.game.tick_count() / 15) % 4)
            ping_str = "pinging" .. dots
            pr, pg, pb = 0.48, 0.56, 0.70
        end
        local ptw = approx_text_w(ping_str, 0.90 * s)
        draw_text(ping_str, row_x + row_w - math.floor(14*s) - ptw, ry + math.floor(18*s), 0.90 * s, pr, pg, pb)

        if clk then
            servers.select(i)
            load_server_creds()
            set_state(S_LOGIN)
        end
        ry = ry + row_h
    end

    if #list == 0 then
        local fs, fe = servers.get_fetch_status()
        local msg, mr, mg, mb
        if fs == "fetching" then
            local dots = string.rep(".", math.floor(mod.game.tick_count() / 15) % 4)
            msg = "Fetching server list" .. dots
            mr, mg, mb = 0.60, 0.70, 0.85
        elseif fs == "no_http" then
            msg = "HTTP unavailable — rebuild the mod DLL"
            mr, mg, mb = 0.90, 0.50, 0.30
        elseif fs == "error" then
            msg = "Error: " .. tostring(fe or "unknown")
            mr, mg, mb = 0.92, 0.40, 0.40
        else
            msg = "No servers found — check server_list_url in config"
            mr, mg, mb = 0.75, 0.70, 0.55
        end
        draw_text_center(msg, L.cx, L.content_y + math.floor(80 * s), 0.96 * s, mr, mg, mb)
    end
end

function hub.open()
    hub.ensure_state()
    click_pending = false
    pending_auth  = nil
    -- Fetch fresh server list + re-ping every time the hub opens
    if servers and servers.refresh then servers.refresh() end

    local ps = proto.get_state()
    if ps == "connected" then
        if username ~= "" then set_state(S_HUB) else set_state(S_LOGIN) end
        if post_open_message and post_open_message ~= "" then
            status(post_open_message)
            post_open_message = nil
        end
    elseif ps == "connecting" then
        set_state(S_CONNECTING)
    else
        -- Disconnected — if no server selected yet, show server picker first
        if not servers.get_selected() then
            server_select_back = S_LOGIN
            set_state(S_SERVER_SELECT)
        else
            load_server_creds()
            set_state(S_LOGIN)
            status("")
        end
    end
    ui_enter_state(STATE_NAME)
end

function hub.close()
    click_pending = false
    ui_leave_state()
end

function hub.consume_start_request()
    local want = pending_start
    pending_start = false
    return want
end

function hub.draw(dt)
    pump_messages()
    servers.update()   -- drive fetch + ping state machines every frame
    if state == S_SERVER_SELECT then
        draw_server_select()
    elseif state == S_CONNECTING then
        draw_connecting()
    elseif state == S_LOGIN then
        draw_login()
    elseif state == S_LOGGING_IN then
        draw_logging_in()
    elseif state == S_HUB then
        draw_hub_screen()
    elseif state == S_QUEUING then
        draw_queueing()
    elseif state == S_MATCH_FOUND then
        draw_match_found(dt)
    end
    if state ~= S_IN_GAME then
        draw_mouse_cursor()
    end
end

function hub.on_event(e)
    if e.type == "mousebuttondown" and e.button == 1 then
        click_pending = true
        click_x = e.x or 0
        click_y = e.y or 0
        return true
    end

    if state == S_LOGIN then
        if e.type == "textinput" then
            local ch = e.text or ((e.sym and e.sym > 0) and string.char(e.sym) or nil)
            if ch and ch ~= "" then
                if focus == 1 and #field_user < 24 then
                    field_user = field_user .. ch
                elseif focus == 2 and #field_pass < 64 then
                    field_pass = field_pass .. ch
                end
            end
            return true
        elseif e.type == "keydown" then
            local sym = e.sym
            if sym == 8 or sym == 127 then
                if focus == 1 and #field_user > 0 then
                    field_user = field_user:sub(1, -2)
                elseif focus == 2 and #field_pass > 0 then
                    field_pass = field_pass:sub(1, -2)
                end
                return true
            elseif sym == 9 then
                focus = (focus == 1) and 2 or 1
                return true
            elseif sym == 13 then
                begin_login_request("login")
                return true
            elseif sym == 27 then
                proto.disconnect()
                hub.close()
                return true
            end
        end
    elseif e.type == "keydown" then
        if e.sym == 27 and state == S_SERVER_SELECT then
            set_state(server_select_back)
            return true
        end
        if e.sym == 27 and state == S_CONNECTING then
            pending_auth = nil
            proto.disconnect()
            set_state(S_LOGIN)
            status("")
            return true
        end
        if e.sym == 13 then
            if state == S_HUB then
                proto.send({ type = "join_queue" })
                set_state(S_QUEUING)
                status("")
                return true
            elseif state == S_QUEUING then
                return true
            end
        elseif e.sym == 27 then
            if state == S_QUEUING then
                proto.send({ type = "leave_queue" })
                set_state(S_HUB)
                status("Left queue.")
            elseif state == S_HUB or state == S_CONNECTING or state == S_LOGGING_IN then
                proto.disconnect()
                hub.close()
            end
            return true
        end
    end

    return true
end

function hub.get_state()
    return state
end

function hub.get_role()
    return my_role
end

function hub.get_authority_role()
    return my_authority_role
end

function hub.get_map_key()
    return my_map_key
end

function hub.get_map_sel()
    return my_map_sel
end

function hub.get_match_seed()
    return my_match_seed
end

function hub.get_start_tick()
    return my_start_tick
end

function hub.get_input_delay()
    return my_input_delay
end

function hub.get_username()
    return username
end

function hub.get_opponent()
    return my_opponent
end
