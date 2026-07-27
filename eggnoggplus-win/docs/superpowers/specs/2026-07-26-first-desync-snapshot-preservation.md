# First-Desync Snapshot Preservation

Date: 2026-07-26

## Outcome

The rollback transport automatically preserves both peers' first divergent canonical
post-frame state before coordinated correction replaces either history generation. The
capture is bounded, does not perform filesystem I/O on the gameplay thread, and emits no
credentials, endpoint, username, raw session ID, or match token.

This is the first slice of the larger per-session repro bundle. The complete
content/map/mod/rules compatibility-manifest fingerprint, a full-session input trace,
lossless event spill beyond the bounded recent chaos history, and sequence-aware network
measurements remain separate work.

## Capture boundary

A mismatch reported for simulation frame `N` compares that frame's post-tick checksum.
The exact bytes checksummed are the retained canonical pre-state of frame `N + 1`.
Capture therefore requires an exact valid `N + 1` history generation and copies it before
queuing any background work.

The detecting peer records both local and remote checksums. A host responding to a
correction request also records its opposite boundary before preparing correction. Since
the request can arrive before the requester's checksum, responder metadata explicitly
marks whether its remote checksum was known rather than inventing a value.

Only the first eligible divergence is captured per session. Allocation or thread-start
failure leaves the capture eligible for a later attempt.

## Files and privacy

Production writes two files per peer beneath `mods\desync_repros`:

- `trace_<pair>_f<frame>_p<role>.bin`
- `meta_<pair>_f<frame>_p<role>.txt`

`pair` is a one-way 32-bit correlation tag derived from the unordered random peer session
IDs and state epoch. Raw session IDs are never persisted. Temporary files are flushed and
renamed with replace and write-through semantics. The copied canonical state is erased
before the worker task is freed.

The binary file is one record in the existing peer-trace format: little-endian frame,
state length, local checksum, then the canonical state bytes. It can be compared with
`tools/peer_trace_diff.py`, which reports hashes and structural locations without printing
raw state.

The versioned ASCII metadata includes the role, divergent and boundary frames, local
commands, prediction status, bilateral executable/framework build fingerprints, rollback
layout IDs/capacity, a deterministic transport-config hash, FP controls, configured
delay/prediction limits, and bounded transport/rollback/stall/correction/chaos counters.
It deliberately excludes account identity, credentials, network addresses, and rendezvous
data.

Each local simulator receives an independent random nonzero seed rather than deriving the
published value directly from a session ID. A fixed 8,192-entry ring records every
simulated packet drop, delay, and delay-queue overflow through the divergence. Each event
contains only its sequence, service tick, event kind, packet type, and delay value. The
capture copies the ring in chronological order and writes both stored and total counts so
truncation is explicit. Lossless spill for unusually late divergences that exceed this
bound remains open.

## Runtime behavior

The gameplay thread validates and copies only one retained state. A background worker
does all directory and file operations. Session teardown gives the worker a bounded
two-second completion window before freeing rollback storage; the worker owns its state
copy and can safely finish independently.

The test-only build writes only when `EGGNOGGPLUS_TEST_REPRO_DIR` names an isolated
directory. Production ignores that override and always uses the fixed diagnostics folder.

## Verification

The guarded short and 2,047-frame long correction pairs inject a real one-sided canonical
mismatch under authenticated loss, delay, reordering, duplicate replay, and socket
backpressure. Each requires exactly one trace and metadata file per peer, the same pair
tag and frame, opposite local checksums, different canonical state, correct role and
boundary identity, reciprocal build/layout/config identities, and at least one peer's
reciprocal checksum proof. Test artifacts are deleted by the supervisor.
Both the short and frame-600 long divergence require a nonzero seed, structurally valid
chronological event entries, and `stored == total`, proving those guarded scenarios retain
their complete pre-divergence chaos history.

Static coverage pins the exact `N + 1` boundary, pre-thread state copy, detector and
responder hooks, production/test directory split, atomic rename, metadata privacy
boundary, state erasure, and teardown ordering.
