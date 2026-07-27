# Authenticated P2P Packet Transport

**Date:** 2026-07-17  
**Protocol:** GGPO UDP v17<br>
**Primary code:** `ggpo_net.c`, `ggpo_net.h`, `hooks.c`, `online_server/server.js`  
**Status:** v17 client/server source, packet-layer wiring, deployment preflight, and server runtime protocol coverage implemented; the live public service still advertises v16, so coordinated v17 deployment and real two-machine gameplay/retry acceptance remain

## Outcome

Every GGPO peer datagram is authenticated before it may influence the selected
peer address, remote session, handshake, state transfer, cosmetics, rollback,
input, or disconnect state. A random Internet sender cannot win the first-HELLO
race, replace a pre-match session, inject inputs/state, or replay an old packet
without the current match secret.

This layer provides peer-packet authentication and integrity. It does **not**
encrypt packet contents, hide traffic metadata, or make the existing control
connection secure.

## Match secrets

The server creates two different kinds of secret and they must not be confused:

- `p2p_token` remains a per-user capability used only to authorize that user's
  UDP discovery probe at the server.
- `p2p_auth_token` is one 256-bit value created with
  `crypto.randomBytes(32).toString("hex")` and sent identically to both peers in
  their `match_found` messages. It is the shared input to peer-packet
  authentication.

`ggpo_net_set_match_token()` accepts exactly 64 hexadecimal characters, decodes
them to 32 bytes, and immediately derives the root authentication key. The net
layer does not retain or log the token text. Invalid replacement input clears
any previously armed key so a stale match key cannot be used accidentally.

The derived pending key is consumed by one successful `ggpo_net_start_*()` call.
Every fresh-socket retry must therefore call `ggpo_net_set_match_token()` again.
The in-session root and direction keys are erased with `SecureZeroMemory` during
teardown. The online owner must also erase its `p2p_auth_token` text buffers when
the match is established, cancelled, rejected, or completed.

Starting a peer session without an armed token fails closed. Direct developer
host/join uses `ggpo.net key`: it reads the shared 64-hex value from the
clipboard, requires successful clipboard clearing, derives the one-shot key,
and erases the temporary text. The secret is never typed into console history,
persisted, or logged. F6/F7 and direct host/join then consume that armed key;
they never fall back to unauthenticated v15 traffic.

## Wire format and key schedule

Protocol v17 gives every P2P packet type this packed common prefix:

```text
magic:u32 | version:u16 | type:u16 | session_id:u64 | sender_player:u32 |
sequence:u64 | tag:16 bytes
```

All integer wire values and KDF context integers use the existing Windows/x86
little-endian representation. The protocol remains version-locked; packets with
another version are ignored before dispatch.

Windows CNG (`BCrypt`) performs HMAC-SHA-256. Domain strings are exact ASCII bytes
without a trailing NUL:

```text
root = HMAC-SHA-256(
  key = decoded p2p_auth_token,
  data = "EGGNOGG+ GGPO v17 match auth root"
)

direction_key = HMAC-SHA-256(
  key = root,
  data = "EGGNOGG+ GGPO v17 direction key" ||
         version:u16 || sender_player:u32 || session_id:u64
)

full_tag = HMAC-SHA-256(
  key = direction_key,
  data = "EGGNOGG+ GGPO v17 packet tag" ||
         datagram_length:u32 || datagram_bytes_with_tag_zeroed
)

wire_tag = first 16 bytes of full_tag
```

The 128-bit wire tag keeps the fixed 60 Hz input datagram at 1,292 bytes, below the
project's compile-time 1,400-byte safety ceiling, while retaining a 128-bit online-forgery
bound. The sender role and session are both authenticated and included in direction-key
derivation. A receiver accepts only its expected remote role, preventing a
captured outbound packet from being reflected back into its sender.

V17 also authenticates the entire input-confirmation envelope: flags, cumulative ACK base,
mutually confirmed checksum frame, and sixteen 32-bit words forming a 512-bit selective
ACK. It additionally authenticates generation-scoped cumulative `checksum_ack_next`, which
proves successful in-order comparison rather than mere receipt. The 64 input slots
prioritize the current live edge and then the oldest unacknowledged hole; checksum slots
send their live edge first and then retry from the oldest unacknowledged checksum. Bumping
both the wire version and all three authentication domains is intentional: even if a match
token were reused accidentally, a v16 datagram cannot verify as a v17 packet or be
interpreted under the new ACK semantics.

Every peer protocol type uses this path: HELLO, INPUT, BYE, state chunks and
acknowledgements, resync requests, cosmetic profile packets, and cosmetic asset
chunks. The server discovery JSON probe is not a peer protocol packet and keeps
its separate per-user authorization token.

## Session entropy, replay handling, and endpoint adoption

- Each socket attempt gets a nonzero 64-bit session ID from
  `BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)`. RNG failure aborts start;
  there is no timestamp, address, or game-RNG fallback.
- Each sender starts a 64-bit sequence at 1 and increments it for every logical
  peer datagram before loss/delay simulation.
- The receiver maintains an exact 4,096-sequence replay window per authenticated
  remote session. A sequence already seen is rejected. A sequence at least 4,096
  behind the highest accepted value is rejected. Previously unseen out-of-order
  packets inside the window remain valid for normal UDP reordering.
- Authentication, expected-role validation, and constant-time tag comparison
  happen before any packet handler runs.
- The first authenticated remote session must arrive in a HELLO. Before strict
  link confirmation, an authenticated HELLO may select the server-chosen
  loopback/LAN/public route. A same-session packet from another endpoint cannot
  migrate a confirmed pin.
- During server-managed prematch only, a confirmed frame-zero peer may adopt an
  authenticated HELLO carrying a fresh remote session ID before the synchronized
  start state is loaded. This lets one peer's bounded fresh-socket retry recover
  without accepting alternate-source traffic from the old session. Retired
  session IDs cannot become current again, and migration is disabled once start
  state has been loaded or gameplay has advanced.

Authentication failures are silently dropped to avoid log amplification. A
saturating `ggpo_net_auth_rejected_packets()` counter and `net.diag` authentication
line provide diagnostics without exposing key material.

## Online client integration contract

The online flow must:

1. Parse `p2p_auth_token` into dedicated 65-byte pending and retry-state buffers.
2. Copy it from the pending match to the retry owner, never to long-lived match
   history or result state.
3. Require and pass it to `ggpo_net_set_match_token()` immediately before every
   host/deferred-join start, including fresh-socket retries.
4. Treat missing, malformed, or rejected tokens as a pre-match setup failure.
5. Never include it in logs, status strings, diagnostics, crash text, or probe
   JSON; the separate `p2p_token` remains the probe credential.
6. Securely erase owner buffers when no further retry can occur.

## Deployment compatibility contract

Packet authentication is a coordinated client/server protocol change. After TCP
completion the current client requests `server_info` and validates the exact control,
match, P2P, and packet-auth capability versions before sending login credentials. It
revalidates `auth_ok`, then rechecks the match/P2P versions and required
`p2p_auth_token` in `match_found`. An old or partially restarted server therefore fails
during the pre-authentication handshake; the later prematch checks remain defense in
depth and never open an unauthenticated peer session.

The server makes deployment state observable without an account. On connect and
in response to `{"type":"server_info"}`, it sends one flat object containing:

```text
control_protocol=3 | match_protocol=3 | p2p_protocol=17 | cap_p2p_auth=1
```

`auth_ok` repeats all four scalar fields. Each `match_found` repeats the match
and P2P protocol numbers and includes the shared token. Arrays/nested capability
objects are forbidden because the strict client control parser accepts only
flat bounded JSON.

`online_server/check_deployment.py` probes those fields without logging in and
also requires the UDP discovery socket to answer `udp_ping`. Its default target
is `eggnogg.loafiieee.com:47778`; host and port may be overridden positionally.
An absent `server_info` is reported as an outdated deployment. This is an
operations check, not a substitute for the two-client runtime test. There is
deliberately no legacy unauthenticated fallback.

## Security boundary and explicit limitations

The control client/server connection currently sends newline-delimited JSON over
raw TCP without TLS or server authentication. An on-path attacker can observe or
replace `match_found`, steal `p2p_auth_token`, and then forge v17 UDP packets. The
UDP MAC closes off-path spoofing/injection and accidental cross-match traffic; it
does not repair that control-plane weakness. Authenticated TLS for the control
channel remains required for end-to-end server and match security.

The UDP payload is plaintext. Inputs, state chunks, cosmetic data, packet sizes,
addresses, and timing remain observable. Either legitimate peer possesses the
shared key and can construct valid packets. This design does not defend against a
malicious authenticated opponent or a compromised server/client process.

## Verification evidence

Run:

```powershell
python tests\prematch_net_test.py
python tests\online_server_auth_static_test.py
python tests\online_server_match_protocol_test.py
python tests\online_flow_integration_static_test.py
python online_server\check_deployment.py
```

The test compiles 32-bit C11 with `-Wall -Wextra -Werror`, `GGPO_NET_TEST`,
WinSock, and BCrypt, then uses real localhost UDP sockets to cover:

- same-key host/join handshake, held countdown service, authoritative state
  synchronization, and immediate first visible gameplay tick;
- one-shot state-load failure followed by successful authenticated retransmit;
- missing-key start rejected before a socket session becomes active;
- byte-for-byte replay of each otherwise-valid host datagram: gameplay still
  connects through the originals while the join peer rejects every duplicate;
- different match keys on each peer: neither side connects and both record MAC
  rejections;
- a same-key host whose packets are modified after tag generation: the join peer
  rejects every modified datagram and neither side connects.

The v17 deterministic preflight in the same runner additionally verifies monotonic
contiguous and peer-acknowledged horizons, 512-bit SACK coverage and wraparound,
live-edge/oldest-hole resend ordering, immutable same-frame input, guarded ring retirement,
permanent input-recovery stalls, generation-scoped cumulative checksum ACK validation,
oldest-unacknowledged checksum retry, dual-proof retirement stalls, and bounded checksum
no-progress disconnect. Transaction and skew modes cover complete received-state
prevalidation/rollback plus preservation of newer prematch input and ACK state. The paired
correction mode now completes the mutually confirmed snapshot/replay/release protocol under
loss, delay/reordering, authenticated duplicate replay, and a forced state-chunk
would-block. It also requires exact immutable input-ring commands to outrank stale retained
pre-rollback history and verifies matching correction tuple/transcript plus bilateral
post-correction checksum proof.
The second paired chaos mode starts from a synchronized test epoch at
`UINT32_MAX - 1023`, sends all measured inputs through the same authenticated lossy path,
byte-compares 2,048 finalized canonical states and checksums across frame zero, and drains
both cumulative checksum proofs through frame 1,023.
The long correction mode enables the authenticated lossy/reordered path from frame one,
injects one-sided canonical divergence at frame 600 after ring reuse, completes the same
REQUEST/snapshot/replay/release proof with duplicate replay and a forced blocked state
chunk, and continues both roles through checksum-confirmed frame 2,047.

The original v16 authentication cases were observed passing on 2026-07-17. The current
v17 runner retains those cases and adds the deterministic preflight above. A full DLL link
also requires `-lbcrypt`, which is already present in `compile.sh`.

The dynamic server test starts an isolated Node process with temporary account,
rating, and secret files; probes `server_info`; registers two clients; matches
them; and proves they receive the same 64-hex packet-auth token, different
per-user rendezvous tokens, opposite roles, and exact protocol versions. Static
tests additionally guard token issuance plus short-lived hook ownership, strict
parsing, per-attempt re-arming, and secure text-buffer erasure.

### Public deployment record

The deployment entries below are historical v16 records and remain labeled with the
versions actually observed at those times. They are not evidence that a running service is
compatible with the current v17 client; the checked-out deployment probe now requires
`p2p_protocol=17`. The live public service still advertises v16, so its coordinated v17
replacement, restart, and TCP+UDP preflight remain pending.

On 2026-07-17, the match failures numbered 120 through 123 were traced to the
public process still running the June 21 server file, which did not contain
`p2p_auth_token`. There were zero established control clients at deployment.
Only `server.js` was replaced; accounts, ratings, and the server secret were not
touched.

- Service: `eggnogg.service` on `loaf-server1`
- Deployed file: `/home/loaf/Yule/eggnoggplus-win/online_server/server.js`
- Deployed SHA-256: `943c4a36f749da6f6ee246dd7e1fd60d87471fbd9c6e222de01a060126e908a7`
- Backup: `/home/loaf/Yule/eggnoggplus-win/online_server/server.js.backup-20260717-234114`
- Backup SHA-256: `cf910c469e5f76340ead83d85cac2027469c2ef7180f1b719eed16b9b5caf9df`

The systemd service restarted successfully with both TCP and UDP bound on
47,778. The public deployment probe returned control v2, match v2, P2P v16,
packet authentication, and UDP discovery available.

Exact rollback command (run from the development machine):

```powershell
ssh loaf@192.168.0.143 'cd /home/loaf/Yule/eggnoggplus-win/online_server && cp -p -- server.js.backup-20260717-234114 server.js.rollback && mv -- server.js.rollback server.js && kill -TERM "$(systemctl show -p MainPID --value eggnogg.service)"'
```

### Match-v3 start-barrier deployment

On 2026-07-18, the deterministic-route and two-client READY/commit server was
deployed after the paired prematch and isolated server lifecycle suites passed.
There were zero established control clients immediately before replacement.
Only `server.js` was changed; accounts, ratings, and the server secret were not
touched.

- Service: `eggnogg.service` on `loaf-server1`
- Deployed file: `/home/loaf/Yule/eggnoggplus-win/online_server/server.js`
- Deployed SHA-256: `68ebbb70d3a7643e687898fceeff8c1bb4cce4d43b63bcc29a9a7ffa8e63328e`
- Backup: `/home/loaf/Yule/eggnoggplus-win/online_server/server.js.backup-20260718-012324`
- Backup SHA-256: `943c4a36f749da6f6ee246dd7e1fd60d87471fbd9c6e222de01a060126e908a7`

Systemd restarted the service under a new process. Both the LAN endpoint and
`eggnogg.loafiieee.com:47778` then passed the TCP+UDP probe with control v2,
match v3, P2P v16, packet authentication, and UDP discovery available.

Exact rollback command for this deployment:

```powershell
ssh loaf@192.168.0.143 'cd /home/loaf/Yule/eggnoggplus-win/online_server && cp -p -- server.js.backup-20260718-012324 server.js.rollback && mv -- server.js.rollback server.js && kill -TERM "$(systemctl show -p MainPID --value eggnogg.service)"'
```

### Match-result settlement hardening deployment

Later on 2026-07-18, a focused follow-up removed unilateral and
insertion-order result settlement: normal completion now requires two reports
that name the same winner, while a single-report timeout or conflict is a
no-contest with no Elo mutation. The isolated lifecycle suite covered both
negative cases and the agreed-result path before deployment. There were zero
established control clients, and only `server.js` was replaced.

- Deployed SHA-256: `4ee2c51e02d0814966d8a5ad0d62fafadd13549676ffc51e4b5aa0e1757cd173`
- Backup: `/home/loaf/Yule/eggnoggplus-win/online_server/server.js.backup-20260718-111345`
- Backup SHA-256: `68ebbb70d3a7643e687898fceeff8c1bb4cce4d43b63bcc29a9a7ffa8e63328e`

Systemd restarted under a new PID. The deployed hash matched the reviewed
source, and both LAN and public TCP+UDP probes again reported control v2,
match v3, P2P v16, packet authentication, and UDP discovery available.

Exact rollback command for this follow-up:

```powershell
ssh loaf@192.168.0.143 'cd /home/loaf/Yule/eggnoggplus-win/online_server && cp -p -- server.js.backup-20260718-111345 server.js.rollback && mv -- server.js.rollback server.js && kill -TERM "$(systemctl show -p MainPID --value eggnogg.service)"'
```

Manual acceptance still required after a coordinated v17 server/client deployment:
complete one real server-issued match across two game processes, force one fresh-socket
retry, and confirm both normal connection and `net.diag` authentication counters without
any secret appearing in logs.
