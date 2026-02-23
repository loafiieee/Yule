local logged = false

mod.on_frame(function()
  if not mod.ui.is_state("main") then
    return
  end

  local start_ptr = mod.ui.find_button_by_label("START")
  if not start_ptr then
    return
  end

  local x, y, w, h = mod.ui.button_rect_ptr(start_ptr)
  if not x then
    return
  end

  -- The main menu can rebuild buttons; re-apply size every frame while in main.
  local target_w = w * 0.5
  local target_h = h * 0.5
  local ok = mod.ui.button_resize_ptr(start_ptr, target_w, target_h, 2.0)

  if ok and not logged then
    logged = true
    mod.log(string.format("START size forced to half: %.2f x %.2f (from %.2f x %.2f)", target_w, target_h, w, h))
  end
end)
