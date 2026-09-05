# Duplicate sword-tile investigation

## Finding

The original executable contains a path that resets the initial room twice.
Yule's direct online start can encounter it too. This establishes a concrete
duplicate-spawn cause, but live reproduction is still needed to confirm it
accounts for every instance of the reported intermittent problem.

The decompiled executable and native call ordering show:

1. `game_init` / `game_reset` initialize `old_active_room = -1` and `started = 0`.
2. `game_start_countdown` at `0x4227C0` sets the countdown to 90 and calls
   `reset_room` at `0x41E990`. Reset dispatches mode 9 to each cell in the room.
3. Native GAME entry and `game_start` set `started = 1` after countdown setup.
4. If no earlier unstarted update established the old room, the first
   `game_update` sees a room change and calls `reset_room` again. It then stores
   the active room and only later decrements the countdown.
5. Sword reset actions allocate on each call. They do not replace an existing
   sword from the same marker. The second reset therefore adds another sword.

The issue depends on start/update ordering, explaining why it need not appear
on every start. Normal movement, dropped swords, and later room revisits were
not treated as duplicate initial spawning.

## Repair

The tile dispatcher skips mode-9 actions only when the game is started, the old
room is still -1, and the countdown is exactly 90. The first countdown reset runs
while started is zero and remains intact. Later transitions have a valid old
room and remain intact. The rest of first-room processing, including color/audio
and leader bookkeeping, still runs.

The guard covers all reset actions rather than only sword allocation. It uses
existing serialized native state, with no position-based deduplication or
unsnapshotted once-only flag. Map API version 6, added in the same batch for
`map.every`, changes the production rollback compatibility key on every map;
both online peers need matching builds.

## Validation

- Guarded core suite passed, including the new native lifecycle fixture for
  immediate/delayed starts, frame-zero restoration, restart, and room revisits.
- Static hook checks verify the guard precedes sword and generic reset dispatch.
- Guarded map-script and V2 map suites passed. The new `map.every` helper has
  periodic/phase, invalid argument, rollback replay, and large-clock coverage.
- Full production sources linked to `build/SDL2_sword_backlog.dll`.

Logs: `build/sword_backlog_core.log`, `build/sword_backlog_map_script.log`,
`build/sword_backlog_v2.log`, and `build/sword_backlog_link.log`.

The lifecycle fixture models verified native ordering; it does not execute the
original reset routine. No game was launched and the installed DLL was not
replaced. Follow the sword-spawning pass in `TESTING.md` for live acceptance.
