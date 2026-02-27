local START_ACTION_PTR = 0x432440

local online_menu_open = false
local auth_user = nil
local queue_mode = nil
local queue_started_at = 0
local status_message = "Not logged in"

local server_url = "http://127.0.0.1:8787"
local strict_mod_policy = true

local users = {
  {
    id = "u_alpha",
    name = "Alpha",
    elo = 1000,
    friends = { "u_bravo" }
  },
  {
    id = "u_bravo",
    name = "Bravo",
    elo = 1000,
    friends = {}
  }
}

local friend_directory = {
  u_alpha = { { id = "u_bravo", name = "Bravo", online = true }, { id = "u_charlie", name = "Charlie", online = false } },
  u_bravo = { { id = "u_alpha", name = "Alpha", online = true }, { id = "u_delta", name = "Delta", online = true } },
}

local pending_outbound = {}

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
  if not strict_mod_policy then
    return true
  end

  -- Prototype gate: the framework does not yet expose runtime mod enumeration
  -- inside Lua, so this is currently advisory only.
  status_message = "Ranked policy ON: run with only online_play enabled"
  return true
end

local function draw_online_hub()
  local sw, sh = mod.ui.screen_size()
  local panel_w = 700
  local panel_h = 430
  local x = (sw - panel_w) * 0.5
  local y = (sh - panel_h) * 0.5

  mod.ui.layout(x + 20, y + 20, 30, 6, panel_w - 40, 1.0)
  mod.ui.text("ONLINE PLAY (PROTOTYPE)")
  mod.ui.text("Server: " .. tostring(server_url), 0.8, 0.8, 1.0, 0.9)
  mod.ui.text("Status: " .. tostring(status_message), 1.0, 1.0, 0.7, 0.9)
  mod.ui.next_row()

  if not auth_user then
    mod.ui.text("Login:")
    if mod.ui.button("login_alpha", "Login as Alpha") then
      auth_user = users[1]
      status_message = "Logged in as " .. auth_user.name
    end
    if mod.ui.button("login_bravo", "Login as Bravo") then
      auth_user = users[2]
      status_message = "Logged in as " .. auth_user.name
    end
  else
    mod.ui.text("Logged in as: " .. auth_user.name .. " (ELO " .. tostring(auth_user.elo) .. ")")
    if mod.ui.button("logout", "Logout") then
      auth_user = nil
      queue_mode = nil
      status_message = "Not logged in"
    end
  end

  mod.ui.next_row()

  local ranked_disabled = (not auth_user)
  local casual_disabled = (not auth_user)

  if ranked_disabled then
    mod.ui.text("Ranked requires login.", 1.0, 0.6, 0.6, 0.9)
  end

  if mod.ui.button("queue_ranked", "Ranked Queue") then
    if auth_user then
      if enforce_ranked_mod_policy() then
        queue_mode = "ranked"
        queue_started_at = os.time()
        status_message = "Searching ranked match near ELO " .. tostring(auth_user.elo)
      end
    end
  end

  if mod.ui.button("queue_casual", "Casual Queue") then
    if auth_user then
      queue_mode = "casual"
      queue_started_at = os.time()
      status_message = "Searching casual match"
    end
  end

  if queue_mode and mod.ui.button("leave_queue", "Leave Queue") then
    queue_mode = nil
    status_message = "Queue cancelled"
  end

  mod.ui.next_row()
  mod.ui.text("Friends:")

  if auth_user then
    local friends = friend_directory[auth_user.id] or {}
    if #friends == 0 then
      mod.ui.text("No friends yet.", 0.8, 0.8, 0.8, 0.9)
    else
      for i, f in ipairs(friends) do
        local line = f.name .. (f.online and " (online)" or " (offline)")
        mod.ui.text(line)
        if f.online and mod.ui.button("challenge_" .. tostring(i), "Challenge") then
          status_message = "Challenge sent to " .. f.name
        end
      end
    end

    if mod.ui.button("add_friend", "Add Demo Friend") then
      pending_outbound[#pending_outbound + 1] = "u_demo_" .. tostring(#pending_outbound + 1)
      status_message = "Friend request sent (prototype)"
    end
  else
    mod.ui.text("Login to see friends.", 0.8, 0.8, 0.8, 0.9)
  end

  mod.ui.next_row(2)
  if mod.ui.button("close_online_menu", "Back") then
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
