local ffi = require("ffi")
local state_switch = nil
local pregame_state = nil

-- Engine addresses (same constants used in hooks.c / lua_manager.c).
local ADDR_STATE_SWITCH = 0x405DC0
local ADDR_PREGAME_STATE = 0x4483A8

ffi.cdef[[
  typedef void* (__cdecl *state_switch_fn)(void*);
]]
state_switch = ffi.cast("state_switch_fn", ADDR_STATE_SWITCH)
pregame_state = ffi.cast("void*", ADDR_PREGAME_STATE)

local function cfg_num(key, fallback)
  local v = config.get(key, fallback)
  v = tonumber(v)
  if v == nil then return fallback end
  return v
end

local function cfg_bool(key, fallback)
  local v = config.get(key, fallback)
  if v == nil then return fallback end
  if type(v) == "boolean" then return v end
  local s = tostring(v):lower()
  return s == "1" or s == "true" or s == "yes" or s == "on"
end

local function go_to_pregame(mode)
  config.set("last_selected_mode", mode)

  state_switch(pregame_state)
end

mod.on_load(function()
  mod.log("Main Menu VS Buttons loaded")
end)

mod.on_frame(function()
  if not mod.ui.is_state("main") then return end

  local grid_y = cfg_num("grid_y", -1.05)
  local layout_x = cfg_num("layout_x", 5.0)
  local layout_y = cfg_num("layout_y", 5.0)

  local vs_player_grid_x = cfg_num("vs_player_grid_x", 3.75)
  local vs_ai_grid_x = cfg_num("vs_ai_grid_x", 5.35)

  -- Adds two native menu buttons on title screen.
  -- Note: this is pure-Lua and non-invasive; stock START still exists underneath.
  if mod.ui.native_button("start_vs_player", "vs player", vs_player_grid_x, grid_y, layout_x, layout_y) then
    go_to_pregame("player")
  end

  if mod.ui.native_button("start_vs_ai", "vs ai", vs_ai_grid_x, grid_y, layout_x, layout_y) then
    -- currently routes to normal pregame; AI behavior can branch off this mode later
    go_to_pregame("ai")
  end

  -- Optional visual relabel near stock START text.
  if cfg_bool("overlay_label_enabled", true) then
    local x = cfg_num("overlay_x", 548)
    local y = cfg_num("overlay_y", 126)
    local scale = cfg_num("overlay_scale", 1.0)
    mod.ui.text_at("start_label_overlay", "vs player", x, y, scale)
  end
end)

mod.on_unload(function()
  mod.log("Main Menu VS Buttons unloaded")

  if state_switch ~= nil then
    pcall(function()
      state_switch:free()
    end)
  end
end)
