-- Map API 12: assign this as map.lua in a V2 package binding S to a spring tile.
-- Living players touching S launch only during the one-second active window.
map.sensor('S', {objects={'alive_player'}, contact_scope='binding'})
map.state.spring_active = false
map.on_timer('open', function()
  map.state.spring_active = true
  map.timer_start('close', 60)
end)
map.on_timer('close', function()
  map.state.spring_active = false
  map.timer_start('open', 120)
end)
map.on_enter('S', function(player, tile)
  if map.state.spring_active then player:set_velocity(0, -4) end
end)
map.timer_start('open', 120)
