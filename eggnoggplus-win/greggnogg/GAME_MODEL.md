# Greggnogg game model

This note records the native behavior Greggnogg relies on when it parses,
validates, and previews a map. It is an implementation guide, not a claim that
the browser runs the EGGNOGG+ simulation. The findings combine the current
custom-map loader, tests and design notes with static Ghidra output from the
Windows game.

## Source-to-arena geometry

A source room is always 33 columns by 12 rows. A package contains one through
nine source rooms in `layout.order`, starting at the center and moving outward.
For `n` source rooms, native generation produces:

```text
final room count = 2n - 1
final tile width = (2n - 1) * 33
pixel size of one room = 528 * 192
```

Source room zero is copied to the center. Every later source room is copied to
the left with its x coordinate unchanged and to the right with source x mapped
to `32 - x`. For `[center, mid, outer]`, the result is:

```text
[outer, mid, center, mid, outer]
```

With `n` source rooms, source index `i` occupies destination room `n-1-i` on
the authored side and `n-1+i` on the mirrored side. Terrain seeds and animated
phase offsets use that destination's absolute world column, not merely the
source-room column. Greggnogg's Spatial mirror preview therefore regenerates
the right-side copy; it does not just reflect the finished canvas pixels.

Appearance-bank selection is separate from geometry and is easy to describe
incorrectly. The verified native behavior applies the `mirror` appearance bank
to the left-side copy and the `primary` bank to the center/right copies; the
right-side geometry is the spatially mirrored copy. Greggnogg therefore keeps
“mirror view,” final-room mirroring, and the `mirror` color bank as distinct
concepts.

Omitted appearance data has semantic meaning. Room fields inherit from
`defaults.room` field-by-field, and an omitted mirror bank can resolve from the
completed primary bank. Serializing empty banks can change that result, so the
editor preserves omission rather than manufacturing empty appearance objects.

The native default values for both banks are:

| Slot | RGB |
| --- | --- |
| `fg1`, `fg2`, `bg1`, `bg2` | `[0.5, 0.5, 0.5]` |
| `water`, `special` | `[0.0, 0.5, 0.75]` |
| `water_hi`, `special2` | `[0.9, 0.9, 0.9]` |

Ambience accepts integers `0..9` or the aliases `none`, `bugs`, `clouds`,
`art`, `flies`, `drips`, `dust`, `bats`, `bubbles`, and `boil`/`fumes`.
Native ambience is particle emission, not one looping backdrop: bugs emit on
16-tick boundaries with a one-in-five gate; clouds and art emit every four
ticks; flies every two; drips/dust/bubbles use sparse random gates; bats are
short-lived side-to-side particles; and boil/fumes samples random map positions
and only emits when it hits native shallow water `w`. The editor keeps these as
deterministic visual approximations because the game uses presentation RNG, but
retains native sheets, frame families, layers, and cadence boundaries.
Ambience and appearance are visual; collision and hazards come from map glyphs.

## Strict text model

`data.map` is line-oriented, fixed-width text. A section header is exactly
`[room_id]` after leading whitespace is removed. Each section then has exactly
twelve quoted rows whose interior is exactly thirty-three ASCII bytes. A row
ends at its final quote and cannot have trailing text. Spaces are cells, and an
interior `"` is a legal V2 printable symbol rather than an escape sequence.
Blank lines plus `;` and `//` comments are allowed outside room rows.

The conventional first comment is `; eggnogg-map/v1` or
`; eggnogg-map/v2`, but the manifest's exact `format` value—not the comment—is
the version authority. Duplicate sections, missing rows, unknown glyphs, or a
mismatch between sections and `layout.order` are hard errors.

The exact native V1 glyph allowlist, in addition to space, is:

~~~text
! # ( ) * + - . 1 2 : = ? @ A C E F G H I K L N O P Q S T W X Y Z ^ _ ` c e f i l m q s t u v w x | ~
~~~

## Terrain resolution and player placement

The source map is not the final tile array. Native generation expands or
rewrites several glyphs and stores cells as four-byte records conventionally
described as `[type, frame, arg, state]`.

The frame selected by a glyph case is not always the byte eventually stored in
that record. `map_set_tile` first copies the tile definition's persisted
four-byte default. The room parser writes its local frame value only when that
value is nonzero; zero is a **keep the tile definition's default** sentinel,
not a request for atlas frame zero. This distinction is observable in both
rendering and later map-generation passes. Relevant persisted results are:

| Glyph/result | Stored frame byte |
| --- | --- |
| fixed floor `!` | `0x01` |
| automatic floor `@` | `0x01` |
| automatic wall or ceiling `@` | `0x02` |
| fixed ceiling `_` | `0x02` |
| ground spikes `X` | `0x01` |
| mine `m` | `0x01` |
| spinner `A` | `0x16` |
| chandelier `C`/`c` | `0x18` |
| changing art `P` | `0x08` |
| goals `E`/`^` | `0x65` |

`@` is automatic terrain. Its result depends on neighboring solidity:

- with non-solid space above it becomes a floor/top cell;
- with a solid cell above it becomes a wall cell; and
- a post-pass converts a wall with non-solid space below into a
  ceiling/bottom cell.

Native solidity queries treat coordinates outside the map as solid. A top-row
`@` therefore begins as a wall (and becomes a bottom/ceiling only when the cell
below is non-solid), while a supported `@` on the last row remains a wall
because the below-map lookup is solid.

Any resolved solid behavior—including a V2 alias resolved to solid before this
pass—participates in that topology. `!` is a fixed floor and `_` is a fixed
ceiling; neither is interchangeable with automatic `@` for all gameplay tests.

There is no player-spawn glyph. `find_good_spot` searches the center room for
resolved automatic-floor cells, excludes a floor with a lethal tile above, and
tries to find two candidates separated beyond the opponent's column and its
neighbors. Exact choice is runtime/random. Greggnogg warns when the center room
does not expose at least two plausibly separated candidates rather than
pretending it can predict the selected pixels.

## Gameplay-significant glyphs

Several visually innocuous glyphs are active game objects or expansion
instructions:

- `E` and `^` are pass-through round-end triggers. Entry starts the native
  600-tick countdown; `^` also has a bobbing presentation.
- `X`, `v`, `W`, and `w` are lethal player hazards. `1` and `2` are
  pass-through lethal team/goal cells whose sword-hit scoring is tied to the
  authored team, not sword ownership. `~` is a non-solid, nonlethal waterfall
  visual rather than the same lethal-water behavior.
- `*` is an invisible sword-spawn marker. Karate only changes the natural
  player loadout; an authored `*` still creates a pickup.
- `K` is a marker that creates a separate native type-3 point hazard during
  room reset. The marker is not that entity's collision body or sprite.
- `O` draws no marker-local tile. It emits a `misc[0x11]` sun and trail stack at
  the room's horizontal center and one-quarter height, regardless of the
  marker's authored position.
- `i` begins as backdrop frame `tiles[0x3e]`. During `load_gfx`, Yule creates
  runtime sprite cells `0x80..0xff` by copying only pixels that decode to exact
  RGB `(128,128,128)` from the shipped `sprites.png` into opaque-white masks.
  `_crowd_action` draws four full-size tinted spectator masks from the idle
  `0xb0..0xb4`/`0xb0..0xb2` ranges: a left/right pair at the cell and another
  pair eight pixels above, with seeded position/pose/flip variation. Greggnogg
  reconstructs those masks from original source cells `0x30..`; it does not draw
  the visible character artwork in those cells. Match-only cheering states are
  intentionally not fabricated in the room editor.
- `G`, `L`, `N`, and `Y` expand to multi-cell structures in the three rows
  strictly above their source marker. `G` is centered on the marker; the
  two-column art extends toward source-left and reverses on the mirrored side.
  Their marker needs row `>= 3` and columns `1..31`; the expansions can
  overwrite cells processed earlier.
- `T`, `t`, and `s` build progressively shorter tentacle columns and require
  source row `>= 3`, `>= 2`, and `>= 1` respectively. The unchecked native
  expansion makes those constraints export-blocking rather than advisory.
- When processing `~`, native generation checks the already-plotted cell
  above. If that cell's stored frame byte is zero, it replaces it with static
  upper waterfall frame `0x62` or `0x63`; this can overwrite more than blank
  space. Stacked waterfall markers do not repeat that backfill forever because
  the placed waterfall has nonzero frame byte `0x60`. A zero parser override
  is not sufficient: for example, `E` retains its definition's `0x65` frame
  and is therefore preserved above a waterfall.

Native room reset shares a fixed thing pool. When a room contains `K`, the
combined number of `K`, `*`, and `m` reset spawners must not exceed 13; the
native `K` path dereferences allocation without a failure check. Rooms without
`K` can still receive a resource-pressure warning when the count is high, but
the verified crash condition is treated separately.

The `armed_respawn_limit` rule counts held and loose swords and forces an
unarmed respawn only when the count is greater than the configured limit.
`round_end_rooms` accepts exactly `inner_only` or `any`; the older-looking name
`any_room` is not valid. The names are easy to misread. `inner_only` preserves
the classic route where either outermost final room is automatically a win
condition and authored `E`/`^` goals can finish the round between those
endpoints. `any` removes that endpoint shortcut, so the outer rooms need a goal
or completed score target as well. The center room is never singled out by this
rule.

The palette's hitbox labels come from native tile definitions, not visual
shape. Only the terrain types produced by `!`, `@`, `_`, `X`, `v`, and `m`
carry the native solid mask. `W`, `w`, `1`, and `2` are non-solid cells with a
native lethal flag. `K` spawns a separate physics hazard. Waterfall `~`, all
tentacles, and the structure/art glyphs (`#`, arches, rows, columns, murals,
fences, pipes, and similar decoration) have no tile hitbox.

## Atlas and draw model

Greggnogg uses the decoded alpha in exact copies of the original indexed PNGs;
it never removes pixels by color. Native atlas geometry is:

| Atlas | Image | Grid/crop |
| --- | --- | --- |
| map tiles | `data/tiles.png` | `128x256`, 8 by 16 cells of `16x16` |
| sprites | `data/sprites.png` | `128x256`, 8 by 16 cells of `16x16` |
| miscellaneous | `data/misc.png` | `128x128`, 8 by 8 cells of `16x16` |
| glyph font | `data/font8x8.png` | `145x145`; 16 by 16 glyphs of `8x8` separated by one-pixel gutters |

Font glyph `n` begins at
`(1 + (n & 15) * 9, 1 + (n >> 4) * 9)`. Tile and sprite frame indices are
absolute row-major atlas indices. The recovered full draw schedule is map bank
`1`, particle layers `4/3/2`, map bank `0`, particle layer `1`, actors, map bank
`-1`, then particle layer `0`. The editor uses that same order: `O` joins the
layer-3 particle phase, sword/`K` ghosts occupy the actor phase, and foreground
`W` stays in front of actors.

Examples of verified composite recipes include:

- `!`: atlas frame `0x01` one pixel upward plus frame `0x03`, with the accent
  inheriting the generated horizontal flip;
- automatic `@`: wall frame `0x02`, floor `0x01 + 0x03`, or bottom/ceiling
  `0x02 + 0x22` after topology resolution (native tile type is separate from
  atlas frame number);
- `X`: unflipped `0x01 + 0x03` floor composite plus white spike `0x04`;
- `v`: `0x02 + 0x22` ceiling composite plus white hanging spike `0x34`; the ceiling lip's
  horizontal flip remains active for the spike tip;
- `m`: `0x01 + 0x03` floor composite plus mine `0x35` (`0x36` for its
  active/tinted state);
- `~`: water `0x60 + 0x61`, with upper surface frames `0x62/0x63`;
- `G`: `0x28 0x29 0x2a / 0x30 0x31 0x32 / 0x38 0x39 0x3a`;
- `L`: `0x40 0x41 / 0x48 0x49 / 0x50 0x51`;
- `N`: `0x44 0x45 / 0x4c 0x4d / 0x54 0x55`;
- `Y`: `0x42 0x43 / 0x4a 0x4b / 0x52 0x53`; and
- tentacles `T`, `t`, `s`: descending prefixes of
  `0x23, 0x2b, 0x33, 0x3b`.

Recovered animation details used by the preview include:

- `W` is Yule's foreground override: `tiles[0x65]` tinted `water_hi`, with
  vertical scale `3 + a + 4b`, where
  `a=.5+.5sin(6*ticks+30*x)` and `b=.5+.5sin(ticks)`;
- `w` uses the same atlas frame tinted `water`, scale 2, and canvas offset
  `8-8a`, so neighboring columns form a wave;
- `~` draws simultaneous moving 8-by-8 crops of frames `0x60` and `0x61`,
  scaled to a cell and flipped by source-row parity; `f` likewise scrolls an
  8-by-8 crop through frame `0x2e` rather than cycling atlas frames;
- `A` draws four copies of persisted frame `0x16` at uniform scale 4. Their
  angles are `23*ticks + 45*world_x + 20*i` for `i=0..3`. Native
  `quad_batch` stores the unchecked alpha calculation in an unsigned byte, so
  the exact four alpha bytes are `38, 147, 255, 107` rather than a saturated
  final copy;
- `C` and `c` assemble absolute frames `0x19`, `0x18`, `0x20`, `0x21`, and
  `0x15` into one foreground chandelier with 30-degree and 5-degree swing
  amplitudes;
- tentacle segments keep their authored frames and translate/flip together as
  one stalk; they do not cycle to neighboring atlas frames;
- `E` and `^` share the same unavailable-player fallback color. `^` uses
  phase `3*ticks+15*x`, preserving the native stagger across columns; and
- the `O` trail uses `misc[0x11]` at the screen-relative position described
  above, not a tile-atlas sun icon at the source cell.

The renderer applies these recovered actions continuously at a 60 Hz game-tick
clock. The native simulation remains authoritative for live object physics,
runtime random histories, players, and goal state. Spawn markers are shown as
editor ghosts even when the native map tile is invisible: `*` creates a sword
from `misc[0]`, while `K` creates an entity rendered from `tiles[0x6d]`.

## V2 boundary

V2 retains the V1 room dimensions, center-out geometry, and manifest fields,
then adds strict custom tile definitions, direct PNG sheets, and optional direct
`map.lua`. Custom-symbol resolution happens before native footprint, spawn, and
topology checks, so a printable symbol can alias a safe one-cell native behavior
without inheriting the original source glyph's expansion.

Package identity includes exact `data.json` and `data.map` bytes, external PNG
digests, and exact `map.lua` bytes. JSON declaration order can also affect the
script binding identity. A visual editor that rewrites whitespace, reorders
definitions, drops an unrecognized field, or converts an empty Lua script to
“absent” can therefore change compatibility or behavior. Greggnogg accepts only
strictly parsed V2 packages, preserves imported PNG/script content while staging
it, then clearly treats the result as an editable canonical project. It never
executes `map.lua` in the browser.

## Evidence trail

The model was derived from static sources; no executable needs to be launched
to use or verify the editor.

- `MAP_FORMAT.md` defines the public V1/V2 package contract.
- `custom_maps.c` is the current parser, validator, source-room generator,
  appearance resolver, package scanner, and resource-limit authority.
- `hooks.c` supplies Yule's high-water rendering override and foreground layer
  selection.
- `content_registry.c` defines V2 identity normalization and safe native
  behavior resolution.
- `tests/custom_maps_v2_test.c` captures strict-schema, asset, alias,
  footprint, identity, and package-boundary cases.
- `docs/superpowers/specs/2026-07-17-content-registry-map-v2-design.md`
  records the declarative V2 bridge and mirroring rules.
- `docs/superpowers/specs/2026-07-18-map-local-lua-design.md` records direct
  script discovery, exact-byte identity, sandboxing, rollback, and native
  `K`-hazard findings.
- `ghidra/eggnoggplus.exe.c` supplies the native control flow and atlas recipes,
  especially `mapgen_build_map` (`0x437DB0` in the audited Windows build),
  `tile_action_ex` (`0x440250`), `map_draw`, `tiledef_init`, `tiledef_update`,
  `find_good_spot`, and `player_respawn`.

When this note conflicts with current loader code or tests, those executable
contracts take priority and the editor model should be updated.
