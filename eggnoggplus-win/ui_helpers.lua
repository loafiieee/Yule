local ui = mod and mod.ui
if not ui then return end

local function copy(src, ancestors, depth)
  local out = {}
  if type(src) ~= 'table' then return out end
  ancestors, depth = ancestors or {}, depth or 0
  if ancestors[src] then error('cyclic UI table', 3) end
  if depth >= 32 then error('UI table nesting exceeds 32 levels', 3) end
  ancestors[src] = true
  for k, v in pairs(src) do
    if type(v) == 'table' then out[k] = copy(v, ancestors, depth + 1) else out[k] = v end
  end
  ancestors[src] = nil
  return out
end

local function merge_values(dst, src)
  if type(src) ~= 'table' then return dst end
  for k, v in pairs(src) do
    if type(v) == 'table' and type(dst[k]) == 'table' then
      merge_values(dst[k], v)
    elseif type(v) == 'table' then
      dst[k] = copy(v)
    else
      dst[k] = v
    end
  end
  return dst
end

local function merge(dst, src)
  -- Validate the entire input before writing any destination field.
  return merge_values(dst, copy(src))
end

local default_dark = {
  bg = {0.06, 0.07, 0.08, 0.88},
  fg = {0.92, 0.94, 0.96, 1.0},
  muted = {0.55, 0.60, 0.68, 1.0},
  accent = {1.0, 0.78, 0.25, 1.0},
  border = {0.23, 0.27, 0.32, 1.0},
  hover = {0.13, 0.16, 0.19, 0.94},
  active = {0.18, 0.17, 0.10, 0.96},
  disabled = {0.16, 0.17, 0.18, 0.50},
  pad = 8,
  gap = 6,
  text_scale = 1.0,
}

local themes = {
  default_dark = default_dark,
  high_contrast = merge(copy(default_dark), {
    bg={0,0,0,1}, fg={1,1,1,1}, muted={0.75,0.75,0.75,1},
    accent={1,1,0,1}, border={0.8,0.8,0.8,1},
    hover={0.15,0.15,0.15,1}, active={0.25,0.25,0,1},
    disabled={0.1,0.1,0.1,1},
  }),
}
local base_style_stack = { copy(default_dark) }
local main_thread = {}
local scoped_stacks = setmetatable({}, {__mode='k'})
local function thread_key() return coroutine.running() or main_thread end
local function get_style_stack()
  return scoped_stacks[thread_key()] or base_style_stack
end
local function set_style_stack(stack)
  local key = thread_key()
  if scoped_stacks[key] then scoped_stacks[key] = stack
  else base_style_stack = stack end
end

local function style_for(opts)
  local style_stack = get_style_stack()
  local s = copy(style_stack[#style_stack] or default_dark)
  if type(opts) == 'table' and type(opts.style) == 'table' then merge(s, opts.style) end
  return s
end

local function color(s, key, fallback)
  local v = s and s[key] or fallback
  if type(v) ~= 'table' then v = fallback or {1, 1, 1, 1} end
  return v
end

local function finite_number(value, fallback)
  local n = tonumber(value)
  if not n or n ~= n or n == math.huge or n == -math.huge then return fallback end
  return n
end

local function clamp01(v)
  v = tonumber(v) or 0
  if v ~= v then return 0 end
  if v < 0 then return 0 elseif v > 1 then return 1 end
  return v
end

local function shade(c, amount, alpha)
  c = type(c) == 'table' and c or {1, 1, 1, 1}
  amount = tonumber(amount) or 0
  local a = alpha ~= nil and alpha or c[4] or 1
  return { clamp01((c[1] or 0) + amount), clamp01((c[2] or 0) + amount), clamp01((c[3] or 0) + amount), a }
end

function ui.push_style(style)
  local style_stack = get_style_stack()
  local s = copy(style_stack[#style_stack] or default_dark)
  merge(s, style)
  style_stack[#style_stack + 1] = s
  return copy(s)
end

function ui.pop_style()
  local style_stack = get_style_stack()
  if #style_stack > 1 then return table.remove(style_stack) end
  return copy(style_stack[1])
end

-- A private stack isolates even unbalanced pushes/pops and set_theme calls.
-- Capture all pcall results explicitly: a callback may return trailing nils.
local function packed(...)
  return { n = select('#', ...), ... }
end

function ui.with_style(style, callback, ...)
  if type(style) ~= 'table' then error('style must be a table', 2) end
  if type(callback) ~= 'function' then error('callback must be a function', 2) end
  local style_stack = get_style_stack()
  local scoped = copy(style_stack[#style_stack] or default_dark)
  merge(scoped, style)
  local key = thread_key()
  local previous = scoped_stacks[key]
  scoped_stacks[key] = { scoped }
  local result = packed(pcall(callback, ...))
  scoped_stacks[key] = previous
  if not result[1] then error(result[2], 0) end
  return unpack(result, 2, result.n)
end

function ui.current_style()
  local style_stack = get_style_stack()
  return copy(style_stack[#style_stack] or default_dark)
end

function ui.define_theme(name, style)
  if type(name) ~= 'string' or name == '' or type(style) ~= 'table' then return false end
  themes[name] = merge(copy(default_dark), style)
  return true
end

function ui.theme_names()
  local names = {}
  for name in pairs(themes) do names[#names + 1] = name end
  table.sort(names)
  return names
end

function ui.style_color(key, fallback)
  local style_stack = get_style_stack()
  return copy(color(style_stack[#style_stack] or default_dark, key, fallback))
end

function ui.theme(style)
  local style_stack = get_style_stack()
  local updated = copy(style_stack[#style_stack])
  if type(style) == 'table' then merge(updated, style) end
  style_stack[#style_stack] = updated
  return copy(updated)
end

function ui.set_theme(name)
  local t = themes[tostring(name or 'default_dark')]
  if not t then return false end
  set_style_stack({ copy(t) })
  return true
end

local function resolve_bounds(opts, def_w, def_h)
  opts = opts or {}
  local x = tonumber(opts.x)
  local y = tonumber(opts.y)
  if (not x or not y) and ui.cursor then x, y = ui.cursor() end
  x = x or 0
  y = y or 0
  local w = tonumber(opts.w or opts.width) or def_w or 80
  local h = tonumber(opts.h or opts.height) or def_h or 28
  return x, y, w, h
end

local function advance_if_layout(opts, x, y, h, gap)
  opts = opts or {}
  if opts.no_advance then return end
  if opts.x ~= nil or opts.y ~= nil or not ui.cursor then return end
  ui.cursor(x, y + h + (tonumber(opts.gap) or gap or 6))
end

local function text_center(text, x, y, w, h, scale, c)
  text = tostring(text or '')
  scale = tonumber(scale) or 1
  local tw = 0
  local th = 9 * scale
  if ui.measure_text then tw, th = ui.measure_text(text, scale) end
  local max_w = math.max(4, w - 4)
  while tw > max_w and scale > 0.55 do
    scale = scale * 0.9
    if ui.measure_text then tw, th = ui.measure_text(text, scale) else break end
  end
  ui.text_at(text, x + (w - tw) * 0.5, y + h * 0.58, scale, c[1] or 1, c[2] or 1, c[3] or 1, c[4] or 1)
end

local function measured_width(text, scale)
  if ui.measure_text then local w = ui.measure_text(tostring(text or ''), scale) return w or 0 end
  return #tostring(text or '') * 9 * (tonumber(scale) or 1)
end

function ui.wrap_text(text, max_w, scale)
  text = tostring(text or '')
  max_w = tonumber(max_w) or 0
  scale = tonumber(scale) or 1
  local lines = {}
  local function push(line) lines[#lines + 1] = tostring(line or '') end
  local function push_long_word(word)
    local chunk = ''
    for i = 1, #word do
      local next_chunk = chunk .. word:sub(i, i)
      if max_w > 0 and chunk ~= '' and measured_width(next_chunk, scale) > max_w then
        push(chunk)
        chunk = word:sub(i, i)
      else
        chunk = next_chunk
      end
    end
    return chunk
  end
  local function emit_para(para)
    if para == '' then push('') return end
    local line = ''
    for word in tostring(para):gmatch('%S+') do
      if max_w > 0 and measured_width(word, scale) > max_w then
        if line ~= '' then push(line) line = '' end
        line = push_long_word(word)
      else
        local candidate = (line == '') and word or (line .. ' ' .. word)
        if max_w > 0 and line ~= '' and measured_width(candidate, scale) > max_w then
          push(line)
          line = word
        else
          line = candidate
        end
      end
    end
    push(line)
  end
  for para in (text .. '\n'):gmatch('(.-)\n') do emit_para(para) end
  if #lines == 0 then lines[1] = '' end
  return lines
end

function ui.text_wrapped(text, x, y, w, opts)
  opts = opts or {}
  local scale = tonumber(opts.scale) or 1
  local line_gap = tonumber(opts.line_gap) or 4
  local s = style_for(opts)
  local fg = opts.color or opts.fg or color(s, 'fg')
  local lines = ui.wrap_text(text, w, scale)
  local line_h = 9 * scale
  if ui.measure_text then local _, measured_h = ui.measure_text('Ag', scale) line_h = tonumber(measured_h) or line_h end
  for i = 1, #lines do
    ui.text_at(lines[i], x, y + (i - 1) * (line_h + line_gap) + line_h * 0.82, scale, fg[1] or 1, fg[2] or 1, fg[3] or 1, fg[4] or 1)
  end
  return #lines * line_h + math.max(#lines - 1, 0) * line_gap, #lines
end

function ui.tooltip(text, opts)
  if not text or text == '' then return false end
  opts = opts or {}
  local s = style_for(opts)
  local mx, my = ui.mouse_pos()
  local scale = tonumber(opts.scale) or 0.85
  local pad = tonumber(opts.pad) or 6
  local max_w = tonumber(opts.max_w or opts.w) or 280
  local lines = ui.wrap_text(tostring(text), max_w, scale)
  local tw, th = 0, 9 * scale
  for i = 1, #lines do
    local lw, lh = ui.measure_text(lines[i], scale)
    if lw > tw then tw = lw end
    if lh > th then th = lh end
  end
  local box_w = tw + pad * 2
  local box_h = (#lines * th) + math.max(#lines - 1, 0) * 4 + pad * 2
  local x = tonumber(opts.x) or (mx + 12)
  local y = tonumber(opts.y) or (my + 12)
  local sw, sh = ui.screen_size()
  if x + box_w > sw - 4 then x = sw - box_w - 4 end
  if y + box_h > sh - 4 then y = sh - box_h - 4 end
  if x < 4 then x = 4 end
  if y < 4 then y = 4 end
  ui.rect(x, y, box_w, box_h, { color = color(s, 'bg') })
  ui.border(x, y, box_w, box_h, { color = color(s, 'border') })
  ui.text_wrapped(tostring(text), x + pad, y + pad, tw, { scale = scale, color = color(s, 'fg'), line_gap = 4 })
  return true
end

local function maybe_tooltip(opts, hovered)
  if hovered and type(opts) == 'table' and opts.tooltip then ui.tooltip(opts.tooltip, opts.tooltip_opts) end
end

function ui.icon_button(id, icon, x, y, w, h, opts)
  opts = opts or {}
  local s = style_for(opts)
  local hovered, clicked, down = false, false, false
  if ui.hitbox then hovered, clicked, down = ui.hitbox(tostring(id or ''), x, y, w, h) end
  if opts.focused and opts.activate then clicked = true end
  if opts.disabled then clicked, down = false, false end
  local bg = color(s, 'bg')
  if opts.disabled then bg = color(s, 'disabled')
  elseif opts.selected then bg = color(s, 'active')
  elseif down then bg = color(s, 'active')
  elseif hovered then bg = color(s, 'hover') end
  ui.rect(x, y, w, h, { color = bg })
  ui.border(x, y, w, h, { line_w = opts.line_w or 1, color = opts.selected and color(s, 'accent') or color(s, 'border') })
  if opts.focused and not opts.disabled then
    ui.border(x, y, w, h, {line_w=2, color=color(s, 'fg')})
  end
  local fg = opts.disabled and color(s, 'muted') or color(s, 'fg')
  if type(icon) == 'number' and ui.draw_sprite then
    ui.draw_sprite(icon, x + w * 0.5, y + h * 0.5, { scale = opts.icon_scale or opts.scale or 1, tint = opts.tint or fg })
  elseif type(icon) == 'table' and ui.draw_sprite then
    local draw_opts = copy(icon)
    merge(draw_opts, opts.sprite_opts)
    if not draw_opts.scale then draw_opts.scale = opts.icon_scale or opts.scale or 1 end
    if not draw_opts.tint then draw_opts.tint = opts.tint or fg end
    ui.draw_sprite(draw_opts, x + w * 0.5, y + h * 0.5, draw_opts)
  else
    text_center(icon or opts.label or '', x, y, w, h, opts.text_scale or s.text_scale or 1, fg)
  end
  maybe_tooltip(opts, hovered)
  return clicked and not opts.disabled, hovered
end

function ui.panel(a, b, c, d, e, f)
  local id, x, y, w, h, opts
  if type(a) == 'string' then id, x, y, w, h, opts = a, b, c, d, e, f or {} else x, y, w, h, opts = a, b, c, d, e or {} end
  opts = opts or {}
  local s = style_for(opts)
  local hovered, clicked, down = false, false, false
  if id and ui.hitbox then hovered, clicked, down = ui.hitbox(tostring(id), x, y, w, h, opts.hitbox or opts) end
  if opts.disabled then clicked, down = false, false end
  local bg = opts.bg or opts.fill or color(s, 'bg')
  if opts.disabled then bg = opts.disabled_bg or color(s, 'disabled') elseif down and opts.active then bg = opts.active elseif hovered and opts.hover then bg = opts.hover end
  ui.rect(x, y, w, h, { color = bg, alpha = opts.alpha })
  if opts.inner ~= false then
    local inner = type(opts.inner) == 'table' and opts.inner or shade(bg, tonumber(opts.inner_shade) or 0.025, opts.inner_alpha or ((bg[4] or 1) * 0.48))
    local inset = tonumber(opts.inset) or 3
    if w > inset * 2 and h > inset * 2 then ui.rect(x + inset, y + inset, w - inset * 2, h - inset * 2, { color = inner }) end
  end
  if opts.accent_edge then
    local aw = tonumber(opts.accent_w or opts.accent_width) or 4
    local accent = opts.accent or color(s, 'accent')
    if opts.accent_edge == 'right' then ui.rect(x + w - aw, y, aw, h, { color = accent, alpha = opts.accent_alpha })
    elseif opts.accent_edge == 'top' then ui.rect(x, y, w, aw, { color = accent, alpha = opts.accent_alpha })
    elseif opts.accent_edge == 'bottom' then ui.rect(x, y + h - aw, w, aw, { color = accent, alpha = opts.accent_alpha })
    else ui.rect(x, y, aw, h, { color = accent, alpha = opts.accent_alpha }) end
  end
  if opts.border ~= false then
    local border = type(opts.border) == 'table' and opts.border or color(s, 'border')
    ui.border(x, y, w, h, { line_w = opts.line_w or opts.line_width or 1, color = border, alpha = opts.border_alpha })
  end
  maybe_tooltip(opts, hovered)
  return clicked, hovered, down
end

function ui.progress_bar(id, value, x, y, w, h, opts)
  if type(id) ~= 'string' then opts, h, w, y, x, value, id = h, w, y, x, value, id, nil end
  opts = opts or {}
  local s = style_for(opts)
  value = clamp01(value)
  x, y = finite_number(x, nil), finite_number(y, nil)
  w, h = finite_number(w, nil), finite_number(h, nil)
  if not x or not y or not w or not h or w <= 0 or h <= 0 then return value, false, false end
  local hovered, clicked, down = false, false, false
  if id and ui.hitbox and (opts.interactive or opts.hitbox) then hovered, clicked, down = ui.hitbox(tostring(id), x, y, w, h, opts.hitbox or opts) end
  local changed = false
  if opts.interactive and not opts.disabled and (clicked or down) and ui.mouse_pos then
    local mx, my = ui.mouse_pos()
    local next_value = opts.vertical and (1 - (my - y) / h) or ((mx - x) / w)
    if opts.reverse then next_value = 1 - next_value end
    next_value = clamp01(next_value)
    if next_value ~= value then value, changed = next_value, true end
  end
  local bg = opts.bg or opts.rail or color(s, 'border')
  local fill = opts.disabled and color(s, 'muted') or opts.fill or opts.color or color(s, 'accent')
  ui.rect(x, y, w, h, { color = bg, alpha = opts.alpha })
  if opts.vertical then
    local fh = h * value
    local fy = opts.reverse and y or (y + h - fh)
    ui.rect(x, fy, w, fh, { color = fill, alpha = opts.fill_alpha })
  else
    local fw = w * value
    local fx = opts.reverse and (x + w - fw) or x
    ui.rect(fx, y, fw, h, { color = fill, alpha = opts.fill_alpha })
  end
  if opts.border ~= false then ui.border(x, y, w, h, { line_w = opts.line_w or 1, color = type(opts.border) == 'table' and opts.border or color(s, 'border'), alpha = opts.border_alpha }) end
  if opts.label then text_center(opts.label, x, y, w, h, opts.text_scale or s.text_scale or 1, opts.text_color or color(s, 'fg')) end
  maybe_tooltip(opts, hovered)
  return value, changed, hovered
end

function ui.close_button(id, x, y, size, opts)
  opts = opts or {}
  size = tonumber(size) or 24
  local s = style_for(opts)
  local hovered, clicked, down = false, false, false
  if ui.hitbox then hovered, clicked, down = ui.hitbox(tostring(id or 'close'), x, y, size, size, opts.hitbox or opts) end
  if opts.disabled then clicked, down = false, false end
  local bg = opts.bg or (hovered and (opts.hover or color(s, 'hover')) or color(s, 'bg'))
  if down and opts.active then bg = opts.active end
  ui.rect(x, y, size, size, { color = bg, alpha = opts.alpha })
  if opts.border ~= false then ui.border(x, y, size, size, { line_w = opts.line_w or 1, color = type(opts.border) == 'table' and opts.border or color(s, 'border'), alpha = opts.border_alpha }) end
  local fg = opts.color or opts.fg or color(s, 'fg')
  local p = size * (tonumber(opts.pad_ratio) or 0.30)
  ui.line(x + p, y + p, x + size - p, y + size - p, { line_w = opts.stroke or 2, color = fg, alpha = opts.fg_alpha })
  ui.line(x + size - p, y + p, x + p, y + size - p, { line_w = opts.stroke or 2, color = fg, alpha = opts.fg_alpha })
  maybe_tooltip(opts, hovered)
  return clicked and not opts.disabled, hovered, down
end

local function item_key_label(item, i)
  if type(item) == 'table' then return item.id or item.value or i, item.label or item.name or tostring(item.id or item.value or i) end
  return i, tostring(item)
end

-- Input stays with the owning mod: callers pass one navigation edge only to
-- the focused control. The helper never polls or captures another mod's keys.
function ui.navigate_items(items, selected, direction, opts)
  opts = opts or {}
  if type(items) ~= 'table' or #items > 4096 then error('navigate_items expects at most 4096 items') end
  if direction ~= -1 and direction ~= 0 and direction ~= 1 then error('navigation direction must be -1, 0 or 1') end
  if type(opts) ~= 'table' then error('navigation options must be a table') end
  if direction == 0 or opts.disabled then return selected, false end
  local count, current = #items, nil
  for i = 1, count do
    local key = item_key_label(items[i], i)
    if key == selected then current = i break end
  end
  local index = current or (direction > 0 and 0 or count + 1)
  for attempt = 1, count do
    index = index + direction
    if index < 1 or index > count then
      if not opts.wrap then break end
      index = index < 1 and count or 1
    end
    local item = items[index]
    if type(item) ~= 'table' or not item.disabled then
      local key = item_key_label(item, index)
      return key, key ~= selected
    end
  end
  return selected, false
end

function ui.tabs(id, tabs, selected, opts)
  opts = opts or {}
  tabs = tabs or {}
  local s = style_for(opts)
  local x, y, w, h = resolve_bounds(opts, (#tabs > 0 and #tabs or 1) * 88, 30)
  local gap = tonumber(opts.gap) or 0
  local tab_w = tonumber(opts.tab_w) or ((w - gap * math.max(#tabs - 1, 0)) / math.max(#tabs, 1))
  local changed
  selected, changed = ui.navigate_items(tabs, selected, opts.navigate or 0, {wrap = opts.wrap, disabled = opts.disabled or (type(opts.item_opts) == 'table' and opts.item_opts.disabled)})
  for i = 1, #tabs do
    local key, label = item_key_label(tabs[i], i)
    local bx = x + (i - 1) * (tab_w + gap)
    local item_opts = merge({ selected = selected == key, text_scale = opts.text_scale or s.text_scale }, opts.item_opts)
    item_opts.disabled = opts.disabled or item_opts.disabled or (type(tabs[i]) == 'table' and tabs[i].disabled)
    local clicked = ui.icon_button(tostring(id) .. ':' .. tostring(key), label, bx, y, tab_w, h, item_opts)
    if clicked and selected ~= key then selected = key changed = true end
  end
  advance_if_layout(opts, x, y, h, s.gap)
  return selected, changed
end

ui.segmented = ui.tabs

function ui.swatch_grid(id, colors, selected, opts)
  opts = opts or {}
  colors = colors or {}
  local s = style_for(opts)
  local cols = math.max(1, math.floor(tonumber(opts.cols or opts.columns) or 8))
  local cell = tonumber(opts.cell or opts.cell_w) or 24
  local gap = tonumber(opts.gap) or 5
  local x, y = resolve_bounds(opts, cols * cell + (cols - 1) * gap, cell)
  local changed
  selected, changed = ui.navigate_items(colors, selected, opts.navigate or 0, opts)
  for i = 1, #colors do
    local item = colors[i]
    local key = type(item) == 'table' and (item.id or item.value or i) or i
    local c = type(item) == 'table' and (item.color or item.tint or item) or {1, 1, 1, 1}
    local col = (i - 1) % cols
    local row = math.floor((i - 1) / cols)
    local bx = x + col * (cell + gap)
    local by = y + row * (cell + gap)
    local hovered, clicked = ui.hitbox(tostring(id) .. ':' .. tostring(key), bx, by, cell, cell)
    ui.rect(bx, by, cell, cell, { color = c })
    ui.border(bx, by, cell, cell, { line_w = selected == key and 2 or 1, color = selected == key and color(s, 'accent') or color(s, 'border') })
    if hovered then ui.border(bx + 2, by + 2, cell - 4, cell - 4, { color = color(s, 'fg') }) end
    if clicked and not opts.disabled and not (type(item) == 'table' and item.disabled) and selected ~= key then selected = key changed = true end
  end
  local rows = math.ceil(#colors / cols)
  advance_if_layout(opts, x, y, rows * cell + math.max(rows - 1, 0) * gap, s.gap)
  return selected, changed
end

function ui.item_grid(id, items, selected, opts)
  opts = opts or {}
  items = items or {}
  local s = style_for(opts)
  local cols = math.max(1, math.floor(tonumber(opts.cols or opts.columns) or 5))
  local cell_w = tonumber(opts.cell_w or opts.cell or opts.w_cell) or 84
  local cell_h = tonumber(opts.cell_h or opts.cell or opts.h_cell) or 64
  local gap = tonumber(opts.gap) or 6
  local x, y = resolve_bounds(opts, cols * cell_w + (cols - 1) * gap, cell_h)
  local changed
  selected, changed = ui.navigate_items(items, selected, opts.navigate or 0, opts)
  for i = 1, #items do
    local item = items[i]
    local key, label = item_key_label(item, i)
    local col = (i - 1) % cols
    local row = math.floor((i - 1) / cols)
    local bx = x + col * (cell_w + gap)
    local by = y + row * (cell_h + gap)
    local hovered, clicked, down = ui.hitbox(tostring(id) .. ':' .. tostring(key), bx, by, cell_w, cell_h)
    ui.rect(bx, by, cell_w, cell_h, { color = down and color(s, 'active') or (hovered and color(s, 'hover') or color(s, 'bg')) })
    ui.border(bx, by, cell_w, cell_h, { line_w = selected == key and 2 or 1, color = selected == key and color(s, 'accent') or color(s, 'border') })
    if type(item) == 'table' and item.color then ui.rect(bx + 8, by + 8, cell_w - 16, cell_h - 26, { color = item.color }) end
    if type(item) == 'table' and item.sprite and ui.draw_sprite then ui.draw_sprite(item.sprite, bx + cell_w * 0.5, by + cell_h * 0.42, item.sprite_opts or {}) end
    text_center(label, bx + 4, by + cell_h - 22, cell_w - 8, 18, opts.text_scale or 0.75, color(s, 'fg'))
    if clicked and not opts.disabled and not (type(item) == 'table' and item.disabled) and selected ~= key then selected = key changed = true end
  end
  local rows = math.ceil(#items / cols)
  advance_if_layout(opts, x, y, rows * cell_h + math.max(rows - 1, 0) * gap, s.gap)
  return selected, changed
end

function ui.grid_layout(x, y, width, count, opts)
  opts = opts or {}
  if type(opts) ~= 'table' then error('grid_layout options must be a table') end
  local function number(value, fallback, name, minimum, maximum, integer)
    if value == nil then value = fallback end
    if type(value) ~= 'number' or value ~= value or value < minimum or value > maximum or (integer and value ~= math.floor(value)) then
      error('invalid grid_layout ' .. name)
    end
    return value
  end
  x = number(x, nil, 'x', -10000000, 10000000)
  y = number(y, nil, 'y', -10000000, 10000000)
  width = number(width, nil, 'width', 1, 10000000)
  count = number(count, nil, 'count', 0, 4096, true)
  local minimum = number(opts.min_cell_w, 120, 'min_cell_w', 1, 10000000)
  local height = number(opts.cell_h, 28, 'cell_h', 1, 10000000)
  local gap = number(opts.gap, 6, 'gap', 0, 10000000)
  local maximum = number(opts.max_cols, 4096, 'max_cols', 1, 4096, true)
  if count == 0 then return {}, 0, 0 end
  local cols = math.min(count, maximum, math.max(1, math.floor((width + gap) / (minimum + gap))))
  local cell_width = (width - (cols - 1) * gap) / cols
  local rows = math.ceil(count / cols)
  local rects = {}
  for i = 1, count do
    rects[i] = {x = x + ((i - 1) % cols) * (cell_width + gap), y = y + math.floor((i - 1) / cols) * (height + gap), w = cell_width, h = height}
  end
  return rects, rows * height + (rows - 1) * gap, cols
end

-- Pure fixed-height list virtualization. Partially visible rows are included;
-- callers own clipping and input routing, as with absolute-position widgets.
function ui.list_window(count, row_h, viewport_h, scroll, reveal)
  local function number(value, name, minimum, maximum, integer)
    if type(value) ~= 'number' or value ~= value or value < minimum or value > maximum or (integer and value ~= math.floor(value)) then
      error('invalid list_window ' .. name)
    end
    return value
  end
  count = number(count, 'count', 0, 1000000, true)
  row_h = number(row_h, 'row_h', 1, 10000000)
  viewport_h = number(viewport_h, 'viewport_h', 1, 10000000)
  scroll = number(scroll or 0, 'scroll', -10000000000000, 10000000000000)
  if reveal ~= nil then reveal = number(reveal, 'reveal', 1, count, true) end
  local total = count * row_h
  local maximum = math.max(0, total - viewport_h)
  scroll = math.max(0, math.min(maximum, scroll))
  if reveal then
    local top = (reveal - 1) * row_h
    if top < scroll or row_h > viewport_h then scroll = top
    elseif top + row_h > scroll + viewport_h then scroll = top + row_h - viewport_h end
    scroll = math.max(0, math.min(maximum, scroll))
  end
  local first = count == 0 and 1 or math.floor(scroll / row_h) + 1
  local last = math.min(count, math.ceil((scroll + viewport_h) / row_h))
  return {first = first, last = last, offset_y = (first - 1) * row_h - scroll,
          scroll = scroll, max_scroll = maximum, total_h = total}
end

-- Stateful input belongs to the caller; scroll is a zero-based row offset.
-- Only complete rows are drawn, so no native scissor state is needed.
function ui.list_box(id, items, selected, opts)
  opts = opts or {}
  if type(opts) ~= 'table' or type(items) ~= 'table' or #items > 4096 then
    error('list_box expects options and at most 4096 items')
  end
  local function number(v, fallback, name, lo, hi)
    if v == nil then v = fallback end
    if type(v) ~= 'number' or v ~= v or v < lo or v > hi then error('invalid list_box ' .. name) end
    return v
  end
  local x = number(opts.x, 0, 'x', -10000000, 10000000)
  local y = number(opts.y, 0, 'y', -10000000, 10000000)
  local w = number(opts.w, 240, 'width', 32, 10000000)
  local h = number(opts.h, 168, 'height', 8, 10000000)
  local row_h = number(opts.row_h, 28, 'row_h', 8, h)
  local capacity = math.floor(h / row_h)
  local scroll = math.floor(number(opts.scroll, 0, 'scroll', -1000000, 1000000))
  local delta = math.floor(number(opts.scroll_rows, 0, 'scroll_rows', -4096, 4096))
  local command = opts.navigate or 'none'
  local commands = {none=true, previous=true, next=true, page_up=true, page_down=true, first=true, last=true}
  if not commands[command] then error('invalid list_box navigation') end
  if opts.filter ~= nil and type(opts.filter) ~= 'string' then error('list_box filter must be a string') end
  if #(opts.filter or '') > 4096 then error('list_box filter exceeds 4096 bytes') end
  local filter = string.lower(opts.filter or '')
  local rows, keys, current = {}, {}, nil
  for i = 1, #items do
    local key, label = item_key_label(items[i], i)
    if (type(key) ~= 'string' and type(key) ~= 'number') or key ~= key then error('invalid list_box item key') end
    if #label > 4096 then error('list_box label exceeds 4096 bytes') end
    if keys[key] then error('duplicate list_box item key') end
    keys[key] = true
    if string.find(string.lower(label), filter, 1, true) then
      rows[#rows + 1] = {key=key, label=label, disabled=type(items[i]) == 'table' and items[i].disabled}
      if key == selected then current = #rows end
    end
  end
  local original, activated = selected, false
  local maximum = math.max(0, #rows - capacity)
  local function clamp(v) return math.max(0, math.min(maximum, v)) end
  scroll = clamp(scroll)
  if not opts.disabled then
    scroll = clamp(scroll + delta)
    if command ~= 'none' then
      local direction = (command == 'previous' or command == 'page_up' or command == 'last') and -1 or 1
      local target
      if command == 'first' then target = 1
      elseif command == 'last' then target = #rows
      elseif not current then target = direction > 0 and 1 or #rows
      else
        local distance = (command == 'page_up' or command == 'page_down') and capacity or 1
        target = math.max(1, math.min(#rows, current + direction * distance))
      end
      while target >= 1 and target <= #rows and rows[target].disabled do target = target + direction end
      if target >= 1 and target <= #rows then current, selected = target, rows[target].key end
    end
  end
  if current and (opts.reveal or (not opts.disabled and command ~= 'none')) then
    if current <= scroll then scroll = current - 1
    elseif current > scroll + capacity then scroll = current - capacity end
    scroll = clamp(scroll)
  end
  local s = style_for(opts)
  ui.rect(x, y, w, h, {color=color(s, 'bg')})
  ui.border(x, y, w, h, {color=color(s, opts.focused and 'fg' or 'border')})
  local row_w = w - (maximum > 0 and 16 or 0)
  -- Scrollbar interaction happens before rows are laid out in this frame.
  if maximum > 0 then
    local bx, bw = x + w - 12, 12
    local hovered, clicked, down = ui.hitbox(tostring(id) .. ':scroll', bx, y, bw, h)
    local thumb_h = math.min(h - 1, math.max(4, h * capacity / #rows))
    if not opts.disabled and (clicked or down) then
      local _, my = ui.mouse_pos()
      scroll = clamp(math.floor((my - y - thumb_h / 2) / (h - thumb_h) * maximum + 0.5))
    end
    ui.rect(bx, y, bw, h, {color=color(s, 'border')})
    ui.rect(bx, y + (h - thumb_h) * scroll / maximum, bw, thumb_h, {color=color(s, opts.disabled and 'muted' or 'accent')})
  end
  local scale = math.min(number(opts.text_scale, 1, 'text_scale', 0.01, 100), row_h / 18)
  for index = scroll + 1, math.min(#rows, scroll + capacity) do
    local row = rows[index]
    local disabled = opts.disabled or row.disabled
    local label = row.label:gsub('[%c]', ' ')
    if measured_width(label, scale) > row_w - 8 then
      local low, high = 0, #label
      while low < high do
        local middle = math.floor((low + high + 1) / 2)
        if measured_width(label:sub(1, middle) .. '...', scale) <= row_w - 8 then low = middle else high = middle - 1 end
      end
      label = label:sub(1, low) .. '...'
      if measured_width(label, scale) > row_w - 8 then label = '' end
    end
    local clicked = ui.icon_button(tostring(id) .. ':row:' .. tostring(index), label,
      x, y + (index - scroll - 1) * row_h, row_w, row_h,
      {selected=row.key == selected, disabled=disabled, style=opts.style,
       text_scale=scale, focused=opts.focused and row.key == selected})
    if clicked and not disabled then selected, current, activated = row.key, index, true end
  end
  if opts.focused and opts.activate and not opts.disabled and current and not rows[current].disabled then activated = true end
  return selected, selected ~= original, scroll, activated, #rows
end

function ui.slider(id, value, min_value, max_value, opts)
  opts = opts or {}
  local s = style_for(opts)
  local x, y, w, h = resolve_bounds(opts, 180, 26)
  x, y = finite_number(x, 0), finite_number(y, 0)
  value = finite_number(value, 0)
  min_value = finite_number(min_value, 0)
  max_value = finite_number(max_value, 1)
  if max_value < min_value then min_value, max_value = max_value, min_value end
  local span = max_value - min_value
  if not finite_number(span) then error('slider range is too wide') end
  w = math.max(1, finite_number(w, 180))
  h = math.max(8, finite_number(h, 26))
  local original = value
  value = math.max(min_value, math.min(max_value, value))
  local navigate = opts.navigate or 0
  if navigate ~= -1 and navigate ~= 0 and navigate ~= 1 then error('slider navigate must be -1, 0 or 1') end
  local keyboard_step = finite_number(opts.keyboard_step)
  if opts.keyboard_step ~= nil and (not keyboard_step or keyboard_step <= 0) then
    error('slider keyboard_step must be finite and positive')
  end
  if not keyboard_step then
    keyboard_step = finite_number(opts.step)
    if not keyboard_step or keyboard_step <= 0 then keyboard_step = span / 100 end
  end
  local hovered, clicked, down = ui.hitbox(tostring(id), x, y, w, h)
  local changed = value ~= original
  if not opts.disabled and (clicked or down) then
    local mx = ui.mouse_pos()
    local t = (mx - x) / w
    if t < 0 then t = 0 elseif t > 1 then t = 1 end
    local nv = min_value + (max_value - min_value) * t
    local step = finite_number(opts.step)
    if step and step > 0 then
      local units = (nv - min_value) / step
      if finite_number(units) then nv = min_value + math.floor(units + 0.5) * step end
    end
    nv = math.max(min_value, math.min(max_value, nv))
    if nv ~= value then value = nv changed = true end
  end
  -- Mouse input wins if both input methods arrive in the same frame.
  if not opts.disabled and navigate ~= 0 and not (clicked or down) then
    local next_value = math.max(min_value, math.min(max_value, value + navigate * keyboard_step))
    if next_value ~= value then value, changed = next_value, true end
  end
  if opts.focused and not opts.disabled then
    ui.border(x, y, w, h, {line_w=2, color=color(s, 'fg')})
  end
  local t = span > 0 and (value - min_value) / span or 0
  if t < 0 then t = 0 elseif t > 1 then t = 1 end
  local track_y = y + h * 0.5 - 2
  ui.rect(x, track_y, w, 4, { color = color(s, 'border') })
  ui.rect(x, track_y, w * t, 4, { color = color(s, opts.disabled and 'muted' or 'accent') })
  ui.rect(x + w * t - 4, y + 4, 8, h - 8, { color = hovered and color(s, 'fg') or color(s, opts.disabled and 'muted' or 'accent') })
  if opts.label then local fg = color(s, 'fg'); ui.text_at(tostring(opts.label), x, y - 4, opts.text_scale or 0.75, fg[1] or 1, fg[2] or 1, fg[3] or 1, fg[4] or 1) end
  maybe_tooltip(opts, hovered)
  advance_if_layout(opts, x, y, h, s.gap)
  return value, changed
end

function ui.checkbox(id, value, opts)
  opts = opts or {}
  local s = style_for(opts)
  local size = tonumber(opts.size) or 22
  local x, y = resolve_bounds(opts, size, size)
  local clicked = ui.icon_button(id, value and 'X' or '', x, y, size, size, opts)
  if opts.label then local fg = color(s, 'fg'); ui.text_at(tostring(opts.label), x + size + (opts.gap or s.gap), y + size * 0.72, opts.text_scale or s.text_scale, fg[1] or 1, fg[2] or 1, fg[3] or 1, fg[4] or 1) end
  advance_if_layout(opts, x, y, size, s.gap)
  if clicked then return not value, true end
  return not not value, false
end

function ui.cursor_sprite(index)
  if not ui.sprite_id then return nil end
  return ui.sprite_id('misc', tonumber(index) or 7)
end

function ui.draw_cursor(opts)
  if opts == nil or (type(opts) == 'table' and next(opts) == nil) then
    if ui._draw_native_cursor and ui._draw_native_cursor() then return true end
  end
  opts = opts or {}
  local mx, my = ui.mouse_pos()
  local scale = tonumber(opts.scale) or (ui.readable_scale and ui.readable_scale(1.0)) or 1
  local sprite = opts.sprite or ui.cursor_sprite(opts.index)
  if sprite and ui.draw_sprite then
    local size = tonumber(opts.size) or 16
    local hot_x = tonumber(opts.hot_x) or 0
    local hot_y = tonumber(opts.hot_y) or 0
    if ui.draw_sprite(sprite, mx + (size * 0.5 - hot_x) * scale, my + (size * 0.5 - hot_y) * scale, { scale = scale, tint = opts.tint, layer = opts.layer or 1000 }) then return true end
  end
  if ui.line then
    ui.line(mx - 6, my, mx + 6, my, { color = opts.color or {1, 1, 1, 1}, line_w = 1 })
    ui.line(mx, my - 6, mx, my + 6, { color = opts.color or {1, 1, 1, 1}, line_w = 1 })
    return true
  end
  return false
end

if not ui.tile_preview then
  function ui.tile_preview(id, frame, arg, x, y, scale, tile_y, opts)
    if not ui.sprite_id or not ui.draw_sprite then return false end
    local sprite = ui.sprite_id('tiles', tonumber(id) or 0)
    if not sprite then return false end
    local draw_opts = type(opts) == 'table' and copy(opts) or {}
    if not draw_opts.scale then draw_opts.scale = tonumber(scale) or 1 end
    return ui.draw_sprite(sprite, tonumber(x) or 0, tonumber(y) or 0, draw_opts) and true or false
  end
end

local state_specs = {}
local state_router_installed = false
local active_state = nil
local last_clock = os.clock()

local function install_state_router()
  if state_router_installed then return end
  state_router_installed = true
  mod.on_frame(function()
    local now = os.clock()
    local dt = now - last_clock
    if dt < 0 then dt = 0 elseif dt > 0.25 then dt = 0.25 end
    last_clock = now
    local name = ui.state_name()
    if name ~= active_state then
      local old = active_state
      local old_spec = old and state_specs[old]
      -- Retire the old lifecycle before calling user code: leave may redirect.
      active_state = nil
      if old_spec and old_spec.leave then old_spec.leave(name) end
      if ui.state_name() ~= name then return end
      active_state = name
      local new_spec = state_specs[name]
      if new_spec and new_spec.enter then new_spec.enter(old) end
      if ui.state_name() ~= name then return end
    end
    local spec = state_specs[name]
    if spec then
      if spec.update then spec.update(dt) end
      if ui.state_name() ~= name or state_specs[name] ~= spec then return end
      if spec.render then spec.render() end
      if ui.state_name() ~= name or state_specs[name] ~= spec then return end
      local cursor_opts = type(spec.cursor) == 'table' and spec.cursor or spec.cursor_opts
      if spec.cursor == false then
        if ui._set_default_cursor_visible then ui._set_default_cursor_visible(false) end
      elseif cursor_opts and ui.draw_cursor then
        local drew = false
        if ui.begin_overlay and ui.end_overlay then ui.begin_overlay() drew = ui.draw_cursor(cursor_opts) ui.end_overlay() else drew = ui.draw_cursor(cursor_opts) end
        if drew and ui._set_default_cursor_visible then ui._set_default_cursor_visible(false) end
      end
    end
  end)
  mod.on_event(function(e)
    local spec = state_specs[ui.state_name()]
    if spec and spec.event then return spec.event(e) and true or false end
    return false
  end)
end

function ui.define_state(name, spec)
  if type(name) ~= 'string' or name == '' then return false, 'state name required' end
  if spec == nil then spec = {} end
  if type(spec) ~= 'table' then return false, 'state spec must be a table' end
  for _, key in ipairs({'enter', 'update', 'render', 'event', 'leave'}) do
    if spec[key] ~= nil and type(spec[key]) ~= 'function' then
      return false, 'state ' .. key .. ' must be a function'
    end
  end
  if not ui.create_state then return false, 'native state registration unavailable' end
  local ok, err = ui.create_state(name)
  if not ok then return false, err or 'native state registration failed' end
  state_specs[name] = spec
  install_state_router()
  return true
end
