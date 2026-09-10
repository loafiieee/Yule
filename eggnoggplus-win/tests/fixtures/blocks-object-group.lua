map.on_tick(function()
  for _, handle in ipairs(entity.list()) do
    if entity.exists(handle) and entity.type(handle) == 'demo:orb' then
    entity.set(handle, {vx = 2})
    entity.set(handle, {animation_paused = true})
    end
  end
end)
