# Netcode Disconnect-Phase Test Design

Status: implemented with guarded paired coverage.

## Goal

Prove that an established peer shutdown is authenticated, terminal, visible to the other
peer, and locally clean both during ordinary gameplay and while coordinated correction is
active. Reuse the production transport and existing hidden two-process runner so the tests
cannot create visible consoles or bypass runtime-DLL preflight.

## Ordinary gameplay phase

Both peers complete prematch and advance with deterministic nonzero inputs. The host may
disconnect only after frame 2,048 and bilateral checksum confirmation through frame 2,047,
covering four complete 512-slot history generations. It calls the normal committed-session
shutdown path, which sends an authenticated `BYE`.

The joiner continues normal gameplay service until `ggpo_net_advance` reports the explicit
`peer disconnected` terminal condition. Generic failure, timeout, premature exit, a still
active local transport, or a missing host notification fails the pair.

## Coordinated-correction phase

The existing asymmetric mismatch fixture first proves its checksum baseline, corrupts one
join-side canonical byte, and withholds its checksum long enough to enter the normal
REQUEST/OFFER correction protocol. The host may disconnect only in OFFER with a nonzero
correction ID and a captured snapshot frame.

The joiner must already be in a nonzero correction phase when it receives the authenticated
terminal packet. REQUEST legitimately has no correction ID yet; the host-owned OFFER ID is
therefore the identity assertion. Both processes must finish with the transport inactive.

## Snapshot-receiving phase

A separate pair waits until the host is still in OFFER but has authenticated the joiner's
RECEIVING phase for the same nonzero correction ID. Only then may it shut down. The joiner
must receive `BYE` with the same nonzero correction identity; authenticated loss/reordering
may let its already-complete snapshot advance locally from RECEIVING to READY before the
terminal packet arrives. This proves a partially transferred or newly staged snapshot
cannot turn terminal peer departure into correction timeout or partial apply. The
peer-phase observer exists only in the guarded test build.

## Post-replay commit phase

A third pair lets the same real mismatch continue through snapshot transfer and
deterministic replay. The host may disconnect only after its local phase is COMMIT with a
nonzero correction ID, snapshot, and replay transcript. At that instant the joiner is
stably READY with the same host-owned identity, proving an authenticated shutdown remains
terminal after the receiver has staged state but before mutual release.

The host sends the normal `BYE`, and the joiner must report the explicit disconnect while
its local correction phase and ID are still nonzero. Both transports again finish
inactive; completing correction or timing out instead fails the case.

## Applied/release phase

A fourth pair advances one transition further. It permits host shutdown only in RELEASE
after the joiner has reported APPLIED, with the same nonzero correction ID, snapshot, and
transcript. The joiner must receive `BYE` while its local phase is still APPLIED. This
proves replay completion does not accidentally turn an authenticated peer exit into a
successful release, stale correction, or timeout.

## Release-ack reverse direction

The sixth pair reverses the terminating role. The joiner waits until RELEASE_ACK with the
host still observed in RELEASE, then sends the normal authenticated `BYE`. The host must
report explicit peer disconnection against the exact RELEASE/RELEASE_ACK tuple and shared
nonzero identity. If the host consumes RELEASE_ACK and clears active correction state one
service tick before `BYE` arrives, the authenticated terminal tuple is retained for
disconnect attribution without resurrecting the barrier. This covers the final stable
correction phase and proves terminal shutdown is bilateral rather than accidentally
host-specific.

## Harness contract

`tests/prematch_net_test.py` exposes `--disconnect-only` and
`--disconnect-correction-only`, `--disconnect-receiving-only`,
`--disconnect-commit-only`, `--disconnect-release-only`, and
`--disconnect-release-ack-only`. It includes all six pairs in its complete matrix,
supplies the required MinGW runtime DLL search path, launches children without visible
windows, drains both output pipes concurrently, and validates role-specific
notification/detection markers. Test executables are never launched outside this guarded
runner.

## Remaining boundary

These cases cover transport teardown phases. Native entity spawn, despawn, death, and slot
reuse through the same long paired history remain separate open work because the synthetic
state fixture does not exercise the game's native allocator or lifecycle.
