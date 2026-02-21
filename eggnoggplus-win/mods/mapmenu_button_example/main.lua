mod.on_load(function()
  mod.log("Map Menu Button Example loaded")
  mod.log("Open map select (pregame) to see the test button")
end)

mod.on_frame(function()
  if not mod.ui.is_state("pregame") then return end

  -- Native engine button (selector-hover style).
  -- Args: id, label, grid_x, grid_y, layout_across, layout_down
  if mod.ui.native_button("map_select_log", "TEST", 4.0, 0.2, 5.0, 5.0) then
    mod.log("Map select button pressed")
  end
end)

mod.on_unload(function()
  mod.log("Map Menu Button Example unloaded")
end)
