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
- `api_version` (number): compatible framework API **major**.
  - A different major is rejected before the entry script runs.
- `api_revision` (number): minimum additive revision within that major (default `0`).
  - A newer minimum is rejected by an older framework.
- `api_requires` (array): up to 32 stable capability ids that must all exist.
  - Use this only for mandatory features; branch on `mod.api.has(...)` for optional behavior.
- `allow_api_mismatch` (boolean): unsafe major/revision/capability override for local testing.
  - Do not ship released mods with this enabled.
  - For local testing only, environment variable `LUNA_ALLOW_API_MISMATCH=1` provides the same override.
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
  "api_revision": 1,
  "api_requires": ["input.bindings"],
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
local frame_subscription = mod.on_frame(function() end)
-- Safe to call repeatedly; only the first call removes it.
frame_subscription:remove()
```

Called once per rendered frame (hooked from `SDL_GL_SwapWindow`). Good for UI, overlays, and presentation logic.

### Per-tick callback

```lua
local tick_subscription = mod.on_tick(function() end)
```

Called once per gameplay update (hooked from the native `game_update`) **before** the game consumes player commands for that tick. Use this for bots, deterministic control logic, and data capture.

### Event callback

```lua
local event_subscription = mod.on_event(function(e)
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

The framework also emits a synthetic `delta_time` event once per rendered frame.
Its `value` field is the elapsed time in seconds and may be changed for offline
time-scale effects. Changing it classifies the mod as gameplay-affecting; online
rollback always supplies and keeps the unmodified value.

`mod.on_frame`, `mod.on_tick`, `mod.on_tick_post`, `mod.on_event`,
`mod.on_layout`, and `config.on_action` each return a subscription handle.
Calling `handle:remove()` unregisters that exact callback and returns `true`;
later calls return `false`. Dispatch uses stable registration IDs, so a callback
may safely remove itself or another callback without corrupting the current
iteration. Registrations added during a dispatch begin on the next dispatch.
All remaining handles are removed automatically when their mod unloads. The
single `on_load` and `on_unload` registrations retain replacement semantics.

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
print(mod.framework_api_revision) -- additive revision compatibility alias
print(mod.api.major, mod.api.revision)
print(mod.api.has("fs.pick_file"))
```

### API compatibility and capability discovery

`mod.api.major` is the public compatibility boundary. A breaking signature,
behavior, or removal requires a new major. `mod.api.revision` grows only when
contracts are added without breaking API-major-compatible mods. A mod with
`api_version = 1` and no `api_revision` therefore keeps loading on every API-1
revision.

`mod.api.capabilities` is a sorted array of stable feature ids.
`mod.api.has("feature.id")` returns a boolean for optional branches.
`mod.api.require("feature.id")` returns `true` when present, or
`false, error` when a well-formed id is unavailable. Hard requirements belong
in `mod.json` so the loader can reject before the entry script runs:

```json
{
  "api_version": 1,
  "api_revision": 1,
  "api_requires": ["content.tiles.v1", "map.lua.v1"]
}
```

Within one major, a published capability id never changes meaning or
disappears. Deprecated functions remain usable for the rest of that major and
must name their replacement. Ordinary removal waits for the next major. An
emergency security repair may replace unsafe behavior with a bounded failure,
and must be called out in release notes.

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
Service tables should be plain tables. The framework recursively snapshots them
and wraps function values with the provider's enabled/suspension guard;
metatables are intentionally not exposed across the interop boundary.

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
  - Declared inter-cell padding is removed before native packing. A rebuild
    refuses a sheet when it would exceed the engine's fixed 8,192-sprite global
    capacity instead of allowing the native allocator to wrap or crash.
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
  - With no options (or `{}`), calls the game's exact two-pass mouse renderer: native
    global scale, black shadow, animated red/yellow color, hotspot, and render-state
    restore.
  - Passing options selects the configurable sprite renderer and supports `sprite`,
    `index`, `scale`, `size`, `hot_x`, `hot_y`, `tint`, and `layer`.
  - Registered custom states draw the exact native default cursor automatically.
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

## Online rollback safety

The framework automatically suspends gameplay-affecting Lua mods for the full
online matchmaking/countdown/match window. Cosmetic-only mods keep receiving
frame, layout, and input events, so overlays, UI, textures, fonts, and cosmetic
audio can remain active without changing the rollback simulation.

A mod is classified as gameplay-affecting as soon as it does any of the
following:

- registers `mod.on_tick(...)` or `mod.on_tick_post(...)`;
- registers a bot provider or custom gameplay menu mode;
- begins or commits a `mod.content` registration transaction;
- changes the synthetic `delta_time` event's `e.value`; or
- calls any state-changing `mod.game` function (input injection, state restore,
  RNG/native-tick setters, match control, player mutation, and similar APIs).

The classification is automatic and sticky for the lifetime of that loaded mod.
Read-only `mod.game` telemetry does not classify a mod, so a HUD that only reads
snapshots can continue online. If one package mixes a HUD with bot or gameplay
logic, the whole package is suspended; split the cosmetic portion into a separate
mod if it should remain visible online.

While suspended:

- ordinary callbacks and config action handlers for that mod are not dispatched;
- mutating `mod.game` calls return `false, error_message`, including calls made
  from the console or a retained Lua function reference;
- cached interop service methods are guarded by their provider and refuse calls
  while that provider is suspended or disabled;
- tick/poll input overrides, raw-input blocks, blocked ticks, AI match state,
  transient player hide/sword-offset overrides, time scaling, and stale native
  UI buttons are cleared;
- frame delta is forced to its unmodified value and the reported time scale is
  `1.0`; and
- the complete loaded mod set and Lua-code hot reload are frozen until online
  teardown, while bind/config writes belonging to suspended gameplay mods are
  rejected, so no entry, load, or unload callback can bypass the guard.

Native UI actions that can indirectly enter, leave, or restart a match
(`button_invoke_ptr`, `button_activate_ptr`, `enter_state`, `leave_state`, and
`goto_main_menu`) are refused for every mod during the online safety window.
Cosmetic drawing, overlays, button layout, texture/font reloads, audio, and
read-only telemetry remain available.

`mod.info()` includes `gameplay_affecting`, `suspended_online`, and
`gameplay_reason` for diagnostics. `mod.online.status()` exposes the global
`gameplay_mods_suspended` flag. Suspension does not replay missed callbacks when
it ends; gameplay mods should naturally re-check the current state on their next
callback.

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

`register_menu_mode{ id=, label=, color={r,g,b}, on_activate=fn } -> true | nil, err`
- Adds an entry to the main menu's mode-cycling PLAY button (PLAY and ONLINE are framework built-ins; everything else comes from this registry, up to 8 entries).
- `id`: stable string, also used to persist the selected mode across sessions.
- `label`: the button text while your mode is selected (keep it short).
- `color`: optional `{r,g,b}` accent for the button and selector arrows.
- `on_activate(player_index)`: called when the button is activated with your mode selected; `player_index` is `0`/`1` for whichever player's controls pressed it. Return `true` to proceed with the native START flow (map select → match); return `false`/nothing to stay in the menu (e.g. if you opened your own screen).
- Entries disappear from the cycle while your mod is disabled and are restored on re-enable/hot-reload (re-register in your entry file).

`arm_ai_match(ai_player, training) -> true`
- Arms the AI-match flag consumed via `ai_match()` (typically from a `register_menu_mode` `on_activate`). Ignored while an online session is active or launching.

`register_bot_provider() -> true`
- Marks the calling mod as a bot provider (informational; menu entries are now added via `register_menu_mode` instead).

`ai_match() -> nil | table`
- Returns the AI-match flag armed by the main menu, or `nil` when no AI match is active.
- Table fields: `active` (always `true`), `ai_player` (`0` or `1` — the player the bot should drive; the opposite of whoever activated the menu button), `training` (`true` for TRAIN AI mode, where the bot drives both players).
- The flag clears automatically when the game returns to the main menu. It is never armed while an online session is active or pending.

`room_tiles([room_index=0]) -> table | nil, err`
- Returns one room's full tile grid: `{ room, w, h, tile_w, tile_h, origin_x, origin_y, ids }` where `ids` is a flat row-major array of tile type ids (`ids[row * w + col + 1]` with 0-based `row`/`col`; `-1` = unreadable cell).
- `tile_w`/`tile_h` are the tile pixel dimensions and `origin_x`/`origin_y` the world-pixel position of the room's top-left cell, so `col = floor((x - origin_x) / tile_w)` maps world coordinates to cells directly.
- Intended for bot/navigation mods — one call replaces hundreds of per-cell `room_tile()` calls.

`tile_solid(tile_id) -> bool`
- Solidity of a tile TYPE id straight from the engine's tile property table (the same data native collision and spawn-safety use).

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
- `CMD_ATTACK = 0x01`
- `CMD_JUMP = 0x02`
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

`mod.audio` supports three SFX paths:
- **asset path**: plays a file from your mod folder via `SDL2_mixer` (e.g. `.wav`, `.ogg`, `.mp3` if your mixer build supports it).
- **built-in id**: triggers Eggnogg's native synth SFX (`pip`, `noise`, `thump`, etc.).
- **generated PCM**: renders bounded bytebeat/floatbeat once and mixes it through
  Eggnogg's already-open audio device without requiring `SDL2_mixer`.

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

-- bounded procedural audio
mod.audio.play_bytebeat("t*(t>>5|t>>8)", {
  mode = "bytebeat", sample_rate = 8000, duration = 8,
})
mod.audio.play_bytebeat("sin(2*pi*220*time)*0.25", {
  mode = "floatbeat", sample_rate = 22050, duration = 4,
})
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

`play_bytebeat(expression [,opts]) -> true, info | false, err`
- Compiles and plays bounded JS-256-style bytebeat or floatbeat on this mod's native
  generated-audio voices. It never evaluates JavaScript/Lua, opens another audio
  device, or writes a temporary file.
- `opts`: `mode`, `sample_rate`, `duration`, `fade_ms`, `gain`, `volume`, `loops`,
  and `ticks`.
- Exact expression/render options reuse an owned decoded cache entry.

`bytebeat_info(expression [,opts]) -> info | nil, err`
- Runs the same parser and resource validation without initializing audio or rendering.
- Syntax diagnostics include a zero-based byte offset.

`stop_sfx() -> stopped_channels`
- Stops this mod's owned SFX channels.

`clear_generated() -> removed_chunks`
- Stops this mod's SFX channels and frees its generated chunks, preserving file caches.

`status() -> table`
- Reports the current backend, volumes, cached/generated counts, generated PCM bytes, and
  limits.

See `BYTEBEAT.md` for the full expression grammar, floatbeat rules, options, examples,
diagnostics, resource ceilings, cache ownership, and verification procedure.

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

## Declarative content tiles (`mod.content`)

`mod.content` is the owner-scoped, transactional foundation for custom map
tiles. It separates deterministic tile definitions from volatile atlas sprite
ids: definitions store a symbolic `owner:sheet` key, and the framework resolves
that key again after atlas rebuilds.

Register a sheet first, then commit a complete replacement set for your mod:

```lua
mod.assets.load_spritesheet("terrain", "assets/terrain.png", {
  cell_w = 16,
  cell_h = 16,
  padding = 0,
})

local tx, err = mod.content.begin()
assert(tx, err)

local ok
ok, err = tx:register_tile({
  id = "moss_floor",
  name = "Moss Floor",
  collision = "solid",
  sheet = "terrain",
  sprite_index = 0,
  frame_count = 4,
  frame_ticks = 6,
  animation = "loop",
  native_visual = "underlay",
  mirror_with_room = true,
  random_phase = true,
  offset_x = 0,
  offset_y = 0,
  scale = 1,
  angle_degrees = 0,
  tint = {1, 1, 1, 1},
})
assert(ok, err)

ok, err = tx:commit()
assert(ok, err)
```

API:

- `mod.content.begin() -> transaction | nil, err`
- `transaction:register_tile(def) -> true | false, err`
- `transaction:commit() -> true | false, err`
- `transaction:abort() -> true | false, err`
- `mod.content.qualify(local_id) -> "owner:id" | nil, err`
- `mod.content.find_tile(local_or_qualified_id) -> definition | nil`
- `mod.content.fingerprint() -> sha256_hex, tile_count, generation`
- `mod.content.owner` is the normalized owner id, or `nil` when the manifest id
  cannot be used as a content namespace.

`mod.content.fingerprint()` describes the complete live process registry, not
just the calling mod. Its digest/count/generation include definitions owned by
all loaded mods and map packages, so it must not be used as a package-only
version identifier.

`find_tile` returns a detached Lua table (never a live registry pointer) with
the normalized `key`, `owner`, `id`, display `name`, collision/force/sheet/frame fields,
both string `animation` and numeric `animation_mode`, transform/tint fields,
flags plus their boolean forms, normalized `native_visual`, and the `asset_sha256` and
`definition_sha256` digests.

Content-compatible mod ids are at most 47 characters and use letters, numbers,
`.`, `_`, and `-` without starting with `.` or `-`. The `map.` owner prefix is
reserved for map packages, preventing a mod transaction from replacing map-owned
definitions.

A transaction is the complete definition set for its owner. Committing an empty
transaction removes that owner's tiles. Failed validation or commit changes
nothing. Transactions automatically abort during garbage collection, and every
committed owner is removed when its mod disables, unloads, or hot reloads.
Each owner may register at most 4,096 tiles, and the process-wide registry is
capped at 65,535 tiles.

Tile definition fields:

- required: `id`, `sheet` (or `sprite_sheet`); `native_glyph` is additionally
  required when `collision` is omitted or `native`
- optional: `name`, `sprite_index`, `frame_count`, `frame_ticks`, `animation`,
  `layer`, `mirror_with_room`, `random_phase`, `offset_x`, `offset_y`, `scale`,
  `scale_x`, `scale_y`, `angle`/`angle_degrees`, `tint`, `asset_sha256`,
  `collision`, `native_glyph`, `force_mode`, `force_x`, `force_y`,
  `max_speed_x`, `max_speed_y`, `native_visual`

`collision` is `native` (default), `solid`, `pass_through`, or `hazard`. The
three presets resolve to the verified vanilla `@`, `x`, and `X` behaviors;
omit `native_glyph` for a preset. If it is supplied, it must match the preset.
Native `K` is deliberately rejected as a custom behavior glyph until the map
loader owns a safe per-room thing-pool spawn budget; it may still be a source
symbol when the definition selects a safe preset instead.
`force_x`/`force_y` opt their axes into deterministic per-tick velocity changes
in `-64..64`. `force_mode` is `add` (default) or `set`; set mode changes only
authored axes. Matching `max_speed_x`/`max_speed_y` values in `0..64` clamp
absolute velocity after the force. `mirror_with_room` also reverses `force_x`
for cells bound into a mirrored-right room.

`native_visual` is `replace` (default) or `underlay`. `underlay` first draws the
cell's already resolved native terrain—including its generic `map_draw` sprite
fallback when the action itself returns no draw—and then the custom sprite,
matching the visual composition used by vanilla surface props such as mines
without taking their spawn/trigger behavior. It affects drawing only; collision
still comes from `collision`/`native_glyph`.

`animation` is `loop`, `ping_pong`/`pingpong`, or `once`. `tint` is exactly four
RGBA numbers in `0..1`. Unknown keys, fractional integer fields, NaN/infinity,
unknown flags, overlong strings, unsafe behavior glyphs, duplicate tile ids, and
out-of-range values return a precise error instead of being coerced.

`layer` defaults to `0` and must be `0` or `1`. These values select the engine's
two real sprite-batch banks; arbitrary signed z-order values are not supported.

For a mod-owned sheet, `sheet` names an id previously passed to
`mod.assets.load_spritesheet`. The framework hashes the actual file bytes at
registration time. `asset_sha256` is optional for mods, but when supplied it
must match those bytes. Built-in keys such as `builtin:tiles` derive a stable
identity internally and must omit `asset_sha256`. Recognized built-in content
sheets are `builtin:sprites`, `builtin:tiles`, `builtin:misc`, and
`builtin:glyphs`; unknown built-in names remain unresolved and use the native
fallback.

A sheet used by `mod.content` must itself be content-key compatible: at most 47
characters using letters, numbers, `.`, `_`, and `-`, without a leading `.` or
`-`. `mod.assets` accepts a few broader ids for UI-only use, but those cannot be
made into an unambiguous `owner:sheet` content key.

Every stored sheet name is normalized as a qualified `owner:sheet` key. External
mod sheets are range-checked against the sprite count reported by
`mod.assets.load_spritesheet`. After an atlas rebuild the resolver looks up the
new base id, so tile definitions never retain volatile atlas ids.

Content registration classifies the package as gameplay-affecting because its
native fallback behavior can affect collision/update logic. Mutating content
operations are therefore suspended during online rollback just like mutating
`mod.game` calls.

The registry, map-v2 metadata path, deterministic animation calculations,
built-in/mod/map sheet atlas loading, bounds-checked atlas-key resolver, live
generated-map binding, and native tile draw bridge are implemented. Bound V2
symbolic cells resolve and draw the registered sprite; animation uses the
rollback-tracked game tick, and missing content automatically draws the declared
native fallback. The custom transform is relative to the engine's current tile
transform, tint multiplies the current map/native tint, and the complete turtle
state is restored after drawing.

V2 map packages also have a real map-level sheet. Put `sprite_sheet`,
`asset_sha256` (optional), `cell_w`, `cell_h`, and `padding` directly under
`tileset`; entries in `tileset.tiles` inherit them and name another sheet only
for an exception. Setting `native_layout: true` on an external sheet with a
128-cell native prefix reskins ordinary unlisted map glyphs as well. The sheet
may append any number of custom cells, and its declared cell size, padding, and
grid shape are not fixed. The engine still runs each glyph's native
collision/update action; only its synchronous tile-atlas draw base changes.
Explicit definitions take precedence. See `MAP_FORMAT.md` for the schema
and checked-in example.

Only V2 map cells currently obtain live bindings. Mod-owned definitions can be
registered and queried, but general runtime tile placement/editing remains a
future API. Mod-owned definitions do not receive raw native callbacks. V2 map
packages use the separate bounded `map.lua` API below. Fixed-address integration
still requires a manual in-game visual pass for mirrored rooms, external sheets,
atlas rebuilds, and forced missing-asset fallback.

## Map-local behavior (`map.lua`)

An `eggnogg-map/v2` package may put one optional `map.lua` beside its
`data.json` and `data.map`. This is intentionally separate from ordinary mods:
it has its own deterministic sandbox, no `mod.*` API, no filesystem/network/OS
access, and a fixed state surface that is embedded in rollback snapshots.

Keep JSON simple: declare the sprite and native collision fallback there, then
register only behavior unique to the map:

```lua
map.sensor(">", {
  tile_box = { left = 0, top = 0, right = 1, bottom = 0.375 },
  object_box = { left = -0.375, top = 0, right = 0.375, bottom = 0.375 },
  objects = { "alive_player", "sword", "hazard" },
  contact_scope = "binding",
})

function spring_launch_vy(object)
  if object.kind == "player" then
    return -4.0
  end
  return -2.8
end

map.on_enter(">", function(object, tile)
  local target_vy = spring_launch_vy(object)
  object.vy = target_vy
  object:set_velocity_limits({ min_vy = target_vy }, 8)
  tile:set_sprite(129, 16, { offset_y = 8 })
  map.state.spring_hits = (map.state.spring_hits or 0) + 1
end)

map.on_contact("}", function(object, tile)
  object:add_velocity(tile.mirrored and -0.25 or 0.25, 0)
end)
```

Available registration functions are `map.sensor(tile_ref, options)`,
`map.on_enter(tile_ref, callback)`,
`map.on_contact(tile_ref, callback)`, `map.on_leave(tile_ref, callback)`, and
`map.on_tick(callback)`. A tile reference is its one-byte source symbol or
canonical `map.<map-id>:<tile-id>` key.

One optional sensor may be registered per tile binding while the file loads.
`tile_box = {left,top,right,bottom}` uses tile-local coordinates (`0..1` is the
cell), `object_box` is `center`, native-radius `body`, `feet`, or a bounded
custom relative box. `objects` accepts legacy `player` (living and dead player
bodies), the narrower `alive_player`/`dead_body`, `sword`, and opt-in `hazard`;
the last profile is the verified native type-3 point hazard spawned by `K`.
`contact_scope` is `cell` by default or `binding` to union adjoining cells of
the same definition without seam re-entry/multiplied callbacks, and
`mirror_with_room` optionally mirrors the region. Authored edges are fixed at
1/256-tile precision, edge touching counts, and the runtime evaluates the
complete bounded neighborhood in stable map order from one immutable physics
sample. See `MAP_FORMAT.md` for exact ranges and defaults.

Contact callbacks receive:

- `object`: writable finite `x`, `y`, `vx`, `vy`, plus
  `set_velocity(vx,vy)`, `add_velocity(dx,dy)`, and
  `set_velocity_limits(limits,duration_ticks)` plus
  `clear_velocity_limits()`; read-only `kind`, `id`, and
  verified native `contact_radius`. Kinds are `player`, `dead_body`, `sword`,
  and `hazard`; their radii are respectively 6, 6, 4, and 0 pixels. A limits
  table contains one or more of `min_vx`, `max_vx`, `min_vy`, and `max_vy` in
  `-64..64`, with valid min/max pairs, for 1 through 1,000,000 ticks. It replaces
  the object's previous temporary limit; clearing an absent limit is a no-op.
  Limits are id/lifecycle/kind-safe rollback state, with only the native
  live-player/state-8-dead-body transition retaining one lifecycle's record;
- `tile`: read-only `x`, `y`, `key`, `symbol`, and `mirrored`, plus
  `set_sprite(index,duration_ticks [,options])` and `reset_sprite()`.

`set_sprite` options are a strict plain table with optional finite `offset_x`
and `offset_y` destination-pixel deltas in `-4096..4096`, fixed to 1/256-pixel
precision. They are added to the JSON transform after atlas cropping and expire
or reset atomically with the sprite. This can draw an extended active frame
above its 16x16 cell without moving the native underlay, collision cell, sensor,
or a spawned entity. Omitted offsets reset to zero rather than inheriting a
previous override.

Persistent data must use `map.state`, whose 64 bounded entries accept only nil,
boolean, finite number, or short string values. `map.random(...)` is the only
gameplay RNG and is snapshotted; `map.tick()` returns the deterministic script
clock. Ordinary mutable globals/captured state, `math.random`, bytecode, dynamic
loading, debug/FFI/JIT/coroutine APIs, unbounded execution, and the numeric `^`
operator are rejected. `^` would route through non-bit-stable libm power code;
use explicit multiplication for bounded powers. Carets in strings/comments are
unaffected.

Bindings without a sensor use the legacy verified player center plus half-tile
foot probes and native type-2 sword center point. Sensor bindings instead use
their declared tile/object boxes with player/dead-body radius 6, sword radius 4,
or the opt-in type-3 hazard's native point radius 0. These are physics-contact
profiles, not the separate sprite-derived combat hurtboxes. Other native thing
types remain skipped. Reusable thing slots carry rollback-snapshotted lifecycle
generations and explicit snapshotted kinds, so a stale leave from a sword or
hazard cannot alter a newly allocated occupant of that slot.

Raw velocity units are not interchangeable between supported kinds. Players and
dead bodies use native gravity `0.15`, while swords and the K-spawned hazard use
`0.075`. This example deliberately selects only living players: repeatedly
relaunching a corpse can prevent the native grounded respawn gate from completing.
Its `-4.0` living-player and `-2.8` sword/hazard targets reach roughly the same
53-pixel height. The eight-tick `{ min_vy = target_vy }` limit is applied after
native physics/callbacks and persists after the shallow contact ends, so a delayed
unarmed kick cannot add upward speed on the following tick. It does not cap falling
velocity because only the upward minimum is supplied.

The native `K` glyph is a room-reset spawn marker, not a moving tile. It creates
a separate type-3 point-mass hazard with gravity, damped map collision, a fixed
vanilla sprite, and a fixed player-damage radius. A map sensor may opt into its
verified `hazard` profile and change that spawned object's velocity; visual
changes to the marker do not affect it. Its native handler also does not check a failed thing-pool
allocation, so arbitrary `K` counts are unsafe. Programmable physics blocks
therefore require the future deterministic custom-entity API and a validated
spawn budget, not a tile render offset.

The loader auto-discovers a direct non-reparse `map.lua` up to 256 KiB, hashes
and validates the exact retained bytes, and includes source plus canonical tile
bindings in the package signature/server map key. That existing 32-bit key is a
compatibility hint, not cryptographic integrity. A live map pins that generation;
script edits apply on the next map generation rather than hot-swapping under
rollback. Managed online prematch also requires the exact pinned script to be
active and healthy before READY; a bind/start failure aborts setup visibly.
See `MAP_FORMAT.md` and
`docs/superpowers/specs/2026-07-18-map-local-lua-design.md` for the full limits,
event order, failure behavior, and example acceptance map.

## JSON (`mod.json`)

Use the built-in strict JSON helpers for web payloads, interop messages, and
structured imports instead of bundling an evaluator or handwritten parser:

```lua
local encoded, encode_err = mod.json.encode({
  action = "queue",
  tags = mod.json.array({ "casual", "public" }),
  cursor = mod.json.null
})

local decoded, decode_err = mod.json.decode(encoded)
if decoded and mod.json.is_null(decoded.cursor) then
  decoded.cursor = "start"
end
```

`mod.json.encode(value)` supports booleans, finite numbers, valid UTF-8
strings, tables, and `mod.json.null`. Dense positive-integer tables encode as
arrays, string-keyed tables encode as objects, and object keys are emitted in
deterministic bytewise order. An untagged empty table is an object. Use
`mod.json.array([table])` or `mod.json.object([table])` to shallow-copy a table
and preserve explicit container intent, especially `[]` versus `{}`.

`mod.json.decode(text)` parses one complete strict UTF-8 JSON document. JSON
null becomes `mod.json.null`; `mod.json.is_null(value)` recognizes it. Decoded
arrays and objects retain their kind, including when empty. Duplicate object
keys, invalid escapes/surrogates/numbers, and trailing data are errors instead
of being normalized silently.

Both directions are limited to 32 levels, 65,536 nodes, and 256 KiB per
decoded/encoded string. Decode input and encoded output are each capped at
1 MiB. Encoding rejects nil, cycles, sparse/mixed tables, unsupported key/value
types, non-finite numbers, and invalid UTF-8. Failures return `nil, error` with
a JSON value path; decode syntax errors also include a byte offset. No helper
evaluates code, calls `tostring`, or invokes table metamethods.

## File selection (`mod.fs`)

New mods should use the general owner-bound file picker:

```lua
local path, err = mod.fs.pick_file({
  title = "Choose a map package",
  filters = {
    {
      name = "Map packages (*.zip;*.json)",
      patterns = { "*.zip", "*.json" }
    },
    {
      name = "JSON files (*.json)",
      patterns = { "*.json" }
    }
  },
  allow_all = false,
  filter_index = 1
})
```

`title` is optional and defaults to `Select file`. `filters` may contain at
most 16 entries; each entry has a display `name` and 1-16 filename `patterns`.
When `filters` is absent or empty, the picker supplies `All files (*.*)`.
Set `allow_all = true` to append that entry to a custom list, and use the
one-based `filter_index` to select the initial filter.

The call returns an absolute UTF-8 Windows path. Closing the dialog returns
`nil, "cancelled"`; native dialog failures return `nil` plus an error. Invalid
types, UTF-8, lengths, counts, patterns, or filter indexes raise a Lua argument
error before the dialog opens. Patterns are filename expressions such as
`*.png`; path separators, control characters, and shell metacharacters are
rejected. The owner must still be enabled when the picker is invoked.

The dialog is modal. Open it only in response to an explicit user action, never
from `on_frame` or `on_tick`. `mod.fs.pick_character_file([title])` remains as
a deprecated API-1 compatibility wrapper. `mod.fs.pick_folder([title])` is
also owner-bound and returns a Unicode path. `mod.fs.find_file` keeps its
depth-12 bound and skips directory reparse points/junctions.

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

Breaking changes bump `MOD_API_MAJOR` in `mod_api.h`. Additive public changes
bump `MOD_API_REVISION`, append stable capability identifiers when discovery is
useful, and update the manifest/runtime/schema/docs/native regression suite.
