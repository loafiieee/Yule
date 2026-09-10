# Greggnogg

Greggnogg is a standalone, dependency-free browser editor for native
EGGNOGG+ custom maps. It provides a visual 33-by-12 room canvas, native tile
palette, room ordering and appearance controls, live loader/playability checks,
and Yule-compatible package import and export.

The editor is intentionally self-contained in `greggnogg/`. It is not linked
from the repository documentation site and does not participate in that site's
build. It does not run map scripts in the browser. Preview opens the installed
Yule protocol handler; custom maps then upload their package to a token-scoped
HTTP session on the local game process (127.0.0.1:31785).

## Run it locally

No package install, bundler, or application server is required. From the
`eggnoggplus-win/` game/source directory (the directory containing
`MAP_FORMAT.md`):

```powershell
Set-Location .\greggnogg
py -m http.server 4173
```

Then open `http://127.0.0.1:4173/` in a current desktop browser. Serving the
folder is recommended over opening `index.html` as a `file:` URL because browser
security rules for local images and downloads vary.

The four JavaScript/CSS files and game atlases are all local. Work is
autosaved to that browser profile's local storage, but local autosave is not an
installed map or a backup: download a package when a revision matters. Changing
the origin or port creates a different browser storage namespace.

## Editor scope

Greggnogg edits `eggnogg-map/v1` packages. It supports:

- one through nine fixed `33x12` source rooms, ordered center-out;
- the complete native V1 glyph palette;
- pencil, eraser, flood-fill, line, rectangle, eyedropper, and symmetry tools;
- room creation, duplication, renaming, deletion, and reordering;
- sword/karate rules, round-end behavior, score and armed-respawn limits;
- per-room ambience plus primary and mirror color banks;
- undo/redo, keyboard operation, grid and mirrored-source previews;
- import from `data.json` plus `data.map`, an extracted folder, or a ZIP; and
- loader-shaped errors and playability warnings before export.

An otherwise valid raw `data.map` can be imported without `data.json`; the
editor supplies placeholder V1 metadata that must be reviewed before export.
Unknown glyphs are errors and are never silently replaced.

Imported V1 files are compiled back to Greggnogg's canonical JSON and map-text
layout. Because the game's current online compatibility key includes the raw
bytes of both files, even whitespace normalization changes that key. The import
summary calls this out, and manifests with unknown compatibility fields are
refused instead of silently dropping those fields.

Greggnogg edits ordinary maps and V2 custom-content maps. Creating the first
custom object upgrades the current draft automatically. Object definitions,
assets and logic are saved with the map and participate in undo/redo.

## Typical workflow

1. Choose **New** or **Import**.
2. Author source rooms from the center outward. Two source rooms become three
   final rooms; three become five; in general `n` becomes `2n - 1`.
3. Draw native glyphs and set the map and room properties. The canvas uses the
   real game atlases; labels call out procedural or runtime-driven previews.
4. Open **Validation** and resolve errors. Errors block export. Warnings point
   out likely playability or native-resource problems but do not rewrite cells.
5. Choose **Preview** for a local test match when the Yule link handler is
   installed.
6. Choose **Export map**. Download the ready-to-extract ZIP or either source
   file separately.
7. Extract the ZIP so the map folder is directly under the game's `maps/`
   directory. Do not leave the ZIP itself in `maps/`; the runtime scans folders,
   not archives.

Keyboard help is built into the `?` dialog. Important shortcuts include
`1`–`5` for drawing tools, `I` for the eyedropper, `/` for palette search,
arrow keys for grid navigation, and the normal undo/redo shortcuts.

## Preview in Yule

Choose **Preview** to play the current map in EGGNOGG+. The installed Yule
protocol handler may prompt before opening the game. Custom maps require the
package-preview runtime build and may also require local-network permission in
the browser. Preview waits while an online match or online match setup is active.

V1 drafts use the compact `yule://preview/v1z/...` link and remain in memory.
Custom maps use a short `yule://preview/session/<token>` link, followed by a local
upload containing map data, PNG assets, entities and Lua. The runtime validates
that package again and stages it under `maps/_greggnogg_previews/<token>/`.
These folders are excluded from normal map discovery. Superseded previews are
removed after the registry and engine release them. Cleanup only removes files
created by the current process whose identity, size and modification time still
match; edited files are preserved. Older-process caches and the last preview at
exit can remain on disk. Do not remove the active folder while the game runs.

Preview does not mark the draft as exported or admit it to online manifests.
A preview keeps the map's authored IDs and temporarily overrides an installed
map with the same ID; entering the online hub restores the installed map.
Right-click copying remains available for compact V1 links. To share a custom
map with its assets and logic, use **Export map**.

The transport, package staging and native loader have automated coverage. Actual
browser-to-game acceptance, including browser permission prompts, remains a live
check; it has not been verified by UI automation.

## Yule package contract

“Yule format” is a folder contract, not a `.yule` file extension. A V1 install
looks like this:

```text
maps/
  my_map/
    data.json
    data.map
```

The two files must be direct children of one non-reparse map folder and each is
limited to 4 MiB. Folder names beginning with `_` are ignored. `data.json`
declares `format`, display metadata, rules, the center-out `layout.order`, and
room ambience/appearance overrides. `data.map` contains one section for every
ordered room, with exactly twelve quoted rows of exactly thirty-three ASCII
bytes. Spaces are meaningful. Room section IDs and `layout.order` must form an
exact one-to-one match.

Greggnogg's ZIP contains exactly one top-level folder with `data.json` and
`data.map` beneath it. ZIP is a transport convenience only; extraction is part
of installation.

## Asset provenance

No AI-generated or otherwise generated artwork is used. In particular,
Greggnogg does not use the
derived tile previews from `docs-site/map-tiles` and does not chroma-key the
game images. The PNG files already contain indexed transparency, which the
browser decodes directly.

The editor copies only these original runtime atlases:

| Runtime source | Standalone copy | SHA-256 |
| --- | --- | --- |
| `data/tiles.png` | `greggnogg/assets/game/tiles.png` | `bc7203e68fdf244e563bf859e7ffc879359b4917c57cb1cc81455fc55ecfcf94` |
| `data/sprites.png` | `greggnogg/assets/game/sprites.png` | `a358fd13efd3bb39a642dbf41de572b2b773676b8c371a5895f5782069a2de85` |
| `data/misc.png` | `greggnogg/assets/game/misc.png` | `a8d964ea50f13a977752b090dd7aab4540c2d7058e96349a03077a4acf36316a` |
| `data/font8x8.png` | `greggnogg/assets/game/font8x8.png` | `7550e0b4411555d57a3386befc76ba5f5274da47d705f4869d24a2b8103d60ff` |

`glow.png` is not copied: reverse engineering shows the native map draw path
uses `misc` sprite `0x11` for the relevant room particle rather than loading
that file as a map atlas.

## Preview boundary

The renderer reproduces the recovered atlas crops, layer order, room colors,
spatial mirroring, composite recipes, water transforms, chandelier/tentacle
motion, waterfall/scrolling crops, sun stack, and ambience layers. It is still
an editor preview, not a reimplementation of the simulation. Runtime-only
random selection, particle histories, object physics, player-dependent state,
and active goal behavior are represented deterministically or annotated.
Package validation—not a pleasing preview—is the authority for export safety.
See `GAME_MODEL.md` for the derived model and its source trail.

## Repository sources used

The editor model was checked against these project-root paths:

- `MAP_FORMAT.md`
- `custom_maps.c`
- `content_registry.c`
- `tests/custom_maps_v2_test.c`
- `ghidra/eggnoggplus.exe.c`
- `docs/superpowers/specs/2026-07-17-content-registry-map-v2-design.md`
- `docs/superpowers/specs/2026-07-18-map-local-lua-design.md`

## Development checks

From the `eggnoggplus-win/` game/source directory, the dependency-free
JavaScript tests are:

```powershell
node --test greggnogg/editor-core.test.js greggnogg/atlas-renderer.test.js tests/greggnogg_color_controls_test.js
# Legacy Windows Script Host remains supported:
cscript //nologo greggnogg\editor-core.test.js
cscript //nologo greggnogg\atlas-renderer.test.js
```

The current native-loader regression suite must only be launched through its
guarded runner, as required by the repository safety policy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_v2_map_test.ps1
```

Never launch `eggnoggplus.exe` or a `build/*.exe` test binary directly.

### Forced Eggnogg color

In the map inspector, enable **Force a map-wide goal color** and choose a color. The override applies to E and ^ goals across every room, including mirrored rooms. Uncheck it to return to automatic player-derived color. Team targets 1 and 2 retain their team colors.

Exports store `rules.eggnogg_color` as three numeric RGB channels from 0 to 1. Imports preserve exact channel values until the color is edited; automatic mode omits the field. Invalid colors are rejected instead of silently clamped. The canvas previews the override immediately, and changes support undo, redo, and draft restoration.

### Opponent spawning

The map inspector's **Default opponent spawn** controls room-entry opponent spawning. **Native** preserves the game's normal room rules; **Always** requests opponent spawning and **Never** suppresses it. The room inspector can inherit that map default or override it for a source room and its mirrored copy. An explicit **Native** room override restores native behavior even when the map default is Always or Never.

These choices round-trip as optional `defaults.room.opponent_spawn` and `rooms.<id>.opponent_spawn` strings (`default`, `always`, or `never`). An omitted room property preserves inheritance. Edits participate in normal undo/redo, project saves, and room duplication.

## Entity content authoring

Choose **Objects** in the command bar to edit designs for the current map.
Use **New**, edit the picture and Lua callbacks, then choose **Done**. The design
appears under **Custom tiles** in the regular tile palette. Paint and erase its
instances with the map tools; terrain remains underneath. The object designer
has no separate map picker or placement screen. Animated pictures play in the
map preview without executing gameplay scripts.

Exports contain `entities.json`, combined `map.lua`, picture assets and
`objects.greggnogg.json` editor metadata. Keep the metadata when reopening a map
to recover separate object callbacks. A mismatch with a manually edited `map.lua`
is reported rather than silently replacing the edited script.

Entity maps remain offline-only. The existing one-click game preview supports
V1 maps; export custom-content maps into `maps/` for runtime testing.

## Map logic

Open **Map logic** in the main command bar to edit the current map's `map.lua`.
**Advanced Lua** provides syntax highlighting, line numbers, indentation and
matching brackets. **Blocks** provides a Blockly workspace with game-tick and
object events, player actions, object motion, map values, logic and arithmetic.
The first script automatically upgrades the map format.

Blocks generate Lua for the same bounded runtime. Handwritten Lua is preserved
as a Lua code block when it cannot be recovered from an unchanged saved block
layout; edit that code in Advanced. `logic.greggnogg.json` preserves the block
layout alongside the exported script. Both Blockly and CodeMirror are bundled
locally with their licenses and pinned package hashes under `vendor/`.

Custom objects occupy a separate layer from terrain. Painting an object leaves
the normal tile underneath; this supports pickups, hazards and decorations on
existing surfaces. Erasing with an object selected removes the object only.

Object callback fields use the same Lua syntax highlighting, line numbers and
bracket matching as Advanced Map logic. Switching Map logic from Blocks to
Advanced requires connected, valid blocks; errors keep the workspace visible so
unfinished block edits are not discarded.

The Objects block category includes position/speed setters, animation clock
control, animation pause and horizontal flip. These act on the object belonging
to the surrounding creation or update event. The shared movement/pause fixture
is generated by Blockly and executed in the native map Lua VM, including a
snapshot restore and deterministic replay check.

Object blocks also support creation at an x/y position and removal of the current
object. Removal ends its statement stack, since later operations on that handle
would target a removed object. Creation still obeys the map's entity capacity;
creating objects every tick eventually fills that capacity unless logic removes
them. Both generated creation and removal programs run in the native regression
suite and reproduce identical snapshots after rollback.

Object designer **Logic** now has **Blocks** and **Advanced Lua** for each of
Every game tick, When created, and When removed. The supplied event block already
selects the current object and event; attach actions underneath it. Per-event
block layouts travel with the map's object-editor metadata. Existing handwritten
callback code is retained in an opaque Lua block when switching to Blocks.

Both logic editors include repeat loops, an every-N-ticks condition, deterministic
random integers, absolute value, rounding, square root, sine/cosine, and minimum/
maximum. Object event/creation blocks offer current-map object pickers. Missing
saved object IDs remain explicitly labeled instead of selecting another design.
Loops and random calls retain the runtime's execution limits and rollback rules.

Use **Players ? for each active player** to react to players in object or map
logic. Inside the loop, **this player** exposes number, position, speed and contact
radius; velocity and defeat actions can target **this player**. Absent players
are skipped. Observations describe the query snapshot; query again after actions
when you need updated values. Player actions still require tick/update context.

Inside an object's event and a player loop, **this player touches this object**
checks the player's contact circle against the object's authored areas. Choose
any area, sensor, body, attack area or damage receiver. It accounts for world
placement and mirrored areas; visuals alone do not create collision areas.
Touching includes the boundary and tests the circle against rectangle corners.
This condition stays true while touching; combine it with speed checks or map
values when an action should trigger only once.

Renaming an object updates matching event/creation references in verified map and
object block layouts, regenerating their Lua. Stale layouts never overwrite newer
code. Handwritten Lua and opaque Lua blocks are preserved; update their explicit
object names yourself when renaming a referenced design.

**Objects ? for each object** selects all live instances of a chosen design.
Inside that loop, position, speed, animation, overlap and removal blocks refer to
that instance. It works in map events as well as object callbacks. The loop takes
a stable list when it starts, skips instances removed before their turn, and
leaves newly created instances for the next query. Renaming a design also updates
verified block references in these loops.

The block editors use a dark workspace/toolbox theme. Background drag-to-pan is
disabled; use scrollbars or wheel scrolling to move the workspace. Blocks remain
draggable. Dropdowns are hosted inside the active dialog; live pointer acceptance
still needs verification.

Object designer **Movement** includes an opt-in gravity/drag toggle and sliders
with numeric inputs for gravity, horizontal drag and maximum vertical speed. The
settings run before your update callback and are saved in object editor metadata.
Gravity is pixels per tick squared; drag is the fraction of horizontal speed lost
per tick. Maximum vertical speed bounds both upward and downward velocity. These
controls support optional **Collide with solid map blocks**, with a centered collision
width and height. This stops movement at walls and floors without bouncing. Detection
areas remain separate. Place objects clear of terrain; custom update logic runs after
collision and can override velocity. This requires runtime API 23 and does not add
native weapon physics or collisions with other custom objects.
Detection areas likewise need logic to choose a collision/damage response.


### Variables in Blocks

Both Map logic and Object logic have a **Variables** category. Set, read, change,
clear, or test a named variable. Choose **map** for shared state, **player 1** or
**player 2** for a specific player, or **this player** inside a player loop. The
same name and scope refer to the same value across map and object callbacks:
a collectible can set this player's `extra_jump`, and map logic can read it.
Names use 1?20 letters, digits or underscores, starting with a letter or underscore.
Unset reads return 0; false remains false. Clear removes the value. Change expects
a number. Values persist between ticks and participate in rollback snapshots;
they reset when the map script is activated again. The runtime allows 64 state
entries in total, including other `map.state` uses. Each player's variable uses
one entry. These blocks use `v:m:NAME` and `v:p:NUMBER:NAME` keys in `map.state`.

Variables alone do not implement double jump: the bounded map API still needs
player input/ground-contact observations and authorable player-following indicators.
Named runtime regions remain pending; solid-region blocking is available in API 25.
Detection-area controls now sit beside the object preview on wider screens and
stack below it on narrow screens. Sprite scaling uses nearest-neighbor drawing.


### Sensing terrain and reversing direction

New variables start with the neutral name `variable`. The toolbox has one Variables
category; existing map-value blocks remain readable in saved projects. Game tick,
periodic timing, and terrain sensors are in **Game**. **Players** contains reporters
for this player's position, velocity, number and contact radius inside a player loop.
**Objects** contains position, velocity and animation-tick reporters. Player sprite
and exact terrain-character queries are not exposed yet.

For a patrolling object, set its initial horizontal velocity (for example 2). In
its update event, use **if solid terrain 9 pixels ahead of this object**, then
**reverse this object horizontal direction**. The engine moves the object once
per tick automatically. Ahead is measured from its center along its velocity;
for diagonal movement the largest axis advances by the requested distance. A
stationary object reports false. Choose a distance that accounts for its size
and speed. Automatic collision can stop velocity before this callback, so use
this sensing behavior with automatic collision disabled, or use the coordinate
sensor and explicitly managed direction instead.

**Solid terrain at x/y, width/height** checks a centered rectangle at any world
coordinate. It returns a boolean for native solid terrain, including map boundaries,
not a tile character such as `@`. Width and height default to one pixel. Detection
is a sample, not a swept collision test; thin obstacles can be skipped at high speeds.
The generated patrol fixture is exercised by the native runner with rollback replay.


### Creating variables and explicit movement (API 24)

Use **Variables ? Create variable?** to name a variable, then select it from the
variable blocks' dropdowns. **This object** stores a separate value for each live
instance, using its generation-aware handle. Object-variable names are limited to
12 characters by the current runtime key limit; map/player names allow 20. All
scopes still share the 64-entry runtime state budget. Object values are cleared
when their instance is removed and restore with rollback. Advanced Lua can access
these entries as `map.state["o:" .. handle .. ":direction"]`.

New object designs disable automatic velocity integration. Set this object's x or
y from ordinary arithmetic blocks to move it explicitly. Existing maps retain
their previous movement behavior. The standalone automatic-velocity control has been removed. Enabling gravity
or terrain collision opts into its physics integration. Advanced Lua uses `entity.set(handle, {automatic_motion=false})`; animation
continues while automatic movement is disabled. The corresponding boolean block
is available under this object's flags.

Exact tile/object-at-coordinate matching remains pending. Solid custom hitboxes are available in API 25.
The earlier patrol-specific blocks remain loadable for compatibility; they are not
a substitute for those general queries. The dropdown handler now restores workspace
focus and popup focus containment before opening; live post-drag acceptance remains
unverified by automation.


### Object draw order

Appearance now has a **Draw order** selector: **Behind players (default)** or
**In front of players**. These correspond to visual layer 0 and 1. The runtime
submits default objects before the first native actor pass, and foreground objects
after the final actor pass. The old numeric layer control under offsets is removed.
The reverse-direction block is no longer in the toolbox; its reader remains for
existing saved projects. Use arithmetic and position writes for authored movement.
Only regions marked Solid block native players, swords, corpses and hazards.
Other detection regions remain nonblocking.


### Solid custom regions (API 25)

In Object designer, open a detection area and enable **Solid ? blocks players and
native physics objects**. Its purpose becomes **Solid obstacle**. Disable Solid
to return it to Body. The region's rectangle (not the picture bounds) blocks
players, corpses, thrown swords and native point-mass hazards. Region coordinates
are relative to the object's center and mirror with the object. Image scaling does
not resize the region. Use separate regions when you also need sensor/attack roles.

Native motion is swept against these rectangles, stopping the blocked velocity
component and retaining tangential movement. Floors set the native grounded flag.
The solver uses a conservative axis-aligned body envelope, with each verified
native body's radius, and resolves initially embedded bodies toward a nearby face.
Solid regions persist even if map behavior faults and are restored by snapshots.
Entity maps remain offline-only. Contact layer/mask filters affect event queries,
not physical blocking. This is blocking geometry, not full moving-platform carry,
crushing, bounce or custom-entity-versus-custom-entity rigid-body simulation.


### Object point detector (API 26)

**Game ? Object at x/y is [type]** accepts number/arithmetic blocks for world
coordinates. Combine multiple detectors with **OR** to match different designs.
It tests authored regions of any purpose, including Solid, and returns true if
at least one matching instance contains the point. It includes the querying object
if that object matches. Objects without regions do not match. Left/top edges are
included; right/bottom edges are excluded. Mirroring is respected; sprite scaling
is not collision scaling. This detector does not read terrain tile characters.

Advanced Lua: `entity.at(x, y)` returns a detached list of matching handles in stable
instance order, once per instance even when regions overlap. Coordinates round to
1/256 pixel. Query work consumes the shared VM execution budget. Use
`entity.type(handle)` to distinguish types, and `entity.exists(handle)` if your
loop can remove instances after taking the query result.
