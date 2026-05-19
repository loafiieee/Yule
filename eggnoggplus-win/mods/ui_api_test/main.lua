local STATE = "ui_api_test"
local SDLK_ESCAPE = 27
local SDLK_F8 = 1073741889

local ui = mod.ui

local state = {
  frame = 0,
  old_clicks = 0,
  icon_clicks = 0,
  hitbox_clicks = 0,
  hitbox_right_clicks = 0,
  right_clicks = 0,
  tab = "legacy",
  panel = "legacy",
  swatch = "gold",
  item = "none",
  slider = 0.5,
  checkbox = true,
  last_event = "none",
  entered_at = 0,
}

local swatches = {
  { id = "gold", color = {1.00, 0.78, 0.25, 1.0} },
  { id = "mint", color = {0.35, 0.95, 0.68, 1.0} },
  { id = "sky", color = {0.36, 0.70, 1.00, 1.0} },
  { id = "rose", color = {1.00, 0.45, 0.62, 1.0} },
  { id = "white", color = {0.94, 0.94, 0.90, 1.0} },
  { id = "ink", color = {0.08, 0.10, 0.12, 1.0} },
}

local tabs = {
  { id = "legacy", label = "Legacy" },
  { id = "widgets", label = "Widgets" },
  { id = "state", label = "State" },
}

local panel_tabs = {
  { id = "legacy", label = "Old API" },
  { id = "primitives", label = "Primitives" },
  { id = "widgets", label = "Widgets" },
}

local items = {
  { id = "none", label = "X", color = {0.18, 0.20, 0.23, 1.0} },
  { id = "hat", label = "Hat", color = {1.00, 0.78, 0.25, 1.0} },
  { id = "mask", label = "Mask", color = {0.36, 0.70, 1.00, 1.0} },
  { id = "boots", label = "Boots", color = {0.45, 0.95, 0.65, 1.0} },
}

local function c(name)
  local style = ui.current_style and ui.current_style() or {}
  return style[name] or {1, 1, 1, 1}
end

local function text(x, y, s, value, color_name)
  local col = c(color_name or "fg")
  ui.text_at(tostring(value), x, y, s or 1.0, col[1], col[2], col[3])
end

local function wrapped(x, y, w, s, value, color_name)
  local col = c(color_name or "fg")
  w = math.max(32, tonumber(w) or 32)
  if ui.text_wrapped then
    return ui.text_wrapped(tostring(value), x, y, w, {
      scale = s or 1.0,
      color = col,
      line_gap = 3,
    })
  end

  text(x, y, s, value, color_name)
  return 18, 1
end

local function enter_state()
  if ui.enter_state then
    ui.enter_state(STATE)
  end
end

local function leave_state()
  if ui.leave_state then
    ui.leave_state()
  elseif ui.goto_main_menu then
    ui.goto_main_menu()
  end
end

local function draw_panel(x, y, w, h, title)
  ui.rect(x, y, w, h, { color = {0.07, 0.08, 0.10, 0.90} })
  ui.border(x, y, w, h, { line_w = 1, color = {0.24, 0.28, 0.34, 1.0} })
  ui.rect(x, y, w, 32, { color = {0.10, 0.12, 0.15, 0.94} })
  ui.line(x, y + 32, x + w, y + 32, { color = {0.22, 0.26, 0.31, 1.0} })
  text(x + 12, y + 23, 0.95, title)
end

local function draw_status_row(x, y, label, value, value_w)
  text(x, y, 0.78, label, "muted")
  value = tostring(value)
  value_w = value_w or 190
  local value_width = ui.measure_text and select(1, ui.measure_text(value, 0.78)) or 0
  if ui.text_wrapped and value_width > value_w then
    wrapped(x + 128, y - 14, value_w, 0.78, value, "fg")
  else
    text(x + 128, y, 0.78, value, "fg")
  end
end

local function draw_legacy_panel(x, y, w, h)
  draw_panel(x, y, w, h, "Old API")

  local bx = x + 14
  local by = y + 54
  local desc_h = wrapped(bx, by - 4, w - 28, 0.82, "text_at, layout/text/button, button_at, sprites")

  local layout_y = by + desc_h + 8
  local layout_w = math.max(96, math.min(172, w - 28))
  ui.layout(bx, layout_y, 24, 6, layout_w, 0.82)
  ui.text("layout text")
  if ui.button("legacy_layout_button", "layout button", layout_w, 25) then
    state.old_clicks = state.old_clicks + 1
  end

  local abs_x = bx + layout_w + 16
  local abs_y = layout_y + 30
  if abs_x + 126 > x + w - 14 then
    abs_x = bx
    abs_y = layout_y + 62
  end
  if ui.button_at("legacy_abs_button", "button_at", abs_x, abs_y, 126, 25) then
    state.old_clicks = state.old_clicks + 1
  end

  local sprite_y = math.max(layout_y + 106, abs_y + 52)
  local sprite_id = ui.sprite_id and ui.sprite_id("sprites", 0)
  if sprite_id and ui.draw_sprite then
    ui.draw_sprite(sprite_id, bx + 35, sprite_y, {
      scale = 3.0,
      tint = {1, 1, 1, 1},
    })
    wrapped(bx + 74, sprite_y - 14, w - 102, 0.78, "draw_sprite('sprites', 0)")
  else
    wrapped(bx, sprite_y - 14, w - 28, 0.78, "draw_sprite unavailable", "muted")
  end

  local tile_y = sprite_y + 58
  if ui.tile_preview then
    local ok = ui.tile_preview(1, 0, 0, bx + 40, tile_y, 2.4, 0)
    wrapped(bx + 74, tile_y - 14, w - 102, 0.78, "tile_preview: " .. tostring(ok))
  else
    wrapped(bx, tile_y - 14, w - 28, 0.78, "tile_preview unavailable", "muted")
  end

  local measured_w, measured_h = 0, 0
  if ui.measure_text then
    measured_w, measured_h = ui.measure_text("measure_text", 0.82)
  end
  draw_status_row(bx, y + h - 54, "clicks", state.old_clicks, w - 160)
  draw_status_row(bx, y + h - 30, "measured", string.format("%.1fx%.1f", measured_w, measured_h), w - 160)
end

local function draw_primitives_panel(x, y, w, h)
  draw_panel(x, y, w, h, "New Primitives")

  local bx = x + 14
  local by = y + 52
  local hovered, clicked, down = ui.hitbox("primitive_hitbox", bx, by, w - 28, 52)
  local _, right_clicked, right_down = ui.hitbox("primitive_hitbox_rmb", bx, by, w - 28, 52, 3)
  if clicked then
    state.hitbox_clicks = state.hitbox_clicks + 1
  end
  if right_clicked then
    state.hitbox_right_clicks = state.hitbox_right_clicks + 1
  end

  ui.rect(bx, by, w - 28, 52, {
    color = right_down and {0.16, 0.08, 0.18, 0.95} or (down and {0.18, 0.16, 0.08, 0.95} or (hovered and {0.14, 0.17, 0.20, 0.95} or {0.08, 0.10, 0.12, 0.90})),
  })
  ui.border(bx, by, w - 28, 52, {
    line_w = hovered and 2 or 1,
    color = hovered and c("accent") or c("border"),
  })
  ui.line(bx + 12, by + 38, bx + w - 42, by + 12, {
    line_w = 2,
    color = c("accent"),
  })
  wrapped(bx + 12, by + 8, w - 52, 0.8, "rect + border + line + hitbox")
  if hovered then
    ui.tooltip("This is an invisible hitbox over low-level primitives.")
  end

  local icon = ui.sprite_id and ui.sprite_id("misc", 0)
  local clicked_icon = ui.icon_button(
    "primitive_icon_button",
    icon or "*",
    bx,
    by + 72,
    48,
    42,
    {
      tooltip = "icon_button uses sprites or text",
      icon_scale = 2.0,
    }
  )
  if clicked_icon then
    state.icon_clicks = state.icon_clicks + 1
  end
  wrapped(bx + 60, by + 82, w - 88, 0.82, "icon_button clicks: " .. state.icon_clicks)

  draw_status_row(bx, by + 142, "hitbox hover", tostring(hovered), w - 160)
  draw_status_row(bx, by + 166, "hitbox clicks", state.hitbox_clicks, w - 160)
  draw_status_row(bx, by + 190, "mouse", string.format("%d,%d", ui.mouse_pos()), w - 160)
  if ui.mouse_buttons then
    local left_down, left_pressed, right_down_state, right_pressed = ui.mouse_buttons()
    draw_status_row(bx, by + 214, "LMB", tostring(left_down) .. " / " .. tostring(left_pressed), w - 160)
    draw_status_row(bx, by + 238, "RMB", tostring(right_down_state) .. " / " .. tostring(right_pressed), w - 160)
  end
  draw_status_row(bx, by + 262, "RMB clicks", state.right_clicks .. " / hitbox " .. state.hitbox_right_clicks, w - 160)
  draw_status_row(bx, by + 286, "cursor", ui.draw_cursor and "auto misc[7]" or "unavailable", w - 160)
end

local function draw_widgets_panel(x, y, w, h)
  draw_panel(x, y, w, h, "New Widgets")

  local bx = x + 14
  local by = y + 50
  state.tab = select(1, ui.tabs("test_tabs", tabs, state.tab, {
    x = bx,
    y = by,
    w = w - 28,
    h = 28,
    gap = 4,
  }))

  state.swatch = select(1, ui.swatch_grid("test_swatches", swatches, state.swatch, {
    x = bx,
    y = by + 46,
    cols = 6,
    cell = 26,
    gap = 6,
  }))

  state.item = select(1, ui.item_grid("test_items", items, state.item, {
    x = bx,
    y = by + 94,
    cols = 2,
    cell_w = (w - 34) / 2,
    cell_h = 56,
    gap = 6,
  }))

  state.slider = select(1, ui.slider("test_slider", state.slider, 0.0, 1.0, {
    x = bx,
    y = by + 226,
    w = w - 28,
    h = 30,
    step = 0.05,
    tooltip = "Drag to test slider input.",
  }))

  state.checkbox = select(1, ui.checkbox("test_checkbox", state.checkbox, {
    x = bx,
    y = by + 272,
    label = "checkbox",
    tooltip = "Checkbox toggles a Lua boolean.",
  }))

  draw_status_row(bx, y + h - 54, "selected", state.tab .. " / " .. state.swatch .. " / " .. state.item, w - 160)
  draw_status_row(bx, y + h - 30, "slider/check", string.format("%.2f / %s", state.slider, tostring(state.checkbox)), w - 160)
end

local function render_state()
  state.frame = state.frame + 1

  if ui.begin_overlay then
    ui.begin_overlay()
  end

  ui.push_style({
    bg = {0.06, 0.07, 0.08, 0.88},
    fg = {0.92, 0.94, 0.96, 1.0},
    muted = {0.58, 0.64, 0.72, 1.0},
    accent = {1.0, 0.78, 0.25, 1.0},
    border = {0.23, 0.27, 0.32, 1.0},
  })

  local sw, sh = ui.screen_size()
  ui.rect(0, 0, sw, sh, { color = {0.025, 0.030, 0.035, 0.96} })

  local margin = 34
  local content_w = math.max(240, sw - margin * 2)
  text(margin, 42, 1.18, "UI API Test State")
  local subtitle_h = wrapped(margin, 54, math.min(590, content_w), 0.82, "Main menu button or F8 opens this state. Escape leaves.", "muted")

  local compact = content_w < 900
  local mx, my = ui.mouse_pos()
  local state_name = ui.state_name and ui.state_name() or "?"
  local info_x = sw - 318
  local info_y = 42
  if compact or info_x < margin + 360 then
    info_x = margin
    info_y = 78 + subtitle_h
  end
  text(info_x, info_y, 0.78, "state: " .. state_name, "muted")
  text(info_x, info_y + 22, 0.78, string.format("screen: %.0fx%.0f  mouse: %d,%d", sw, sh, mx, my), "muted")
  local readable_scale = ui.readable_scale and ui.readable_scale(1.0) or 1.0
  text(info_x, info_y + 44, 0.78, string.format("readable scale: %.2f", readable_scale), "muted")
  wrapped(info_x, info_y + 52, math.min(318, content_w), 0.78, "last event: " .. state.last_event, "muted")

  local gap = 18
  local top = math.max(118, info_y + 92)
  local bottom = 44
  if compact then
    state.panel = select(1, ui.tabs("ui_api_test_panels", panel_tabs, state.panel, {
      x = margin,
      y = top,
      w = content_w,
      h = 28,
      gap = 4,
    }))
    top = top + 42
    local panel_h = math.max(300, sh - top - bottom)
    if state.panel == "legacy" then
      draw_legacy_panel(margin, top, content_w, panel_h)
    elseif state.panel == "primitives" then
      draw_primitives_panel(margin, top, content_w, panel_h)
    else
      draw_widgets_panel(margin, top, content_w, panel_h)
    end
  else
    local panel_w = math.floor((content_w - (gap * 2)) / 3)
    local panel_h = math.max(300, sh - top - bottom)
    draw_legacy_panel(margin, top, panel_w, panel_h)
    draw_primitives_panel(margin + panel_w + gap, top, panel_w, panel_h)
    draw_widgets_panel(margin + (panel_w + gap) * 2, top, panel_w, panel_h)
  end

  if ui.button_at("ui_api_test_back", "Back", sw - 116, sh - 38, 82, 26) then
    leave_state()
  end

  ui.pop_style()

  if ui.end_overlay then
    ui.end_overlay()
  end
end

local function event_state(e)
  if not e then
    return false
  end

  state.last_event = tostring(e.type or "?") .. ":" .. tostring(e.sym or e.button or "")

  if e.type == "keydown" and e.sym == SDLK_ESCAPE then
    leave_state()
    return true
  end

  if e.type == "mousebuttondown" and e.button == 3 then
    state.right_clicks = state.right_clicks + 1
    return true
  end

  return ui.is_state and ui.is_state(STATE) or false
end

local function install_state()
  if ui.define_state then
    ui.define_state(STATE, {
      enter = function()
        state.entered_at = os.clock()
        state.last_event = "enter"
      end,
      update = function()
      end,
      render = render_state,
      event = event_state,
      leave = function()
        state.last_event = "leave"
      end,
    })
    return
  end

  ui.create_state(STATE)
  mod.on_frame(function()
    if ui.state_name() == STATE then
      render_state()
    end
  end)
  mod.on_event(event_state)
end

install_state()

mod.on_frame(function()
  if ui.state_name() == "main" or ui.state_name() == "main_initial" then
    if ui.native_button("ui_api_test_open", "UI API", 1.0, 6.0, 6.0, 6.0) then
      enter_state()
    end
  end
end)

mod.on_event(function(e)
  if e and e.type == "keydown" and e.sym == SDLK_F8 then
    enter_state()
    return true
  end
  return false
end)

mod.on_load(function()
  mod.log("UI API Test loaded. Open it from the main menu with UI API or press F8.")
end)
