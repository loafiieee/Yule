
entity.on_update("demo:orb", function(handle, value)
local motion = entity.get(handle)
entity.set(handle, {vx = motion.vx * 1, vy = math.max(-6, math.min(6, motion.vy + 0.25))})
local body = entity.get(handle)
if math.abs(body.vx) > 64 or math.abs(body.vy) > 64 then error('Terrain collision speed exceeds 64 pixels per tick') end
local function sweep(x, y, speed, horizontal)
  local steps = math.ceil(math.abs(speed))
  if steps == 0 then return 0 end
  local step = speed / steps
  local moved = 0
  for i = 1, steps do
    local function blocked(distance)
      local cx, cy = x + (horizontal and distance or 0), y + (horizontal and 0 or distance)
      return map.solid_box(cx, cy, 16, 16) or entity.solid_box(cx, cy, 16, 16, handle)
    end
    if blocked(moved + step) then
      local low, high = 0, 1
      for j = 1, 8 do
        local middle = (low + high) / 2
        if blocked(moved + step * middle) then high = middle else low = middle end
      end
      return moved + step * low
    end
    moved = moved + step
  end
  return moved
end
local dx = sweep(body.x, body.y, body.vx, true)
local dy = sweep(body.x + dx, body.y, body.vy, false)
entity.set(handle, {vx = dx, vy = dy})

end)
