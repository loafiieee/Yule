# Per-room opponent spawning acceptance

Automated coverage checks strict V1/V2 parsing, map-default inheritance, explicit
native overrides, mirrored generated-room mapping, native policy decisions,
actual executable CALL bytes, and Greggnogg import/export. Live gameplay remains
required; the game is never launched by these tests.

1. Open a copy of an existing multi-room V1 map in Greggnogg. Leave the map default
   Native. Set one middle source room to Never and the final source room to Always.
   Export and preview it. Confirm saving/reopening preserves both overrides.
2. Start a fresh local round. Both fighters must spawn normally. Before gaining a
   leader, verify normal death/respawn. Win a fight and enter the Never room:
   no forced trailing-opponent respawn should occur. A surviving opponent is not
   deleted; after dying there it should remain absent while the leader advances.
3. Enter a subsequent Native room: ordinary trailing-opponent respawn must resume.
   Enter the Always end room: the opponent must respawn, remain present, and be
   hittable with armed and unarmed attacks. Repeat with the other player leading
   and moving through the mirrored side. Verify goals still finish the round.
4. Set a score target. Reaching it must still stop respawning and finish normally,
   even in an Always room. Restart, switch back to a vanilla map, and confirm its
   end rooms retain native behavior. Test explicit Native under a map-wide Never.
5. Repeat on two clients with identical exported packages and framework builds,
   including deaths, leader reversals, room revisits, restart, and rollback/correction.
   Confirm no desyncs and both players see the same spawn/combat decisions.

## Native boundary

Nine verified calls to native `game_is_win_condition` are redirected: two
respawn gates, five combat gates, one dead-player timer gate, and one end-room
loser-removal gate. The global
predicate, goal checks, crowd and render callers retain native behavior. Before
writing, installation verifies every expected five-byte CALL against the supported
executable; a mismatch leaves all nine calls unchanged and logs an error.

The respawn bridge explicitly forwards EBX's target player and aligns the nested
C call stack. Continuations reload caller-saved registers before using them;
nonvolatile registers retain the normal x86 ABI. The replacement policy uses no
floating-point operations or random numbers. Its room/rules/leader/score inputs
are already part of rollback state; package policy comes from the pinned registry.
