-- Abilities Demo: dash + double jump via the World Control API.
--
-- Two things that bit the earlier version:
--   1) Timing: the native player update runs BETWEEN mod.on_tick and
--      mod.on_tick_post. Velocity written in on_tick is overwritten; we write in
--      on_tick_post so it survives.
--   2) Input: the player's cmd_bits struct field is consumed by the native update,
--      so rising-edge detection off it is unreliable. We read LIVE input with
--      mod.game.poll_cmds(i) and track our own previous value instead.
--
-- JUMP is a dedicated button (0x01), not "up". -y is up. Dash is position-based
-- (and wall-checked) so it isn't fought by the native horizontal movement model.

local CMD_JUMP  = 0x01
local CMD_RIGHT = 0x04
local CMD_LEFT  = 0x08

local function has(bits, mask) return (bits % (mask + mask)) >= mask end

local st = {
  [0] = { prev = 0, jumps = 0, last_dir = 0, last_tap = -1000, dash_dir = 0, dash_frames = 0 },
  [1] = { prev = 0, jumps = 0, last_dir = 0, last_tap = -1000, dash_dir = 0, dash_frames = 0 },
}

mod.on_load(function()
  mod.log("Loaded. Double-jump = press JUMP again in the air; dash = double-tap a direction.")
end)

mod.on_tick_post(function()
  if mod.game.world.online_active() then return end
  if not config.get("enabled", true) then return end

  local dbg       = config.get("debug", true)
  local tick      = mod.game.tick_count()
  local jump_v    = config.get("jump_strength", 12.0)
  local max_extra = math.floor(config.get("max_extra_jumps", 1))
  local dash_dist = config.get("dash_distance", 16.0)
  local dash_win  = math.floor(config.get("dash_window_ticks", 12))
  local want_dj   = config.get("double_jump", true)
  local want_dash = config.get("dash", true)

  for i = 0, 1 do
    local p = mod.game.world.player(i)
    if p.valid then
      local s = st[i]
      local cmds = mod.game.poll_cmds(i)
      local jump_press  = has(cmds, CMD_JUMP)  and not has(s.prev, CMD_JUMP)
      local right_press = has(cmds, CMD_RIGHT) and not has(s.prev, CMD_RIGHT)
      local left_press  = has(cmds, CMD_LEFT)  and not has(s.prev, CMD_LEFT)
      s.prev = cmds

      if p.grounded then s.jumps = 0 end

      -- Double jump: press #1 is the engine's ground jump; presses #2.. are ours.
      if want_dj and jump_press then
        s.jumps = s.jumps + 1
        if s.jumps >= 2 and s.jumps <= max_extra + 1 and not p.grounded then
          p:set_velocity(nil, -jump_v)
          if dbg then mod.log(("P%d: air jump #%d (vy=%.1f)"):format(i + 1, s.jumps - 1, -jump_v)) end
        elseif dbg then
          mod.log(("P%d: jump press (count=%d grounded=%s)"):format(i + 1, s.jumps, tostring(p.grounded)))
        end
      end

      -- Dash: double-tap LEFT/RIGHT -> a short, wall-checked position dash.
      if want_dash then
        local dir = right_press and 1 or (left_press and -1 or 0)
        if dir ~= 0 then
          if s.last_dir == dir and (tick - s.last_tap) <= dash_win then
            s.dash_dir, s.dash_frames = dir, 6
            s.last_dir, s.last_tap = 0, -1000
            if dbg then mod.log(("P%d: DASH dir=%d"):format(i + 1, dir)) end
          else
            s.last_dir, s.last_tap = dir, tick
          end
        end
      end
      if s.dash_frames > 0 then
        local nx = p.x + s.dash_dir * (dash_dist / 6.0)
        if not mod.game.is_solid(nx, p.y) then p:set_pos(nx, p.y) end
        s.dash_frames = s.dash_frames - 1
      end
    end
  end
end)
