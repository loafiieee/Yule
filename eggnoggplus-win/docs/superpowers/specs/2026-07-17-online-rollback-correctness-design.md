# Online Rollback Correctness and Recovery

**Date:** 2026-07-17  
**Status:** Layout freeze, ring admission, v17 input/checksum hard recovery, transactional received-state application, a coordinated correction barrier, single-capture canonical frame boundaries, a canonical FP tick guard, and a standalone typed-envelope foundation are implemented; native typed-state integration and checksum-field coverage remain open
**Primary code under audit:** `ggpo_net.c/.h`, rollback serialization in `lua_manager.c`, prematch lifecycle in `hooks.c`  
**Related:** `ONLINE_MULTIPLAYER.md`, `TODO.txt`, `2026-07-17-online-prematch-runtime-design.md`, `2026-07-17-p2p-packet-authentication-design.md`

## Purpose and status boundary

Packet authentication, replay rejection, bounded socket retry, and hidden prematch setup
are implemented, but they do not establish deterministic rollback correctness. Field
reports still include intermittent desync and poor recovery behavior, and the current
audit found concrete ways for valid authenticated peers to diverge.

The current UDP session must remain labeled a prototype until every P0 invariant below is
implemented and the deterministic/chaos acceptance suite passes. Automatic state
correction is provisional recovery, not evidence that the simulation is deterministic;
it must never hide a recurring defect in production or ranked play.

## Audited failure modes

### Map-dependent state layout freeze (implemented)

Server-managed startup now opens a socket/auth-only held session with zero rollback
capacity. It does not allocate history or state-transfer storage from the previous map.
The client first installs the exact selected map/content and synchronized seed, performs
native reset and native start initialization, and then calls transactional layout
finalization. Finalization measures a versioned structural layout, stages all 512 history
slabs and the initial/correction buffers off-side, validates its first capture, and swaps
them into the session only on complete success. Repeating finalization is idempotent for
the identical frozen layout and rejects any later size/fingerprint change.

Before hold release, both peers must advertise the same non-cryptographic 32-bit
structural layout ID and exact state capacity. Missing proof keeps setup waiting; a
conflicting ID, capacity, or changed nonzero proof aborts before gameplay. Hold generation,
session replacement, and retry clear the old proof and require reannouncement. Release
then permits the host's authoritative initial capture and state transfer.

The layout-proof slice originally landed in v16 by reusing the formerly receiver-unused
`last_checksum` field for the layout ID without changing that version's packed size. V17
retains the field as `state_layout_id` and intentionally expands the version-locked
ordinary packet with input confirmation metadata. V16 and v17 cannot interoperate, and
server-managed mixed binaries are rejected by protocol negotiation plus the immutable
game/framework binary fingerprint. This structural ID is not a content digest. Exact map
content is selected by `map_key`, and the complete deterministic gameplay-mod/content/
rules/config compatibility manifest remains a separate release blocker.

### Input rings and v17 input/checksum confirmation (implemented)

The earlier packet format carried only a newest-to-oldest history. A recovered older frame
could therefore become `last_remote_cmd` after a newer one was already observed, so
prediction resumed from stale input. A delayed frame could also replace a newer frame with
the same modulo-512 ring index.

The prototype now closes that bounded ring-admission slice. It uses uint32 serial
ordering, keeps the admitted interval to one complete ring generation (283 frames behind
and 228 ahead, where the future allowance covers the larger configured prediction/frame-
advantage limit plus input delay), and rejects older/far-future aliases before indexing.
Input and rollback-history stores refuse an older generation over a newer slot. The
latest received command marker advances monotonically under packet reordering, and a
missing frame predicts from the nearest known command at or before that frame rather than
from a future input. Rollback spans, frame-advantage/catch-up comparisons, correction
staleness, prediction scans, checksum retention, packet-history scans, and RNG diagnostic
scans use the same wrap-safe ordering.

V17 adds explicit cumulative and selective receipt state to every authenticated INPUT
packet. A validity flag distinguishes “no contiguous frame yet” from a cumulative base at
frame zero. The base is the highest remote input received contiguously from frame zero, and
sixteen 32-bit words provide a 512-bit SACK for exact frames above the base. That covers the
entire admitted ring generation. ACK application validates the complete envelope before
mutation, records SACKs on absolute local ring entries, never lowers the peer-acknowledged
local horizon, and folds newly contiguous SACK entries into that horizon. All frame and ACK
math uses the same uint32 serial ordering across wraparound.

Inputs are immutable after commitment. Recommitting the same local value is idempotent,
but a different local value is refused. A peer that advertises two commands for the same
frame is disconnected for equivocation. An unacknowledged local ring generation cannot be
silently overwritten. The packet's 64 input slots send the live edge first, then scan from
the oldest unresolved hole; remaining slots retain a newest-to-oldest defensive tail for
startup and stale-ACK cases. Unlike the former newest-64 window, any unresolved frame
remains selectable throughout the 512-frame recoverable generation.

The checksum horizon is the minimum of the highest-contiguous remote-input horizon, the
peer-acknowledged local-input horizon, and the latest locally finalized simulated frame.
Checksum publication and comparison are disabled while rollback or correction makes the
history provisional. Received checksums are cached by exact frame, protected from older
generation overwrite and same-frame equivocation, then compared oldest-first once the
local replay and mutual horizon make them eligible. Each `(state_epoch, correction_id)`
generation has a cumulative `checksum_ack_next`; receipt alone cannot advance it. The ACK
advances only through checksums successfully compared in order. Each packet carries the
live checksum edge first and uses every remaining checksum slot as go-back-N recovery from
the peer's oldest unacknowledged frame. Rollback history is retired only after both peers'
cumulative checksum proofs have passed it. An individually received input above an earlier
gap may be used as exact simulation input, but it cannot advance confirmation, checksum
eligibility, or retirement through that gap.

The fixed v17 ordinary packet is 1,292 bytes. Compile-time guards require it to remain at
or below 1,400 bytes and require SACK coverage to span all 512 history entries.

### Hard input and checksum recoverability (implemented)

V17 no longer limits retransmission to the newest 64 frames: its SACK keeps an old hole
selectable while it remains in the 512-entry local-input ring. Before prediction could pass
the oldest recoverable hole, advancement now enters a permanent selective-resend stall.
There is no timeout path that resumes prediction; the exact input must arrive or the
existing peer-liveness timeout disconnects.

The state ring has a second hard boundary. A modulo-512 history slot cannot be reused until
both cumulative checksum ACKs prove its checksum was successfully compared. While blocked,
the peer continues pumping INPUT packets, checksum retries, and ACKs. Progress clears the
stall; bounded checksum no-progress disconnects rather than overwriting required history
or resuming simulation from an unverifiable generation.

### Coordinated correction barrier and transactional received-state apply (implemented protocol slice)

V17 now freezes both simulations around an authenticated, idempotent correction state
machine. The divergence frame `D` is the first checksum mismatch. The host stages the
retained pre-state of `D`, selects the newest exact mutual input horizon `H` that is still
fully retained and non-predicted, pins the role-stable P0/P1 commands for every frame
`D..H`, and declares `R = H + 1`. It never captures a predicted live edge as the correction
base. Input delivery and selective ACKs continue while gameplay is frozen, but checksum
publication is suspended for the in-flight generation.

Every normal packet repeats the epoch, correction ID, `D`, `R`, phase, and phase-specific
snapshot/transcript checksum. The phases are REQUEST, OFFER, RECEIVING, READY, COMMIT,
APPLIED, RELEASE, and RELEASE_ACK. Repeated and reordered older phases are idempotent;
conflicting tuples in the current generation fail closed. The joiner reaches READY only
after receiving and validating the complete full snapshot and independently pinning every
exact input through `H`. The host applies only after READY, and the joiner applies only
after COMMIT. Both replay `D..H` from the same staged snapshot and hash the epoch/tuple,
snapshot checksum, frame numbers, P0/P1 inputs, and pre/post replay checksums into one
transcript identity. RELEASE is accepted only for an identical transcript, and the joiner
continues holding through RELEASE_ACK until it observes the host's terminal NONE state.
The immutable role-local input rings are authoritative for replay; finalized history is
consulted only if an acknowledged input's ring generation was legitimately retired. This
prevents stale pre-rollback history or a delayed receiver-side view of cumulative ACKs from
vetoing exact commands already held by both peers. A phase made actionable by a packet at
the timeout boundary is consumed before the no-progress deadline is applied.

The old 60-tick path that resumed prediction while correction transfer continued is gone.
No-progress has a bounded correction timeout and disconnects instead of clearing history
or resuming unilaterally. The prior checksum generation is cleared only inside the mutual
READY/COMMIT barrier after the agreed replay has rebuilt `D..H`; exact input rings remain
intact. Delta correction transfer is disabled until it has an explicit authenticated base-
negotiation/fallback phase, so this barrier always stages one unambiguous full snapshot.
The host does not begin bulk correction chunks until it has authenticated the joiner's
RECEIVING tuple, making that phase a real bilateral transfer boundary instead of a
best-effort status that a fast snapshot could skip.

The receiver-side mutation boundary remains transactional. A complete start/correction
transfer is transport-canonical prevalidated for schema, size, and advertised checksum
before any live write. Apply captures a preallocated raw backup, loads the candidate,
verifies the canonical live checksum, and on failure restores and byte-verifies the exact
backup. A failed restore is terminal. Exactly three synchronous attempts are allowed only
after a verified exact restore. This makes the selected blob safe to apply within the
coordinated barrier. It does not make the native-memory blob a portable deterministic
schema; the typed-schema blocker below still prevents calling correction production-ready.

### Raw pointer-bearing state is not a deterministic schema

Current canonicalization zeroes only selected header/transient pointers. It deliberately
leaves other raw fields inside whole native slabs: every 0x15c-byte thing record can carry
an animation/updater pointer at `+0x158`, transient state contains the 16-entry danger
pointer list, and particle state contains sprite/camera/update/draw pointers. Transport
validation neither rejects nor remaps those fields. Controller/player identity is also not
represented semantically: canonical transport clears the controller/player pointer fields,
and load leaves controller ownership zero even when the byte blob passes a checksum.

Rollback state must become a typed, versioned, pointer-free schema. References are stable
IDs or explicit enum roles, lengths are bounded, optional sections have presence/version
tags, and deserialization builds a staged state before committing. Tests must assert
semantic save-load equality for players, controllers, things, rooms, tilemaps, rules,
timers, and ownership—not merely equal allocation sizes.

The standalone `rollback_schema.c/.h` envelope is an implemented foundation for that
replacement, not the replacement itself. Version 1 has explicit little-endian section
headers, checked sizes, compatibility and exact map-script identity validation, canonical
booleans/floats/reserved bytes, explicit P0/P1/controller/leader/loser roles, fixed 16-slot
occupancy and allocator metadata, stable behavior IDs, ordered danger-slot references,
tiles, and the existing MapScript v5 subdocument. Decode occurs in scratch storage,
requires byte-identical canonical re-encoding, and leaves its complete output unchanged on
failure. The adversarial test covers self-sized truncation at every section boundary,
directory/size corruption, overflow, invalid values and identities, lifecycle disagreement,
and canonical encode/decode/re-encode equality.

The foundation intentionally retains an opaque 342-byte active-entity bridge for native
offsets `0x02..0x157`; only the known `+0x158` executable pointer is represented by a
stable behavior ID. It therefore cannot yet prove all bridged bytes pointer-independent or
semantically valid. It also has no native capture/apply adapter, behavior resolver, role-
pointer reconstruction, complete allocator/global model, or transactional native commit.
The current MapScript producer supplies its canonical subdocument from a packed 32-bit x86
little-endian snapshot; a portable producer must encode every field explicitly. These are
the reasons the typed-state P0 remains open and v9 remains the production serializer.

The native thing array has a fixed 16-record capacity; `game_get_thing_count()` reports
that capacity, not live cardinality. The existing blob already byte-copies all 16 records
plus `_thing_count`, `_things_allocated`, and `_latest`, but does not prove their semantic
consistency. The typed schema must explicitly encode active/free occupancy, slot/type/
lifecycle identity, allocator counters, controller/player roles, and map-script lifecycle
generation; validate their cross-field invariants; and rebuild every process pointer from
a bounded stable ID. Spawn, despawn, death, and recycled-slot tests must assert semantic
save-load equality rather than merely equal fixed capacity.

V9 also has ambiguous overlapping ownership. Its two standalone 0x15c player records are
copied first even though real P0/P1 records live in the 16-slot thing pool, and the later
whole-pool copy overwrites them. The opaque transient slab duplicates role/allocator fields
and overlaps all of pool slot 0 plus part of slot 1, so inconsistent duplicates currently
resolve by memcpy order rather than validation. The next schema must give every field one
owner and reject inconsistent aliases before mutation.

The typed load is a staged codec, not another native memcpy layout:

1. parse explicit little-endian, versioned sections with checked sizes and reserved-zero
   fields into scratch storage;
2. validate capacity 16, slot markers/types, active-count/cursor invariants, distinct player
   roles, controller/leader/loser enums, ordered danger-slot references, finite values, and
   map-script lifecycle/contact references against the same native snapshot;
3. map allowlisted behavior IDs to local animation/updater functions and stable slot/role
   references to local pointers while building a native scratch image;
4. atomically commit deterministic globals, pool/allocator state, tilemap, and map-script
   state, then reconstruct roles and reset/rebuild explicitly presentation-only state; and
5. reserialize and require semantic equality, using the existing exact backup/restore
   transaction if any commit or postcondition fails.

### Canonical checksum audit remains incomplete

Canonicalization v5 now retains gameplay RNG, simulation camera X/Y, camera-shake
amplitude/decay, `_game_w`, `_game_h`, and `_resumed`. Native `player_update_logic`
compares a losing player's prior-tick camera-relative distance against `_game_w * 0.5`
before calling `player_respawn_self`; `game_update_camera` also reads `_game_w` for
horizontal clamping. The exhaustive native-reference audit found 22 reads and four
`adjust_layout` writes of `_game_h` (`0x0055A324`); native update uses height for vertical
camera clamps and camera-relative ambient sampling, so the current deterministic boundary
pins it while preserving peer-local render geometry. In the same native update,
`_resumed != 0` clears both buffered attack counters. Rollback apply therefore restores
all of these historical values instead of preserving/zeroing them locally.

Shake is not presentation-only. `game_update_camera` (`0x004222A0`) tests amplitude,
consumes either zero or two `frnd` calls, multiplies by decay, and writes camera X/Y. Score
handling in `game_update` adds `5.0` and sets decay `0.95`; `_mine_anim` adds `5.0` and sets
decay `0.9`; init/reset clears the entire camera block. The previous RNG classifier routed
the two camera draws to an unsnapshotted peer-local cosmetic seed even though prior-tick
camera X can change loser respawn. That route is removed: shake now consumes gameplay RNG,
is finite-validated and checksum-authoritative, and is captured/restored with the clean
simulation snapshot. The unused `_game_rumble_camera` export is also covered by that clean
boundary if an out-of-tick caller appears.

Native `adjust_layout` rewrites both dimensions from the local drawable outside simulation.
The online net loop captures clean authoritative post-tick RNG/camera/shake/dimensions and
restores them before every live, rollback, and correction pre-state capture/tick. A
separate local render snapshot is restored after received-state validation and after each
simulation/replay path, so physical window and logical render geometry remain client-local.
The guarded production-serializer regression proves checksum sensitivity and apply restore
for RNG, camera X/Y, shake/decay, both dimensions, and resume state; it also rejects
nonfinite camera state, nonfinite/out-of-range geometry, and read-only destinations without
partial mutation. A paired test gives the peers different aspect ratios, perturbs both
local dimensions plus shake, and fails unless authoritative values reach the saved pre-
state/native tick while local dimensions return for rendering. A static integration test
pins every load/restore/capture ordering. Presentation state may be excluded only after no
simulation branch reads it; the eventual typed schema should encode synchronized logical
simulation dimensions separately from local render geometry.

Canonicalization v6 also retains `0x541EFC`. Native `_mine_anim` compares it with
`_mad_ticks` at `0x004250ED` and exits on equality before the sound call, camera-shake
write, and mine-tile mutation; the accepted path records the current tick at `0x00425108`.
Online live, rollback, and correction ticks pin `_mad_ticks` from serialized `_game_ticks`
before entering native update, so this guard is deterministic. The production serializer
regression saves a state representing a second mine callback in the same tick, proves the
guard changes the checksum, restores it, and confirms equality with the synchronized clock.
The adjacent `0x541EF8` sword-sound and `0x541F00` crowd-cheer debounce stamps remain masked.
`0x541F00` can indirectly decide whether the near-win chant calls `frnd(0.9, 1.0)`, but the
result only writes the chant sound object's pitch. Its exact return address `0x0042C9B8`
therefore uses the private cosmetic RNG; replay-normalized crowd/chant timing cannot advance
the rollback gameplay seed.

Room and mine palette interpolation remains presentation-only. Native `game_update`
consumes `_game_do_lerp_colours` near the start of an update, while room-transition and
mine paths can set it later in that same update for the next tick. Transport
canonicalization deliberately clears that flag and `_lerp_time`, and checksum
canonicalization excludes the 48-float colour block at `0x541F6C..0x54202B`; applying only
the canonical controls could therefore discard a pending visual transition and strand the
old room or mine palette. Before an active-match ordinary rollback or coordinated
correction load, the net loop captures the flag, timer, and complete block as one
peer-local sidecar. It finite-validates and atomically restores all destinations after the
load/transaction and before simulation-state capture/replay. Prematch start loads do not
preserve a prior map's palette. This does not change the wire schema or gameplay checksum.
The production serializer regression proves byte-exact restoration and rejects invalid
flags, timers, nonfinite values, or unwritable destinations without a partial write; a
static test pins capture/load/restore order. Live two-client transition/mine visual
acceptance remains required.

The target split is:

- deterministic state: every field that can affect a future gameplay tick, fully
  serialized and checksummed;
- presentation state: render interpolation, cosmetic/audio-only randomness, and other
  values proven unable to affect simulation; and
- external state: pointers, handles, caches, and process-local resources reconstructed
  from stable deterministic IDs.

Live and replay native simulation now crosses one enforced per-tick FP boundary. It loads
the vanilla-compatible raw x87 control word `0x037F` (masked exceptions, 64-bit extended
precision, nearest/even) and MXCSR `0x00001F80` (masked exceptions, nearest/even, gradual
underflow, DAZ disabled), runs exactly one native GAME update plus deterministic content
interactions, then restores the caller's exact x87 control word and MXCSR. Offline live,
GGPO live, rollback and correction replay, loopback/local, and self-test paths converge on
that primitive. Hostile-mode, nested-guard, callback-failure/control-mutation, restoration,
and SSE-subnormal tests run without launching the game; a static test pins the complete
call graph. Desync diagnostics now record raw x87 and MXCSR values rather than relying on
MinGW `_controlfp` output that hid the precision bits.

## Required protocol invariants

- Implemented: a frame's local input is assigned exactly once; a conflicting reassignment
  is refused.
- Implemented: prediction hard-stalls before the oldest unresolved input can leave the
  retained generation; exact selective resend continues until recovery or disconnect.
- Implemented: hook-side physical input is a tentative wall-tick snapshot and is committed
  exactly once only after frame-zero, correction, frame-advantage, prediction, and checksum-
  retirement no-advance gates clear. Stalled wall ticks repoll and discard their snapshots;
  a successful retry of the same logical frame preserves its already committed input.
- Implemented: remote receipt, contiguous-remote, and peer-acknowledged-local horizons are
  monotonic under loss, duplication, delay, reordering, and frame-number wraparound.
- Implemented for checksums and retirement: the mutual horizon is capped by both contiguous
  input directions and finalized history; publication/comparison waits for clean replay;
  generation-scoped cumulative ACK advances only after successful comparison; and history
  retirement requires both ACK directions or enters a hard stall with bounded disconnect.
  The correction generation uses the same bilateral exact-input barrier before replay.
- Implemented: wrap-safe range checks prevent stale/far-future inputs from evicting newer
  ring entries.
- Implemented: the 512-bit SACK covers the full retained input generation, and 64-slot
  packet selection prioritizes the live edge then oldest unresolved holes.
- Implemented for received blobs: complete transport-canonical prevalidation, advertised
  checksum verification, transactional load, exact rollback on failure, and fatal handling
  of an unverifiable restore.
- Implemented for live ticks: start state has an explicit no-advance gate; after a canonical
  pre-state save, any native advance, post-tick checksum, or clean-state capture failure
  restores that pre-state before the session aborts. A failed restore is terminal.
- Implemented at the current transport-blob layer: every correction selects a retained
  divergence boundary and exact mutual horizon, coordinates snapshot readiness, replays
  the same pinned inputs, verifies a transcript identity, and releases both peers through
  an idempotent commit/release handshake with bounded timeout.
- Implemented: one canonical boundary capture supplies retained history, frame checksum,
  correction snapshots, and optional component diagnostics. A simulated frame's post-state
  is boundary `N + 1` and is reused as the next pre-state; authoritative start/correction
  snapshots seed the same cache. CRC and component summary are derived from one
  checksum-canonical scratch blob rather than separate live-state saves.
- Open at the native boundary: the bounded/versioned canonical envelope foundation exists,
  but production capture/apply still uses v9 raw slabs and the opaque entity bridge is not
  yet a pointer-free per-kind model.
- Open: snapshot load must validate and reconstruct fixed-pool occupancy, allocator
  counters, slot/type/lifecycle identity, controller roles, and stable pointer IDs across
  spawn, despawn, death, and slot reuse.
- Open: all simulation-affecting RNG, room-selection, map, controller, and entity state must be
  canonical; presentation-only exclusions are documented and tested.
- Implemented for native simulation: live advancement, rollback replay, and correction
  replay converge on one native tick primitive and canonical floating-point controls.

## Diagnostics and reproducibility

Aggregate CRC mismatch is insufficient. Each frame should retain a lightweight hierarchy
of component hashes and counts, such as global/rules, RNG, players/controllers, rooms,
tilemap, things/entities, and registered deterministic content. On the first mismatch,
preserve both canonical snapshots and report the exact first differing schema field,
entity, or tile cell.

Each session should be able to emit a bounded, secret-free repro bundle containing:

- protocol, game, framework, schema, map, rules, mod, and content fingerprints;
- role/assignment and deterministic start configuration;
- complete local and received remote input traces;
- frame-by-frame confirmation, prediction, rollback, stall, and correction events;
- monotonic-clock RTT, jitter, loss, reorder, duplicate, local-drop, and would-block data;
- x87/MXCSR settings;
- seeded network-chaos configuration and generated events; and
- first-divergence component hashes and minimal state slices.

Match secrets, account credentials, raw authentication tokens, and unrelated personal data
must never enter the bundle. The first peer-trace diff tool now validates and compares one
exact canonical frame sequence, including uint32 wrap, and identifies its earliest state
or checksum disagreement without printing state. The future bundle format must add explicit
session/epoch metadata so the tool can align independent live captures before that
frame-level comparison.

## Automated acceptance

The current paired prematch test proves connection/authentication and a minimal synchronized
start. It now covers zero-capacity held startup, differing bootstrap sizes converging on a
shared final size including tail bytes, matching and mismatched structural schema/capacity
proofs, idempotent finalization and changed-layout rejection, guarded release, and retry
proof reset/reannouncement. Its deterministic native preflight also covers both receive-
window edges, 512-slot delayed aliases, input/history generation preservation, newest-to-
oldest command ordering, prediction provenance, immutable same-frame input, cumulative and
512-bit selective ACK folding, live-edge/oldest-hole packet selection, unacknowledged input
and unconfirmed-history overwrite guards, permanent prediction recovery stalls, rollback
deferral, generation-scoped cumulative checksum ACK validation/retry/retirement stalls,
remote-checksum caching and conflict handling, and uint32 frame/ACK/prediction-window wrap.
The state-transaction mode covers malformed/noncanonical blobs, forged checksums, partial
loads, post-load corruption, exact restore, restore failure, and valid commit. The paired
prematch skew case proves the synchronized final state load preserves newer authenticated
input rings and ACK horizons, stops after exactly three safely restored failures without
mutation, and succeeds on one subsequent explicit clean retry. The paired correction case
injects a one-sided canonical-state divergence, withholds that peer's checksum payload to
force the asymmetric REQUEST path, and runs the complete snapshot/replay/release barrier
under seeded loss, variable delay/reordering, and authenticated duplicate replay. It proves
both peers agree on target checksum, correction ID, `D`, `R`, and replay transcript, then
drains the new generation's checksum proof in both directions. It also covers the case where
retained pre-rollback history disagrees with the immutable exact input rings and requires
the correction replay to use the rings without deadlocking. The fixed packet-size
assertion pins the v17 ordinary datagram at 1,292 bytes under its 1,400-byte ceiling.
The paired input-sampling case forces a prediction hard stall while repeatedly changing the
host's physical sample. It proves the blocked future frame remains unassigned, the latest
post-stall sample is committed on recovery, and both peers subsequently checksum-confirm
the same deterministic target. This commit rule does not replace the still-open adaptive
input-delay and GGPO-style time-sync policy.

The paired live-tick failure case injects a clean-geometry read failure only after the first
native tick has mutated state. Both peers must reject the advance, retain frame zero, and
byte-match their canonical pre-tick state after restoration. A static test also requires
the explicit `start_state_loaded` gate and both post-tick failure branches to use that
restore path.

The ordinary paired chaos case now runs changing nonzero inputs through frame 2,047 under
20% seeded loss and one-to-eight-tick delay/reordering. This reuses every 512-slot history
entry across four complete generations. Both roles must record predictions and rollbacks,
agree on the target checksum, and drain the cumulative checksum proof in both directions.
As each checksum horizon advances, the test-only net seam copies the retained canonical
pre-frame blob and finalized post-frame checksum into a private length-delimited trace.
The supervisor requires exactly 2,048 ordered records, byte-compares every frame between
roles, reports the exact first frame/byte/checksum/length disagreement, and deletes both
temporary traces.
The Python harness drains both child pipes concurrently: sequential `communicate` calls
could fill the unread peer's Windows stderr pipe, suspend it inside rollback logging, and
manufacture a transport timeout unrelated to the UDP session.

The paired uint32-wrap mode applies a synchronized test-only epoch rebase at
`UINT32_MAX - 1023` while preserving the exact canonical starting boundary. It quarantines
pre-rebase packets and recreates only the neutral prediction and cumulative-ACK prefix that
a naturally long-running session already has; production sessions have no rebase path.
The same authenticated 20%-loss and variable-delay/reordering path then produces 2,048
ordered exact records through frame 1,023. The supervisor byte-compares every state and
post-frame checksum across `UINT32_MAX -> 0`, requires prediction and rollback on both
roles, and requires the cumulative checksum stream to drain in both directions.

`tools/peer_trace_diff.py` is the offline, secret-safe reader for those length-delimited
traces. It streams one bounded record per peer, validates nonzero state lengths and exact
consecutive uint32 frame order (including wrap), and stops at the first valid difference.
Reports include the record/frame, state length or first byte offset, post-frame checksum,
and SHA-256 of each canonical state, never the state bytes. Plain output and JSON use exit
codes 0/1/2 for identical/divergent/invalid. The guarded regression covers equality across
wrap, each divergence class, extra records, and hostile empty/truncated/oversized or
nonconsecutive streams. Component-schema decoding and automatic repro bundles remain open.

The long forced-mismatch mode runs the changing nonzero-input correction pattern under 20%
loss and variable delay/reordering from frame one. After the 512-slot rings have reused a
generation, the join peer mutates canonical state at frame 600 and suppresses its checksum
payload long enough to exercise the asymmetric checksum-detector/REQUEST path. Both peers
must agree on correction snapshot `D=600`, the same bounded resume `R`, correction ID, and
exact input replay transcript. Authenticated duplicate replay and one forced state-chunk
`would-block` remain active; the blocked send may advance neither the physical-send count
nor full/delta cursor. After release, both roles must continue predicting and rolling back
through frame 2,047, agree on its finalized checksum, and drain the new generation's
cumulative checksum proof in both directions. The original frame-52/179 case remains the
fast correction regression.

`tests/run_core_native_tests.ps1` also builds and runs a real production-serializer
regression through a test-only native-address binding seam. It proves raw
save/load/re-save byte identity, raw versus transport-canonical validation, rejection of
checksum-masked pointer injection, canonical pointer scrubbing, and no live mutation when
apply preflight rejects an unavailable player. It also proves byte-exact, atomic
restoration of the peer-local room/mine palette sidecar and rejects invalid or nonfinite
sidecar values without a partial write. Its canonical-blob analyzer regression proves the
boundary CRC and component-summary `full_crc` are derived from the same already-canonical
blob. The seam changes only where native data is
located in the test process; it does not replace the serializer implementation. This is a
regression for the current v9 blob and must not be read as completion of the typed schema.

The same guarded runner builds the standalone canonical-envelope test with strict C11
warnings-as-errors and the independent FP-control test. The envelope test exercises
identity-pinned canonical decoding, every section boundary, malformed directories and
reserved bytes, fixed-slot/role/danger/lifecycle invariants, invalid finite/enum/boolean
values, complete failed-decode output atomicity, and byte-identical re-encoding. The FP
test seeds hostile x87 precision/rounding and MXCSR rounding/FTZ/DAZ modes, proves the
callback observes `0x037F`/`0x00001F80` exactly once, checks nested and failure returns,
verifies gradual SSE underflow, and proves exact caller-control restoration.

That focused test does not replace the remaining long paired/native acceptance below,
which additionally requires:

- exact per-frame cross-peer canonical state for real native entity/map lifecycles (the
  complete ordinary and uint32-wrap 2,048-frame synthetic canonical states are
  byte-compared);
- correction loss, duplication, corruption, timeout, abort, and retry cases;
- disconnect at every protocol phase with bounded cleanup;
- differing prior maps followed by one identical selected map/content layout;
- controller/player role round trips through every save/load path;
- fixed-pool entity spawn/despawn/death/reuse with differing occupancy, allocator counters,
  controller roles, and pointer-ID reconstruction;
- gameplay RNG and active-room divergence caught on the first affected frame; and
- intentionally different x87/MXCSR startup modes normalized before tick zero.

The focused local-backpressure baseline is implemented: raw UDP sends distinguish physical
success, retryable `WSAEWOULDBLOCK`/`WSAENOBUFS`, and hard errors; blocked delayed packets
and state/correction cursors remain pending; hard failures are counted and retired from the
delay queue; fresh control traffic precedes delayed/bulk work; and correction bursts are
capped at eight datagrams. A paired correction case injects one blocked state chunk under
loss/delay and proves no physical-send count or full/delta cursor advance before successful
recovery. Static coverage pins queue retention, cosmetic cursor discipline, control-first
service order, counter exposure, and the cap.

Seeded network matrices must still cover ordinary and burst loss, variable delay, reorder,
duplicate, repeated/burst local backpressure, bandwidth cap, MTU pressure, and sustained
traffic competition between inputs and correction chunks. Boundary cases include the full
resend horizon, oldest retained state, prediction cap, ACK bitmap edge, and ring wrap.

Native gameplay soaks must cross rooms, deaths, respawns, scoring, round/match reset,
interactive hazards, custom maps/content, menu-time simulation, and render/audio/window
variants. Acceptance requires no unexplained divergence, bounded stalls/catch-up, and a
deterministic first-failure repro whenever a deliberately injected mismatch occurs.

## Latency and transport quality after correctness

Once the state/input protocol is correct:

- replace service-tick RTT with high-resolution monotonic, sequence-aware RTT/jitter/loss
  sampling that ignores reordered timing regressions;
- negotiate/adapt input delay only at mutually confirmed safe boundaries;
- replace hard frame-advantage and freeze-then-ignore behavior with GGPO-style time sync;
- budget rollback/correction replay separately from rendering so catch-up cannot monopolize
  a visible frame;
- extend the current control-first/fixed-eight-datagram baseline into explicit priority
  classes and a shared byte budget;
- selectively acknowledge/resend correction chunks and pace bulk transfer from measured
  backpressure, bandwidth, loss, and congestion rather than a fixed cap;
- reuse preallocated frame/state/crypto buffers and avoid per-checksum full-blob allocation;
  and
- expose ping, jitter, loss, local drops, rollback depth/rate, stalls, and direct/relay
  route health to players and diagnostics.

Established-match reconnect/resume, authenticated NAT rebinding, direct-to-relay failover,
MTU discovery, congestion control, and IPv6 remain production transport work.

## Production GGPO gate

Replacing the custom transport with a production GGPO session wrapper remains the chosen
direction, but library integration does not repair an incomplete game-state schema. Before
the switch, freeze the deterministic callbacks and pass the schema/input/chaos suite above.
Then map save/load/free/advance, synchronized inputs, time-sync advice, disconnect events,
authentication envelope, and direct/relay behavior to GGPO and rerun the same suite without
weakening any acceptance invariant.
