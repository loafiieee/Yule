local ffi = require("ffi")
local crash_ptr = ffi.cast("volatile int*", 0)

mod.on_load(function()
  mod.log("Loaded! id=" .. mod.id .. " name=" .. mod.name)
end)

mod.on_event(function(e)
  if e.type == "keydown" then
    if e.sym == 56 then
      mod.log("Crashing now!")
      crash_ptr[0] = 1
    end
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
