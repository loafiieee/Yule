# Eggnogg Online Server Prototype (Python)

This is a **working prototype backend** for multiplayer features:

- account registration/login with hashed passwords
- bearer access tokens + rotating refresh tokens
- friend requests + friend list
- ranked/casual queue + simple matchmaking
- match report/finalization flow with anti-stall timeout
- ranked ELO updates on finalized matches

> Notes:
> - Storage is currently in-memory for rapid prototyping.
> - This is not production-ready security, but establishes the architecture and flows.

## Quick start

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn app:app --reload --port 8787
```

Server runs at `http://127.0.0.1:8787`.

## API overview

- `POST /auth/register`
- `POST /auth/login`
- `POST /auth/refresh`
- `GET /me`
- `POST /friends/request`
- `POST /friends/accept`
- `GET /friends`
- `POST /queue/join`
- `POST /queue/leave`
- `GET /queue/status`
- `GET /match/{match_id}`
- `POST /match/{match_id}/report`

## Anti-boosting prototype rule

When a match ends:

1. either player can submit a winner report
2. if both agree -> finalize
3. if only one reports and opponent stays silent past timeout -> reporter wins by forfeit
4. if both report conflicting winners -> match becomes `disputed` (no ELO change)

This avoids the "I will refuse to confirm forever" exploit while also avoiding blind trust of a single winner claim.
