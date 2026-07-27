# Online Match Result Toast

**Date:** 2026-07-17
**Status:** Implemented; toast-only revision 2026-07-18; two-client visual/result-report acceptance remains
**Primary code:** online result tracking and toast rendering in `hooks.c`

## Outcome

Online play has no framework-owned fullscreen win, loss, draw, or game-over
state. A completed server-managed match returns to the normal online hub and
shows one compact notification in the bottom-right corner. The hub continues
pumping the control connection while the opponent report, final result, and
rating update arrive.

This notification does not replace the game's local-play round presentation.
It exists only for a committed server-managed online match. A failure before
synchronized gameplay begins returns to the hub without creating result UI or
changing Elo.

## Result admission and confirmation

Match protocol 3 keeps both clients pending until each reports a locally
restorable synchronized frame zero and the server broadcasts the exact-match
gameplay commit. Only that committed match may send or accept `match_end`,
`match_report_ack`, `match_result`, or rating data.

Normal completion requires both clients to name the same winner. Native detection reports
the synchronized winning player slot (0/1) alongside the compatibility `win`/`loss` word.
The server records its random host/join player assignment and maps that slot to an account;
an inverted local-player interpretation can therefore no longer turn one synchronized
winner into two contradictory account names. A lone report
that expires or two conflicting reports becomes a no-contest with no Elo
mutation. Fully authoritative server-side result validation remains a
production requirement.

Deliberately leaving a committed match is not an ordinary result guess. The
leaving client sends `match_abort`, which the server treats as a forfeit and
immediately resolves as a win for the connected opponent. If that opponent's
P2P teardown observer has already queued its own loss report, the server
replays the exact cached terminal result for that participant instead of
returning `invalid or stale match result`. The one-message replay cache is
cleared when a new match begins.

The client first creates a provisional toast after reporting its local result. Reporting
does not stop rollback or switch state: the native terminal countdown and win animation
continue normally. When Eggnogg itself switches from GAME to main, the hook lets that
native switch finish, then tears down transport and queues the hub. A server confirmation
or no-contest arriving during the animation updates retained result/status state but cannot
skip the presentation.
Its heading is `RESULT REPORTED`, not an unconfirmed win/loss claim. The exact
match ID remains retained until the asynchronous server answer arrives. A
confirmed `match_result` refreshes the toast with `YOU WON`, `YOU LOST`, or
`DRAW`, plus the final Elo and delta when supplied. An exact `match_abort`
clears the provisional record and notification.

TCP may deliver `match_started {committed:1}` and an immediate post-commit
disconnect/forfeit `match_result` in one receive batch. The client captures the
pending metadata, stops P2P, prepares the confirmed toast, and returns to the
hub without entering a server-deleted GAME state.

## Toast lifecycle

`OnlineResultToast.active` and `toast_visible` have separate meanings:

- `active` retains the exact match identity and result/rating metadata.
- `toast_visible` controls only drawing and hit testing.

Closing or timing out a provisional toast hides it without discarding the
match identity. Its later exact-ID confirmation is therefore still accepted
and makes the confirmed toast visible again. A confirmed record may be fully
retired when its refreshed toast is closed or expires. Losing the control
connection clears an unconfirmable provisional record.

The toast uses the shared responsive online panel primitives, includes
opponent and map labels, and has a mouse close button. Its hover cursor is the
same two-pass native cursor used by the hub and other online overlays.

## Navigation and requeue safety

After the native win sequence returns to main, completion queues the normal hub handoff and explicitly assigns the native
main menu as the hub's Back owner. It cannot return to an ended GAME state or
enter the former Result -> Hub -> Result loop because no result `GameState`
exists.

The usual Casual and Competitive queue rows remain the requeue path. Queue
requests are rejected locally while an exact result is still awaiting server
confirmation, with a clear hub status message. They become available as soon
as confirmation arrives. Prematch aborts and server disconnects use the same
single stable hub destination.

## Invariants

- `g_online_result_state` and its fullscreen input/render paths do not exist.
- Prematch/connect failures never manufacture a result notification.
- Only an exact-ID, server-committed match may report or accept a result.
- Normal reports use the synchronized winner slot and the server-owned slot/account map.
- A normal report/confirmation cannot stop the native win countdown or skip its animation.
- A committed deliberate exit is an explicit forfeit and awards the opponent.
- A late duplicate report can only replay that participant's exact most-recent
  terminal message; it cannot mutate ratings or another match.
- Hiding provisional UI never discards the identity needed for confirmation.
- An abort/no-contest clears any provisional local outcome.
- Requeue cannot occur before server confirmation.
- Completed-match Hub/Back resolves to a stable native main-menu owner.
- No result record copies or retains P2P authentication secrets.

## Verification

`python tests\online_flow_integration_static_test.py` guards toast-only routing,
terminal native winner gating, winner-slot publication, post-native-switch teardown,
committed-forfeit routing, exact-ID retention,
confirmation refresh, abort cleanup, requeue gating, and stable hub ownership.
`python tests\online_server_match_protocol_test.py` exercises the live control
socket, including a forfeit plus late P2P-loss race that must replay the same
win without a stale-result error and a deliberately inverted local result paired with
an agreed synchronized winner slot. `python tests\online_cursor_static_test.py` guards native
cursor layering and toast hit-area coverage. A MinGW syntax check covers the C
translation unit.

Manual acceptance still needs two clients covering casual/ranked win, loss,
draw, Elo delta, delayed opponent report, provisional close/expiry, requeue
before and after confirmation, server disconnect, and small/fullscreen layouts.
