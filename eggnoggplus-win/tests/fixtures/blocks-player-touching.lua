entity.on_update('demo:launch_pad', function(handle, value)
  for _, player in ipairs(map.players()) do
    if (function() for _, region in ipairs(entity.regions(handle)) do if region.role == 'sensor' then local dx = math.max(region.world_x - player.x, 0, player.x - region.world_x - region.width) local dy = math.max(region.world_y - player.y, 0, player.y - region.world_y - region.height) if dx * dx + dy * dy <= player.contact_radius * player.contact_radius then return true end end end return false end)() then
      map.set_player_velocity(player.player, 0, -4)
    end
  end
end)
