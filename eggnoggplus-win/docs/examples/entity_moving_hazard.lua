-- Map API 22. Pair with entity_moving_hazard.json as entities.json.
-- The placement room must match a room in your map. Entity maps are offline-only.
entity.on_spawn("demo:moving_hazard", function(handle, value)
    map.state["home:" .. handle] = value.x
end)
entity.on_remove("demo:moving_hazard", function(handle)
    map.state["home:" .. handle] = nil
end)
entity.on_update("demo:moving_hazard", function(handle)
    local value = entity.get(handle)
    local home = map.state["home:" .. handle]
    if value.x >= home + 32 then entity.set(handle, {vx = -0.5})
    elseif value.x <= home - 32 then entity.set(handle, {vx = 0.5}) end
    for _, area in ipairs(entity.regions(handle)) do
        if area.role == "hitbox" then
            for _, player in ipairs(map.players()) do
                local x = math.max(area.world_x, math.min(player.x, area.world_x + area.width))
                local y = math.max(area.world_y, math.min(player.y, area.world_y + area.height))
                local dx, dy = player.x - x, player.y - y
                if dx * dx + dy * dy < player.contact_radius * player.contact_radius then
                    map.defeat_player(player.player)
                end
            end
        end
    end
end)
