# Online Player Palette Synchronization

## Status

Implemented in the checked-out client and covered by the guarded prematch pair plus
static integration tests. Live two-client visual acceptance remains part of release QA.

## Problem

The host publishes the authoritative frame-zero gameplay state, but each peer owns a
different local player preference. Without a separate exchange, the joiner's chosen P2
skin/clothing colors can be replaced by the host's local settings. The generic cosmetic
profile and asset transport is intentionally compile-disabled because arbitrary peer
cosmetic payloads are outside the current online security policy.

Player palette choices are presentation state. They must not enter rollback snapshots,
checksums, match compatibility, or gameplay ownership.

## Design

- Keep the generic cosmetic profile and asset transport disabled.
- Add one fixed-size authenticated P2P palette packet. It contains only:
  - the sender's fixed palette entry count;
  - one bounded skin index and one bounded clothing index;
  - an exact echo of the last valid peer tuple as acknowledgement.
- Capture the host's local P1 choice or the joiner's local P2 choice immediately before
  each socket attempt. Configuration survives the existing prematch socket-retry path.
- Accept palette packets only after the ordinary authenticated HELLO exchange has pinned
  the current peer session and endpoint.
- Reject zero/oversized entry counts, out-of-range indices, a count that differs from the
  local build, and any conflicting tuple within one socket session.
- Repeat the tiny packet during prematch until each peer has received the other's tuple
  and received an exact echo of its own tuple.
- Include that bilateral proof in `ggpo_net_prematch_ready()`. There is no fail-open
  timeout: gameplay does not begin with an unknown or one-sided palette.
- After the final authoritative frame-zero restore, map each tuple to its server player
  assignment and write only the four native presentation indices. Do not serialize those
  indices or alter gameplay state.
- Direct/developer sessions that do not configure a palette remain source-compatible and
  do not acquire a new readiness requirement.

## Failure and lifecycle rules

- A malformed or conflicting authenticated tuple marks palette negotiation invalid and
  keeps prematch fail-closed.
- Socket retries clear peer receipt/acknowledgement but retain the local configured tuple.
- Session stop clears all negotiated peer data. No palette payload, username, endpoint,
  token, or credential is persisted.
- The diagnostics report whether local/remote tuples and the exact echo are ready, without
  exposing private match data.

## Verification

- Native API tests cover invalid configuration and local-only accessors.
- The guarded two-process prematch matrix gives host and join deliberately different
  skin/clothing choices, requires bilateral readiness, and verifies the exact remote
  tuple before the synchronized state is prepared.
- Existing tamper/replay/retry/layout/chaos/correction cases exercise the packet under the
  same authentication and prematch lifecycle as gameplay.
- Static integration coverage pins role-based capture, post-restore application, the
  generic-cosmetics compile-off policy, and rollback/checksum separation.

