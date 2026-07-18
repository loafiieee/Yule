# Online Connection, Prematch, and Menu-Time Simulation Runtime

**Date:** 2026-07-17  
**Status:** Implemented in the custom UDP rollback path; two-machine/NAT runtime verification remains required  
**Primary code:** online lifecycle in `hooks.c`, transport in `ggpo_net.c/.h`, rollback load in `lua_manager.c`

## Goals

This slice fixes five player-visible online problems:

- one failed NAT mapping no longer ends the connection attempt immediately;
- connection and deterministic setup happen behind the match-found countdown;
- the client never exposes a frozen first frame of gameplay while setup finishes;
- supported in-match menus keep the rollback simulation advancing; and
- local window geometry is not overwritten by a peer/rollback snapshot.

It also makes both local control sets drive the client's one assigned online character.

This is still the project's custom IPv4 UDP rollback transport, not a production GGPO
library integration. IPv6 and relay fallback are separate work.

## Match lifecycle

The server-managed lifecycle is:

```text
match assignment
  -> pending match + gameplay-mod safety window
  -> held P2P attempt(s) while hub countdown is visible
  -> native map/reset initialization in the hub
  -> authoritative state + neutral frame-0 exchange
  -> readiness proof
  -> switch to GAME as the final operation
  -> active rollback match
  -> result/disconnect cleanup and hub/result screen
```

The distinction between `pending` and `active` is an invariant. A connected socket is not
an active match. `g_online_connect.established` is set only after the synchronized state
has been prepared and the GAME switch succeeds.

The complete pending/setup window has a 45-second wall-clock timeout. Setup failure or
timeout sends a loss result when possible, tears down transport and mod suspension,
records network diagnostics, and returns to the hub with a recoverable status.

## Fresh-socket P2P retry

P2P starts immediately when a valid match assignment is received.

- Maximum attempts: 3.
- A live, unconnected socket gets 8 seconds per attempt.
- A socket that could not start gets a short 250 ms rollover window.
- Attempt 1 honors the configured local port.
- Later attempts request port `0`, creating a fresh ephemeral socket/NAT mapping.
- Each retry republishes the new endpoint through the server probe path and reapplies
  direct, public, and LAN peer candidates.
- A retry closes the old socket without sending `BYE`, because the peer may have received
  its final HELLO and could otherwise reject a viable race.
- Host/join authority is never swapped. Those roles affect authoritative state sync, not
  the symmetric hole-punch.

The match card reports `attempt N/3`. After attempt 3, the client logs the detailed
`net.diag` report, reports a loss/release to the control server, and returns to the hub.
There is no automatic reconnect after gameplay is established; a live-match teardown
uses the normal disconnect/loss policy.

Retry depends on the matchmaking control connection being alive so the fresh endpoint
can be relayed. Losing that connection terminates the pending match instead of spending
attempts that cannot be published.

The server-selected nonempty `map_key` is authoritative and must resolve to the exact
installed manifest entry. If that key is unknown, changed since matchmaking, wrong-typed,
or too large, setup aborts before any socket/gameplay launch. Only a missing or explicitly
empty key may use the legacy numeric `map_sel` fallback.

## Prematch hold protocol

The transport protocol is authenticated GGPO UDP version 16. Both peers must receive the
same server-generated 256-bit `p2p_auth_token` (exactly 64 hexadecimal characters) and
arm it through `ggpo_net_set_match_token` before each fresh-socket attempt. This shared
match secret is separate from the per-user `p2p_token`, which authorizes only the UDP
server discovery probe. Missing/malformed keys and wrong protocol versions fail closed.

A session starts with `prematch_hold=1` before peer candidates are serviced. While either
peer is held:

- UDP receive/send, HELLOs, probes, and liveness continue;
- gameplay frame remains zero;
- constructor-time or previous-attempt state is quarantined;
- state/input exchange that could start gameplay is not accepted; and
- even an accidental `ggpo_net_advance` services transport only and reports no advance.

Every v16 peer datagram has an HMAC-SHA-256 tag truncated to 128 bits. Direction keys bind
the protocol version, sender role, and a 64-bit CSPRNG session ID. Tags are compared in
constant time before any source/session adoption or handler dispatch. An exact
4,096-sequence replay window rejects duplicates and stale traffic. Endpoint/session
migration is limited to an authenticated HELLO at frame zero before bidirectional
confirmation; afterward the peer endpoint is pinned. This provides authentication and
integrity, not confidentiality—the UDP payload remains plaintext.

Hold changes are epoch-numbered and are illegal after gameplay begins. A new retry
reengages the hold and clears pending preparation so state from an older socket cannot
be reused.

## Work performed during the countdown

The match-found countdown is 150 update frames (nominally 2.5 seconds at 60 Hz). It is a
presentation minimum, not permission to enter gameplay early.

Once the socket connects, still while the hub card is displayed, the client:

1. applies the selected map and synchronized RNG seed;
2. calls native `game_reset`;
3. starts native room/start-countdown state with synth output temporarily muted;
4. releases the transport hold;
5. lets the host capture this final post-reset state;
6. transfers/acknowledges that state to the joiner; and
7. exchanges deliberately neutral local and remote input for frame zero.

Release is intentionally asymmetric. The host recaptures one fresh authoritative state.
The joiner discards constructor/countdown state and waits for the host's newer state
epoch.

Cosmetic profile and asset transport is disabled in the current online protocol, so it
does not gate match readiness.

## Readiness proof and first visible frame

`ggpo_net_prematch_ready` returns true only when:

- both hold flags are known released;
- the peer link is confirmed;
- authoritative state is synchronized and acknowledged;
- transport readiness caps are satisfied; and
- both local and remote frame-zero inputs exist and are zero.

When the countdown reaches zero before this proof, the player remains on the hub card.
Its status changes to Connecting, Preparing, or Synchronizing; the code does not switch
to GAME and freeze there.

At readiness, `ggpo_net_prepare_prematch_start` loads the synchronized state, restores
only the two already-proven neutral frame-zero inputs cleared by that load, and sends one
final input packet. Switching to GAME is then the final operation. The next visible
gameplay tick has a remote input and can advance from frame 0 to frame 1 immediately.

There are two defensive backstops:

- `hooked_game_update` refuses native simulation if a pending match somehow reaches GAME;
- `ggpo_net_advance` cannot advance while either prematch hold is active.

## Simulation while menus are open

An online match cannot pause on only one peer. When an active match is behind a supported
overlay, the framework services the control connection and advances exactly one rollback
tick from that overlay's update path. Catch-up may run up to four internal steps when the
net layer reports it is behind.

Supported in-match states are:

- paused options;
- options/controls;
- framework console;
- Mods menu; and
- Mods entry state.

The console has its own update path because it does not pass through the native
menu-button updater. GAME itself remains the normal owner of gameplay ticks, preventing
double advancement.

While one of these menus is open, the local player's submitted command is forced to zero
so menu navigation cannot move the fighter. Remote simulation and network service keep
running. Leaving the actual match state for an unsupported screen still triggers the
existing abandon grace/loss behavior.

## Dual local control sets

Before each online advance, raw commands from local player-control sets 0 and 1 are ORed
together into the slot assigned to this client. The unassigned slot is still sourced only
from the peer through rollback input exchange.

This means keyboard/controller bindings configured for either local player can control
the user's fighter online, regardless of whether the server assigned that client player
0 or player 1. It does not grant local control of the opponent.

## Local viewport preservation

Rollback/full-state blobs retain `game_w` and `game_h` for ordinary save/load
compatibility, but those values are derived from each machine's current window and
drawable. `lua_manager_game_state_load_rollback` now snapshots the local values, applies
the rollback blob, then restores the local values before simulation resumes.

This prevents a host's resolution—or an old snapshot captured before a local resize—from
shifting/cropping another client's online gameplay UI.

The online path also logs a geometry snapshot at match start, on each detected change,
and at match end. It includes window/drawable dimensions, GL viewport and scissor,
logical `mad`/game sizes, camera, fullscreen state, native state, match/player identity,
and local/remote frames. The logging is diagnostic; it does not invent a new viewport
calculation.

## Failure and cleanup invariants

- No retry may reuse a prepared state or input from a prior socket.
- A countdown reaching zero is not itself readiness.
- Pending setup never advances a gameplay frame.
- A failed state load aborts before GAME is shown.
- Match input during the countdown is always neutral.
- Setup/connection failure releases mod suspension and pending state.
- Active match completion/disconnect stops netcode, captures result state, and ends the
  safety window.
- Window geometry is local-only. The host owns initial-state transfer and the current
  correction/recovery path; during normal play both peers run the same deterministic
  rollback simulation from exchanged inputs rather than treating one peer as gameplay
  truth.

## Verification

Automated coverage:

- `tests/prematch_hooks_static_test.py` enforces ordering from match assignment through
  held service, native preparation, readiness, GAME switch, and the defensive GAME gate.
- `tests/prematch_net_test.c` plus `tests/prematch_net_test.py` run paired host/join
  processes over loopback. They verify held sessions do not advance, delayed release,
  host-state authority, neutral frame zero, an injected joiner load failure, and an
  immediately advancing first visible tick.

Manual verification still required:

- two normal online clients through the real hub flow;
- countdown shorter and longer than connection/state transfer;
- forced attempt rollover with a changed local UDP port;
- home NAT, shared-LAN/public-address path, packet loss, and high latency;
- opening each supported menu for an extended period while the peer keeps playing;
- both control sets when locally assigned player 0 and player 1; and
- different window sizes, fullscreen modes, DPI scaling, and a resize during rollback.

## Out of scope

- Relay fallback for symmetric NAT/CGNAT.
- IPv6 sockets and candidate exchange.
- A player-facing map picker for friend challenges. The implemented flow advertises each
  client's map manifest and accepts the server-selected shared `map_key`; it does not let
  the challenger choose from that intersection yet.
- Reconnect/resume after an established-match disconnect.
- A production GGPO library transport.
- Cosmetic/color synchronization.
- Physical multi-monitor/DPI acceptance for the implemented F1/F11 and launch-mode
  window policy; see `2026-07-17-window-mode-launch-design.md`.
