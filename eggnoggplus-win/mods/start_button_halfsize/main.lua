local did_apply = false
local original_w = nil
local original_h = nil

mod.on_frame(function()
  if not mod.ui.is_state("main") then
    did_apply = false
    return
  end

  local start_ptr = mod.ui.find_button_by_label("START")
  if not start_ptr then
    return
  end

  if did_apply then
    return
  end

  local x, y, w, h = mod.ui.button_rect_ptr(start_ptr)
  if not x then
    return
  end

  if not original_w then
    original_w = w
    original_h = h
    mod.log(string.format("START original size: %.2f x %.2f", w, h))
  end

  local ok = mod.ui.button_resize_ptr(start_ptr, w * 0.5, h * 0.5, 2.0)
  if ok then
    did_apply = true
    mod.log(string.format("START resized to: %.2f x %.2f", w * 0.5, h * 0.5))
  end
end)
