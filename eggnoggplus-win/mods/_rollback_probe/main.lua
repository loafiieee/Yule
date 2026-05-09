local pending_probe = false
local was_in_game = false
local probe_run_count = 0
local next_probe_tick = 10
local stop_probing_for_current_entry = false

local masks = {
  0,
  mod.game.CMD_LEFT,
  mod.game.CMD_RIGHT,
  mod.game.CMD_JUMP,
  mod.game.CMD_ATTACK,
  mod.game.CMD_LEFT + mod.game.CMD_JUMP,
  mod.game.CMD_RIGHT + mod.game.CMD_ATTACK,
  mod.game.CMD_LEFT + mod.game.CMD_ATTACK
}

local function current_snapshot()
  return mod.game.snapshot(0, false)
end

local function in_game(snap)
  snap = snap or current_snapshot()
  return snap and snap.in_game
end

local function game_ready_for_probe()
  local snap = current_snapshot()
  local native_tick = mod.game.native_tick()
  if not in_game(snap) then
    return false
  end
  if snap.start_countdown ~= nil and snap.start_countdown > 0 then
    return false
  end
  if snap.end_countdown ~= nil and snap.end_countdown > 0 then
    return false
  end
  return native_tick ~= nil and native_tick >= next_probe_tick
end

local function should_reset_probe_state()
  local native_tick = mod.game.native_tick()
  return native_tick == nil or native_tick <= 1
end

local function probe_interval_ticks()
  return config.get("interval_ticks", 180)
end

local function probe_max_runs()
  return config.get("max_runs", 5)
end

local function probe_more_runs_allowed()
  local max_runs = probe_max_runs()
  return max_runs <= 0 or probe_run_count < max_runs
end

local function clear_input_state()
  mod.game.input_clear(0)
  mod.game.input_clear(1)
  mod.game.block_raw_input(0, false)
  mod.game.block_raw_input(1, false)
end

local function restore_blob(blob)
  clear_input_state()
  if not blob then
    return false, "missing rollback state blob"
  end
  local ok, err = mod.game.apply_full_state_blob(blob)
  if not ok then
    return false, err or "apply_full_state_blob failed during restore"
  end
  clear_input_state()
  return true
end

local function build_sequence(frame_count)
  local seq = {}
  for i = 1, frame_count do
    local a = masks[((i - 1) % #masks) + 1]
    local b = masks[(((i - 1) * 3 + 2) % #masks) + 1]
    seq[i] = { a, b }
  end
  return seq
end

local function checksum()
  local crc, err = mod.game.state_checksum()
  if crc == nil then
    return nil, err or "state_checksum failed"
  end
  return crc
end

local function run_sequence(seq)
  mod.game.block_raw_input(0, true)
  mod.game.block_raw_input(1, true)

  for i = 1, #seq do
    local step = seq[i]
    mod.game.set_input(0, step[1], 1, true)
    mod.game.set_input(1, step[2], 1, true)
    local ok, ran = mod.game.simulate_ticks(1, 0)
    if not ok or ran ~= 1 then
      clear_input_state()
      return nil, ("simulate_ticks failed at step %d (ok=%s ran=%s)"):format(i, tostring(ok), tostring(ran))
    end
  end

  clear_input_state()
  return checksum()
end

local function run_probe()
  local blob, err = mod.game.full_state_blob()
  local base_crc
  local replay1_crc
  local replay2_crc
  local restored_crc
  local ok

  if not blob then
    return false, err or "full_state_blob failed"
  end

  base_crc, err = checksum()
  if not base_crc then
    restore_blob(blob)
    return false, err
  end

  replay1_crc, err = run_sequence(build_sequence(config.get("frames", 48)))
  if not replay1_crc then
    restore_blob(blob)
    if err == "game tick simulation unavailable" or string.find(err or "", "game tick simulation unavailable", 1, true) then
      return "defer"
    end
    return false, err
  end

  ok, err = restore_blob(blob)
  if not ok then
    return false, err or "restore failed after replay 1"
  end

  restored_crc, err = checksum()
  if not restored_crc then
    restore_blob(blob)
    return false, err
  end
  if restored_crc ~= base_crc then
    restore_blob(blob)
    return false, ("restore mismatch: base=%u restored=%u"):format(base_crc, restored_crc)
  end

  replay2_crc, err = run_sequence(build_sequence(config.get("frames", 48)))
  if not replay2_crc then
    restore_blob(blob)
    if err == "game tick simulation unavailable" or string.find(err or "", "game tick simulation unavailable", 1, true) then
      return "defer"
    end
    return false, err
  end

  ok, err = restore_blob(blob)
  if not ok then
    return false, err or "restore failed after replay 2"
  end

  if replay1_crc ~= replay2_crc then
    restore_blob(blob)
    return false, ("replay mismatch: replay1=%u replay2=%u"):format(replay1_crc, replay2_crc)
  end

  return true, ("probe passed: size=%d base=%u replay=%u frames=%d"):format(
    mod.game.full_state_size(),
    base_crc,
    replay1_crc,
    config.get("frames", 48)
  )
end

mod.on_load(function()
  mod.log("rollback probe will auto-run repeatedly during gameplay")
end)

mod.on_frame(function()
  local snap = current_snapshot()
  local now_in_game = in_game(snap)

  if not now_in_game then
    was_in_game = false
    if should_reset_probe_state() then
      pending_probe = false
      probe_run_count = 0
      next_probe_tick = 10
      stop_probing_for_current_entry = false
    end
    return
  end

  if not was_in_game then
    was_in_game = true
    pending_probe = true
    probe_run_count = 0
    next_probe_tick = 10
    stop_probing_for_current_entry = false
  end

  if stop_probing_for_current_entry or not probe_more_runs_allowed() then
    return
  end

  if not pending_probe then
    local native_tick = mod.game.native_tick()
    if native_tick ~= nil and native_tick >= next_probe_tick then
      pending_probe = true
    end
  end

  if not pending_probe or not game_ready_for_probe() then
    return
  end

  pending_probe = false
  local run_number = probe_run_count + 1
  local ok, msg = run_probe()
  if ok == "defer" then
    pending_probe = true
    return
  end

  if ok then
    probe_run_count = run_number
    next_probe_tick = (mod.game.native_tick() or next_probe_tick) + probe_interval_ticks()
    mod.log(("run %d passed: %s"):format(run_number, msg))
  else
    stop_probing_for_current_entry = true
    mod.error(("run %d failed: %s"):format(run_number, msg))
  end
end)
