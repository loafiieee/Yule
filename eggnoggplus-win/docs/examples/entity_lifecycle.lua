-- Save as map.lua beside a V2 map's data.json/data.map and copy
-- entity_package.json as entities.json. Current managed entities are offline-only
-- logical objects: this example does not yet draw sprites or damage native players.
-- Position and velocity are in pixels and pixels per simulation tick.

map.state.spawned = 0
map.state.removed = 0

entity.on_spawn("demo:orb", function(handle, initial)
    map.state.spawned = map.state.spawned + 1
    entity.set(handle, { vx = 1 })
end)

entity.on_remove("demo:orb", function(handle, final)
    -- handle is already stale. final contains the last x/y/vx/vy values.
    map.state.removed = map.state.removed + 1
    map.state.last_removed_x = final.x
end)

entity.on_update("demo:orb", function(handle)
    if entity.get(handle).x >= 192 then
        entity.remove(handle)
    end
end)

map.on_tick(function()
    if map.tick() % 30 == 0 then
        entity.spawn("demo:orb", { x = 48, y = 64 })
    end
end)
