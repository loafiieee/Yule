local START_ACTION_PTR = 0x432440

-- Two-buttons-on-main-menu scaffold:
--   VS \x9E          -> sets config.vs_ai = false and starts
--   VS <alloc glyph> -> sets config.vs_ai = true and starts

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

-- We must capture the *original* START button rect only once per main-menu visit.
-- If we recapture after we resize it, we'd keep halving it every frame.
local baseline_captured = false
local baseline_from_initial = false
local base_x, base_y, base_w, base_h = nil, nil, nil, nil

local function capture_start_button_ptr_and_baseline(state_name)
    local ptr = mod.ui.find_button_by_action_ptr(START_ACTION_PTR)
    if not ptr then return nil end

    -- Always keep the pointer so we can invoke the START action even if we
    -- later hide/shrink the vanilla button.
    last_start_ptr = ptr

    -- Capture baseline once (before we touch size/pos).
    -- If we first see the button in "main_initial", capture a provisional baseline,
    -- then refresh it once when we reach "main" (the fully-laid-out state).
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

local function set_vs_ai(enabled)
    if config and config.set then
        config.set("vs_ai", enabled and true or false)
    end
end

local function invoke_start(ptr)
    if not (ptr and mod.ui.button_invoke_ptr) then return end

    -- Different buttons in Eggnogg+ use different "event codes" for activation.
    -- Rather than hard-coding one, try a small set until the state changes.
    -- This makes the mod robust across minor game updates.
    local before = mod.ui.state_name()
    local codes = { 1, 2, 3, 4, 0 }
    for _, code in ipairs(codes) do
        local ret, err = mod.ui.button_invoke_ptr(ptr, code)
        if ret == nil then
            mod.warn("button_invoke_ptr failed (code=" .. tostring(code) .. "): " .. tostring(err))
        end
        local after = mod.ui.state_name()
        if after ~= before and after ~= "main" and after ~= "main_initial" then
            return
        end
    end
end

mod.on_frame(function()
    local st = mod.ui.state_name()
    if st ~= "main" and st ~= "main_initial" then
        -- Leaving the menu: allow a fresh baseline capture next time we return.
        baseline_captured = false
        baseline_from_initial = false
        base_x, base_y, base_w, base_h = nil, nil, nil, nil
        return
    end

    local start_ptr = capture_start_button_ptr_and_baseline(st)
    if not start_ptr then return end

    -- Keep the vanilla START button alive so its action works.
    -- We repurpose it as "VS <human>" (label + size/pos).
    if mod.ui.button_set_label_ptr then
        mod.ui.button_set_label_ptr(start_ptr, LABEL_HUMAN)
    end

    -- Create the AI button (engine-backed so selector can navigate to it).
    local clicked_ai = mod.ui.native_button("vs_ai", LABEL_AI, 1.0, 4.5, 3.0, 6.0)

    -- If we know where START was, position our two buttons to fit in the same slot.
    if base_w and base_h and base_x and base_y then
        local gap = GAP
        if gap < 0.0 then gap = 0.0 end

        local w_each = (base_w - gap) * 0.5
        if w_each < 40.0 then
            -- Too tight; fall back to equal split without a gap.
            gap = 0.0
            w_each = base_w * 0.5
        end

        local left_edge = base_x - (base_w * 0.5)
        local human_x = left_edge + (w_each * 0.5)
        local ai_x    = human_x + w_each + gap

        -- Resize/move the vanilla start button into the left half.
        if mod.ui.button_resize_ptr then
            mod.ui.button_resize_ptr(start_ptr, w_each, base_h)
        end
        if mod.ui.button_set_pos_ptr then
            mod.ui.button_set_pos_ptr(start_ptr, human_x, base_y)
        end

        -- Position the AI button into the right half.
        mod.ui.native_resize("vs_ai", w_each, base_h)
        mod.ui.native_set_pos("vs_ai", ai_x, base_y)
    end

    -- Human: default off. Set every frame we're on the main menu so it stays consistent.
    set_vs_ai(false)

    if clicked_ai then
        set_vs_ai(true)
        invoke_start(last_start_ptr)
    end
end)
