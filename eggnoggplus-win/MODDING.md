# Eggnogg+ Modding Framework (Lua)

This project loads **Lua mods** from the `mods/` folder.

## Quick start

1. Copy the template:
   - `mods/_template/` → `mods/<your_mod_folder>/`
2. Edit `mods/<your_mod_folder>/mod.json`
3. Write your Lua in the file named by `"entry"`.
4. Launch the game (the framework is loaded via `SDL2.dll` proxy).

Logs go to `mods/modframework.log`.

---

## Folder layout

```
mods/
  my_mod/
    mod.json
    main.lua
    lib/...
    assets/...
```

Folders starting with `_` are ignored by the loader (useful for `_template`, `_disabled`, etc.).

---

## mod.json

Required:
- `id` (string): stable identifier (used in logs)
- `name` (string)
- `version` (string)
- `author` (string)
- `entry` (string): the Lua entry file (relative to the mod folder)

Recommended:
- `description` (string)
- `api_version` (number): framework API version the mod was written against
 - `config` (string): optional path to a **line-based config file** (relative to the mod folder). If present, the framework will surface it in the in-game **Options → Mods** menu.

Example:

```json
{
  "id": "cool_mod",
  "name": "Cool Mod",
  "version": "1.0.0",
  "author": "you",
  "description": "Adds something neat",
  "api_version": 1,
  "entry": "main.lua",
  "config": "config.cfg"
}
```

---

## The `mod` API (Lua)

Each mod runs in its **own Lua environment**.
- Mods do **not** share globals with each other.
- Standard Lua libraries are available (the mod env falls back to `_G`).

### Lifecycle

```lua
mod.on_load(function() end)
mod.on_unload(function() end)
```

### Per-frame callback

```lua
mod.on_frame(function() end)
```

Called once per frame (hooked from `SDL_GL_SwapWindow`).

### Event callback

```lua
mod.on_event(function(e)
  -- return true to consume the event
end)
```

`e` is a table:

| field | meaning |
|------:|---------|
| `type` | one of: `keydown`, `keyup`, `mousebuttondown`, `mousebuttonup`, `mousemotion` |
| `sym` | SDL keycode (for key events) |
| `scancode` | SDL scancode (for key events) |
| `mod` | SDL modifier bitmask (Shift/Ctrl/Alt, etc.) |
| `x`, `y` | mouse coordinates (for mouse events) |
| `button` | mouse button (for mouse button events) |

If any `on_event` handler returns `true`, the framework will **consume** the SDL event (the game won't see it).

### Logging

```lua
mod.log("info")
mod.warn("warn")
mod.error("error")
```

### Info / paths

```lua
local info = mod.info()     -- returns a table of metadata
local p = mod.get_path()    -- mod folder path
local q = mod.get_path("assets/sprite.png")

mod.dofile("lib/util.lua") -- run another file in *this mod's* environment
```

Convenience fields:

```lua
print(mod.id)
print(mod.name)
print(mod.version)
print(mod.framework_api) -- the framework API version
```

---

## Config (in-game editable settings)

If you add a `"config"` field to `mod.json`, the framework will read that file and automatically add settings to the in-game **Options → Mods** menu.

### Config file format

The file is a simple line-based format:

```text
# comments start with # (or //)

enabled: bool, true
max_lives: int, 3
speed: float, 1.25
jump_count: int[1,10], 3
gravity: float[0.2,2.5], 1.0
player_name: str, "Loaf"

reset_stats: action
big_red_button: action, "Reset Everything"
```

Rules:
- One setting per line.
- `key: type, value` for values.
- Optional numeric bounds are supported for `int`/`float` as `type[min,max]`.
  - Example: `lives: int[1,9], 3`
  - Example: `speed: float[0.25,3.0], 1.0`
  - Spaces are fine too: `speed: float[0.5, 10], 1.0`
- `key: action` (or `key: action, "Label"`) for buttons.
- Strings can be quoted (recommended if they contain spaces/commas).

### In-game behavior

- **bool**: click to toggle `true/false`.
- **int/float**: click to increment.
- **str**: click to edit; type; **Enter** saves; **Esc** cancels.
- **action**: click to trigger.

### The `config` API (Lua)

Each mod gets a `config` table in its environment:

```lua
local enabled = config.get("enabled", true)
config.set("max_lives", 5)

config.on_action("reset_stats", function()
  mod.log("reset stats pressed")
end)

print(config.path) -- absolute path to the config file (or "" if none)
```

Notes:
- `config.get(key, default)` returns `default` (or `nil`) if the key doesn't exist.
- `config.set(key, value)` returns `true/false`.
- `config.on_action(key, fn)` registers a handler for action buttons.

---

## UI API (draw anywhere, including main menu)

Each mod gets an immediate-mode UI helper at `mod.ui`.
Use it from `mod.on_frame(...)`.

### State helpers

```lua
local name = mod.ui.state_name() -- e.g. "main", "options", "game", "mods", ...
local ptr = mod.ui.state_ptr()    -- raw state pointer as a number
local is_main = mod.ui.is_state("main")
local is_any_menu = mod.ui.is_state("menu")
```

### Screen / mouse helpers

```lua
local w, h = mod.ui.screen_size()
local mx, my = mod.ui.mouse_pos()
```

### Layout + elements

```lua
-- layout(x, y, row_h, gap, width, text_scale)
mod.ui.layout(40, 40, 28, 6, 220, 1.0)

mod.ui.text("My Mod Overlay")

if mod.ui.button("hello_btn", "Click me") then
  mod.log("Button clicked")
end
```

Absolute positioning helpers:

```lua
mod.ui.text_at("Top right", 1040, 40, 1.0, 1.0, 1.0, 1.0)
if mod.ui.button_at("x", "X", 1180, 20, 40, 28) then
  mod.log("Close clicked")
end
```

Tile preview helper:

```lua
local tile = mod.game.room_tile(2, 4)
if tile and tile.exists then
  mod.ui.tile_preview(tile.id, tile.frame, tile.arg, 760, 128, 3.0, 3)
end
```

- `mod.ui.tile_preview(id, frame, arg, x, y [,scale [,tile_y]]) -> bool`
- Draws the exact rendered output for a tile byte triplet at screen position `x`,`y`.
- `scale` defaults to `1.0`.
- `tile_y` defaults to `0` and should usually be the source tile's zero-based row when previewing tiles whose draw callback depends on vertical position.
- Returns `false` if the preview could not be drawn.


### Native menu buttons (engine-backed)

`mod.ui.native_button(...)` creates an engine-backed menu button (works with selector swords/navigation).
After creation, you can adjust that native button by id in the current state:

```lua
mod.ui.native_button("my_btn", "My Button", 1.0, 3.0, 5.0, 5.0)

-- Move center (screen-space coordinates)
mod.ui.native_set_pos("my_btn", 640, 360)

-- Change grid layout defaults used for future recreation
mod.ui.native_set_layout("my_btn", 5.0, 5.0)

-- Resize clickable/rendered bounds
mod.ui.native_resize("my_btn", 240, 52)

-- Adjust native button label scale (x [,y]); useful when resizing buttons
mod.ui.native_set_text_scale("my_btn", 0.85, 0.85)

-- Hide/show from navigation + click handling
mod.ui.native_hide("my_btn", true)
mod.ui.native_hide("my_btn", false)

-- Remove mod-owned native button record for this state
mod.ui.native_remove("my_btn")
```

Notes:
- These functions only affect **mod-owned native buttons** created via `mod.ui.native_button`.
- To target vanilla/base-game buttons, use the pointer APIs in the next section (`find_button_by_label`, `button_*_ptr`).


### Engine button access (including vanilla buttons)

You can also target currently-active engine buttons (including base-game/vanilla menu buttons)
by finding a button pointer from its action pointer (most reliable) or label text, then mutating it:

```lua
local START_ACTION_PTR = 0x432440 -- from ghidra button_ex(..., "START", 0x432440)
local baseline = baseline or {}
if mod.ui.state_name() == "main" then
for nth = 1, 8 do
  local p = mod.ui.find_button_by_action_ptr(START_ACTION_PTR, nth)
  if not p then break end
  local x, y, w, h = mod.ui.button_rect_ptr(p)
  if x then
    local b = baseline[nth]
    if (not b) or b.ptr ~= p then
      b = { ptr = p, w = w, h = h }
      baseline[nth] = b
    end
    mod.ui.button_resize_ptr(p, b.w * 0.5, b.h * 0.5)
  end
end
end
```

Pointer APIs:
- `mod.ui.find_button_by_action_ptr(action_ptr [,nth]) -> ptr|nil`
- `mod.ui.find_button_by_label(label [,nth]) -> ptr|nil`
- `mod.ui.button_rect_ptr(ptr) -> x, y, w, h` (or `nil` if invalid)
- `mod.ui.button_set_pos_ptr(ptr, x, y) -> bool`
- `mod.ui.button_invoke_ptr(ptr [,event_code]) -> int | nil, err`
  - Calls the button's action callback.
  - When `event_code=3` (activation/click), it also follows the button's link target (state transition) if the engine would.
- `mod.ui.button_activate_ptr(ptr [,event_code]) -> int | nil, err`
  - Calls the game's framed button handler (used for hover/selector behavior; does **not** follow link targets).
- `mod.ui.button_set_label_ptr(ptr, label) -> bool`
- `mod.ui.button_resize_ptr(ptr, w, h [,shrink]) -> bool`
  - Tip: some menu screens recreate buttons every frame/state transition; call this from `on_frame` to keep your override applied.
  - Tip: use `mod.ui.state_name() == "main"` (not `is_state("main")`) for vanilla main-menu button mutations, and consider a short frame delay after entering main.
- `mod.ui.button_hide_ptr(ptr, hidden) -> bool`
- `mod.ui.button_remove_ptr(ptr) -> bool` (best-effort remove by hide + tiny size + offscreen)

API summary:
- `mod.ui.layout(x, y [,row_h [,gap [,width [,text_scale]]]])`
- `mod.ui.cursor([x [,y]]) -> x, y`
- `mod.ui.next_row([count])`
- `mod.ui.text(text [,r [,g [,b [,scale]]]])`
- `mod.ui.text_at(text, x, y [,scale [,r [,g [,b]]]])`
- `mod.ui.tile_preview(id, frame, arg, x, y [,scale [,tile_y]]) -> bool`
- `mod.ui.button(id, label [,w [,h]]) -> clicked`
- `mod.ui.button_at(id, label, x, y [,w [,h]]) -> clicked`

## Gameplay API (`mod.game`)

`mod.game` exposes low-level gameplay telemetry and command-bit input overrides.

```lua
local s = mod.game.snapshot(0) -- player 0 perspective
if s.in_game then
  mod.log(("p=(%.2f,%.2f) v=(%.2f,%.2f)"):format(s.player_x, s.player_y, s.player_vx, s.player_vy))
end
```

`snapshot(player_index [,include_tiles=true]) -> table`
- Returns a table with:
  - `player_x`, `player_y`, `player_vx`, `player_vy`
  - `enemy_dx`, `enemy_dy`, `enemy_vx`, `enemy_vy` (enemy relative position + velocity)
  - `player_has_sword`, `enemy_has_sword`
  - `start_countdown`, `end_countdown`, `leader_index` (0/1 or `nil` when unknown)
  - `nearest_sword_dx`, `nearest_sword_dy` (relative to player, or `nil` if none)
  - `tiles_of_current_room` (2D array of tile ids, or `nil` if not available)
  - `room_index`, `room_width`, `room_height`, `in_game`

`room_tile(col, row [,room_index]) -> table | nil`
- Returns exact bytes for a room-relative tile cell.
- `col`/`row` are 1-based room coordinates.
- `room_index` defaults to the current active room.
- Returned table fields:
  - `id`, `frame`, `arg`
  - `exists`
  - `room_index`, `x`, `y`, `global_x`, `global_y`
- Returns `nil` if the room dimensions are unavailable or the coordinate is out of bounds.

`poll_cmds(player_index [,mode=1]) -> int`
- Returns the game's raw command bitmask for the player.

`input_override(player_index, cmd_mask [,frames=1 [,replace=false]]) -> bool`
- Simulates inputs by overriding command bits in `main_player_poll_cmds`.
- `frames > 0`: apply for N polls then clear.
- `frames = 0`: clear override.
- `frames < 0`: hold until cleared.
- `replace=false` ORs bits with real input, `replace=true` fully replaces real input.

`input_clear(player_index) -> bool`
- Clears any active override for that player.

`input_status(player_index) -> { active, mask, frames, replace }`
- Returns current override state.

## Font glyph overlays (`mod.font`)

If you like the "icons-as-bytes" style for menu labels:

```lua
local AI = mod.font.alloc_glyph("icons/ai_8x8.png")
local label = "VS " .. string.char(AI)
```

The framework patches `data/font8x8.png` **as it is loaded** so the base game text renderer can draw your new 8×8 glyph.

- `mod.font.alloc_glyph(rel_path) -> byte | nil, err`
  - Allocates a free glyph in the range `0x80..0xFF` for your mod.
  - `rel_path` is relative to your mod folder.
  - The image must be a PNG sized **8×8**.
- `mod.font.register_glyph(byte, rel_path [,opts]) -> true | false, err`
  - Manually assigns a specific glyph byte (0..255). Useful for resource-pack style mods.
  - `opts.override = true` allows replacing a glyph owned by another mod.
- `mod.font.font_loaded() -> bool`
  - Returns true after the game has loaded `data/font8x8.png` once.
  - If you register/allocate **after** this, you'll need to restart for it to show (no hot-patch yet).
- `mod.ui.state_name() -> string`
- `mod.ui.state_ptr() -> number`
- `mod.ui.is_state(name) -> bool`
- `mod.ui.screen_size() -> w, h`
- `mod.ui.mouse_pos() -> x, y`
- `mod.ui.native_button(id, label, grid_x, grid_y [,layout_x [,layout_y]]) -> clicked`
- `mod.ui.native_set_pos(id, x, y) -> bool`
- `mod.ui.native_set_layout(id, layout_x, layout_y) -> bool`
- `mod.ui.native_resize(id, w, h [,shrink]) -> bool`
- `mod.ui.native_set_text_scale(id, sx [,sy]) -> bool`
- `mod.ui.native_hide(id, hidden) -> bool`
- `mod.ui.native_remove(id) -> bool`
- `mod.ui.find_button_by_action_ptr(action_ptr [,nth]) -> ptr|nil`
- `mod.ui.find_button_by_label(label [,nth]) -> ptr|nil`
- `mod.ui.button_rect_ptr(ptr) -> x, y, w, h`
- `mod.ui.button_set_pos_ptr(ptr, x, y) -> bool`
- `mod.ui.button_resize_ptr(ptr, w, h [,shrink]) -> bool`
- `mod.ui.button_hide_ptr(ptr, hidden) -> bool`
- `mod.ui.button_remove_ptr(ptr) -> bool`

---

## Notes for modders

### Isolated globals

Because each mod has its own environment table, you can safely do:

```lua
my_state = { counter = 0 }
```

…without clobbering other mods.

### Multi-file mods

Prefer `mod.dofile("lib/foo.lua")` so extra scripts run in the same environment.

---

## Notes for framework developers

Breaking changes to the API should bump `MOD_API_VERSION` in `lua_manager.c`.
