mod.on_load(function()
  mod.log("Loaded! id=" .. mod.id .. " name=" .. mod.name)

  mod.log("enabled=" .. tostring(config.get("enabled", false)))
  mod.log("speed=" .. tostring(config.get("speed", 1.0)))
end)

mod.on_event(function(e)
  -- simple speedhack, if the mod is enabled, multiply the game's delta time by the configured speed multiplier
  if e.type == "delta_time" then
    if not config.get("enabled", false) then
      return false
    end
    local speed = config.get("speed", 1.0)
    e.value = e.value * speed
    -- mod.log("delta_time event, speed=" .. speed .. " new_value=" .. e.value)
  end

  -- Return true to consume the event (prevent the game from seeing it)
  return false
end)

mod.on_frame(function()
  -- This runs every frame, put code here that needs to run continuously
end)

mod.on_unload(function()
  mod.log("Unloaded")
end)
