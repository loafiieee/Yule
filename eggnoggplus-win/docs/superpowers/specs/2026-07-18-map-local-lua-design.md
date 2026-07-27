# Deterministic Map-Local Lua Behaviors

**Date:** 2026-07-18  
**Status:** Implemented foundation; live game acceptance remains required  
**Primary code:** `map_script.c/.h`, `custom_maps.c/.h`, `content_tiles.c/.h`,
`content_bridge.c/.h`, `hooks.c`, and rollback integration in `lua_manager.c`

## Purpose

V2 JSON describes data: a printable map symbol, its sprite sheet/index, visual
transform, and a safe native collision fallback. It does not need to encode a
new JSON field for every possible behavior. A V2 package may instead place one
optional `map.lua` beside `data.json` and `data.map` for deterministic tile and
map logic.

This is a focused map-behavior API, not the completion of the broad custom
content API. It supports contact-driven physics, map-wide tick logic, bounded
state, deterministic randomness, temporary per-cell sprite changes, and safe
interaction with the already-spawned native point hazard used by `K`. Bounded,
kind/lifecycle-safe temporary velocity limits can bridge native impulses that
arrive after a contact has ended. New
entity types, arbitrary spawning, programmable moving-hazard definitions,
weapons, arbitrary collision solvers, audio, filesystem access, and editor
integration remain separate work.

## Package and identity rules

- `map.lua` is auto-discovered; JSON does not name or embed it.
- Scripts are accepted only for `eggnogg-map/v2` packages.
- The script must be a direct, regular, non-reparse file in the map directory.
- The source is capped at 256 KiB and may not contain embedded NUL bytes or Lua
  bytecode.
- The scanner reads one bounded byte buffer, hashes those exact bytes with
  SHA-256, validates that buffer, and retains it for activation. Activation does
  not reopen the path, preventing a validation/use race.
- The script definition id covers the source digest and the ordered canonical
  symbol-to-qualified-tile bindings. A JSON-only binding change therefore
  cannot reinterpret a rollback snapshot under an old script id.
- Script bytes participate in the map package signature, hot-reload detection,
  and server-selected `map_key`. Peers require the same key string, but its
  existing 32-bit package signature is not a cryptographic content identity.
  The 64-bit runtime id is likewise rollback identity, not a substitute for the
  retained SHA-256 or the still-future complete compatibility manifest.
- A bad newly discovered package is skipped. A bad edit to a known package
  leaves its last valid registry generation installed.

The source and bindings used by native room generation are pinned as one map
generation. A live match does not swap script code underneath rollback. A valid
edit is used the next time that map generation is installed.

## Author API

Tile references are either the one-byte symbol from `data.map` or the tile's
canonical qualified key, such as `map.mossy_caverns:spring`. Unknown references
and duplicate registration for the same event/tile pair reject the package.

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
  local push = tile.mirrored and -0.25 or 0.25
  object.vx = object.vx + push
end)

map.on_leave(">", function(object, tile)
  -- With binding scope, runs after the object leaves every adjoining spring cell.
end)

map.on_tick(function()
  -- Runs once after all contacts in each deterministic native game tick.
end)
```

Callbacks:

- `map.sensor(tile_ref, options)` declares immutable contact geometry for one
  tile binding while the source loads. It is configuration, not a callback.
- `map.on_enter(tile_ref, callback)` fires once when the configured cell- or
  binding-scoped contact begins.
- `map.on_contact(tile_ref, callback)` fires once per deterministic tick while
  the pair remains in contact.
- `map.on_leave(tile_ref, callback)` fires once when contact ends.
- `map.on_tick(callback)` runs once per deterministic game tick after contact
  dispatch.

`object` exposes writable finite `x`, `y`, `vx`, and `vy`, plus
`object:set_velocity(vx, vy)`, `object:add_velocity(dx, dy)`, and
`object:set_velocity_limits(limits, duration_ticks)` plus
`object:clear_velocity_limits()`. Writes are
staged for a callback and committed only after it succeeds. Read-only
`object.kind`, `object.id`, and `object.contact_radius` expose the supported
native physics profile without permitting identity/radius mutation. Object ids are
host-internal: ids 0 and 1 are the player slots, and ids 2 through 17 address
the fixed 16-slot native thing pool. Supported kinds are `player`, state-8
`dead_body`, verified type-2 `sword`, and the opt-in verified type-3 `hazard`
whose updater is exactly the native K-hazard routine. Each reusable thing slot
also has a rollback-snapshotted lifecycle generation and every active contact
stores its explicit kind. A recycled slot is therefore a new contact identity,
and a delayed `on_leave` from its former occupant cannot write into a replacement
of another type.

`set_velocity_limits` accepts a strict plain table containing at least one of
`min_vx`, `max_vx`, `min_vy`, and `max_vy`. Values are finite and in `-64..64`,
paired minima must not exceed maxima, and duration is an integer from 1 through
1,000,000 ticks. A successful call replaces that object's prior temporary limit;
clearing a record that is already absent succeeds without changing state.
After native physics and script callbacks on each covered tick, authored axes
are clamped and the remaining duration advances. Records are keyed by object id,
explicit kind, and lifecycle, so they cannot carry into a recycled native slot.
A live player becoming a state-8 dead body (or returning under rollback) retains
the record because both are phases of the same fixed player-slot lifecycle; no
other kind transition is compatible.
This lifetime is independent of contact history: a spring may protect launch
velocity for several ticks after `on_leave` rather than relying on a single
contact-exit callback.

Native velocity scales differ by object kind. Players and state-8 dead bodies
use gravity `0.15`; type-2 swords and the K-spawned type-3 hazard use gravity
`0.075`. A single raw launch value would therefore send native things about
twice as high. The demo uses `-4.0` for living players and `-2.8` for
swords/hazards, producing an approximately 53-pixel rise for each. It excludes
dead bodies because repeatedly relaunching one keeps the native grounded respawn
gate from completing. Its eight-tick `min_vy` limit also catches the living
player's native unarmed-kick impulse on the tick after launch, even when the
shallow spring sensor has already been left.

`tile` exposes read-only `x`, `y`, `key`, `symbol`, and `mirrored`. Its methods
are `tile:set_sprite(index, duration_ticks [, options])` and
`tile:reset_sprite()`. `options` is an optional strict plain table containing
finite `offset_x`/`offset_y` additive destination-pixel deltas in
`-4096..4096`. Values are canonicalized to 1/256 pixel. Missing fields default
to zero on every call, preventing an older transform from leaking into a new
sprite override.

An override changes only that exact generated cell, uses an index in the tile's
already declared sheet, and expires on the deterministic map clock. Its offsets
are applied after source-cell cropping and add to the declarative JSON offset.
The sprite and transform are one atomic rollback/expiry/reset record. They do
not move the native underlay, map cell, collision, contact sensor, or a native
entity spawned by that cell. Missing/invalid sprites still take the normal
native fallback path.

### Contact sensor geometry

The engine's native collision remains declared by `collision`/`native_glyph` in
JSON. A binding without `map.sensor` preserves the original compatibility path:
players sample their center plus the half-tile foot boundary needed after native
floor collision restores their center outside a solid cell, while swords use
the center point used by their native movement routine.

`map.sensor` replaces those point probes for its binding. Its strict immutable
options are:

- `tile_box = {left, top, right, bottom}`, relative to each cell in tile units,
  defaults to `(0,0,1,1)`, permits edges in `-2..3`, and requires positive
  width/height after 1/256-tile quantization;
- `object_box = "center"` (default), `"body"`, `"feet"`, or a custom
  relative box. `body` is the native-radius AABB and `feet` its full-width
  bottom edge. Custom edges are limited to `-2..2` tile units and must retain
  positive extent after quantization;
- `objects`, a strict non-empty dense selector list. Legacy `"player"` includes
  both living player records and state-8 dead bodies; `"alive_player"` and
  `"dead_body"` select them independently, `"sword"` selects verified type 2,
  and `"hazard"` opts into the verified K-spawned type 3. The default remains
  player/dead-body/sword, excluding hazards;
- `contact_scope`, `"cell"` by default or `"binding"`. Binding scope unions
  intersecting cells of one qualified definition for an object/lifecycle,
  coalesces them to one callback per tick in stable order, treats a direct
  A-to-B cell transition as continuous contact, and leaves only after no cell
  in that binding was touched for a full tick; and
- `mirror_with_room`, default false, which maps horizontal tile-box edges
  `[left,right]` to `[1-right,1-left]` in mirrored rooms.

Native reverse engineering pins the physics radius at object offset `0x6c`:
6 pixels for players/dead bodies, 4 for type-2 swords, and exactly 0 for the
K-spawned point hazard. `center`, `body`, and `feet` use that value. This
contact geometry is deliberately distinct from combat: native
combat hurtboxes are sprite/state-derived in `bbox_hit_ex` and are not mutated
by this API. Detailed custom entity collision bodies, combat hit/hurt boxes,
layers, and masks remain future content-entity work.

The authored limits imply a complete fixed search of five cells in each
direction. The host enumerates valid bound cells top-to-bottom then left-to-right
and uses inclusive AABB edges. Object sensor coordinates are captured once
after native physics/declarative forces and before any map callback. A callback
may still stage `object.x/y/vx/vy`, but its position write cannot change which
later sensors intersect during the same tick. Cell-scoped contacts remain keyed
by exact generated cell; binding-scoped contacts use the canonical binding and
one stable representative cell. Both run after native collision.

### Native `K` is an entity spawn marker

Ghidra confirms that `K` does not make a map tile participate in physics. Room
generation emits pass-through tile type `0x1c`; on room reset its
`spawn_thing_action` creates a separate native type-3 thing. That point-mass
entity owns position/previous position/velocity, gravity, damped center-point
map bouncing, a fixed vanilla sprite, and a fixed 10-pixel player-damage test.
The host admits it to a map sensor only when the native type and updater match
that verified profile; scripts may then change its staged position/velocity.
It still does not use the marker's custom sprite or render offset.

The native action also dereferences `thing_new(3)` without checking allocation
failure. Because the native thing pool is fixed and shared with players,
weapons, and other effects, unrestricted `K` markers can exhaust it and crash.
The package loader now rejects a `K`-bearing room above 13 combined `K`, sword,
and mine reset spawners: Ghidra confirms 16 records, an allocator that skips
slot zero, and two native player allocations. Mine-only and sword-only rooms
retain their established native fail-safe/recycling behavior. Generic
`native_glyph: "K"` remains excluded because a registry owner need not be tied
to an audited map package.
Programmable moving blocks/hazards need a distinct custom-entity system with
deterministic allocation/lifecycle, rollback-owned component state, symbolic
sprites, editable bodies/hit/hurt/sensor shapes, layers/masks, physics
parameters, update order, and room-transition ownership. A visual tile offset
must never be presented as moving that gameplay entity.

## Persistent state and randomness

Arbitrary Lua heap state is not a rollback surface. Persistent behavior must
use `map.state`:

- at most 64 keys;
- keys are at most 31 bytes;
- values are `nil`, boolean, finite number, or a string of at most 63 bytes;
- assigning `nil` deletes a key; and
- negative zero is canonicalized and NaN/infinity are rejected.

Registered callbacks may not retain mutable captured upvalues or writable
global state. This ensures a save/load cannot restore the C snapshot while
silently keeping a different Lua closure counter.

`map.random()`, `map.random(max)`, and `map.random(min, max)` use the runtime's
own deterministic, snapshotted generator. `math.random` is unavailable.
`map.tick()` returns the snapshotted map clock. Scripts must not derive gameplay
from render time, wall time, process state, pointer strings, or table iteration
order. The numeric `^` operator is rejected because it routes through
platform libm/pow behavior that is not bit-stable enough for cross-CPU rollback;
use explicit multiplication for bounded powers. The source scanner distinguishes
operators from harmless carets inside short strings, long strings, and comments.

## Sandbox and resource boundary

Each activated map gets its own LuaJIT 5.1 state, separate from framework mods.
It uses a capped allocator (2 MiB by default) and a per-load/per-callback
instruction budget (100,000 by default). The callback environment is frozen
after loading. Filesystem, process, network, dynamic-code, bytecode, module,
debug, FFI, JIT-control, coroutine, and nondeterministic RNG surfaces are not
available. Userdata metatables are protected and expose only the methods above.

An instruction, memory, type, API, or callback failure faults the runtime,
logs a bounded diagnostic, discards partial object changes, and stops further
script callbacks. The fault flag is rollback state. Native collision and the
declared base sprite remain available, so failure is fail-closed rather than a
native pointer/callback escape. Loading a healthy pre-fault snapshot clears
local-only stale error text; loading a faulted snapshot installs one canonical
rollback-restored diagnostic rather than leaking a peer-specific callback
message into later reporting.

## Rollback and rendering

`MapScriptSnapshot` is fixed, packed, pointer-free POD. It contains:

- the composite script id, map tick, deterministic RNG, and fault flag;
- the bounded typed `map.state` store;
- per-cell sprite/index, fixed-point render-offset, and expiration overrides; and
- current object kind/lifecycle and cell- or binding-scoped contact history,
  temporary velocity-limit records, plus the 16 reusable-slot lifecycle
  generations needed for correct enter/stay/leave and limit behavior.

The full game-state blob embeds map-script snapshot version 5 and advances to
blob version 9 and layout schema 6. Save, header validation, and load all include the script
snapshot. Load validates the active script id and complete internal checksum
before any native game memory is changed, then restores POD without executing
Lua. Snapshot version and size participate in the prematch rollback-layout
fingerprint and exact capacity proof. The source-visible/host dispatch contract
has map-script API version 5; that version is also mixed into the structural
layout fingerprint so peers cannot start with incompatible sensor/visual
override semantics.

All native update owners call the same post-update behavior path: ordinary live
play, local/loopback advancement, GGPO live advancement, and rollback replay.
The renderer queries the snapshotted per-cell override after base animation
selection and before resolving the sprite. Map scripts are deterministic map
content and therefore remain active online; they are not the general gameplay
mod VM that online matchmaking suspends. During managed prematch, native reset
first deactivates any stale map VM. Before the rollback layout can be finalized
or READY can be sent, the exact script id from the mapgen-pinned generation must
be active and non-faulted. Bind/activation failure aborts the match setup with a
player-visible reason and a server `match_abort`; it never silently starts the
map with missing behavior.

## Acceptance

Automated coverage must prove:

- valid callbacks, state, RNG, object writes, mirrored tiles, sprite expiry,
  enter/stay/leave order, exact-cell separation, binding-scope seam coalescing,
  temporary velocity-limit validation/expiry/replacement, reusable-slot
  kind/lifecycle separation, stale-record write rejection, and snapshot round
  trips;
- rejection of unsafe globals/libraries, bytecode, unknown bindings, duplicate
  handlers, captured mutable state, numeric power operators, invalid values,
  memory exhaustion, and instruction exhaustion, while carets in strings and
  comments remain valid;
- inactive/active/faulted snapshot validation and script-id mismatch rejection;
- direct/non-reparse/size-bounded package loading and exact-byte SHA identity;
- source-only and binding-only changes alter the package/script identity;
- activation uses the pinned generation and deactivates on vanilla/bind/error
  paths;
- online READY fails closed unless the exact pinned script is active and
  healthy;
- no-sensor bindings retain player center/foot and sword center probes;
- sensor parsing rejects malformed/sparse/duplicate/unbounded configuration,
  overlapping selectors, and invalid contact scope; fixed-point tests cover
  center/body/feet/custom profiles, player/dead/sword/hazard filters, mirroring,
  inclusive edges, full spring width, vertical cutoff, and cell seams;
- the host verifies native player/dead/sword/hazard type, updater, and radii,
  searches the complete fixed neighborhood in stable y/x order, and uses
  immutable pre-callback coordinates;
- temporary sprite offsets reject malformed/non-finite/out-of-range options,
  compose additively after atlas cropping, preserve complete turtle state, and
  expire/reset/snapshot with their sprite as one record;
- unsupported native thing types receive no guessed collision bounds, and a
  non-K type-3 updater is rejected;
- all three native update owners dispatch behavior exactly once; and
- a full DLL link includes `map_script.c` and LuaJIT.

The checked-in V2 demo is the live acceptance fixture. Its spring behavior must
launch when walking into or landing on the left, center, and right portions of
its full-width six-pixel lower-body/probe sensor, must not trigger above that
shallow region, and must temporarily change only the representative cell's
sprite. Adjacent spring cells form one binding-scoped pad, an airborne unarmed
attack cannot stack extra upward speed during the eight-tick post-contact limit,
and the explicitly selected living-player/sword/K-hazard profiles all participate.
Living-player `-4.0` and sword/hazard `-2.8` targets must reach approximately the
same 53-pixel height under their different native gravity scales. Dead bodies
must remain outside the spring sensor with unchanged velocity and complete normal
native respawn. The active frame uses its checked-in user-tuned render-only offset,
without moving collision or native underlay. `native_visual: underlay` must
retain the resolved ordinary floor beneath the spring. Its fan's full-cell body sensor
must accelerate in opposite directions in normal and mirrored room copies. The
same fixture declares its external atlas once at `tileset` scope and uses
`native_layout` for ordinary, unlisted terrain. A two-client run must preserve
the same behavior through prediction and rollback.
