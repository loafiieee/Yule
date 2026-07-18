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
- `net.diag` (alias: `net.trouble`)

## Rollback Model

The rollback model is intentionally close to GGPO:

1. Capture a compact game snapshot before a frame.
2. Feed deterministic inputs for player 0 and player 1.
3. Run exactly one native game tick.
4. Save/checksum the resulting state.
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
- camera state
- thing count and thing array
- tilemap dimensions and data pointer contents
- transient game state
- leader/loser/controller/player pointer fields
- waterfall/effect pointers if they leak into checksum or simulation

Do not fix desyncs by blindly zeroing fields. First decide whether the field is:

- gameplay state that must be saved/restored
- process-local pointer state that must be remapped after load
- render/audio-only state that can be excluded from rollback checksums

## What Real GGPO Needs

The existing code has callback-shaped rollback scaffolding, but its schema, input
confirmation, correction, checksum, and floating-point P0 defects must be repaired first.
After that contract passes the seeded chaos suite, the custom UDP harness can be replaced
or bypassed with GGPO sessions and callbacks.

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
- Friends: username entry for adding friends, incoming friend requests, incoming 5-minute challenges, friend list, accept/decline actions, and friend challenges.
- Settings: server address as a single `host:port` field, the local P2P UDP port (`Auto` by default), and challenge notifications.
- Game Over: after an online match ends, both clients report win/loss to the server, receive the confirmed result and Elo update, then can requeue or return to the hub.

The default control server is `eggnogg.loafiieee.com:47778`, persisted in `mods\online_hub.cfg`. The prototype server lives in `online_server/server.js` and handles login/registration, public Elo, hidden server-side MMR, casual queue, competitive MMR-range queue, friends, friend requests, 5-minute challenges, shared-map selection, and P2P match setup.

The current control channel is bounded newline-delimited JSON over nonblocking raw TCP.
TCP connect and login response each have independent 10-second wall-clock deadlines.
Incoming lines are capped at 8,191 bytes and parsed as complete flat JSON objects; nested
values, duplicate/escaped-alias keys, malformed UTF-8/escapes, integer overflow, embedded
NUL, and truncated destination values are rejected. Protocol/framing failure disconnects
and clears a pending authentication secret. Because this channel still has no TLS or
certificate validation, it does not protect credentials or match tokens from an on-path
attacker.

After TCP connects, the client requests and validates the server's flat `server_info`
advertisement before it sends the login/register request. Control protocol 2, match
protocol 2, P2P protocol 16, and packet-auth capability are all required. `auth_ok` repeats
the same fields and `match_found` repeats the match/P2P versions, so a stale or mixed
deployment fails at the handshake (and again at match setup as defense in depth) instead
of consuming a queue match that cannot start.

Gameplay remains direct P2P through `ggpo_net`. The server chooses the map from
the intersection of both complete client manifests (capped at 96 KiB), sends
each client the stable key and that client's local selector, and randomly assigns
host/join only for player slot and authoritative initial-state duties. Both peers
publish fresh public/LAN candidates and symmetrically send authenticated HELLOs
to every candidate; swapping host/join cannot improve NAT traversal. Every
nonempty server `map_key` must resolve to that exact installed map, and different
game/framework fingerprints abort server-managed prematch before native setup.
Connection attempts, native match initialization, state transfer, and neutral
frame-zero input exchange run behind the match-found countdown. The client stays
in the hub if that work outlasts the timer and enters gameplay only when the
synchronized first frame can advance. Setup has a bounded timeout and never
exposes a frozen GAME frame. Cosmetic profile/asset packets are compile-disabled
in `ggpo_net`; online match setup does not send cosmetics.

GGPO UDP v16 authenticates every peer datagram. The server supplies both peers one
identical 256-bit `p2p_auth_token` as exactly 64 hexadecimal characters; the older
per-user `p2p_token` remains only server-probe authorization. `ggpo_net_set_match_token`
decodes and domain-separates the match secret. Each packet uses HMAC-SHA-256 truncated to
128 bits, with a direction key binding v16, sender role, and a 64-bit CSPRNG session ID.
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
the player-facing online flow. Direct v16 host/join requires both developers to
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

State correction is capped to avoid giant hard pauses:

- the host keeps sending/retrying correction state, but does not freeze while waiting for the peer's ACK
- correction, frame-advantage, and prediction waits are capped to about 60 ticks
- the joiner hard-waits for a correction for about 60 ticks, then resumes prediction while the state transfer continues
- if the correction arrives late, the client applies it as a visible snap instead of staying frozen for many seconds
- correction transfers use delta chunks against the last shared correction/start-state baseline when possible, with full-state chunks as the fallback

The freeze cap improves responsiveness but can let simulation continue beyond recoverable
input/history and can apply a late correction without a shared barrier. Those are P0
correctness problems; tuning the cap or prediction count is not their solution.

## Rollback Correctness Status

The current authenticated v16 transport still has known desync and recovery hazards. The
full design and acceptance contract are in
`docs/superpowers/specs/2026-07-17-online-rollback-correctness-design.md`; prioritized work
is tracked in `TODO.txt`.

The required P0 repairs are:

- freeze rollback schema/size only after the selected map and deterministic content load;
- separate monotonic latest-received and highest-contiguous remote input horizons, with
  ACK/selective resend that cannot regress prediction under reordering;
- gate checksum/correction/state retirement on the highest contiguous horizon and reject
  stale/far-future frames before they can overwrite newer wrapped ring entries;
- refuse to advance beyond recoverable input or retained-state history;
- replace asynchronous host correction with a mutually confirmed-frame transaction that
  validates the full blob before applying and replays buffered inputs on both peers;
- replace pointer-dependent raw slabs with a typed, versioned, pointer-free schema and
  explicit controller/player roles, including reconstruction of dynamic entity count and
  allocator state across spawn/despawn/death; and
- checksum every simulation input, including gameplay RNG and active-room dependencies,
  while enforcing one x87/MXCSR environment for live and replay ticks.

Authentication, replay rejection, and socket retry remain valuable completed layers, but
they do not close these deterministic-state defects. Production GGPO integration is gated
on the repaired state/input callbacks and seeded network-chaos suite; a library swap cannot
make incomplete serialization deterministic.

## Online UX Status

The current built-in frontend is queue-first. Players use account login/registration,
Casual or Competitive queue, queue cancellation, friends, and challenges; the F6/F7
host/join path is developer-only. The hub shows connection/account/Elo state and explicit
match-found, retry, synchronizing, loading, abort, and disconnect status. Its login line
uses a compact conventional `Remember me` checkbox, and every framework-owned online
screen queues the game's actual mouse renderer once at the topmost render layer. It uses
the live native global scale, black shadow pass, animated red/yellow color pass,
`misc[7]` artwork, and native hotspot instead of the former white/custom-scale plot.

The current Game Over state shows the outcome, opponent, map, server confirmation/rating
text, and responsive Requeue and Hub buttons. Requeue remains in a waiting state until the
server confirms the result. The Friends tab supports add/remove, requests, presence,
direct challenges, accept/decline, and five-minute expiry.

During an established match, the pause, options, console, and Mods overlays keep rollback
service and simulation moving while local gameplay input is neutralized. The GGPO-active
key guard consumes F1-F12 so unsafe debug/window actions cannot mutate the match. Support
for arbitrary native screens outside those audited overlays remains future work.

Still-open UX work includes:

- ping and direct/relay route display on the player-facing match card;
- a bilateral private rematch flow (distinct from queue-again);
- friend-challenge map selection from the two clients' manifest intersection;
- block/mute and richer social presence;
- synchronized player colors if cosmetics are deliberately reintroduced; and
- live small-window, DPI, mouse-hitbox, native-cursor, and two-client acceptance.

## Player Identity And Cosmetics

Cosmetics and character customization are out of the online protocol for now. They can still be designed for offline/local play, but online match setup should not send cosmetic profiles, cosmetic assets, hats, outfits, or customization payloads.

Current online cosmetic rules:

- usernames and player indicators can be UI-only overlays
- gameplay match setup does not exchange cosmetic data
- cosmetic data is not sent every frame
- cosmetics do not affect gameplay simulation
- cosmetics are not included in rollback state checksums or desync decisions
- missing or mismatched cosmetic assets should not block an online match because they are not part of the match contract

## Matchmaking / Transport Status

The bundled Node.js prototype server currently supplies account registration/login,
public Elo and hidden MMR, casual/competitive queues, matchmaking, friend/request/
challenge state, shared-map selection, peer candidate signaling, per-match P2P secrets,
result confirmation, and rating updates. The client exposes queue/match lifecycle words,
not host/join details.

The current direct transport supplies symmetric public/LAN candidate probing, three
fresh-socket attempts, bounded setup/disconnect policy, and authenticated/replay-protected
v16 UDP packets. The client-side control transport supplies atomic queued sends, bounded
receive framing, strict flat-JSON parsing, connect/auth deadlines, and all-or-nothing map
manifest publication.

This is still a prototype service boundary. Before public internet credentials or ranked
results are trusted, it needs at least:

- TLS with certificate/hostname validation on the account/control channel;
- hardened session authentication so reconnects do not repeatedly submit passwords;
- durable production account/match storage, rate limiting, abuse controls, and an admin
  dashboard;
- authoritative result validation instead of trusting two client reports;
- relay fallback for strict NAT/CGNAT and route selection/latency display;
- IPv6 candidate exchange and sockets; and
- an explicit production deployment, key rotation, monitoring, and recovery plan.

GGPO itself would handle rollback protocol details, but not these accounts, queues,
friends, signaling, NAT traversal, or relay responsibilities. Replacing the custom
rollback UDP harness with the production GGPO library therefore remains a separate
roadmap item.

## Implementation Milestones

The following prototype milestones are implemented in source and covered by focused
tests: data/control messages, account/queue/friend/challenge server flow, built-in hub,
strict client control transport, map manifests, symmetric direct signaling, v16 packet
authentication, bounded P2P retry, prematch preparation behind the countdown, menu-time
simulation, result reporting/UI, credential storage, and gameplay-Lua suspension.

Remaining release milestones include the P0 rollback schema/input/history/correction/
checksum repairs, their deterministic chaos/soak acceptance, the security and
infrastructure items above, full mod/content/config compatibility enforcement, production
GGPO integration, physical two-machine/NAT/window acceptance, and production operations.
Private rematch, friend map choice, color synchronization, social integrations, and richer
UI are later product work.

## Mod And Anti-Cheat Policy

Framework-managed Lua owners are classified from actual API use. Gameplay-affecting
owners are suspended from match assignment through cleanup, their callbacks/mutators are
guarded, and known transient overrides are neutralized. Cosmetic/read-only Lua may remain
active inside that boundary. Hiding the Mods page is not the mechanism; it remains
inspectable and explains suspension.

Server-managed prematch also rejects differing peer-reported 32-bit game/framework build
fingerprints and requires an exact server-selected `map_key`. These identifiers catch
ordinary incompatibility but are non-cryptographic reports from the clients, so they are
not anti-cheat.

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
real-socket send-queue behavior, v16 packet authentication/replay rejection and paired
prematch flow, server auth/token generation, result-screen routing, gameplay-mod guards,
credential storage, map V2/content bridge behavior, map-generation retirement, updater
transactions, and window policy/static integration. The exact command set and remaining
manual order live in `docs/superpowers/plans/2026-07-17-todo-batch-verification.md`.

Additional tests to add:

- paired deterministic sessions with thousands of changing, nonzero input frames and
  exact per-frame canonical state/checksum assertions;
- prediction, multi-frame rollback, input/state ring wrap, frame-number wrap, forced
  mismatch, transactional correction, and disconnect at every correction phase;
- seeded ordinary/burst loss, delay, reorder, duplicate, `WSAEWOULDBLOCK`, bandwidth cap,
  and input-versus-correction traffic competition at the 64-input and 512-state edges;
- semantic save/load tests for controller/player roles and every typed schema component;
- differing prior maps followed by the same selected map/content layout;
- long native soaks across rooms, deaths, respawns, score changes, match reset, hazards,
  custom maps, menus, audio/render/window variants, and differing initial FP modes;
- first-divergence component/field/entity/tile diagnostics and a secret-free two-peer repro
  bundle/diff tool;
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

- account login/registration, queue-first hub, casual/competitive queue, friends and
  challenges;
- synchronized hidden prematch setup and direct authenticated P2P rollback transport;
- complete shared-map manifests and exact server-selected map identity;
- bounded retry/disconnect handling and a custom confirmed-result Requeue/Hub screen;
- gameplay-Lua suspension, debug diagnostics, desync dumps, simulated loss/jitter, and
  configurable prototype latency controls; and
- strict client control framing/parser/deadlines (without transport encryption).

Open production gates:

- typed deterministic state schema, monotonic/recoverable input protocol, coordinated
  correction, full simulation checksum coverage, canonical FP controls, and chaos/soak
  acceptance;
- real GGPO session wrapper;
- TLS/certificate validation and hardened session authentication;
- cryptographically trustworthy match configs plus complete version/map/mod/content/
  rules compatibility enforcement;
- relay fallback, IPv6, route choice, and two-machine internet soak testing;
- hardened server-side result validation and durable production storage/operations;
- a clear public/private content policy and stronger integrity boundary;
- private rematch and the remaining UX/runtime acceptance; and
- release logging/monitoring that is useful without exposing secrets.

## Build Command

`compile.sh` is the canonical source and library list. From the repository root with the
32-bit MSYS2 toolchain available, run:

```bash
bash compile.sh
```

The script links `build/SDL2_test.dll` first and only replaces `SDL2.dll` after that link
succeeds. When real GGPO is added, update the script's source/library list there.
