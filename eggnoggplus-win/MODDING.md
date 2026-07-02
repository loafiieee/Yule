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
- `api_version` (number): framework API version the mod was written against.
  - If this does not match the current framework version, the mod is rejected by default.
  - You can explicitly override this by setting `"allow_api_mismatch": true` (not recommended for released mods).
  - For local testing only, you can also set environment variable `LUNA_ALLOW_API_MISMATCH=1`.
- `config` (string): optional path to a **line-based config file** (relative to the mod folder). If present, the framework will surface it in the in-game **Options → Mods** menu.
- `storage` (string): optional path to a **line-based persistent storage file** (relative to the mod folder). Defaults to `storage.cfg`.
- `binds` (string): optional path to the mod bind file (relative to the mod folder). Defaults to `binds.cfg`.
- `priority` (number): load-order tie breaker; higher numbers load earlier.
- `depends` (array of dependency specs): hard dependency list. Mod will not load unless all dependencies load and satisfy version constraints.
- `optional_deps` (array of dependency specs): soft dependencies. If present and compatible, they affect load order but do not block loading.
- `conflicts` (array of mod ids): incompatible mods. When conflicts occur, deterministic precedence decides which mod is kept.
- `load_after` / `load_before` (array of mod ids): soft ordering constraints.

Dependency spec formats:
- `"other_mod"` (any version)
- `"other_mod@>=1.2.0 <2.0.0"`
- `"other_mod>=1.2.0"`
- operators: `=`, `==`, `!=`, `>`, `>=`, `<`, `<=`, `^`, `~`

Manifest parsing is strict JSON (full-file parse, explicit type validation). Invalid `mod.json` now fails with a concrete error in `mods/modframework.log`.

Example:

```json
{
  "id": "cool_mod",
  "name": "Cool Mod",
  "version": "1.0.0",
  "author": "you",
  "description": "Adds something neat",
  "api_version": 1,
  "priority": 10,
  "depends": ["shared_lib@>=1.0.0 <2.0.0"],
  "optional_deps": ["ui_pack@^1.3.0"],
  "load_after": ["ui_pack"],
  "conflicts": ["other_cool_mod"],
  "entry": "main.lua",
  "config": "config.cfg",
  "storage": "storage.cfg"
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

`on_unload` fires when the mod is disabled, hot reloaded, or the framework shuts down.

### Per-frame callback

```lua
mod.on_frame(function() end)
```

Called once per rendered frame (hooked from `SDL_GL_SwapWindow`). Good for UI, overlays, and presentation logic.

### Per-tick callback

```lua
mod.on_tick(function() end)
```

Called once per gameplay update (hooked from the native `game_update`) **before** the game consumes player commands for that tick. Use this for bots, deterministic control logic, and data capture.

### Event callback

```lua
mod.on_event(function(e)
  -- return true to consume the event
end)
```

`e` is a table:

| field | meaning |
|------:|---------|
| `type` | one of: `keydown`, `keyup`, `textinput`, `mousebuttondown`, `mousebuttonup`, `mousemotion`, `mousewheel`, `controlleraxismotion`, `controllerbuttondown`, `controllerbuttonup`, `joyaxismotion`, `joybuttondown`, `joybuttonup`, `joyhatmotion`, `quit` |
| `sym` | Primary event code. For keyboard: SDL keycode. For controller/joy button/axis/hat events: button/axis/hat id. For `textinput`: first byte of input text. |
| `scancode` | SDL scancode (keyboard events). |
| `mod` | SDL modifier bitmask (keyboard events). |
| `x`, `y` | Event payload values by type. Mouse: coordinates. Mousewheel: wheel delta `(x,y)`. Controller/Joy: device `which` in `x`, event value/state in `y`. |
| `button` | Mouse button for mouse button events. For mousewheel: wheel direction flag. |

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
difficulty: options[easy, normal, hard], normal

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
- `key: options[a, b, c], default` for a fixed list of choices (an enum/dropdown).
  - The value is always one of the listed options; the default after the comma must be one of them (otherwise the first option is used).
  - Example: `quality: options[low, medium, high], medium`
  - Read it from Lua with `config.get("quality")` (returns the chosen string).
- `key: action` (or `key: action, "Label"`) for buttons.
- Strings can be quoted (recommended if they contain spaces/commas).

### In-game behavior

- **bool**: click to toggle `true/false`.
- **int/float**: click to increment.
- **str**: click to edit; type; **Enter** saves; **Esc** cancels.
- **options**: click (or press right) to cycle to the next choice; press left for the previous. Shown as `< value >`.
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

## Named input bindings (`mod.input`)

Mods can register named keyboard bindings and let players rebind them in the in-game **Options → Mods** menu or through the console.

Example:

```lua
mod.input.bind("dash", "space", "Dash")

mod.on_frame(function()
  if mod.input.pressed("dash") then
    mod.log("dash pressed")
  end
end)
```

API:

```lua
mod.input.bind(key, default_binding, label)  -- registers or updates a named bind
mod.input.get(key)                           -- returns current binding name
mod.input.set(key, binding_name)             -- returns true/false[, err]
mod.input.clear(key)                         -- unbinds the key
mod.input.down(key)                          -- true while held
mod.input.pressed(key)                       -- true on the press edge
mod.input.released(key)                      -- true on the release edge
mod.input.list()                             -- returns an array of bind tables
```

Notes:
- `key` is the stable internal id used by your mod.
- `default_binding` is optional; examples: `"space"`, `"a"`, `"left"`, `"escape"`, `"F1"`.
- `label` is the user-facing name shown in the mods menu.
- `mod.input.list()` returns entries with `key`, `label`, `binding`, and `conflict`.
- Bind overrides are stored in the mod's bind file (default `binds.cfg`).
- The mods menu supports live key capture: select a bind row, press `Enter`, then press the new key.

---

## Persistent Storage (`storage` / `mod.storage`)

Each mod gets a persistent key/value store backed by a file (default: `storage.cfg`):

```lua
local runs = storage.get("runs", 0)
storage.set("runs", runs + 1)

if storage.schema() < 2 then
  local ok, err = storage.migrate(2, function(from_schema, to_schema)
    if from_schema < 1 then storage.set("coins", 0) end
    if from_schema < 2 then storage.set("xp", 0) end
    return true
  end)
  if not ok then mod.error("storage migrate failed: " .. tostring(err)) end
end

print(storage.path)
```

Supported value types: `bool`, `number`, `string`, and `nil` (delete).

API:
- `storage.get(key [,default]) -> value`
- `storage.set(key, value) -> true | false, err`
- `storage.delete(key) -> bool`
- `storage.save() -> true | false, err`
- `storage.schema() -> int`
- `storage.set_schema(version) -> true | false, err`
- `storage.migrate(target_schema, fn(from_schema, target_schema)) -> true, schema | false, err`

---

## Mod Interop (`mod.interop`)

Use this to share small service tables between mods with version checks.

```lua
-- provider mod
mod.interop.provide("my_mod:api", "1.0.0", {
  ping = function() return "pong" end
})

-- consumer mod
local api, ver = mod.interop.require("my_mod:api", ">=1.0.0 <2.0.0")
if api then
  mod.log("interop version=" .. ver .. " ping=" .. tostring(api.ping()))
end
```

API:
- `mod.interop.provide(namespace, version, table) -> true | false, err`
- `mod.interop.require(namespace [,range]) -> table, version | nil, err`

Namespaces are global, so use a unique prefix like `"your_mod_id:service_name"`.

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
local left_down, left_pressed, right_down, right_pressed = mod.ui.mouse_buttons()
local rmb_down, rmb_pressed = mod.ui.mouse_buttons(3)
```

### Overlay + drawing primitives

Use `begin_overlay()` / `end_overlay()` when drawing HUDs or custom states from `mod.on_frame(...)`. They flush the engine sprite batch before and after the mod draw calls so your UI appears above the game/menu render.

```lua
mod.ui.begin_overlay()
mod.ui.rect(40, 40, 260, 120, { color = {0.05, 0.06, 0.07, 0.90} })
mod.ui.border(40, 40, 260, 120, { line_w = 2, color = {1.0, 0.78, 0.25, 1.0} })
mod.ui.line(40, 82, 300, 82, { line_w = 1, color = {0.3, 0.35, 0.4, 1.0} })
local tw, th = mod.ui.measure_text("Cosmetics", 1.0)
local rendered_scale = mod.ui.readable_scale(1.0)
mod.ui.text_at("Cosmetics", 52, 70, 1.0, 0.95, 0.95, 0.95)
mod.ui.end_overlay()
```

Primitive helpers:
- `mod.ui.rect(x, y, w, h, opts)` draws a filled rectangle. `opts.color`, `opts.bg`, or `opts.fill` can be `{r,g,b,a}`.
- `mod.ui.border(x, y, w, h, opts)` draws a rectangle outline. `opts.line_w` and `opts.color` are supported.
- `mod.ui.line(x1, y1, x2, y2, opts)` draws a line. `opts.line_w` and `opts.color` are supported.
- `mod.ui.measure_text(text [,scale]) -> w, h` returns the approximate rendered bounds after the framework's readable text scaling.
- `mod.ui.readable_scale([scale]) -> rendered_scale` returns the effective native text scale for the current window size.
- `mod.ui.wrap_text(text, max_w [,scale]) -> lines` splits text into measured lines that fit `max_w`.
- `mod.ui.text_wrapped(text, x, y, w [,opts]) -> height, line_count` draws readable wrapped text. `opts.scale`, `opts.color`, and `opts.line_gap` are supported.
- `mod.ui.hitbox(id, x, y, w, h [,button]) -> hovered, clicked, down` registers an invisible interactive region. `button` uses SDL button ids: `1` left, `2` middle, `3` right.
- `mod.ui.fill_rect(...)` and `mod.ui.stroke_rect(...)` remain as compatibility aliases.

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

Styled immediate-mode widgets:

```lua
mod.ui.push_style({
  bg = {0.08, 0.09, 0.10, 0.92},
  fg = {0.92, 0.94, 0.96, 1.0},
  accent = {1.0, 0.78, 0.25, 1.0},
})

category = select(1, mod.ui.tabs("category", {
  { id = "hats", label = "Hats" },
  { id = "masks", label = "Masks" },
}, category, { x = 48, y = 96, w = 240, h = 28 }))

color_id = select(1, mod.ui.swatch_grid("skin_colors", colors, color_id, {
  x = 48, y = 140,
  cols = 8,
  cell = 22,
}))

scale = select(1, mod.ui.slider("preview_scale", scale, 1.0, 4.0, {
  x = 48, y = 220,
  w = 220,
  step = 0.25,
}))

mod.ui.pop_style()
```

Widget helpers return `value, changed` where applicable:
- `mod.ui.icon_button(id, icon, x, y, w, h [,opts]) -> clicked, hovered`
- `mod.ui.tabs(id, items, selected, opts) -> selected, changed`
- `mod.ui.segmented(id, items, selected, opts) -> selected, changed`
- `mod.ui.swatch_grid(id, colors, selected, opts) -> selected, changed`
- `mod.ui.item_grid(id, items, selected, opts) -> selected, changed`
- `mod.ui.slider(id, value, min, max, opts) -> value, changed`
- `mod.ui.checkbox(id, value, opts) -> value, changed`
- `mod.ui.tooltip(text [,opts])`

Style helpers:
- `mod.ui.push_style(style)`, `mod.ui.pop_style()`
- `mod.ui.theme(style)` merges into the active style.
- `mod.ui.set_theme("default_dark")`
- `mod.ui.current_style() -> table`

Sprite helpers:

```lua
local id = mod.ui.sprite_id("sprites", 12) -- base spritesheet + 12
mod.ui.draw_sprite(id, 400, 240, {
  flip = false,
  scale = 2.0,
  tint = {1.0, 0.8, 0.8, 1.0},
  layer = 0,
})
```

- `mod.ui.sheet_base(name) -> sprite_base | nil`
  - Known names: `sprites`, `tiles`, `misc`, `glyphs` (+ `data/*.png` aliases), plus any mod-owned sheet loaded with `mod.assets.load_spritesheet`.
- `mod.ui.sprite_id(sheet_or_base, index) -> sprite_id | nil, err`
  - `sheet_or_base` can be a sheet name or numeric base id.
- `mod.ui.draw_sprite(sprite_id_or_frame, x, y [,opts]) -> bool`
  - Supports `flip`, `scale`/`scale_x`/`scale_y`, `tint` (`{r,g,b,a}`), `r/g/b/a`, `angle`, `layer`.

Asset helpers:

```lua
local sheet = mod.assets.load_spritesheet("my_icon", "assets/icon.png", {
  cell_w = 32,
  cell_h = 32,
})
local sprite = mod.assets.sprite_id("my_icon", 0)
mod.ui.draw_sprite(sprite, 400, 240, { scale = 1.0 })
```

- `mod.assets.load_spritesheet(id, rel_path [,opts]) -> sheet | nil, err`
  - Registers a PNG relative to the mod folder and rebuilds the live atlas so it can be drawn immediately.
  - Options: `cell_w`, `cell_h`, `padding`, `flags`, `force`.
  - Returns `{ id, path, full_path, base_id, count, cell_w, cell_h, padding, flags }`.
  - If the engine graphics atlas is not ready yet, the sheet is registered and the call returns `nil, err`; call again on a later frame or use `mod.assets.info(id)`.
- `mod.assets.begin_batch() -> true | nil, err`
- `mod.assets.end_batch() -> true | nil, err`
- `mod.assets.cancel_batch() -> true | nil, err`
  - Use a batch when registering several related sheets. While a batch is open, `load_spritesheet` records metadata without rebuilding the atlas. `end_batch` rebuilds once for the whole group.
  - After `end_batch`, call `mod.assets.info(id)` or `mod.assets.sprite_id(id, index)` to fetch the packed `base_id`/sprite IDs.
  - If a sheet registration fails before `end_batch`, call `cancel_batch`. If `end_batch` fails because the atlas is not ready, retry `end_batch` on a later frame.

```lua
local ok, err = mod.assets.begin_batch()
if ok then
  local base = mod.assets.load_spritesheet("body_base", "assets/body_base.png", { cell_w = 64, cell_h = 64 })
  local skin = mod.assets.load_spritesheet("body_skin", "assets/body_skin.png", { cell_w = 64, cell_h = 64 })
  local clothes = mod.assets.load_spritesheet("body_clothes", "assets/body_clothes.png", { cell_w = 64, cell_h = 64 })
  if base and skin and clothes then
    ok, err = mod.assets.end_batch()
  else
    ok = false
    mod.assets.cancel_batch()
  end
  if ok then
    local base_info = mod.assets.info("body_base")
    local sprite = mod.assets.sprite_id("body_base", 0)
  end
end
```

- `mod.assets.sprite_id(id [,index=0]) -> sprite_id | nil, err`
- `mod.assets.info([id]) -> sheet | nil` or a list of loaded sheets when `id` is omitted.

Mouse cursor helpers:

```lua
-- misc[7] is the vanilla menu mouse cursor, the top-right 16x16 sprite in data/misc.png.
mod.ui.draw_cursor()
```

- `mod.ui.cursor_sprite([index=7]) -> sprite_id | nil`
- `mod.ui.draw_cursor([opts]) -> bool`
  - Draws the vanilla cursor by default.
  - Supports `sprite`, `index`, `scale`, `size`, `hot_x`, `hot_y`, `tint`, and `layer`.
  - Registered custom states draw the default cursor automatically.
  - `define_state` can disable it with `cursor = false` or replace it with custom cursor options.

Tile preview helper:

```lua
local tile = mod.game.room_tile(2, 4)
if tile and tile.exists then
  mod.ui.tile_preview(tile.id, tile.frame, tile.arg, 760, 128, 3.0, 3)
end
```

- `mod.ui.tile_preview(id, frame, arg, x, y [,scale [,tile_y]]) -> bool`
- Draws a preview for a tile byte triplet at screen position `x`,`y`.
- `scale` defaults to `1.0`.
- `tile_y` defaults to `0` and should usually be the source tile's zero-based row when previewing tiles whose draw callback depends on vertical position.
- Current fallback previews the base `tiles` spritesheet entry by `id`; callback-specific animated/special tiles may be approximate until native tile-renderer routing is added.
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
- `mod.ui.state_name() -> string`
- `mod.ui.state_ptr() -> number`
- `mod.ui.is_state(name) -> bool`
- `mod.ui.screen_size() -> w, h`
- `mod.ui.mouse_pos() -> x, y`
- `mod.ui.mouse_buttons([button]) -> left_down, left_pressed, right_down, right_pressed, middle_down, middle_pressed` or, with `button`, `down, pressed`
- `mod.ui.begin_overlay()`
- `mod.ui.end_overlay()`
- `mod.ui.flush()`
- `mod.ui.rect(x, y, w, h, opts)`
- `mod.ui.border(x, y, w, h, opts)`
- `mod.ui.line(x1, y1, x2, y2, opts)`
- `mod.ui.measure_text(text [,scale]) -> w, h`
- `mod.ui.readable_scale([scale]) -> rendered_scale`
- `mod.ui.text_scale_factor([scale]) -> rendered_scale` (alias)
- `mod.ui.wrap_text(text, max_w [,scale]) -> lines`
- `mod.ui.text_wrapped(text, x, y, w [,opts]) -> height, line_count`
- `mod.ui.hitbox(id, x, y, w, h [,button]) -> hovered, clicked, down`
- `mod.ui.layout(x, y [,row_h [,gap [,width [,text_scale]]]])`
- `mod.ui.cursor([x [,y]]) -> x, y`
- `mod.ui.next_row([count])`
- `mod.ui.text(text [,r [,g [,b [,scale]]]])`
- `mod.ui.text_at(text, x, y [,scale [,r [,g [,b]]]])`
- `mod.ui.tile_preview(id, frame, arg, x, y [,scale [,tile_y]]) -> bool`
- `mod.ui.cursor_sprite([index]) -> sprite_id | nil`
- `mod.ui.draw_cursor([opts]) -> bool`
- `mod.ui.button(id, label [,w [,h]]) -> clicked`
- `mod.ui.button_at(id, label, x, y [,w [,h]]) -> clicked`
- `mod.ui.icon_button(id, icon, x, y, w, h [,opts]) -> clicked, hovered`
- `mod.ui.tabs(id, items, selected, opts) -> selected, changed`
- `mod.ui.segmented(id, items, selected, opts) -> selected, changed`
- `mod.ui.swatch_grid(id, colors, selected, opts) -> selected, changed`
- `mod.ui.item_grid(id, items, selected, opts) -> selected, changed`
- `mod.ui.slider(id, value, min, max, opts) -> value, changed`
- `mod.ui.checkbox(id, value, opts) -> value, changed`
- `mod.ui.tooltip(text [,opts])`
- `mod.ui.push_style(style)`, `mod.ui.pop_style()`
- `mod.ui.theme(style)`, `mod.ui.set_theme(name)`, `mod.ui.current_style()`

Custom state API:
- `mod.ui.create_state(name) -> bool`
- `mod.ui.register_state(name) -> bool` (alias of `create_state`)
- `mod.ui.enter_state(name) -> bool`
- `mod.ui.leave_state() -> bool`
- Custom states are blank framework-managed states intended for fully custom Lua-driven screens.
- While a custom state is active, `mod.ui.state_name()` returns the registered state name, `on_frame` continues to run, and `on_event` can fully consume input.
- `mod.ui.define_state(name, { enter, update, render, event, leave, cursor }) -> bool` creates the state and routes lifecycle callbacks for that mod.
- `cursor = false` disables the automatic custom-state mouse cursor. `cursor = { ... }` passes options to `mod.ui.draw_cursor`.

## Gameplay API (`mod.game`)

`mod.game` exposes low-level gameplay telemetry and command-bit input overrides.

```lua
local s = mod.game.snapshot(0) -- player 0 perspective
if s.in_game then
  mod.log(("p=(%.2f,%.2f) v=(%.2f,%.2f)"):format(s.player_x, s.player_y, s.player_vx, s.player_vy))
end
```

`snapshot(player_index [,include_tiles=true]) -> table`
- Returns a gameplay snapshot from the chosen player's perspective. A richer field list appears below after the input/tick APIs.

`room_tile(col, row [,room_index]) -> table | nil`
- Returns exact bytes for a room-relative tile cell.
- `col`/`row` are 1-based room coordinates.
- `room_index` defaults to the current active room.
- Returned table fields:
  - `id`, `frame`, `arg`
  - `exists`
  - `room_index`, `x`, `y`, `global_x`, `global_y`
- Returns `nil` if the room dimensions are unavailable or the coordinate is out of bounds.

`tick_count() -> number`
- Returns the current gameplay tick counter.

`native_tick() -> number | nil`
- Returns the game's native tick counter directly from memory.

`set_native_tick(value) -> bool`
- Overwrites the native game tick counter. Mostly useful for low-level sync tools.

`native_state() -> table`
- Returns a small table with `game_ticks`, `rng_seed`, `game_level`, `score_p0`, `score_p1`, `score_target`, and `leader` (`0`/`1` = leading player index, `-1` = none).

`register_bot_provider() -> true`
- Marks the calling mod as a bot provider. While at least one enabled mod has registered, the main menu's mode-cycling PLAY button offers the **VS AI** and **TRAIN AI** modes.

`ai_match() -> nil | table`
- Returns the AI-match flag armed by the main menu, or `nil` when no AI match is active.
- Table fields: `active` (always `true`), `ai_player` (`0` or `1` — the player the bot should drive; the opposite of whoever activated the menu button), `training` (`true` for TRAIN AI mode, where the bot drives both players).
- The flag clears automatically when the game returns to the main menu. It is never armed while an online session is active or pending.

`simulate_ticks([count=1 [,arg0=0]]) -> ok, ran`
- Runs the native gameplay update loop immediately without re-entering Lua `on_tick(...)`.
- Intended for rollback/resimulation workflows after `apply_snapshot(...)`.
- `count` is clamped internally; `ok` is false if the game is not currently in gameplay state.

`poll_cmds(player_index [,mode=1]) -> int`
- Returns the **effective** command bitmask for the player (real input plus any active framework overrides).

`poll_cmds_raw(player_index [,mode=1]) -> int`
- Returns the raw command bitmask from the game before framework overrides are applied.

`set_input(player_index, cmd_mask_or_table [,ticks=1 [,replace=true]]) -> bool`
- Tick-synchronous input scheduling for bots.
- Applied for the entire gameplay tick from `mod.on_tick(...)`.
- `cmd_mask_or_table` can be an integer mask or a table like `{ left=true, jump=true }`.
- `ticks > 0`: apply for N gameplay ticks then clear.
- `ticks = 0`: clear scheduled input.
- `ticks < 0`: hold until cleared.
- `replace=true` fully replaces real input; `replace=false` ORs into it.

`input_override(player_index, cmd_mask_or_table [,frames=1 [,replace=false]]) -> bool`
- Low-level poll-based override. Mostly useful for legacy mods and experiments.
- `frames > 0`: apply for N polls then clear.
- `frames = 0`: clear override.
- `frames < 0`: hold until cleared.

`input_clear(player_index) -> bool`
- Clears both tick-scheduled and poll-based overrides for that player.

`input_status(player_index) -> table`
- Returns both tick and poll override state, plus `raw_now` and `effective_now`.

`apply_snapshot(snapshot_table) -> bool`
- Applies a snapshot previously returned by `snapshot(...)` back into live game memory.
- Intended for authoritative multiplayer / rollback experiments.
- Applies the active room, leader index, both player structs, and listed non-player entity states.

`entities([current_room_only=true [,include_players=false]]) -> table`
- Returns active thing/entity tables with fields such as `type`, `x`, `y`, `vx`, `vy`, `dx`, `dy`, `room_index`, `state_id`, `flags`.

`snapshot(player_index [,include_tiles=true]) -> table`
- Still returns the legacy flat fields, and now also includes richer nested tables:
  - `player`, `enemy`: `x`, `y`, `prev_x`, `prev_y`, `vx`, `vy`, `dx`, `dy`, `has_sword`, `facing`, `state_id`, `state_timer`, `cmd_bits`, `prev_cmd_bits`, `jump_buffer`, `attack_buffer`, `collision_flags`, `grounded`, `ceiling`, `wall_left`, `wall_right`
  - `entities`: current-room active things/entities
  - `tick`, `enemy_x`, `enemy_y`, `nearest_sword_x`, `nearest_sword_y`

Command bit constants are available on `mod.game`:
- `CMD_JUMP = 0x01`
- `CMD_ATTACK = 0x02`
- `CMD_RIGHT = 0x04`
- `CMD_LEFT = 0x08`
- `CMD_UP = 0x10`
- `CMD_DOWN = 0x20`
- `CMD_MENU = 0x40`

### State Serialization (GGPO Rollback)

These functions provide fast binary state save/load for rollback netcode. Unlike `snapshot()` / `apply_snapshot()` (which use Lua tables), these operate on raw memory blobs suitable for GGPO's per-frame save/load callbacks.

`state_checksum() -> number`
- Returns a CRC32 checksum of all mutable gameplay state (globals, both players, all entities).
- Use this to detect desyncs: both peers compute checksums and compare.

`full_state_blob() -> string | nil, err`
- Captures the entire mutable game state as a binary Lua string.
- Includes: game globals (room, countdowns, RNG seed, tick counter, level), both player structs (full raw memory), and all entity/thing slots.
- Typical size: ~45KB. Suitable for GGPO save_game_state.

`apply_full_state_blob(blob) -> bool [, err]`
- Restores game state from a blob previously returned by `full_state_blob()`.
- Suitable for GGPO load_game_state.

`full_state_size() -> number`
- Returns the byte size that `full_state_blob()` would produce for the current game configuration.

## Audio API (`mod.audio`)

`mod.audio` supports two SFX paths:
- **asset path**: plays a file from your mod folder via `SDL2_mixer` (e.g. `.wav`, `.ogg`, `.mp3` if your mixer build supports it).
- **built-in id**: triggers Eggnogg's native synth SFX (`pip`, `noise`, `thump`, etc.).

```lua
-- file-based SFX (relative to mod folder)
mod.audio.play_sfx("assets/click.wav")

-- built-in synth IDs
mod.audio.play_sfx("pip", { pitch = 1.2, duration = 80 })
mod.audio.play_sfx("sword_ching")

-- music (single global track; ownership is tracked per mod)
mod.audio.play_music("assets/loop.ogg", { loops = -1 })
mod.audio.set_music_volume(0.6)
mod.audio.set_sfx_volume(0.8)
mod.audio.stop_music()
```

`play_sfx(path_or_id [,opts]) -> true | false, err`
- If `path_or_id` resolves to a file, it plays that file.
- Otherwise it is treated as a built-in id.
- `opts`:
  - `volume` (0..1, default `1.0`) multiplies the mod's current SFX volume.
  - `loops` (default `0`) for file-based sounds (`0` = once, `-1` = loop forever).
  - `ticks` (default `-1`) max playback duration in ms for file-based sounds.
- Built-in ids currently available:
  - `pip` (`pitch`, `duration`)
  - `noise` (`freq`, `duration`)
  - `thump` (`freq`)
  - `shred` (`amount`, `duration`)
  - `fm` (`carrier`, `mod`, `index`)
  - `ringmod` (`freq`, `duration`)
  - `warble` (`amount`)
  - `creepy` (`freq`)
  - `pulse` (`pitch`, `duration`)
  - `sword_ching` / `ching` (`pitch`, `tone`)

`play_music(path [,opts]) -> true | false, err`
- Plays a music file (relative to mod folder unless absolute).
- `opts.loops` defaults to `-1` (loop forever).
- `opts.volume` (0..1) multiplies the mod's music volume.
- Music is a single global channel; calling `play_music` replaces currently active mod music.

`stop_music() -> true | false, err`
- Stops music if your mod currently owns the active track.

`set_music_volume(v) -> true`
- Sets this mod's music volume scalar (`0..1`).
- If this mod owns active music, the new volume is applied immediately.

`set_sfx_volume(v) -> true`
- Sets this mod's SFX volume scalar (`0..1`) used by `play_sfx`.

## Animation helpers (`mod.anim`)

`mod.anim` is a lightweight frame-animation helper that pairs with `mod.ui.draw_sprite`.

```lua
local walk = mod.anim.new({
  strip = { sheet = "sprites", start = 32, count = 6, step = 1 },
  fps = 10,
  loop = true,
  ping_pong = true,
  scale = 2.0,
  tint = {1, 1, 1, 1},
})

mod.on_frame(function()
  walk:update(1/60)
  walk:draw(640, 360, { flip = false })
end)
```

- `mod.anim.frame_strip(opts) -> frames`
  - Builds frame tables from an atlas strip.
  - `opts`: `sheet|atlas`, `start|first`, `count|len`, `step`.
- `mod.anim.frame_table(opts_or_frames) -> frames`
  - Normalizes explicit frame lists (`number` ids or frame tables).
- `mod.anim.new(opts) -> anim`
  - Common options: `frames`, `strip`, `fps`, `loop`, `ping_pong`/`pingpong`, `speed`, `start_frame`, `flip`, `scale`, `scale_x`, `scale_y`, `tint`.
- `mod.anim.update(anim, dt)` and `anim:update(dt)`
- `mod.anim.frame(anim) -> frame, index` and `anim:frame()`
- `mod.anim.draw(anim, x, y [,opts]) -> bool` and `anim:draw(x, y [,opts])`
- `anim:reset([frame_index])`

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
  - If you register/allocate after this, the framework attempts a live atlas rebuild.

Registered glyph PNGs are now watched for source-file changes:
- Changes are reloaded from disk and applied live by rebuilding graphics atlases.
- If live rebuild fails, the framework logs that restart may still be required.

## Texture pack overlays (`mod.texture`)

Use this for resource-pack style atlas swaps from your mod folder:

```lua
mod.texture.register_spritesheet("assets/sprites.png")
mod.texture.register_tilesheet("assets/tiles.png")
mod.texture.register_miscsheet("assets/misc.png")
mod.texture.register_glowsheet("assets/glow.png")
```

The framework patches `data/*.png` atlases **as they are loaded** by the game.

- `mod.texture.register(target_path, rel_path [,opts]) -> true | false, err`
  - Generic replacement API.
  - `target_path` can be `data/tiles.png` style or shorthand like `tiles.png`.
  - `rel_path` is relative to your mod folder.
  - The replacement PNG must have the exact same dimensions as the target image.
  - `opts.override = true` allows replacing a target registration owned by another mod.
- `mod.texture.loaded(target_path) -> bool`
  - Returns true after that target atlas has loaded at least once.
- `mod.texture.register_spritesheet(rel_path [,opts]) -> true | false, err`
- `mod.texture.register_tilesheet(rel_path [,opts]) -> true | false, err`
- `mod.texture.register_miscsheet(rel_path [,opts]) -> true | false, err`
- `mod.texture.register_glowsheet(rel_path [,opts]) -> true | false, err`
  - Convenience wrappers for `data/sprites.png`, `data/tiles.png`, `data/misc.png`, `data/glow.png`.
- `mod.texture.sprites_loaded() -> bool`
  - Shorthand for `mod.texture.loaded("data/sprites.png")`.
- `mod.texture.reload_all() -> ok, info`
  - Force-reloads registered texture replacement PNGs and registered font glyph PNGs.
  - `info` fields:
    - `textures_reloaded`, `textures_failed`, `textures_restart_required`
    - `fonts_reloaded`, `fonts_failed`, `fonts_restart_required`

Registered texture PNGs are watched for source-file changes:
- Updates are reloaded from disk and applied live by rebuilding graphics atlases.
- If live rebuild fails, the framework logs that restart may still be required.

- `storage.get(key [,default]) -> value`
- `storage.set(key, value) -> true | false, err`
- `storage.delete(key) -> bool`
- `storage.save() -> true | false, err`
- `storage.schema() -> int`
- `storage.set_schema(version) -> true | false, err`
- `storage.migrate(target_schema, fn(from_schema, target_schema)) -> true, schema | false, err`
- `mod.interop.provide(namespace, version, table) -> true | false, err`
- `mod.interop.require(namespace [,range]) -> table, version | nil, err`

- `mod.ui.state_name() -> string`
- `mod.ui.state_ptr() -> number`
- `mod.ui.is_state(name) -> bool`
- `mod.ui.screen_size() -> w, h`
- `mod.ui.mouse_pos() -> x, y`
- `mod.ui.mouse_buttons([button]) -> left_down, left_pressed, right_down, right_pressed, middle_down, middle_pressed` or, with `button`, `down, pressed`
- `mod.ui.begin_overlay()`
- `mod.ui.end_overlay()`
- `mod.ui.flush()`
- `mod.ui.rect(x, y, w, h, opts)`
- `mod.ui.border(x, y, w, h, opts)`
- `mod.ui.line(x1, y1, x2, y2, opts)`
- `mod.ui.measure_text(text [,scale]) -> w, h`
- `mod.ui.readable_scale([scale]) -> rendered_scale`
- `mod.ui.text_scale_factor([scale]) -> rendered_scale` (alias)
- `mod.ui.hitbox(id, x, y, w, h [,button]) -> hovered, clicked, down`
- `mod.ui.icon_button(id, icon, x, y, w, h [,opts]) -> clicked, hovered`
- `mod.ui.tabs(id, items, selected, opts) -> selected, changed`
- `mod.ui.segmented(id, items, selected, opts) -> selected, changed`
- `mod.ui.swatch_grid(id, colors, selected, opts) -> selected, changed`
- `mod.ui.item_grid(id, items, selected, opts) -> selected, changed`
- `mod.ui.slider(id, value, min, max, opts) -> value, changed`
- `mod.ui.checkbox(id, value, opts) -> value, changed`
- `mod.ui.tooltip(text [,opts])`
- `mod.ui.push_style(style)`, `mod.ui.pop_style()`
- `mod.ui.theme(style)`, `mod.ui.set_theme(name)`, `mod.ui.current_style()`
- `mod.ui.sheet_base(name) -> sprite_base | nil`
- `mod.ui.sprite_id(sheet_or_base, index) -> sprite_id | nil, err`
- `mod.ui.draw_sprite(sprite_id_or_frame, x, y [,opts]) -> bool`
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

### Isolation policy (important)

The framework uses a **trusted-mod** model:
- Mods get isolated global tables, but all mods run inside one shared Lua VM/runtime.
- Standard Lua libraries are available.
- This is not a security sandbox; treat installed mods as trusted code.
- A mod can still impact process stability/performance (infinite loops, heavy allocations, etc.).

### Multi-file mods

Prefer `mod.dofile("lib/foo.lua")` so extra scripts run in the same environment.

---

## Notes for framework developers

Breaking changes to the API should bump `MOD_API_VERSION` in `lua_manager.c`.
