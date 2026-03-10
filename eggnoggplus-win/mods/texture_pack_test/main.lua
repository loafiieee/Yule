local did_register_any = false
local last_clock = 0

local function register_target(label, register_fn, rel_path, loaded_fn)
  local ok, err = register_fn(rel_path)
  if not ok then
    mod.error(label .. " failed: " .. tostring(err))
    return
  end

  did_register_any = true
  mod.log("Registered " .. label .. " from " .. rel_path)

  if loaded_fn and loaded_fn() then
    mod.warn(label .. " was already loaded this session; restart once to apply.")
  end
end

local function test_manual_reload()
  if not (mod.texture and mod.texture.reload_all) then
    mod.warn("mod.texture.reload_all is unavailable in this build.")
    return
  end

  local ok, info = mod.texture.reload_all()
  info = info or {}
  mod.log(
    string.format(
      "reload_all ok=%s tex=%d fail=%d restart=%d font=%d fail=%d restart=%d",
      tostring(ok),
      tonumber(info.textures_reloaded) or 0,
      tonumber(info.textures_failed) or 0,
      tonumber(info.textures_restart_required) or 0,
      tonumber(info.fonts_reloaded) or 0,
      tonumber(info.fonts_failed) or 0,
      tonumber(info.fonts_restart_required) or 0
    )
  )
end

mod.on_load(function()
  mod.log("Texture Pack Test loading...")

  if not (mod.texture and mod.texture.register_spritesheet and mod.texture.register_tilesheet and mod.texture.register_miscsheet and mod.texture.register_glowsheet) then
    mod.error("mod.texture API not available. Rebuild/update SDL2.dll with texture_ext changes.")
    return
  end

  register_target("spritesheet", mod.texture.register_spritesheet, "assets/sprites_test.png", function()
    return mod.texture.sprites_loaded and mod.texture.sprites_loaded()
  end)

  register_target("tilesheet", mod.texture.register_tilesheet, "assets/tiles_test.png", function()
    return mod.texture.loaded and mod.texture.loaded("data/tiles.png")
  end)

  register_target("miscsheet", mod.texture.register_miscsheet, "assets/misc_test.png", function()
    return mod.texture.loaded and mod.texture.loaded("data/misc.png")
  end)

  register_target("glowsheet", mod.texture.register_glowsheet, "assets/glow_test.png", function()
    return mod.texture.loaded and mod.texture.loaded("data/glow.png")
  end)

  mod.log("Press R in-game to run mod.texture.reload_all().")
end)

mod.on_frame(function()

  local now = os.clock()
  local dt = now - last_clock
  if dt < 0 then dt = 0 end
  if dt > 0.25 then dt = 0.25 end
  last_clock = now
end)

mod.on_event(function(e)
  if not e or e.type ~= "keydown" then
    return
  end

  -- SDLK_r is 114 in SDL keycode space.
  if e.sym == 114 then
    test_manual_reload()
  end
end)

mod.on_unload(function()
  if did_register_any then
    mod.log("Texture Pack Test unloaded.")
  end
end)
