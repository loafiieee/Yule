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
local my_map_sel = 0
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
local sword_cursor_id = nil

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
    if s < 0.94 then s = 0.94 end
    if s > 1.35 then s = 1.35 end
    return s
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

local function ensure_cursor_sprite()
    if sword_cursor_id ~= nil then return end
    local id = mod.ui.sprite_id("misc", 14)
    if not id then id = mod.ui.sprite_id("misc", 31) end
    sword_cursor_id = id or false
end

function hub.ensure_state()
    if not state_ready then
        state_ready = ui_create_state(STATE_NAME) and true or false
        ensure_cursor_sprite()
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
        set_state(S_HUB)
        status("Logged in as " .. username)
    elseif t == "auth_fail" then
        set_state(S_LOGIN)
        status("Login failed: " .. tostring(msg.reason or "?"))
    elseif t == "queue_update" then
        queue_count = tonumber(msg.count) or queue_count
    elseif t == "match_found" then
        my_role = tonumber(msg.role) or 0
        my_map_sel = tonumber(msg.map_sel) or 0
        sync.prepare(my_role, my_map_sel)
        cd_timer = 3.0
        match_ready_sent = false
        set_state(S_MATCH_FOUND)
        status("Match found. You are player " .. tostring(my_role + 1))
    elseif t == "match_start" then
        if state == S_MATCH_FOUND then
            sync.apply_map()
            start_match()
        end
    elseif t == "match_end" then
        sync.stop()
        set_state(S_HUB)
        status(post_open_message or "Match over.")
        post_open_message = nil
        hub.open()
    elseif t == "error" then
        status("Server error: " .. tostring(msg.message or "?"))
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

local function ui_layout_for_state()
    local W, H = mod.ui.screen_size()
    local s = ui_scale()

    local panel_w = math.floor(math.min(W - 56, math.max(W * 0.62, 760 * s)))
    local panel_h = math.floor(math.min(H - 56, math.max(H * 0.52, 420 * s)))

    if state == S_CONNECTING or state == S_LOGGING_IN then
        panel_h = math.floor(math.min(H - 56, math.max(H * 0.34, 280 * s)))
    elseif state == S_HUB then
        panel_h = math.floor(math.min(H - 56, math.max(H * 0.44, 360 * s)))
    elseif state == S_QUEUING then
        panel_h = math.floor(math.min(H - 56, math.max(H * 0.38, 320 * s)))
    elseif state == S_MATCH_FOUND then
        panel_h = math.floor(math.min(H - 56, math.max(H * 0.40, 330 * s)))
    end

    local px = math.floor((W - panel_w) * 0.5)
    local py = math.floor((H - panel_h) * 0.5)
    return W, H, px, py, panel_w, panel_h, s
end

local function draw_panel(title, subtitle)
    local W, H, px, py, pw, ph, s = ui_layout_for_state()
    local cx = px + pw * 0.5
    local title_h = math.floor(62 * s)
    local footer_h = math.floor(40 * s)

    fill_rect(0, 0, W, H, 0.01, 0.02, 0.03, 0.06)
    fill_rect(px, py, pw, ph, 0.03, 0.05, 0.08, 0.88)
    fill_rect(px, py, pw, title_h, 0.10, 0.13, 0.20, 0.94)
    fill_rect(px, py + ph - footer_h, pw, footer_h, 0.02, 0.04, 0.06, 0.92)
    fill_rect(px + math.floor(22 * s), py + title_h + math.floor(18 * s), pw - math.floor(44 * s), ph - title_h - footer_h - math.floor(36 * s), 0.04, 0.07, 0.11, 0.46)
    stroke_rect(px, py, pw, ph, 2.0, 0.35, 0.44, 0.59, 0.96)

    draw_text_center(title, cx, py + math.floor(14 * s), 1.62 * s, 0.96, 0.97, 0.99)
    if subtitle and subtitle ~= "" then
        draw_text_center(subtitle, cx, py + title_h + math.floor(18 * s), 1.06 * s, 0.94, 0.77, 0.42)
    end
    if status_msg ~= "" then
        draw_text_center(status_msg, cx, py + title_h + math.floor(54 * s), 0.96 * s, 0.78, 0.85, 0.95)
    end
    draw_text("ESC to back out", px + math.floor(18 * s), py + ph - footer_h + math.floor(8 * s), 0.84 * s, 0.60, 0.68, 0.80)
    return W, H, px, py, pw, ph, cx, s, title_h, footer_h
end

local function button_box(label, x, y, w, h, primary)
    local hovered = mouse_in_rect(x, y, w, h)
    local clicked = consume_click(x, y, w, h)
    local br, bg, bb, ba = 0.10, 0.15, 0.23, 0.88
    local tr, tg, tb = 0.90, 0.93, 0.98

    if primary then
        br, bg, bb, ba = 0.18, 0.13, 0.05, 0.94
        tr, tg, tb = 0.98, 0.88, 0.42
    end
    if hovered then
        br = br + 0.06
        bg = bg + 0.06
        bb = bb + 0.06
    end

    fill_rect(x, y, w, h, br, bg, bb, ba)
    stroke_rect(x, y, w, h, hovered and 2.0 or 1.0, 0.34, 0.42, 0.56, hovered and 1.0 or 0.82)
    draw_text_center(label, x + w * 0.5, y + h * 0.5 - math.floor(10 * ui_scale()), 1.08 * ui_scale(), tr, tg, tb)
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
    if #shown > 28 then shown = shown:sub(#shown - 27) end

    draw_text(label, x + 2, y - math.floor(28 * ui_scale()), 0.94 * ui_scale(), 0.72, 0.80, 0.92)
    fill_rect(x, y, w, h, focused and 0.09 or 0.05, focused and 0.12 or 0.08, focused and 0.17 or 0.11, hovered and 0.96 or 0.90)
    stroke_rect(x, y, w, h, focused and 2.2 or 1.2,
        focused and 0.98 or 0.34,
        focused and 0.84 or 0.42,
        focused and 0.38 or 0.56,
        focused and 1.0 or 0.85)
    draw_text(shown ~= "" and shown or " ", x + math.floor(16 * ui_scale()), y + h * 0.5 - math.floor(10 * ui_scale()), 1.08 * ui_scale(), 0.90, 0.94, 0.99)
    return clicked, hovered
end

local function draw_mouse_cursor()
    local mx, my = mouse_pos()
    local s = ui_scale()
    if sword_cursor_id and sword_cursor_id ~= false then
        mod.ui.draw_sprite(sword_cursor_id, mx + math.floor(12 * s), my + math.floor(11 * s), {
            scale = 3.0 * s,
            r = 0.0, g = 0.0, b = 0.0, a = 0.35,
        })
        mod.ui.draw_sprite(sword_cursor_id, mx + math.floor(9 * s), my + math.floor(8 * s), {
            scale = 2.8 * s,
            r = 1.0, g = 1.0, b = 1.0, a = 1.0,
        })
    else
        fill_rect(mx + 2, my + 2, math.floor(4 * s), math.floor(18 * s), 0.04, 0.05, 0.08, 0.85)
        fill_rect(mx + 2, my + 2, math.floor(18 * s), math.floor(4 * s), 0.04, 0.05, 0.08, 0.85)
        fill_rect(mx, my, math.floor(3 * s), math.floor(18 * s), 0.98, 0.88, 0.42, 0.98)
        fill_rect(mx, my, math.floor(18 * s), math.floor(3 * s), 0.98, 0.88, 0.42, 0.98)
    end
end

local function draw_connecting()
    local _, _, px, py, pw, ph, cx, s = draw_panel("EGGNOGG+ ONLINE", "Connecting to server")
    draw_text_center("Opening socket to " .. tostring(config.get("server_host", "127.0.0.1")) .. ":" .. tostring(config.get("server_port", 7878)),
        cx, py + math.floor(132 * s), 0.98 * s, 0.76, 0.82, 0.94)
    if button_box("BACK", px + pw * 0.5 - math.floor(128 * s), py + ph - math.floor(98 * s), math.floor(256 * s), math.floor(56 * s), false) then
        proto.disconnect()
        hub.close()
    end
end

local function draw_login()
    local _, _, px, py, pw, _, _, s = draw_panel("EGGNOGG+ ONLINE", "Sign in to queue for matches")
    local margin = math.floor(52 * s)
    local fx = px + margin
    local fw = pw - margin * 2
    local field_h = math.floor(64 * s)
    local y = py + math.floor(136 * s)

    if select(1, field_box("Username", field_user, fx, y, fw, field_h, focus == 1, false)) then focus = 1 end
    if select(1, field_box("Password", field_pass, fx, y + math.floor(96 * s), fw, field_h, focus == 2, true)) then focus = 2 end

    if button_box("LOG IN", fx, y + math.floor(216 * s), fw, math.floor(62 * s), true) then
        begin_login_request("login")
    end
    if button_box("REGISTER", fx, y + math.floor(290 * s), fw, math.floor(56 * s), false) then
        begin_login_request("register")
    end
end

local function draw_logging_in()
    local _, _, px, py, pw, ph, cx, s = draw_panel("AUTHENTICATING", "Waiting for server")
    draw_text_center("Please wait...", cx, py + math.floor(128 * s), 1.14 * s, 0.78, 0.84, 0.98)
    if button_box("CANCEL", px + pw * 0.5 - math.floor(128 * s), py + ph - math.floor(98 * s), math.floor(256 * s), math.floor(56 * s), false) then
        proto.disconnect()
        set_state(S_LOGIN)
        status("Disconnected.")
    end
end

local function draw_hub_screen()
    local _, _, px, py, pw, _, _, s = draw_panel("ONLINE HUB", username ~= "" and ("Logged in as " .. username) or "Connected")
    local margin = math.floor(52 * s)
    local row_y = py + math.floor(150 * s)
    draw_text("Players in queue", px + margin, row_y, 1.02 * s, 0.68, 0.74, 0.84)
    draw_text(tostring(queue_count), px + pw - margin - math.floor(24 * s), row_y - math.floor(2 * s), 1.30 * s, 0.95, 0.88, 0.42)
    if button_box("FIND MATCH", px + margin, py + math.floor(206 * s), pw - margin * 2, math.floor(64 * s), true) then
        proto.send({ type = "join_queue" })
        set_state(S_QUEUING)
        status("Searching for opponent...")
    end
    if button_box("DISCONNECT", px + margin, py + math.floor(286 * s), pw - margin * 2, math.floor(56 * s), false) then
        proto.disconnect()
        hub.close()
    end
end

local function draw_queueing()
    local _, _, px, py, pw, ph, cx, s = draw_panel("MATCHMAKING", "Looking for another player")
    local dots = string.rep(".", math.floor(mod.game.tick_count() / 20) % 4)
    draw_text_center("Searching" .. dots, cx, py + math.floor(126 * s), 1.16 * s, 0.78, 0.84, 0.98)
    draw_text_center("Players in queue: " .. tostring(queue_count), cx, py + math.floor(174 * s), 1.00 * s, 0.66, 0.74, 0.90)
    if button_box("CANCEL", px + pw * 0.5 - math.floor(128 * s), py + ph - math.floor(98 * s), math.floor(256 * s), math.floor(56 * s), false) then
        proto.send({ type = "leave_queue" })
        set_state(S_HUB)
        status("Left queue.")
    end
end

local function draw_match_found(dt)
    local _, _, px, py, pw, ph, cx, s = draw_panel("MATCH FOUND", "Get ready")
    cd_timer = math.max(0, cd_timer - dt)
    draw_text_center("You are player " .. tostring(my_role + 1), cx, py + math.floor(122 * s), 1.08 * s, 0.66, 0.96, 0.70)
    draw_text_center("Map " .. tostring((my_map_sel or 0) + 1), cx, py + math.floor(170 * s), 1.00 * s, 0.68, 0.76, 0.92)
    if cd_timer > 0 then
        draw_text_center("Starting in " .. tostring(math.ceil(cd_timer)) .. "...", cx, py + math.floor(220 * s), 1.24 * s, 0.98, 0.84, 0.42)
    else
        draw_text_center("Waiting for start...", cx, py + math.floor(220 * s), 1.24 * s, 0.98, 0.84, 0.42)
    end
    if not match_ready_sent then
        proto.send({ type = "ready" })
        match_ready_sent = true
    end
    fill_rect(px + math.floor(52 * s), py + ph - math.floor(84 * s), pw - math.floor(104 * s), math.floor(16 * s), 0.09, 0.12, 0.18, 0.92)
    local progress = 1.0 - math.max(0.0, math.min(1.0, cd_timer / 3.0))
    fill_rect(px + math.floor(52 * s), py + ph - math.floor(84 * s), (pw - math.floor(104 * s)) * progress, math.floor(16 * s), 0.95, 0.83, 0.32, 0.95)
    stroke_rect(px + math.floor(52 * s), py + ph - math.floor(84 * s), pw - math.floor(104 * s), math.floor(16 * s), 1.0, 0.34, 0.42, 0.56, 0.75)
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
                status("Searching for opponent...")
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
