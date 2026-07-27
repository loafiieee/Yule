# Declarative Content Registry and Custom Map V2

**Date:** 2026-07-17  
**Status:** Registry, parser, inherited map-level sheets, full native-layout map reskinning, symbolic overrides, collision/legacy forces, atlas, native per-cell binding/draw, and the separate bounded map-script follow-on are implemented; in-game visual/physics QA remains manual
**Primary code:** `content_registry.c/.h`, `content_tiles.c/.h`, `content_bridge.c/.h`, `custom_maps.c/.h`, `map_script.c/.h`, `lua_manager.c/.h`, `hooks.c`

## Purpose

This subsystem gives mods and map packages a common, deterministic way to name and
validate custom tile definitions. Custom-map V2 adds per-map symbolic tilesets while
remaining compatible with the V1 fixed-room format.

The safety rule is fundamental: every custom tile resolves to a validated, single-cell
vanilla behavior glyph. Authors may select that glyph directly with `native_glyph` or
use the `solid`, `pass_through`, and `hazard` presets. The engine receives the resolved
glyph for collision/update behavior. A custom visual may replace the vanilla sprite only
when the visual bridge can resolve it; otherwise gameplay continues with the native
fallback.

Deterministic additive/set velocity fields remain a backwards-compatible part of this
declarative slice. V2 map-local scripting was implemented afterward as a separate,
bounded layer described in `2026-07-18-map-local-lua-design.md`; raw native callbacks,
self-propelled entity logic, and new weapon behavior are still not exposed.

## Implemented architecture

The declarative implementation has five layers:

1. `content_registry` owns normalized, immutable tile definitions and atomic owner
   replacement.
2. `mod.content` exposes owner-scoped registry transactions to Lua mods.
3. Custom-map V2 parses map-local symbols and assets into `map.<id>` owner transactions.
4. `content_tiles` stores compact per-cell bindings and computes deterministic render
   records and force interactions without owning or altering the native tilemap.
5. `content_bridge` maps source-room metadata onto the completed native map and submits
   resolved custom visuals from the native tile draw action, with vanilla fallback on
   every failure.

The hook layer also owns the narrower whole-map presentation bridge. For a validated
`native_layout` sheet it temporarily replaces the native `_tiles` sprite-record base
only around synchronous mode-2 tile drawing, then restores it. Unlisted glyphs retain
their original action/collision/update code while resolving the same cell offset from
the map atlas.

The follow-on `map_script` layer consumes exact-cell or binding-union contacts and
optional sprite overrides without giving Lua native pointers. It distinguishes player,
dead-body, sword, and the verified K-spawned point-hazard profiles; its contact kind,
scope, lifecycle, and behavior state are embedded in rollback rather than added to the
declarative registry schema.

Registry definitions deliberately store a symbolic sprite-sheet key instead of a
process-local atlas sprite id. The atlas bridge rebuilds a key-to-range cache whenever
graphics are packed, and render consumers must resolve through that cache rather than
retain sprite ids across an atlas rebuild.

## Identity and ownership

- Owners and local ids are normalized to lowercase ASCII.
- Allowed characters are letters, numbers, `.`, `_`, and `-`.
- An owner or local id cannot start with `.` or `-`.
- Each part is at most 47 bytes; a qualified key is `owner:id`.
- A Lua mod uses its manifest id as its owner. The `map.` prefix is reserved and cannot
  be claimed by a mod.
- A V2 map must declare a non-empty stable `id`; its owner is `map.<id>`. The folder
  name is never an identity fallback for V2.
- Duplicate qualified keys, duplicate owners in a batch, and duplicate map namespaces
  are rejected rather than resolved by load order.

The registry allows at most 4,096 tiles for one owner and 65,535 tiles globally. Map V2
adds stricter package limits of 64 tile definitions and 16 distinct external sheets per
map.

## Transaction semantics

`content_registry_begin(owner)` starts a complete replacement set for one owner.
`content_registry_commit` atomically replaces that owner's prior definitions; committing
an empty transaction removes the owner. Validation or allocation failure leaves the live
registry unchanged.

`ContentRegistryBatch` applies several owner replacements/removals in one swap. The map
scanner uses it so a filesystem rescan cannot expose a half-old, half-new content set.
The registry is sorted by qualified key and protected by an SRW lock. A successful swap
increments a generation counter; callers copy definitions out and do not retain pointers
into registry storage.

Map hot reload has an additional last-known-good rule. If a previously valid package is
now invalid, the incoming map and content registries are rejected together. A newly
added invalid package is diagnosed and skipped without invalidating unrelated packages.
The failed filesystem signature is remembered to avoid retry/log spam until files change.

Native room definitions retain pointers to the registry that supplied the live custom
map. Hot reload therefore pins exactly that one retired registry generation until the
next custom or vanilla map definition has replaced every native pointer. Intermediate
registries that were parsed but never applied are freed on the next swap, and the old
engine pin is released immediately after the next map definition is installed. Repeated
editor saves no longer retain every historical registry until process exit.

## Tile definition contract

Every definition contains:

- normalized owner, local id, qualified key, and display name;
- a collision preset plus its resolved safe `native_glyph` for engine behavior;
- optional deterministic per-axis force values, mode, and absolute speed clamps;
- symbolic `sprite_sheet`, first `sprite_index`, animation parameters, and layer;
- transform (`offset`, `scale`, angle), RGBA tint, and visual flags; and
- SHA-256 identities for the asset and canonical definition.

Supported animation modes are `loop`, `ping_pong`, and `once`. Animation is selected from
a deterministic tick. `random_phase` derives a stable phase from the cell rather than a
runtime RNG draw. `mirror_with_room` affects visual horizontal flip and reverses authored
`force_x` on the mirrored-right copy; it does not alter the native behavior glyph.
`native_visual: underlay` is a draw-only flag: the generated native action renders
first. If that action returns zero, the hook reproduces `map_draw`'s exact generic
tile-info/frame/flip fallback while the selected native-layout sheet and action turtle
state are still live, then reports handled so the outer renderer cannot draw a second
vanilla-sheet copy. Its complete turtle state is restored before the custom sprite
renders over it. The default `replace` behavior continues to suppress the native draw
after a custom sprite succeeds.

Validation is fail-closed:

- `sprite_index`: `0..1,000,000`
- `frame_count`: `1..256`
- `frame_ticks`: `1..3,600`
- `layer`: `0` or `1`, matching the engine's two native sprite-batch banks
- offsets: finite values in `-4,096..4,096`
- scales: finite, non-zero values in `-64..64`
- angle: finite value in `-360,000..360,000`
- tint: exactly four finite channels in `0..1`
- flags: only `mirror_with_room`, `random_phase`, and the draw-only
  `native_visual: underlay`
- collision: `native`, `solid`, `pass_through`, or `hazard`
- force mode: `add` or `set`; authored components are finite `-64..64`
- optional absolute per-axis speed clamps: finite `0..64` (`0` means no clamp)

The safe glyph whitelist contains only bounded one-cell native behaviors.
Blank/no-op glyphs, waterfall/back-fill generators, and other multi-cell glyphs are
rejected. `K` is also rejected: although its marker is one cell, room reset performs an
unchecked allocation from the fixed shared native thing pool. It cannot become an
explicit custom behavior until map validation enforces a strict per-room spawn budget.

The definition SHA-256 includes normalized identity, behavior, sheet key, asset digest,
animation, transform, tint, layer, and flags. Registry fingerprinting hashes definitions
in sorted order, so declaration order cannot change compatibility identity.

## Lua API behavior

The implemented API is:

- `mod.content.begin()`
- `transaction:register_tile(def)`
- `transaction:commit()` / `transaction:abort()`
- `mod.content.qualify(local_id)`
- `mod.content.find_tile(local_or_qualified_id)`
- `mod.content.fingerprint()`
- `mod.content.owner`

The fingerprint API is deliberately process-wide: it covers the sorted live
definitions from every mod owner and map owner, plus the global count and
generation. It is a compatibility snapshot of the complete registry, not a
fingerprint of whichever mod calls it.

A mod-owned external sheet must first be registered through
`mod.assets.load_spritesheet`. Registration hashes the bytes actually on disk. An
optional caller-supplied SHA-256 must match. `builtin:*` keys derive stable internal
identity and must not provide an external digest.

Transactions abort during garbage collection if the mod does not finish them. Committed
definitions are removed when the owner disables, unloads, or hot reloads. Content
mutation classifies a mod as gameplay-affecting and is blocked while that owner is
suspended for online play.

## Custom map V2 package behavior

`eggnogg-map/v2` retains the V1 `33x12` quoted room rows, mirrored center-out layout,
rules, room overrides, ambience, and appearance fields. It adds:

```jsonc
"tileset": {
  "sprite_sheet": "terrain.png",
  "cell_w": 16,
  "cell_h": 16,
  "padding": 0,
  "tiles": [{
    "id": "wind",
    "symbol": "$",
    "collision": "pass_through",
    "force_mode": "add",
    "force_x": 0.125,
    "max_speed_x": 4,
    // Optional integrity pin. Normal map packages can omit this.
    "sprite_index": 0
  }]
}
```

`sprite_sheet`, optional `asset_sha256`, `cell_w`, `cell_h`, and `padding` at
the `tileset` level are inherited by tile definitions. A tile may name another
sheet/grid as an explicit exception. A tile needs its own `sprite_sheet` only
when the map default is absent.

`native_layout: true` turns an external default into the complete map skin. Its
declared grid must contain at least 128 sprites; those first 128 row-major cells
form the native `data/tiles.png` prefix. Extra cells are intentionally allowed
for explicit custom tiles, and image size, cell size, padding, and grid shape are
otherwise unrestricted within the normal external-sheet bounds. It may be used
with an empty/absent `tiles` array; the default sheet is still registered, packed,
hashed into package identity, and exposed by the generation-pinned content view.
A built-in default, fewer than 128 cells, missing file, or non-boolean flag
rejects the package. Without this flag, the map-level sheet is only an authoring
default for listed symbolic tiles.

`symbol` is exactly one printable ASCII byte (`0x20..0x7e`) and must be unique within the
package. This deliberately includes space and every vanilla glyph. Map-local alias
lookup runs before vanilla parsing, so an override of `G`, `L`, `T`, `~`, or another
normally expanding/back-filling source symbol collapses that occurrence to the chosen
single-cell behavior instead of running its original expansion. The source symbol's
footprint is irrelevant; only the effective `native_glyph` must be safe and single-cell.
During room parsing, the loader emits the effective behavior glyph and stores a compact
alias beside the source cell. V1 packages continue through their prior path and cannot
declare `tileset`.

### Declarative collision and force behavior

`collision` defaults to `native`:

- `native` emits `native_glyph`. For a safe single-cell builtin-symbol override, an
  omitted `native_glyph` defaults to the source symbol. Space, multi-cell, no-op, and
  back-filling source symbols require an explicit safe `native_glyph`.
- `solid` emits vanilla `@` auto-terrain behavior.
- `pass_through` emits vanilla `x` open/decorative behavior.
- `hazard` emits vanilla `X` spike-hazard behavior.

For a preset, `native_glyph` should be omitted; if supplied, it must exactly equal the
preset glyph. These presets use the engine's existing tile property/action tables, so
players and native things receive native solid/hazard behavior without framework
collision code.

`force_x` and `force_y` opt their respective axes into a deterministic velocity field.
`force_mode: "add"` (default) adds the component once per simulation tick;
`force_mode: "set"` replaces only explicitly authored axes without destroying the
other component. `max_speed_x` and
`max_speed_y` optionally clamp absolute velocity after the force. Values participate in
definition SHA-256, registry fingerprints, raw package identity, hot reload, rollback
replay, and online map identity.

These legacy declarative values are raw native velocity units and are therefore
unsuitable for a spring shared by unlike object kinds: players/dead bodies use gravity
`0.15`, while swords and the K-spawned point hazard use `0.075`. The separate `map.lua`
layer can select living players, swords, and hazards; choose calibrated targets (`-4.0`
and `-2.8`, respectively, for an approximately 53-pixel rise); and install an eight-tick
rollback-owned `min_vy` limit. The demo deliberately excludes dead bodies so repeated
spring entry cannot prevent native grounded respawn. The limit remains active after
shallow contact ends and catches a living player's delayed native kick impulse. The JSON
example is consequently a gentle additive horizontal field rather than a misleading
universal vertical launch.

Runtime lookup matches native `ROUND(coord / tile_size)` semantics and rejects negative,
non-finite, or out-of-map coordinates before integer conversion. Players sample `(x,y)`
for pass-through overlap plus the half-tile foot boundary used after native solid
collision restores their center outside the cell. Static disassembly verifies type-2
swords use center-point map collision, so they receive the center sample only. Other
native records in the fixed 16-slot thing pool are skipped until their bounds are proven.
Duplicate hits on one exact cell apply once. The same post-native-tick path runs offline,
local/loopback, GGPO live play, and rollback replay.

Within `tileset` and each tile object, unknown keys are errors. Duplicate JSON object
keys, embedded NUL escapes, fractional or out-of-range integers, duplicate symbols/ids,
and invalid types reject the package with path-specific diagnostics. Unknown legacy
top-level map keys remain warnings for V1 compatibility.

### External per-map asset policy

An external sheet must be a direct `.png` filename in the map folder. Paths with a drive,
separator, `..`, alternate data stream, or absolute component are rejected. The file
must:

- exist as a regular non-reparse file;
- be from 1 byte through 64 MiB;
- contain a PNG signature and IHDR with dimensions `1x1..4096x4096`;
- form a whole sprite grid for `cell_w`, `cell_h`, and `padding`; and
- exactly match `asset_sha256` when the package deliberately supplies that
  optional integrity pin.

The loader always computes SHA-256 from the bytes on disk. That runtime digest
is stored in the registry definition, participates in the scanned map/online
compatibility identity, and is rechecked before atlas upload even when the JSON
omits `asset_sha256`. Authors therefore do not need a hand-maintained hash just
to use a local PNG, while changing the PNG still changes compatibility identity
and triggers hot reload. Built-in sheets cannot declare `asset_sha256`.

External-sheet geometry is declared on the tile reference. `cell_w` and `cell_h`
default to 16 and are limited to `1..512`; `padding` defaults to 0 and is limited to
`0..64`. These fields are forbidden for `builtin:*` sheets. A valid grid may contain at
most 8,192 sprites, and `sprite_index + frame_count` must remain within that grid.
Sheet keys incorporate the asset digest and geometry, so the same file packed with
different cell geometry cannot alias one atlas range.

The native engine has one fixed 8,192-record sprite store and its allocator's
failure is not safely handled by the original sheet packer. Before calling that
packer, the bridge decodes the PNG, verifies the whole declared grid, removes
inter-cell padding into a tight temporary image, and rejects any sheet that does
not fit the remaining global capacity. The original loader's fifth integer is a
sprite-count limit rather than gutter padding, so it is never used as padding.

`custom_maps_content_sheet_for_key` repeats the non-reparse, PNG-header, and SHA-256
checks against the runtime digest captured during the scan before exposing a resolved
path. A changed or missing asset therefore becomes an unresolved visual rather than
silently using stale metadata.

## Native map binding and draw contract

The runtime bridge uses two instruction-boundary-verified native detours:

- `mapgen_build_map` at `0x437DB0`, with a six-byte prologue patch, is called normally
  first. The post-call path reads the completed native tilemap and opens one
  generation-stable custom-map content view. It never polls the filesystem once per
  cell.
- `tile_action_ex` at `0x440250`, with a seven-byte prologue patch, receives
  `(tile, mode, x, y, arg5)`. Only mode `2` and an exactly aligned bound map cell can
  enter custom drawing. Returning `1` tells `map_draw` to skip its vanilla sprite;
  every unresolved/error path invokes the original action and preserves vanilla
  fallback.

For `n` source rooms, the generated width must be exactly `(2n - 1) * 33` and height
must be `12`. Source metadata maps exactly as the native generator does:

- source room `0` -> final room `n - 1`, source x unchanged, not mirrored;
- source room `i > 0` -> left room `n - 1 - i`, source x unchanged; and
- source room `i > 0` -> right room `n - 1 + i`, local x `32 - source_x`, mirrored.

Any stale view, invalid dimension, allocation failure, empty/missing definition, or
mapping failure destroys the partial metadata map. It cannot leave bindings from a
previous match attached to a new native tilemap.

The draw path resolves the current symbolic sheet/index before changing render state.
Animation uses the rollback-tracked engine game tick at `0x547BA0`, never wall time or
render count. Once resolved, it saves all 96 bytes of native turtle state, applies the
definition relative to the tile transform already prepared by `map_draw`, multiplies
the existing RGBA by the definition tint, and submits the resolved sprite. Offset is
applied before the custom angle/scale; scale and angle are composed with the current
tile state. The 96-byte state is restored byte-for-byte after success or any transform/
batch failure.

`layer` is deliberately only `0` or `1`: these are the two layer selectors actually
accepted by the native sprite batch. The public API does not claim unsupported signed
z-order semantics.

## Runtime fallback and current boundary

Implemented now:

- V1/V2 parsing and validation;
- symbolic-cell substitution to native behavior;
- whole-map override precedence for every printable source symbol;
- native collision presets and deterministic per-axis force fields for players and
  verified type-2 swords across live and replay ticks;
- owner transactions and atomic multi-map hot reload;
- deterministic definition and registry hashes;
- compact cell metadata and generation-aware re-resolution in `content_tiles`;
- deterministic animation/frame selection; and
- C/Lua query surfaces and validated map-sheet path lookup;
- enumeration of validated per-map external sheets before atlas upload;
- atlas packing with an exact expected-versus-packed sprite-count check; and
- symbolic map-sheet resolution to the current atlas base/count range;
- exact generated-map cell binding for center, left, and mirrored-right rooms; and
- custom sprite submission from `tile_action_ex`, including deterministic animation,
  relative transform/tint composition, optional native underlay, state restoration,
  and a no-double-draw native fallback.

The remaining boundary is manual runtime verification against the supported game
executable: visually inspect a V2 package using built-in and external sheets on center,
left, and mirrored-right rooms, then force a missing/changed sheet to confirm the native
glyph remains visible. No automated test can execute the fixed-address native engine in
the unit-test process.

Only V2 map-local bound cells (new symbols and vanilla-symbol overrides) are currently
bound into a live tilemap. Mod-owned
`mod.content` definitions share the same validated registry/resolver, but a general
runtime placement/editing API is future work.

## Failure paths

- Invalid Lua input returns `false, error` without changing the live owner.
- A garbage-collected or explicitly aborted transaction cannot leak partial content.
- A failed batch commit keeps every old owner.
- A removed definition cannot leave a stale pointer; cell metadata re-resolves by key.
- A missing definition or sheet returns control to native rendering.
- A failed atlas pack or unexpected packed sprite count leaves that sheet unresolved.
- A failed live-map bind clears all partial/previous metadata before native drawing.
- A hot reload retains only the one generation still referenced by native roomdefs;
  superseded, never-applied generations are reclaimed immediately.
- A failed resolve, turtle transform, or batch submission restores the full turtle state
  and returns control to `tile_action_ex`/`map_draw` fallback.
- A missing/stale whole-map sheet skips the native base swap; the original `_tiles`
  pointer remains installed. A successful native-layout draw restores that pointer
  immediately after the native action.
- Invalid or changed map assets reject reload or fail visual resolution without changing
  native collision behavior.

## Verification

Automated coverage is in:

- `tests/content_registry_test.c`: key/glyph validation, owner replacement/removal,
  transaction and batch atomicity, collision preset resolution, force validation,
  behavior-sensitive fingerprints, numeric bounds, SHA-256, and transparent tint;
- `tests/content_tiles_test.c`: cell binding, native-mode filtering, animation sequence,
  transforms, finite coordinate guards, engine-matching nonnegative truncate/floor cell
  lookup and exact boundary coverage, mirrored
  force direction, add/set/clamp semantics, generation refresh, and owner fallback; and
- `tests/content_force_hooks_static_test.py`: all three native tick owners, post-update
  ordering, player center/foot contact, sword center-only declarative contact,
  bounded 16-slot filtering, and verified K-hazard admission to map sensors without
  silently broadening the legacy declarative force-field policy; and
- `tests/map_tileset_hooks_static_test.py`: generation-pinned map metadata, draw-time
  atlas resolution, explicit-definition precedence, native base replacement, and
  synchronous restore; and
- `tests/lua_content_native_visual_static_test.py`: strict mod-registry
  `native_visual` parsing, flag mapping, and normalized query output; and
- `tests/content_bridge_test.c`: exact center/left/right generated-room mapping,
  fail-closed partial binding, native-batch layer semantics, deterministic frame
  resolution, relative transform/tint composition, byte-exact 96-byte turtle restore,
  and resolver/transform/batch fallback; and
- `tests/custom_maps_v2_test.c`: V1 compatibility, required/overlong stable V2 ids,
  symbolic cells, duplicate/unknown key failures, embedded NUL and huge-number
  rejection, inherited defaults/per-tile exceptions, zero-definition native layouts,
  native-prefix minimum/flexible grid types, external asset hash checks, grid geometry,
  sprite-range bounds, and validation of the checked-in
  `maps/v2_symbolic_demo` runtime fixture.

`tests/run_v2_map_test.ps1` is the non-developer entry point for that suite. The
runtime fixture deliberately covers a map-owned external atlas tile, a transformed
built-in tile in the second native layer bank, unlisted native `@` terrain reskinned by
the complete per-map atlas, solid custom cells,
scripted spring/mirrored-fan behavior, temporary per-cell sprites with
rollback-owned post-crop render offsets, deterministic animation, fixed-point
full-width/shallow spring and full-cell fan sensors, living-player/sword/hazard
equal-height spring calibration, dead-body exclusion for native respawn, and
lifecycle-safe temporary velocity limits that survive contact exit,
mine-style native terrain underlay, mirrored-room rendering, and a deliberately
unresolvable sprite that must use its native fallback.
`MAP_FORMAT.md` defines the exact PLAY/map-selector pass, expected log lines, safe tint
hot reload, and invalid-layer last-known-good test.

Manual verification still covers the fixture's native fixed-address draw path, repeated
graphics rebuild/retirement soaks, additional external-sheet geometry variants, and two
clients comparing the same registry fingerprint.

## Remaining out of scope

- Raw native collision/update callbacks and moving/self-propelled custom entities.
- More weapon types and generalized custom entities.
- General live placement/editing of mod-owned tile definitions outside V2 map packages.
- Variable room dimensions or arbitrary room topology.
- A compatibility handshake that enforces the content fingerprint online.
- A map creator/editor application.
