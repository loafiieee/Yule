-- lib/hub.lua - online hub custom state UI.

hub = hub or {}

local STATE_NAME = "online_hub"

local S_CONNECTING  = "connecting"
local S_LOGIN       = "login"
local S_LOGGING_IN  = "logging_in"
local S_HUB         = "hub"
local S_QUEUING     = "queuing"
local S_MATCH_FOUND = "match_found"
local S_IN_GAME     = "in_game"

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
local my_match_seed = 0
local my_start_tick = 0
local my_input_delay = 1
local match_ready_sent = false
local focus = 1
local field_user = ""
local field_pass = ""
local state_ready = false
local pending_start = false
local post_open_message = nil

local click_pending = false
local click_x = 0
local click_y = 0

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
    username = field_user
    proto.send({ type = kind, username = field_user, password = field_pass })
    set_state(S_LOGGING_IN)
    status(kind == "register" and "Registering..." or "Logging in...")
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
        set_state(S_LOGIN)
        status("Connected. Please log in.")
    end

    if proto.get_state() == "disconnected" and state ~= S_CONNECTING and state ~= S_IN_GAME then
        if state ~= S_LOGIN then
            set_state(S_LOGIN)
            status("Disconnected.")
        end
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
    elseif state == S_HUB then
        panel_h = clamp(math.floor(448 * s), math.floor(H * 0.56), H - pad * 2)
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

    fill_rect(0, 0, L.W, L.H, 0.01, 0.02, 0.03, 0.10)
    fill_rect(x, y, w, h, 0.03, 0.05, 0.08, 0.92)
    fill_rect(x + 3, y + 3, w - 6, h - 6, 0.06, 0.08, 0.12, 0.55)
    fill_rect(x, y, w, L.header_h, 0.10, 0.13, 0.20, 0.96)
    fill_rect(x, y + h - L.footer_h, w, L.footer_h, 0.03, 0.04, 0.06, 0.94)
    fill_rect(L.content_x, L.content_y, L.content_w, L.content_h, 0.04, 0.07, 0.11, 0.48)
    stroke_rect(x, y, w, h, 2.0, 0.40, 0.50, 0.66, 1.0)
    stroke_rect(x + 4, y + 4, w - 8, h - 8, 1.0, 0.16, 0.22, 0.32, 0.92)

    draw_text_center(title, L.cx, y + math.floor(18 * s), 1.90 * s, 0.96, 0.97, 0.99)
    if subtitle and subtitle ~= "" then
        draw_text_center(subtitle, L.cx, y + L.header_h + math.floor(4 * s), 1.10 * s, 0.95, 0.82, 0.46)
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
    local L = draw_panel("EGGNOGG+ ONLINE", "Connecting to server")
    draw_text_center("Opening socket to " .. tostring(config.get("server_host", "127.0.0.1")) .. ":" .. tostring(config.get("server_port", 7878)),
        L.cx, L.content_y + math.floor(22 * L.s), 1.04 * L.s, 0.76, 0.82, 0.94)
    local bw = math.floor(280 * L.s)
    local bh = math.floor(56 * L.s)
    if button_box("BACK", L.cx - bw * 0.5, L.y + L.h - L.footer_h - bh - math.floor(20 * L.s), bw, bh, false) then
        proto.disconnect()
        hub.close()
    end
end

local function draw_login()
    local L = draw_panel("EGGNOGG+ ONLINE", "Sign in to queue for matches")
    local fx = L.content_x + math.floor(18 * L.s)
    local fw = L.content_w - math.floor(36 * L.s)
    local field_h = math.floor(64 * L.s)
    local btn_h = math.floor(58 * L.s)
    local gap = math.floor(24 * L.s)
    local y = L.content_y + math.floor(32 * L.s)

    if select(1, field_box("Username", field_user, fx, y, fw, field_h, focus == 1, false)) then focus = 1 end
    if select(1, field_box("Password", field_pass, fx, y + field_h + math.floor(52 * L.s), fw, field_h, focus == 2, true)) then focus = 2 end

    local by = L.y + L.h - L.footer_h - btn_h * 2 - gap - math.floor(28 * L.s)
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
    local L = draw_panel("ONLINE HUB", username ~= "" and ("Logged in as " .. username) or "Connected")
    local fx = L.content_x + math.floor(18 * L.s)
    local fw = L.content_w - math.floor(36 * L.s)
    local btn_h = math.floor(62 * L.s)
    local gap = math.floor(28 * L.s)
    local info_y = L.content_y + math.floor(40 * L.s)
    draw_text("Players in queue", fx, info_y, 1.10 * L.s, 0.72, 0.80, 0.92)
    draw_text(tostring(queue_count), fx + fw - math.floor(34 * L.s), info_y - math.floor(4 * L.s), 1.44 * L.s, 0.96, 0.88, 0.42)
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
end

local function draw_queueing()
    local L = draw_panel("MATCHMAKING", nil, false)
    local t = mod.game.tick_count()
    local pulse = 0.60 + 0.30 * math.sin(t * 0.08)
    local spinners = {"|", "/", "-", "\\"}
    local spin = spinners[(math.floor(t / 10) % 4) + 1]
    draw_text_center(spin .. " SEARCHING " .. spin, L.cx, L.content_y + math.floor(22 * L.s),
        1.26 * L.s, pulse * 0.68, pulse * 0.82, pulse)
    local qr = queue_count >= 2 and 0.68 or 0.52
    local qg = queue_count >= 2 and 0.96 or 0.64
    local qb = queue_count >= 2 and 0.48 or 0.78
    draw_text_center(tostring(queue_count) .. " in queue", L.cx,
        L.content_y + math.floor(78 * L.s), 1.08 * L.s, qr, qg, qb)
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
    cd_timer = math.max(0, cd_timer - dt)
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

function hub.open(host, port)
    hub.ensure_state()
    click_pending = false
    if host and port and proto.get_state() == "disconnected" then
        focus = 1
        if field_user == "" then field_user = username or "" end
        set_state(S_CONNECTING)
        status("Connecting...")
        proto.connect(host, port)
    elseif proto.get_state() == "connected" then
        if username ~= "" then set_state(S_HUB) else set_state(S_LOGIN) end
        if post_open_message and post_open_message ~= "" then
            status(post_open_message)
            post_open_message = nil
        end
    elseif proto.get_state() == "connecting" then
        set_state(S_CONNECTING)
    else
        set_state(S_LOGIN)
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
    if state == S_CONNECTING then
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
