local event, frame, unload
local calls = {}
local cleared = 0
local state = "game"
local settings = {}

config = { get = function(key, default)
  if settings[key] ~= nil then return settings[key] end
  return default
end }
mod = {
  game = {
    camera = function() return { x = 100, y = 200, w = 528, h = 384 } end,
    set_render_camera = function(x, y, zoom, options)
      calls[#calls + 1] = { x = x, y = y, zoom = zoom, options = options }
      return true
    end,
    clear_render_camera = function() cleared = cleared + 1 end,
  },
  ui = {
    state_name = function() return state end,
    screen_size = function() return 528, 384 end,
    begin_overlay = function() end,
    end_overlay = function() end,
    text_at = function() end,
  },
  on_event = function(callback) event = callback end,
  on_frame = function(callback) frame = callback end,
  on_unload = function(callback) unload = callback end,
  log = function() end,
  warn = function(message) error(message) end,
}

dofile("mods/freecam/main.lua")
assert(not event({ type = "keydown", sym = string.byte("g"), mod = 0 }))
assert(not event({ type = "keydown", sym = string.byte("g"), mod = 1 }))
assert(not event({ type = "keyup", sym = string.byte("g"), mod = 1 }))
assert(event({ type = "keydown", sym = string.byte("g"), mod = 1 }))
assert(event({ type = "keydown", sym = string.byte("g"), mod = 1 }))
assert(event({ type = "keyup", sym = string.byte("g"), mod = 1 }))
event({ type = "delta_time", value = 0.05 })
assert(event({ type = "keydown", sym = string.byte("w"), mod = 0 }))
frame()
assert(#calls == 1 and calls[1].x == 100 and calls[1].y > 200)
assert(not calls[1].options.hide_players and not calls[1].options.hide_head_indicators
       and not calls[1].options.hide_go_arrow and calls[1].options.hide_pause_button)
assert(event({ type = "mousewheel", y = -1 }))
frame()
assert(calls[2].zoom < 1)
assert(event({ type = "keyup", sym = string.byte("w"), mod = 0 }))
assert(event({ type = "keydown", sym = string.byte("r"), mod = 0 }))
frame()
assert(calls[3].x == 100 and calls[3].y == 200 and calls[3].zoom == 1)
settings.hide_players = true
settings.hide_head_indicators = true
settings.hide_go_arrow = true
settings.hide_pause_button = false
frame()
assert(calls[4].options.hide_players and calls[4].options.hide_head_indicators
       and calls[4].options.hide_go_arrow and not calls[4].options.hide_pause_button)
assert(event({ type = "keydown", sym = string.byte("g"), mod = 2 }))
assert(cleared == 1)
assert(#calls == 4)
unload()
assert(cleared == 2)
print("freecam smoke: OK")
