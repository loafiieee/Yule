-- START action from ghidra: button_ex(..., "START", 0x432440)
local START_ACTION_PTR = 0x432440

-- Startup-safe one-shot resize per main-menu visit.
-- Handles both initial boot (main_initial) and normal main menu.

local in_main_menu = false
local frames_in_menu = 0
local resized_this_visit = false

local function is_main_like_state(name)
    return name == "main" or name == "main_initial"
end

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

    return mod.ui.button_resize_ptr(ptr, w * 0.5, h * 0.5)
end

mod.on_frame(function()
    local state = mod.ui.state_name()
    local now_in_main = is_main_like_state(state)

    if not now_in_main then
        in_main_menu = false
        frames_in_menu = 0
        resized_this_visit = false
        return
    end
    frames_in_main = frames_in_main + 1
    if frames_in_main < 1 then return end

    if not in_main_menu then
        in_main_menu = true
        frames_in_menu = 0
        resized_this_visit = false
    else
        frames_in_menu = frames_in_menu + 1
    end

    if resized_this_visit then return end

    -- Retry for a startup/state-transition window.
    if frames_in_menu > 240 then return end

    if try_resize_start_once() then
        resized_this_visit = true
        mod.log("START button resized to half size")
    end
end)
