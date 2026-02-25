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

mod.on_frame(function()
    local st = mod.ui.state_name()
    local in_main = in_main_menu_state(st)

    -- Entering main menu: default to VS human.
    if in_main and not was_in_main then
        cfg_set_bool("vs_ai", false)
    end

    was_in_main = in_main

    if not in_main then
        -- Leaving: allow a fresh baseline capture next time.
        baseline_captured = false
        baseline_from_initial = false
        base_x, base_y, base_w, base_h = nil, nil, nil, nil
        return
    end

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
        dispatch_start(last_start_ptr)
    end
end)
