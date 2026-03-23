-- Eggnogg+ Mod Template
-- Copy this folder to mods/<your_mod_id>/ and edit mod.json.

mod.on_load(function()
  mod.log("Hello from " .. mod.id .. "! Framework API=" .. tostring(mod.framework_api))

  -- Read config values (in-game editable via Options → Mods)
  local enabled = config.get("enabled", true)
  local name = config.get("player_name", "Player")
  mod.log("enabled=" .. tostring(enabled) .. ", player_name=" .. tostring(name))

  -- Persistent storage (saved to storage.cfg by default)
  local launches = storage.get("launches", 0)
  storage.set("launches", launches + 1)
  mod.log("launches=" .. tostring(storage.get("launches", 0)))

  -- Optional schema migration helper
  if storage.schema() < 1 then
    storage.migrate(1, function()
      storage.set("launches", storage.get("launches", 0))
      return true
    end)
  end

  -- Optional interop service publish
  -- mod.interop.provide(mod.id .. ":api", "1.0.0", {
  --   ping = function() return "pong" end
  -- })

  -- Register an action button handler
  config.on_action("action", function()
    mod.log("action pressed")
  end)

  -- You can load extra lua files relative to your mod folder:
  -- mod.dofile("lib/util.lua")

  -- Texture-pack examples:
  -- mod.texture.register_spritesheet("assets/sprites.png")
  -- mod.texture.register_tilesheet("assets/tiles.png")
  -- mod.texture.register_miscsheet("assets/misc.png")
  -- mod.texture.register_glowsheet("assets/glow.png")
  -- Generic form:
  -- mod.texture.register("data/tiles.png", "assets/tiles.png")
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
    mod.log("keydown sym=" .. tostring(e.sym) .. "as 0x" .. string.format("%x", e.sym)  .. " scancode=" .. tostring(e.scancode))
  end

  -- return true
end)

mod.on_unload(function()
  -- Called when the mod is disabled, hot reloaded, or the framework shuts down.
end)
