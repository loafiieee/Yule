-- Glyph Browser (tiny)
-- Cycles through 0x00..0xFF and displays the glyph + its value.
-- Hotkeys:
--   Left/Right or A/D = +/- 1
--   Up/Down or Q/E    = +/- 16

local cur = 0x20 -- start at space-ish

-- SDL keycodes (standard SDL2)
local SDLK_LEFT  = 1073741904
local SDLK_RIGHT = 1073741903
local SDLK_UP    = 1073741906
local SDLK_DOWN  = 1073741905

local function wrap_byte(x)
  x = x % 256
  if x < 0 then x = x + 256 end
  return x
end

local function step(delta)
  cur = wrap_byte(cur + delta)
end

mod.on_event(function(e)
  if e.type ~= "keydown" then return false end

  -- Only handle keys while in menus so we don't mess with gameplay input.
  if not mod.ui.is_state("menu") then return false end

  local sym = e.sym

  -- +/- 1
  if sym == SDLK_LEFT or sym == string.byte("a") or sym == string.byte("A") then
    step(-1); return true
  end
  if sym == SDLK_RIGHT or sym == string.byte("d") or sym == string.byte("D") then
    step(1); return true
  end

  -- +/- 16
  if sym == SDLK_UP or sym == string.byte("q") or sym == string.byte("Q") then
    step(-16); return true
  end
  if sym == SDLK_DOWN or sym == string.byte("e") or sym == string.byte("E") then
    step(16); return true
  end

  return false
end)

mod.on_frame(function()
  if not mod.ui.is_state("menu") then return end

  local value_hex = string.format("0x%02X", cur)
  local value_dec = tostring(cur)

  -- The glyph we *ask* for
  local glyph = string.char(cur)

  -- HUD text
  mod.ui.text_at("Glyph Browser", 24, 24, 1.2)
  mod.ui.text_at("Value: " .. value_hex .. " (" .. value_dec .. ")", 24, 48, 1.0)
  mod.ui.text_at("Keys: Left/Right or A/D = +/-1   Up/Down or Q/E = +/-16", 24, 68, 1.0)

  -- Big glyph preview
  mod.ui.text_at("Glyph:", 24, 96, 1.0)
  mod.ui.text_at(glyph, 120, 86, 6.0)
end)