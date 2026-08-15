local game = mod.game

local flight_enabled = false
local toggle_latched = false
local anchor_x = nil
local anchor_y = nil
local desired_x = 0
local desired_y = 0
local velocity_x = 0
local velocity_y = 0
local active_player = nil
local was_in_game = false

local KEY_F = 102
local KMOD_CTRL_DIVISOR = 64

local function has_bit(value, flag)
  value = math.floor(tonumber(value) or 0)
  return math.floor(value / flag) % 2 == 1
end

local function ctrl_is_down(modifiers)
  -- SDL KMOD_LCTRL and KMOD_RCTRL are bits 0x40 and 0x80.
  return math.floor((tonumber(modifiers) or 0) / KMOD_CTRL_DIVISOR) % 4 ~= 0
end

local function configured_player()
  if config.get("controlled_player", "p1") == "p2" then
    return 1
  end
  return 0
end

local function reset_anchor()
  anchor_x = nil
  anchor_y = nil
  desired_x = 0
  desired_y = 0
  velocity_x = 0
  velocity_y = 0
end

local function release_controls()
  if active_player ~= nil then
    game.input_clear(active_player)
  end
  active_player = nil
  reset_anchor()
end

local function set_flight_enabled(enabled, snapshot)
  if enabled == flight_enabled then return end

  flight_enabled = enabled
  if enabled then
    active_player = configured_player()
    anchor_x = snapshot and snapshot.player and snapshot.player.x or nil
    anchor_y = snapshot and snapshot.player and snapshot.player.y or nil
    mod.log("Flight enabled for " .. (active_player == 0 and "P1" or "P2"))
  else
    release_controls()
    mod.log("Flight disabled")
  end
end

local function movement_from(raw)
  local horizontal = 0
  local vertical = 0

  if has_bit(raw, game.CMD_LEFT) then horizontal = horizontal - 1 end
  if has_bit(raw, game.CMD_RIGHT) then horizontal = horizontal + 1 end
  -- Eggnogg's vertical world axis is opposite to its command direction.
  if has_bit(raw, game.CMD_UP) then vertical = vertical + 1 end
  if has_bit(raw, game.CMD_DOWN) then vertical = vertical - 1 end

  if horizontal ~= 0 and vertical ~= 0 and
     config.get("normalize_diagonal_speed", true) then
    local diagonal_scale = 0.7071067811865476
    horizontal = horizontal * diagonal_scale
    vertical = vertical * diagonal_scale
  end

  local speed = tonumber(config.get("speed", 1.5)) or 1.5
  return horizontal * speed, vertical * speed
end

local function approach(current, target, amount)
  if current < target then return math.min(current + amount, target) end
  if current > target then return math.max(current - amount, target) end
  return target
end

local function apply_flight_position(snapshot)
  if not snapshot or not snapshot.player then return false end

  if anchor_x == nil or anchor_y == nil then
    anchor_x = snapshot.player.x
    anchor_y = snapshot.player.y
  end

  local previous_x = anchor_x
  local previous_y = anchor_y
  local acceleration = tonumber(config.get("acceleration", 0.25)) or 0.25
  local deceleration = tonumber(config.get("deceleration", 0.35)) or 0.35

  velocity_x = approach(
    velocity_x,
    desired_x,
    desired_x == 0 and deceleration or acceleration
  )
  velocity_y = approach(
    velocity_y,
    desired_y,
    desired_y == 0 and deceleration or acceleration
  )
  anchor_x = anchor_x + velocity_x
  anchor_y = anchor_y + velocity_y

  -- A deliberately small snapshot changes only the controlled player's motion.
  -- Global values copied below prevent apply_snapshot from changing match state.
  local motion = {
    player_index = active_player,
    enemy_index = 1 - active_player,
    room_index = snapshot.room_index,
    start_countdown = snapshot.start_countdown,
    end_countdown = snapshot.end_countdown,
    native_tick = snapshot.native_tick,
    rng_seed = snapshot.rng_seed,
    game_level = snapshot.game_level,
    leader_index = snapshot.leader_index,
    player = {
      x = anchor_x,
      y = anchor_y,
      prev_x = previous_x,
      prev_y = previous_y,
      vx = 0,
      vy = 0
    }
  }

  local ok, err = game.apply_snapshot(motion)
  if not ok then
    mod.error("Flight stopped: " .. tostring(err or "player state was unavailable"))
    flight_enabled = false
    release_controls()
    return false
  end
  return true
end

mod.on_event(function(event)
  if event.type == "keyup" and event.sym == KEY_F then
    toggle_latched = false
    return false
  end

  if event.type ~= "keydown" or event.sym ~= KEY_F or
     not ctrl_is_down(event.mod) or toggle_latched then
    return false
  end

  toggle_latched = true
  local player = configured_player()
  local snapshot = game.snapshot(player, false)
  if not snapshot or not snapshot.in_game or not snapshot.player then
    mod.warn("Ctrl+F only toggles flight during a match")
    return true
  end

  set_flight_enabled(not flight_enabled, snapshot)
  return true
end)

mod.on_tick(function()
  local player = configured_player()
  local snapshot = game.snapshot(player, false)
  local in_game = snapshot and snapshot.in_game and snapshot.player ~= nil

  if not flight_enabled or not in_game then
    if was_in_game and active_player ~= nil then
      release_controls()
    end
    was_in_game = in_game
    return
  end

  if active_player ~= player then
    if active_player ~= nil then game.input_clear(active_player) end
    active_player = player
    reset_anchor()
  end

  was_in_game = true
  -- Poll mode 2 reports held controls instead of press edges.
  local raw = game.poll_cmds_raw(active_player, 2) or 0
  desired_x, desired_y = movement_from(raw)

  -- Take over movement while leaving sword attacks and menu input available.
  local preserved = 0
  if has_bit(raw, game.CMD_ATTACK) then preserved = preserved + game.CMD_ATTACK end
  if has_bit(raw, game.CMD_MENU) then preserved = preserved + game.CMD_MENU end
  game.set_input(active_player, preserved, 1, true)
end)

mod.on_tick_post(function()
  if not flight_enabled or active_player == nil then return end

  local snapshot = game.snapshot(active_player, false)
  if not snapshot or not snapshot.in_game then
    release_controls()
    return
  end
  apply_flight_position(snapshot)
end)

mod.on_unload(function()
  if active_player ~= nil then
    game.input_clear(active_player)
  end
end)

mod.log("Flight Mode loaded. Press Ctrl+F during a match to toggle flight.")
