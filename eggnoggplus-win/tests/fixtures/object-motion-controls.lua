
entity.on_update("demo:orb", function(handle, value)
local motion = entity.get(handle)
entity.set(handle, {vx = motion.vx * 0.5, vy = math.max(-1, math.min(1, motion.vy + 0.25))})

end)
