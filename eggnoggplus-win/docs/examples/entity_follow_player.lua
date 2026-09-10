-- Use with entity_visual_package.json as entities.json in a V2 map.
-- Demonstrates visible player tracking, not collision response or player damage.
-- On the first gameplay tick it moves the orb near the first live player.
map.state.has_target = false
map.on_tick(function()
    local players = map.players()
    map.state.has_target = #players > 0
    if #players > 0 then
        map.state.target_x = players[1].x
        map.state.target_y = players[1].y
        if map.tick() == 0 then
            local orb = entity.find("first_orb")
            if orb then
                entity.set(orb, { x = players[1].x - 64, y = players[1].y - 32 })
            end
        end
    end
end)
entity.on_update("demo:orb", function(handle)
    if not map.state.has_target then
        entity.set(handle, { vx = 0, vy = 0 })
        return
    end
    local value = entity.get(handle)
    entity.set(handle, {
        vx = math.max(-1, math.min(1, map.state.target_x - value.x)),
        vy = math.max(-1, math.min(1, map.state.target_y - value.y))
    })
end)
