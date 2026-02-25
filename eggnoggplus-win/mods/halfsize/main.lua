local START_ACTION_PTR = 0x432440

-- Two-buttons-on-main-menu scaffold:
--   Left:  VS <human glyph>  -> sets config.vs_ai = false, then normal START
--   Right: VS <ai glyph>     -> sets config.vs_ai = true, then normal START

-- You said you like: "VS " .. string.char(0x9E)
local HUMAN_GLYPH = 0x9E

-- AI icon comes from an 8x8 PNG via mod.font (falls back to a built-in glyph).
local AI_GLYPH = 0x86
if mod.font and mod.font.alloc_glyph then
    local b, err = mod.font.alloc_glyph("icons/ai_8x8.png")
    if b then
        AI_GLYPH = b
    else
        mod.warn("alloc_glyph failed (icons/ai_8x8.png): " .. tostring(err))
    end
end

local LABEL_HUMAN = "VS " .. string.char(HUMAN_GLYPH)
local LABEL_AI    = "VS " .. string.char(AI_GLYPH) 

-- Layout tuning
local GAP = 10.0 -- pixels between the two buttons

-- main_player_poll_cmds command bits (from ghidra)
local CMD_START   = 0x01
local CMD_JUMP    = 0x02
local CMD_LEFT_A  = 0x04
local CMD_RIGHT_A = 0x08
local CMD_LEFT_B  = 0x10
local CMD_RIGHT_B = 0x20
local CMD_ATTACK  = 0x40

local bitlib = bit32 or bit

local function bor(a, b)
    if bitlib and bitlib.bor then
        return bitlib.bor(a, b)
    end
    local res, bitv = 0, 1
    while a > 0 or b > 0 do
        local abit = a % 2
        local bbit = b % 2
        if abit ~= 0 or bbit ~= 0 then
            res = res + bitv
        end
        a = math.floor(a / 2)
        b = math.floor(b / 2)
        bitv = bitv * 2
    end
    return res
end

local function band(a, b)
    if bitlib and bitlib.band then
        return bitlib.band(a, b)
    end
    local res, bitv = 0, 1
    while a > 0 and b > 0 do
        local abit = a % 2
        local bbit = b % 2
        if abit ~= 0 and bbit ~= 0 then
            res = res + bitv
        end
        a = math.floor(a / 2)
        b = math.floor(b / 2)
        bitv = bitv * 2
    end
    return res
end

local last_start_ptr = nil

-- Baseline START rect capture
local baseline_captured = false
local baseline_from_initial = false
local base_x, base_y, base_w, base_h = nil, nil, nil, nil

local function in_main_menu_state(st)
    return st == "main" or st == "main_initial"
end

local function cfg_set_bool(key, val)
    if not (config and config.get and config.set) then return end
    local cur = config.get(key, false)
    local want = val and true or false
    local have = cur and true or false
    if have ~= want then
        config.set(key, want)
    end
end

local function capture_start_ptr_and_baseline(state_name)
    local ptr = mod.ui.find_button_by_action_ptr(START_ACTION_PTR)
    if not ptr then return nil end

    last_start_ptr = ptr

    -- Capture baseline once (before we touch size/pos).
    -- If we first see the button in "main_initial", capture a provisional baseline,
    -- then refresh it once when we reach "main" (fully laid out).
    local want_capture = (not baseline_captured) or (baseline_from_initial and state_name == "main")
    if want_capture then
        local x, y, w, h = mod.ui.button_rect_ptr(ptr)
        if (x and y and w and h and w > 20.0 and h > 10.0) then
            base_x, base_y, base_w, base_h = x, y, w, h
            baseline_captured = true
            baseline_from_initial = (state_name == "main_initial")
            if state_name == "main" then
                baseline_from_initial = false
            end
        end
    end

    return ptr
end

local function dispatch_start(ptr)
    if not ptr then return end

    local fn = mod.ui.button_invoke_ptr or mod.ui.button_activate_ptr
    if not fn then return end

    local before = mod.ui.state_name()
    local codes = { 1, 3, 0, 2, 4 }

    for _, code in ipairs(codes) do
        local ret, err = fn(ptr, code)
        if ret == nil then
            mod.warn("start dispatch failed (code=" .. tostring(code) .. "): " .. tostring(err))
        end
        local after = mod.ui.state_name()
        if after ~= before then
            return
        end
    end
end

-- Ensure clicking the *left* (vanilla) button disables vs_ai BEFORE the game handles the click.
mod.on_event(function(e)
    if e.type ~= "mousebuttondown" or e.button ~= 1 then
        return false
    end

    local st = mod.ui.state_name()
    if not in_main_menu_state(st) then
        return false
    end

    local ptr = mod.ui.find_button_by_action_ptr(START_ACTION_PTR)
    if not ptr then
        return false
    end

    local x, y, w, h = mod.ui.button_rect_ptr(ptr)
    if not (x and y and w and h) then
        return false
    end

    -- Rect is center-based
    if math.abs(e.x - x) <= (w * 0.5) and math.abs(e.y - y) <= (h * 0.5) then
        cfg_set_bool("vs_ai", false)
    end

    return false
end)

local was_in_main = false
local frame_no = 0

local last_menu_input_frame = { -1, -1 }
local last_menu_start_frame = { -1, -1 }

local ai_human_player = nil
local ai_player = nil
local ai_logged = false

local function clear_ai_inputs()
    if mod.game and mod.game.input_clear then
        mod.game.input_clear(0)
        mod.game.input_clear(1)
    end
end

local function pick_human_selector_player()
    -- Prefer whichever selector pressed START most recently (the one that clicked).
    if last_menu_start_frame[1] ~= last_menu_start_frame[2] then
        return (last_menu_start_frame[1] > last_menu_start_frame[2]) and 0 or 1
    end

    -- Fallback to any recent selector movement.
    if last_menu_input_frame[1] ~= last_menu_input_frame[2] then
        return (last_menu_input_frame[1] > last_menu_input_frame[2]) and 0 or 1
    end

    -- Deterministic final fallback.
    return 0
end

local function cmd_left()
    return bor(CMD_LEFT_A, CMD_LEFT_B)
end

local function cmd_right()
    return bor(CMD_RIGHT_A, CMD_RIGHT_B)
end

local function build_ai_mask_for_player(player_index)
    if not (mod.game and mod.game.snapshot) then
        return 0
    end

    local s = mod.game.snapshot(player_index, false)
    if not s or not s.in_game then
        return 0
    end

    local enemy_dx = s.enemy_dx or 0.0
    local enemy_dy = s.enemy_dy or 0.0
    local sword_dx = s.nearest_sword_dx
    local sword_dy = s.nearest_sword_dy

    local mask = 0

    local want_sword = not s.player_has_sword and sword_dx ~= nil and sword_dy ~= nil
    local target_dx = want_sword and sword_dx or enemy_dx
    local target_dy = want_sword and sword_dy or enemy_dy

    local abs_dx = math.abs(target_dx)
    local abs_dy = math.abs(target_dy)

    -- Horizontal tracking.
    if abs_dx > 8.0 then
        if target_dx < 0.0 then mask = bor(mask, cmd_left()) end
        if target_dx > 0.0 then mask = bor(mask, cmd_right()) end
    end

    -- Jump if target is above us (enemy or desired sword), or if we are very close in X.
    if target_dy < -14.0 or (abs_dx < 24.0 and abs_dy > 20.0 and target_dy < 0.0) then
        mask = bor(mask, CMD_JUMP)
    end

    -- Attack when close to enemy.
    local eabsx = math.abs(enemy_dx)
    local eabsy = math.abs(enemy_dy)
    if eabsx < 32.0 and eabsy < 24.0 then
        mask = bor(mask, CMD_ATTACK)
    end

    return mask
end

mod.on_frame(function()
    frame_no = frame_no + 1

    local st = mod.ui.state_name()
    local in_main = in_main_menu_state(st)

    -- Track which selector (player 0 or 1) is actively navigating menus.
    if in_main and mod.game and mod.game.poll_cmds then
        for p = 0, 1 do
            local cmd = mod.game.poll_cmds(p, 1) or 0
            if cmd ~= 0 then
                last_menu_input_frame[p + 1] = frame_no
            end
            if band(cmd, CMD_START) ~= 0 then
                last_menu_start_frame[p + 1] = frame_no
            end
        end
    end

    -- Entering main menu: default to VS human.
    if in_main and not was_in_main then
        cfg_set_bool("vs_ai", false)
        ai_human_player = nil
        ai_player = nil
        ai_logged = false
        clear_ai_inputs()
    end

    was_in_main = in_main

    if not in_main then
        -- Leaving: allow a fresh baseline capture next time.
        baseline_captured = false
        baseline_from_initial = false
        base_x, base_y, base_w, base_h = nil, nil, nil, nil
    else
        local start_ptr = capture_start_ptr_and_baseline(st)
        if not start_ptr then return end

        -- Repurpose the vanilla START button as the left (VS human) button.
        if mod.ui.button_set_label_ptr then
            mod.ui.button_set_label_ptr(start_ptr, LABEL_HUMAN)
        end

        -- Create the right (VS AI) button.
        local clicked_ai = mod.ui.native_button("vs_ai", LABEL_AI, 1.0, 4.5, 3.0, 6.0)

        -- Position both buttons into the original START slot.
        if base_w and base_h and base_x and base_y then
            local gap = GAP
            if gap < 0.0 then gap = 0.0 end

            local w_each = (base_w - gap) * 0.5
            if w_each < 40.0 then
                gap = 0.0
                w_each = base_w * 0.5
            end

            local left_edge = base_x - (base_w * 0.5)
            local human_x = left_edge + (w_each * 0.5)
            local ai_x    = human_x + w_each + gap

            if mod.ui.button_resize_ptr then
                mod.ui.button_resize_ptr(start_ptr, w_each, base_h)
            end
            if mod.ui.button_set_pos_ptr then
                mod.ui.button_set_pos_ptr(start_ptr, human_x, base_y)
            end

            mod.ui.native_resize("vs_ai", w_each, base_h)
            mod.ui.native_set_pos("vs_ai", ai_x, base_y)
        end

        if clicked_ai then
            cfg_set_bool("vs_ai", true)
            ai_human_player = pick_human_selector_player()
            ai_player = (ai_human_player == 0) and 1 or 0
            ai_logged = false
            dispatch_start(last_start_ptr)
        end

        -- While in menus we never force input overrides.
        clear_ai_inputs()
        return
    end

    -- Gameplay AI: only active when VS AI is selected.
    local enabled = config and config.get and config.get("vs_ai", false)
    if not enabled then
        ai_human_player = nil
        ai_player = nil
        ai_logged = false
        clear_ai_inputs()
        return
    end

    if ai_human_player == nil or ai_player == nil then
        ai_human_player = pick_human_selector_player()
        ai_player = (ai_human_player == 0) and 1 or 0
        ai_logged = false
    end

    local s = (mod.game and mod.game.snapshot) and mod.game.snapshot(ai_player, false) or nil
    if not (s and s.in_game) then
        clear_ai_inputs()
        return
    end

    local mask = build_ai_mask_for_player(ai_player)
    if mod.game and mod.game.input_override then
        mod.game.input_override(ai_player, mask, 1, true)
        mod.game.input_clear(ai_human_player)
    end

    if not ai_logged then
        mod.log(("VS AI active: human=p%d ai=p%d"):format(ai_human_player + 1, ai_player + 1))
        ai_logged = true
    end
end)
