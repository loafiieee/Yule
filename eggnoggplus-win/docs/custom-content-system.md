# Custom-content system implementation contract

Status: active implementation. This document separates intended public behavior
from code that exists today. Managed entity scripting/rendering is implemented;
weapons, native combat, advanced authoring and online admission remain unfinished.

## Authoring model

Object authoring is integrated into the current Greggnogg map. **Objects** edits
appearance and logic; **Custom tiles** in the normal palette handles placement.
This follows the user's later correction to the original separate-workspace plan.

A project owns named assets, reusable entity definitions, weapon definitions,
placements, and behavior scripts. A weapon is composition (held presentation,
attack states, hit regions, projectiles and effects), not a recolored vanilla
sword. A moving object is a framework-owned entity, not a native K marker or a
visual offset on a stationary tile.

Definitions should support sensible defaults without hiding the
underlying data. Authors must be able to override defaults in code or the editor;
manual edits and editor exports use the same validated representation. Unknown or
unsupported functionality must produce a precise diagnostic, never silently
substitute the nearest vanilla behavior.

## Shared entity contract

- Stable generation-qualified handles and explicit ownership. Removing/reusing an
  entity never makes an old handle point at its replacement.
- Independent transform, visual/animation, physics body, sensor, hitbox/hurtbox,
  collision filter, script state and lifetime components. Combat geometry is not
  inferred from the sprite. Tile-grid size must not constrain entity geometry.
- Ordered lifecycle notifications: spawn, fixed update, contacts, damage and
  removal. Define creation/removal ordering and callback reentrancy before exposing
  these to scripts. Queue structural edits at documented simulation boundaries.
- Distinct physics and combat response. Authors choose blocking, sensing, damage,
  impulses and custom callbacks; a collision layer is not itself a damage rule.
- Explicit native-player bridge. Only verified reads/writes enter native player
  state. Do not expose raw addresses as the ordinary entity API or reuse the
  native 16-slot allocator for arbitrary content.
- Deterministic animation state and entity-local script state share the snapshot
  transaction. Asset/GPU handles are presentation resources, re-resolved from
  stable asset identities after restore or reload.

## Online policy

Preserve the existing restriction on unrestricted gameplay mods until the managed
content runtime covers their behavior. A definition is not online-safe merely
because its metadata says so. Use a versioned manifest of framework/content API,
ordered definitions, assets, scripts, placement, rule configuration and execution
budgets. Peers must agree before instantiating content.

Online simulation uses fixed steps, owned random streams, deterministic callback
ordering and bounded resources. Wall time, arbitrary host I/O, native pointers,
untracked Lua heap mutation and unvalidated native writes do not belong in that
surface. Powerful host integrations may exist offline, but the editor/loader must
explain why a project cannot enter managed online play. Never silently disable a
component or change behavior to make it connect.

Resource limits are explicit configuration negotiated by peers, with allocation
failure diagnostics. These limits bound memory/work; they must not masquerade as
arbitrary restrictions on what a weapon or object can do.

## Implemented foundation

`entity_world.c` / `entity_world.h` now provide independent entity allocation,
generation handles, copy-based access, immutable type identity, stable slot-order
enumeration, checked fixed-point motion, and a canonical little-endian snapshot.
Capacity is explicit (1..4096). Snapshot decoding validates the entire document
before mutation; exhausted generations retire a slot. Movement overflow rejects
the whole tick before any entity moves. The format is internal YEW1/version 2,
not yet a network envelope and not a public project format.

Guarded tests cover slot reuse and stale references, all truncated snapshot
lengths, invalid state/count/reserved fields, exhausted generations, whole-world
movement atomicity and 2,048-tick save/replay equality. Run:

```powershell
./tests/run_core_native_tests.ps1 -EntityOnly
```

The world is now connected to map loading, scripting, native snapshots and sprite
submission, as detailed below. Native collision response/combat, shared public
registration, editor export and online admission remain open. The broad backlog
is not complete.

## Integration sequence and acceptance

1. Definition registry and schema: component validation, ownership, stable hashes,
   bounded dependencies and transaction-safe replacement. Connect the world to
   definitions; reject unknown type IDs before any snapshot commit.
2. Native fixed-tick and snapshot adapter: include the world and queued lifecycle
   state in rollback/correction, and ensure any failed native commit is atomic.
3. Geometry/contact system and player bridge: deterministic broad/narrow phase,
   separate sensor/physics/combat regions, native player lifecycle safety, and
   moving-hazard demonstration on both mirrored sides of a room.
4. Public Lua facade: entity-local state, creation/removal, component inspection,
   lifecycle callbacks, deterministic errors and beginner presets. Document every
   phase using runnable examples and generated API reference entries.
5. Rendering/animation, then weapons: resource ownership, projectiles, attacks,
   independent hit/hurt regions, equipment lifecycle and native interaction.
6. Advanced editor workflow: create/import asset, animate, draw regions, attach a
   script, place instances, preview, inspect errors, undo, export and reopen.
   Prototype this flow before making export a supported user-facing feature.
7. Online admission and paired soaks: mismatched dependencies rejected, repeated
   spawn/despawn/death/reuse through rollback and correction, equivalent native
   save-load-save state, bounded stalls, and actionable diagnostics.

All seven stages are required before calling the broad custom-content API complete.

## Definition and contact implementation update

The internal world now accepts immutable type definitions before the first spawn.
Each type has independent body/sensor/hitbox/hurtbox rectangle regions, local
fixed-point offsets, stable region IDs and bilateral layer/mask filtering. Region
purpose is reported to the caller; it does not impose automatic damage rules.
Unknown type IDs are rejected on spawn and restore. Definitions are copied and
validated atomically, including duplicate IDs and invalid region geometry.

Typed snapshots include canonical definitions and compare them byte-for-byte
before any entity state is restored. This prevents same-ID/different-geometry
restores; it does not replace the future script/asset compatibility manifest.

Contact queries use stable entity-slot then region-declaration order and exclude
edge-only touches. Callers can query the required output size without allocation;
an undersized buffer remains untouched. The reference implementation rejects work
beyond one million comparisons, before writing results. A spatial broad phase,
swept collision, non-rectangular shapes, response solver and native bridge remain
required before this becomes a production moving-object system.

Guarded tests now also cover copied-definition isolation, rejected duplicate or
invalid definitions, same-ID geometry mismatch rejection, body vs combat overlap,
layer filtering, edge contact and output-buffer atomicity.

Type catalogs are canonicalized by numeric ID before use and serialization;
registration order cannot cause a mismatch. Runtime type lookup uses binary search
rather than rescanning the whole catalog inside each contact candidate pair.
Region declaration order remains the explicit tie-breaker for contact ordering.

## Fixed-update transaction implementation

`entity_world_update` now snapshots the world, captures the entry handle list,
invokes the host callback for each still-live entry in slot order, and integrates
motion once. A callback may spawn/remove/write entities. New or reused handles
are not revisited during the same callback pass; their motion begins that tick.
Callback failure, attempted nested advancement/restore, or motion overflow restores
world state and allocator generations before returning failure. The callback host
must include external script state in its enclosing transaction; this function
cannot roll back arbitrary host I/O or caller-owned memory.

Tests exercise removal of a later entry, immediate slot reuse, skipped replacement
updates, callback failure and nested-tick rejection with byte-identical restoration.
Spawn/removal notifications, the Lua binding and the native fixed-tick adapter are
still integration work; this internal callback is not yet exposed to content authors.

## Lua adapter implementation

`entity_lua.c` supplies the internal spawn/get/set/remove/exists/list facade over
an explicitly owned `EntityLuaBinding`. Types resolve through a host-provided
name lookup. Handles are opaque 16-character strings, retaining all generation
bits and fitting the existing rollback-owned map.state string representation.
Positions and velocities use ordinary pixel numbers in Lua and validated 1/256
pixel fixed-point values internally. Returned property tables are detached copies.
All supplied properties validate before a write; unknown fields are errors.

The adapter cannot advance or restore the world, access native pointers, open
files or register arbitrary host callbacks. The embedding host owns sandbox and
instruction/memory budgets, binding lifetime, and the combined script/world
transaction. Close the Lua state before freeing the binding/world. The adapter is now installed in the managed map VM through the explicit content
activation entry point described below. Production map loading and native rollback
still require integration before packages can be played.

The guarded EntityOnly runner exercises a real LuaJIT VM: type resolution, handle
reuse/rejection, numeric validation, copy isolation, field-error atomicity and
world restoration after a failing Lua update that both writes and spawns.

## Owned package instances and placements

`entity_package.c` constructs a complete isolated world from copied named type
and placement records. Qualified type keys use owner:name identifiers; placements
have unique local names. The catalog sorts by key before assigning numeric IDs,
and placements sort by name before allocation, so input declaration order does not
alter the initial state. Unknown references, duplicate names, invalid geometry or
positions, and insufficient capacity reject construction without publishing a
partial instance. Error text describes the rejected category.

A SHA-256 identity covers canonical type names, placement names and the complete
initial world (capacity, definitions, geometry and placement values). The package
snapshot envelope verifies that identity before delegating to the atomic world
decoder. Scripts/assets are not covered yet; the future full manifest must include
them before online admission. `entity_package_resolve_type` plugs directly into
the Lua adapter's name resolver. Placement lookup returns the original generation
handle; it does not attach an old name to a replacement after destruction.

Guarded tests compare reordered declarations, independent copies, changed names,
changed placements, unknown references, malformed names and mismatched snapshot
rejection. These are internal C package records so far: JSON import, public
registration, map ownership, native integration and editor export remain pending.


## Package JSON decoding (internal schema 1)

`entity_package_decode` now loads an isolated package directly from JSON bytes.
The decoder uses the existing bounded JSON parser (1 MiB input, depth 32,
65,536 nodes), with no script execution or Lua libraries. It rejects duplicate
JSON keys, unknown fields, object/array confusion (including empty collections),
null or coerced numeric values, malformed identifiers, unknown placement types,
duplicate names/region IDs, invalid dimensions and unsupported schema versions.
Failures include a JSON path; nothing is published until the entire package passes.

See [the package example](examples/entity_package.json). This is an internal
runtime fixture, **not yet a file that Greggnogg imports**. Top-level
`schema`, `capacity`, `types` and `placements` are required. Capacity is 1..4096;
types contain a qualified `key` and a `regions` array (at most 16). A region has
an integer `id`, `role` (`body`, `sensor`, `hitbox`, `hurtbox`), unsigned 32-bit
`layer` and `mask`, and positive `width`/`height`. Local `x`/`y` default to zero.
Placement `name` is local and unique, and `type` references a qualified key.
Placement `x`, `y`, `vx`, `vy` default to zero. Region roles currently describe
contact purpose; this example does not yet damage players or resolve physics.

Positions, dimensions and velocities are in pixels (velocity per fixed tick).
Values round to the nearest 1/256 pixel, ties away from zero. Positive dimensions
must be at least 1/256 pixel before rounding. Coordinates are bounded to
+/-4,194,303.99609375 pixels. Type keys require exactly one namespace separator,
and each identifier is at most 96 ASCII bytes using lowercase letters, digits,
`_`, `-`, `.` (plus the colon in qualified keys).

JSON spelling/order does not enter the fingerprint: decoding produces the same
canonical package identity as equivalent native records. The guarded package
tests compare those identities, verify fractional coordinates and full 32-bit
masks, reject every truncated prefix of a valid document, and exercise malformed
shapes, keys, references, embedded NULs, nulls and limits. The wider API remains
open until the native, script, rendering, editor and online stages above are wired.


## Managed map runtime integration (source API 13)

The host can now call `map_script_activate_content` with a map-script definition,
host services and package JSON. Activation constructs both in isolation; invalid
packages, script errors or sandbox violations retain the previous live runtime.
The runtime owns the package, Lua binding and a reusable entity checkpoint. Lua
closes before package memory is released.

The reserved, read-only `entity` table exposes `spawn`, `get`, `set`, `remove`,
`exists`, `list` and `on_update`. Without content activation, entity operations
report that no entity runtime is available. Register one
`entity.on_update("owner:type", function(handle) ... end)` per type while the
script loads. As with map callbacks, captured upvalues and mutable globals are
rejected; persistent handles and values belong in `map.state`.

After synthesized leaves, timers and `map.on_tick`, live entities receive their
registered updates in stable slot order, then motion integrates once. Removed
entities are skipped. Entities created during that entity phase first receive an
update on the next tick, but integrate their initial velocity during the current
tick. Entities created earlier by a map callback join the current entity phase.
All entity callbacks share one configured instruction budget for the phase; each
callback is conservatively charged its unmeasured final hook quantum. This keeps
many short callbacks from evading the total budget. The world remains a bounded
reference implementation and currently allocates its update scratch buffers.

Contact callbacks and tick dispatch checkpoint both map state and the owned
entity package. Script errors, budget exhaustion or invalid motion restore both
before applying the existing terminal timeline fault policy. Entity callback
argument allocation occurs inside the protected Lua call. Native object writes
retain their existing transaction boundaries.

Hosts use `map_script_content_snapshot_size/save/validate/load` for a combined YMC2
snapshot. Loading checks the map snapshot, package fingerprint, full entity state
and matching map/entity clocks before committing either component. The ordinary
fixed map snapshot save/load functions deliberately reject an entity-bearing
runtime. This prevents current native serializers from silently dropping entities.
YMC2 is a same-platform host snapshot, not an approved network envelope. The fixed
MapScriptSnapshot remains version 6; API negotiation advances to 13 because the
reserved Lua surface changed.

`tests/run_map_script_test.ps1` now exercises the real managed VM with entity
packages: 256-tick replay, combined-state byte equality, failed replacement,
truncated/mismatched snapshots, clock mismatch, callback rollback, spawn ordering,
read-only globals and API tables, captured-upvalue rejection, and infinite-loop
budgets. Production V2 map loading now calls the content entry point for entities.json;
rendering, native player interaction, complete lifecycle/equipment callbacks,
native correction serialization and online admission remain open.


The read-only `validate` entry point checks the same combined state without
mutating the world. Native serializers can preflight all their components before
committing any game memory. Entity worlds and packages expose corresponding
read-only snapshot validators. Native EGG0/correction integration now consumes this preflight as described below.


## Native rollback extension (EGG0 version 12)

The native serializer now includes a YMC2 extension after its ordinary native
payload whenever a managed entity package is active. The fixed header and native
player/thing/tilemap offsets retain their v10 layout. Maps without entities have
no extension. The outer version advances to 12, so peers with older serializers
cannot mistake the new format for a compatible state layout.

Sizing and layout negotiation include the active extension. Capture saves both
managed components, then copies its map snapshot into the ordinary header.
Preflight checks the complete native layout, writable native destinations,
managed package identity, clock agreement and byte-for-byte agreement between
the two map snapshots. Restore commits managed state before the native memory
copy phase. A malformed extension cannot leave native memory partly restored.
The extension participates in canonical state bytes and checksums.

The native serializer test fixture covers a live managed runtime: save, movement,
native mutation, load and byte-identical canonical resave; foreign fingerprints
and truncated extensions must fail without changing either managed state or
native globals. The trace analyzer recognizes v9/v10/v11/v12 and reports extension
hashes/differences as `managed_content`, without disclosing state bytes.

Production discovery and offline activation are described below. Native rendering,
combat, full content manifests and paired network acceptance remain open.


Package discovery can now use `map_script_validate_content` to build an isolated
entity-aware VM, execute declarations, validate callbacks and destroy it without
replacing the active map. Tests include successful validation with entity spawns,
failed initialization after a spawn, malformed package input and missing input;
the live combined snapshot remains byte-identical after every attempt.

Discovery integration must also enforce its policy for direct LAN sessions.
The existing online-hub pinned-script check alone is insufficient coverage for
that admission rule. Both paths now enforce the offline-only decision described
below; online eligibility still requires the full future admission work. The combined runtime now binds actual source/configuration as described below.


## Combined content identity

YMC2 has a 48-byte header: magic, a 32-byte SHA-256 identity and twelve reserved
zero bytes. The identity binds the exact Lua source bytes, canonical entity-package
fingerprint, ordered normalized tile bindings, map API version, effective memory
and instruction limits, and the initialization RNG seed. The seed matters because
loading Lua can derive immutable declarations from random values. Diagnostic
chunk names and host callback addresses do not enter this identity.

An explicit default limit and an omitted default limit produce the same identity.
Reusing a host-supplied script ID is insufficient to load a snapshot with changed
source, bindings, limits or seed. Identity validation precedes either component's
commit. Focused tests exercise each mismatch and preservation of the current
combined state. Native transport uses EGG0/v12 for the changed extension format;
the trace reader retains v9/v10/v11 support alongside v12. This identity is still
not the full future asset/dependency manifest or permission to admit online content.


## V2 package discovery and offline activation

A V2 map folder can now include a direct `entities.json` file (at most 1 MiB)
using the package schema above. `map.lua` is optional; when absent the loader uses
an empty script and entities still integrate their declared velocity. With a
script, declarations and per-type update callbacks validate in an isolated VM.
Invalid JSON, references or Lua reject the candidate registry generation.

The file reader uses the same locked-handle, direct-file and non-reparse checks
as map.lua. Validated bytes remain owned by the map registry; running maps keep
the exact pinned generation through reloads. Entity file bytes enter the registry
poll digest and script identity. Same-size edits with restored timestamps are
therefore detected. Retirement, failed validation and shutdown free entity bytes
alongside script bytes. No script or JSON is reread during gameplay activation.

Entity-bearing maps are currently offline-only. They are omitted from the online
map manifest and online key resolution. Startup rejects a direct LAN session on
such a map; held online setup checks the newly generated map rather than the
previous offline map; the common network gameplay-tick path also stops before
simulation if entity content appears after startup. Local rollback and serializer
tests remain supported. These restrictions are temporary admission policy, not a
claim that entity content already has multiplayer acceptance.

The guarded V2 fixture exercises no-script activation metadata, malformed files,
V1 rejection, a directory in place of the file, entity-aware Lua validation,
online manifest exclusion, pinned runtime activation, timestamp-preserving edits,
and old/new generation activation yielding different expected movement. A
fixture-only pin helper avoids native fixed-address writes in that test and is
excluded from production builds.

Discovery initially supplied logical updates and motion. Sprite rendering and
spawn/removal callbacks are now implemented as described below. Collision response,
native player damage, equipment, editor authoring and paired online tests remain open.

## Script contact queries (Map API 14)

`entity.contacts()` returns a detached array of current managed-entity overlaps.
Each record contains opaque handles `a` and `b`, numeric `region_a` and `region_b`
IDs, and `role_a`/`role_b` strings (`body`, `sensor`, `hitbox`, `hurtbox`). Results
use stable entity-slot and region-declaration order. Both regions' layer/mask
filters must agree, and touching edges do not count as overlap. Removed entities
are absent from subsequent queries; previously returned records remain detached.

Call from `map.on_tick` to apply one world-wide interaction pass before entity
updates and velocity integration. Queries reflect mutations already made in the
current callback. This is an overlap query, not automatic enter/leave events,
physical response, native-player collision or damage. Contact-driven mutations
participate in the combined map/entity rollback transaction.

The query accepts no arguments. It raises a diagnostic instead of truncating if
there are more than 4,096 results or the world comparison budget is exhausted.
Scratch storage and returned tables use the bounded Lua allocator. Native query work consumes the shared callback instruction budget: one unit per
slot visit or region comparison, including counting and writing passes. Budget
exhaustion leaves the output untouched and faults the surrounding transaction. Dense worlds
still need the planned spatial broad phase. API 14 enters the content identity;
MapScriptSnapshot and the native envelope formats are unchanged.

```lua
map.on_tick(function()
    local contacts = entity.contacts()
    for i = 1, #contacts do
        local c = contacts[i]
        if c.role_a == "sensor" and c.role_b == "body" then
            entity.set(c.b, { vy = -2 })
        end
    end
end)
```

Guarded adapter tests cover exact handles/IDs/roles, detached results, removal,
edge exclusion and a one-subpixel overlap. The managed runtime fixture uses a
contact to change motion, then restores and replays eight ticks with byte-identical
combined snapshots.

## Spawn and removal callbacks (Map API 15)

Register `entity.on_spawn("owner:type", function(handle, value) ... end)` and
`entity.on_remove("owner:type", function(handle, value) ... end)` while map.lua
loads. Each event accepts one callback per type, with the same no-upvalue,
read-only environment rules as `on_update`. `value` is a detached table containing
`x`, `y`, `vx`, and `vy` at the event. Modifying it does not change the entity.

After declarations finish and callback environments are locked, entities alive
at the end of loading receive initial spawn callbacks in stable slot order. This
includes package placements and entities spawned by initialization Lua. Entities
created and removed entirely during declarations produce no notifications. The
initial handle list is captured before notifications: a removed initial entity
is skipped, and newly spawned entities receive their synchronous notification
without being visited again by the initial pass. Initialization callback failure
rejects the candidate and preserves the previously active map.

During gameplay, `entity.spawn` invokes its callback before returning; `remove`
first removes the entity, then invokes the removal callback. The removed handle
is already stale, so use the detached `value` for its final motion data. Repeated
removal returns false and emits no second callback. A spawn callback may remove
its own entity, so callers can use `entity.exists` on the returned handle.

Callbacks may create or remove other entities. Nested notifications share the
current Lua instruction budget and have a 32-callback recursion limit. Failures
roll back the enclosing map/entity transaction. Snapshot load and runtime teardown
do not emit lifecycle events; restoring a snapshot must not rerun gameplay effects.
Initial callbacks share one execution budget for the entire initial pass.

Guarded tests cover initial/runtime spawn, detached removal state, duplicate
removal, replay equality, failed removal rollback, invalid/upvalue/duplicate
registration, locked initial environments, and recursive initialization rejection.
This adds spawn/removal lifecycle events; native combat, equipment, rendering and
the advanced authoring workspace remain required.

A complete logical spawn/update/remove example is [entity_lifecycle.lua](examples/entity_lifecycle.lua), paired with the existing entity package JSON.

## Entity sprite rendering

An optional type `visual` object selects a sprite independently of its regions:

```json
"visual": {
  "sheet": "builtin:tiles",
  "sprite": 4,
  "frames": 3,
  "frame_ticks": 6,
  "offset_x": 0,
  "offset_y": -8,
  "scale_x": 1,
  "scale_y": 1,
  "tint": "#FFFFFFFF",
  "layer": 1
}
```

`sheet` and `sprite` are required. Omitted values default to one frame, one tick
per frame, zero offsets, unit scales, opaque white, and layer 1. Animation loops
from the world's simulation tick, so rollback restores the exact frame. Offsets
use pixels and scales are multipliers; both round to 1/256 units. Scales may be
negative for flipping but must not round to zero and must lie within ?256. Tint
is exactly #RRGGBBAA. Layers select native sprite batch 0 or 1. There are at most
65,536 frames, 1,000,000 ticks per frame, and the last sprite must fit signed int32.
No visual means invisible; it does not disable entity updates or contacts.

Built-in sheets use existing `builtin:` keys. A direct PNG filename resolves only
to a sheet already declared by the enclosing map's V2 tileset; arbitrary file paths
and other packages' sheets are not permitted. Standalone entity-owned asset
registration and editor-assisted sheet declarations remain to be implemented.
Missing/unavailable sprites are not submitted. Visual metadata enters the package
fingerprint, while runtime atlas IDs are resolved during drawing and never saved.
Packages without visuals keep their previous canonical package fingerprint.

The native bridge submits entities once in the final native thing draw pass,
using the native camera-X subtraction and inverted world-Y translation. Sprite
submission restores all 96 bytes of native turtle state on success or failure.
No Lua callbacks, file reads, RNG draws, or world advancement occur in rendering.
Schema 1 placements use generated-map world coordinates. Schema 2 also accepts
room-local coordinates and source/mirrored/both sides (Map API 20); the loader
expands these before activation and fingerprints the effective placements.

Guarded package tests cover metadata rejection, canonical native/JSON identity,
animation wrap, read-only enumeration, movement and snapshot restoration. Bridge
tests cover independent submission and full state restoration at each failing draw
operation. `tests/entity_render_source_test.py` verifies the draw function prologue,
register argument, call site and camera operations against the actual executable.
Live sprite visibility, camera alignment, scale/tint, atlas rebuild and native layer
ordering still require in-game acceptance; this source integration does not prove
those visual results.

## Visual admission and named lookup (Map API 16)

V2 discovery validates visual references before running initialization Lua.
Unknown built-in sheet names, undeclared PNG names and external animation ranges
outside the declared grid reject the candidate with an entities.json diagnostic
identifying the type and visual field. Built-in sprite counts depend on the live
atlas, so their range remains checked by the runtime sprite resolver.

`entity.find("placement_name")` returns the original authored placement's live
handle, or nil if it is unknown or removed. It never follows a reused slot to a
replacement object. Snapshot restoration restores the original handle's liveness.
`entity.type(handle)` returns the qualified type key; a stale handle raises an
error. Both queries return detached Lua values and do not mutate the world.

```lua
map.on_tick(function()
    local door = entity.find("entry_door")
    if door then
        entity.set(door, { vy = -1 })
    end
end)
```

Guarded tests cover unknown names, malformed lookup arguments, stale type queries,
removal/slot reuse, and snapshot restore/replay. Visual package tests cover valid
built-ins, unknown sheets, missing external declarations, and exact/excessive PNG
animation ranges.

## Greggnogg object designer

The **Objects** command edits appearance, animation, interaction areas and Lua
callbacks in the current map. Designs appear under **Custom tiles** in the normal
palette; the map tools handle placement. The former standalone chooser/preset
workflow is retired. The [editor guide](../greggnogg/README.md) describes usage.

Edits use the map's draft and undo/redo. Room-local placements follow room edits.
Map previews animate sprites without running gameplay Lua. Visual/browser and
live runtime acceptance remain pending; custom equipment is still unfinished.

## Native player observation (Map API 17)

`map.players()` returns a detached array of currently live native players in slot
order. Each record contains `player` (1 or 2), `x`, `y`, `vx`, and `vy`. Missing or
dead players are omitted; use the `player` field rather than assuming array index
is player identity. Values are sampled when called, canonicalized through the
existing map-object float rules and never expose native pointers. Editing the
returned table has no effect on the native player.

Queries are available only in gameplay callbacks, not loading or initial entity
spawn notification. A host without player observation raises a diagnostic. Invalid
host status, nonfinite motion or a mismatched player profile faults the surrounding
transaction. Each query charges 64 units to the shared instruction budget in
addition to Lua execution. The production host reads the same native player slots,
kind classifier and motion offsets as the existing tile interaction bridge.

This enables tracking/steering custom entities toward players. API 18 adds atomic
velocity changes below; damage and equipment remain unfinished. The existing synthesized-leave writer returns void and can independently
reject a stale/invalid target, so it must not be reused as proof of an all-or-nothing
multi-player mutation API.

Guarded runtime tests cover initialization rejection before any host read, detached
values, missing player slots, the explicit player number, invalid profiles/NaNs/host
errors, missing host support and 16-tick entity tracking replay equality. Native
in-game tracking acceptance remains pending.

The checked-in [player-following example](examples/entity_follow_player.lua) uses
the visual package and moves its first orb near a live player on the first tick.
Its guarded 100-tick test reaches the expected player position and animation frame,
then restores and replays with byte-identical combined state. It applies no damage.

## Atomic native velocity changes (Map API 18)

`map.set_player_velocity(player, vx, vy)` queues a velocity for live native player
1 or 2. The arguments must be numbers and velocities must fit finite 32-bit floats.
The function is available in map tick/timer callbacks and entity callbacks invoked
within that phase. It is unavailable during loading, initial spawn notification,
and native tile contact/leave callbacks; those retain their existing object API.

Writes are deferred until tick callbacks, entity updates/motion and velocity-limit
validation all succeed. Repeated writes to the same player use the last value.
`map.players()` observes queued velocities during the phase. Existing map velocity
limits apply at commit, and synthesized leave updates cannot overwrite a later
queued velocity. Positions and previous-position fields are not changed.

The host validates every targeted player's current identity, liveness and writable
velocity fields before the first store. A dead/missing/invalid target, aliased player
slots, unsupported host or rejected batch faults the transaction. Native velocities
remain unchanged, and map/entity state restores to its checkpoint. Successful writes
occur before the next native physics tick and enter the existing native rollback
capture. Each setter charges 64 execution-budget units. The transient write queue
is cleared on success/failure and is never serialized as pending gameplay state.

Runtime tests cover two-player batching, queued reads, last-write ordering,
script failure after both writes, missing player two, host commit rejection, restore
and replay. The guarded production-serializer test calls the actual native writer
through a test-only slot binding: read-only player-two memory, dead players and
aliased slots reject without changing either player; a successful batch changes
only vx/vy bytes. In-game movement and paired-native acceptance remain pending.
This supplies native movement interaction, not player damage, equipment or combat.

Entity PNG names must resolve to one unique map sheet declaration. Repeated tiles
using the same grid are valid; declaring the same filename with different grids
and referencing it from an entity is rejected instead of silently choosing the
first declaration. Use distinct PNG filenames when entities need distinct grids.
Maps without entities may continue to use multiple grids for the same PNG.
The guarded V2 test covers identical and conflicting declarations, and Workshop
regressions cover rectangular crops, padding, inheritance, range errors and glyphs.

## Object packaging in Greggnogg

Normal map export writes definitions to `entities.json`, combines map logic and
per-object callback bodies in `map.lua`, and includes picture assets. Optional
`objects.greggnogg.json` preserves callback separation for reopening in the editor.
The importer verifies that this metadata reproduces map.lua before using it;
manual script changes are not silently replaced. Imported maps are canonicalized
by the normal map editor, so their raw-byte online key may change.

Independent PNG declarations use `tileset.sheets` entries with `sprite_sheet`,
`cell_w`, `cell_h`, optional `padding` and `asset_sha256`. These register pictures
without inventing terrain tile definitions. Native and browser validation enforce
sheet limits, exact grid geometry and referenced sprite ranges.

## Authored region queries (Map API 19)

`entity.regions(handle)` returns detached region records in declaration order.
Each has `id`, `role`, `layer`, `mask`, local `x`/`y`, `width`/`height`, and translated
`world_x`/`world_y`, all coordinates in pixels. World bounds use the entity's current
position. Sprite offsets, tint and scale do not change these rectangles. Editing a
returned table does not alter the definition; stale handles and invalid arguments
raise errors. A query charges 64 plus 8 per region against the shared work budget.

`map.players()` now also returns `contact_radius`, using the same verified native
six-pixel profile as the tile sensor bridge. This supports explicit body/feet
probes without copying player dimensions into Lua. These are query primitives;
roles/masks do not automatically apply native collision response or damage.

The [launch-pad Lua example](examples/entity_launch_pad.lua), paired with
[its entity metadata](examples/entity_launch_pad.json), compares player feet against
authored sensor rectangles and queues an upward velocity while descending. It
preserves horizontal motion, uses the atomic host commit, and applies no damage.
Move its placement to the desired generated-world location in your map.

Guarded tests verify detached definitions, stale handles, exact budget exhaustion,
native player radius, two-player launch, single response while rising, combined
snapshot replay and no player changes when the host rejects the batch. The map
API identity advances to 19; POD/entity snapshot layouts remain unchanged. Live
in-game sensor alignment and movement acceptance remain pending.

## Per-object animation clocks (Map API 21)

Each entity starts with `animation_tick = 0` and `animation_paused = false`.
The clock advances once per successful simulation step, independently of the
entity's creation time and other entities. Pausing animation does not pause motion
or callbacks. Rendering selects the frame from this clock and never mutates it.

```lua
-- Restart an object's animation when its behavior changes.
entity.set(handle, { animation_tick = 0, animation_paused = false })
-- Freeze on a chosen relative frame (six ticks per frame in this example).
entity.set(handle, { animation_tick = 2 * 6, animation_paused = true })
```

Both fields are accepted by `entity.spawn` and `entity.set`, and returned by
`entity.get` and lifecycle value records. The clock must be an exact integer from
0 through 9007199254740991; paused must be boolean. Exhausting an unpaused clock
rejects the entire step without partial movement. The instance clock and pause
flag are serialized and restored with the world. YEW1 snapshot version 2 uses
40-byte instance records; old snapshots reject before mutation. API 21 changes
content identity to prevent mixing the old global-clock semantics with this API.

Guarded world/Lua/package tests cover independent starts, pause with motion,
invalid writes, overflow atomicity, render selection and snapshot restoration.

## Native defeat transactions (Map API 22)

`map.defeat_player(player)` queues the normal native one-hit death transition for
player 1 or 2. It returns true when queued and false for an absent/dead player or
an already queued target. It is available in tick, timer and entity update
callbacks. `map.players()` omits queued targets, and later velocity writes to
those targets reject. Velocity writes queued before defeat are applied first.

The VM commits defeats together with player velocities only after all callbacks,
entity integration and limits succeed. A callback error or host rejection discards
the batch and restores managed state. The host validates every target before any
write, rejects goal-dive state and aliased slots, then invokes the existing native
player death routine in player-number order. That routine owns corpse animation,
sword dropping, leader changes and RNG; the framework does not manufacture these
by patching a dead flag. Hosts without the combined commit capability reject this
API rather than partially applying it.

The [moving hazard example](examples/entity_moving_hazard.lua) and
[its definition](examples/entity_moving_hazard.json) use an independently moving
entity and circle/rectangle contact to queue native defeat. Copy them as map.lua
and entities.json into a V2 map with a center room, or adjust the placement room.
The visual, hit region and motion are independently editable. This is a lethal
hazard, not an equipment API or a generalized hit-point/damage system.

Guarded runtime tests execute the actual example for 100 ticks, restore and
replay, and reject a failing host without defeating either player. Native adapter
tests cover read-only memory, missing engine callbacks, goal-dive rejection and
mixed velocity/death batches with a test death callback. The existing death ABI
and detour are reused. Actual in-game sword drops, corpse animation, leader
selection, simultaneous deaths and room transitions still require live acceptance.
Entity maps remain offline-only; API 22 changes the content identity.
