entity.on_update('demo:orb', function(handle, value)
  if (function() local body = entity.get(handle) local speed = math.max(math.abs(body.vx), math.abs(body.vy)) if speed == 0 then return false end local distance = 9 return map.solid_box(body.x + body.vx / speed * distance, body.y + body.vy / speed * distance, 1, 1) end)() then
    entity.set(handle, {vx = -entity.get(handle).vx})
  end
end)
