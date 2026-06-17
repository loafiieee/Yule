-- Tile Demo: custom tile behaviors bound to map glyphs, with spawn-safety and
-- animated overlay "textures".
--
-- Play the "Content Demo" map (a copy of example_map):
--   '!'  spring  -> bounces the player upward on contact (green marker)
--   '_'  kill    -> kills the player on contact (red marker)
--
-- Spawn-safety: kill tiles are made deadly like the engine's spikes (copy the
-- spike tile's property row onto our '_' type), so the engine's respawn logic
-- won't drop a player onto one. Bounce works because tile dispatch runs after
-- the native player update (velocity survives). Local-first: disabled online.

mod.on_load(function()
  -- Spawn-safety + native deadliness: make the '_' kill tile behave like the
  -- spike tile 'X' (deadly, and avoided by the engine's spawn placement).
  if config.get("kill_is_deadly", true) then
    mod.game.world.copy_tile_props("X", "_")
  end

  -- Spring: glyph '!' (engine tile type 3, unused by the base game).
  mod.game.register_tile({
    glyph = "!",
    on_enter = function(p) p:bounce(-config.get("spring_strength", 6.0)) end,  -- -y is up
  })

  -- Kill: glyph '_'. (Native death also fires now that it's spike-like, but this
  -- guarantees it and demonstrates the API.)
  mod.game.register_tile({
    glyph = "_",
    on_enter = function(p) p:kill() end,
  })

  mod.log("Loaded. Play 'Content Demo': '!' springs you up, '_' kills (and is spawn-safe).")
end)

-- ── Animated overlay "textures" for the custom tiles ────────────────────────
-- Tile world->screen projection lives here so it's tunable without a rebuild.
-- If markers look offset/scaled, adjust this function (camera may be view-center
-- vs view-left; ui/game resolutions may differ).
local function project(v, wx, wy)
  local sw = (v.game_w > 0) and (v.ui_w / v.game_w) or 1.0
  local sh = (v.game_h > 0) and (v.ui_h / v.game_h) or 1.0
  local sx = (wx - v.cam_x) * sw + v.ui_w * 0.5
  local sy = (wy - v.cam_y) * sh + v.ui_h * 0.5
  return sx, sy, (v.tile_w or 16) * sw, (v.tile_h or 16) * sh
end

mod.on_frame(function()
  if not config.get("show_markers", true) then return end
  if mod.game.world.online_active() then return end
  local v = mod.game.world.view()
  local phase = (mod.game.tick_count() or 0) * 0.2
  local pulse = 0.45 + 0.35 * math.sin(phase)

  local function draw(glyph, r, g, b)
    for _, t in ipairs(mod.game.world.find_tiles(glyph)) do
      local sx, sy, tw, th = project(v, t.x, t.y)
      mod.ui.rect(sx - tw * 0.5, sy - th * 0.5, tw, th, r, g, b, pulse)
    end
  end
  draw("!", 0.30, 1.00, 0.45)   -- spring: green
  draw("_", 1.00, 0.25, 0.25)   -- kill: red
end)

-- Optional tile-id inspector (off by default).
local last
mod.on_tick(function()
  if not config.get("inspect", false) then return end
  if mod.game.world.online_active() then return end
  local t = mod.game.world.player_tile(0)
  local id = t and t.id or -1
  if id ~= last then
    last = id
    if t then mod.log(("tile under P1 = %d%s"):format(id, t.solid and " (solid)" or "")) end
  end
end)
