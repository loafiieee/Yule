entity.on_update('demo:orb', function(handle, value)
  entity.set(handle, {vx = 2})
  entity.set(handle, {animation_paused = true})
  map.state['tick'] = map.tick()
  if map.every(1) then
    for count = 1, 2 do
      map.state['random'] = math.abs(map.random(1, 10))
    end
  end
  entity.remove(handle)
end)
