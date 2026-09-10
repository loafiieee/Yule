# Eggnogg+ Online Server

Dependency-free Node server for the built-in online hub. TCP handles accounts,
friends, queues, challenges, and results. UDP handles direct P2P endpoint
discovery and a bounded match-owned relay fallback when direct traversal fails.

```powershell
cd online_server
npm run check
$env:PORT='47778'
npm start
```

Main protocol is newline-delimited JSON over TCP. The server handles:

- account registration and login
- public Elo and private server-only MMR
- casual queue with loose matching
- competitive queue with MMR range matching
- friend requests and friend list snapshots
- persistent private block lists and per-friend challenge-notification mutes
- server-derived friend presence for queues, match setup, and active gameplay
- 5-minute friend challenges with an authoritative compatible-map picker
- shared-map selection from each client's submitted map manifest
- symmetric P2P candidate publication/hole-punch discovery, followed by a bounded
  match-owned UDP relay when a fresh direct-path socket generation is required
- one random 256-bit `p2p_auth_token` per match for GGPO UDP v17 packet MACs

## Admin audit records

Maintenance actions append JSONL intent/outcome records to `admin-audit.jsonl`,
or `ADMIN_AUDIT_FILE`. Each pair shares an operation ID and records timestamp,
action, target account and LAN source address. Passwords, session cookies, ban
reasons, result messages and exception text are excluded. Writes are flushed
before proceeding. An intent-write failure blocks the action; an outcome-write
failure explicitly says the action may already have completed. A requested entry
without an outcome needs investigation, not automatic replay.

This is a local append-only operational history, not a tamper-proof ledger or a
transaction with the account stores. Administrators with filesystem access can
alter it, and a crash can interrupt the final record. Retain/rotate it according
to your operational needs. The updater preserves `.jsonl` runtime files.

Admin password, ban/unban, and rating maintenance persists a detached proposed
record before publishing it in memory or disconnecting/notifying clients. A
failed file replacement leaves the previous live record intact and produces a
failed audit outcome. Ordinary gameplay/social persistence is still a separate
work item; these changes do not make multi-file updates transactional.

After an interrupted audit append, the incomplete trailing line is retained and
separated from new records. Audit readers must flag malformed lines for operator
inspection, rather than silently treating the history as complete.

## Offline backup and restore

Stop the server before backup or restore. Use the same `DB`, `RATINGS`, and
`SECRET_FILE` environment paths as the service; defaults are files alongside
`server.js`. All three existing files must be valid. This tool supports file-backed
rating secrets; deployments using `RATING_SECRET` must preserve that secret through
their configuration backup instead. No secret bytes are printed.

```text
node maintenance.js backup /private/backups/yule-before-change --offline
node maintenance.js verify /private/backups/yule-before-change
node maintenance.js restore /private/backups/yule-before-change /private/backups/yule-before-restore --offline
```

Choose new snapshot/recovery directories beneath an existing private parent, outside
the application update tree. Backup preserves exact bytes and records SHA-256/size
checks for users, ratings and the signing secret. Verify detects damage, not a
maliciously rewritten manifest. Keep backups private: they contain password hashes
and signing material. Runtime audit logs and service environment configuration are
retained separately; restore never replaces audit history.

Restore validates the entire snapshot and saves a verified recovery copy before
changing destinations. Ordinary write failure attempts to recover all previous
files and reports whether that succeeded. Abrupt interruption is not a multi-file
transaction: keep the server stopped and restore the recovery snapshot before
restarting. `--offline` is your assertion that the service is stopped; the tool
cannot detect every supervisor or remote process. Existing backup directories
are never overwritten.

## Optional Discord LFG bridge

The server can mirror genuinely waiting casual/competitive players to one configured
Discord channel. It waits two seconds before creating one embed per genuinely waiting
player, then edits that exact post into an inactive state on leave, match, disconnect,
queue change, or clean shutdown. Direct friend challenges never post. Discord receives
only the public username and queue. Two link buttons enter the existing validated challenge or public-queue flow;
there is no direct match/token link.

The integration uses outbound Discord REST only and needs no npm dependency, Gateway
connection, intents, command handler, or inbound public bot port. Its HTTPS link handoff
runs on a separate loopback listener behind the deployment's TLS reverse proxy.

```ini
DISCORD_LFG_ENABLED=1
DISCORD_LFG_BOT_TOKEN=secret
DISCORD_LFG_CHANNEL_ID=123456789012345678
DISCORD_LFG_PUBLIC_BASE_URL=https://play.example.com/yule
DISCORD_LFG_POST_DELAY_MS=2000
LFG_REDIRECT_HOST=127.0.0.1
LFG_REDIRECT_PORT=47880
```

Keep the token in a mode-`0600` service environment file. The public base must be HTTPS
with the exact `/yule` path; proxy `/yule/` to the loopback redirect port. See
`../DISCORD_LFG_BOT.md` for permissions, proxy configuration, privacy rules, failure
behavior, and live acceptance.

## Optional LAN administration UI

The server includes a separate dependency-free maintenance UI for account lookup,
password resets, bans/unbans, forced disconnects, and rating resets. Its live dashboard
shows authenticated and pre-auth connection counts, player activity and compatible
client builds, both matchmaking queues and wait/rating windows, match participants,
phase/map/direct-or-relay state, pending challenges and rematches, complete friendship
pairs and friend requests, blocks, mutes, and searchable account details. It never
renders credential material, match authentication tokens, or raw P2P endpoints.

There is no login screen or admin password: access is granted only to clients arriving
from a private LAN address on a private-address listener. Set `ADMIN_ENABLED=0` to
disable it completely.

For same-machine access, keep the default loopback bind:

```ini
ADMIN_HOST=127.0.0.1
ADMIN_PORT=47779
```

For direct LAN access, set `ADMIN_HOST` to the server's exact private LAN address, such
as `192.168.1.50`, and allow only the administrator subnet:

```ini
[Service]
Environment=ADMIN_HOST=192.168.1.50
Environment=ADMIN_PORT=47779
```

Add those lines with `sudo systemctl edit eggnogg`, then apply them:

```bash
sudo systemctl daemon-reload
sudo systemctl restart eggnogg
sudo ss -ltnp | grep 47779
```

The admin listener is integrated into `server.js`; do not launch
`node admin_server.js` as a second process.

Admin requests must use `localhost` or a private literal IP in the Host header.
Public DNS names are rejected before a session is granted, preventing a DNS
rebinding page from gaining access through a private client. A reverse proxy must
forward a private literal Host to this listener and enforce its own authentication.

The dashboard refreshes activity, totals, queues, matches, social relationships,
and account rows every two seconds while the tab is visible. Search filters stay
applied. A section pauses while it contains a focused control, an open maintenance
panel, or an edited password/reason; other sections keep updating. Close the panel
and clear or submit the edit to resume that section. Interrupted requests retry
without clearing the last snapshot; expired sessions stop updates and ask for a
manual refresh. Maintenance actions still require an explicit form submission.

```bash
sudo ufw allow from 192.168.1.0/24 to any port 47779 proto tcp
```

Open `http://192.168.1.50:47779/` from that LAN. The listener rejects wildcard/public
binds and non-private client addresses by default. Do not port-forward this port or add
an `Anywhere` firewall rule. Plain HTTP is suitable only for a trusted isolated LAN; use
an SSH tunnel or an authenticated TLS reverse proxy across any untrusted network.
`ADMIN_ALLOW_WILDCARD=1` is an explicit unsafe override, not a normal deployment
setting. The UI still uses an automatic private-client session and CSRF token so an
unrelated webpage cannot submit maintenance actions. Set `ADMIN_COOKIE_SECURE=1` when
the browser always reaches the UI through HTTPS.

Default bind is `0.0.0.0:47778` for TCP and UDP. For public internet play, the
host must allow inbound TCP `47778` and inbound UDP `47778`; set `UDP_PORT` or
`UDP_HOST` only if the discovery socket needs a different bind.

For deployment checks, send UDP JSON `{"type":"udp_ping","seq":1}` to the
server port and expect `udp_pong`. Valid match probes log `[p2p#...] ... udp=`
when received and `[p2p#...] sent peer ...` when both peers have probed. If
matches are found over TCP but those P2P logs never appear, UDP is not reaching
the discovery socket or the remote process is not running this server code.

The server never sends cosmetics or cosmetic asset data. Match messages contain
opponent identity, public Elo, candidate data, input delay, the server-selected
map key/local selector, a per-user probe token, and the shared match packet-auth
token. Host/join decides player/initial-state authority only; both clients probe
and send HELLOs symmetrically.

If a direct attempt needs a fresh UDP socket, the server changes both clients to one
`relay` route on `UDP_PORT`. Relay packets are admitted only from the observed endpoint
owned by that active match and server-assigned player slot, are bounded to protocol-v17
packet sizes/types and per-second packet/byte budgets, and are forwarded one-for-one only
to the recorded opponent endpoint. The peer still verifies the original end-to-end HMAC
and replay sequence. A finalized relay match keeps only those existing authenticated
endpoints for 15 seconds so the native win presentation can finish, then revokes them;
setup aborts revoke immediately. Set `P2P_RELAY_ENABLED=0` only to diagnose direct
traversal. `RELAY_FINISH_GRACE_MS` is clamped to 1-30 seconds.

## Safe source update

From the deployed checkout, run:

```bash
cd /home/loaf/Yule/eggnoggplus-win/online_server
chmod +x update_server.sh
./update_server.sh
```

If a deployment still has the legacy updater and reports
`clone did not contain online_server/server.js`, bootstrap only the updater from
the tracked repository copy, validate it, and rerun:

```bash
cd /home/loaf/Yule
git fetch origin main
git show origin/main:eggnoggplus-win/online_server/update_server.sh \
  > /tmp/yule-update-server.sh
bash -n /tmp/yule-update-server.sh
install -m 0755 /tmp/yule-update-server.sh \
  eggnoggplus-win/online_server/update_server.sh
cd eggnoggplus-win/online_server
./update_server.sh
```

This recovery changes only the updater script. It does not check out or overwrite
the live server directory. The current updater discovers the project root from
the checked-out `online_server/server.js`, `package.json`, and `tests/` structure,
so both flat and nested repository layouts are accepted.

The updater clones and validates the latest `main` in a temporary directory before
stopping systemd, swaps only server application files, restarts, and runs the local
TCP/UDP deployment probe. It never stages `users.json`, `ratings.json`,
`server_secret.key`, logs, environment/PID/socket files, caches, or `node_modules`, and
restores the prior application files if startup or validation fails.

The restart probe waits up to 30 seconds for both listeners instead of treating
the first refused connection as a failed deployment. An unusually slow host can
use `YULE_READINESS_TIMEOUT_SECONDS=60 ./update_server.sh`. If readiness really
fails, the updater prints systemd status and the tails of the server logs before
performing the rollback.

## Deployment compatibility check

The server sends an unauthenticated, flat `server_info` welcome on every TCP
connection and answers an explicit `{"type":"server_info"}` request. Control
protocol 3 adds the counted friend-challenge map-intersection stream and
server-revalidated selected map. Match protocol 4 advertises:

```json
{"type":"server_info","control_protocol":3,"match_protocol":4,"p2p_protocol":17,"cap_p2p_auth":1,"cap_social_controls":1,"cap_private_rematch":1,"cap_p2p_relay":1,"cap_client_build_gate":1}
```

The same scalar version/capability fields are repeated in `auth_ok`, and each
`match_found` carries `match_protocol`, `p2p_protocol`, and the per-match
`p2p_auth_token`. These fields must remain scalar because the game intentionally
accepts only bounded flat control JSON.

The game client explicitly requests `server_info` after TCP completion and does not send
its login/register message until every required capability matches. It validates the
repeated `auth_ok` and `match_found` fields as well, so an old or partially restarted
deployment is rejected before queueing and again before peer setup.

`cap_client_build_gate:1` means the server requires each authenticated map manifest to
include its exact protocol tuple plus deterministic `build_id`, `game_exe_id`, and
`framework_dll_id` fingerprints. It will not pair clients unless all three fingerprints
match. The display-oriented framework version is logged for diagnosis but never replaces
this exact comparison.

`cap_p2p_relay:1` is required by the current client so an old discovery-only deployment
cannot silently strand strict-NAT players. A server started with
`P2P_RELAY_ENABLED=0` advertises zero and is intentionally rejected.

`cap_social_controls:1` is required by the current client. A block removes the mutual
friendship, both pending request directions, and challenges in either direction. Either
player's block makes new requests/challenges unavailable and prevents the pair from being
selected in casual or competitive matchmaking. The blocker's snapshot contains only the
blocked username—never that account's Elo or online state—and the other user receives no
explicit block reason. Unblocking does not restore friendship.

Mute is a private per-friend notification preference. Muted challenges remain in the
Friends inbox and can still be accepted, but carry `muted:1` so the receiving client does
not create a pop-up toast. Both lists are normalized and persisted in `users.json`.

`cap_private_rematch:1` is also required by the current client. After a committed
confirmed result, the server retains a 45-second exact-match offer only while both players
remain connected, idle, and mutually unblocked. `rematch_request` is a vote, not an
immediate match: the first player receives `rematch_waiting`, the opponent receives
`rematch_offer`, and only the second explicit vote produces `rematch_starting` plus a fresh
`match_found`. The normal matcher revalidates the exact previous map. A rematch is private
and unranked even when the prior game was competitive; it never rejoins a public queue or
changes Elo. `REMATCH_TTL_MS` may override the default for testing/deployment and is clamped
to 100 ms through five minutes.

Queueing, blocking, disconnecting, declining, expiry, or entering another match closes the
offer for both participants. Terminal-result replay recomputes remaining time and never
reopens a closed offer. Every rematch message carries the prior positive `match_id`; a
fresh rematch receives new match, rendezvous, packet-auth, role, and seed data.

Friend `presence` is derived only from authenticated server state: `offline`, `online`,
`queue_casual`, `queue_competitive`, `match_setup`, or `in_match`. Clients cannot publish
an arbitrary presence string. Friends receive refreshed snapshots at every queue and match
lifecycle boundary. A busy friend cannot be challenged, and blocked-user entries continue
to omit all presence.

P2P v17 is a wire-breaking client requirement, even though the control server only
advertises the number and relays match metadata. Its authenticated INPUT packet adds a
monotonic cumulative input ACK, a 512-bit selective input ACK, selective resend within 64
input slots, a generation-scoped cumulative checksum-comparison ACK, and the repeated
correction phase/snapshot/resume tuple used by the bilateral replay/release barrier.
Checksums send the live edge first and then retry from the oldest unacknowledged frame;
history retirement requires comparison proof from both peers. The fixed packet is 1,292
bytes and is compile-time limited to at most 1,400 bytes. A v16 client must therefore be
rejected instead of being matched with a v17 client.

Correction coordination and canonical x87/MXCSR tick controls are client runtime features;
the server neither performs rollback nor validates game-state blobs. The standalone
canonical rollback envelope in the client is foundation-only. Production still needs its
native typed capture/reconstruction/transaction adapter and remaining checksum-field audit.

The checked-out server source advertises v17 plus the required relay capability. Every
deployment must restart the service and pass the TCP+UDP deployment preflight; replacing
files without restarting is not sufficient.

Match protocol 4 retains the server-authoritative gameplay-start barrier. After the
authenticated P2P link, authoritative state, and neutral frame zero are fully
ready, each client sends `match_started`. The server broadcasts
`match_started` with `committed:1` only after both assigned clients are ready;
clients remain on the hub match card until that commit. Before commit,
`match_abort`, disconnect, timeout, or an accidental `match_end` cancels setup
for both players without a winner, result screen, or Elo change. After commit,
an abort/disconnect is a forfeit. Normal completion is settled only when both
clients report the same winner; a conflicting pair or an unconfirmed single
report times out as a no-contest and cannot change Elo.
An established P2P socket failure is reported separately as
`match_transport_failure`; it is not converted into a local loss. One report
waits through the normal result-confirmation window, while two transport
failures resolve immediately as a no-contest. A native winner already committed
on the same tick still uses `match_end`.
Deliberately leaving committed gameplay uses the forfeit path, so the connected
opponent is awarded the win without needing a second normal result report. A
late P2P-disconnect report that races that terminal result replays the client's
exact most-recent terminal message; it is idempotent and does not produce an
`invalid or stale match result` error.
`MATCH_REPORT_TIMEOUT_MS` controls that confirmation window and defaults to
10 seconds; the legacy `MATCH_SINGLE_REPORT_GRACE_MS` environment name remains
an override alias for deployment compatibility, but no longer awards a lone
report.
Every active lifecycle message requires the exact positive current `match_id`.
Only a connected participant's single most-recent terminal result may be
replayed, and a new match clears that replay record, so a delayed message from
an older match cannot cancel or finish a newer one.

The server closes a control socket after 120 seconds without client traffic.
Authenticated clients send a small JSON `ping` every 30 seconds and accept the
matching `pong`, so an idle hub or a long match is not mistaken for a disconnect.
This heartbeat is control-plane liveness only; gameplay remains direct P2P.

Run the preflight against the public deployment (the defaults require no
arguments), or name another host and port:

```powershell
python check_deployment.py
python check_deployment.py 127.0.0.1 47778
python ..\tests\online_server_match_protocol_test.py
python ..\tests\discord_lfg_server_static_test.py
npm test
```

The probe validates the TCP protocol/capability response, including the client-build gate,
and the UDP
discovery `udp_pong`. It fails clearly if the listening process is older than
the checked-out source or UDP is unavailable. Updating `server.js` on disk is
not sufficient: the running Node process must be restarted.

Safe server-only deployment sequence:

Existing account/ratings stores that are unreadable, malformed, or have invalid
record shapes now stop startup. They are never replaced with an empty database.
Back up the damaged file and repair or restore it before restarting. A missing
store still initializes normally. Account and ratings writes use a flushed sibling
file followed by replacement; the two stores are not a single transaction.
An invalid or unwritable rating secret also stops startup instead of silently
generating a replacement secret and invalidating stored signatures.

1. Confirm or drain active TCP clients.
2. Run `npm run check` and `npm test` locally.
3. Stage the complete matching application set (`server.js`, `storage.js`, `admin_server.js`, `admin_audit.js`, `maintenance.js`, LFG
   modules, package metadata, and scripts); do not replace `users.json`, `ratings.json`,
   `server_secret.key`, external admin-password/environment files, or their paths.
4. Run the same checks on the staged remote application.
5. Back up the deployed application files, atomically install the checked set, and
   restart it through the deployment's existing supervisor/process mechanism.
6. Confirm both TCP and UDP listeners, then run `check_deployment.py` against
   the public hostname.

The current public host is managed by `eggnogg.service` with `Restart=always`.
Restart that service (or terminate its reported `MainPID` and let systemd restart
it); do not launch a second `nohup node server.js`, which will only contend for
TCP/UDP port 47,778.

Do not restore compatibility with an old server by allowing unauthenticated
GGPO packets. A missing match token must continue to abort before opening the
peer session.

The TCP control socket is currently raw newline-delimited JSON. It has no TLS or
server certificate validation, so passwords and match tokens remain exposed to
an on-path attacker even though v17 rejects off-path UDP spoofing. Put the
service behind a future authenticated TLS control endpoint before treating it
as production-secure.
