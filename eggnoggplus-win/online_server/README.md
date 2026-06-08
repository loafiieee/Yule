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
- P2P match setup with UDP hole-punch discovery; no relay fallback

Default bind is `0.0.0.0:47778` for TCP and UDP. For public internet play, the
host must allow inbound TCP `47778` and inbound UDP `47778`; set `UDP_PORT` or
`UDP_HOST` only if the discovery socket needs a different bind.

The server never sends cosmetics or cosmetic asset data. Match messages contain
only opponent identity, public Elo, P2P endpoint data, input delay, and the
server-selected map key/selector.
