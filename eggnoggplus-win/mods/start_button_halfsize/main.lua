-- From ghidra: main_layout calls button_ex(..., "START", 0x432440)
local START_ACTION_PTR = 0x432440

local baseline = {}
local was_main = false
local frames_in_main = 0

local function clear_baseline()
    baseline = {}
end

mod.on_frame(function()
    -- Vanilla menu button mutation is most reliable against exact state name.
    local is_main = (mod.ui.state_name() == "main")
    if not is_main then
        was_main = false
        frames_in_main = 0
        clear_baseline()
        return
    end

    -- Give the menu one frame to finish building buttons after state transitions.
    if not was_main then
        was_main = true
        frames_in_main = 0
        clear_baseline()
        return
    end
    frames_in_main = frames_in_main + 1
    if frames_in_main < 1 then return end

    for nth = 1, 8 do
        local ptr = mod.ui.find_button_by_action_ptr(START_ACTION_PTR, nth)
        if not ptr then break end

        local x, y, w, h = mod.ui.button_rect_ptr(ptr)
        if x and y and w and h and w > 1.0 and h > 1.0 then
            local b = baseline[nth]
            if (not b) or b.ptr ~= ptr then
                b = { ptr = ptr, w = w, h = h }
                baseline[nth] = b
            end

            -- Re-apply every frame while in main in case the game rebuilds menu buttons.
            mod.ui.button_resize_ptr(ptr, b.w * 0.5, b.h * 0.5)
        end
    end
end)
