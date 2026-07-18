# Declarative Content Registry and Custom Map V2

**Date:** 2026-07-17  
**Status:** Registry, parser, atlas, native per-cell binding, and native draw integration implemented; in-game visual QA remains manual  
**Primary code:** `content_registry.c/.h`, `content_tiles.c/.h`, `content_bridge.c/.h`, `custom_maps.c/.h`, `lua_manager.c/.h`, `hooks.c`

## Purpose

This subsystem gives mods and map packages a common, deterministic way to name and
validate custom tile definitions. Custom-map V2 adds per-map symbolic tilesets while
remaining compatible with the V1 fixed-room format.

The safety rule is fundamental: a custom tile always declares a validated vanilla
`native_glyph`. The engine receives that glyph for collision and update behavior. A
custom visual may replace the vanilla sprite only when the visual bridge can resolve it;
otherwise gameplay continues with the native fallback.

Arbitrary native callbacks, custom collision code, and new weapon behavior are not part
of this slice.

## Implemented architecture

The implementation has five layers:

1. `content_registry` owns normalized, immutable tile definitions and atomic owner
   replacement.
2. `mod.content` exposes owner-scoped registry transactions to Lua mods.
3. Custom-map V2 parses map-local symbols and assets into `map.<id>` owner transactions.
4. `content_tiles` stores compact per-cell bindings and computes deterministic render
   records without owning or altering the native tilemap.
5. `content_bridge` maps source-room metadata onto the completed native map and submits
   resolved custom visuals from the native tile draw action, with vanilla fallback on
   every failure.

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
- one safe `native_glyph` for engine behavior;
- symbolic `sprite_sheet`, first `sprite_index`, animation parameters, and layer;
- transform (`offset`, `scale`, angle), RGBA tint, and visual flags; and
- SHA-256 identities for the asset and canonical definition.

Supported animation modes are `loop`, `ping_pong`, and `once`. Animation is selected from
a deterministic tick. `random_phase` derives a stable phase from the cell rather than a
runtime RNG draw. `mirror_with_room` affects visual horizontal flip only; it does not
alter the native behavior glyph.

Validation is fail-closed:

- `sprite_index`: `0..1,000,000`
- `frame_count`: `1..256`
- `frame_ticks`: `1..3,600`
- `layer`: `0` or `1`, matching the engine's two native sprite-batch banks
- offsets: finite values in `-4,096..4,096`
- scales: finite, non-zero values in `-64..64`
- angle: finite value in `-360,000..360,000`
- tint: exactly four finite channels in `0..1`
- flags: only `mirror_with_room` and `random_phase`

The safe glyph whitelist contains only one-cell native behaviors. Blank/no-op glyphs,
waterfall/back-fill generators, and other multi-cell glyphs are rejected.

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
  "tiles": [{
    "id": "moss",
    "symbol": "$",
    "native_glyph": "x",
    "sprite_sheet": "terrain.png",
    "asset_sha256": "<64 lowercase or uppercase hex characters>",
    "sprite_index": 0
  }]
}
```

`symbol` is exactly one printable non-vanilla ASCII byte and must be unique within the
package. During room parsing, the loader replaces the symbol with `native_glyph` and
stores a compact alias beside the source cell. V1 packages continue through their prior
path and cannot declare `tileset`.

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
- exactly match the required `asset_sha256`.

External-sheet geometry is declared on the tile reference. `cell_w` and `cell_h`
default to 16 and are limited to `1..512`; `padding` defaults to 0 and is limited to
`0..64`. These fields are forbidden for `builtin:*` sheets. A valid grid may contain at
most 65,535 sprites, and `sprite_index + frame_count` must remain within that grid.
Sheet keys incorporate the asset digest and geometry, so the same file packed with
different cell geometry cannot alias one atlas range.

`custom_maps_content_sheet_for_key` repeats the non-reparse, PNG-header, and SHA-256
checks before exposing a resolved path. A changed or missing asset therefore becomes an
unresolved visual rather than silently using stale metadata.

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
  relative transform/tint composition, state restoration, and native fallback.

The remaining boundary is manual runtime verification against the supported game
executable: visually inspect a V2 package using built-in and external sheets on center,
left, and mirrored-right rooms, then force a missing/changed sheet to confirm the native
glyph remains visible. No automated test can execute the fixed-address native engine in
the unit-test process.

Only V2 map symbolic cells are currently bound into a live tilemap. Mod-owned
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
- Invalid or changed map assets reject reload or fail visual resolution without changing
  native collision behavior.

## Verification

Automated coverage is in:

- `tests/content_registry_test.c`: key/glyph validation, owner replacement/removal,
  transaction and batch atomicity, stable fingerprints, numeric bounds, SHA-256, and
  explicit transparent tint;
- `tests/content_tiles_test.c`: cell binding, native-mode filtering, animation sequence,
  mirroring, transforms, generation refresh, and owner removal fallback; and
- `tests/content_bridge_test.c`: exact center/left/right generated-room mapping,
  fail-closed partial binding, native-batch layer semantics, deterministic frame
  resolution, relative transform/tint composition, byte-exact 96-byte turtle restore,
  and resolver/transform/batch fallback; and
- `tests/custom_maps_v2_test.c`: V1 compatibility, required/overlong stable V2 ids,
  symbolic cells, duplicate/unknown key failures, embedded NUL and huge-number
  rejection, external asset hash checks, grid geometry, sprite-range bounds, and
  validation of the checked-in `maps/v2_symbolic_demo` runtime fixture.

`tests/run_v2_map_test.ps1` is the non-developer entry point for that suite. The
runtime fixture deliberately covers a map-owned external atlas tile, a transformed
built-in tile in the second native layer bank, deterministic animation, mirrored-room
rendering, and a deliberately unresolvable sprite that must use its native fallback.
`MAP_FORMAT.md` defines the exact PLAY/map-selector pass, expected log lines, safe tint
hot reload, and invalid-layer last-known-good test.

Manual verification still covers the fixture's native fixed-address draw path, repeated
graphics rebuild/retirement soaks, additional external-sheet geometry variants, and two
clients comparing the same registry fingerprint.

## Remaining out of scope

- Custom collision/update callbacks or executable map scripts.
- More weapon types and generalized custom entities.
- General live placement/editing of mod-owned tile definitions outside V2 map packages.
- Variable room dimensions or arbitrary room topology.
- A compatibility handshake that enforces the content fingerprint online.
- A map creator/editor application.
