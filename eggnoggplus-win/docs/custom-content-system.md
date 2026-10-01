# Custom-content system implementation contract

Status: active implementation. This document separates intended public behavior
from code that exists today. Managed entity scripting/rendering and deterministic
contact lifecycle callbacks and map-scoped online admission are implemented;
equipment, full weapons, the process-wide mod manifest, and paired live acceptance
remain unfinished.

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

Validated map packages use a map-scoped admission boundary. The advertised key
contains a 128-bit, domain-separated SHA-256 prefix over the exact map/config text
and loader-computed hashes for external sheets, `map.lua`, and `entities.json`.
The matchmaking server offers a map only to peers with identical keys. The P2P
layout handshake then checks snapshot size, and YMC3 initial-state/correction
preflight checks the full 256-bit managed-content identity before mutation.

Unrestricted gameplay mods remain suspended because their complete process-wide
manifest is unfinished. Only the bounded map VM, validated packages, and exact
pinned assets enter the admitted map surface.

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

The world is now connected to map loading, scripting, native snapshots, sprite
submission, editor export, and map-scoped online admission. Full native-style
weapons/equipment, public cross-package registration, and live acceptance remain.

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
7. Online admission and paired soaks: map-scoped mismatch rejection is implemented;
   repeated spawn/despawn/death/reuse through rollback and correction, equivalent
   native save-load-save state, bounded stalls, and actionable live diagnostics remain.

All seven stages are required before calling the broad custom-content API complete.

## Deterministic map ambiance slice

V2 maps now own bounded `particles` and `ambiances` catalogs. An ambiance
combines up to 16 emitters with spawn rectangles, velocity ranges,
acceleration, rotation, five native draw positions, alpha/additive blending and
room mirroring. Particle definitions reference built-in or declared map sheets
and control animation, tint, scale, lifetime and fades. The loader validates
all references and asset ranges before publishing a map.

The runtime intentionally does not allocate these visuals in the native mutable
particle pool. A stateless lane enumerator derives position, animation and fade
from immutable catalog identity plus source room, tick, emitter and lane. The
native `particles_draw_ex` hook draws each custom lane immediately after the
corresponding native layer. Custom lanes always use the world-camera globals:
the native renderer supplies screen-space camera arguments for layers 4 and 3,
which would otherwise place world-space ambiance particles off-screen outside
the first room. No gameplay RNG, rollback bytes or script-visible state are
touched.

Greggnogg preserves and validates both catalogs, lists custom ambiance IDs in
the normal room selector, and provides a focused editor with an animated room
preview. Ambiance composition and particle appearance use separate tabs, the
selected particle has an isolated preview, and the property pane scrolls while
the active room remains visible. Preview rendering is single-flight and capped
at 30 refreshes per second so slow image decoding cannot build an unbounded
render queue. The main map canvas receives the same custom catalogs and imported
images, and a newly selected custom ambiance remains selected while its options
are rebuilt. At runtime, `modframework.log` records `[ambiance] active` once when
the selected room effect changes and emits a bounded warning if generated
particles cannot resolve or enter a native sprite batch. The checked-in
`maps/ambiance_demo/` fixture and guarded C,
parser, staged-package preview, editor and static tests cover this slice. Live
GPU/game visual acceptance remains part of release QA.

## Definition and contact implementation update

The internal world now accepts immutable type definitions before the first spawn.
Each type has independent body/sensor/hitbox/hurtbox rectangle regions, local
fixed-point offsets, stable region IDs, optional stable names and bilateral layer/mask filtering. Region
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
Spawn/removal notifications, the Lua binding and native fixed-tick adapter now use
this transaction boundary in production map content.

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
activation entry point described below. Production V2 map loading and native rollback
activate the adapter for validated offline entity packages.

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
decoder. The enclosing map key separately covers script and asset bytes for online
admission. `entity_package_resolve_type` plugs directly into
the Lua adapter's name resolver. Placement lookup returns the original generation
handle; it does not attach an old name to a replacement after destruction.

Guarded tests compare reordered declarations, independent copies, changed names,
changed placements, unknown references, malformed names and mismatched snapshot
rejection. The JSON loader, V2 map ownership, native integration and Greggnogg export
all use these package records.


## Package JSON decoding (internal schema 1)

`entity_package_decode` now loads an isolated package directly from JSON bytes.
The decoder uses the existing bounded JSON parser (1 MiB input, depth 32,
65,536 nodes), with no script execution or Lua libraries. It rejects duplicate
JSON keys, unknown fields, object/array confusion (including empty collections),
null or coerced numeric values, malformed identifiers, unknown placement types,
duplicate type/placement/region names or region IDs, invalid dimensions and unsupported schema versions.
Failures include a JSON path; nothing is published until the entire package passes.

See [the package example](examples/entity_package.json). Top-level
`schema`, `capacity`, `types` and `placements` are required. Capacity is 1..4096;
types contain a qualified `key` and a `regions` array (at most 16). A region has
an integer `id`, optional local `name`, `role` (`body`, `sensor`, `hitbox`, `hurtbox`,
`solid`), unsigned 32-bit
`layer` and `mask`, and positive `width`/`height`. Local `x`/`y` default to zero.
Placement `name` is local and unique, and `type` references a qualified key.
Placement `x`, `y`, `vx`, `vy` default to zero. A placement may also select its
initial named `animation`, set independent `scale_x`/`scale_y`, add
`visual_offset_x`/`visual_offset_y`, add `visual_rotation` in degrees, multiply the design tint with
`visual_tint: "#RRGGBBAA"`, choose `draw_layer: "behind"` or `"front"`, and
start with `visible: false`. These values initialize rollback-owned instance
state; Lua may change them afterward. Visibility affects rendering only; update
callbacks, regions, damage, and native-trigger interaction remain active, and
Greggnogg shows invisible placements as faint editor-only ghosts. Region roles describe contact purpose;
`solid` regions also participate in the managed-object collision queries and
optional authored motion response.

Positions, dimensions and velocities are in pixels (velocity per fixed tick).
Values round to the nearest 1/256 pixel, ties away from zero. Positive dimensions
must be at least 1/256 pixel before rounding. Coordinates are bounded to
+/-4,194,303.99609375 pixels. Type keys require exactly one namespace separator,
and each identifier is at most 96 ASCII bytes using lowercase letters, digits,
`_`, `-`, `.` (plus the colon in qualified keys). Region names use the same local
character set, are 1-32 bytes, and must be unique within their type.

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
rejected; shared values belong in `map.state`, while instance values belong in
the handle-owned `entity.value` store.

After synthesized leaves, timers and `map.on_tick`, live entities receive their
registered updates in stable slot order, then motion integrates once. Removed
entities are skipped. Entities created during that entity phase first receive an
update on the next tick, but integrate their initial velocity during the current
tick. Entities created earlier by a map callback join the current entity phase.
All entity callbacks share one configured instruction budget for the phase; each
callback is conservatively charged its unmeasured final hook quantum. This keeps
many short callbacks from evading the total budget. The world remains a bounded
reference implementation and currently allocates its update scratch buffers.

Greggnogg exposes the same deterministic timer model in beginner Map logic. A
named timer-finished event declares each timer, while start-once,
start-repeating, cancel, active and remaining blocks operate on names selected
from those events. Missing, duplicate, oversized and over-limit definitions are
rejected before generated Lua replaces the current script.

Beginner logic also supports repeat, while/until, and counted-for loops. A
dedicated stop-loop block generates Lua `break` only when nested in a real loop;
the editor does not expose Blockly's synthetic `continue` output. Reusable
Functions may accept parameters and optionally return a value. Function
definitions, calls, parameters, and loop counters are validated and serialized
with the workspace, while persistent state remains in the bounded map/player/
object variable APIs. Function calls share their caller's VM instruction budget
and transaction, so recursion and loops cannot bypass execution limits or
rollback.
Object-callback functions are emitted as callback-local functions. They retain
the callback's current-object handle while avoiding writable script globals.

The **Text** category supplies literals, joining, length/empty tests, first/last
index lookup, character and substring extraction, case conversion, whitespace
trimming, and reversal. Its generators use sandboxed Lua string primitives;
random character selection consumes `map.random`, so rollback replay stays exact.
Printing and prompt/input blocks remain unavailable because they would introduce
unsynchronized output or local input into gameplay logic.

Contact callbacks and tick dispatch checkpoint map state, handle-owned object values,
and the entity package. Script errors, budget exhaustion or invalid motion restore all
before applying the existing terminal timeline fault policy. Entity callback
argument allocation occurs inside the protected Lua call. Native object writes
retain their existing transaction boundaries.

Hosts use `map_script_content_snapshot_size/save/validate/load` for a combined YMC3
snapshot. Loading checks the map snapshot, package fingerprint, full entity state
object-variable checksum/handles, and matching map/entity clocks before committing. The ordinary
fixed map snapshot save/load functions deliberately reject an entity-bearing
runtime. This prevents current native serializers from silently dropping entities.
YMC3 is a same-platform host snapshot, not an approved network envelope. The fixed
MapScriptSnapshot remains version 6; API negotiation advances to 29 for the
handle-owned variable surface.

`tests/run_map_script_test.ps1` now exercises the real managed VM with entity
packages: 256-tick replay, combined-state byte equality, failed replacement,
truncated/mismatched snapshots, clock mismatch, callback rollback, spawn ordering,
read-only globals and API tables, captured-upvalue rejection, and infinite-loop
budgets. Production V2 map loading now calls the content entry point for entities.json;
rendering, native player interaction, lifecycle callbacks, native correction
serialization, and map-scoped online admission are connected. Equipment remains open.


The read-only `validate` entry point checks the same combined state without
mutating the world. Native serializers can preflight all their components before
committing any game memory. Entity worlds and packages expose corresponding
read-only snapshot validators. Native EGG0/correction integration now consumes this preflight as described below.


## Native rollback extension (EGG0 version 12)

The native serializer now includes a YMC3 extension after its ordinary native
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

Production discovery and activation are described below. Native rendering and
managed combat are implemented; full weapons/equipment, process-wide mod manifests,
and paired network acceptance remain open.


Package discovery can now use `map_script_validate_content` to build an isolated
entity-aware VM, execute declarations, validate callbacks and destroy it without
replacing the active map. Tests include successful validation with entity spawns,
failed initialization after a spawn, malformed package input and missing input;
the live combined snapshot remains byte-identical after every attempt.

Discovery integration also enforces policy for direct sessions. The online hub
requires the exact script/runtime pinned during native map construction, while the
transport rejects incompatible state layouts and managed identities before starting
simulation. The combined runtime binds actual source/configuration as described below.


## Handle-owned object variables (Map API 29)

Managed instances no longer have to encode a 16-byte handle into a shared
`map.state` key. The read-only `entity` namespace now exposes:

```lua
entity.set_value(handle, "direction", 1)
entity.change_value(handle, "direction", -2)
local direction = entity.value(handle, "direction")
local exists = entity.has_value(handle, "direction")
local names = entity.value_keys(handle)
local removed = entity.clear_values(handle)
```

The store accepts nil, booleans, finite numbers, and strings of at most 63 bytes.
Names are 1-32 bytes without NULs. Assigning nil deletes one value. There are 512
entries across the active entity package, independent of the fixed 64-entry
`map.state` table. `change_value` treats an absent value as zero, rejects an
existing non-number, and returns the new number. `value_keys` returns detached names
in bytewise lexical order.

Entries belong to the exact 64-bit generation-aware handle. A removal callback may
read the departing instance's values even though `entity.exists(handle)` is already
false. Cleanup runs after the callback; if it raises, the encompassing transaction
restores the entity and its values. Reused slots never inherit the prior generation's
state. Combined snapshots carry a checksummed fixed-size object-state section and
reject duplicate keys, noncanonical values, stale generations, and counts that do
not match occupied slots before committing any managed component.

This additional state is present only for managed entity runtimes. The negotiated
online rollback layout grows to its exact pinned size before frame zero; ordinary
maps retain their existing size. Greggnogg's **this object** variable scope emits these APIs; existing saved
Lua that uses `map.state["o:" .. handle .. ...]` remains valid.


## Combined content identity

YMC3 has a 48-byte header: magic, a 32-byte SHA-256 identity, a four-byte object-state
section size, a four-byte object-state version, and four reserved zero bytes. The
identity binds the exact Lua source bytes, canonical entity-package
fingerprint, ordered normalized tile bindings, map API version, effective memory
and instruction limits, and the initialization RNG seed. The seed matters because
loading Lua can derive immutable declarations from random values. Diagnostic
chunk names and host callback addresses do not enter this identity.

An explicit default limit and an omitted default limit produce the same identity.
Reusing a host-supplied script ID is insufficient to load a snapshot with changed
source, bindings, limits or seed. Identity validation precedes either component's
commit. Focused tests exercise each mismatch and preservation of the current
combined state. Native transport uses EGG0/v14 for the current rollback layout;
the trace reader retains v9 through v13 support alongside v14. This full identity is
the managed-state preflight behind map-scoped online admission. It does not replace
the future process-wide gameplay-mod/dependency manifest.


## V2 package discovery and activation

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

Entity-bearing maps are included in the online manifest after the same strict
validation used for local play. Their stable key includes the exact entity bytes,
script, map/config text, and external asset hashes. Held online setup checks the
newly generated pinned map rather than a prior runtime, and compares that pinned
package key with the nonempty server-selected key again before publishing the
rollback layout. Direct sessions use the
layout and YMC3 identity handshakes as their fail-closed mismatch boundary.

The guarded V2 fixture exercises no-script activation metadata, malformed files,
V1 rejection, a directory in place of the file, entity-aware Lua validation,
online manifest inclusion and signature changes, pinned runtime activation, timestamp-preserving edits,
and old/new generation activation yielding different expected movement. A
fixture-only pin helper avoids native fixed-address writes in that test and is
excluded from production builds.

Discovery initially supplied logical updates and motion. Sprite rendering and
spawn/removal callbacks are now implemented as described below. Collision response,
equipment, remaining editor authoring, and paired online tests remain open. Verified
native player attacks now feed managed object damage as described below.

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
  "mode": "loop",
  "offset_x": 0,
  "offset_y": -8,
  "scale_x": 1,
  "scale_y": 1,
  "rotation": 15,
  "tint": "#FFFFFFFF",
  "layer": 1
}
```

`sheet` and `sprite` are required. Omitted values default to one frame, one tick
per frame, looping playback, zero offsets, unit scales, zero-degree rotation,
opaque white, and layer 1.
`mode` is `loop`, `once`, or `ping_pong`. Playback uses the instance's rollback-owned
animation clock, so restore selects the exact same frame. Offsets
use pixels and scales are multipliers; both round to 1/256 units. Scales may be
negative for flipping but must not round to zero and must lie within -256..256.
Rotation is in degrees, rounds to 1/256 degree, and accepts -360,000..360,000. Tint
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
Clicking an already placed copy with its custom brush, or choosing **Pick** and
clicking it, opens the normal editor's **Object** inspector. It controls that
copy's name, arena side, position, starting velocity, initial animation,
visibility, scale, visual offset, tint, and player-relative draw order. Map
previews apply those presentation settings and animate sprites without running
gameplay Lua. Live runtime acceptance remains pending; custom equipment is still
unfinished.

Design deletion is intentionally cascading: deleting a custom object definition
removes all placements that refer to it, plus its block/Lua and movement metadata.
The UI reports the number of removed copies. Authors can delete a single instance
from the normal map editor's Object inspector without changing the shared design.

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

Lifecycle history is part of the fixed rollback snapshot. Each player record and
player-contact record includes `spawned`, `respawned`, `room_changed`, and
`previous_room`. The first is true on the first observed tick after a slot appears;
the second marks the transition out of native defeated state 9; the third compares
the current room with `previous_room`. Failed callbacks restore this history with
the rest of the transaction, and it does not consume author-visible state slots.

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

## Atomic native player positioning (Map API 36)

`map.set_player_position(player, x, y)` queues a new world position for live player
1 or 2 during tick, timer and entity-update callbacks. Coordinates must be finite
numbers within `-4194303..4194303`. The setter uses the same 64-unit charge and
deferred transaction as player velocity and defeat actions. `map.players()` and
player-contact records observe the queued position immediately, so later logic in
the same tick sees one coherent player view.

At commit, the native host validates every targeted player and all position and
velocity values before its first store. It moves both current and previous position
by the same displacement; this prevents a scripted teleport from becoming a false
one-frame native movement vector. Position and velocity changes for both players are
then written together. A callback fault, missing player, invalid native body, aliased
slot, read-only body or rejected host leaves all players and managed state unchanged.
Successful changes are captured by the ordinary native rollback snapshot and replay
without an additional serialized queue.

Greggnogg exposes this as **move player … to x … y …** in the Players block group,
including the scoped **this player** choice inside player events and loops. This is
the low-level primitive for authored checkpoints, portals and co-op traversal.
Declarative room spawn handles, facing/loadout rules and a purpose-built room-advance
action remain separate wishlist work so authors do not have to reconstruct those
common features from coordinates.

Runtime coverage verifies queued reads, combined position/velocity ordering,
two-player atomic rejection, callback rollback and byte-identical replay. The guarded
native serializer test verifies translated previous-position fields, unchanged
unrelated player bytes, invalid targets and all-player preflight.

## Room-graph doorway locks (Map API 36)

`map.set_exit_locked(connection, locked)` sets one canonical room-graph
connection, numbered from 1 in `layout.connections`.
`map.exit_locked(connection)` reads it. A lock blocks both directions of a
two-way doorway and the permitted direction of a one-way doorway. Native
movement rewinds the crossing position while keeping the current room; unlocking
allows the next valid crossing. A faulted map script fails open.

The runtime stores all 256 lock bits in the fixed map snapshot rather than in
`map.state`, so locks neither consume author variables nor disappear during
rollback. Snapshot validation, restore/replay, the native movement guard, and
the 1/256 boundary are covered by guarded tests. Greggnogg numbers doorway rows
and supplies matching lock action and condition blocks in the Game category.
Connection indices are currently structural array positions; removing an earlier
connection renumbers later native connections. Greggnogg connection records carry
stable editor IDs, and door blocks store those IDs before resolving the current
number during Lua generation. Handwritten Lua uses numeric positions directly and
must be reviewed after connection order changes.

## Native player color and visibility (Map API 36)

`map.set_player_presentation(player, options)` changes draw-only state for player
1 or 2 during tick, timer and entity-update callbacks. The supported fields are
`body_visible` (boolean or `"default"`), plus `skin_tint` and `clothing_tint`
(`#RRGGBBAA` multipliers or `"default"`). Omitted fields retain their current
override. `map.reset_player_presentation(player)` clears all three at once.

```lua
-- Flash player 1 red while leaving clothing unchanged.
map.set_player_presentation(1, { skin_tint = "#ff4040ff" })

-- Hide the body, then restore every native presentation default later.
map.set_player_presentation(1, { body_visible = false })
map.reset_player_presentation(1)
```

`map.players()` and player-contact records report the effective `body_visible`
and either a tint string or `"default"`. These overrides are stored in the map
snapshot: callback failure discards partial changes, rollback loads restore them,
a terminal script fault clears them, and unloading the map removes them. Tints
multiply the player's selected native colors without changing palette selection.

The native body is assembled from several sprites and arm/sword draw paths, so
the tint/visibility options do not directly rewrite its sprite sheet, animation,
scale, offset, rotation or draw layer. Body hiding suppresses native body and arm
draws; independently rendered equipment such as the sword remains. Replacement
sprites use the dedicated renderer below because native sprite/state bytes also
drive combat behavior.

The first dedicated replacement-renderer slice is available through
`map.set_player_sprite(player, object_type [, animation [, restart [, restore]]])` and
`map.clear_player_sprite(player)`. It reuses a validated object type's picture,
named animations, scale, offsets, rotation, tint, and draw order, follows the
live native player position, and suppresses the composite native body while it
is active. Repeating the same selection does not restart its rollback clock
unless `restart` is true. With `restore` true, a validated `once` animation
restores the native player on its rollback-owned completion tick. Loop and
ping-pong animations reject this option because they never finish. This changes drawing only: native collision, movement,
combat, and equipment continue normally, and an independently drawn sword remains visible.
`map.player_sprite(player)` returns a detached
`{type, animation, sprite, tick, frame, frames, mode, finished, just_finished, restore}`
record for conditions and diagnostics, or nil while the native body is active.
Live player and player-contact records also expose these values with a
`custom_sprite_` prefix. `just_finished` is a one-tick rollback edge when a
one-shot clip crosses its end, including playback speeds that skip frames.
Use `map.set_player_sprite_transform(player, options)` for independent `scale_x`,
`scale_y`, `offset_x`, `offset_y`, `rotation`, `tint`, `draw_layer`,
`animation_speed`, `mirrored`, and `visible` overrides. Values are quantized to 1/256 and
stored in rollback state. `map.reset_player_sprite_transform` clears all of them;
`map.clear_player_sprite` clears both the selected picture and its transforms. They persist through death and room changes until cleared; map exit, unload, and script faults restore native presentation.

## Optional managed health

An object opts in with `entity.enable_health(handle, maximum [, current])`, usually
inside its `entity.on_spawn` callback. `entity.damage` then subtracts health before
delivering `entity.on_damage`; `entity.on_defeated` runs once when positive health
reaches zero. `entity.health` returns current and maximum health, while
`entity.set_health`, `entity.set_max_health`, and `entity.heal` provide bounded
updates. Defeat does not remove an object automatically.
`entity.on_damage_filter(type, callback)` runs before managed health changes and
must return a boolean. Its `(object, damage)` record contains `amount` and optional
managed-object `source` and `source_type`. Returning false rejects that attempt;
the following damage event reports `blocked = true` and `applied = 0`. Because the
filter is ordinary bounded Lua, authors can combine source types, teams, cooldowns,
phases, and per-instance variables instead of choosing from a fixed resistance list.
`entity.set_invulnerable(object, boolean)` blocks managed damage without suppressing
the damage callback. Its event reports requested `amount`, actual `applied` damage,
and `blocked`; `entity.invulnerable` queries the flag. `entity.disable_health`
removes the managed current, maximum, and invulnerability state.

```lua
entity.on_spawn("example:crate", function(object)
    entity.enable_health(object, 20)
end)

entity.on_damage_filter("example:crate", function(object, damage)
    return damage.source_type == "example:sword"
end)

entity.on_defeated("example:crate", function(object, event)
    entity.play_animation(object, "break")
end)
```

Player health uses the parallel `map.enable_player_health`, `map.player_health`,
`map.set_player_health`, `map.set_player_max_health`, `map.damage_player`, and
`map.heal_player` functions. Player damage, health-changed, and defeated callbacks
receive the numeric player and an event containing `old_health`, `health`,
`max_health`, `delta`, `reason`, `defeated`, and optional entity `source` and
`source_type`. Damage events also contain requested `amount`, actual `applied`, and
`blocked`. `map.set_player_invulnerable` and `map.player_invulnerable` control the
rollback-owned damage gate, while `map.disable_player_health` opts out. A lethal change
queues the native defeat transaction. Player health is part of the map snapshot,
so state, callback effects, and native defeat either commit or roll back together.
When that native player becomes live again, managed health resets to its maximum
and emits a health-changed event with reason `respawn`.

## Per-instance object presentation (Map API 36)

Managed object properties include numeric `scale_x` and `scale_y`. They default
to `1`, accept nonzero values from `-256..256` at 1/256 precision, and may be set at
spawn time or changed later with `entity.set`. `entity.get` returns the active
multipliers. Negative values flip that visual axis; ordinary `mirrored` behavior
still composes with the horizontal multiplier.

Instance scale multiplies the object type's authored visual scale and affects only
draw submission. It never stretches bodies, Solid areas, sensors, attack areas or
damage receivers. This keeps presentation changes from silently changing gameplay
or introducing collision differences online. Both multipliers were introduced in
YEW1/v3 and live in the current v6 entity snapshot, restore under rollback, and
reproduce the same rendered transform.
Greggnogg exposes them as **width scale** and **height scale** in the ordinary object
property getter/setter blocks.

Every instance can also add `visual_offset_x` and `visual_offset_y` in pixels,
add `visual_rotation` in degrees,
multiply the authored color with `visual_tint = "#RRGGBBAA"`, and select
`draw_layer = "behind"` or `"front"`. `visual_tint = "default"` and
`draw_layer = "authored"` restore the default ordering: terrain behind actors for
solid types, and the type's authored ordering for other types. Transparent black is a real
override rather than being confused with the default. These properties affect draw
submission only, are bounds checked, serialize with the same world record, and roll
back with animation selection. Greggnogg supplies numeric offset properties plus
color set/reset and explicit draw-order blocks. The object designer's base draw-order
choice controls non-solid instances that retain `authored`. Solids can use an
explicit per-instance foreground override for deliberate overlays.

The type visual's `rotation` and the instance's `visual_rotation` are additive,
use 1/256-degree precision, and accept -360,000..360,000 degrees. Mirrored
instances negate the combined rotation. Rotation does not rotate or resize any
body, Solid area, sensor, hitbox, or hurtbox. The instance value is accepted by
`entity.spawn` and `entity.set`, returned by `entity.get`, and serialized in
YEW1/v6.

## General animation playback (Map API 36)

Managed object properties now include `animation_speed`, a per-instance multiplier
from `1/256` through `256`. A value of `1` advances one animation tick per simulation
tick. Fractional speeds accumulate in exact 1/256 steps, while speeds above one may
advance several animation ticks at once. `animation_paused` freezes both whole and
fractional progress; setting `animation_tick` restarts fractional progress at zero.

The speed and fractional accumulator are serialized in the current YEW1/v6 snapshot. Whole-world
preflight checks the largest pending advance before changing any entity, preserving
the existing all-or-nothing overflow behavior. Greggnogg exposes animation speed in
the same general numeric object getter/setter blocks as animation tick. Playback is
per object and independent of why the animation is used.

Each object type may also declare up to 32 arbitrary named clips. They reuse the
base visual's sheet, tint, offsets, scale, and layer while selecting their own
starting sprite, frame count, tick duration, and playback mode:

```json
"animations": [
  {"name":"open", "sprite":12, "frames":3, "frame_ticks":2, "mode":"once"},
  {"name":"glow", "sprite":20, "frames":4, "frame_ticks":4, "mode":"ping_pong"}
]
```

Names are 1-32 lowercase identifier characters; `default` refers to the base
visual and is reserved. `entity.play_animation(handle, name [, restart])` selects
a clip. Restart defaults to true. Passing false preserves progress when that clip
is already selected, while changing clips always begins the new clip at zero.
`entity.animation(handle)` returns the current name.

`entity.animation_status(handle)` returns a detached table with `name`, zero-based
`frame`, `frames`, `mode`, `finished`, and `tick`. `finished` becomes true after a
`once` clip has displayed its last frame for its full duration; loop and ping-pong
clips never finish. Greggnogg exposes the clips and playback mode beside the object
preview, animates every preview mode, and supplies play, current-animation, and
animation-finished blocks. Selection and progress are per object, fingerprinted,
serialized in YEW1/v6, and restored exactly during rollback.

`entity.on_animation_finish(type, callback)` registers one lifecycle callback for
that object type. It fires once when a `once` clip crosses its completion boundary
and receives `(handle, status)`. The status table has the same fields as
`entity.animation_status`. Restarting the clip permits a later completion; loop and
ping-pong clips never emit this event. Completions run in stable entity-slot order
after object updates and animation advancement, before contact callbacks. They share
the entity execution budget, and a callback failure restores the entire tick,
including animation clocks, variables, objects, and queued native player writes.
Greggnogg exposes this as a normal object event rather than requiring a polling loop.

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

## Authored region queries (Map APIs 19 and 30)

`entity.regions(handle)` returns detached region records in declaration order.
Each has `id`, optional `name`, `role`, `layer`, `mask`, local `x`/`y`, `width`/`height`, and translated
`world_x`/`world_y`, all coordinates in pixels. World bounds use the entity's current
position. Sprite offsets, tint and scale do not change these rectangles. Editing a
returned table does not alter the definition; stale handles and invalid arguments
raise errors. A query charges 64 plus 8 per region against the shared work budget.
API 30 adds optional region names without changing geometry or ordering. Names are
validated as 1-32 byte local identifiers, are unique per type, and are included in
the package fingerprint. Older packages remain byte-identical when every region is
unnamed. Greggnogg exposes names beside the hitbox preview and offers them in the
player/object touching block, alongside purpose-based choices.

## Player input, ground state and visibility (Map API 31)

`map.players()` retains the API 17/19 identity, position, velocity and contact-radius
fields and adds two ground booleans plus three detached input tables:

```lua
for _, player in ipairs(map.players()) do
    if player.pressed.jump and not player.grounded then
        -- A general input edge; decide your own rule and response.
    end
end
```

`grounded` and `previously_grounded` come from the native current/previous collision
flags. `input`, `pressed` and `released` each contain boolean `attack`, `jump`,
`right`, `left`, `up`, `down` and `menu` fields. The production adapter captures
the old command byte immediately before every live or replayed native tick because
the native update overwrites its own previous-command byte before map callbacks.
The captured player pointer must still match after the tick; a newly appearing
player receives an all-clear previous command instead of inheriting another body.
All source bytes are already inside the canonical player rollback record, so no
second persistent command history is serialized.

The same detached record now carries numeric `native_state`, signed `room`, and
`facing` (`-1` left, `0` neutral/unknown, `1` right), the
complete `collision_flags` and `previous_collision_flags` bytes, plus synchronized
`skin_palette` and `clothing_palette` indices. The `has_sword` boolean reports
native weapon ownership without requiring scripts to decode state numbers. These are direct observations of
rollback-owned native state and the rollback-restored palette sidecar. Numeric state
and collision values deliberately remain low-level so scripts can observe new native
states without the framework guessing a pose or physics meaning. Greggnogg offers
them through the ordinary numeric player-value block.

Greggnogg exposes all 21 held/pressed/released action combinations through one
composable player-action condition. A top-level player-action event additionally
filters any player, player 1 or player 2 and supplies **this player** to its body.
Both compile to `map.players()` observations, so they respect configured keyboard or
controller bindings and deterministic rollback edges. Physical device-key events are
intentionally a separate pending API because raw local keys cannot safely affect an
online simulation without an explicit synchronization policy.

An entity placement may specify `"visible": false`. Scripts can read
`entity.get(handle).visible` and write `entity.set(handle, {visible = boolean})`.
Visibility suppresses draw submission only. Hidden objects still receive lifecycle
and update callbacks, retain rollback state, move when requested and participate in
sensor, contact and Solid-region queries. Visibility is stored in the existing
entity flags and therefore restores with the combined managed snapshot.

The complete [double-jump demo map](../maps/double_jump_demo/) implements an extra
airborne jump, per-player charge variables, indicators that follow each player and
a named-area collectible recharge. `logic.greggnogg.json` and
`objects.greggnogg.json` preserve the actual beginner blocks that generated its
`map.lua`; the general **with named object** block resolves current-map placement
names through `entity.find`. Native runtime tests simulate grounded refresh, native
takeoff, airborne input, pickup removal, visibility changes and exact snapshot
restore/replay. The package signature and managed snapshot identity cover these
behaviors in admitted online maps.

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

## Managed contact lifecycle (Map API 32)

`entity.player_contacts([handle [, selector]])` returns a detached post-movement
view of authored regions touching live native players. The optional handle limits
the query to one exact generation. A selector may be `any`, a region purpose
(`body`, `sensor`, `hitbox`, `hurtbox`, or `solid`), `name:NAME`, or a positive
region ID. Results use stable entity-slot, region-declaration, then player-number
order. Each record includes the entity handle, region ID/name/purpose and the same
detached player motion, ground, and input fields returned by `map.players()`.

`entity.on_contact(type, callback)` and
`entity.on_player_contact(type, callback)` register one callback per type. After
all object updates and velocity integration, the runtime captures object/object
and object/player contacts before invoking any contact callback. Object callbacks
receive `(handle, contact)` with the other handle/type and both region IDs,
names, and purposes. Player callbacks receive `(handle, contact)` with the query
record above. Removing an object skips later callbacks for that object without
changing the captured order for surviving objects.

These callbacks choose their own response. A hitbox does not imply damage and a
hurtbox does not imply health. Scripts may update instance values, spawn or remove
objects, change player velocity, or queue native defeat. The entire phase shares
the bounded instruction/contact budget. Callback failure restores the managed
checkpoint and discards queued native player writes. API 32 participates in
content compatibility; paired online soak coverage remains release work.

## Managed object damage events (Map API 33)

`entity.damage(target, amount [, source])` synchronously sends a deterministic
damage attempt to a live managed object. It returns `true` when the target type has
a damage filter, managed health, or an `entity.on_damage(type, callback)` handler,
and `false` when none can handle it. The
callback receives `(target, damage)`, where `damage.amount` is the finite positive
amount. When a live source handle was supplied, the detached record also contains
`damage.source` and `damage.source_type`.

Damage remains a general authored signal. Optional managed health supplies bounded
current/maximum values, invulnerability and defeat events, while authors still choose
knockback, removal, teams, and attack timing. `entity.on_damage_filter` can accept or
reject each attempt from its amount, optional source/type, and any authored state.

Verified native combat probes use this same path. Punch and kick attempts carry 12
and 25 damage; held swords, thrown swords, K spike balls, and mine blasts carry 100.
Their source types are `native:punch`, `native:kick`, `native:sword`,
`native:thrown_sword`, `native:spike_ball`, and `native:mine`. The bridge tests an
authored hurtbox when present and otherwise uses body regions. A continuous overlap
is latched so one native attack does not apply once per frame. The held-sword bridge
accepts only the engine's resolved attacking probes. Its separate idle stance/body-
blocking probe cannot damage a managed object. The read-only
`entity.was_hit_by_player(handle, player)` query is true during the object update in
which player 1 or 2 landed an accepted, nonzero hit. Player-owned probes also attach
`damage.source_player` (1 or 2); unowned spike-ball and mine attempts omit it.
Contact logic may call `entity.damage` after testing
named hitbox and hurtbox regions; scripts may also send damage for projectiles,
timers, environmental effects, or any other rule.

Mine attempts are emitted from the verified `mine_anim` explosion callback, after
its active-state and same-tick guards accept the blast. Arming a mine does not deal
managed damage. `entity.set_mine_knockback(handle, true)` separately opts an object
into the native 32-pixel radial impulse and distance falloff;
`entity.mine_knockback(handle)` reports the current setting. The impulse does not
require managed health and is independent of whether `native:mine` damage is
allowed, blocked, or lethal. Greggnogg's **Knocked away by mine explosions**
checkbox generates the spawn-time opt-in.

Calls are allowed only during gameplay callbacks, consume 64 units from the
shared execution budget, and require an amount greater than zero and no more than
1,000,000,000. Target and optional source must be exact live generations. Delivery
is synchronous in caller order, with lifecycle nesting bounded to 32 callbacks.
One handler may be registered per type. A handler error aborts its enclosing
transaction, restores managed world and object values, and discards queued native
player writes. The transient nesting/removal stack is also cleared on fault and
checkpoint restore so callback failure cannot poison a later rollback branch.

The [damage example](examples/entity_damage.lua), paired with
[its entity package](examples/entity_damage.json), demonstrates named attack and
receiver areas, contact filtering, object-owned health, source inspection and
removal. Greggnogg exposes **When taking damage**, **deal damage**, amount and
source blocks. The editor also exposes a before-damage event and an allow-damage
action for building source filters without Lua. API 33 changes content identity and
therefore the online map key.

`map.trigger_mine_at(x, y)` gives authored objects an explicit native-mine action.
It accepts finite world coordinates, queues at most 32 exact requests per tick, and
returns false for a duplicate coordinate. Queued work is discarded when any callback
or player commit fails. After successful script work, production resolves each
coordinate through the native map and calls EGGNOGG's verified `trigger_mine_pos`
path only when the current tile is native mine `0x06`; it cannot invoke another tile
action. Greggnogg exposes it as **Can interact with vanilla physics-triggered objects** in each
object's Movement settings, using that object's position automatically.
Automatic collision-driven mine activation for custom solids remains backlog work.

## Spawn-time object values (Map API 34)

`entity.spawn(type, properties)` accepts an optional `values` table with up to
32 per-instance variables. Each name follows the normal 1-32 byte object-value
rules, and each value is a boolean, finite number, or string of at most 63 bytes.
The values are installed after the exact object generation is allocated and
before its `on_spawn` callback runs. This lets one reusable projectile type
receive its owner, damage, team, lifetime or other configuration without using
shared map state or creating a different type for every weapon.

```lua
local shot = entity.spawn("demo:projectile", {
    x = origin.x,
    y = origin.y,
    vx = 3,
    values = { owner = handle, damage = 4, team = "red" },
})
```

The call still returns the new handle. Unknown names, invalid values, more than
32 entries, capacity exhaustion, or a failing `on_spawn` callback abort the
enclosing transaction without leaving the object or any initial values behind.
Each installed value consumes the ordinary object-variable execution budget and
the package-wide 512-entry store. Greggnogg adds a configured-create block for
one initial variable and a **this object's handle** reporter; Advanced Lua can
initialize the complete table. API 34 changes content identity.

Initial names are stored in bytewise order rather than Lua table iteration
order. Equivalent configuration therefore produces the same object-state slot
layout and snapshot bytes even when a table was assembled in a different order.

The [managed projectile example](examples/entity_projectile.lua) and
[package](examples/entity_projectile.json) combine spawn-time owner/damage/lifetime
values, fixed-point movement, named hitbox/hurtbox contacts, damage delivery and
receiver health. It is a reusable authoring pattern: the runtime does not hardcode
its weapon timing, amount, team rules, visuals or response.

## Managed object signals (Map API 35)

`entity.signal(target, name [, value [, source]])` synchronously delivers a
general authored message to one live managed object. The receiver registers one
`entity.on_signal(type, callback)` handler and branches on `signal.name`. A signal
may carry one boolean, finite number, or string of at most 63 bytes. When a live
source handle is supplied, the detached event also includes `signal.source` and
`signal.source_type`.

```lua
entity.signal(door, "open", 2, switch)

entity.on_signal("demo:door", function(handle, signal)
    if signal.name == "open" then
        entity.set_value(handle, "open_amount", signal.value or 1)
        entity.set(handle, { visible = false })
    end
end)
```

Names contain 1-32 bytes, start with a lowercase letter, and use lowercase
letters, numbers, `.`, `_`, or `-`. Calls are gameplay-only, cost 64 shared
budget units, preserve caller order, and allow at most 32 nested lifecycle
callbacks. They return `true` when the target type has a receiver and `false`
otherwise. An invalid handle/value/name or receiver error aborts the enclosing
transaction, restoring object state and discarding queued native player writes.

Signals provide the common interaction channel for switches, doors, pickups,
equipment, reusable weapons, quests, and authored state machines without adding
a hardcoded action for each design. Greggnogg includes **When receiving a
signal**, send-signal, name, value, and source blocks. The ordinary text block is
available for comparing signal names and sending string values. API 35 changes
content identity and therefore the online map key.

The [signal example](examples/entity_signal.lua) and
[package](examples/entity_signal.json) show a reusable switch sending an `open`
message and numeric amount to a door, including source-type filtering and
per-instance state.

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
flag are serialized and restored with the world. The historical YEW1/v2 format used
40-byte instance records; the current v6 record also carries scale, fractional speed,
named-clip selection, and presentation transforms. Old snapshots reject before mutation. API 21 changes
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
API 22 changes the content identity used by online admission.
