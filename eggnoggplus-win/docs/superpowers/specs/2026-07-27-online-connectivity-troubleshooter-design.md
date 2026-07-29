# Online Connectivity Troubleshooter Design

**Date:** 2026-07-27  
**Status:** Source implementation and focused automated coverage complete; live
network acceptance remains required  
**Primary code:** `net_ext.c`, `net_ext.h`, online/console integration in `hooks.c`,
`tests/net_ext_test.c`, and `tests/online_troubleshooter_static_test.py`

## Goal and command surface

`online.troubleshoot` (alias `net.trouble`) gives a player an actionable first-pass
answer when login or peer connectivity works for others but not for their machine. It
runs independently of authentication and never reads or prints credentials. `net.diag`
remains the instantaneous secret-free match/route/packet snapshot; it does not start
network traffic.

The active test starts from the console and is pumped once per normal game update. DNS
resolution and socket creation happen only at start. TCP completion, UDP reply handling,
timeouts, reporting, and cleanup are frame-driven, so the five-second diagnostic window
does not block the game loop or pause an online match. Starting it again cancels and
replaces the previous test. Because the console is available before the Online screen has
ever opened, command startup first loads and clamps the same persisted/default hub
configuration. A zero-initialized `0:0` target is never probed.

## Probes

The test targets the configured online server host and port:

- a separate nonblocking TCP connection checks control/login reachability without
  changing the live authenticated control connection;
- a separate nonblocking UDP socket sends the server's bounded `udp_ping` request;
- only a `udp_pong` from the resolved target address/port with the exact sequence,
  valid observed host, and valid observed port completes the UDP test;
- the result includes round-trip wall time and the public source address/port observed
  by the server.

All temporary sockets close on success, failure, restart, timeout, and framework
shutdown. The UDP parser accepts only the small closed reply shape and never treats an
unrelated datagram as success.

## Windows network evidence

`GetAdaptersAddresses` inspects active non-loopback IPv4 adapters. After resolving the
configured server, `GetBestInterface` identifies the interface Windows would actually use
to reach that server. If Windows cannot supply a target route, fallback selection prefers
a non-VPN adapter by gateway presence and IPv4 metric before considering any virtual VPN
adapter. The report includes its friendly name, address, active-adapter count, and whether
it is the exact server route.

VPN reporting is deliberately heuristic. Tunnel/PPP interface types and known VPN
adapter-name markers are evidence, not proof; absence is also not conclusive. A virtual
adapter such as Radmin may be active without carrying Eggnogg traffic. In that case the
report says it is not the route Windows selected, rather than blaming it or recommending
that it be disabled. A local
address in `100.64.0.0/10` is reported as likely CGNAT. An RFC1918 address proves that
ordinary address translation exists, but the PC cannot distinguish a normal home router
from carrier/upstream NAT. When UDP succeeds, the report tells the player to compare the
router's WAN IPv4 against the server-observed address. It never claims that a remapped
UDP port alone proves CGNAT.

## Result guidance

The four TCP/UDP combinations distinguish a broadly blocked server, a UDP-specific
firewall/VPN/router problem, a TCP control problem, and healthy basic server
connectivity. On healthy TCP+UDP with match failure, the player is directed to run
`net.diag` during the failed peer attempt. Every active run automatically appends that
current snapshot, including the server state, exact match ID, selected route, retry
count, candidates, packet counters, compatibility state, and transport verdict.

## Verification

The native transport test owns a loopback UDP responder and proves exact sequence/source
validation, observed-address parsing, result delivery, and socket cleanup. It also smoke
tests Windows adapter discovery. Static coverage pins command separation, persisted-target
initialization, nonblocking pump ownership, TCP+UDP calls, privacy exclusions, exact
route selection with non-VPN fallback, adapter/VPN/CGNAT evidence, build
linkage to `iphlpapi`, and automatic `net.diag` output. Manual acceptance must run the
command on ordinary NAT, with a VPN enabled/disabled, with UDP blocked, and on a known
CGNAT/mobile connection.
