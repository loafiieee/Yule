# Netcode audit, 2026-09-04

## Scope

Reviewed the WinSock TCP/probe implementation, GGPO receive dispatch and
authentication gates, input/checksum envelopes, send/backpressure paths, correction
and state-sync handling, and selected serializer/native end-room code. This is a
focused source audit with regression execution, not certification that the entire
native simulation or online service is defect-free.

## Repaired findings

1. **Unbounded receive work:** both UDP poll loops drained until the socket was
   empty. Sustained invalid traffic could keep the main thread inside the loop.
   Each poll now permits 64 receive attempts, counting rejected traffic and
   recoverable errors. Remaining datagrams are processed on subsequent calls.
2. **Oversized UDP traffic interrupted useful reception:** the GGPO poll stopped
   at `WSAEMSGSIZE`; the reachability probe canceled completely. Winsock consumes
   that datagram, so polling now continues within its budget. Asynchronous
   `WSAECONNRESET` notifications likewise do not prevent subsequent reception.
3. **TCP failure handling:** startup and nonblocking configuration failures were
   unchecked, and `select` errors looked like pending connects forever. Failures
   now release the socket/slot. Null or empty hosts and out-of-range ports are
   rejected before resolution.
4. **Invalid receive arguments closed healthy TCP connections:** a zero-length
   receive could be mistaken for EOF; invalid pointers/lengths could cause a socket
   error and close. These arguments now return `-1` without touching the socket.
5. **Probe numeric-prefix acceptance:** `strtoul` accepted a sign or valid decimal
   prefix followed by junk/fractional text, and overflow was unchecked. Parsing now
   checks the first digit, overflow, and the following JSON field delimiter.
   This hardens the existing narrow probe parser; it does not replace it with a
   general strict JSON parser.

## Evidence and validation

- `tests/run_core_native_tests.ps1`: guarded native TCP/probe, control parser,
  rollback schema/production serializer, FP, and associated static suites.
- `python -u tests/prematch_net_test.py`: full authenticated paired suite, including
  2,048-frame ordinary and uint32-wrap chaos, long correction after ring reuse,
  input/checksum confirmation, retry, tampering, and disconnect phases.
- New real-loopback GGPO regression queues an oversized datagram and 95 malformed
  packets; the production poll consumes 64 then 32 attempts. The probe regression
  similarly puts more than one budget of malformed traffic ahead of a valid pong.
- The complete production source/library set links to
  `build/SDL2_netcode_audit.dll`. This is a reviewable build artifact; the installed
  proxy DLL is not replaced by the audit build.
- `node --test tests/online_admin_server_test.js` and
  `python tests/online_admin_integration_static_test.py` cover the subsequent live
  admin TODO implementation, including protected edits, visibility, retry,
  non-overlapping requests, filtered snapshots, expired sessions, and CSRF.

Test logs are under `build/netcode_audit_*.log`. The paired harness uses a synthetic
simulation adapter, so passing it is not proof of native room/death/respawn
determinism. No game executable was launched. Browser discovery returned no
available browsers; dashboard visual acceptance is still pending.

## Open findings and limitations

- The last-room Vanilla 1 disconnect/desync remains unproven and unfixed. In the
  available `mods/desync_dump.log`, six paired tile dumps each differ in one
  decorative `0x0c` cell's icon bytes, while the summaries' other components and
  RNG agree. Current checksum canonicalization already masks those icon bytes.
  This older diagnostic evidence does not justify expanding the checksum mask or
  claiming it explains the current last-room report.
- The pre-existing P0 native pointer-dependent snapshot and simulation-field
  audit work in TODO.txt remains. The inspected terminal countdown/score fields
  are serialized, but every native reader/exclusion was not exhaustively proven.
- Existing release blockers remain: plaintext account/control TCP, IPv4-only P2P,
  incomplete compatibility/integrity policy, and production persistence/result
  hardening. DNS resolution still uses synchronous `getaddrinfo`; service-tick
  RTT, adaptive pacing, and broader chaos matrices remain backlog work.
- A receive budget bounds work; it does not guarantee gameplay progress under
  sustained link saturation. Congestion control and production network soaks are
  still required.
