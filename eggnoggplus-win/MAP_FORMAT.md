# Eggnogg+ Custom Map Format

Status: v1 supported; v2 parsing, registry, atlas loading, and native tile draw bridge implemented; in-game visual acceptance pending

This document defines the first custom map format for Eggnogg+.

The goal of `v1` is to match the current engine closely enough that custom maps can:
- appear in the existing pregame selector automatically
- preserve vanilla room glyph behavior
- preserve vanilla room colour and ambience behavior
- support sword and karate maps
- support score maps and other map-wide rule flags

This format is intentionally engine-native first. It does not try to invent a new abstract level format.

## Folder layout

Each custom map lives in its own folder under `maps/`.

```text
maps/
  map1/
    data.map
    data.json

  map2/
    data.map
    data.json
```

Rules:
- The folder name is the fallback map id.
- Folders starting with `_` are ignored by the scanner.
- A map folder must contain both `data.map` and `data.json`.
- Reparse-point map folders and reparse-point package files are rejected.
- `data.json` and `data.map` are each capped at 4 MiB.

## Design summary

`data.map` stores the raw room glyphs.

`data.json` stores:
- display metadata like name and author
- map-wide rules like swords vs karate
- room order
- room ambience
- room colour banks

Gameplay-significant room content stays in `data.map`.

That includes things like:
- native water tiles and waterfalls
- native sword pickup tiles
- decorative and special-purpose vanilla glyphs

This split exists because the engine already treats maps as:
- a raw source-room template
- plus separate per-room descriptor data

## Versioning

`data.json` must contain:

```json
{
  "format": "eggnogg-map/v1"
}
```

If the format string is missing or unknown, the loader must reject the map.

## V1 engine limits

These are deliberate `v1` constraints:

- Source rooms are fixed at `33x12`.
- Current loader implementation supports at most `9` source rooms per map.
- Map length is variable by changing the number of source rooms.
- Final visible room count is still mirrored by the engine.
- Layout is center-out mirrored only.
- No per-room width overrides.
- No per-room height overrides.
- No custom glyph definitions.
- No custom room callbacks yet.
- No asymmetric final-room layout yet.

If we want true variable-width rooms later, that should be a new format version because the current engine hardcodes `33` tile room width in map generation and room math.

## Map model

The engine builds a map from source rooms ordered from center outward.

If the source-room order is:

```text
[center, mid_1, outer_1]
```

The final arena is effectively:

```text
[outer_1, mid_1, center, mid_1, outer_1]
```

So in `v1`:
- `layout.order` is always source-room order from center outward
- the engine mirror is automatic
- the selector room count shown to the player should reflect the final mirrored count

## data.json

### Required fields

```json
{
  "format": "eggnogg-map/v1",
  "name": "Combat Caverns+",
  "author": "potato",
  "layout": {
    "kind": "mirrored_source_rooms",
    "room_format": "vanilla_33x12",
    "order": ["center", "mid_1", "outer_1"]
  }
}
```

### Full shape

```jsonc
{
  "format": "eggnogg-map/v1",

  "id": "combat_caverns_plus",
  "name": "Combat Caverns+",
  "author": "potato",
  "description": "Optional freeform text",
  "sort_order": 0,

  "rules": {
    "mode": "swords",
    "round_end_rooms": "inner_only",
    "score_target": null,
    "armed_respawn_limit": 4
  },

  "layout": {
    "kind": "mirrored_source_rooms",
    "room_format": "vanilla_33x12",
    "order": ["center", "mid_1", "outer_1"]
  },

  "defaults": {
    "room": {
      "ambient": "none",
      "appearance": {
        "primary": {
          "fg1": [1.0, 1.0, 1.0],
          "fg2": [1.0, 1.0, 1.0],
          "bg1": [0.0, 0.0, 0.0],
          "bg2": [0.0, 0.0, 0.0],
          "water": [0.0, 0.0, 0.0],
          "water_hi": [1.0, 1.0, 1.0],
          "special": [1.0, 1.0, 1.0],
          "special2": [1.0, 1.0, 1.0]
        },
        "mirror": {
          "fg1": [1.0, 1.0, 1.0],
          "fg2": [1.0, 1.0, 1.0],
          "bg1": [0.0, 0.0, 0.0],
          "bg2": [0.0, 0.0, 0.0],
          "water": [0.0, 0.0, 0.0],
          "water_hi": [1.0, 1.0, 1.0],
          "special": [1.0, 1.0, 1.0],
          "special2": [1.0, 1.0, 1.0]
        }
      }
    }
  },

  "rooms": {
    "center": {
      "ambient": "none"
    },
    "outer_1": {
      "ambient": "bubbles",
      "appearance": {
        "primary": {
          "bg1": [0.08, 0.08, 0.12],
          "bg2": [0.02, 0.02, 0.05]
        }
      },
      "hook": null
    }
  }
}
```

### Top-level fields

- `format`: required string, must be `eggnogg-map/v1`
- `id`: optional stable id string
  - default: folder name
- `name`: required string
  - used by the pregame selector
- `author`: required string
  - used by the pregame selector
- `description`: optional string
  - not required by the engine, but worth keeping
- `sort_order`: optional integer
  - default: `0`
  - used for deterministic custom map ordering

### rules

- `mode`: required if `rules` exists
  - `"swords"` or `"karate"`
  - controls natural player spawn loadout
  - does not remove or suppress map-authored pickups
- `round_end_rooms`: optional
  - `"inner_only"` or `"any"`
  - default: `"inner_only"`
- `score_target`: optional
  - integer greater than `0`, or `null`
  - if set, this is a score-target map
- `armed_respawn_limit`: optional integer
  - default: `4`
  - controls how many armed entities can exist before respawns are forced empty-handed

### layout

- `kind`: required
  - must be `"mirrored_source_rooms"` in `v1`
- `room_format`: required
  - must be `"vanilla_33x12"` in `v1`
- `order`: required non-empty array of room ids
  - listed from center outward
  - each id must exist in `data.map`
  - no duplicates

### defaults.room

Optional room defaults applied before per-room overrides.

- `ambient`: optional
- `appearance.primary`: optional
- `appearance.mirror`: optional

### rooms.<room_id>

Per-room overrides keyed by room id.

Supported fields:
- `ambient`
- `appearance`
- `hook`

`hook` is reserved for future room callbacks.

In `v1`:
- `hook` must be omitted or `null`
- any non-null value is an error

## Room ambience

`ambient` may be either:
- a string name
- or an integer `0..9`

Named values for `v1`:

- `none` = `0`
- `bugs` = `1`
- `clouds` = `2`
- `art` = `3`
- `flies` = `4`
- `drips` = `5`
- `dust` = `6`
- `bats` = `7`
- `bubbles` = `8`
- `boil` / `fumes` = `9`

If we discover a better canonical name for one of the numbered styles later, we can add it as an alias without changing the stored integer meaning.

## Room appearance

Each source room has two appearance banks:
- `primary`
- `mirror`

The engine uses one bank on one side of the arena and the other bank on the mirrored side.

Each bank may define:
- `fg1`
- `fg2`
- `bg1`
- `bg2`
- `water`
- `water_hi`
- `special`
- `special2`

Each value is an RGB triple:

```json
[0.0, 0.0, 0.0]
```

Rules:
- exactly 3 numbers
- each channel is a float from `0.0` to `1.0`

These colours are visual only.

They do not control whether water exists in a room or whether it is hazardous.

Actual water placement and hazard behavior comes from the room glyphs in `data.map`.

Inheritance:
- omitted room config inherits from `defaults.room`
- omitted `appearance` fields inherit field-by-field
- omitted `mirror` bank inherits the fully resolved `primary` bank

This means authors only need to specify the values they want to change.

## data.map

`data.map` is a strict text format.

It stores room glyphs exactly as the engine expects them.

### Syntax

```text
; eggnogg-map/v1

[center]
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"                _              # "
"       #        C    #           "
" #         #              #      "
"   G   #           #         G   "
"@@@@@@        #         #  @@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"

[outer_1]
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
"@  #              @@@@         @@"
"        #      #           #     "
"     #            #              "
"           #            #     #  "
"^^^^^^^^^^^^^^^^^^^^^^^^^^^@@@@@@"
"EEEEEEEEEEEEEEEEEEEEEEEEEEE@@@@@@"
"EEEEEEEEEEEEEEEEEEEEEEEEEEE@@@@@@"
"EEEEEEEEEEEEEEEEEEEEEEEEEE@@@@@@@"
"EEEEEEEEEEEEEEEEEEEEEEEEEE@@@@@@@"
"EEEEEEEEEEEEEEEEEEEEEEEEE@@@@@@@@"
```

### data.map rules

- Each room begins with `[room_id]`
- Each room must contain exactly `12` rows
- Each row must be a quoted string
- Each row must contain exactly `33` characters inside the quotes
- Leading and trailing spaces inside quotes are significant
- Blank lines between rooms are allowed
- Comment lines are allowed only outside quoted rows
- Comment prefixes allowed in `v1`:
  - `;`
  - `//`

### Allowed glyphs

`v1` should validate against the glyphs currently recognized by map generation.

Allowed glyphs:

```text
space ! # ( ) * + - . 1 2 : = ? @
A C E F G H I K L N O P Q S T W X Y Z
^ _ ` c e f i l m q s t u v w x | ~
```

Notes:
- `space` is significant and valid
- `.` is allowed because vanilla room data uses it
- `?` is reserved by current map generation and still valid as a recognized glyph
- Any other character is a hard error in `v1`

The loader should not silently treat unknown characters as empty space. That would hide author mistakes.

### Full glyph reference

This is the engine-native glyph table recovered from `mapgen_plot_room.part.0`.

For ordinary single-tile glyphs, the plotter writes:
- a tile id
- an optional frame/variant byte
- an optional arg byte

The table below uses this shorthand:
- `tile` = the tile id written by the plotter
- `action` = the tile callback registered by `tiledef_init`
- `frame` = the byte retained in `tile[1]` after the tile definition is copied
  and any nonzero parser override is applied. A zero parser override means
  “keep the tile definition's default”; it does not force atlas frame zero.
- `arg` = the byte written into `tile[2]`

Some names below are exact because the callback is named in Ghidra. Some plain-English descriptions are inferred from vanilla usage.

| Glyph | Output | Meaning / notes |
| --- | --- | --- |
| `space` | `tile 0x14`, `colouring_action`, `frame 0x00`, `arg 0x00` | Empty/open filler. `.` behaves the same way. |
| `.` | `tile 0x14`, `colouring_action`, `frame 0x00`, `arg 0x00` | Same as `space`. |
| `!` | `tile 0x03`, `floor_action`, `frame 0x01`, `arg = random byte` | Floor/platform variant. |
| `#` | `tile 0x15`, `colouring_action`, `frame 0x2D`, `arg = random byte` | brick outline decoration tile |
| `(` | `tile 0x14`, `colouring_action`, `frame 0x6A`, `arg = side bit` | left half of arch |
| `)` | `tile 0x14`, `colouring_action`, `frame 0x6B`, `arg = side bit` | right half of arch |
| `*` | `tile 0x1C`, `spawn_thing_action`, `frame 0x00`, `arg 0x02` | sword spawn glyph |
| `+` | `tile 0x16`, `colouring_action`, `frame 0x5D..0x5F`, `arg 0x00` | mushrooms |
| `-` | `tile 0x15`, `colouring_action`, `frame 0x3C`, `arg 0x00` | horizontal row tile |
| `1` | `tile 0x1E`, `teamnogg_action`, `frame 0x05`, `arg 0x01` | Team/score target eggnogg tile for side 1. |
| `2` | `tile 0x1E`, `teamnogg_action`, `frame 0x05`, `arg 0x02` | Team/score target eggnogg tile for side 2. |
| `:` | `tile 0x15`, `colouring_action`, `frame 0x3D`, `arg 0x00` | Vertical column |
| `=` | `tile 0x15`, `colouring_action`, `frame 0x1F`, `arg = random byte` | Decorative tile, looks like a hashtag |
| `?` | no tile is placed | Explicit no-op/reserved glyph. |
| `@` | `tile 0x01` or `tile 0x02`, `wall_action` or `floor_action`, `frame 0x02` (wall) or `0x01` (floor), `arg = auto/random` | Auto terrain. It becomes wall-like when supported by solid tile above, otherwise floor-like. |
| `A` | `tile 0x0B`, `spinny_action`, `frame 0x16`, `arg 0x04` | Spinny tile |
| `C` | `tile 0x08`, `chandelier_action`, `frame 0x18`, `arg 0x1E` | Swinging chandelier |
| `E` | `tile 0x0A`, `tile_action_default`, `frame 0x65`, `arg 0x00` | eggnogg, unmoving. top with ^ |
| `F` | `tile 0x17`, `colouring_action`, `frame 0x59`, `arg 0x00` | Vertical column |
| `G` | emits a `3x3` block of `tile 0x07`, `decal_action`, frames `0x28..0x3F` | Large `3x3` decal/mural block anchored above the glyph. It needs 3 tiles of headroom and cannot sit on either side edge. |
| `H` | `tile 0x15`, `colouring_action`, `frame 0x06`, `arg 0x00` | vertical column tile |
| `I` | `tile 0x15`, `colouring_action`, `frame 0x1E`, `arg = random byte` | vertical column tile |
| `K` | `tile 0x1C`, `spawn_thing_action`, `frame 0x00`, `arg 0x03` | spike ball physics hazard |
| `L` | emits a `2x3` block of `tile 0x0D`, `arty_action` | Large art block variant  |
| `N` | emits a `2x3` block of `tile 0x0D`, `arty_action` | Large art block variant |
| `O` | `tile 0x1B`, `sky_glow_action`, `frame 0x21`, `arg = side bit` | sun |
| `P` | `tile 0x0C`, `puzzley_action`, `frame 0x08`, `arg 0x00` | changing art tile. |
| `Q` | `tile 0x1A`, `colouring_action`, `frame 0x46 or 0x47`, `arg = random byte` | skull on a stick |
| `S` | `tile 0x14`, `colouring_action`, `frame 0x07`, `arg 0x00` | Eye tile. |
| `T` | emits a tentacle column using `tile 0x13`, `tentacle_action` | large tentacle |
| `W` | `tile 0x0E`, `high_water_action`, `frame 0x05`, `arg 0x00` | High/deep water tile. |
| `X` | `tile 0x05`, `spikes_action`, `frame 0x01`, `arg = random byte` | ground spikes |
| `Y` | emits a `2x3` block of `tile 0x0D`, `arty_action` | person art block. expands upwards from glyph, 2x3 |
| `Z` | `tile 0x17`, `colouring_action`, `frame 0x05`, `arg = random byte` | metal plate tile. |
| `^` | `tile 0x09`, `eggnogg_action`, `frame 0x65`, `arg 0x00` | waving eggnogg tile |
| `_` | `tile 0x04`, `ceiling_action`, `frame 0x02`, `arg = random byte` | Ceiling tile. |
| `` ` `` | `tile 0x15`, `colouring_action`, `frame 0x56`, `arg = random byte` | fence |
| `c` | `tile 0x08`, `chandelier_action`, `frame 0x18`, `arg 0x05` | chandelier, swinging only slightly |
| `e` | `tile 0x15`, `colouring_action`, `frame 0x40`, `arg 0x00` | top left of head tile? |
| `f` | `tile 0x18`, `scroll_action`, `frame 0x2E`, `arg 0x00` | scrolling decor tile. |
| `i` | `tile 0x1D`, `crowd_action`, `frame 0x3E`, `arg = random byte` | Crowd/backdrop tile. |
| `l` | `tile 0x1F`, `score_light_action`, `frame 0x02`, `arg 0x00` | score light |
| `m` | `tile 0x06`, `mine_action`, `frame 0x01`, `arg 0x00` | mine tile |
| `q` | `tile 0x1A`, `colouring_action`, `frame 0x6F`, `arg = random byte` | hanging skeleton |
| `s` | emits a tentacle column using `tile 0x13`, `tentacle_action` | small tentacle |
| `t` | emits a tentacle column using `tile 0x13`, `tentacle_action` | large tentacle |
| `u` | `tile 0x14`, `colouring_action`, `frame 0x6C`, `arg 0x00` | arch |
| `v` | `tile 0x05`, `spikes_action`, `frame 0x02`, `arg 0x00` | hanging spikes |
| `w` | `tile 0x0F`, `water_action`, `frame 0x05`, `arg 0x00` | shallow water tile |
| `x` | `tile 0x14`, `colouring_action`, `frame 0x56`, `arg 0x00` | fence |
| `|` | `tile 0x19`, `colouring_action`, `frame 0x26`, `arg 0x00` | Vertical Pipe |
| `~` | `tile 0x10`, `waterfall_action`, `frame 0x60`, `arg = row parity` | Waterfall tile |

### Gameplay-relevant native glyph behavior

Some important glyphs need explicit `v1` semantics:

- `*` is a native sword pickup/spawn tile
  - it remains valid even when `rules.mode` is `"karate"`
  - karate mode changes natural spawn loadout, not map-authored pickups
- `G` is not a simple single-cell glyph
  - it expands to a `3x3` decal block
  - it must not be placed on the top 3 rows
  - it must not be placed on the leftmost or rightmost column
- `L`, `N`, and `Y` are not simple single-cell glyphs
  - each expands upward into a `2x3` art block
  - they must not be placed on the top 3 rows
  - they must not be placed on the leftmost or rightmost column
- native water behavior is room-local because it comes from room glyphs, not JSON flags
  - water-related glyphs like `W`, `w`, and `~` can appear in some rooms and not others within the same map
  - this already allows maps with lethal water in some rooms and dry rooms elsewhere
  - `v1` does not add a separate `water_kills` room flag

If we later want decorative non-lethal water that still uses the water visuals, that should be a separate extension or a future format version.

## Pregame selector behavior

The selector metadata should come from `data.json`:
- map name from `name`
- author from `author`

The visible room count shown in the selector should be derived from the final mirrored room count, not the source-room count.

Custom maps should sort after vanilla maps, using:

1. `sort_order`
2. `name`
3. folder `id`

## Online identity and local selectors

The online manifest identifies a validated custom package as
`custom:<normalized-id>:<sig>`, where `sig` is derived from the package's
`data.json` and `data.map` text plus the loader-computed SHA-256 of every
external V2 sprite sheet. Authors do not need to calculate or maintain those
digests. Changing a PNG therefore changes the advertised map key even when
`data.json` is untouched. Vanilla entries use `vanilla:<index>`.

Numeric selectors are local registry positions and may change when installed
maps are added, removed, renamed, or reordered. The server matches the stable
manifest key, then sends each peer its own selector; peers must not compare or
persist another client's numeric selector. The broader process-wide content
registry fingerprint is available for diagnostics, but online enforcement of
that fingerprint remains future work.

## Loader behavior

The loader should scan `maps/` on startup or on map registry build.

For each candidate map folder:

1. log that scanning started
2. load `data.json`
3. load `data.map`
4. validate both files fully
5. if valid, register the map
6. if invalid, skip the map and log every collected issue

The loader should never partially register a broken map.

## Validation rules

A map is invalid if any of the following are true:

- `data.json` is missing
- `data.map` is missing
- `format` is missing or unsupported
- `name` is missing or empty
- `author` is missing or empty
- `layout.kind` is not `mirrored_source_rooms`
- `layout.room_format` is not `vanilla_33x12`
- `layout.order` is empty
- `layout.order` contains more than `9` source rooms
- a room id in `layout.order` is missing from `data.map`
- a room section exists in `data.map` but is not referenced by `layout.order`
- a room id is duplicated
- a room row is not quoted
- a room has fewer or more than `12` rows
- a room row is shorter or longer than `33` characters
- a row contains an invalid glyph
- a room containing `K` has more than `13` combined `K`, `*`, and `m`
  room-reset spawners
  - Ghidra shows 16 thing records, but `thing_new` skips slot zero
  - the two native players reserve two of the remaining 15 slots
  - mine-only and sword-only rooms retain their native fail-safe/recycling behavior
- a glyph is placed somewhere its native multi-tile expansion would write out of bounds
  - confirmed `v1` hard errors:
  - `G` on the top 3 rows or either side edge
  - `L`, `N`, or `Y` on the top 3 rows or either side edge
- a colour array is not exactly 3 numeric values
- a colour channel is outside `0.0..1.0`
- `mode` is not `swords` or `karate`
- `round_end_rooms` is not `inner_only` or `any`
- `score_target` is not `null` or a positive integer
- `armed_respawn_limit` is not a non-negative integer
- `ambient` is not a valid string alias or integer `0..9`
- `hook` is non-null

## Warning rules

Warnings do not block loading.

The loader should warn for:
- unknown top-level keys
- unknown keys under `rules`
- unknown keys under `layout`
- unknown keys under `defaults.room`
- unknown keys under `rooms.<room_id>`
- duplicate colour values between `primary` and `mirror` when both are explicitly provided
- maps with only one source room
- very long maps with many source rooms, if we think they may stress current assumptions

Unknown keys should be ignored with a warning in `v1`.

## Verbose error logging

This project is going to need aggressive diagnostics.

The map loader should log:
- every folder it scans
- every file it opens
- every validation error
- every warning
- final registration success
- final skip reason

Each log line should include a stable prefix:

```text
[maps]
```

And when possible:
- folder id
- file name
- room id
- line number
- column number
- offending key or glyph

### Example errors

```text
[maps][combat_caverns_plus] scanning folder: maps/combat_caverns_plus
[maps][combat_caverns_plus] loading json: maps/combat_caverns_plus/data.json
[maps][combat_caverns_plus] loading map: maps/combat_caverns_plus/data.map
[maps][combat_caverns_plus][data.json] warning: unknown key "display_name"; ignoring
[maps][combat_caverns_plus][data.map][room=center][line=5][col=17] error: invalid glyph "$"
[maps][combat_caverns_plus][data.map][room=outer_1][line=19] error: expected 33 glyphs, got 32
[maps][combat_caverns_plus][data.json][rooms.outer_1.ambient] error: unknown ambient "bubbls"
[maps][combat_caverns_plus] map rejected: 3 errors, 1 warning
```

### Error collection

The loader should collect multiple issues per map before rejecting it.

Do not stop on the first error unless parsing cannot continue at all.

Preferred behavior:
- collect as many structural issues as practical
- emit one summary line at the end

Summary example:

```text
[maps][combat_caverns_plus] map rejected: 7 errors, 2 warnings
```

### Hard parse failures

If a file cannot even be parsed, emit the exact parse context:

```text
[maps][combat_caverns_plus][data.json][line=14][col=9] error: invalid JSON, expected ',' after property
```

```text
[maps][combat_caverns_plus][data.map][line=22] error: room row must be a quoted 33-character string
```

### Success logging

On success, log the resolved registration info:

```text
[maps][combat_caverns_plus] registered: name="Combat Caverns+" author="potato" source_rooms=4 final_rooms=7 mode=swords
```

This makes it much easier to confirm that parsed values match intent.

## Historical implementation order

The implementation followed this dependency order:

1. scanner and registry
2. `data.json` parser
3. `data.map` parser
4. validation and verbose logging
5. in-memory custom map descriptor
6. map selector integration
7. map generation hook

This keeps the parser and diagnostics testable before touching gameplay hooks.

## Explicit non-goals for v1

Not supported in `v1`:
- variable room width
- variable room height
- arbitrary final-room layouts
- V2 `tileset` declarations inside a v1 package
- room script callbacks
- a separate room-level `water_kills` override independent of the native water glyphs
- custom thing types placed directly by new syntax
- arbitrary scripted/native tile callbacks or self-propelled custom entities

Those can be added later, but they should not blur the first loader implementation.

## V2 symbolic tiles and per-map tilesets

`eggnogg-map/v2` is a backwards-compatible extension of the fixed `33x12`,
center-out mirrored map model. All v1 metadata, rules, room ordering, ambience,
and appearance fields keep the same meaning. Existing v1 packages require no
changes.

V2 adds a top-level `tileset` whose definitions bind any printable ASCII symbol
in `data.map` to namespaced declarative tiles. Map-local lookup takes precedence
over the vanilla glyph table, so the tileset can skin ordinary builtin symbols
as well as introduce otherwise-unused symbols. The loader always converts a
bound cell to a validated, single-cell native behavior glyph before handing the
room template to the engine. This preserves known collision/update behavior even
if the custom definition or visual asset later becomes unavailable.

```jsonc
{
  "format": "eggnogg-map/v2",
  "id": "mossy_caverns",
  "name": "Mossy Caverns",
  "author": "potato",
  "layout": {
    "kind": "mirrored_source_rooms",
    "room_format": "vanilla_33x12",
    "order": ["center", "outer"]
  },
  "tileset": {
    "sprite_sheet": "terrain.png",
    "cell_w": 16,
    "cell_h": 16,
    "padding": 0,
    "tiles": [
      {
        "id": "moss_floor",
        "symbol": "$",
        "name": "Moss Floor",
        "collision": "solid",
        "sprite_index": 0,
        "frame_count": 4,
        "frame_ticks": 6,
        "animation": "loop",
        "layer": 0,
        "mirror_with_room": true,
        "random_phase": true,
        "offset_x": 0,
        "offset_y": 0,
        "scale_x": 1,
        "scale_y": 1,
        "angle_degrees": 0,
        "tint": [1, 1, 1, 1]
      }
    ]
  }
}
```

The corresponding cell in `data.map` is just the declared one-byte symbol:

```text
[center]
"$                                "
```

The row still contains exactly 33 cells and the room still contains exactly 12
rows.

### Five-minute V2 test

The repository includes `maps/v2_symbolic_demo`, a deliberately small runtime
acceptance map. Its 128x384 `tiles.png` declares a 128-cell native prefix plus
64 appended custom cells, alongside five exception definitions:

- `$` is a green, animated, solid tile loaded from the map's own `tiles.png` atlas;
- `%` is a tall, tilted orange tile from `builtin:tiles`, drawn in layer bank 1;
- `&` deliberately requests a nonexistent built-in sprite, so the declared
  native `x` fallback must appear instead of a custom blue tile;
- `@` has no tile definition at all. It proves that `native_layout: true`
  automatically reskins ordinary vanilla terrain through the map atlas;
- `>` is a solid spring that uses appended map-atlas cell `128`; `map.lua`
  gives it a full-width six-pixel lower-body/probe sensor, treats adjoining
  spring cells as one pad, gives living players and native things calibrated
  launch velocities for the same approximate height, deliberately ignores dead
  bodies so native respawn can complete, and temporarily changes the representative
  cell to the second appended frame at
  index `129`. Its custom art is drawn over the resolved native terrain so the
  ground beneath it keeps the normal floor surface; and
- `}` is a pass-through fan whose `map.lua` acceleration reverses in the
  mirrored room.

To validate the package and parser without starting the game, run this from the
repository folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_v2_map_test.ps1
```

`PASS` proves collision preset resolution, the backwards-compatible declarative
force helper, native nonnegative truncate/floor cell lookup, programmable
fixed-point sensor geometry, post-native-tick integration, the
checked-in JSON/room/script package, sandbox validation, runtime PNG/script
hashing, asset-bound online identity, whole-map symbol overrides (including
space/multi-cell vanilla source glyphs), and parser regression cases. The
regression changes external PNG and `map.lua` bytes and verifies that the
scanned online map key changes without hand-authored digest fields.
It does not prove the fixed-address renderer/physics against the game executable;
use this short runtime pass for that:

1. Restart Eggnogg+ so the installed framework DLL is loaded.
2. Choose **PLAY**, advance the normal map selector to
   **V2 Symbolic Tile Demo**, and start the match.
3. In the center room, confirm the `$` groups animate green and block movement,
   the `%` groups are orange/tall/tilted, all ordinary `@` terrain comes from the
   map-local native-layout sheet, and the `&` groups use the native `x` fallback
   behavior rendered through that same whole-map sheet.
4. Walk into and land on the far-left, center, and far-right portions of yellow
   `>` pads and confirm every portion launches vertically without erasing
   horizontal motion. Passing above the six-pixel lower-body/probe sensor
   without touching it must not launch. Crossing a seam between adjoining
   spring cells must not create another launch. Living players use `vy = -4.0`
   while swords and the native point-mass hazard spawned by the nearby `K`
   marker use `vy = -2.8`; their different native gravity scales should
   produce a rise of roughly 53 pixels instead of throwing native things much
   higher. Attack immediately before the bounce and confirm the eight-tick
   rollback-owned upward limit prevents the delayed unarmed kick from increasing
   launch speed even after the player has left the sensor.
   Put a dead body on the pad and confirm it is never relaunched, the spring does
   not flash for it, and the normal native respawn completes.
   The native floor surface beneath/around the spring must remain visually
   continuous. Only the contacted spring cell should flash to its temporary
   sprite, with the extended frame using its user-tuned render-only alignment
   so its base stays aligned with the idle artwork, and then restore itself.
   The native floor/collision must not move with that visual offset. Walk
   through a blue `}` fan and confirm its full-cell body sensor accelerates
   horizontally up to its cap.
5. Walk into both outer rooms. They are copies of the same source room; the
   right copy must apply `mirror_with_room` while the center-out layout remains
   playable, and its fan must push in the opposite horizontal direction.
6. Check `mods/modframework.log` for all three successful stages:
   `[maps][v2_symbolic_demo] registered`,
   `map content: packed map.v2_symbolic_demo:sheet-...`,
   `map script: selector=... active`, and
   `content bridge: ... definitions=5`.

For a safe hot-reload check, leave the match open, change only the first tile's
green `tint` in `maps/v2_symbolic_demo/data.json`, save, and wait about two
seconds. The `$` cells should change color without restarting. Restore the
checked-in tint afterward. Script source is intentionally pinned with the
native room generation, so a valid `map.lua` edit is used the next time the map
is generated rather than replacing callbacks underneath live rollback. To
exercise last-known-good rejection, temporarily make `map.lua` invalid; the
edit must be rejected in the log and the last valid package must remain
installed. Restore the file before continuing.

Generation-retirement is a memory-lifetime invariant rather than a reliable
visual observation. Its automated coverage remains the retirement/static and
content-bridge tests; long live-edit memory soaks remain release QA.

### V2 namespace and identifiers

- A map's content owner is derived as `map.<id>` and normalized to lowercase.
- V2 therefore requires a stable `id` that can form a content namespace.
- Owner and tile ids use ASCII letters, numbers, `.`, `_`, and `-`.
- An id may not start with `.` or `-`.
- A tile's qualified key is `map.<id>:<tile-id>`.
- Owner and local ids are capped at 47 characters each. Because `map.` is part
  of the owner, a v2 map `id` is capped at 43 characters; diagnostics report an
  overlong or invalid id instead of truncating it.
- Two packages may not claim the same map id or content namespace.

### Map-level `tileset`

The following fields belong to the map's tileset and are inherited by every
listed tile that does not override them:

- `sprite_sheet`: `builtin:<name>` or a direct `.png` filename in the map folder
- `asset_sha256`: optional external-PNG integrity pin; normally omitted
- `cell_w`, `cell_h`: external-sheet cell size, default `16`, range `1..512`
- `padding`: external-sheet spacing in pixels, default `0`, range `0..64`
- `native_layout`: default `false`. When `true`, the default must be a direct
  external PNG whose declared grid contains at least 128 cells. The first 128
  cells are the native prefix and use the same row-major order as
  `data/tiles.png`; any additional cells are free for custom tile sprites.
  Image dimensions, cell dimensions, padding, grid shape, and the number of
  additional cells are otherwise author choices. During native draw actions,
  that sheet replaces the atlas base for every ordinary, unlisted vanilla map
  glyph.

This is a genuine per-map tileset: a package can declare the sheet once and use
it without any `tiles` entries at all. `native_layout` changes presentation only;
the original glyphs still own collision, hazards, animation logic, particles,
and every other native update. It is rejected for built-in sheets and grids
with fewer than 128 cells, because native actions can address any record in
that prefix. PNG bytes are still hashed into package/online identity even when
no explicit tile references the sheet.

With `native_layout: false`, the top-level sheet/grid is simply a concise default
for entries in `tileset.tiles`. It does not reskin unlisted native glyphs.

### `tileset.tiles[]`

Required fields:

- `id`: local tile id, unique within this map
- `symbol`: exactly one printable ASCII byte (`0x20..0x7e`, including space and
  vanilla glyphs), unique within this map
- `sprite_sheet` only when `tileset.sprite_sheet` is absent; otherwise the map
  default is inherited

Optional fields and defaults:

- `name`: display/debug name; defaults to `id`
- `collision`: `native` (default), `solid`, `pass_through`, or `hazard`
- `native_glyph`: safe, single-cell vanilla behavior glyph; required for
  `collision: native` unless `symbol` itself is safe and single-cell, in which
  case it defaults to `symbol`; omit it for collision presets
- `force_mode`: backwards-compatible declarative shortcut, `add` (default) or
  `set`; requires an authored force axis
- `force_x`, `force_y`: finite per-tick velocity components in `-64..64`; each
  present field opts that axis into the interaction, including explicit zero
- `max_speed_x`, `max_speed_y`: optional absolute post-force speed clamps in
  `0..64`; `0` means unclamped and a clamp requires its matching force axis
- `sprite_sheet`: optional per-tile exception to the map default, using
  `builtin:<name>` or a direct `.png` filename in the map folder
- `asset_sha256`: optional external-PNG integrity pin; when present it must be
  64 hexadecimal characters and match the runtime-computed digest; omit it for
  normal map authoring, and always omit it for a built-in key. It inherits only
  while using the same default sheet; a per-tile sheet exception owns its pin.
- `cell_w`, `cell_h`: external-sheet cell size inherited from the map default
  (otherwise `16`), range `1..512`
- `padding`: external-sheet spacing inherited from the map default (otherwise
  `0`), range `0..64`
- `sprite_index`: first frame, default `0`, range `0..1000000`
- `frame_count`: consecutive frames, default `1`, range `1..256`
- `frame_ticks`: deterministic ticks per frame, default `1`, range `1..3600`
- `animation`: `loop` (default), `ping_pong`, or `once`
- `layer`: native sprite-batch bank, default `0`; valid values are `0` and `1`
- `mirror_with_room`: horizontal visual flip on the mirrored side and horizontal
  reversal for the legacy declarative `force_x`, default `false`; `map.lua`
  reads the same cell flag as `tile.mirrored`
- `random_phase`: deterministic per-cell animation phase, default `false`
- `native_visual`: `replace` (default) or `underlay`. `underlay` draws the
  generated native cell first, including the engine's generic sprite fallback
  when its action returns no draw, restores the complete turtle state, and then
  draws the custom sprite. It does not change native collision/update behavior;
  use it for mine-like props such as springs that should retain the vanilla
  floor surface beneath their artwork.
- `offset_x`, `offset_y`: finite offsets in `-4096..4096`, default `0`
- `scale_x`, `scale_y`: finite non-zero scale in `-64..64`, default `1`
- `angle_degrees`: finite rotation in `-360000..360000`, default `0`
- `tint`: four finite RGBA channels in `0..1`, default `[1,1,1,1]`

A package may declare at most 64 tile definitions and 16 unique external-sheet
grid configurations. Reusing the same filename/hash/grid does not consume
another sheet slot.

Safe single-cell behavior glyphs are deliberately narrower than the full v1
glyph set. Multi-cell generators, blank/no-op glyphs, waterfalls, and other
glyphs that back-fill neighboring cells cannot be used as `native_glyph`.
`K` remains excluded from generic `native_glyph` despite its one-cell marker
because content-registry owners are not necessarily tied to a fully audited map
package. Direct `K` glyphs in `data.map` are supported only after the loader
proves that their room has no more than 13 combined `K`, `*`, and `m`
room-reset spawners. The exact diagnostic includes the per-kind counts. These
glyphs can still be used as the source `symbol`: alias lookup happens first and
collapses each occurrence to the explicitly selected safe one-cell behavior.
Defining `symbol: " "` is therefore valid and intentionally binds every space
cell in the package.

Collision presets are implemented through verified native behaviors rather
than a framework collision solver: `solid` emits `@`, `pass_through` emits `x`,
and `hazard` emits `X`. This makes the preset apply to native players and map-
aware entities even if the custom sprite or script cannot resolve. Existing
`force_x`/`force_y` definitions remain supported as a small declarative
shortcut, but new special behavior should normally live in `map.lua` instead
of growing JSON. Both layers run after each native simulation tick for players
and verified type-2 swords in the fixed 16-slot thing pool. World points use
the engine's nonnegative truncate/floor grid, so each tile owns its full visual
rectangle. Players sample that grid at their center plus the half-tile foot
boundary needed after native floor collision restores their center outside a
solid cell.
Swords use only the center point used by their native movement routine;
unverified native thing types receive no guessed player-sized bounds.
Non-finite/out-of-map coordinates are ignored. Live play, local/loopback,
GGPO, and rollback replay use the same call path.

Vanilla `@` terrain is resolved once while each room is plotted from top to
bottom. It checks only the already-plotted cell directly above: a non-solid
cell produces the floor form, while a solid cell produces the wall/fill form.
A solid custom tile therefore makes the `@` below it use the wall form, just as
a vanilla mine does. Mines look like standalone surface props because their
draw action renders a floor base before the mine artwork; they do not have a
special neighbor exemption. `native_visual: underlay` exposes that safe visual
composition without inheriting the mine's trigger/spawn behavior.

### Optional `map.lua`

A V2 folder may contain one `map.lua` beside `data.json` and `data.map`. It is
auto-discovered; do not add a script path or behavior program to JSON. Keeping
native collision in JSON means the map stays physically safe if a sprite or
script fails, while Lua can express the behavior that is actually unique to the
map.

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
  local vx = object.vx + push
  if vx > 3 then vx = 3 end
  if vx < -3 then vx = -3 end
  object.vx = vx
end)
```

Event registration:

- `map.sensor(tile_ref, options)` optionally replaces the legacy point probes
  for that tile binding with one deterministic contact sensor;
- `map.on_enter(tile_ref, callback)` runs once when an object starts touching
  the configured cell or binding-scoped region;
- `map.on_contact(tile_ref, callback)` runs once per deterministic game tick
  while it remains touching that cell;
- `map.on_leave(tile_ref, callback)` runs once when it stops touching that
  cell, or every cell of a binding-scoped region; and
- `map.on_tick(callback)` runs once after that tick's contact events.

`tile_ref` is either the definition's one-byte source symbol or its canonical
qualified key (`map.<map-id>:<tile-id>`). Unknown references or duplicate
event/reference registrations reject the package.

`map.sensor` is registered once while the file loads. Its strict options are:

- `tile_box = { left, top, right, bottom }`: the active rectangle relative to
  each generated cell, in tile units; `(0,0)` is its top-left and positive Y is
  down. The default is the full cell. Each edge is quantized to 1/256 tile,
  must be in `-2..3`, and width/height must remain positive after quantization.
- `object_box`: `"center"` (default), `"body"`, `"feet"`, or a custom
  `{ left, top, right, bottom }` relative to the object's native center. `body`
  is the verified native-radius square and `feet` is its full-width bottom
  edge. Custom edges use tile units in `-2..2` and the same positive-extent
  rule.
- `objects`: a non-empty dense list of selectors. `"player"` retains legacy
  behavior and includes both living player records and state-8 dead bodies;
  `"alive_player"` and `"dead_body"` select those profiles independently,
  while `"sword"` selects verified type-2 things and `"hazard"` opts into the
  verified native type-3 point hazard used by `K`. The compatibility default is
  player/dead-body/sword, with hazards excluded. Overlapping, unknown, duplicate,
  or sparse selectors are rejected.
- `contact_scope`: `"cell"` (default) tracks each generated cell independently;
  `"binding"` unions adjoining/intersecting cells with the same qualified tile
  binding for an object. Binding scope fires at most one contact callback per
  tick, does not re-enter while crossing directly from one such cell to another,
  and leaves only after a complete tick touching none of them.
- `mirror_with_room`: when true, mirrors the tile box horizontally inside the
  cell in a mirrored room; default `false`.

Edge contact counts. The host enumerates the complete bounded cell neighborhood
in stable top-to-bottom, then left-to-right order. All sensor intersections for
one object use the same pre-callback physics position, so moving `object.x` in
one callback cannot change which later cells fire during that tick. A binding
without `map.sensor` keeps the original player center/half-tile-foot and sword
center probes for compatibility with existing scripts and declarative forces.

The callback `object` has writable finite `x`, `y`, `vx`, and `vy`, with
`object:set_velocity(vx, vy)`, `object:add_velocity(dx, dy)`, and
`object:set_velocity_limits(limits, duration_ticks)` plus
`object:clear_velocity_limits()`. A velocity-limit table is
strict and must contain at least one of `min_vx`, `max_vx`, `min_vy`, or
`max_vy`, each finite and in `-64..64`; paired minima may not exceed their
maxima. Duration is an integer from 1 through 1,000,000 ticks. Calling it
replaces that object's previous temporary limits; clearing an absent record is
a no-op. Limits clamp after native physics and map callbacks and store the
remaining lifetime with object id, lifecycle, and explicit kind in rollback
state. A live player and its state-8 dead body share one fixed native lifecycle,
so that specific kind transition retains the policy; unrelated/recycled kinds
cannot inherit it. This lets a shallow spring keep `{ min_vy =
target_vy }` active after contact ends, covering native changes such as the
unarmed kick that arrives on the following tick without blocking ordinary
downward motion. The object also exposes
read-only `kind` (`"player"`, `"dead_body"`, `"sword"`, or `"hazard"`), stable
numeric `id`, and the verified `contact_radius` (6 pixels for players/dead
bodies, 4 for swords, and 0 for the point-colliding native hazard). These sensor
profiles describe supported physics contact geometry; they do not replace the
game's separate sprite-derived combat hurtboxes. The callback
`tile` has read-only `x`, `y`, `key`, `symbol`, and `mirrored`, plus
`tile:set_sprite(index, duration_ticks [, options])` and `tile:reset_sprite()`.
The optional strict plain table accepts `offset_x` and `offset_y`: finite
additive destination-pixel offsets in `-4096..4096`, quantized to 1/256 pixel.
Positive X is right and positive Y is down. Missing offsets default to zero on
every call, and unknown fields, non-numbers, metatables, or out-of-range values
fault the script callback.

A temporary sprite applies only to that exact cell, stays on its already
declared sheet, and uses the deterministic map clock rather than render time.
Its offset is applied after atlas-cell cropping and is added to the JSON tile
offset, so artwork may extend outside its source grid cell without sampling a
neighboring sprite. The sprite and offsets expire, reset, fault-roll back, and
snapshot-restore atomically. They move only the temporary custom sprite—not the
native underlay, tile/collision grid, contact sensor, or any spawned entity.

Persistent script values belong in `map.state`. It holds at most 64 entries;
keys are at most 31 bytes, and values are `nil`, boolean, finite number, or a
string of at most 63 bytes. `nil` deletes a value. `map.tick()` returns the
rollback-tracked clock. `map.random()`, `map.random(max)`, and
`map.random(min,max)` use a separate rollback-tracked deterministic generator.
Do not keep mutable callback state in ordinary globals or captured locals.

Map scripts run in their own restricted LuaJIT state, not the general mod VM.
They have a 2 MiB default memory cap and 100,000-instruction load/callback
budget. Files, OS/process access, networking, modules/`require`, dynamic code,
bytecode, debug, FFI, JIT controls, coroutines, and `math.random` are unavailable.
A real `^` power operator is also rejected because its libm result is not
bit-stable enough for rollback across different CPUs; carets inside strings or
comments remain ordinary text. Use explicit multiplication for bounded powers.
A runtime fault discards partial callback writes, logs a bounded error, and
stops scripted behavior while preserving native collision/fallback visuals.

The complete fixed script state—`map.state`, RNG, tick, object lifecycle
generations, explicit object kinds, cell/binding contact history, temporary
velocity limits, temporary sprites, and fault status—is embedded in rollback
snapshots. Reusing a native
thing slot creates a new contact identity; exact kind/updater/lifecycle checks
prevent a delayed sword or K-hazard leave callback from writing into the
replacement object. Script
bytes and canonical tile bindings affect the package signature and advertised
server map key; that existing 32-bit key is compatibility metadata, not a
cryptographic content proof. The script is
validated and retained from one exact bounded read, then activated only from
the map generation pinned by native map creation. It never hot-swaps underneath
a running online rollback match. Managed prematch requires that exact pinned
script id to be active and non-faulted before publishing the rollback layout or
sending READY; failure aborts setup with a clear hub reason instead of silently
starting without the map's behavior.

`map.lua` is limited to deterministic map/tile behavior. It may inspect and
change position/velocity for the explicitly selected, verified native `K`
hazard profile, but it cannot create a new entity type, spawn arbitrary moving
hazards, replace the collision solver, access audio/UI, or use the general
`mod.*` API. Those remain parts of the broader custom-content backlog. The full
runtime contract and threat model are in
`docs/superpowers/specs/2026-07-18-map-local-lua-design.md`.

Native `K` is not a movable tile or a shortcut around that boundary. Ghidra
shows that it is a pass-through room-reset marker which spawns a separate native
type-3 point-mass hazard with gravity, damped map bouncing, a fixed vanilla
sprite, and a 10-pixel player-damage radius. A sensor that opts into `"hazard"`
can interact with that already-spawned object's velocity; moving or offsetting
the marker's custom sprite still does not move the entity. Its native spawn
action also fails to check pool-allocation failure. Package validation now
rejects every `K`-bearing room above the conservative 13-object combined
`K`/sword/mine reset budget (16 records, minus skipped slot zero and two
players). This makes bounded direct native `K` use safe without pretending the
marker is programmable. Moving blocks still belong in the rollback-safe
custom-entity API rather than `tile:set_sprite`.

Built-in keys currently recognized by the runtime resolver are
`builtin:sprites`, `builtin:tiles`, `builtin:misc`, and `builtin:glyphs`.
External grid fields are forbidden on built-in keys because the native atlas
already defines their slicing.

### Asset security and compatibility

External v2 sprite sheets are intentionally constrained:

- the path must be a direct filename in the map folder (no subdirectories,
  drive names, `..`, alternate data streams, or absolute paths);
- the file may not be a directory or reparse point;
- it must have a valid PNG signature/IHDR and dimensions from `1x1` through
  `4096x4096`;
- it must be non-empty and no larger than 64 MiB;
- its dimensions must form a whole `cell_w` by `cell_h` grid with the declared
  inter-cell `padding`, with at most 8,192 cells;
- `sprite_index + frame_count` must stay inside that grid;
- the loader always computes the file's SHA-256; an optional declared
  `asset_sha256` must match it; and
- every map asset participates in hot-reload change detection.

An optional `map.lua` follows the stricter direct/non-reparse 256 KiB rules in
the section above. Its exact retained bytes are SHA-256 hashed automatically;
there is no JSON digest field for authors to maintain.

Authors do not need to calculate or paste a SHA-256 for `tiles.png`. The runtime
hash is mandatory for safe asset identity and online compatibility; the JSON
field is only an optional pin for packages that want an explicit expected hash.

Before every atlas upload, the bridge rechecks the PNG header, non-reparse
status, and full SHA-256 against the digest captured during the registry scan,
then decodes each valid sheet, removes declared inter-cell padding into a tight
temporary grid, and checks that the complete sheet fits the engine's fixed
8,192-sprite global store before packing it. Its symbolic key is cached with
the resulting atlas range. A missing, modified, mis-sliced, over-capacity, or
unsuccessfully packed file is not resolved and therefore degrades to the native
fallback instead of loading stale metadata.

The normal hot-reload poll rescans map packages, commits a valid registry swap,
and rebuilds the graphics atlas when the map generation changes. Reload is held
stable during online rollback. If an atlas rebuild is temporarily unavailable
or fails, the new definitions stay safe and unresolved until a later retry.

The tile-definition fingerprint includes its normalized qualified id, display
name, behavior, animation, transform, tint, layer, flags, symbolic sheet key,
and runtime-computed asset SHA-256.
Definitions are sorted before hashing, so declaration order does not affect the
registry fingerprint. This fingerprint is suitable for a later online content
compatibility gate.

### Atomic reload and fallback behavior

Every map owns a complete content transaction. A successful rescan replaces all
affected map owners in one registry swap. Duplicate ids/symbols, conflicting
owners, allocation failure, or a validation error in a previously valid package
leaves the prior hot-loaded map/content registry intact. A newly added invalid
folder is diagnosed and skipped without blocking unrelated valid packages. The
failed filesystem signature is remembered so the loader does not spam retries
until a file changes again.

The native engine keeps direct pointers to the custom map name, author, and room rows.
During hot reload the framework pins the registry generation that supplied the
currently installed native room definitions and optional script. Once another custom
or vanilla definition is installed, that old generation is released; intermediate
saved versions that were never installed are reclaimed immediately. This keeps editor-
driven reload memory bounded without invalidating native pointers or changing live
callbacks.

At runtime, a map-local bound cell retains both its generated native fallback glyph and
a compact reference to the registered qualified tile definition. Removed
definitions never leave stale pointers: active metadata re-resolves on registry
generation changes and falls back to the native render path when a definition is
missing. A hot reload that changes `native_glyph` also falls back until the map
is regenerated; an already-created engine cell is never relabeled with new
collision behavior in place.

### Native v2 rendering bridge

The v2 package parser, whole-map symbol override precedence, collision presets,
deterministic map-local Lua/contact behavior, legacy declarative force helper,
validation, namespaced registry, hashes, atomic hot reload, compact per-cell
metadata, deterministic animation/override selection, C/Lua query surfaces,
atlas packing, native draw bridge, and rollback integration are implemented.

After native map generation completes, the bridge requires the exact fixed
layout dimensions `(2 * source_rooms - 1) * 33` by `12`. Source room `0` binds
to the center. Every later source room binds once on the left with source x
unchanged and once on the right at local x `32 - source_x`. Only the right copy
is marked mirrored.

During native tile draw mode, an exactly aligned bound cell resolves its current
animation frame from the rollback-tracked game tick. The custom transform is
composed with the tile transform already prepared by the engine, its tint
multiplies the current native/map tint, and all 96 bytes of turtle state are
restored after the sprite is queued. `layer` selects native batch `0` or `1`;
there is no arbitrary signed z-order in this engine.

When `tileset.native_layout` is enabled, an explicitly bound cell still takes
precedence and may use the map default or a different per-tile sheet. If its
custom render cannot resolve, the native fallback runs with the map-wide sheet.
Unbound native cells never enter the content bridge; their normal native action
is simply drawn against the validated map atlas base.

If metadata is stale, dimensions differ, a definition/sheet/sprite is missing,
or any transform/batch operation cannot run, the bridge returns unhandled and
the engine draws the declared `native_glyph`. A partial bind is discarded in
full. V2 does not expose raw native callbacks; its bounded map VM is the
programmable gameplay layer.

The code path is covered by strict unit tests, but fixed-address integration
still needs an in-game visual/physics pass on the supported executable for
built-in and external sheets, collision presets, scripted spring/fan contact,
temporary per-cell sprites, mirrored rooms, rollback, atlas rebuilds, and
forced missing-asset/script fallback.

## Conformance checklist

When changing the implemented format, preserve these constraints:

- `33x12` fixed source rooms for `v1`
- mirrored center-out layout for `v1`
- `data.map` uses quoted rows
- `name` and `author` live in `data.json`
- `rules.mode` affects natural spawn loadout only
- map-authored sword pickups remain valid in karate maps
- water hazard is authored through native room glyphs, room by room
- v1 compatibility keys warn when unknown; v2 `tileset` and tile-definition
  keys reject unknown fields so misspelled content settings cannot be ignored
- optional executable behavior is a direct bounded `map.lua`, never JSON code
- non-null `hook` is rejected in `v1`
- verbose multi-error logging is mandatory
