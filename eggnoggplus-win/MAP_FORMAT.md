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
- `frame` = the byte written into `tile[1]`
- `arg` = the byte written into `tile[2]`

Some names below are exact because the callback is named in Ghidra. Some plain-English descriptions are inferred from vanilla usage.

| Glyph | Output | Meaning / notes |
| --- | --- | --- |
| `space` | `tile 0x14`, `colouring_action`, `frame 0x00`, `arg 0x00` | Empty/open filler. `.` behaves the same way. |
| `.` | `tile 0x14`, `colouring_action`, `frame 0x00`, `arg 0x00` | Same as `space`. |
| `!` | `tile 0x03`, `floor_action`, `frame 0x00`, `arg = random byte` | Floor/platform variant. |
| `#` | `tile 0x15`, `colouring_action`, `frame 0x2D`, `arg = random byte` | Decorative colouring tile variant. |
| `(` | `tile 0x14`, `colouring_action`, `frame 0x6A`, `arg = side bit` | Side-aware decorative variant. |
| `)` | `tile 0x14`, `colouring_action`, `frame 0x6B`, `arg = side bit` | Side-aware decorative variant. |
| `*` | `tile 0x1C`, `spawn_thing_action`, `frame 0x00`, `arg 0x02` | Sword pickup/spawn tile. Works even when `rules.mode` is `karate`. |
| `+` | `tile 0x16`, `colouring_action`, `frame 0x5D..0x5F`, `arg 0x00` | Random decorative colouring variant. |
| `-` | `tile 0x15`, `colouring_action`, `frame 0x3C`, `arg 0x00` | Decorative colouring variant. |
| `1` | `tile 0x1E`, `teamnogg_action`, `frame 0x05`, `arg 0x01` | Team/score target tile for side 1. |
| `2` | `tile 0x1E`, `teamnogg_action`, `frame 0x05`, `arg 0x02` | Team/score target tile for side 2. |
| `:` | `tile 0x15`, `colouring_action`, `frame 0x3D`, `arg 0x00` | Decorative colouring variant. |
| `=` | `tile 0x15`, `colouring_action`, `frame 0x1F`, `arg = random byte` | Decorative colouring variant. |
| `?` | no tile is placed | Explicit no-op/reserved glyph. |
| `@` | `tile 0x01` or `tile 0x02`, `wall_action` or `floor_action`, `frame 0x00`, `arg = auto/random` | Auto terrain glyph. It becomes wall-like when supported by solid tile above, otherwise floor-like. |
| `A` | `tile 0x0B`, `spinny_action`, `frame 0x00`, `arg 0x04` | Spinny special tile. Likely a spinning hazard/prop. |
| `C` | `tile 0x08`, `chandelier_action`, `frame 0x00`, `arg 0x1E` | Chandelier/hanging decoration variant. |
| `E` | `tile 0x0A`, `tile_action_default`, `frame 0x00`, `arg 0x00` | EGGNOGG |
| `F` | `tile 0x17`, `colouring_action`, `frame 0x59`, `arg 0x00` | Decorative colouring variant. |
| `G` | emits a `3x3` block of `tile 0x07`, `decal_action`, frames `0x28..0x3F` | Large `3x3` decal/mural block anchored above the glyph. It needs 3 tiles of headroom and cannot sit on either side edge. |
| `H` | `tile 0x15`, `colouring_action`, `frame 0x06`, `arg 0x00` | Decorative colouring variant. |
| `I` | `tile 0x15`, `colouring_action`, `frame 0x1E`, `arg = random byte` | Decorative colouring variant. |
| `K` | `tile 0x1C`, `spawn_thing_action`, `frame 0x00`, `arg 0x03` | Hazard spawn tile. This creates the moving hazard entity used by the vanilla engine's hazard update path. |
| `L` | emits a `2x3` block of `tile 0x0D`, `arty_action` | Large art block variant keyed by glyph `L`. It expands upward and cannot sit on the top 3 rows or either side edge. |
| `N` | emits a `2x3` block of `tile 0x0D`, `arty_action` | Large art block variant keyed by glyph `N`. It expands upward and cannot sit on the top 3 rows or either side edge. |
| `O` | `tile 0x1B`, `sky_glow_action`, `frame 0x21`, `arg = side bit` | Sky-glow / backdrop-light tile. |
| `P` | `tile 0x0C`, `puzzley_action`, `frame 0x00`, `arg 0x00` | Puzzle-style special tile/prop. |
| `Q` | `tile 0x1A`, `colouring_action`, `frame 0x46 or 0x47`, `arg = random byte` | Decorative colouring variant. |
| `S` | `tile 0x14`, `colouring_action`, `frame 0x07`, `arg 0x00` | Decorative colouring variant. |
| `T` | emits a tentacle column using `tile 0x13`, `tentacle_action` | Special tentacle generator glyph. `s` and `t` behave the same at plot time. |
| `W` | `tile 0x0E`, `high_water_action`, `frame 0x05`, `arg 0x00` | High/deep water tile. |
| `X` | `tile 0x05`, `spikes_action`, `frame 0x00`, `arg = random byte` | Spike hazard variant. |
| `Y` | emits a `2x3` block of `tile 0x0D`, `arty_action` | Large art block variant keyed by glyph `Y`. It expands upward and cannot sit on the top 3 rows or either side edge. |
| `Z` | `tile 0x17`, `colouring_action`, `frame 0x05`, `arg = random byte` | Decorative colouring variant. |
| `^` | `tile 0x09`, `eggnogg_action`, `frame 0x00`, `arg 0x00` | Eggnogg special tile used in vanilla maps. |
| `_` | `tile 0x04`, `ceiling_action`, `frame 0x00`, `arg = random byte` | Ceiling tile. |
| `` ` `` | `tile 0x15`, `colouring_action`, `frame 0x56`, `arg = random byte` | Decorative colouring variant. |
| `c` | `tile 0x08`, `chandelier_action`, `frame 0x00`, `arg 0x05` | Chandelier/hanging decoration variant. |
| `e` | `tile 0x15`, `colouring_action`, `frame 0x40`, `arg 0x00` | Decorative colouring variant. |
| `f` | `tile 0x18`, `scroll_action`, `frame 0x2E`, `arg 0x00` | Scroll/moving-surface tile. |
| `i` | `tile 0x1D`, `crowd_action`, `frame 0x00`, `arg = random byte` | Crowd/backdrop tile. |
| `l` | `tile 0x1F`, `score_light_action`, `frame 0x00`, `arg 0x00` | Score light / scoreboard indicator tile. |
| `m` | `tile 0x06`, `mine_action`, `frame 0x00`, `arg 0x00` | Mine tile. |
| `q` | `tile 0x1A`, `colouring_action`, `frame 0x6F`, `arg = random byte` | Decorative colouring variant. |
| `s` | emits a tentacle column using `tile 0x13`, `tentacle_action` | Same tentacle generator family as `T` and `t`. |
| `t` | emits a tentacle column using `tile 0x13`, `tentacle_action` | Same tentacle generator family as `T` and `s`. |
| `u` | `tile 0x14`, `colouring_action`, `frame 0x6C`, `arg 0x00` | Decorative colouring variant. |
| `v` | `tile 0x05`, `spikes_action`, `frame 0x02`, `arg 0x00` | Spike hazard variant. Often reads like a hanging/ceiling spike tile in vanilla rooms. |
| `w` | `tile 0x0F`, `water_action`, `frame 0x05`, `arg 0x00` | Regular/shallow water tile. |
| `x` | `tile 0x14`, `colouring_action`, `frame 0x56`, `arg 0x00` | Decorative colouring variant. |
| `|` | `tile 0x19`, `colouring_action`, `frame 0x26`, `arg 0x00` | Decorative colouring variant, commonly used as a vertical column/pipe edge in vanilla maps. |
| `~` | `tile 0x10`, `waterfall_action`, `frame 0x60`, `arg = row parity` | Waterfall tile. Also back-fills the tile above with a decorative colouring tile when appropriate. |

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
`data.json` and `data.map` text. External V2 asset SHA-256 values are mandatory
inside `data.json`, so those declared asset identities participate in the map
key. Vanilla entries use `vanilla:<index>`.

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
- custom map glyph definitions
- room script callbacks
- a separate room-level `water_kills` override independent of the native water glyphs
- custom thing types placed directly by new syntax
- custom tile behavior definitions

Those can be added later, but they should not blur the first loader implementation.

## V2 symbolic tiles and per-map tilesets

`eggnogg-map/v2` is a backwards-compatible extension of the fixed `33x12`,
center-out mirrored map model. All v1 metadata, rules, room ordering, ambience,
and appearance fields keep the same meaning. Existing v1 packages require no
changes.

V2 adds a top-level `tileset` whose definitions bind otherwise-unused printable
ASCII symbols in `data.map` to namespaced declarative tiles. The loader always
converts a symbolic cell to its validated `native_glyph` before handing the room
template to the engine. This preserves known collision/update behavior even if
the custom definition or visual asset later becomes unavailable.

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
    "tiles": [
      {
        "id": "moss_floor",
        "symbol": "$",
        "name": "Moss Floor",
        "native_glyph": "x",
        "sprite_sheet": "terrain.png",
        "asset_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "cell_w": 16,
        "cell_h": 16,
        "padding": 0,
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
acceptance map. It uses three symbols:

- `$` is a green, animated tile loaded from the map's own `tiles.png` atlas;
- `%` is a tall, tilted orange tile from `builtin:tiles`, drawn in layer bank 1;
- `&` deliberately requests a nonexistent built-in sprite, so the declared
  native `x` fallback must appear instead of a custom blue tile.

To validate the package and parser without starting the game, run this from the
repository folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_v2_map_test.ps1
```

`PASS` proves that the checked-in JSON, 33-by-12 room text, external PNG hash,
symbol table, and parser regression cases are valid. It does not prove the
fixed-address renderer; use this short visual pass for that:

1. Restart Eggnogg+ so the installed framework DLL is loaded.
2. Choose **PLAY**, advance the normal map selector to
   **V2 Symbolic Tile Demo**, and start the match.
3. In the center room, confirm the `$` groups animate green, the `%` groups are
   orange/tall/tilted, and the `&` groups use ordinary native `x` visuals rather
   than the blue tint declared on the intentionally missing sprite.
4. Walk into both outer rooms. They are copies of the same source room; the
   right copy must apply `mirror_with_room` while the center-out layout remains
   playable.
5. Check `mods/modframework.log` for all three successful stages:
   `[maps][v2_symbolic_demo] registered`,
   `map content: packed map.v2_symbolic_demo:tiles.png`, and
   `content bridge: ... definitions=3`.

For a safe hot-reload check, leave the match open, change only the first tile's
green `tint` in `maps/v2_symbolic_demo/data.json`, save, and wait about two
seconds. The `$` cells should change color without restarting. Restore the
checked-in tint afterward. To exercise last-known-good rejection, temporarily
set that tile's `layer` to `2`; the invalid edit must be rejected in the log and
the currently loaded map must remain unchanged. Restore `layer` to `0` before
continuing.

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

### `tileset.tiles[]`

Required fields:

- `id`: local tile id, unique within this map
- `symbol`: exactly one printable ASCII byte that is not already a vanilla map
  glyph, unique within this map
- `native_glyph`: a safe, single-cell vanilla behavior glyph
- `sprite_sheet`: `builtin:<name>` or a direct `.png` filename in the map folder

Optional fields and defaults:

- `name`: display/debug name; defaults to `id`
- `asset_sha256`: required for an external PNG and forbidden for a built-in key
- `cell_w`, `cell_h`: external-sheet cell size, default `16`, range `1..512`
- `padding`: external-sheet spacing in pixels, default `0`, range `0..64`
- `sprite_index`: first frame, default `0`, range `0..1000000`
- `frame_count`: consecutive frames, default `1`, range `1..256`
- `frame_ticks`: deterministic ticks per frame, default `1`, range `1..3600`
- `animation`: `loop` (default), `ping_pong`, or `once`
- `layer`: native sprite-batch bank, default `0`; valid values are `0` and `1`
- `mirror_with_room`: horizontal visual flip on the mirrored side, default `false`
- `random_phase`: deterministic per-cell animation phase, default `false`
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
The loader reports the exact rejected definition.

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
  inter-cell `padding`, with at most 65,535 cells;
- `sprite_index + frame_count` must stay inside that grid;
- `asset_sha256` is mandatory and must match the bytes on disk; and
- every map asset participates in hot-reload change detection.

Before every atlas upload, the bridge rechecks the PNG header, non-reparse
status, and full SHA-256, then packs each valid sheet with its declared grid.
Its symbolic key is cached with the resulting atlas range. A missing, modified,
mis-sliced, or unsuccessfully packed file is not resolved and therefore
degrades to the native fallback instead of loading stale metadata.

The normal hot-reload poll rescans map packages, commits a valid registry swap,
and rebuilds the graphics atlas when the map generation changes. Reload is held
stable during online rollback. If an atlas rebuild is temporarily unavailable
or fails, the new definitions stay safe and unresolved until a later retry.

The tile-definition fingerprint includes its normalized qualified id, display
name, behavior, animation, transform, tint, layer, flags, symbolic sheet key,
and asset SHA-256.
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
During hot reload the framework pins only the registry generation that supplied the
currently installed native room definitions. Once another custom or vanilla definition
is installed, that old generation is released; intermediate saved versions that were
never installed are reclaimed immediately. This keeps editor-driven reload memory
bounded without invalidating a live native pointer.

At runtime, a symbolic cell retains both its generated native fallback glyph and
a compact reference to the registered qualified tile definition. Removed
definitions never leave stale pointers: active metadata re-resolves on registry
generation changes and falls back to the native render path when a definition is
missing. A hot reload that changes `native_glyph` also falls back until the map
is regenerated; an already-created engine cell is never relabeled with new
collision behavior in place.

### Native v2 rendering bridge

The v2 package parser, validation, namespaced registry, hashes, atomic hot
reload, compact per-cell metadata, deterministic animation selection, C/Lua
query surfaces, atlas packing, and native draw bridge are implemented.

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

If metadata is stale, dimensions differ, a definition/sheet/sprite is missing,
or any transform/batch operation cannot run, the bridge returns unhandled and
the engine draws the declared `native_glyph`. A partial bind is discarded in
full. V2 does not permit custom native callbacks.

The code path is covered by strict unit tests, but fixed-address integration
still needs an in-game visual pass on the supported executable for built-in and
external sheets, mirrored rooms, atlas rebuilds, and forced missing-asset
fallback.

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
- non-null `hook` is rejected in `v1`
- verbose multi-error logging is mandatory
