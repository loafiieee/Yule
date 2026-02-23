local logged_once = false
local baseline = {}
local stable_main_frames = 0

mod.on_frame(function()
  local state = mod.ui.state_name()
  if state ~= "main" then
    baseline = {}
    stable_main_frames = 0
    return
  end

  -- Let the title->main handoff settle before mutating engine buttons.
  stable_main_frames = stable_main_frames + 1
  if stable_main_frames < 20 then
    return
  end

  local changed = 0

  for nth = 1, 16 do
    local ptr = mod.ui.find_button_by_label("START", nth)
    if not ptr then break end

    local x, y, w, h = mod.ui.button_rect_ptr(ptr)
    if x and w and h and w > 1.0 and h > 1.0 and w < 2000.0 and h < 2000.0 then
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
