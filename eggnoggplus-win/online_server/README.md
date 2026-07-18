# Eggnogg+ Online Server

Dependency-free Node server for the built-in online hub. TCP handles accounts,
friends, queues, challenges, and results. UDP is used only for direct P2P
endpoint discovery; gameplay packets still go directly peer-to-peer.

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
- 5-minute friend challenges
- shared-map selection from each client's submitted map manifest
- symmetric P2P candidate publication/hole-punch discovery; no relay fallback
- one random 256-bit `p2p_auth_token` per match for GGPO UDP v16 packet MACs

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

## Deployment compatibility check

The server sends an unauthenticated, flat `server_info` welcome on every TCP
connection and answers an explicit `{"type":"server_info"}` request. Match
protocol 2 advertises:

```json
{"type":"server_info","control_protocol":2,"match_protocol":2,"p2p_protocol":16,"cap_p2p_auth":1}
```

The same scalar version/capability fields are repeated in `auth_ok`, and each
`match_found` carries `match_protocol`, `p2p_protocol`, and the per-match
`p2p_auth_token`. These fields must remain scalar because the game intentionally
accepts only bounded flat control JSON.

The game client explicitly requests `server_info` after TCP completion and does not send
its login/register message until every required capability matches. It validates the
repeated `auth_ok` and `match_found` fields as well, so an old or partially restarted
deployment is rejected before queueing and again before peer setup.

Run the preflight against the public deployment (the defaults require no
arguments), or name another host and port:

```powershell
python check_deployment.py
python check_deployment.py 127.0.0.1 47778
python ..\tests\online_server_match_protocol_test.py
```

The probe validates both the TCP protocol/capability response and the UDP
discovery `udp_pong`. It fails clearly if the listening process is older than
the checked-out source or UDP is unavailable. Updating `server.js` on disk is
not sufficient: the running Node process must be restarted.

Safe server-only deployment sequence:

1. Confirm or drain active TCP clients.
2. Run `node --check server.js` locally.
3. Copy only `server.js` to a temporary file beside the deployed copy; do not
   replace `users.json`, `ratings.json`, `server_secret.key`, or their paths.
4. Run `node --check` on that temporary remote file.
5. Back up the deployed `server.js`, atomically rename the checked file into
   place, and restart it through the deployment's existing supervisor/process
   mechanism.
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
an on-path attacker even though v16 rejects off-path UDP spoofing. Put the
service behind a future authenticated TLS control endpoint before treating it
as production-secure.
