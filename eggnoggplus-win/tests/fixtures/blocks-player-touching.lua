entity.on_update('demo:launch_pad', function(handle, value)
  for _, player in ipairs(map.players()) do
    if (function() for _, contact in ipairs(entity.player_contacts(handle, 'sensor')) do if contact.player == player.player then return true end end return false end)() then
      map.set_player_velocity(player.player, 0, -4)
    end
  end
end)
