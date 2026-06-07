# Eggnogg+ Online Server

Dependency-free Node TCP server for the built-in online hub.

```powershell
cd online_server
npm run check
$env:PORT='47778'
npm start
```

Protocol is newline-delimited JSON over TCP. The server handles:

- account registration and login
- public Elo and private server-only MMR
- casual queue with loose matching
- competitive queue with MMR range matching
- friend requests and friend list snapshots
- 5-minute friend challenges
- shared-map selection from each client's submitted map manifest
- P2P match setup; gameplay UDP remains direct peer-to-peer

Default bind is `0.0.0.0:47778`. Persistent users are stored in
`online_server/users.json` unless `DB` is set.

The server never sends cosmetics or cosmetic asset data. Match messages contain
only opponent identity, public Elo, P2P endpoint data, input delay, and the
server-selected map key/selector.
