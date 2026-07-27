# Online Connection, Prematch, and Menu-Time Simulation Runtime

**Date:** 2026-07-17  
**Status:** Implemented in the custom UDP rollback path; two-machine/NAT runtime verification remains required  
**Primary code:** online lifecycle in `hooks.c`, control service in
`online_server/server.js`, transport in `ggpo_net.c/.h`, rollback load in `lua_manager.c`

## Goals

This slice fixes five player-visible online problems:

- one failed NAT mapping no longer ends the connection attempt immediately;
- connection and deterministic setup happen behind the match-found countdown;
- the client never exposes a frozen first frame of gameplay while setup finishes;
- supported in-match menus keep the rollback simulation advancing; and
- physical window/viewport state stays local, and peer-local logical render dimensions
  are restored after authoritative state work instead of remaining overwritten by a
  peer/rollback snapshot.

It also makes both local control sets drive the client's one assigned online character.

This is still the project's custom IPv4 UDP rollback transport, not a production GGPO
library integration. IPv6 and relay fallback are separate work.

## Match lifecycle

The server-managed lifecycle is:

```text
match assignment
  -> pending match + gameplay-mod safety window
  -> socket/auth-only held P2P attempt(s) while hub countdown is visible
  -> selected map/content, seed, reset, and native start initialization in the hub
  -> exact mapgen-pinned map.lua identity/health postcondition
  -> transactional rollback-layout finalization + exact peer layout proof
  -> release the transport hold
  -> authoritative state + neutral frame-0 exchange
  -> local readiness proof + restorable frame zero
  -> report READY and wait for the server's two-client commit
  -> switch to GAME only after commit
  -> active rollback match
  -> result/disconnect cleanup and stable hub with compact result toast
```

The distinction between `pending` and `active` is an invariant. A connected socket is not
an active match. `g_online_connect.established` is set only after the synchronized state
has been prepared and the GAME switch succeeds.

The complete pending/setup window has a 45-second wall-clock timeout. Before the
server commits gameplay, setup failure or timeout sends `match_abort`, tears down
transport and mod suspension, records network diagnostics, and returns to the hub
with a recoverable status. It never manufactures a winner, result toast, or Elo
change for a match that did not start.

A package that declares `map.lua` is part of that fail-closed boundary. Native
reset first deactivates any stale VM. After the current map build/start, the
exact script id from the mapgen-pinned registry generation must be active and
non-faulted before rollback-layout finalization. A content-bind or activation
failure sends `match_abort`, prevents READY/layout publication, and preserves a
specific hub-visible reason. Scriptless/vanilla maps require no map VM. The
offline map hook keeps its native-visual fallback behavior.

## Fresh-socket P2P retry

P2P starts immediately when a valid match assignment is received.

- Maximum attempts: 3.
- A live, unconnected socket gets 8 seconds per attempt.
- A socket that could not start gets a short 250 ms rollover window.
- Attempt 1 honors the configured local port.
- Later attempts request port `0`, creating a fresh ephemeral socket/NAT mapping.
- Each retry republishes the new endpoint through the server probe path. The server
  selects exactly one symmetric route generation—loopback, LAN, or public—and both
  clients probe only that selected peer endpoint for the attempt.
- A retry closes the old socket without sending `BYE`, because the peer may have received
  its final HELLO and could otherwise reject a viable race.
- Host/join authority is never swapped. Those roles affect authoritative state sync, not
  the symmetric hole-punch.

The match card reports `attempt N/3`. After attempt 3, the client logs the detailed
`net.diag` report, cancels the uncommitted setup on the control server, and returns to the hub.
There is no automatic reconnect after gameplay is established; a live-match teardown
uses the normal disconnect/loss policy.

Retry depends on the matchmaking control connection being alive so the fresh endpoint
can be relayed. Losing that connection terminates the pending match instead of spending
attempts that cannot be published.

The server-selected nonempty `map_key` is authoritative and must resolve to the exact
installed manifest entry. If that key is unknown, changed since matchmaking, wrong-typed,
or too large, setup aborts before any socket/gameplay launch. Only a missing or explicitly
empty key may use the legacy numeric `map_sel` fallback.

## Friend-challenge map selection

Control protocol 3 makes the challenger choose from the authoritative shared-map
intersection before creating a challenge:

1. The client sends `challenge_maps` with the online friend's username and a monotonic
   request ID.
2. After rechecking login, friendship, and live presence, the server computes the full
   intersection of the two sanitized manifests. Manifests and picker responses are
   bounded to 4,096 unique entries.
3. The server streams a counted `challenge_maps_begin`, one flat
   `challenge_map_choice` per compatible key/label, and `challenge_maps_end`. Every line
   repeats the target and request ID.
4. The client accepts only its current target/request, requires both advertised counts
   to equal the exact number received, and exposes the resulting dynamic list as a
   cancellable left/right map row with Send and Cancel actions.
5. `challenge` carries the selected key. The server rejects any key outside the current
   intersection, stores the canonical key/label on the five-minute challenge, and shows
   that map to the recipient in both the Friends row and compact challenge toast.
6. Accept recomputes the intersection once more. Only the still-compatible stored key is
   forced into `makeMatch`, which supplies the two clients' distinct local selectors for
   that same map identity.

Queue matchmaking continues to choose a random shared map. Its availability probe uses
the non-mutating intersection helper, so merely scanning queue pairs no longer advances
the recent-map distribution history.

## Prematch hold protocol

The transport protocol is authenticated GGPO UDP version 17. Both peers must receive the
same server-generated 256-bit `p2p_auth_token` (exactly 64 hexadecimal characters) and
arm it through `ggpo_net_set_match_token` before each fresh-socket attempt. This shared
match secret is separate from the per-user `p2p_token`, which authorizes only the UDP
server discovery probe. Missing/malformed keys and wrong protocol versions fail closed.

A server-managed session starts with `prematch_hold=1` as a socket/auth-only transport.
Its rollback capacity is zero: no history slabs, initial-state buffer, correction buffer,
or constructor-time snapshot is allocated before the selected deterministic content has
finished initialization. While either peer is held:

- UDP receive/send, HELLOs, probes, and liveness continue;
- gameplay frame remains zero;
- constructor-time or previous-attempt state is quarantined;
- state/input exchange that could start gameplay is not accepted; and
- even an accidental `ggpo_net_advance` services transport only and reports no advance.

V16 introduced the non-cryptographic 32-bit `state_layout_id` by reusing the packed slot
formerly named `last_checksum`, which had no receiver consumer. V17 retains that field and
its structural meaning, but intentionally expands the ordinary packet with input
confirmation metadata; v16 and v17 are not wire compatible. The field reuse is not a
general old-DLL backward-compatibility promise. Server-managed prematch rejects mixed
binaries through the exact P2P version and immutable game/framework fingerprint before map
setup. The structural ID covers the versioned rollback-blob layout and final tilemap
dimensions/capacity, not map bytes. The exact server-selected `map_key` remains the map-
content identity, and a complete gameplay-mod/content/config compatibility manifest is
still required for production.

Every v17 peer datagram has an HMAC-SHA-256 tag truncated to 128 bits. Direction keys bind
the protocol version, sender role, and a 64-bit CSPRNG session ID. Tags are compared in
constant time before any source/session adoption or handler dispatch. An exact
4,096-sequence replay window rejects duplicates and stale traffic. Endpoint/session
migration within one session is limited to an authenticated HELLO at frame zero before
bidirectional confirmation. A confirmed prematch peer may adopt one authenticated fresh
session/endpoint only while still at frame zero and before the synchronized start state
is loaded; this is the bounded fresh-socket retry recovery path. Same-session packets
from alternate endpoints remain rejected after confirmation. This provides authentication
and integrity, not confidentiality—the UDP payload remains plaintext.

Once INPUT exchange is legal, each v17 ordinary packet carries a cumulative contiguous
input ACK and a 512-bit selective ACK for holes above it. The sender keeps the live edge in
the first of 64 input slots, then resends the oldest unacknowledged holes before its
newest-to-oldest defensive tail. The receiver keeps monotonic contiguous-remote and
peer-acknowledged-local horizons, refuses conflicting same-frame input, and will not retire
an unacknowledged input generation. The mutually confirmed checksum horizon is the minimum
of those two horizons and the last finalized simulated frame. Checksums are deferred while
rollback is pending and cached by exact remote frame until eligible. A generation-scoped
cumulative checksum ACK advances only after successful comparison; packets send the live
checksum edge and then retry from the peer's oldest unacknowledged frame. Prediction now
hard-stalls at the oldest unrecovered input, and history reuse hard-stalls until both peers
prove checksum comparison, with bounded checksum no-progress ending in disconnect. The
fixed packet is 1,292 bytes under a compile-time 1,400-byte ceiling. Receiver-side
start/correction blob validation and apply are transactional. V17 correction now freezes
both peers around a retained divergence pre-state, stages and acknowledges the full
snapshot plus exact mutual input span, replays on both peers, and releases only after a
matching transcript and bilateral commit/release handshake. The production serializer is
still the raw v9 layout; the fully typed native adapter remains a release blocker.

Hold changes are epoch-numbered and are illegal after gameplay begins. A new retry
reengages the hold, clears pending preparation and the prior peer layout proof, and
reannounces the finalized local layout for the fresh session so state or proof from an
older socket cannot be reused.

## Work performed during the countdown

The match-found countdown is 150 update frames (nominally 2.5 seconds at 60 Hz). It is a
presentation minimum, not permission to enter gameplay early.

Once the socket connects, still while the hub card is displayed, the client:

1. applies the exact selected map/content and synchronized RNG seed;
2. calls native `game_reset`;
3. starts native room/start-countdown state with synth output temporarily muted;
4. calls `ggpo_net_finalize_state_layout`, which measures the final versioned structural
   layout and transactionally allocates the 512 history slabs plus initial/correction
   transfer buffers;
5. waits until the peer proves the same 32-bit structural ID and exact byte capacity,
   aborting setup on a conflicting ID or capacity;
6. releases the transport hold;
7. lets the host capture this final post-reset state;
8. transfers/acknowledges that state to the joiner; and
9. exchanges deliberately neutral local and remote input for frame zero.

Finalization is idempotent only while the frozen layout remains identical. A second call
after any structural size/fingerprint change fails instead of silently reallocating an
active session. Allocation and the first capture are staged off-side, so a failure leaves
the session held with no partially installed rollback storage.

Release is intentionally asymmetric. The host recaptures one fresh authoritative state.
The joiner discards constructor/countdown state and waits for the host's newer state
epoch.

Cosmetic profile and asset transport is disabled in the current online protocol, so it
does not gate match readiness.

## Readiness proof and first visible frame

`ggpo_net_prematch_ready` returns true only when:

- local rollback storage is finalized and the peer has proved the exact structural layout
  ID and byte capacity;
- both hold flags are known released;
- the peer link is confirmed;
- authoritative state is synchronized and acknowledged;
- transport readiness caps are satisfied; and
- both local and remote frame-zero inputs exist and are zero.

When the countdown reaches zero before this proof, the player remains on the hub card.
Its status changes to Connecting, Preparing, or Synchronizing; the code does not switch
to GAME and freeze there.

At readiness, `ggpo_net_prepare_prematch_start` performs every fallible input-delay and
frame-zero operation before transactionally loading the synchronized state. That load
prevalidates the complete canonical blob, verifies its checksum and resulting live state,
and restores the exact prior raw state after a failed mutation. A verified exact restore
may be attempted at most three times in one synchronous call. A third safely restored
failure returns with the byte-exact old state and a retryable connected session; a later
explicit clean call may commit once. The final state load preserves authenticated
local/remote input rings and monotonic ACK horizons, including any newer prematch evidence,
rather than clearing them with rollback history. A direct postcondition requires
`start_state_loaded` before remote progress is published or the final input packet is sent;
an impossible ready-without-loaded race fails closed. Only then does it send one final input
packet. The client sends `match_started` and remains in the hub. The
server records READY idempotently and broadcasts `match_started {committed:1}` only after
both assigned clients are ready. That exact current-match commit is the final permission
to switch to GAME. The next visible gameplay tick has a remote input and can advance from
frame 0 to frame 1 immediately.

The loaded frame-zero state closes the transport retry window. Therefore any P2P service
failure after local start preparation or READY aborts the setup; it cannot open a fresh
socket and reuse stale readiness. A failure before start preparation may retry, but must
clear every preparation, release, start, READY, and commit latch first.

After commit, match duration is not governed by the pending-setup TTL. The authenticated
control client sends a 30-second heartbeat to satisfy the server's 120-second receive-idle
timeout, so a quiet committed game is resolved only by result/disconnect handling rather
than an arbitrary ten-minute setup sweep or two-minute control idle.

The commit and a post-commit forfeit result may share one TCP receive batch. If
that happens before the launch pump promotes pending metadata, the client
resolves the exact committed pending match directly and never enters a GAME the
server has already retired. Normal completion requires two reports naming the
same winner; a lone-report timeout or conflict is a no-contest with no Elo
mutation.

A deliberate local exit after commit sends `match_abort`, not an ordinary
locally inferred loss. The server resolves that message as an immediate forfeit
win for the still-connected opponent. If the opponent's P2P observer races a
late loss report after resolution, the server replays that participant's exact
terminal result rather than rejecting it as stale.

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
- options;
- player-one input remapping;
- player-two input remapping;
- framework console;
- Mods menu; and
- Mods entry state.

Both native input-remapping states share `menu_common_update`, so they pass through the
same detoured button-update owner as Options and receive exactly one online advance after
their native menu update. They must not have a second remap-specific tick path. The
console has its own update path because it does not pass through the native menu-button
updater. GAME itself remains the normal owner of gameplay ticks, preventing double
advancement.

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

## Local rendering and authoritative simulation dimensions

Native `adjust_layout` derives `game_w` and `game_h` from each machine's window, but both
fields also enter native update. Width changes respawn/camera branches; height changes
vertical camera and camera-relative ambient paths. Rollback canonicalization v6 therefore
checksums and restores both deterministic dimensions while keeping the physical viewport
and between-tick logical render dimensions local.

The net loop keeps a clean post-tick simulation copy and restores it before every pre-state
capture and native tick. Separately, it captures each client's local logical dimensions
before received-state application or simulation and restores them after the authoritative
work. Thus peers simulate identical dimensions without making the host's aspect ratio the
join client's persistent render layout. Physical window/drawable/GL viewport ownership
remains local.

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
- GAME entry requires the server's exact-match two-client start commit.
- A pre-commit abort, disconnect, stale timeout, or premature result is a no-contest.
- A delayed lifecycle message with another match ID cannot affect the current match.
- A batched commit plus immediate result cannot be discarded or enter stale gameplay.
- Unilateral and conflicting normal result reports cannot select a winner or change Elo.
- Match input during the countdown is always neutral.
- Setup/connection failure releases mod suspension and pending state.
- Active match completion/disconnect stops netcode, captures result metadata, and ends the
  safety window.
- Physical window/drawable/viewport and between-tick render geometry are local-only;
  synchronized logical simulation dimensions are restored only for capture/ticks. The host
  owns initial-state transfer and the current
  correction/recovery path; during normal play both peers run the same deterministic
  rollback simulation from exchanged inputs rather than treating one peer as gameplay
  truth.

## Verification

Automated coverage:

- `tests/prematch_hooks_static_test.py` enforces ordering from match assignment through
  zero-capacity held service, native map/reset/start preparation, transactional layout
  finalization, readiness, GAME switch, and the defensive GAME gate.
- `tests/prematch_net_test.c` plus `tests/prematch_net_test.py` run paired host/join
  processes over loopback. They verify zero-capacity held sessions, differing bootstrap
  sizes converging on one shared final size (including tail bytes), matching and mismatched
  structural schema/capacity proofs, idempotent finalization and changed-layout rejection,
  guarded release, retry proof reset/reannouncement, delayed release, host-state authority,
  neutral frame zero, an exact three-attempt safely restored joiner load failure followed by
  one explicit clean retry, ACK-horizon preservation, and an immediately advancing first
  visible tick. Its deterministic frame-ring preflight additionally covers the 512-bit
  SACK edge, monotonic cumulative folding and uint32 wrap, immutable same-frame input,
  live-edge/oldest-hole resend order, input/history overwrite guards, and checksum-horizon
  deferral during rollback. Its paired correction mode injects a one-sided divergence and
  proves matching correction ID, snapshot/resume horizon, and replay transcript under loss,
  delay/reordering, and authenticated duplicate replay before both checksum directions drain.

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
- Reconnect/resume after an established-match disconnect.
- A production GGPO library transport.
- Cosmetic/color synchronization.
- Physical multi-monitor/DPI acceptance for the implemented F1/F11 and launch-mode
  window policy; see `2026-07-17-window-mode-launch-design.md`.
