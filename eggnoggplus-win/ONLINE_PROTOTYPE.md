# Online Multiplayer Prototype Plan (Implemented Scaffold)

This repo now includes two prototype pieces:

1. `mods/online_play` (Lua):
   - adds a **Play Online** button under START on the main menu
   - opens an in-game **Online Hub** UI with prototype login/ranked/casual/friends/add-friend actions
2. `../online_server` (Python FastAPI):
   - provides auth, friends, queue/matchmaking, match reports, and ELO updates

## Current limitation

The current Lua mod framework does not yet expose built-in HTTP helpers, and LuaSocket is not wired by default.
So this prototype keeps menu/UI in-game and ships a separately runnable backend API.

A next step is to add a small bridge layer (C-side HTTP functions exposed to Lua), then wire each UI button directly to API calls.

## Mod quick usage

- Ensure `mods/online_play` exists.
- Launch game with framework.
- On main menu, click **Play Online**.
- In the hub:
  - login as Alpha/Bravo (prototype identities)
  - queue ranked/casual
  - view friend statuses/challenge buttons
  - send demo friend request

## Backend quick usage

See `../online_server/README.md`.

## Security direction

- Keep ranked constrained to strict client policy (approved build + approved mod set).
- Do not trust single-client winner claims.
- Use report timeout for forfeit resolution to prevent confirmation stalling.
