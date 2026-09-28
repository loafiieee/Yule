-- Map API 32. Pair with entity_moving_hazard.json as entities.json.
-- The placement room must match a room in your map. Entity maps are offline-only.
entity.on_spawn("demo:moving_hazard", function(handle, value)
    entity.set_value(handle, "home_x", value.x)
end)
entity.on_update("demo:moving_hazard", function(handle)
    local value = entity.get(handle)
    local home = entity.value(handle, "home_x")
    if value.x >= home + 32 then entity.set(handle, {vx = -0.5})
    elseif value.x <= home - 32 then entity.set(handle, {vx = 0.5}) end
end)

entity.on_player_contact("demo:moving_hazard", function(handle, contact)
    if contact.role == "hitbox" then
        map.defeat_player(contact.player)
    end
end)
