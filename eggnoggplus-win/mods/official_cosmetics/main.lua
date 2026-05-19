local STATE = "official_cosmetics_hats"
local SDLK_ESCAPE = 27

local ui = mod.ui
local game = mod.game

local selected_player = storage.get("selected_player", "p1")
local profile = {
  p1 = { hat = storage.get("p1_hat", "none") },
  p2 = { hat = storage.get("p2_hat", "none") },
}

local hat_sheet = nil
local asset_error = nil
local last_asset_try = 0

local hats = {
  {
    id = "none",
    name = "None",
    motion = false,
    sprite_index = nil,
    color = {0.16, 0.18, 0.21, 1.0},
  },
  {
    id = "cap",
    name = "Cap",
    sprite_index = 0,
    motion = true,
    scale = 0.64,
    y = 14.0,
    bob = 0.7,
    tilt = 7.0,
    drag = 0.40,
  },
  {
    id = "crown",
    name = "Crown",
    sprite_index = 1,
    motion = true,
    scale = 0.68,
    y = 15.5,
    bob = 0.45,
    tilt = 5.0,
    drag = 0.25,
  },
  {
    id = "halo",
    name = "Halo",
    sprite_index = 2,
    motion = false,
    scale = 0.72,
    y = 20.5,
    bob = 0.0,
    tilt = 0.0,
    drag = 0.0,
  },
  {
    id = "beanie",
    name = "Beanie",
    sprite_index = 3,
    motion = true,
    scale = 0.66,
    y = 14.5,
    bob = 0.75,
    tilt = 6.0,
    drag = 0.35,
  },
  {
    id = "top_hat",
    name = "Top Hat",
    sprite_index = 4,
    motion = false,
    scale = 0.72,
    y = 18.5,
    bob = 0.0,
    tilt = 0.0,
    drag = 0.0,
  },
  {
    id = "visor",
    name = "Visor",
    sprite_index = 5,
    motion = true,
    scale = 0.62,
    y = 13.0,
    bob = 0.35,
    tilt = 4.0,
    drag = 0.20,
  },
}

local hat_by_id = {}
for i = 1, #hats do
  hat_by_id[hats[i].id] = hats[i]
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function valid_hat_id(id)
  return hat_by_id[id] and id or "none"
end

profile.p1.hat = valid_hat_id(profile.p1.hat)
profile.p2.hat = valid_hat_id(profile.p2.hat)
selected_player = (selected_player == "p2") and "p2" or "p1"

local function save_hat(player_id, hat_id)
  player_id = (player_id == "p2") and "p2" or "p1"
  hat_id = valid_hat_id(hat_id)
  profile[player_id].hat = hat_id
  storage.set(player_id .. "_hat", hat_id)
end

local function set_selected_player(player_id)
  selected_player = (player_id == "p2") and "p2" or "p1"
  storage.set("selected_player", selected_player)
end

local function ensure_assets()
  if hat_sheet then return true end

  local now = os.clock()
  if now - last_asset_try < 0.35 then
    return false
  end
  last_asset_try = now

  local sheet, err = mod.assets.load_spritesheet("hats", "assets/hats.png", {
    cell_w = 32,
    cell_h = 32,
  })
  if sheet then
    hat_sheet = sheet
    asset_error = nil
    return true
  end

  local info = mod.assets.info and mod.assets.info("hats") or nil
  if info and info.base_id then
    hat_sheet = info
    asset_error = nil
    return true
  end

  asset_error = err
  return false
end

local function hat_sprite(hat)
  if not hat or hat.id == "none" or not hat.sprite_index then return nil end
  if not ensure_assets() then return nil end
  return mod.assets.sprite_id("hats", hat.sprite_index)
end

local function hat_items()
  local items = {}
  for i = 1, #hats do
    local h = hats[i]
    local item = {
      id = h.id,
      label = h.name,
      color = h.color,
    }
    local sprite = hat_sprite(h)
    if sprite then
      item.sprite = sprite
      item.sprite_opts = {
        scale = h.id == "halo" and 1.10 or 1.0,
        angle = h.motion and math.sin(os.clock() * 3.0 + i) * 4.0 or 0.0,
      }
    end
    items[#items + 1] = item
  end
  return items
end

local function world_to_screen(x, y, cam)
  local sw, sh = ui.screen_size()
  local cw = tonumber(cam and cam.w) or sw
  local ch = tonumber(cam and cam.h) or sh
  if cw <= 1 then cw = sw end
  if ch <= 1 then ch = sh end
  local scale_x = sw / cw
  local scale_y = sh / ch
  local scale = math.min(scale_x, scale_y)
  local sx = sw * 0.5 + (x - (tonumber(cam and cam.x) or 0)) * scale
  local sy = sh * 0.5 - (y - (tonumber(cam and cam.y) or 0)) * scale
  return sx, sy, scale
end

local function draw_hat(hat_id, player, opts)
  local hat = hat_by_id[valid_hat_id(hat_id)]
  local sprite = hat_sprite(hat)
  if not sprite or not player then return false end

  opts = opts or {}
  local tick = opts.tick or os.clock() * 60.0
  local facing = tonumber(player.facing) or 1
  local vx = tonumber(player.vx) or 0
  local vy = tonumber(player.vy) or 0
  local bob = 0.0
  local drag = 0.0
  local angle = 0.0

  if hat.motion then
    bob = math.sin(tick * 0.27 + (player.index or 0) * 1.8) * (hat.bob or 0.0)
    drag = clamp(-vx * (hat.drag or 0.0), -2.0, 2.0)
    angle = clamp(-vx * (hat.tilt or 0.0) + vy * 0.6, -14.0, 14.0)
  end

  local draw_x
  local draw_y
  local draw_scale
  if opts.screen then
    draw_x = opts.x + drag * (opts.body_scale or 1.0) * 0.15
    draw_y = opts.y - ((hat.y or 14.0) * (opts.body_scale or 1.0)) + bob * (opts.body_scale or 1.0) * 0.16
    draw_scale = (opts.body_scale or 1.0) * (hat.scale or 0.65) * 0.5
  else
    local sx, sy, world_scale = world_to_screen((tonumber(player.x) or 0) + drag,
                                                (tonumber(player.y) or 0) + (hat.y or 14.0) + bob,
                                                opts.camera)
    draw_x = sx
    draw_y = sy
    draw_scale = world_scale * (hat.scale or 0.65)
  end

  return ui.draw_sprite(sprite, draw_x, draw_y, {
    scale = draw_scale,
    angle = angle,
    flip = facing < 0,
  })
end

local function draw_preview_player(cx, cy, body_scale, player_id)
  local clock = os.clock()
  local bounce = math.sin(clock * 3.2) * 2.0
  local frame = 0
  local base = ui.sprite_id and ui.sprite_id("sprites", frame)
  local mask = ui.sprite_id and ui.sprite_id("sprites", 128 + frame)
  local tint = player_id == "p2" and {0.34, 0.66, 1.00, 1.0} or {1.00, 0.46, 0.28, 1.0}

  if base then
    ui.draw_sprite(base, cx, cy + bounce, { scale = body_scale })
  end
  if mask then
    ui.draw_sprite(mask, cx, cy + bounce, { scale = body_scale, tint = tint })
  end

  draw_hat(profile[player_id].hat, {
    index = player_id == "p2" and 1 or 0,
    facing = 1,
    vx = math.sin(clock * 2.1) * 0.8,
    vy = math.cos(clock * 2.5) * 0.5,
  }, {
    screen = true,
    x = cx,
    y = cy + bounce,
    body_scale = body_scale,
    tick = clock * 60.0,
  })
end

local function draw_main_button()
  if not ui.is_state("main") then return end
  ensure_assets()

  local sw, sh = ui.screen_size()
  local sprite = hat_sprite(hat_by_id.cap)
  local size = math.max(38, math.min(52, sw * 0.04))
  local x = sw - size - 22
  local y = 22

  ui.begin_overlay()
  local clicked
  if sprite then
    clicked = ui.icon_button("official_cosmetics_open", sprite, x, y, size, size, {
      icon_scale = 0.95,
      tooltip = "Hats",
    })
  else
    clicked = ui.icon_button("official_cosmetics_open", "H", x, y, size, size, {
      tooltip = asset_error or "Hats",
    })
  end
  ui.end_overlay()

  if clicked then
    ui.enter_state(STATE)
  end
end

local player_tabs = {
  { id = "p1", label = "PLAYER 1" },
  { id = "p2", label = "PLAYER 2" },
}

local function draw_hats_state()
  ensure_assets()

  local sw, sh = ui.screen_size()
  local margin = math.max(22, math.min(46, sw * 0.035))
  local top = margin
  local bottom = sh - margin
  local left_w = math.max(260, math.min(420, sw * 0.34))
  local gap = math.max(18, sw * 0.018)
  local right_x = margin + left_w + gap
  local right_w = sw - right_x - margin
  local panel_h = bottom - top
  local header_h = 44

  ui.begin_overlay()
  ui.rect(0, 0, sw, sh, { color = {0.045, 0.048, 0.052, 0.96} })
  ui.rect(margin, top, left_w, panel_h, { color = {0.075, 0.082, 0.092, 0.96} })
  ui.border(margin, top, left_w, panel_h, { line_w = 1, color = {0.22, 0.25, 0.30, 1.0} })
  ui.rect(right_x, top, right_w, panel_h, { color = {0.070, 0.076, 0.086, 0.96} })
  ui.border(right_x, top, right_w, panel_h, { line_w = 1, color = {0.22, 0.25, 0.30, 1.0} })

  ui.text_at("HATS", margin + 18, top + 31, 1.1, 0.94, 0.95, 0.96)
  if ui.button_at("official_cosmetics_back", "BACK", sw - margin - 110, top + 8, 110, 30) then
    ui.leave_state()
  end

  selected_player = select(1, ui.segmented("official_cosmetics_player", player_tabs, selected_player, {
    x = margin + 18,
    y = top + header_h + 10,
    w = left_w - 36,
    h = 30,
    text_scale = 0.78,
  }))
  set_selected_player(selected_player)

  local preview_cx = margin + left_w * 0.5
  local preview_cy = top + panel_h * 0.54
  local body_scale = math.max(4.8, math.min(7.0, left_w / 60.0))
  ui.rect(margin + 18, top + 100, left_w - 36, panel_h - 150, { color = {0.10, 0.11, 0.12, 0.82} })
  ui.border(margin + 18, top + 100, left_w - 36, panel_h - 150, { line_w = 1, color = {0.20, 0.23, 0.27, 1.0} })
  draw_preview_player(preview_cx, preview_cy, body_scale, selected_player)

  local active_hat = hat_by_id[profile[selected_player].hat] or hat_by_id.none
  ui.text_at(active_hat.name, margin + 24, bottom - 34, 0.82, 0.82, 0.85, 0.88)

  ui.text_at("HEADWEAR", right_x + 18, top + 31, 1.0, 0.94, 0.95, 0.96)
  local grid_y = top + header_h + 18
  local cols = right_w > 620 and 4 or (right_w > 440 and 3 or 2)
  local cell_w = math.max(126, (right_w - 36 - (cols - 1) * 10) / cols)
  local cell_h = 92
  local new_hat = select(1, ui.item_grid("official_cosmetics_hats_" .. selected_player,
                                         hat_items(),
                                         profile[selected_player].hat,
                                         {
                                           x = right_x + 18,
                                           y = grid_y,
                                           cols = cols,
                                           cell_w = cell_w,
                                           cell_h = cell_h,
                                           gap = 10,
                                           text_scale = 0.70,
                                         }))
  if new_hat ~= profile[selected_player].hat then
    save_hat(selected_player, new_hat)
  end

  ui.end_overlay()
end

local function draw_gameplay_hats()
  if ui.state_name() ~= "game" then return end
  if not ensure_assets() then return end

  local snap = game.snapshot(0, false)
  if not snap or not snap.in_game then return end
  local cam = game.camera and game.camera() or nil
  local room = snap.room_index

  ui.begin_overlay()
  local p0 = snap.player
  local p1 = snap.enemy
  if p0 and p0.room_index == room then
    draw_hat(profile.p1.hat, p0, { camera = cam, tick = snap.native_tick or snap.tick or os.clock() * 60.0 })
  end
  if p1 and p1.room_index == room then
    draw_hat(profile.p2.hat, p1, { camera = cam, tick = snap.native_tick or snap.tick or os.clock() * 60.0 })
  end
  ui.end_overlay()
end

if storage.schema and storage.schema() < 1 then
  storage.set_schema(1)
end

if ui.define_state then
  ui.define_state(STATE, {
    render = draw_hats_state,
    event = function(e)
      if e.type == "keydown" and e.sym == SDLK_ESCAPE then
        ui.leave_state()
        return true
      end
      return true
    end,
  })
else
  ui.create_state(STATE)
end

mod.on_frame(function()
  draw_main_button()
  draw_gameplay_hats()
end)
