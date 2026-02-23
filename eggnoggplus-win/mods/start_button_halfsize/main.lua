local logged_once = false
local baseline = {}

mod.on_frame(function()
  if not mod.ui.is_state("main") then
    baseline = {}
    return
  end

  -- There can be multiple engine buttons with the same label in menu lists.
  -- Resize every active START entry so the visible one is always affected.
  local changed = 0

  for nth = 1, 16 do
    local ptr = mod.ui.find_button_by_label("START", nth)
    if not ptr then
      break
    end

    local x, y, w, h = mod.ui.button_rect_ptr(ptr)
    if x and w and h and w > 1.0 and h > 1.0 then
      local slot = baseline[nth]
      if (not slot) or slot.ptr ~= ptr then
        slot = { ptr = ptr, w = w, h = h }
        baseline[nth] = slot
      end

      if mod.ui.button_resize_ptr(ptr, slot.w * 0.5, slot.h * 0.5, 2.0) then
        changed = changed + 1
      end
    end
  end

  if changed > 0 and not logged_once then
    logged_once = true
    mod.log("Resized visible START button candidates to half size")
  end
end)
