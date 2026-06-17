-- Example mod for the "options" (enum) config type.
--
-- The menu writes config values; mods read them back with config.get(). This
-- demo logs the current selection on load and again whenever you cycle the
-- "difficulty" option in Options -> Mods. (Use the "Hide log console" toggle in
-- that same menu to show the console and watch it live, or read mods/modframework.log.)

local last = nil

local function show()
  local diff  = config.get("difficulty", "normal")
  local greet = config.get("greeting", "hello")
  local msg = greet .. "! difficulty = " .. diff
  if config.get("loud", false) then msg = string.upper(msg) end
  mod.log(msg)
end

mod.on_load(function()
  mod.log("Loaded. Cycle 'difficulty' in Options -> Mods and watch the log.")
  last = config.get("difficulty", "normal")
  show()

  -- config.set validates against the declared choices:
  mod.log("set hard -> "    .. tostring(config.set("difficulty", "hard")))   -- true
  mod.log("set EASY -> "    .. tostring(config.set("difficulty", "EASY")))   -- true (case-insensitive)
  mod.log("set impossible-> ".. tostring(config.set("difficulty", "impossible"))) -- false
  config.set("difficulty", last) -- restore
end)

mod.on_frame(function()
  local diff = config.get("difficulty", "normal")
  if diff ~= last then
    last = diff
    show()
  end
end)
