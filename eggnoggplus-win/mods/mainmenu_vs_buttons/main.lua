local ffi = require("ffi")

-- Engine addresses (same constants used in hooks.c / lua_manager.c).
local ADDR_STATE_SWITCH = 0x405DC0
local ADDR_PREGAME_STATE = 0x4483A8

ffi.cdef[[
  typedef void* (__cdecl *state_switch_fn)(void*);
]]

local state_switch = ffi.cast("state_switch_fn", ADDR_STATE_SWITCH)
local pregame_state = ffi.cast("void*", ADDR_PREGAME_STATE)

local function cfg_num(key, fallback)
  local v = tonumber(config.get(key, fallback))
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

  local grid_y = cfg_num("grid_y", 0.0)
  local layout_x = cfg_num("layout_x", 5.0)
  local layout_y = cfg_num("layout_y", 5.0)

  -- Tune these in config.cfg for your exact menu scale.
  local vs_player_grid_x = cfg_num("vs_player_grid_x", 4.0)
  local vs_ai_grid_x = cfg_num("vs_ai_grid_x", 5.2)

  if mod.ui.native_button("start_vs_player", "VS PLAYER", vs_player_grid_x, grid_y, layout_x, layout_y) then
    go_to_pregame("player")
  end

  if mod.ui.native_button("start_vs_ai", "VS AI", vs_ai_grid_x, grid_y, layout_x, layout_y) then
    go_to_pregame("ai")
  end

  -- Optional visual relabel over the stock START text.
  if cfg_bool("overlay_label_enabled", true) then
    local x = cfg_num("overlay_x", 548)
    local y = cfg_num("overlay_y", 126)
    local scale = cfg_num("overlay_scale", 1.0)
    -- text_at signature is: text, x, y, scale[, r,g,b]
    mod.ui.text_at("VS PLAYER", x, y, scale)
  end
end)

mod.on_unload(function()
  mod.log("Main Menu VS Buttons unloaded")
  pcall(function()
    state_switch:free()
  end)
end)
