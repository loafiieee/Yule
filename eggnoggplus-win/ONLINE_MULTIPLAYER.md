# Online Multiplayer / Rollback Roadmap

This document explains how the current online multiplayer work is structured and what still needs to be added before it is a real GGPO-backed online mode.

## Current Baseline

The game currently has a working GGPO-style rollback prototype:

- rollback-oriented save/load/checksum prototype
- rollback state capture and restore
- input injection for player 0 and player 1
- local rollback probes
- callback-shaped local session
- custom UDP host/join test session
- host-owned initial state sync
- input prediction and rollback/replay
- checksum-based desync detection

The custom UDP session is a prototype harness. It proves the game can run with rollback semantics, but it is not the final networking layer. The final implementation should wire the existing rollback callbacks into real GGPO.

## Important Files

- `ggpo_ext.c` / `ggpo_ext.h`
  - Shared rollback API used by tests, loopback, local session, and net session.
  - Saves/loads game state through `lua_manager`.
  - Advances exactly one game tick with injected player inputs.
  - Computes rollback checksums.

- `lua_manager.c` / `lua_manager.h`
  - Owns the broad rollback-state serialization prototype.
  - Captures native globals, player structs, thing arrays, tilemap data, room state, particle state, and transient game state.
  - Applies rollback snapshots back into the native game.
  - Canonicalizes process-local or non-gameplay values before checksum comparison.

- `ggpo_loopback.c` / `ggpo_loopback.h`
  - F3 live rollback verifier.
  - Saves history, replays recent frames, and checks that replayed state matches original state.

- `ggpo_local.c` / `ggpo_local.h`
  - F4 callback-shaped local session.
  - Stand-in for the callbacks real GGPO will use: begin, save, load, free, advance.

- `ggpo_net.c` / `ggpo_net.h`
  - F6/F7 custom UDP peer prototype.
  - Handles host/join, host state sync, input exchange, prediction, rollback, and checksum exchange.

- `hooks.c`
  - Hotkeys, console commands, built-in online hub, tick hook, raw input override, and per-frame routing into loopback/local/net sessions.

## Hotkeys And Commands

F1/F11 retain framework window controls, while F9/F10/F12 remain base-game
development controls. The framework's rollback diagnostics use F2–F7 below.

- F2: `ggpo.roundtrip`
  - Saves current state, reloads it, and checks that the rollback checksum survives the roundtrip.

- F3: `ggpo.loopback`
  - Toggles live loopback rollback verification.

- F4: `ggpo.local`
  - Toggles callback-shaped local session.

- F6: `ggpo.net host`
  - Starts UDP host on port `47777` as player 0 after `ggpo.net key` arms a shared key.

- F7: `ggpo.net join 127.0.0.1 47777`
  - Joins localhost host as player 1 after `ggpo.net key` arms the same shared key.

Console commands:

- `online.hub`
- `ggpo.roundtrip`
- `ggpo.selftest [frames]`
- `ggpo.loopback`
- `ggpo.local`
- `ggpo.net key` / `ggpo.net key clear`
- `ggpo.net host [port]`
- `ggpo.net join <host> [port] [local_port]`
- `ggpo.net hud [on|off]`
- `ggpo.net delay [frames]`
- `ggpo.net advantage [frames]`
- `ggpo.net predict [frames]`
- `ggpo.net highping [frames]`
- `ggpo.net smoothping [frames]`
- `ggpo.net correction [on|off]`
- `ggpo.net sim [loss_pct] [min_delay] [max_delay]`
- `ggpo.net rngtrace [on|off]`
- `ggpo.net off`
- `ggpo.net status`
- `online.troubleshoot` (alias: `net.trouble`) - nonblocking active TCP/UDP and
  Windows adapter/VPN/NAT/CGNAT evidence test
- `net.diag` - current secret-free match/route/packet snapshot

## Rollback Model

The rollback model is intentionally close to GGPO:

1. Reuse the retained canonical boundary snapshot before a frame.
2. Feed deterministic inputs for player 0 and player 1.
3. Run exactly one native game tick.
4. Capture the resulting state once as the next boundary; derive its checksum and optional
   component diagnostics from the same checksum-canonical scratch blob.
5. If remote input arrives late and differs from prediction:
   - load the saved pre-frame state
   - replay frames with corrected inputs
   - update frame history and checksum state

`ggpo_ext_advance_frame()` blocks normal raw input, injects per-player input for the tick, runs one simulation tick, clears the input override, and optionally computes the rollback checksum.

Rollback replay can suppress one-shot native sound effects so replays do not spam audio. Live frames should not be globally muted.

## Determinism Rules

For rollback to work, every gameplay-affecting value must be either:

- fully deterministic from prior state and inputs, or
- captured/restored in rollback state, or
- canonicalized out only if it truly cannot affect gameplay.

Values that have already mattered:

- RNG seed
- native game tick counter
- player structs
- active room / old room / room lerp state
- camera X/Y plus shake amplitude/decay
- authoritative simulation `_game_w` / `_game_h`; online capture/ticks temporarily pin
  the synchronized values, then restore each client's local logical dimensions for render
- thing count and thing array
- tilemap dimensions and data pointer contents
- deterministic V2 `map.lua` state, RNG, clock, object lifecycle generations,
  contact history, sprite overrides, script identity, and fault state
- transient game state
- leader/loser/controller/player pointer fields
- waterfall/effect pointers if they leak into checksum or simulation

Do not fix desyncs by blindly zeroing fields. First decide whether the field is:

- gameplay state that must be saved/restored
- process-local pointer state that must be remapped after load
- render/audio-only state that can be excluded from rollback checksums

## What Real GGPO Needs

The existing code has callback-shaped rollback scaffolding plus v17 reliable input and
checksum confirmation with hard recoverability boundaries, a coordinated correction
barrier, and canonical native-tick floating-point controls. Production still needs the
fully typed native state adapter, the remaining checksum-field audit, and the long seeded
chaos/native-soak contract. After those pass, the custom UDP harness can be replaced or
bypassed with GGPO sessions and callbacks.

Required GGPO callback mapping:

- `begin_game`
  - Initialize an online match and lock the chosen rules/map/player slots.

- `save_game_state`
  - Call the existing rollback save path and return the serialized state buffer plus checksum.

- `load_game_state`
  - Call the existing rollback load path.

- `free_buffer`
  - Free state buffers allocated by `save_game_state`.

- `advance_frame`
  - Poll GGPO synchronized inputs, inject player commands, advance one native tick, and report checksum.

- `on_event`
  - Handle connected, synchronizing, synchronized, running, interrupted, resumed, disconnected, and timesync events.

Implementation tasks:

- Add GGPO headers/library to the build.
- Create a `ggpo_session.c` layer separate from the current `ggpo_net.c` prototype.
- Keep `ggpo_ext.c` as the game-side callback implementation.
- Translate local input bitmasks into GGPO inputs each frame.
- Translate GGPO synchronized inputs back into the current `GgpoFrameInputs`.
- Use GGPO disconnect/time-sync advice instead of the custom frame-advantage throttle.
- Keep checksum logging for desync diagnostics.

## Match Start And State Sync

The final online flow should not depend on two already-running game instances being in similar states.

Required match-start data:

- game version/build hash
- framework version
- GGPO protocol version
- map id and map checksum
- ruleset/mode
- score target
- RNG seed
- starting room
- spawn positions
- player assignment
- enabled/disabled mod policy

Recommended flow:

1. Player logs in before entering online features.
2. Player enters a competitive/casual queue or accepts a friend challenge.
3. Backend matches two players and creates a signed match config.
4. Backend chooses player slots, seed, map/rules, and transport route.
5. Both clients verify version, assets, map, rules, and mod policy.
6. Both clients enter the match through the same deterministic start path.
7. GGPO begins at frame 0 after both clients are synchronized.

Host full-state sync can remain as a debug or recovery tool, but production online should prefer deterministic match initialization over transferring arbitrary live state.

## Chosen Production Direction

The finished frontend should not expose a host/join menu. Players should see an online hub, queue buttons, account status, friends, and challenges. The server decides who gets matched and how the match connects.

Use a hybrid transport model:

- backend for accounts, sessions, queues, hidden MMR, public Elo, matchmaking, match configs, results, and friend systems
- direct P2P UDP gameplay when both clients can connect directly and the route is good
- relay gameplay fallback when direct P2P fails, when NAT is strict, or when hiding player IPs is required
- no fully authoritative gameplay server for the first production online version

In normal gameplay there should not be a gameplay-authoritative host. Both clients run deterministic rollback simulation. The server can still choose a session owner, P0/P1 assignment, or direct/relay route, but gameplay truth comes from deterministic inputs and checksum validation.

Recommended backend stack:

- Go service for the first backend because it can handle HTTP/WebSocket control traffic and UDP relay/signaling in one deployable binary.
- PostgreSQL for accounts, player profile data, MMR, match history, cosmetics ownership, and friend data.
- In-memory queue state at first, with Redis later if queues need to span multiple backend instances.
- HTTPS REST for account/profile operations.
- WebSocket for live queue, matchmaking, match found, ready checks, and post-match result flow.
- UDP sockets for gameplay relay and connectivity probes.

This keeps the client simple, gives enough performance headroom for a relay, and avoids committing to a heavyweight service layout too early.

## Online Hub / Server Status

The built-in online hub is back in the framework and can be opened from the main-menu `ONLINE` button or the `online.hub` console command. It is translated from the old Lua hub into C-side framework UI and currently has three tabs:

- Play: account login/register with a compact opt-in `Remember me` checkbox backed by Windows Credential Manager, Casual Queue, Competitive Queue, queue leave, and server-driven match launch.
- Friends: username entry for adding friends, incoming friend requests, incoming 5-minute
  challenges with their selected map, friend list, accept/decline actions, and a
  left/right picker containing the exact compatible-map intersection before a challenge
  is sent. A keyboard/controller/mouse social menu exposes persistent mute, block,
  unblock, and unfriend controls.
- Settings: server address as a single `host:port` field, the local P2P UDP port (`Auto` by default), and challenge notifications.
- Online result: after a normally completed online match, both clients report win/loss to the server. Only two reports naming the same winner produce a confirmed result and Elo update; a lone or conflicting report becomes a no-contest. Deliberately leaving a committed match sends an explicit forfeit, immediately awarding the connected opponent the win. A late P2P-loss report racing that forfeit replays the same exact terminal result instead of producing a stale-result error. Native GAME keeps ownership through its win countdown; only its own return to main opens the hub and compact bottom-right notification. There is no fullscreen result state. Requeue becomes available after confirmation, while prematch/connect failures create no result notification.

### Launch arguments and `yule://` links

The installer can register a per-user `yule://` handler. These public links are suitable
for LFG messages:

```text
yule://hub
yule://requests
yule://queue/casual
yule://queue/competitive
yule://challenge/player_1
```

Equivalent command-line switches are `--online`, `--requests`, `--queue=casual`,
`--queue=competitive`, and `--challenge=player_1`. Challenge targets must be canonical
lowercase account names. Windows may hand an authority-only link to the application with
one terminal slash, such as `yule://hub/`; this canonical form is accepted without making
unknown, doubled-slash, encoded, query, or fragment routes valid.

Browser and shell protocol launches inherit the caller's working directory. The framework
normalizes it to the directory containing `eggnoggplus.exe` before relative game data,
logs, crash dumps, or dependent DLLs are accessed. Consequently all entry points use the
installed `data/` and `mods/` trees, and an early protocol-launch failure is reported in
the installed `mods\crash.log` rather than the browser's directory.

Only protocol activations are single-instance. The first game process owns a private
per-login-session Windows message broker. A later valid `yule://` activation forwards its
already-parsed bounded intent to that process, permits it to come to the foreground, and
exits before SDL video initialization creates another game window. Starting the executable
normally still permits two local instances for testing. A link received during match setup
or gameplay remains pending and cannot force the player out of the match.

Queue, inbox, and challenge actions wait for authentication; a remembered sign-in proceeds
directly, while another login can be completed in the hub. The custom hub continues
pumping the original launch request after the native menu has handed off to it, so a
successful manual or remembered login immediately resumes the requested inbox, queue, or
challenge action instead of dropping the user on the default Play page. Pending actions
expire after two minutes and cancel when the hub is closed. Challenge links still require
an available friend and open the normal compatible-map picker.

Links cannot carry passwords, server/peer addresses, match IDs, or tokens, and there is no
direct-session join route. Unknown routes, query/fragment syntax, percent encoding,
userinfo, conflicts, and malformed targets reject the complete online action.

The default control server is `eggnogg.loafiieee.com:47778`, persisted in
`mods\online_hub.cfg`. The prototype server lives in `online_server/server.js` and handles
login/registration, public Elo, hidden server-side MMR, casual queue, competitive
MMR-range queue, friends, friend requests, authoritative friend-challenge map selection,
5-minute challenges, persistent private block/mute state, bilateral private rematches,
shared-map selection, and P2P match setup.

Online > Settings includes a default-off **Match Network HUD** toggle, persisted as
`match_hud=0|1`. During an active gameplay/paused-Options state it draws one small
top-right line containing smoothed round-trip ping, local input delay, and cumulative
rollback count. `ggpo.net hud [on|off]` changes the same setting. The ping converts the
current service-tick RTT estimate at the game's approximately 60 Hz cadence; it is a
basic indicator, not sequence-aware jitter/loss telemetry.

The current control channel is bounded newline-delimited JSON over nonblocking raw TCP.
TCP connect and login response each have independent 10-second wall-clock deadlines.
After authentication, a 30-second JSON ping/pong heartbeat keeps the server's
120-second receive-idle timeout from disconnecting a quiet hub or long match.
Incoming lines are capped at 8,191 bytes and parsed as complete flat JSON objects; nested
values, duplicate/escaped-alias keys, malformed UTF-8/escapes, integer overflow, embedded
NUL, and truncated destination values are rejected. Protocol/framing failure disconnects
and clears a pending authentication secret. Because this channel still has no TLS or
certificate validation, it does not protect credentials or match tokens from an on-path
attacker.

After TCP connects, the client requests and validates the server's flat `server_info`
advertisement before it sends the login/register request. Control protocol 3, match
protocol 3, P2P protocol 17, packet-auth capability, social-controls capability, and
private-rematch, UDP-relay, and client-build-gate capabilities are all required. `auth_ok`
repeats the same fields and `match_found` repeats the match/P2P versions, so a stale or
mixed deployment fails at the handshake (and again at match setup as defense in depth)
instead of consuming a queue match that cannot start.

After authentication, every map manifest also identifies the sending client with its
framework label, exact control/match/P2P protocol tuple, and deterministic
build/game-executable/framework-DLL fingerprint tuple. The server stores and logs those
values without secrets. It admits only control-v3/match-v3/P2P-v17 clients with complete
build identities, and pairs only clients whose exact build tuples also match. Queue,
challenge, rematch, and final match creation all use the same check. `match_found` carries
the checked match/P2P versions and bounded opponent build diagnostics rather than blindly
substituting the server's own constants. A client with missing or older fields is told to
update before P2P setup; two supported-protocol clients with different binaries receive a
clear incompatibility error and are not consumed as a match.

P2P v16 and v17 are deliberately not wire-compatible. V17 changed the authenticated
direction-key domain and the fixed input/ACK/correction packet layout, so changing a
version number or accepting the older packet prefix would not be safe compatibility.
Both players must run a build using the server's advertised tuple. The human-facing
release label is diagnostic only; safe pairing requires both the numeric protocol tuple
and exact build fingerprint tuple.

The current client requires control v3/match v3/P2P v17 plus relay and client-build-gate
capabilities. Deploy and restart this matching server revision before launching the
rebuilt client, then require the TCP/UDP deployment preflight to report both build gating
and UDP relay support.

Gameplay uses end-to-end authenticated `ggpo_net` packets over a direct route when
possible and the bounded server relay after a failed direct generation. The server chooses the map from
the intersection of both complete client manifests (capped at 96 KiB), sends
each client the stable key and that client's local selector, and randomly assigns
host/join only for player slot and authoritative initial-state duties. Both peers
publish a fresh observed endpoint on every attempt. The server selects one
symmetric route generation (loopback for two clients on one machine, LAN when
appropriate, otherwise public), and each client sends authenticated HELLOs only
to the selected peer endpoint for that attempt. This prevents the peers from
pinning different simultaneously advertised paths; swapping host/join cannot
improve NAT traversal. Every
nonempty server `map_key` must resolve to the installed package signature (including
external asset and optional `map.lua` bytes), and different
game/framework fingerprints abort server-managed prematch before native setup.
Connection attempts, native match initialization, rollback-layout finalization, state
transfer, and neutral frame-zero input exchange run behind the match-found countdown.
The P2P session begins held with zero rollback capacity. Only after the exact map/content,
the exact mapgen-pinned script is active and non-faulted (or the package declares none),
the synchronized seed, native reset, and native start are installed does it transactionally
allocate history/transfer storage and require the peer's matching 32-bit structural layout
ID plus exact capacity. Missing proof waits; mismatch aborts before hold release. The
client stays in the hub if that work outlasts the timer. Once frame zero is locally restorable,
it reports READY and still waits until the server has received READY from both
clients and broadcasts the gameplay-start commit. Pre-commit failure is an
explicit no-contest setup abort with no win/loss screen or Elo change. Setup has
a bounded timeout and never exposes a frozen GAME frame. Generic cosmetic profile/asset
packets are compile-disabled in `ggpo_net`. The only presentation preference exchanged is
the fixed-size player palette tuple described below.

GGPO UDP v17 authenticates every peer datagram. The server supplies both peers one
identical 256-bit `p2p_auth_token` as exactly 64 hexadecimal characters; the older
per-user `p2p_token` remains only server-probe authorization. `ggpo_net_set_match_token`
decodes and domain-separates the match secret. Each packet uses HMAC-SHA-256 truncated to
128 bits, with a direction key binding v17, sender role, and a 64-bit CSPRNG session ID.
The receiver verifies the role and tag in constant time before source/session adoption,
then applies an exact 4,096-packet replay window. Endpoint or session migration is
permitted only through an authenticated HELLO at frame zero before confirmation. Missing
keys, wrong keys, and wrong versions fail closed. This protects P2P integrity and
authenticity only; packet contents and traffic metadata are not encrypted.

### Secure password remembering

`Remember me` is opt-in and disabled by default. The feature adds only the
username and `remember_me=0|1` as credential lookup metadata; other non-secret
connection/UI settings also remain in the config, but it never contains the password. After a
successful login, the password is stored as a per-user Windows generic
credential scoped to the server host, port, and normalized account name. Turning
the option off deletes that credential, and a saved password rejected by the
server is removed rather than retried forever. Authentication and text-entry
buffers are securely cleared as soon as they are no longer needed. Passwords are
never written to framework logs. Empty, malformed, embedded-NUL, or oversized records
are rejected and deleted from their exact Credential Manager target; cleanup failure is
reported rather than described as success. `auth_ok` must return a canonical lowercase
`[a-z0-9_]{1,24}` account before the credential may be stored.

When an exact remembered credential is available, opening Online immediately begins
authentication. The credential form is skipped while that request is pending and
`auth_ok` enters the normal Play/Friends/Settings menus directly. If connection or
authentication fails, the secret is cleared and the ordinary login form returns for
manual recovery. Player-facing copy deliberately describes only `Remember me`; storage
backend and cleanup details remain in non-secret diagnostics rather than the login UI.

F6/F7 `ggpo.net` host/join and F2/F3/F4 rollback tools remain debug tools, not
the player-facing online flow. Direct v17 host/join requires both developers to
copy the same 64-hex secret and run `ggpo.net key` first. That command clears the
clipboard after deriving a one-shot in-memory key, so the secret never enters
console history or logs.

## Latency Tuning

The debug UDP harness has two high-latency profiles. These are diagnostic controls, not a
claim that the current rollback transport is reliable:

- `ggpo.net highping [frames]`
  - Conservative profile.
  - Uses the supplied latency budget to pick lower frame-advantage and prediction caps.
  - Intended to trade smoothness for fewer desync corrections and shorter hard pauses.
- `ggpo.net smoothping [frames]`
  - Aggressive profile.
  - Sets frame-advantage and prediction caps directly to the supplied budget.
  - Intended for testing smooth long prediction, but it can create large rollbacks/corrections under interactive hazards.

For ugly links, prefer `ggpo.net highping 140` over `ggpo.net smoothping 140`. The conservative profile should feel more delayed or stuttery, but it should avoid the worst long freeze-and-correct cycles.

Correction is now a bilateral barrier, not a short unilateral wait:

- both peers freeze gameplay while authenticated input delivery and selective ACKs continue;
- the host offers the retained pre-state of the first divergent frame plus one exact mutual
  input horizon; bulk snapshot transfer begins only after the host authenticates the
  joiner's RECEIVING tuple, and the joiner validates a complete staged snapshot before
  READY;
- immutable role-local input rings are the replay authority. Retained finalized history is
  only a fallback for an acknowledged input whose ring generation was legitimately retired,
  so stale pre-rollback history and independently delayed ACK viewpoints cannot deadlock a
  fully received barrier;
- both peers rewind and replay the same pinned inputs, compare one transcript identity, and
  resume only after COMMIT/APPLIED/RELEASE/RELEASE_ACK;
- an authenticated `BYE` sent from RELEASE_ACK retains the exact terminal tuple for
  disconnect attribution even if the host consumed that acknowledgment and cleared its
  active barrier one service tick before the datagram arrived;
- the former 60-tick “resume prediction while transfer continues” path is gone;
- no-progress has a bounded terminal timeout, and correction policy cannot be disabled in
  the middle of a barrier; and
- correction currently sends a full snapshot. Delta chunks remain disabled until an
  authenticated base-negotiation/fallback phase exists.

UDP backpressure is no longer reported as a successful send. Physical sends,
`WSAEWOULDBLOCK`/`WSAENOBUFS`, and hard Winsock errors are classified separately. A blocked
state/correction datagram retains its exact queue entry and transfer cursor for retry;
fresh INPUT/ACK/HELLO traffic is attempted before delayed and bulk work, and correction
bursts are capped at eight datagrams per service tick. `net.diag` and `ggpo.net` report
would-block, hard-error, and deferred-work counts. This is a fixed control-first baseline,
not congestion control or selective correction-chunk reliability.

Frame-advantage throttling remains a separate latency policy with its existing bounded
wait/catch-up behavior; it is not allowed to dissolve a correction barrier.

Prediction itself no longer resumes after its recoverability cap. At the oldest unresolved
input it enters a hard selective-resend stall until the exact input arrives or the existing
peer timeout disconnects. A history slot also cannot be reused until both peers have proved
the frame's checksum was successfully compared; checksum-delivery no-progress stalls and
then disconnects instead of overwriting required state. Corrections now use the bilateral
barrier above; the remaining P0 risk is incomplete deterministic native-state coverage,
not unilateral late-snapshot application.

## Rollback Correctness Status

The current authenticated v17 transport still has known desync and recovery hazards. The
full design and acceptance contract are in
`docs/superpowers/specs/2026-07-17-online-rollback-correctness-design.md`; prioritized work
is tracked in `TODO.txt`.

Several bounded correctness slices are complete. Rollback storage is now frozen and allocated
only after the selected map/content and native reset/start state exist. Socket/auth-only
held startup has zero rollback capacity; transactional finalization allocates the complete
history/transfer set, and peers must prove the same non-cryptographic 32-bit structural
layout ID and exact capacity before release and authoritative initial-state capture. Retry
clears the old proof and reannounces it for the fresh session.

Input/history rings also use wrap-safe uint32
serial ordering, reject stale and implausibly future frames within a single 512-slot
generation, preserve newer slot generations, and keep prediction provenance from
regressing under reordered redundant input.

Every simulated tick now creates exactly one canonical frame-boundary capture. Boundary
`N + 1` is simultaneously frame `N`'s post-state/checksum source and the retained pre-state
for the next live or replayed tick. Initial and received authoritative snapshots seed that
same cache. The serializer canonicalizes one scratch copy and derives both its CRC and
optional component summary from it, preventing history, checksum publication, correction,
and diagnostics from recapturing different live-memory moments.

P2P v17 adds the missing wire-level confirmation slice. Each INPUT packet carries a
monotonic cumulative remote-input ACK plus a 512-bit selective ACK covering the complete
admitted ring generation. The receiver tracks separate highest-contiguous remote-input and
peer-acknowledged local-input horizons; duplicate or reordered ACKs cannot lower either
one. Same-frame inputs are immutable: a local second value is refused, while peer
equivocation is a terminal protocol error. Of the 64 input slots, the sender transmits the
live edge first, then the oldest unacknowledged hole, and uses any remaining capacity for a
newest-to-oldest defensive tail. An unacknowledged local input cannot be silently
overwritten by the next modulo-512 generation.

Checksum publication and comparison use the minimum of both contiguous input horizons and
the last completely simulated, non-predicted frame. Publication is suspended while
rollback or correction makes history provisional. Authenticated remote checksums are
cached by exact frame and compared oldest-first only after the local horizon and replayed
history make them eligible; conflicting same-frame checksums fail closed. A
generation-scoped cumulative `checksum_ack_next` advances only after successful comparison.
The sender transmits the live edge first and then fills the checksum batch from the oldest
unacknowledged frame, so dropped batches are recovered by go-back-N retry. Rollback history
is retired only after both directions prove comparison; at the 512-frame boundary the
session services transport in a hard stall and disconnects after bounded checksum
no-progress rather than overwriting the slot. The fixed v17 INPUT packet is 1,292 bytes and
has a compile-time `<= 1,400` byte guard.

Network-provided start and correction blobs also use a receiver-side transaction. The
complete transport-canonical blob, schema, size, and advertised checksum are validated
before mutation. A preallocated raw backup is restored and byte-verified after any load or
post-load verification failure; inability to restore is terminal. The final prematch state
load preserves authenticated input rings and monotonic ACK horizons, including newer
prematch evidence. A safely restored transient load is attempted at most three times in
one call; the third failure returns with the exact old state and a retryable session.

Correction now uses the authenticated bilateral `D..H` barrier described above. The
remaining required P0 repairs are:

- replace production pointer-dependent raw slabs with the fully typed native adapter. A
  standalone canonical envelope now validates compatibility/map-script identity, explicit
  roles, fixed-pool occupancy/allocator metadata, stable behavior IDs, ordered danger
  references, tiles, canonical values, section bounds, and failed-decode atomicity. Active
  entity bytes `0x02..0x157` remain an opaque bridge and there is no native capture/apply
  integration yet. The final adapter still needs checked native reconstruction and semantic
  equality for the encoded controller/player roles. The fixed 16-slot thing pool needs
  semantic occupancy,
  allocator-counter, slot/type/lifecycle, and map-script-generation validation across
  spawn/despawn/death; entity updater/animation, danger-list, and particle pointers need
  stable IDs and checked local reconstruction; and
- finish the simulation-field checksum audit. Canonicalization v6 keeps gameplay RNG,
  simulation camera X/Y, camera-shake amplitude/decay, `_game_w`, `_game_h`, `_resumed`,
  and the `_mine_anim` same-tick guard at `0x541EFC` checksum-authoritative. Shake controls
  zero-versus-two gameplay RNG draws and writes
  camera X/Y; prior-tick camera X can change loser respawn. Height affects vertical camera
  clamps and camera-relative ambient paths. The online loop captures authoritative post-
  tick RNG/camera/shake/dimensions and restores them before every live, rollback, and
  correction pre-state/tick,
  so local `adjust_layout` rewrites cannot affect gameplay; it restores each client's local
  logical dimensions afterward for rendering. Every native GAME update uses
  vanilla-compatible x87 `0x037F` and MXCSR `0x00001F80` controls with exact caller-control
  restoration. The mine guard is retained because native `_mine_anim` checks it before the
  sound call, mine-tile mutation, and shake write; the adjacent sword-sound and crowd-cheer
  debounce stamps remain presentation-only and masked. The masked crowd/chant state can
  change whether a near-win chant-pitch draw runs, so its verified sound-only return site
  `0x42C9B8` is routed to cosmetic RNG. Remaining exclusions still require reader-by-reader
  native proof.

Authentication, replay rejection, and socket retry remain valuable completed layers, but
they do not close these deterministic-state defects. Production GGPO integration is gated
on the repaired state/input callbacks and seeded network-chaos suite; a library swap cannot
make incomplete serialization deterministic.

The v16 layout-proof slice reused the formerly receiver-unused packed `last_checksum` slot
without changing that version's packet size. V17 retains the field as `state_layout_id`
but intentionally expands and version-locks the ordinary packet for its confirmation
envelope; v16 and v17 peers cannot interoperate. The field's meaning is not a general
old-DLL compatibility guarantee: managed mixed clients are also rejected by the immutable
game/framework binary fingerprint. The layout ID identifies structure rather than content;
the server-selected `map_key` chooses an advertised map package, but its current 32-bit
signature is not cryptographic identity. A complete gameplay-mod/content/rules/config
manifest remains required.

## Online UX Status

The current built-in frontend is queue-first. Players use account login/registration,
Casual or Competitive queue, queue cancellation, friends, and challenges; the F6/F7
host/join path is developer-only. The hub shows connection/account/Elo state and explicit
match-found, retry, synchronizing, loading, abort, and disconnect status. Its login line
uses a compact conventional `Remember me` checkbox, and every framework-owned online
screen queues the game's actual mouse renderer once at the topmost render layer. It uses
the live native global scale, black shadow pass, animated red/yellow color pass,
`misc[7]` artwork, and native hotspot instead of the former white/custom-scale plot.

Online completion has no fullscreen win/loss/game-over state. Detecting a winner reports
the synchronized winning player slot while rollback keeps running; Eggnogg finishes its
native win animation and returns to main on its own. The live countdown/final-winner gate
also suppresses any previously queued hub handoff and preserves presentation across a
terminal server message or control disconnect. Relayed sessions retain their authenticated
route for a bounded 15-second post-consensus grace. Only after native MAIN does the framework stop
transport and open the hub's Play tab. A compact bottom-right result notification shows the opponent, map, server
status, and confirmed rating delta. Before confirmation its neutral `RESULT REPORTED`
heading does not claim an unverified outcome. Closing or expiring that provisional toast
retains the exact match identity so a delayed server confirmation can refresh it. The normal
queue rows remain gated until confirmation, and Hub/Back is explicitly owned by the stable
native main menu rather than the ended GAME state. A commit and immediate server forfeit
 result received in one TCP batch resolves directly from the start barrier without entering
 a dead GAME state. The Friends tab supports add/remove, requests, presence,
direct challenges, a complete compatible-map picker, selected-map display,
accept/decline, five-minute expiry, and a focusable social context menu. `C`, controller
X/Y, or right-click opens Challenge/Mute/Block/Unfriend for a friend and Unblock for a
blocked user; arrows/D-pad, confirm, back, mouse hover, and wheel all work inside it.
Muting suppresses only that friend's challenge pop-up—the challenge remains in the inbox.
Blocking persistently removes friendship, requests, and pending challenges in both
directions and excludes the pair from both queues. A block-list snapshot intentionally
omits presence and Elo, the other user receives only generic unavailability, and
unblocking does not silently restore friendship. Friend presence is derived from
authenticated server state and distinguishes ordinary online, casual queue, competitive
queue, setting up a match, active match, and offline. Friends are refreshed at queue and
match lifecycle boundaries; clients cannot publish arbitrary presence, busy friends
cannot be challenged, and blocked entries expose no presence. The control-v3 picker streams an exact
counted begin/choice/end response keyed by a client request ID. The server validates the
 selection when the challenge is created and recomputes the intersection at accept, so
 stale or client-invented keys cannot choose a match map.

Both normal reports include the synchronized winning player slot. The server maps that
slot through the host/join assignment it recorded when creating the match, rather than
depending on each client's local “win/loss” interpretation. Legacy reports remain
compatible, while true disagreement on synchronized slots still becomes a no-contest.

After a confirmed committed result, that same compact notification can expose a
45-second private-rematch offer. It remains non-modal and supports its visible buttons,
`R`/`N`, controller X/Y, and mouse. The first acceptance only waits and notifies the
opponent; both players must explicitly accept before the server creates a fresh match.
The exact previous map is revalidated, while match ID, roles, seed, rendezvous token, and
packet-auth token are regenerated. Rematches are always private and unranked, including
after a competitive match, so they cannot farm Elo or silently re-enter a queue. Joining a
queue, blocking, disconnecting, declining, closing the actionable result notification,
expiration, or entering another match closes the offer for both players. All messages are
bound to the retained result's exact positive match ID, and starting the fresh match clears
the old result UI only after the new protocol and map data validate.

During an established match, the pause/options pages, both players' native input-remapping
pages, the console, and Mods overlays keep rollback service and simulation moving while
local gameplay input is neutralized. The two remapping states use the native common
button-update owner, so they receive one online tick per menu update without a second
remap-specific tick path. The GGPO-active key guard consumes F1-F12 so unsafe debug/window
actions cannot mutate the match. Support for arbitrary native screens outside those
audited overlays remains future work.

Still-open UX work includes:

- ping and direct/relay route display on the player-facing match card;
- live small-window, DPI, mouse-hitbox, native-cursor, and two-client acceptance.

## Player Identity And Cosmetics

Arbitrary cosmetics and character customization remain outside the online protocol. Online
match setup must not send cosmetic profiles, assets, hats, outfits, or mod-defined
customization payloads.

There is one deliberately narrow exception: each client captures the built-in skin and
clothing palette indices for its locally assigned fighter (host/P1 or join/P2). A
fixed-size authenticated P2P packet exchanges those two bounded IDs and an exact peer echo
during prematch. Both tuples and both acknowledgements are required before READY. After the
authoritative frame-zero restore, the client applies each tuple to its server-assigned
player slot. The palette array is presentation-only and is not in rollback state or
gameplay checksums. Socket retries keep the local choice but discard stale peer proof.

Current online cosmetic rules:

- usernames and player indicators can be UI-only overlays
- gameplay match setup exchanges only the two fixed built-in palette IDs
- the palette tuple is repeated only through the frame-zero prematch barrier
- cosmetics do not affect gameplay simulation
- cosmetics are not included in rollback state checksums or desync decisions
- generic cosmetic assets are never transferred and cannot block a match
- a malformed, conflicting, or one-sided palette tuple does block prematch rather than
  silently assigning the wrong player's colors

## Discord Rich Presence

The optional Discord desktop integration reports only a closed set of coarse states:
menus, local play, hub/sign-in/social, casual or competitive queue, prematch, and
casual/competitive/private online match. Hooks passes a fixed enum rather than text, so
the activity cannot contain usernames, opponents, maps, Elo, match IDs, endpoints,
credentials, session/P2P tokens, party secrets, or arbitrary mod data. It publishes no
buttons or joinable instance.

The dependency-free client uses Discord's local Windows RPC pipe with overlapped,
incremental, 64-KiB-bounded I/O. It never waits for Discord or performs network access.
Updates coalesce and are rate-limited; absent, closed, malformed, or restarted Discord
clients cannot stall gameplay. `discord_presence` is toggled from Online Settings. A
deployment-owned public numeric `discord_application_id` is required in
`mods/modframework.cfg`; without one, the row reads `UNAVAILABLE` and no connection is
attempted. See `DISCORD_RICH_PRESENCE.md` for the exact data contract and release test.

## Discord LFG bridge

The optional server-side LFG bridge posts one embed while an authenticated player is
actually waiting in the casual or competitive queue. It is outbound Discord REST only:
there is no Gateway session, intents, command endpoint, or new matchmaking authority.
The server passes only the canonical public username and queue; mentions are disabled and
the bot never receives ratings, match IDs, control/P2P endpoints, credentials,
rendezvous data, or authentication tokens.

Its two HTTPS buttons enter the completed safe `yule://` handoff for the existing friend
challenge or same public queue. A post is created only after the player remains publicly
queued for two seconds; direct challenges never post. Leave, match, disconnect, queue
change, and orderly shutdown edit the exact message into an inactive no-button state.
The HTTPS landing page attempts to close itself after launching Yule and presents a manual
close/reopen fallback when browser policy refuses. Discord request timeouts, response sizes, tracked users,
retries, and 429 delays are bounded. See `DISCORD_LFG_BOT.md` for the secret environment
file, channel permissions, loopback HTTPS-proxy handoff, and release test.

## Matchmaking / Transport Status

The bundled Node.js prototype server currently supplies account registration/login,
public Elo and hidden MMR, casual/competitive queues, matchmaking, friend/request/
challenge state, shared-map selection, peer candidate signaling, per-match P2P secrets,
result confirmation, and rating updates. The client exposes queue/match lifecycle words,
not host/join details.

The current transport supplies symmetric public/LAN candidate probing and authenticated/
replay-protected v17 UDP packets. Direct traversal is attempted first. If that path needs
a fresh socket generation, the server switches both players together to a bounded relay
on its existing UDP port. Relay ownership is limited to the active match's observed
endpoints and player slots, fixed packet bounds/types, one-for-one forwarding, and packet/
byte budgets; peers still perform end-to-end HMAC and replay validation. The client-side control transport supplies atomic queued sends, bounded
receive framing, strict flat-JSON parsing, connect/auth deadlines, and all-or-nothing map
manifest publication.

This is still a prototype service boundary. Before public internet credentials or ranked
results are trusted, it needs at least:

- TLS with certificate/hostname validation on the account/control channel;
- hardened session authentication so reconnects do not repeatedly submit passwords;
- durable production account/match storage, rate limiting, abuse controls, and an admin
  dashboard;
- authoritative result validation instead of trusting two client reports;
- established-match reconnect/rebinding, relay health/latency display, and adaptive route
  failover beyond the completed prematch fallback;
- IPv6 candidate exchange and sockets; and
- an explicit production deployment, key rotation, monitoring, and recovery plan.

GGPO itself would handle rollback protocol details, but not these accounts, queues,
friends, signaling, NAT traversal, or relay responsibilities. Replacing the custom
rollback UDP harness with the production GGPO library therefore remains a separate
roadmap item.

## Implementation Milestones

The following prototype milestones are implemented in source and covered by focused
tests: data/control messages, account/queue/friend/challenge server flow, authoritative
friend-challenge map picking, built-in hub,
strict client control transport, map manifests, symmetric direct signaling, v17 packet
 authentication, bounded direct-to-relay P2P retry, canonical winner-slot reporting with
 native win-sequence preservation, post-content rollback-layout freeze/proof, monotonic
contiguous input confirmation and selective resend, hard input/checksum recovery,
generation-scoped reliable checksum ACK/retry, transactional received-state application,
failure-atomic live-tick pre-state restoration, mutually confirmed correction replay/release,
post-stall once-per-frame local-input commit, checksum-authoritative camera shake and
simulation dimensions with peer-local render restoration, active-match peer-local
room/mine palette preservation across rollback/correction, canonical native-tick FP
controls, a standalone
canonical rollback-envelope foundation,
deterministic map-local Lua rollback state, prematch preparation behind the countdown,
menu-time simulation, result reporting/UI, credential storage, and gameplay-mod Lua
suspension, built-in palette synchronization, and privacy-bounded Discord Rich Presence.
The server-side delayed/edit-in-place Discord LFG bridge, self-closing strict public HTTPS
handoff, existing-process `yule://` broker, and state-preserving source updater are also
complete in source with focused privacy/lifecycle tests.

Remaining release milestones include the P0 typed native-state adapter and remaining
checksum-field coverage, their deterministic chaos/soak acceptance, the security and
infrastructure items above, full mod/content/config compatibility enforcement, production
GGPO integration, physical two-machine/NAT/window acceptance, and production operations.
Broader UI work remains later product work.

## Mod And Anti-Cheat Policy

Framework-managed mod Lua owners are classified from actual API use. Gameplay-affecting
owners are suspended from match assignment through cleanup, their callbacks/mutators are
guarded, and known transient overrides are neutralized. Cosmetic/read-only Lua may remain
active inside that boundary. Hiding the Mods page is not the mechanism; it remains
inspectable and explains suspension.

Server-managed prematch also rejects differing peer-reported 32-bit game/framework build
fingerprints, requires an exact server-selected `map_key`, and proves an exact final
rollback structural ID/capacity after content initialization. These identifiers catch
ordinary incompatibility but are non-cryptographic reports from the clients, so they are
not anti-cheat.

The current custom-map key carries only the existing 32-bit package signature. Requiring
the same server-selected string prevents ordinary mismatches, but hash collisions are not
a cryptographic content proof; the complete manifest upgrade remains open.

An optional V2 `map.lua` is not a suspended general mod. It runs in a separate restricted
VM, its exact source/bindings are map identity, and all permitted persistent state is a
fixed pointer-free section of the rollback blob. Filesystem/network/OS/FFI/debug/dynamic
code and the non-bit-stable numeric power operator are unavailable. Managed prematch
requires the exact pinned script to be active and healthy before layout publication or
READY; bind/start failure is an explicit setup abort, never silent unscripted gameplay.
This is a determinism boundary, not anti-cheat against a modified native client.

Before public/ranked play, add a complete deterministic compatibility manifest covering
the enabled gameplay-mod set, content-registry state, loaded maps/assets, relevant rules
and configuration, and protocol/framework versions. A production policy must define
allowed content and stronger integrity controls; framework Lua guards cannot prevent a
different injected DLL, native patch, debugger, FFI/raw-memory write, or malicious client.

## Testing Needed

Keep the current tests and add production-style tests around them.

Existing debug/manual probes:

1. F2 roundtrip save/load.
2. F3 loopback rollback verification.
3. F4 local callback session.
4. F6/F7 authenticated localhost host/join session after `ggpo.net key`.

Focused automated coverage now includes the strict control parser and hook lifecycle,
real-socket send-queue behavior, v17 packet authentication/replay rejection and paired
prematch flow/layout finalization and mismatch/retry guards, server auth/token generation,
result-toast routing, gameplay-mod guards,
credential storage, map V2/content bridge and map-script sandbox/snapshot behavior,
map-generation retirement, updater transactions, window policy/static integration, and a
guarded production-serializer regression covering raw roundtrip, canonical transport
validation/pointer rejection, canonical load scrubbing, preflight no-mutation, and atomic
peer-local palette sidecar restoration. It also proves that the single canonical-blob
analyzer returns the same CRC used by the boundary and derives its component summary without
a second save. The
guarded core suite also covers hostile/nested FP-control restoration and the standalone
identity-pinned canonical-envelope codec; the paired runner forces a coordinated
correction under loss, delay/reordering, and authenticated duplicate replay, including
exact-ring replay after stale pre-rollback history. A second
paired case holds one peer transport-only, proves stalled wall-tick input samples remain
uncommitted, and commits only the fresh sample present when prediction recovery clears.
The long correction mode enables chaos from frame one, injects one-sided canonical
divergence at frame 600 after history-ring reuse, completes the agreed snapshot/replay
barrier (including duplicate replay and a forced state-chunk `would-block`), and continues
both roles through frame 2,047. It requires prediction and rollback on each role, one final
matching checksum, and bilateral cumulative checksum proof in the new correction
generation.
The ordinary paired chaos case runs changing nonzero inputs through frame 2,047 under
20% seeded loss and one-to-eight-tick delay/reordering, reuses the complete 512-slot
history ring four times, exercises prediction/rollback on both roles, matches the target
checksum, and drains its bilateral checksum proof. Each peer also streams all 2,048
finalized canonical pre-frame blobs plus post-frame checksums to a private temporary trace;
the supervisor structurally parses and byte-compares every record, reports the exact first
frame/byte on disagreement, and deletes both traces. Its Python supervisor reads both child
output pipes concurrently so debug logging cannot block one peer and fabricate a timeout.
A second paired mode preserves the synchronized canonical boundary while rebasing its
test epoch to `UINT32_MAX - 1023`, recreates only the neutral prediction/ACK prefix a
naturally long-running session would already own, and drives the same 2,048 exact records
through authenticated loss and delay/reordering. It crosses frame zero halfway through,
finishes at frame 1,023, exercises prediction and rollback on both roles, and drains the
bilateral checksum proof after the wrap. Production has no rebase path; its normal uint32
serial-ordering code is the path exercised after the test-only setup.

Saved trace pairs can be inspected without exposing raw state:

```text
python tools/peer_trace_diff.py host-trace.bin join-trace.bin
python tools/peer_trace_diff.py --json host-trace.bin join-trace.bin
```

The streaming parser bounds each state record, rejects empty, truncated, oversized, or
nonconsecutive streams (including incorrect wrap sequences), and stops at the first valid
divergence. It prints only the record/frame, difference kinds, first state-byte offset,
post-frame checksums, lengths, and SHA-256 state hashes. Exit codes are `0` for identical,
`1` for divergent, and `2` for invalid input or usage. Recognized EGG0/v9 states add
non-overlapping component SHA-256, changed entity slots/tile counts, and the exact first
known header/map-script/player/entity/tile location without printing state bytes. Older or
arbitrary valid traces retain the generic report.

On the first coordinated checksum divergence, each client now automatically preserves
its exact canonical post-frame boundary under `mods\desync_repros`. Corresponding files
share the opaque pair tag, divergent frame, and player-role suffix:

```text
trace_<pair>_f<frame>_p0.bin
trace_<pair>_f<frame>_p1.bin
meta_<pair>_f<frame>_p0.txt
meta_<pair>_f<frame>_p1.txt
```

Compare the two `.bin` files with `tools/peer_trace_diff.py`. The metadata records the
boundary, commands, FP controls, delay/prediction settings, and current transport,
rollback, stall, correction, and simulated-network counters. It also records both peers'
executable/framework build IDs, rollback layout identity/capacity, and a hash of the
deterministic transport settings. An independently generated chaos seed plus up to 8,192
chronological simulated drop/delay/queue-overflow events records the service tick, packet
type, and outcome without retaining packet bytes. It deliberately contains no account
credentials, username, endpoint, raw session ID, or match token. A responder may mark
`remote_checksum_known=0` when its correction request arrived before the peer's checksum;
the detecting peer retains the reciprocal checksum. File I/O runs on a bounded background
worker and each temporary file is flushed before its atomic replacement. The larger bundle
still needs the complete content/map/mod/rules manifest fingerprint, full-session input
history, lossless spill for more than 8,192 pre-divergence chaos events, and sequence-aware
network measurements.
The exact command set and remaining manual order live in
`docs/superpowers/plans/2026-07-17-todo-batch-verification.md`.

Additional tests to add:

- seeded ordinary/burst loss, delay, reorder, duplicate, repeated/burst socket backpressure,
  bandwidth cap, and input-versus-correction traffic competition at the 64-input and
  512-state edges (one forced correction-chunk would-block/retry is covered);
- semantic save/load tests for controller/player roles and every typed schema component;
- live clients arriving from differing prior maps and then loading the same selected
  map/content layout (the synthetic differing-bootstrap/shared-final-size case is covered);
- long native soaks across rooms, deaths, respawns, score changes, match reset, hazards,
  custom maps, menus, audio/render/window variants, and differing initial FP modes;
- two-client visual soaks that force ordinary rollback and coordinated correction during
  room transitions and mine tints, verifying the new room palette always settles and mine
  tint always returns without making presentation colours gameplay-authoritative;
- completion of the automatic secret-free repro bundle around the paired first-divergence
  snapshots: the complete content/map/mod/rules manifest fingerprint, full-session inputs,
  lossless spill beyond the bounded chaos-event history, and sequence-aware network
  measurements;
- dynamic two-client version/map/content/mod mismatch rejection;
- two-machine LAN and home-NAT tests; and
- internet tests with direct/relay failover after those transports exist.

Desync diagnostics should report:

- frame
- correction/session epoch and confirmation/prediction horizons
- local checksum
- remote checksum
- component hashes and the first differing schema field/entity/tile cell
- local/remote input commands
- loss/reorder/duplicate/local-drop/rollback/stall/correction counters
- x87 control word and MXCSR
- active room
- old active room
- room lerp state
- thing count
- RNG seed
- player positions
- controller/leader/loser pointer classification

## Production Checklist

Implemented prototype surface:

- account login/registration, queue-first hub, casual/competitive queue, friends,
  challenges, persistent block/mute controls, and authoritative compatible-map picking;
- synchronized hidden prematch setup and direct authenticated P2P rollback transport;
- complete shared-map manifests, exact server-selected map identity, and a
  server-revalidated friend-challenge map choice;
- bounded retry/disconnect handling and a compact confirmed-result toast with safe hub requeue gating;
- gameplay-Lua suspension, debug diagnostics, desync dumps, simulated loss/jitter, and
  configurable prototype latency controls; and
- strict client control framing/parser/deadlines (without transport encryption).

Open production gates:

- typed deterministic native-state capture/apply, full simulation checksum coverage, and
  long chaos/native-soak acceptance;
- real GGPO session wrapper;
- TLS/certificate validation and hardened session authentication;
- cryptographically trustworthy match configs plus complete version/map/mod/content/
  rules compatibility enforcement;
- relay fallback, IPv6, route choice, and two-machine internet soak testing;
- hardened server-side result validation and durable production storage/operations;
- a clear public/private content policy and stronger integrity boundary;
- live private-rematch and the remaining UX/runtime acceptance; and
- release logging/monitoring that is useful without exposing secrets.

## Build Command

`compile.sh` is the canonical source and library list. From the repository root with the
32-bit MSYS2 toolchain available, run:

```bash
bash compile.sh
```

The script links `build/SDL2_test.dll` first and only replaces `SDL2.dll` after that link
succeeds. When real GGPO is added, update the script's source/library list there.
