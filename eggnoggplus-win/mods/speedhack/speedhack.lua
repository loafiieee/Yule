mod.on_load(function()
  mod.log("Loaded! id=" .. mod.id .. " name=" .. mod.name)

  mod.log("speed=" .. tostring(config.get("speed", 1.0)))
end)

mod.on_event(function(e)
  if e.type == "delta_time" then
    local speed = config.get("speed", 1.0)
    e.value = e.value * speed
  end

  return false
end)

mod.on_frame(function()
end)

mod.on_unload(function()
  mod.log("Unloaded")
end)
