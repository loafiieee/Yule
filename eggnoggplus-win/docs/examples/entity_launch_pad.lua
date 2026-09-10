-- Map API 19. Pair with entity_launch_pad.json as entities.json in a V2 map.
-- Position the launch_pad placement beneath a player in generated-world pixels.
-- This is an explicit sensor response, not solid terrain or player damage.
entity.on_update("demo:launch_pad", function(handle)
    local regions = entity.regions(handle)
    local players = map.players()
    for _, region in ipairs(regions) do
        if region.role == "sensor" then
            for _, player in ipairs(players) do
                local radius = player.contact_radius
                local feet = player.y + radius
                if player.vy >= 0 and
                   player.x + radius > region.world_x and
                   player.x - radius < region.world_x + region.width and
                   feet >= region.world_y and feet < region.world_y + region.height then
                    map.set_player_velocity(player.player, player.vx, -4)
                    player.vy = -4 -- Avoid another region queuing the same response.
                end
            end
        end
    end
end)
