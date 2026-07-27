-- Optional map-local behavior. JSON owns visuals and native collision; this
-- deterministic sandbox owns only the special behavior that the map needs.

-- The spring listens through the first six pixels of its cell. The matching
-- lower-body probe extends six pixels sideways/down from each native center:
-- grounded players reach it, while players above the pad enter only when that
-- lower edge actually touches. The explicit binding scope makes a row of
-- adjacent spring cells behave like one continuous pad.
map.sensor(">", {
  tile_box = { left = 0, top = 0, right = 1, bottom = 0.375 },
  object_box = { left = -0.375, top = 0, right = 0.375, bottom = 0.375 },
  objects = { "alive_player", "sword", "hazard" },
  contact_scope = "binding",
})

-- Fans act throughout their visible cell and use the supported object's
-- radius-sized body sensor.
map.sensor("}", {
  tile_box = { left = 0, top = 0, right = 1, bottom = 1 },
  object_box = "body",
  objects = { "player", "sword" },
})

function spring_launch_vy(object)
  -- Living players use 0.15 gravity, while native things such as swords and
  -- the K hazard use 0.075. These targets therefore produce roughly the same
  -- 53-pixel rise instead of throwing the lighter native things skyward.
  -- Dead bodies are deliberately excluded so a spring cannot keep the native
  -- respawn gate airborne forever.
  if object.kind == "player" then
    return -4.0
  end
  return -2.8
end

map.on_enter(">", function(object, tile)
  local target_vy = spring_launch_vy(object)
  object.vy = target_vy
  -- An airborne unarmed kick is applied by native code on the following tick,
  -- after the player may already have left the shallow sensor. Keep only the
  -- upward side of this launch capped for eight rollback-owned ticks.
  object:set_velocity_limits({ min_vy = target_vy }, 8)
  -- Cell 129 is the extended spring. Move only this temporary destination
  -- sprite up so its three-pixel base aligns with the shifted idle artwork;
  -- collision, the sensor, and the native terrain underlay stay in place.
  tile:set_sprite(129, 16, { offset_y = 8 })
  map.state.spring_hits = (map.state.spring_hits or 0) + 1
end)

map.on_contact("}", function(object, tile)
  local push = tile.mirrored and -0.25 or 0.25
  local next_vx = object.vx + push
  if next_vx > 3.0 then
    next_vx = 3.0
  elseif next_vx < -3.0 then
    next_vx = -3.0
  end
  object.vx = next_vx
end)
