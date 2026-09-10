-- Greggnogg object begin custom:launch_pad
entity.on_update("custom:launch_pad", function(handle)
    local launch_speed = 4
    local players = map.players()
    for _, area in ipairs(entity.regions(handle)) do
        if area.role == "sensor" then
            for _, player in ipairs(players) do
                local r = player.contact_radius
                local feet = player.y + r
                if player.vy >= 0 and player.x + r > area.world_x and player.x - r < area.world_x + area.width and feet >= area.world_y and feet < area.world_y + area.height then
                    map.set_player_velocity(player.player, player.vx, -launch_speed)
                    player.vy = -launch_speed
                end
            end
        end
    end
end)
-- Greggnogg object end custom:launch_pad
