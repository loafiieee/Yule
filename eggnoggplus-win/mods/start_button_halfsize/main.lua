-- START action from ghidra: button_ex(..., "START", 0x432440)
local START_ACTION_PTR = 0x432440

-- Rewritten from scratch:
-- - no cached button pointers
-- - only resize once per main-menu entry
-- - retry for a short window so startup/layout timing still works

local was_main = false
local frames_in_main = 0
local resized_this_entry = false

local function find_start_button_ptr()
    -- Prefer action pointer (most stable), then label fallback.
    local p = mod.ui.find_button_by_action_ptr(START_ACTION_PTR)
    if p then return p end
    return mod.ui.find_button_by_label("START")
end

local function try_resize_start_once()
    local ptr = find_start_button_ptr()
    if not ptr then return false end

    local x, y, w, h = mod.ui.button_rect_ptr(ptr)
    if not (x and y and w and h) then return false end
    if w <= 1.0 or h <= 1.0 then return false end

    -- Compute target from current size and apply once for this menu entry.
    return mod.ui.button_resize_ptr(ptr, w * 0.5, h * 0.5)
end

mod.on_frame(function()
    local in_main = (mod.ui.state_name() == "main")

    if not in_main then
        was_main = false
        frames_in_main = 0
        resized_this_entry = false
        return
    end

    if not was_main then
        -- Fresh main-menu entry.
        was_main = true
        frames_in_main = 0
        resized_this_entry = false
    else
        frames_in_main = frames_in_main + 1
    end

    if resized_this_entry then return end

    -- Give layout a brief moment to settle, then retry for a while.
    if frames_in_main < 1 then return end
    if frames_in_main > 120 then return end

    if try_resize_start_once() then
        resized_this_entry = true
        mod.log("START button resized to half size")
    end
end)
