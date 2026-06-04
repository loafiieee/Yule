# Eggnogg+ Online Server

This is the single control server for built-in online play. It handles:

- account registration and login
- session tokens
- public Elo
- private server-only MMR
- casual queue
- ranked queue with a hard private-MMR difference cap
- P2P match assignment
- match result/rating storage

Gameplay is not server-authoritative. The server only matches two players and tells one client to host the existing UDP rollback session while the other joins it.

## Run

```powershell
python online_server\server.py --host 0.0.0.0 --port 47778 --data online_server_data.json
```

Local test URL:

```text
http://127.0.0.1:47778
```

The ranked cap defaults to `250` private MMR:

```powershell
python online_server\server.py --ranked-cap 200
```

Environment variables:

- `EGGNOGG_ONLINE_HOST`
- `EGGNOGG_ONLINE_PORT`
- `EGGNOGG_ONLINE_DATA`
- `EGGNOGG_RANKED_MMR_CAP`

## API

All endpoints use JSON. Profile responses expose `elo`, never `mmr`.

- `GET /api/health`
- `POST /api/register`
- `POST /api/login`
- `POST /api/profile`
- `POST /api/queue/join`
- `POST /api/queue/cancel`
- `POST /api/queue/status`
- `POST /api/match/result`

Queue join body:

```json
{
  "token": "...",
  "queue": "ranked",
  "listen_host": "203.0.113.20",
  "listen_port": 47777
}
```

Matched response:

```json
{
  "ok": true,
  "status": "matched",
  "match_id": "...",
  "queue": "ranked",
  "opponent": "other_player",
  "role": "join",
  "player": 1,
  "peer_host": "203.0.113.10",
  "peer_port": 47777,
  "elo": 1000
}
```
