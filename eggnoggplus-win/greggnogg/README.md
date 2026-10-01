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

Greggnogg edits backward-compatible `eggnogg-map/v1` and `eggnogg-map/v2` packages. It supports:

- one through nine source rooms ordered center-out, with classic `33x12` or per-room `variable_cells` dimensions;
- the complete native V1 glyph palette;
- pencil, eraser, flood-fill, line, rectangle, eyedropper, symmetry, and area-selection tools;
- room creation, duplication, renaming, deletion, and reordering;
- sword/karate rules, round-end behavior, score and armed-respawn limits;
- per-room ambience, custom particle systems, primary and mirror color banks;
- undo/redo, keyboard operation, grid and mirrored-source previews;
- import from `data.json` plus `data.map`, an extracted folder, or a ZIP; and
- loader-shaped errors and playability warnings before export.

The **Select** tool drags a rectangular area across the active source room.
Copy, cut, paste, horizontal/vertical flip, and clockwise rotation include both
the room tiles and custom object copies whose anchors are inside the area.
Pastes create unique object instance names; transforms preserve names and rotate
or flip each copy's presentation. Every cut, paste, or transform is one undoable
edit. `Ctrl+C`, `Ctrl+X`, and `Ctrl+V` use Greggnogg's internal area clipboard.

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

Custom room ambience uses the same workflow. The room inspector's **Ambient
effect** selector lists built-in effects and every custom ambiance in the
current map. **Edit room effects** opens a focused designer without leaving the
map. Its separate **Room effects** and **Particle types** tabs keep composition apart
from picture and timing work. Authors define particles once, combine them with
one or more emitters, then select the result on any room. An emitter may fill
a rectangle or ellipse, or spawn along a diagonal line within its area bounds;
older maps continue using rectangles.

The designer previews the animation over the room currently being edited and
keeps that preview visible while the independently scrolling property pane is
used. On desktop the preview stays in a compact right column; narrow screens use
the stacked layout. The full sprite-sheet picker and PNG importer remain behind
a clearly labeled disclosure until needed. The Particle types tab isolates the
selected particle in its preview. It exposes spawn shape and bounds, count, velocity ranges,
acceleration, randomized spin, normal/additive blending, mirroring and all five
native draw positions. Motion timing can ease velocity, acceleration, and spin
independently of the visual transition. Each particle can transition its color, opacity, width,
height and rotation from a starting value to an ending value with a linear or
eased curve. It validates animation bounds, transition values, particle
references, fades and the 512-lane package budget before export.
Opening it upgrades a V1 draft to V2 through ordinary undo history. See
`maps/ambiance_demo/` for an editable example and `MAP_FORMAT.md` for the full
schema and online determinism policy.

The custom tile designer accepts a PNG with any layout up to the runtime's
bounded image limit. **Source X/Y** and **Region width/height** select the usable
rectangle, while **Cell width/height**, padding, sprite index and frame count
describe the cells inside it. Pixels elsewhere in the same PNG are ignored, so
authors can reuse ordinary art atlases without rearranging them into a dedicated
16x16 sheet. Imported PNGs can be removed; Greggnogg resets dependent visuals
and room graphics to safe built-ins so a stale reference does not trap the
project.

Object pictures use the same flexible atlas model. In **Objects**, import the
PNG, expand **Picture grid and crop**, and set the crop origin, crop size, cell
size, and spacing. **Remove this PNG** deletes the selected object picture from
the map and resets anything using it to built-in graphics.

V2 maps can also reskin native tiles per room. Use **Default room graphics** in
the map inspector to choose the normal sheet, then use **Room graphics** to
override the active source room and its mirrored copy. **Add PNG** packages a
sheet directly. Select a packaged sheet and use **Remove selected PNG** beside
the selector to delete it. The canvas, animated native scenery, tile previews, exports,
imports, undo, and room duplication all use the resolved room sheet. Choosing
the first option clears an override and returns to the map default (or the
built-in game graphics when no default exists).

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
The loopback handshake advertises preview API 2 before the upload. If the
registered `yule://` game is an older copy, Greggnogg now asks for an update or
reinstall directly instead of sending a package that the old loader rejects as
unknown V2 fields.
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

The map inspector's **Default opponent spawn** controls room-entry opponent spawning. **Game default** preserves the game's normal room rules, including no new opponent in the two outermost rooms of a symmetrical map; **Always** requests opponent spawning and **Never** suppresses it. Never prevents a new spawn without killing a fighter already there. The room inspector can inherit that map default or override it for a source room and its mirrored copy. An explicit **Game default** room override restores the traditional room behavior even when the map default is Always or Never.

The room inspector and selected placed-room details show the effective spawn behavior after inheritance and the symmetrical outer-room default are resolved. A placed copy can override its room design independently.

These choices round-trip as optional `defaults.room.opponent_spawn` and `rooms.<id>.opponent_spawn` strings (`default`, `always`, or `never`). An omitted room property preserves inheritance. Edits participate in normal undo/redo, project saves, and room duplication.

### Room sizes and spawn markers

Resize each room from the Room inspector. Rooms keep their bottom-left contents
aligned and may use different widths and heights. Spawn controls live in the
left tile catalog under **Spawn markers**. Select Player 1 start, Player 2
start, No respawn, Respawn for either player, Respawn for player 1, or Respawn
for player 2, then draw over the floor tile. Right-click erases a marker;
clicking an existing player start again flips its facing. All spawn overlays are
visible while a spawn marker is selected and hidden after selecting a normal
tile or object.

Greggnogg accepts these overlays only on a native-safe `@` floor cell with open,
non-hazardous space above it. Invalid clicks show an immediate explanation, and
imported invalid points are highlighted by Checks instead of producing a bad
in-game respawn.

Any applicable Respawn marker changes that room to whitelist mode for that
player. Otherwise native respawn search remains active and skips No respawn
cells. The overlays are saved in `rooms.<id>.spawn`, remain separate from the
terrain grid, mirror with their source room, and are included in online package
identity.

### Two-dimensional room layouts

Open **Full map** to arrange rooms and choose the layout type there. The normal
room editor stays focused on painting. **Symmetrical** uses the familiar
center-out line: every added room appears once on each side and the far copy is
flipped automatically, while room dimensions can still differ. Its paired room
copies can be dragged vertically together for an offset line without breaking
the symmetry. **Free layout** lets every placed room move independently in two
dimensions. The canvas renders each room's actual terrain and custom objects at
the authored X/Y position. Overlaps are rejected, while separated rooms remain
editable and are reported as disconnected until they touch again.
Switching a generated left/right line back to Symmetrical realigns moved copies
and their doorways. A free layout with
one independent design per placed room is rebuilt into an automatic mirrored
line and can be restored with Undo; conflicting reused copies are rejected
instead of being merged. When conversion would discard data, the warning names
every affected placed room, its coordinates, the reused design or copy-only
setting, and the action needed to resolve it. Invalid doorway geometry is
reported with its exact `layout` path instead of a generic mode warning.

Touching room edges connect automatically across their shared span. Authors only
need to remove terrain from the matching room edges to make a passage. Moving a
room away removes the stale connection, and moving it beside any other room
creates the new connection. The compact map above the normal editor uses the
same two-dimensional positions; clicking a room there opens that placed room's
contents directly. Double-clicking a room in the full-map canvas or using the
prominent **Edit this room** action does the same.

In Symmetrical mode, **Add room** adds the next distance in the line and its
automatic flipped partner. In Free layout, it creates a new independent room
design and places it at the first open edge as one undoable operation. The
full-map canvas also puts a `+` on every edge of the selected room for exact
placement. **Room options** contains explicit copying controls; a copy can flip
its tiles, spawn and respawn markers, custom objects, and facing horizontally
or vertically, and does not remain linked to its source.

Room designs remain reusable. The normal Map view edits a design's terrain,
while Rooms places copies of that design. Custom objects painted in a graph map
belong to the selected placed copy, so two copies of the same room design can
contain different object instances. Greggnogg migrates existing source/mirrored
objects to exact placed-room targets during conversion.
Removing a placed copy is immediate even when it disconnects the remaining
layout. Objects that named the removed copy are retained, and Checks reports the
result so they can be reassigned or deleted without silently losing authored work.
Removing the current start chooses the first remaining room as the new start.
Removing one half of a symmetrical pair keeps exactly the requested remainder
and switches the authoring view to Free layout; only the final room is protected.

The **Player starts for this copy** section in Full map offers safe floor tiles
for each player and an optional facing override. It matters when that placed
copy is chosen as the start room. Each player can independently inherit the
reusable room design's start. When a spawn marker tile is selected in the
normal room editor, **Markers apply to** switches painting between the reusable
room design and the selected placed copy. A copy begins with inherited respawn
markers; its first marker edit creates an independent list. Erasing every
marker from that copy leaves an explicit empty list. **Reset this copy's
respawn markers to room design** restores inheritance.

Every touching pair participates automatically, so closing a loop only requires
dragging its room edges together. The engine supports at most 17 placed rooms in
one map.

The full-map view opens as a canvas without a permanent inspector rail. Use
**Room options** only when a placed copy needs presentation or policy overrides.
Use **Fit map** and zoom for large layouts. Room previews and animated custom
content keep playing in both the tile shelf and the full-map canvas. In the
full-map canvas, Tab reaches each room, arrow keys select a nearby room,
Shift+arrow moves the focused room one cell, and F2 opens its contents.
Symmetrical room pairs can move vertically; Free layout allows all four directions.
The same placement validation and undo history apply to keyboard moves. In the
normal room editor, full-scale faint previews at connected edges show neighboring rooms;
selecting one slides the next room fully into the editor without fading. A selected
room can flip only that placed copy's tiles and choose
its primary or mirror palette bank as a separate setting. It can also override
the source design's ambience, native graphics sheet, and opponent respawn policy.
Leaving one of those controls on **Use room design** preserves inheritance.
Greggnogg derives the native doorway spans from the touching rectangles and
shows room-layout errors on the canvas. Imported connection metadata is repaired
internally when recovering an older or hand-edited map.
Each doorway row in **Full map** also controls **Can cross** and **Room focus**.
Can cross may allow either player, only Player 1, only Player 2, or the player
who reached GO. Room focus may continue following the GO player or move with the
player who crossed. Existing maps default to either player with GO-based focus.
Room designs may be resized after placement; Greggnogg shifts rooms beyond the
old right and bottom edges and rebuilds adjacency rather than forcing authors to
delete and recreate their layout.

Automatic touching-edge connections are numbered deterministically for map
logic. The Game block group exposes **set Door … to locked/unlocked** and
**Door … is locked** with room names in each choice. Locks are rollback-safe and
work in online simulation. Door blocks retain a stable hidden ID while the same
two rooms remain adjacent. Handwritten Lua uses the current connection number
directly.

## Entity content authoring

Choose **Objects** in the command bar to edit designs for the current map.
Use **New**, edit the picture and Lua callbacks, then choose **Done**. The design
appears under **Custom tiles** in the regular tile palette. Paint and erase its
instances with the map tools; terrain remains underneath. The object designer
has no separate map picker or placement screen. Animated pictures play in the
map preview without executing gameplay scripts.

Deleting a design also deletes every placed copy of that design from the map,
along with its generated logic and movement settings. The designer reports how
many copies it removed. To delete only one copy, select it in the normal map
editor and use **Delete object copy** in the Object inspector.

Exports contain `entities.json`, combined `map.lua`, picture assets and
`objects.greggnogg.json` editor metadata. Keep the metadata when reopening a map
to recover separate object callbacks. A mismatch with a manually edited `map.lua`
is reported rather than silently replacing the edited script.

Validated entity maps use the same one-click preview as other maps and may be
exported for local or online play. Online matchmaking only offers a custom map
when both peers advertise its identical package signature.

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

Object designer **Logic** has **Blocks** and **Advanced Lua** for update, creation,
animation completion, object/player contact, pre-damage filtering, damage, defeat,
signals, and removal. The supplied event block already selects the current object
and event; attach actions underneath it. Per-event
block layouts travel with the map's object-editor metadata. Existing handwritten
callback code is retained in an opaque Lua block when switching to Blocks.

The numeric object properties include **width scale**, **height scale**, and
**picture rotation**. These are rollback-owned per live object and compose with
the design's normal visual transform;
negative values flip one visual axis. They do not resize detection, Solid, attack or
damage-receiver areas. Authors can therefore animate squash/stretch explicitly
without unexpectedly changing collision. Object designs provide a base rotation,
while each placed or scripted instance can add its own rotation. Mirrored copies
negate the combined angle. The Object inspector edits one copy's initial value.
Open it at any time for a selection hint; with the Select tool, click a custom
object to inspect that copy or drag to select an area. The Eyedropper remains a
quick way to select a copy and its design. Its room-local Object copy menu also
reaches overlapping or visually hidden copies, and **Edit object design** opens
that copy's shared art, hitboxes, and logic directly. Logic can change the
initial value later without rotating any authored region.

Both logic editors include repeat, while/until, and counted-for loops, an every-N-ticks condition, deterministic
random integers, absolute value, rounding, square root, degree-based sine/cosine/
tangent, sign, minimum/maximum, clamp, interpolation, range mapping, point
distance, and point direction. Clamp accepts its limits in either order. Range
mapping returns the output minimum when its input range has zero length, so a
variable mistake remains deterministic instead of producing infinity or NaN.
Square root, degree trigonometry, point distance, and point direction compile to
bounded arithmetic implemented by Greggnogg because the rollback VM intentionally
does not expose platform-dependent `math.sqrt`, trigonometry, or `atan2`.
Object event/creation blocks offer current-map object pickers. Missing
saved object IDs remain explicitly labeled instead of selecting another design.
Loops and random calls retain the runtime's execution limits and rollback rules.
The random-integer block evaluates each endpoint once and accepts either endpoint
order, so swapped variables cannot reject a preview with an empty interval.
**Stop this loop** emits Lua `break` and is accepted only inside repeat, while,
counted-for, list, active-player, or object loops. **Stop this event or function**
returns early from the current callback or reusable function; inside an incoming
damage check it preserves the current allow/deny result. The editor intentionally does not
offer a continue block because Lua has no direct continue statement. The
**Functions** category creates reusable action functions or value-returning
functions. Its standard mutator adds parameters; parameter getters and calls are
saved with the block layout. Function code executes inside the same bounded map
VM as its caller, including the normal instruction budget and rollback rules.
Persistent values still use Greggnogg's map, player, or object variable blocks.
Functions made in an object callback are emitted locally inside that callback,
so their blocks may use **this object** without creating script globals.
The **Text** category includes join, length/empty, first/last index, character,
substring, case, trim, and reverse blocks. Random-character selection uses the
map's deterministic random stream. Gameplay logic intentionally omits printing
and interactive text prompts.

The **Data** category creates temporary lists of up to 32 connected values and
provides length, empty, first/last/random item, first/last search, and for-each
blocks. Positions are one-based and a missing item returns no value. Random item
selection uses `map.random`, searches use exact deterministic equality, and
oversized or sparse lists stop the callback with a useful runtime error. Lists
are temporary callback data; saved per-map, per-player, and per-object values
remain scalar variables.
It also constructs, adds, subtracts, scales, measures, normalizes, and reads X/Y
components from temporary 2D vectors. Zero-length vectors normalize to `(0, 0)`.
Explicit **as text** and **as true / false** blocks avoid relying on implicit
display conversions. **As number** accepts numbers, booleans, and strict decimal
text with an explicit fallback for invalid input; it does not accept locale
separators, whitespace, or exponent notation.

Objects and players can also be used as typed values. **Named object** resolves
a placement from the current map, while **this object** supplies the object in
the current callback or object loop. Reference blocks can check whether that
object still exists, read its type or numeric properties, change it, or remove
it. A removed or missing named object returns safe fallback values and makes
change/remove actions do nothing. The player reference block selects player 1,
player 2, or the player supplied by the surrounding event/loop. References can
flow through lists and function parameters; general reference blocks can check
activity, read number/text/condition data, move or accelerate the selected
player, deal damage, or defeat them. Object handles are opaque strings and
player references are slot numbers, so both remain deterministic map data.

Use **Players ? for each active player** to react to players in object or map
logic. The typed reporters use one consistent **player [this player / 1 / 2]
[property]** selector for number, position, speed, native facing/sword ownership, health, appearance, ground state,
and held/pressed/released controls. Inside a player loop, event, contact, or damage
callback, **this player** resolves to that callback's player; velocity
and defeat actions can target **this player**. Absent players
are skipped. Observations describe the query snapshot; query again after actions
when you need updated values. Player actions still require tick/update context.

Map logic also offers **when [player] [presses/releases/holds] [action]** for jump,
attack, directions and menu. It uses configured game actions rather than physical
keyboard keys, so keyboard and controller rebinding work identically and input edges
remain rollback-derived. Inside other player scopes, **this player
[holds/just pressed/just released] [action]** provides the same complete action set
as a composable condition. Authors may add several distinct player-action events
alongside **every game tick**; export merges them into the runtime's single
deterministic tick callback. Exact duplicate action events are rejected.

Use **when timer [name] finishes** to declare a deterministic named timer. The
Game category can start it once, start it with a repeat interval, cancel it, or
read whether it is active and how many ticks remain. Timer action dropdowns list
the timer events in the current workspace, and saving rejects missing or duplicate
definitions instead of exporting a script that fails during preview. Timers use
simulation ticks, survive rollback, and share the runtime limit of 32 names.

Use **Objects ? with named object** when logic needs one placed instance, such as
an indicator. Its dropdown comes from the current map and supplies the same
this-object position, speed, animation and visibility actions as an object callback.
The `maps/double_jump_demo` package includes editable map and object blocks for an
extra airborne jump, two player indicators and a collectible recharge.

Player replacement-picture blocks use the selected object's declared animation
catalog. Their animation dropdown updates with the chosen object, includes
`default`, and preserves an imported missing animation as an explicit warning
choice rather than silently substituting another clip.
The **when [player] custom picture animation finishes** event uses the runtime's
one-tick completion edge, so it runs once even when a fast animation skips across
its final frame. It can sit beside game-tick and player-action events; the block
compiler folds all three into one deterministic tick callback.

The player lifecycle event offers **spawns**, **respawns**, **enters a room**, and
**leaves a room** for either player or a specific player. Inside a transition,
**this player room number** is the destination and **previous room number** is
the room being left. Matching boolean reporters are available in player loops.
These use dedicated rollback observation history and do not create hidden map variables.
The Game category also reports the number of placed rooms, the starting placed
room, and a placed room's stable name, source-room name/number, world bounds,
mirroring, and start status. Room numbers are zero-based to match the native
`player room number` field. These metadata blocks work with arbitrary 2D room
graphs, so scripts do not need to derive identity from X coordinates.
**Move player to placed room** accepts one of those zero-based room numbers and
room-local pixel coordinates. Its native room and translated world position are
committed together, keeping cameras, transitions, and rollback synchronized.

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
**Can interact with vanilla physics-triggered objects** lets the object activate a mine when
its origin enters the mine tile. It is an object property, so authors do not need
to build a repeating mine action in Blocks.


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
Object variables use a separate 512-entry generation-aware store, so they do not
consume these shared slots.

Variables alone do not implement double jump: the bounded map API still needs
player input/ground-contact observations and authorable player-following indicators.
Detection areas can be named in API 30; solid-region blocking is available in API 25.
Detection-area controls now sit beside the object preview on wider screens and
stack below it on narrow screens. Sprite scaling uses nearest-neighbor drawing.


### Coordinate sensing

New variables start with the neutral name `variable`. The toolbox has one Variables
category; existing map-value blocks remain readable in saved projects. Game tick,
periodic timing, and coordinate sensing are in **Game**. **Players** contains
reporters for this player's position, velocity, number and contact radius inside a
player loop. **Objects** contains position, velocity and animation-tick reporters.

**At x/y is [tile or object]** accepts any number or arithmetic blocks for its
world coordinates. Its picker lists normal tiles, custom tiles declared by the
current map, and every current-map object design. Tile checks compare the exact
one-character symbol placed in `data.map`; a custom tile using `$` still reports
`$` even if its collision fallback is `x`. Object checks test all authored regions.
Combine detectors with ordinary **OR** blocks to match any list of tiles and objects.

For a patrolling object, create a per-object `direction` variable. Check the point
at `this object x + direction`, `this object y`; if it is `@` OR `_` OR a custom
wall object, multiply `direction` by -1. Then explicitly set the object's x to its
current x plus `direction`. Movement is controlled by those authored blocks.

Advanced Lua uses `map.tile_at(x, y)`, which returns an authored symbol or `nil`
outside the full map, and `entity.at(x, y)`, which returns stable instance handles.
Both are callback-only bounded queries. Mirrored rooms are resolved to the correct
authored cell. The earlier terrain-ahead, solid-box and reverse-direction blocks
remain readable in existing projects but no longer appear in the toolbox.


### Creating variables and explicit movement (API 29)

Use **Variables ? Create variable?** to name a variable, then select it from the
variable blocks' dropdowns. **This object** stores a separate value for each live
instance, using its generation-aware handle. Object-variable names are limited to
20 characters in the beginner picker, matching map/player names; Advanced Lua accepts
1-32 byte names. Object values use 512 entries separate from the 64-entry shared
map/player store. They are cleared after a successful removal callback and restore
with rollback. Advanced Lua uses `entity.value(handle, "direction")`,
`entity.set_value`, `entity.change_value`, `entity.has_value`, `entity.value_keys`,
and `entity.clear_values`.

New object designs disable automatic velocity integration. Set this object's x or
y from ordinary arithmetic blocks to move it explicitly. Existing maps retain
their previous movement behavior. The standalone automatic-velocity control has been removed. Enabling gravity
or terrain collision opts into its physics integration. Advanced Lua uses `entity.set(handle, {automatic_motion=false})`; animation
continues while automatic movement is disabled. Automatic motion is no longer a
beginner flag choice.

Independent per-object variables are available in API 29. Exact tile/object-at-coordinate
matching is available in API 28. Named detection areas are available in API 30:
edit the name beside the preview, then choose that name in a player/object touching
block. Names distinguish multiple areas with the same purpose and are included in
the native package fingerprint. Solid custom hitboxes are available in API 25.
The dropdown handler restores workspace
focus and popup focus containment before opening; live post-drag acceptance remains
unverified by automation.

API 32 replaces the touching block's generated rectangle/circle math with
`entity.player_contacts(handle, selector)`. The Object logic event picker also
offers **When touching another object** and **When touching a player**. Contact
blocks can test the current named area, the other area's purpose, or the other
object's type. Inside a player-contact event, ordinary **this player** reporters,
input conditions, velocity actions and defeat actions refer to the contacted
player. The runtime takes one stable post-movement contact sample and rolls back
all script/entity changes plus queued native player writes if a callback fails.

API 33 adds **When taking damage** to Object logic. **Deal damage** can target
this object or the contacted other object and may identify this/other object as
the source. Damage-event reporters expose the amount, source, remaining health,
maximum health, and whether the hit defeated the target.

Health is optional. The Object designer's **Health and damage** section enables
health and sets its maximum and starting values without requiring creation logic.
Advanced authors can still use **enable this object health** in **When created**
when initialization depends on other logic. Damage subtracts health automatically. **When taking damage** can
play any animation or change the object's color for a hit reaction. **When
defeated** can play another authored animation, remove the object, or run any
other block sequence; reaching zero does not force removal. Dedicated blocks
read, set, increase, and heal current or maximum health without asking for a
variable name. Invulnerability blocks expose whether a damage attempt was blocked
and how much damage was actually applied. A disable-health block returns an object
to fully author-managed behavior.

The same section chooses which native attacks hurt the object and shows their real
amounts: punch 12, kick 25, and held sword, thrown sword, spike ball, or mine blast
100. A **Damage receiver** area is the hurtbox; Body areas are used when no damage
receiver exists. The **this player landed an attack on this object this tick** block
distinguishes a successful hit from merely holding attack while touching the object.

Use **Before taking damage** when an object should accept only selected attacks.
Start with **set this incoming damage allowed to false**, then use ordinary `if`,
`or`, source-type, variables, and comparison blocks to allow the cases you want.
This works for damage sent by custom objects and for typed native punches, kicks,
held/thrown swords, spike balls, and mine explosions. Native player attacks expose
their source player, so **player [this player] holds attack** can be used directly
in a damage filter. The source checkboxes are the quick allow/deny path; these blocks
remain useful for conditional shields, phases, source players, and reactions.

The Object designer's **Can interact with vanilla physics-triggered objects** property activates
a mine under the object through EGGNOGG's native path. The runtime queues the request
until the script transaction succeeds and production ignores coordinates that do
not actually contain a native mine tile. Advanced Lua can use
`map.trigger_mine_at(x, y)` directly when another coordinate is needed.
Arming a mine does not damage the object. Managed mine damage is emitted only when
the native windup reaches its explosion tick. **Knocked away by mine explosions**
independently enables the native 32-pixel radial impulse, even when the object has
no managed health or blocks mine damage.

The Players category has the same optional current/maximum health controls with
player 1, player 2, and context-sensitive **this player** selectors. Player
damage, health-changed, and defeated events can be filtered to either player and
expose event values such as the old health, new health, delta, reason, and source.
Player health reaching zero queues the normal native defeat. Generated Lua uses
the `entity.*health` and `map.*player_health` APIs; callback failures roll the
whole transaction back. Matching player invulnerability and disable-health blocks
are available in the same category.

Player blocks can also set/reset skin or clothing color, body visibility, and the
complete appearance override. **Show player as object picture** reuses any object
design in the current map as a draw-only player sprite, including its named
animation, scale, offsets, rotation, tint and draw order. **Restore normal sprite**
returns to the native composite player. These blocks leave native movement,
collision, combat and damage rules intact.
**Player picture is ... animation ...** lets block logic inspect the active
replacement without maintaining a second variable. **Player custom picture
animation finished** works for any one-shot animation, so logic can restore
another picture without counting ticks.
The nearby picture blocks control width/height scale, offsets, rotation, color,
draw order, animation speed, horizontal flip and visibility. **Reset player picture size,
position and style** restores the object design's authored values.

API 34 adds a configured creation block: create an object at x/y and set one of
its variables in the same atomic spawn. **This object's handle** can be used as
that value for projectile ownership. The receiver's **When created** logic sees
the initialized value immediately. Advanced Lua may pass up to 32 values in
`entity.spawn(..., {values = {...}})`; all use the ordinary per-object variable
limits and rollback store.

API 35 adds **When receiving a signal** plus blocks to send a named signal with
an optional value and source object. Receiver blocks expose the signal name,
value, source presence and source type. Text values are available in the Logic
category, so a receiver can compare names such as `open`, `equip` or
`restore_jump`. A separate named-object signal action selects a placed door,
pickup or controller directly from the current map. Signals let independently placed switches, pickups, doors,
weapons and other objects communicate while keeping each object's state in its
own variables. The generated `entity.signal`/`entity.on_signal` callbacks are
synchronous, bounded and covered by the same transaction rollback as damage.


### Object draw order

Appearance now has a **Draw order** selector: **Behind players (default)** or
**In front of players**. These correspond to visual layer 0 and 1. The runtime
submits default objects before the first native actor pass, and foreground objects
after the final actor pass. The old numeric layer control under offsets is removed.
Solid objects use terrain ordering behind actors, including older exports with a
foreground type visual. A placement's explicit **In front of players** override
still permits a deliberate overlay. Native and custom PNG atlases are flushed at
these boundaries so texture ownership cannot reverse the chosen order.
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
Their definitions enter the matched map-package signature and managed rollback
identity. Contact layer/mask filters affect event queries,
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


### Custom physics against solid objects (API 27)

**Collide with solid map blocks and objects** now tests both native terrain and
custom Solid regions. The moving object's own regions are excluded. It uses the
Movement collision width/height; picture size and detection geometry remain
independent. This provides blocking, not mass, impulses or moving-platform carry.

Advanced Lua can use `entity.solid_box(x, y, width, height, ignored_handle)` to test
a centered rectangle against custom Solid regions. The ignored live handle is
optional. The query returns a boolean, excludes edge-only contact, respects
mirroring and consumes execution budget. Dimensions are 1/256 through 128 pixels;
coordinates and dimensions round to 1/256 pixel. Use `map.solid_box` for native
terrain. Both APIs are useful independently of the optional generated physics.

Older terrain-only physics metadata retains its original generated Lua on import.
Opening that object in the designer and saving upgrades its collision query to
include custom Solid regions.
