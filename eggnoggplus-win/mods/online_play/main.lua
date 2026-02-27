local START_ACTION_PTR = 0x432440

local online_menu_open = false
local auth_user = nil
local queue_mode = nil
local queue_started_at = 0
local queue_eta = 0
local status_message = "Not logged in"

local server_url = "http://127.0.0.1:8787"
local strict_mod_policy = true

local users = {
  { id = "u_alpha", name = "Alpha", elo = 1000 },
  { id = "u_bravo", name = "Bravo", elo = 1000 },
}

local friend_directory = {
  u_alpha = {
    { id = "u_bravo", name = "Bravo", online = true },
    { id = "u_charlie", name = "Charlie", online = false },
    { id = "u_delta", name = "Delta", online = true },
  },
  u_bravo = {
    { id = "u_alpha", name = "Alpha", online = true },
    { id = "u_echo", name = "Echo", online = true },
  },
}

local baseline = {
  captured = false,
  from_initial = false,
  x = nil,
  y = nil,
  w = nil,
  h = nil,
}

local function in_main_menu_state(st)
  return st == "main" or st == "main_initial"
end

local function read_config()
  server_url = config.get("server_url", server_url)
  strict_mod_policy = config.get("strict_mod_policy", true)
end

local function find_start_ptr()
  return mod.ui.find_button_by_action_ptr(START_ACTION_PTR)
end

local function capture_start_baseline(state_name, ptr)
  if not ptr then return end
  local want_capture = (not baseline.captured) or (baseline.from_initial and state_name == "main")
  if not want_capture then return end

  local x, y, w, h = mod.ui.button_rect_ptr(ptr)
  if x and y and w and h and w > 20.0 and h > 10.0 then
    baseline.x, baseline.y, baseline.w, baseline.h = x, y, w, h
    baseline.captured = true
    baseline.from_initial = (state_name == "main_initial")
    if state_name == "main" then baseline.from_initial = false end
  end
end

local function menu_reset_baseline()
  baseline.captured = false
  baseline.from_initial = false
  baseline.x, baseline.y, baseline.w, baseline.h = nil, nil, nil, nil
end

local function enforce_ranked_mod_policy()
  if not strict_mod_policy then return true end
  status_message = "Ranked policy: run only online-play + cosmetic-safe mods"
  return true
end

local function draw_dark_backdrop()
  local w, h = mod.ui.screen_size()
  -- A wide, dark-ish framed slab to separate the online hub from vanilla buttons.
  -- (Immediate-mode button gives us a visible panel-like primitive in this API.)
  mod.ui.button_at("online_backdrop", "", w * 0.5 - 420, h * 0.5 - 255, 840, 510)
end

local function queue_tick()
  if not queue_mode then return end
  local elapsed = os.time() - queue_started_at
  if elapsed >= queue_eta then
    status_message = "Match found! (prototype)"
    queue_mode = nil
  else
    status_message = ("Searching %s... %ds"):format(queue_mode, elapsed)
  end
end

local function draw_online_hub()
  local w, h = mod.ui.screen_size()
  local cx = w * 0.5
  local top_y = h * 0.5 - 220

  draw_dark_backdrop()

  mod.ui.text_at("PLAY ONLINE", cx - 110, top_y + 8, 1.3, 1.0, 1.0, 1.0)
  mod.ui.text_at("Server: " .. tostring(server_url), cx - 170, top_y + 40, 0.9, 0.80, 0.85, 1.0)
  mod.ui.text_at("Status: " .. tostring(status_message), cx - 170, top_y + 62, 0.9, 1.0, 1.0, 0.75)

  -- Vanilla-like stacked action buttons in the center lane.
  local btn_w = 260
  local btn_h = 40
  local by = top_y + 98

  if not auth_user then
    if mod.ui.button_at("login_alpha", "Login: Alpha", cx - btn_w - 10, by, btn_w, btn_h) then
      auth_user = users[1]
      status_message = "Logged in as " .. auth_user.name
    end
    if mod.ui.button_at("login_bravo", "Login: Bravo", cx + 10, by, btn_w, btn_h) then
      auth_user = users[2]
      status_message = "Logged in as " .. auth_user.name
    end
  else
    mod.ui.text_at(
      ("Signed in: %s  ELO: %d"):format(auth_user.name, auth_user.elo),
      cx - 170,
      by + 8,
      0.95,
      0.95,
      1.0,
      0.95
    )
    if mod.ui.button_at("logout", "Logout", cx + 110, by, 160, btn_h) then
      auth_user = nil
      queue_mode = nil
      status_message = "Not logged in"
    end
  end

  local qy = by + 58
  if mod.ui.button_at("queue_ranked", "Ranked", cx - btn_w - 10, qy, btn_w, btn_h) then
    if auth_user and enforce_ranked_mod_policy() then
      queue_mode = "ranked"
      queue_started_at = os.time()
      queue_eta = 7
      status_message = "Searching ranked near your ELO"
    end
  end

  if mod.ui.button_at("queue_casual", "Casual", cx + 10, qy, btn_w, btn_h) then
    if auth_user then
      queue_mode = "casual"
      queue_started_at = os.time()
      queue_eta = 4
      status_message = "Searching casual"
    end
  end

  if mod.ui.button_at("leave_queue", "Leave Queue", cx - 130, qy + 52, 260, btn_h) then
    queue_mode = nil
    status_message = "Queue cancelled"
  end

  -- Friends section: compact two-column chips instead of list-like rows.
  local fy = qy + 112
  mod.ui.text_at("FRIENDS", cx - 60, fy, 1.0, 0.9, 0.95, 1.0)

  if auth_user then
    local friends = friend_directory[auth_user.id] or {}
    local chip_w = 250
    local chip_h = 34
    for i, f in ipairs(friends) do
      local col = ((i - 1) % 2)
      local row = math.floor((i - 1) / 2)
      local bx = cx - 260 + (col * 270)
      local by2 = fy + 26 + (row * 42)
      local label = f.name .. (f.online and "  • online" or "  • offline")
      if mod.ui.button_at("friend_" .. tostring(i), label, bx, by2, chip_w, chip_h) then
        if f.online then
          status_message = "Challenge sent to " .. f.name
        else
          status_message = f.name .. " is offline"
        end
      end
    end

    if mod.ui.button_at("add_friend", "Add Friend", cx - 130, fy + 160, 260, 34) then
      status_message = "Friend request sent (prototype)"
    end
  else
    mod.ui.text_at("Login to view and challenge friends.", cx - 190, fy + 28, 0.9, 0.8, 0.8, 0.8)
  end

  if mod.ui.button_at("close_online_menu", "Back", cx - 90, top_y + 470, 180, 32) then
    online_menu_open = false
  end
end

mod.on_load(function()
  read_config()
  mod.log("Online Play Prototype loaded")
end)

mod.on_event(function(e)
  if e.type == "keydown" and e.sym == 27 and online_menu_open then
    online_menu_open = false
    return true
  end

  if online_menu_open and (e.type == "mousebuttondown" or e.type == "mousebuttonup") then
    return true
  end

  return false
end)

mod.on_frame(function()
  read_config()
  queue_tick()

  local st = mod.ui.state_name()
  if in_main_menu_state(st) then
    local start_ptr = find_start_ptr()
    capture_start_baseline(st, start_ptr)

    local clicked_online = mod.ui.native_button("play_online", "Play Online", 1.0, 5.2, 3.0, 6.7)

    if baseline.captured then
      mod.ui.native_resize("play_online", baseline.w, baseline.h)
      mod.ui.native_set_pos("play_online", baseline.x, baseline.y + baseline.h + 10)
    end

    if clicked_online then
      online_menu_open = true
      status_message = auth_user and ("Logged in as " .. auth_user.name) or "Not logged in"
    end
  else
    menu_reset_baseline()
    online_menu_open = false
  end

  if online_menu_open then
    draw_online_hub()
  end
end)
