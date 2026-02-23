-- Eggnogg+ Mod Template
-- Copy this folder to mods/<your_mod_id>/ and edit mod.json.

mod.on_load(function()
  mod.log("Hello from " .. mod.id .. "! Framework API=" .. tostring(mod.framework_api))

  -- Read config values (in-game editable via Options → Mods)
  local enabled = config.get("enabled", true)
  local name = config.get("player_name", "Player")
  mod.log("enabled=" .. tostring(enabled) .. ", player_name=" .. tostring(name))

  -- Register an action button handler
  config.on_action("action", function()
    mod.log("action pressed")
  end)

  -- You can load extra lua files relative to your mod folder:
  -- mod.dofile("lib/util.lua")
end)

mod.on_frame(function()
  -- Called once per frame (hooked from SDL_GL_SwapWindow)
  -- Example: draw a tiny overlay only on the main menu.
end)

mod.on_event(function(e)
  -- e = { type, sym, scancode, mod, x, y, button }
  -- Return true to CONSUME the SDL event (the game won't receive it)

  -- Example: print key presses
  if e.type == "keydown" then
    -- e.sym is the SDL keycode
    mod.log("keydown sym=" .. tostring(e.sym))
  end

  -- return true
end)

mod.on_unload(function()
  -- Called when the mod framework is shutting down.
  -- (Hot reload / disable will call this too once those features exist.)
end)
