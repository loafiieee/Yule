# Room graph implementation contract

This document defines the shared contract for Greggnogg's two-dimensional room
layout, the native loader, traversal, rollback state, previews, and online
fingerprints. The editor validator and mirrored-layout conversion exist in
`greggnogg/editor-core.js`; the matching pointer-free native structure,
validator, point lookup, and directed exit resolver live in `room_graph.h` and
`room_graph.c`. The native package reader, movement hooks, Greggnogg exporter,
preview, object placement expansion, and online content identity now consume the
same validated shape. Live multiplayer acceptance remains tracked in TESTING.md.

## Source rooms and room instances

`data.map` continues to store reusable source rooms. A graph node is one final
room instance that refers to a source room. Several nodes may use the same
source room with different positions or presentation settings. This distinction
is required to preserve today's mirrored arenas, where every outer source room
appears on both sides of the center.

Graph coordinates and doorway offsets use integer tile cells. The origin is the
top-left of the graph coordinate system: X increases rightward and Y increases
downward. A room instance occupies the half-open rectangle
`[x, x + width) x [y, y + height)` using its source room's authored dimensions.

## Layout shape

The serialized manifest uses snake_case field names:

```json
{
  "layout": {
    "kind": "room_graph",
    "room_format": "variable_cells",
    "start": "center",
    "nodes": [
      {
        "id": "center",
        "room": "center_source",
        "x": 0,
        "y": 0,
        "mirror_x": false,
        "appearance": "primary"
      },
      {
        "id": "lower_room",
        "room": "puzzle_source",
        "x": 10,
        "y": 18,
        "mirror_x": false,
        "appearance": "primary",
        "overrides": {
          "ambient": "bats",
          "native_tileset": "night.png",
          "opponent_spawn": "never"
        }
      }
    ],
    "connections": [
      {
        "id": "center_lower_room",
        "from": "center",
        "from_side": "bottom",
        "from_offset": 10,
        "to": "lower_room",
        "to_side": "top",
        "to_offset": 0,
        "span": 4,
        "one_way": false,
        "players": "both",
        "focus": "crossing"
      }
    ]
  }
}
```

Editor memory may use `roomFormat`, `mirrorX`, `fromSide`, `fromOffset`,
`toSide`, `toOffset`, and `oneWay`. The shared validator accepts either spelling
so the serializer can be the only camel-case to snake-case boundary.

Node IDs identify final instances and must be unique safe ASCII identifiers of
1-63 characters. `room` identifies a source section from `data.map`. Every
source rooms may remain unused while an author designs them; Greggnogg reports
that as a warning until an instance is placed. `mirror_x` controls geometry;
`appearance` independently selects the source room's `primary` or `mirror`
appearance bank.

`overrides` is optional and belongs to that placed copy. `ambient` accepts the
same built-in aliases, 0..9 values, and declared custom ambiance IDs as a source
room. `native_tileset` names a declared 128+ cell PNG sheet.
`opponent_spawn` is `default`, `always`, or `never`. An omitted field inherits
the source-room setting and then the map default. These fields participate in
the ordinary package fingerprint because they are stored in `data.json`.

The native room-definition bridge resolves every placed node back to its exact
source definition. It preserves that definition's ambient behavior and swaps
the two color banks only when the node's explicit `appearance` differs from
the legacy left/right heuristic. Reusing one source room many times therefore
does not borrow another room's metadata, and `mirror_x` never silently chooses
the palette.

Each connection joins a range of cells on opposite room edges. Offsets start at
the top of a left/right edge and at the left of a top/bottom edge. `span` is the
positive number of connected cells. The two world-space ranges must line up
exactly. A two-way connection can be traversed in both directions; a one-way
connection runs from `from` to `to`.

`players` controls who may cross a connection: `both` (the default),
`player1`, `player2`, or `go`. A disallowed crossing behaves like a closed room
edge without killing or moving the player. `focus` controls which crossing can
move the active room and camera: `go` preserves native GO-player ownership and
is the default, while `crossing` lets whichever allowed player crosses focus the
destination. These policies are immutable package data and therefore take part
in online identity without adding rollback state.

Connections have an optional stable editor `id` plus a canonical 1-based native
number: their position in the `connections` array plus one. Older maps without
IDs receive deterministic IDs when Greggnogg opens them. IDs use the same safe
1-63 character syntax as room nodes and must be unique. They are editor metadata;
the native graph continues to use its bounded compact array.

`map.set_exit_locked(number, boolean)` and
`map.exit_locked(number)` control and inspect that doorway. Lock bits are part
of the ordinary rollback snapshot, cover all 256 connection slots without using
author `map.state`, and block either traversal direction. Greggnogg displays the
same number in the Rooms doorway list. Blockly stores the stable ID and resolves
it to the current number when generating Lua, preventing structural edits from
silently retargeting a saved block. Handwritten numeric Lua must still be reviewed
after connection order changes.

## Validation limits and invariants

The engine-backed contract allows at most 17 final room instances and 256
connections. Every instance, including its full source-room rectangle, must fit
within coordinates -4096 through 4096. Those bounds can be changed before the
format becomes public if runtime measurements justify it.

Validation rejects:

- missing, duplicate, malformed, or unknown node and source-room IDs;
- fractional or out-of-range coordinates;
- overlapping room rectangles;
- missing or unknown start nodes;
- invalid, same-side, or self connections;
- doorway offsets or spans outside either source-room edge;
- room edges or doorway ranges that do not touch in world space;
- overlapping connection ranges on the same instance edge; and
- instances unreachable from `start`, respecting one-way direction.

Touching room rectangles are allowed. A graph containing one room and no
connections is valid. Graph bounds are the smallest rectangle containing every
validly positioned instance and may begin at negative coordinates.

## Mirrored-layout conversion

`createRoomGraphFromMirrored(document)` converts the current
`mirrored_source_rooms` order into an equivalent graph. It lays out the left
outer rooms in reverse source order, the center, then the right outer rooms.
Right-side instances use mirrored geometry and the primary appearance bank;
left-side instances keep source geometry and use the mirror appearance bank.
Adjacent rooms receive full-height connections over the shorter shared edge.

For a source order of `center, outer_1, outer_2`, the stable final instance IDs
are `left_2, left_1, center, right_1, right_2`. Conversion retains mixed source
widths and heights exactly. `convertMirroredToRoomGraph(document)` returns a
detached document with that graph while leaving all source room data unchanged.

## Runtime and editor status

The canonical graph participates in the ordinary data.json/package fingerprint.
Native movement resolves crossed edge cells through the compact pinned
connection table, while the existing rollback-owned final-room index identifies
the active node. Camera, world bounds, resets, starts, and room-local movement
use each node's X/Y origin. Managed-object placements name a final node through
`instance`, and mirrored conversion migrates old source/mirrored copies without
changing their names or geometry.

The stock player logic compares the player's world Y coordinate with
`map_pixels_h` after movement. The variable-room wrapper resolves the graph
crossing first and records the player's destination room, so that comparison
uses the destination's authored world-space bottom for an allowed, unlocked
doorway. A locked or player-restricted doorway rewinds the crossing so it acts
like a solid edge. A genuinely closed pit retains the source-room bottom and
therefore reaches the normal native fall-death path immediately. Room traversal
never writes the player's combat-hit bitfield.

Greggnogg's Rooms mode converts mirrored maps, places and drags variable-sized
room instances, adds branches in four directions, removes safe leaf instances,
changes the start room, and connects already touching rooms to form two-way or
one-way loops. It includes fit-to-view, a clickable viewport minimap,
independent tile-flip, palette-bank, ambience, native-graphics, and opponent
respawn controls, plus validated numeric doorway starts and widths. Exact
doorway spans are drawn at their authored room edges, numbered, keyboard
focusable, and linked to the matching inspector row. The editor reports graph
problems beside the canvas and can reconstruct every room coordinate from the
authored doorway sides and offsets. Arrangement repair is atomic: contradictory
loops, disconnected rooms, invalid openings, and resulting overlaps are
reported without partially moving the map. Runtime/editor doorway locks are implemented through the
rollback-safe Map API and matching beginner blocks; stable editor IDs keep those
blocks attached when earlier connections are removed. Remaining work includes
per-instance goal/progression overrides and the live multiplayer
acceptance pass in TESTING.md.
