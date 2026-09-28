local game = mod.game
local ui = mod.ui
local active = false
local x, y, zoom = 0, 0, 1
local dt = 1 / 60
local held = {}
local dragging = false
local mouse_x, mouse_y = 0, 0
local modifiers = 0
local toggle_latched = false
local toggle_consumed = false

local KEY = {
  g = 103, escape = 27,
  w = 119, a = 97, s = 115, d = 100,
  up = 1073741906, down = 1073741905,
  left = 1073741904, right = 1073741903,
  r = 114, c = 99, minus = 45, equals = 61,
}

local function bit_set(value, flag)
  return math.floor((value or 0) / flag) % 2 == 1
end

local function in_game()
  return ui.state_name() == "game"
end

local function release()
  active = false
  dragging = false
  held = {}
  game.clear_render_camera()
end

local function center(reset_zoom)
  local native = game.camera()
  x, y = native.x, native.y
  if reset_zoom then zoom = 1 end
end

local function set_zoom(factor)
  zoom = math.max(0.25, math.min(4, zoom * factor))
end

local function controlled_key(sym)
  return sym == KEY.w or sym == KEY.a or sym == KEY.s or sym == KEY.d or
         sym == KEY.up or sym == KEY.down or sym == KEY.left or
         sym == KEY.right or sym == KEY.r or sym == KEY.c or
         sym == KEY.minus or sym == KEY.equals
end

mod.on_event(function(e)
  if e.type == "delta_time" then
    dt = math.max(0, math.min(0.05, tonumber(e.value) or 0))
    return false
  end

  if e.type == "keydown" and e.sym == KEY.g then
    if not toggle_latched then
      toggle_latched = true
      toggle_consumed = in_game() and
                        (bit_set(e.mod, 1) or bit_set(e.mod, 2))
      if toggle_consumed then
        if active then
          release()
        else
          center(true)
          active = true
        end
      end
    end
    return toggle_consumed
  end
  if e.type == "keyup" and e.sym == KEY.g then
    local consumed = toggle_consumed
    toggle_latched = false
    toggle_consumed = false
    return consumed
  end
  if not active then return false end

  if e.type == "keydown" or e.type == "keyup" then
    modifiers = e.mod or 0
    if e.sym == KEY.escape and e.type == "keydown" then
      release()
      return true
    end
    if not controlled_key(e.sym) then return false end
    if e.type == "keyup" then
      held[e.sym] = nil
    elseif not held[e.sym] then
      held[e.sym] = true
      if e.sym == KEY.r then center(true) end
      if e.sym == KEY.c then center(false) end
      if e.sym == KEY.equals then set_zoom(tonumber(config.get("zoom_step", 1.2)) or 1.2) end
      if e.sym == KEY.minus then set_zoom(1 / (tonumber(config.get("zoom_step", 1.2)) or 1.2)) end
    end
    return true
  end

  if e.type == "mousebuttondown" and e.button == 2 then
    dragging = true
    mouse_x, mouse_y = e.x or 0, e.y or 0
    return true
  end
  if e.type == "mousebuttonup" and e.button == 2 then
    dragging = false
    return true
  end
  if e.type == "mousemotion" and dragging then
    local mx, my = e.x or mouse_x, e.y or mouse_y
    local native = game.camera()
    local sw, sh = ui.screen_size()
    if sw > 0 and sh > 0 then
      x = x - (mx - mouse_x) * native.w / (sw * zoom)
      y = y + (my - mouse_y) * native.h / (sh * zoom)
    end
    mouse_x, mouse_y = mx, my
    return true
  end
  if e.type == "mousewheel" then
    local wheel = math.max(-12, math.min(12, tonumber(e.y) or 0))
    if wheel ~= 0 then
      set_zoom((tonumber(config.get("zoom_step", 1.2)) or 1.2) ^ wheel)
    end
    return true
  end
  return false
end)

mod.on_frame(function()
  if not active then return end
  if not in_game() then
    release()
    return
  end
  local horizontal = (held[KEY.d] or held[KEY.right]) and 1 or 0
  horizontal = horizontal - ((held[KEY.a] or held[KEY.left]) and 1 or 0)
  local vertical = (held[KEY.w] or held[KEY.up]) and 1 or 0
  vertical = vertical - ((held[KEY.s] or held[KEY.down]) and 1 or 0)
  if horizontal ~= 0 and vertical ~= 0 then
    horizontal, vertical = horizontal * 0.7071067812, vertical * 0.7071067812
  end
  local speed = tonumber(config.get("pan_speed", 480)) or 480
  if bit_set(modifiers, 1) or bit_set(modifiers, 2) then
    speed = speed * (tonumber(config.get("fast_multiplier", 3)) or 3)
  end
  if bit_set(modifiers, 64) or bit_set(modifiers, 128) then
    speed = speed * (tonumber(config.get("slow_multiplier", 0.25)) or 0.25)
  end
  x = x + horizontal * speed * dt / zoom
  y = y + vertical * speed * dt / zoom
  local ok, err = game.set_render_camera(x, y, zoom, {
    hide_players = config.get("hide_players", false),
    hide_head_indicators = config.get("hide_head_indicators", false),
    hide_go_arrow = config.get("hide_go_arrow", false),
    hide_pause_button = config.get("hide_pause_button", true),
  })
  if not ok then
    release()
    mod.warn("Freecam stopped: " .. tostring(err))
    return
  end
  if config.get("show_hud", true) then
    ui.begin_overlay()
    ui.text_at(("FREECAM  x %.0f  y %.0f  zoom %.2fx"):format(x, y, zoom),
               14, 14, 0.8, 1, 1, 1, 1)
    ui.text_at("WASD/Arrows pan  Middle drag  Wheel/+/- zoom  R reset  C center  Shift+G exit",
               14, 34, 0.65, 1, 1, 1, 0.8)
    ui.end_overlay()
  end
end)

mod.on_unload(release)
mod.log("Free Camera loaded. Press Shift+G during a match to toggle.")
