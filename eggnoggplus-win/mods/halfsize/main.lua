local START_ACTION_PTR = 0x432440

mod.on_layout("main", function()
    local ptr = mod.ui.find_button_by_action_ptr(START_ACTION_PTR)
    if not ptr then
        mod.warn("START button not found after main layout")
        return
    end

    local x, y, w, h = mod.ui.button_rect_ptr(ptr)
    if not (w and h and w > 1.0 and h > 1.0) then
        mod.warn("START button has unexpected dimensions")
        return
    end

    mod.ui.button_resize_ptr(ptr, w * 0.5, h * 1.0)
    --move button to the left so it doesn't look weird
    mod.log("START button resized to half size")
end)