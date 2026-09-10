map.on_tick(function()
  for _, player in ipairs(map.players()) do
    map.state['v:p:' .. player.player .. ':charges'] = (map.state['v:p:' .. player.player .. ':charges'] or 0) + (player.player)
    map.set_player_velocity(player.player, 0, (map.state['v:p:' .. player.player .. ':charges'] == nil and 0 or map.state['v:p:' .. player.player .. ':charges']))
  end
end)
