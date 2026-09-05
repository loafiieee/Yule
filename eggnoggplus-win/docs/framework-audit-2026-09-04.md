# Framework and netcode follow-up audit, 2026-09-04

This follow-up extends `netcode-audit-2026-09-04.md`. It supersedes that report's
narrow probe-parser implementation: probe responses now use the strict control
JSON parser, including full-document validation and a checked uint32 getter.

## Repairs

- Reject conflicting initial-state chunks after synchronization, invalid initial
  transfer metadata, and unknown state flags. A late same-epoch initial transfer
  must not replace running state or reset the joining peer to frame zero.
- Reject duplicate JSON keys, trailing garbage, embedded NULs, invalid IPv4 probe
  endpoints, and non-integer/out-of-range unsigned values. Failed numeric parsing
  leaves the caller's output unchanged.
- Preflight native snapshot scalar and slab access before capture/apply. Reject
  invalid role tags and transient ranges. Normalize role modes when raw pointers
  are erased so canonical load/checksum round trips agree. Canonicalization is
  now version 7; layout schema remains 6. Both peers require compatible builds.
- Bound server control frames by raw UTF-8 bytes, including whitespace-only and
  unterminated traffic. Contain malformed dispatch values, reject authentication
  changes on an authenticated socket, and use prototype-free account/token maps.
- Fail startup on corrupt existing account/rating stores or invalid secrets.
  Persist stores through flushed temporary files and atomic rename. This does
  not provide a transaction spanning the account and rating files.
- Validate private admin Host names before issuing sessions, addressing DNS
  rebinding in addition to the existing private-client gate.
- Preserve font allocation ownership and reserved glyph slots across reloads;
  rebuild cleared cached slots and reject slots taken by another owner. Reject
  truncated owner/path identifiers and overflowing decoded texture dimensions.
  Canonicalize texture keys before recording registrations and allocate owner
  bookkeeping before backend mutation.

## New framework surface

API revision 6 adds `mod.font.unregister_glyph(byte)` and
`mod.texture.unregister(target)`. Removal is owner-scoped, replays remaining
enabled declarations, and reports whether a restart/refresh is needed. Disabled
or gameplay-suspended owners cannot mutate these registrations. Capability names
and return contracts are documented in `MODDING.md` and the API reference data.

## Verification

- Guarded core native/static suite: passed, including serializer failure atomicity,
  strict parser boundaries, glyph ownership/cache tests, and 100 texture
  registration/reset cycles with balanced decoded-image allocations.
- Full authenticated paired netcode suite: passed, including initial-state
  rejection, retry/tampering, 2,048-frame chaos and uint32 wrap, correction after
  ring reuse, and disconnect handling.
- Server `npm run check` and `npm test`: passed (25 tests). Disposable server
  control-safety and match-protocol integration tests passed.
- Guarded map-script and V2 map suites: passed.
- Full production DLL linkage: passed, producing `build/SDL2_framework_audit.dll`.

Logs are in `build/framework_deep_audit_*.log`. The installed SDL2.dll was not
replaced and no game executable was launched. New Lua removal APIs have native
backend and static wiring coverage; live Lua/rendering acceptance remains open.

## Still open

The Vanilla 1 last-room desync has no proven root cause or verified fix. The paired
harness uses a synthetic simulation adapter and cannot establish native room,
death, or respawn determinism. Pointer-free production snapshots, complete native
field auditing, GPU atlas retirement soaks, live two-client acceptance, TLS,
IPv6, full compatibility policy, and transactional production persistence remain
in `TODO.txt`. This audit does not establish that all framework or netcode defects
have been found. Browser discovery exposed no available browser, so dashboard
visual acceptance also remains pending.
