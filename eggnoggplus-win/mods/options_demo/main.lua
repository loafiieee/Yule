-- Options Demo: shows the options[...] (enum) config type in action.
-- Cycle "difficulty" / "greeting" in Options -> Mods, or via the console:
--   mods.config.set options_demo difficulty hard
-- This mod only logs the chosen values; it does not touch gameplay.

local last_difficulty = nil
local last_greeting = nil

local function report()
  local difficulty = config.get("difficulty", "normal")
  local greeting = config.get("greeting", "hello")
  if difficulty ~= last_difficulty or greeting ~= last_greeting then
    last_difficulty = difficulty
    last_greeting = greeting
    mod.log("difficulty=" .. tostring(difficulty) .. " greeting=" .. tostring(greeting))
  end
end

mod.on_load(function()
  mod.log("Loaded options demo")
  report()
end)

mod.on_frame(function()
  -- Poll so a change made from the menu/console is logged once.
  report()
end)
