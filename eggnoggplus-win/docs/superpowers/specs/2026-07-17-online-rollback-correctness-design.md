# Online Rollback Correctness and Recovery

**Date:** 2026-07-17  
**Status:** Design/audit backlog; known P0 correctness hazards remain open  
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

### Map-dependent state layout is chosen too early

Session startup currently measures and fingerprints the rollback blob before the
server-selected map/content is installed. The state size includes live tilemap data, so
peers arriving from different prior maps can calculate different compatibility data or
allocate a buffer too small for the final selected map.

The protocol must fingerprint a versioned field schema, install and validate all
deterministic match content, and only then freeze the final state layout. A fixed maximum
is acceptable only if it is proven, bounds-checked, and included in compatibility tests.

### Redundant input packets can regress prediction

Input packets carry a newest-to-oldest history, and receive processing follows that
order. A recovered older frame can therefore become `last_remote_cmd` after a newer one
was already observed. Prediction then resumes from stale input even though the packet was
valid.

Input bookkeeping needs separate monotonic values:

- latest remote frame ever received, used to choose the newest actual command available;
- highest contiguous remote frame, used as the confirmation/irreversibility horizon; and
- an ACK bitmap or ranges describing gaps above that contiguous horizon.

No duplicate, delayed, or reordered packet may lower any monotonic horizon or replace the
prediction base with an older command.

Checksum publication, state retirement, and correction eligibility must never advance
past the highest contiguous confirmed-input horizon. An individually received frame above
an earlier gap is still predicted from the session's confirmation perspective.

The input ring also needs wrap-safe serial arithmetic and an explicit retention window.
Frames older than retention or implausibly far in the future are rejected before indexing;
a delayed frame must never overwrite a newer generation occupying the same ring slot.

### Simulation can outrun recoverable history

Only 64 recent input frames are redundantly transmitted, state history contains 512
frames, prediction can be configured substantially beyond the resend window, and the
current stall timeout eventually permits advancement past its cap. This can create an
input hole that neither peer can retransmit and a required state that is later
overwritten.

Before advancing a frame, the session must prove that every unresolved remote input can
still be recovered and every possible rollback target still has a state. If that proof
would become false, the only valid transitions are bounded pause/retransmission,
coordinated resynchronization, or explicit disconnect. “Wait, then ignore the limit” is
not permitted.

### Correction is not a coordinated transaction

The host can capture potentially predicted live state, clear its history, and continue
while the joiner receives and rewinds later. Inputs between those events may already be
outside the resend window, and the peers no longer share the same rollback base.

A correction epoch must instead:

1. agree on a mutually confirmed frame;
2. retain all later inputs and states until commit;
3. stage a snapshot of that confirmed frame;
4. validate schema, size, complete transfer, and canonical checksum before mutation;
5. acknowledge readiness on both peers;
6. load from a staging buffer and deterministically replay buffered inputs; and
7. commit/retire the old history only after both peers confirm the new epoch.

Failure at any step leaves the prior live state intact or disconnects cleanly. The
receiver must never load a blob first and discover corruption afterward.

### Raw pointer-bearing state is not a deterministic schema

Current canonicalization zeroes pointer fields while load-time role reconstruction still
consults serialized pointer ranges. Controller/player identity can therefore be lost or
misclassified even if the byte blob passes a checksum.

Rollback state must become a typed, versioned, pointer-free schema. References are stable
IDs or explicit enum roles, lengths are bounded, optional sections have presence/version
tags, and deserialization builds a staged state before committing. Tests must assert
semantic save-load equality for players, controllers, things, rooms, tilemaps, rules,
timers, and ownership—not merely equal allocation sizes.

Entity cardinality is part of that schema. Loading a snapshot must reconstruct its
`thing_count`, allocation/capacity metadata, stable identities, and active/free slots. It
must not require the current live entity count to equal the historical snapshot, because
valid rollback and correction routinely cross spawn, despawn, and death events.

### Canonical checksums omit simulation inputs

Gameplay RNG is currently canonicalized away, and camera data is omitted even though it
participates in active-room selection. Similar exclusions need an explicit read audit.
Presentation state may be excluded only after no simulation branch reads it.

The target split is:

- deterministic state: every field that can affect a future gameplay tick, fully
  serialized and checksummed;
- presentation state: render interpolation, cosmetic/audio-only randomness, and other
  values proven unable to affect simulation; and
- external state: pointers, handles, caches, and process-local resources reconstructed
  from stable deterministic IDs.

Live and replay ticks must also run under one enforced x87 control word and MXCSR mode.
Logging floating-point controls only after a desync is diagnostic, not prevention.

## Required protocol invariants

- A frame advances only when its local input is assigned exactly once and all unresolved
  remote inputs remain recoverable.
- Local physical input is sampled/committed at a defined point after any time-sync stall;
  a long wait cannot attach stale input to an unintended future frame.
- Remote receipt and confirmation horizons are monotonic under loss, duplication, delay,
  and reordering, including frame-number wraparound.
- Checksums, correction frames, and state retirement never exceed the highest contiguous
  confirmed-input horizon.
- Wrap-safe range checks prevent stale/far-future inputs from evicting newer ring entries.
- ACK/selective-resend coverage extends at least to the oldest unresolved frame.
- Rollback history is never overwritten while any legal future packet could require it.
- Every correction uses a confirmed-frame epoch and validates before applying.
- State serialization is typed, pointer-free, bounded, versioned, and frozen only after
  deterministic match content is installed.
- Snapshot load reconstructs dynamic entity cardinality/allocator state across spawn,
  despawn, and death.
- One canonical frame capture feeds history, checksum, component hashes, and diagnostics;
  those consumers cannot observe different state from the same frame.
- All simulation-affecting RNG, room-selection, map, controller, and entity state is
  canonical; presentation-only exclusions are documented and tested.
- Live advancement, rollback replay, and correction replay use identical tick code and
  floating-point environment.

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
must never enter the bundle. A peer-trace diff tool should align sessions by epoch/frame
and identify the earliest protocol or state disagreement.

## Automated acceptance

The current paired prematch test proves connection/authentication and a minimal synchronized
start. Rollback acceptance additionally requires:

- thousands of frames of nonzero, changing inputs with exact per-frame canonical state;
- sustained prediction and multi-frame rollback on both roles;
- input/state ring wraparound and frame-number wraparound;
- forced checksum mismatch and one transactional correction epoch;
- correction loss, duplication, corruption, timeout, abort, and retry cases;
- disconnect at every protocol phase with bounded cleanup;
- differing prior maps followed by one identical selected map/content layout;
- controller/player role round trips through every save/load path;
- entity spawn/despawn/death with differing live and snapshot counts;
- gameplay RNG and active-room divergence caught on the first affected frame; and
- intentionally different x87/MXCSR startup modes normalized before tick zero.

Seeded network matrices must cover ordinary and burst loss, variable delay, reorder,
duplicate, local `WSAEWOULDBLOCK`, bandwidth cap, MTU pressure, and traffic competition
between inputs and correction chunks. Boundary cases include the full resend horizon,
oldest retained state, prediction cap, ACK bitmap edge, and ring wrap.

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
- prioritize current inputs and ACKs over heartbeat and correction data;
- selectively acknowledge/resend correction chunks, pace bulk transfer, and treat local
  socket backpressure as a measured drop;
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
