map.on_tick(function()
  for _, player in ipairs(map.players()) do
    map.set_player_velocity(player.player, player.vx, 0)
  end
end)
