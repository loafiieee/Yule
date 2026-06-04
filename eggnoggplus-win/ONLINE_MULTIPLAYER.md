# Online Multiplayer / Rollback Roadmap

This document explains how the current online multiplayer work is structured and what still needs to be added before it is a real GGPO-backed online mode.

## Current Baseline

The game currently has a working GGPO-style rollback prototype:

- deterministic save/load/checksum layer
- rollback state capture and restore
- input injection for player 0 and player 1
- local rollback probes
- callback-shaped local session
- custom UDP host/join test session
- host-authoritative initial state sync
- input prediction and rollback/replay
- checksum-based desync detection

The custom UDP session is a prototype harness. It proves the game can run with rollback semantics, but it is not the final networking layer. The final implementation should wire the existing rollback callbacks into real GGPO.

## Important Files

- `ggpo_ext.c` / `ggpo_ext.h`
  - Shared rollback API used by tests, loopback, local session, and net session.
  - Saves/loads game state through `lua_manager`.
  - Advances exactly one game tick with injected player inputs.
  - Computes rollback checksums.

- `lua_manager.c` / `lua_manager.h`
  - Owns full game-state serialization.
  - Captures native globals, player structs, thing arrays, tilemap data, room state, particle state, and transient game state.
  - Applies rollback snapshots back into the native game.
  - Canonicalizes process-local or non-gameplay values before checksum comparison.

- `ggpo_loopback.c` / `ggpo_loopback.h`
  - F3 live rollback verifier.
  - Saves history, replays recent frames, and checks that replayed state matches original state.

- `ggpo_local.c` / `ggpo_local.h`
  - F4 callback-shaped local session.
  - Stand-in for the callbacks real GGPO will use: begin, save, load, free, advance.

- `ggpo_net.c` / `ggpo_net.h`
  - F6/F7 custom UDP peer prototype.
  - Handles host/join, host state sync, input exchange, prediction, rollback, and checksum exchange.

- `hooks.c`
  - Hotkeys, console commands, tick hook, raw input override, and per-frame routing into loopback/local/net sessions.

## Hotkeys And Commands

Do not use F1, F2, F9, F10, F11, or F12; the base game uses them for development controls.

- F2: `ggpo.roundtrip`
  - Saves current state, reloads it, and checks that the rollback checksum survives the roundtrip.

- F3: `ggpo.loopback`
  - Toggles live loopback rollback verification.

- F4: `ggpo.local`
  - Toggles callback-shaped local session.

- F6: `ggpo.net host`
  - Starts UDP host on port `47777` as player 0.

- F7: `ggpo.net join 127.0.0.1 47777`
  - Joins localhost host as player 1.

Console commands:

- `ggpo.roundtrip`
- `ggpo.selftest [frames]`
- `ggpo.loopback`
- `ggpo.local`
- `ggpo.net host [port]`
- `ggpo.net join <host> [port] [local_port]`
- `ggpo.net delay [frames]`
- `ggpo.net advantage [frames]`
- `ggpo.net predict [frames]`
- `ggpo.net highping [frames]`
- `ggpo.net smoothping [frames]`
- `ggpo.net correction [on|off]`
- `ggpo.net sim [loss_pct] [min_delay] [max_delay]`
- `ggpo.net off`
- `ggpo.net status`

## Rollback Model

The rollback model is intentionally close to GGPO:

1. Capture a compact game snapshot before a frame.
2. Feed deterministic inputs for player 0 and player 1.
3. Run exactly one native game tick.
4. Save/checksum the resulting state.
5. If remote input arrives late and differs from prediction:
   - load the saved pre-frame state
   - replay frames with corrected inputs
   - update frame history and checksum state

`ggpo_ext_advance_frame()` blocks normal raw input, injects per-player input for the tick, runs one simulation tick, clears the input override, and optionally computes the rollback checksum.

Rollback replay can suppress one-shot native sound effects so replays do not spam audio. Live frames should not be globally muted.

## Determinism Rules

For rollback to work, every gameplay-affecting value must be either:

- fully deterministic from prior state and inputs, or
- captured/restored in rollback state, or
- canonicalized out only if it truly cannot affect gameplay.

Values that have already mattered:

- RNG seed
- native game tick counter
- player structs
- active room / old room / room lerp state
- camera state
- thing count and thing array
- tilemap dimensions and data pointer contents
- transient game state
- leader/loser/controller/player pointer fields
- waterfall/effect pointers if they leak into checksum or simulation

Do not fix desyncs by blindly zeroing fields. First decide whether the field is:

- gameplay state that must be saved/restored
- process-local pointer state that must be remapped after load
- render/audio-only state that can be excluded from rollback checksums

## What Real GGPO Needs

The existing code already has most of the game-side pieces GGPO needs. The next step is replacing or bypassing the custom UDP harness with GGPO sessions and callbacks.

Required GGPO callback mapping:

- `begin_game`
  - Initialize an online match and lock the chosen rules/map/player slots.

- `save_game_state`
  - Call the existing rollback save path and return the serialized state buffer plus checksum.

- `load_game_state`
  - Call the existing rollback load path.

- `free_buffer`
  - Free state buffers allocated by `save_game_state`.

- `advance_frame`
  - Poll GGPO synchronized inputs, inject player commands, advance one native tick, and report checksum.

- `on_event`
  - Handle connected, synchronizing, synchronized, running, interrupted, resumed, disconnected, and timesync events.

Implementation tasks:

- Add GGPO headers/library to the build.
- Create a `ggpo_session.c` layer separate from the current `ggpo_net.c` prototype.
- Keep `ggpo_ext.c` as the game-side callback implementation.
- Translate local input bitmasks into GGPO inputs each frame.
- Translate GGPO synchronized inputs back into the current `GgpoFrameInputs`.
- Use GGPO disconnect/time-sync advice instead of the custom frame-advantage throttle.
- Keep checksum logging for desync diagnostics.

## Match Start And State Sync

The final online flow should not depend on two already-running game instances being in similar states.

Required match-start data:

- game version/build hash
- framework version
- GGPO protocol version
- map id and map checksum
- ruleset/mode
- score target
- RNG seed
- starting room
- spawn positions
- player assignment
- enabled/disabled mod policy

Recommended flow:

1. Player logs in before entering online features.
2. Player enters a ranked/casual queue or, later, accepts a friend challenge.
3. Backend matches two players and creates a signed match config.
4. Backend chooses player slots, seed, map/rules, cosmetics metadata, and transport route.
5. Both clients verify version, assets, map, rules, cosmetic availability, and mod policy.
6. Both clients enter the match through the same deterministic start path.
7. GGPO begins at frame 0 after both clients are synchronized.

Host full-state sync can remain as a debug or recovery tool, but production online should prefer deterministic match initialization over transferring arbitrary live state.

## Chosen Production Direction

The finished frontend should not expose a host/join menu. Players should see an online hub, queue buttons, account status, and later friend/challenge UI. The server decides who gets matched and how the match connects.

Use a hybrid transport model:

- backend for accounts, sessions, queues, MMR, matchmaking, cosmetics metadata, match configs, results, and friend systems
- direct P2P UDP gameplay when both clients can connect directly and the route is good
- relay gameplay fallback when direct P2P fails, when NAT is strict, or when hiding player IPs is required
- no fully authoritative gameplay server for the first production online version

In normal gameplay there should not be a gameplay-authoritative host. Both clients run deterministic rollback simulation. The server can still choose a session owner, P0/P1 assignment, or direct/relay route, but gameplay truth comes from deterministic inputs and checksum validation.

Recommended backend stack:

- Go service for the first backend because it can handle HTTP/WebSocket control traffic and UDP relay/signaling in one deployable binary.
- PostgreSQL for accounts, player profile data, MMR, match history, cosmetics ownership, and friend data.
- In-memory queue state at first, with Redis later if queues need to span multiple backend instances.
- HTTPS REST for account/profile operations.
- WebSocket for live queue, matchmaking, match found, ready checks, and post-match result flow.
- UDP sockets for gameplay relay and connectivity probes.

This keeps the client simple, gives enough performance headroom for a relay, and avoids committing to a heavyweight service layout too early.

## Online Hub / Server Status

The built-in online hub and standalone account/queue server have been removed for now. The remaining online code is the lower-level rollback and manual P2P debug harness:

- F6/F7 `ggpo.net` host/join
- F2/F3/F4 rollback verification tools
- game-state save/load/checksum support
- local input prediction, rollback, and checksum logging

The next production pass should rebuild the hub and backend carefully around a deterministic match-start contract instead of layering queue/account UI directly into the current debug harness.

## Latency Tuning

The debug UDP harness has two high-latency profiles:

- `ggpo.net highping [frames]`
  - Conservative profile.
  - Uses the supplied latency budget to pick lower frame-advantage and prediction caps.
  - Intended to trade smoothness for fewer desync corrections and shorter hard pauses.
- `ggpo.net smoothping [frames]`
  - Aggressive profile.
  - Sets frame-advantage and prediction caps directly to the supplied budget.
  - Intended for testing smooth long prediction, but it can create large rollbacks/corrections under interactive hazards.

For ugly links, prefer `ggpo.net highping 140` over `ggpo.net smoothping 140`. The conservative profile should feel more delayed or stuttery, but it should avoid the worst long freeze-and-correct cycles.

State correction is capped to avoid giant hard pauses:

- the host keeps sending/retrying correction state, but does not freeze while waiting for the peer's ACK
- correction, frame-advantage, and prediction waits are capped to about 60 ticks
- the joiner hard-waits for a correction for about 60 ticks, then resumes prediction while the state transfer continues
- if the correction arrives late, the client applies it as a visible snap instead of staying frozen for many seconds
- correction transfers use delta chunks against the last shared correction/start-state baseline when possible, with full-state chunks as the fallback

## Online UX Needed

The real frontend should be queue-first, not host/join-first.

Online hub:

- account login/create account from day one
- current username, rating, connection region/status, and selected character/cosmetics
- ranked queue button
- casual queue button
- queue cancel button
- match found / connecting / synchronizing / loading states
- ping/route display once a match is found
- disconnect reason and recovery path

Gameplay overlay/debug:

- enemy username above their character
- local `V` indicator under the local player's character
- optional debug-only ping, rollback, prediction, transport route, and checksum counters

Post-game screen:

- custom online end screen showing winner/loser
- match result, rating/MMR change when ranked, and disconnect/desync reason if relevant
- rematch button when both players are eligible
- queue again button
- return to online hub button

Friend and challenge system should come after queueing works:

- friend list
- add/remove/block friends
- online presence
- direct challenge
- accept/decline challenge
- private rematch/challenge flow

For development, keep console commands available. Later, online production builds should keep game updates flowing through pause/menu states where needed and should block online-unsafe debug hotkeys.

## Player Identity And Cosmetics

Cosmetics and character customization should be planned soon, before the online menu and matchmaking UI harden around player identity. They should work in offline/local play too, not only online.

Near-term cosmetics planning goals:

- define what can be customized: colors, body parts, hats, outfits, trails, nameplates, animations, etc.
- define which cosmetics are built in, unlockable, account-owned, local-only, or modded
- define how customization is selected in local play
- define how customization is selected and validated in online play
- define how cosmetic assets are loaded, cached, animated, and attached to characters
- define fallback behavior for missing or mismatched cosmetics

Online cosmetic rules:

- enemy username above their character
- small `V` indicator under the local player's character
- hats/outfits/cosmetic choices render consistently on both clients
- cosmetic choices are exchanged and validated before gameplay starts
- cosmetic assets, animation data, and required texture/font resources load before the match begins
- cosmetic data is not sent every frame
- cosmetics do not affect gameplay simulation
- cosmetics are not included in rollback state checksums or desync decisions
- if a cosmetic asset is missing or mismatched, the match should either fall back to a default cosmetic or fail before gameplay starts

Before implementing this section, ask for the full customization/cosmetics design. There is a separate planned design for usernames, player indicators, hats, animations, unlocks, ownership, local customization, online validation, and cosmetic selection UX.

## Matchmaking / Transport Needed

For real internet play, direct UDP localhost testing is not enough.

Needed backend pieces:

- account registration/login
- session tokens
- player profiles
- ranked and casual queue state
- MMR/rating updates
- matchmaking rules
- signed match config generation
- match result reporting
- friend/challenge APIs later
- basic rate limiting and packet validation

Needed transport pieces:

- signaling service for client connection setup
- NAT traversal strategy
- direct UDP connectivity probes
- relay fallback for strict NATs or bad routes
- route selection based on connectivity and latency
- timeout, disconnect, and reconnect policy
- per-match transport tokens so random packets cannot join a match

The backend should hide transport details from the frontend. The online UI should say "queueing", "match found", "connecting", or "synchronizing", not "host" or "join".

GGPO handles rollback protocol details, but it does not provide accounts, queues, matchmaking, friend systems, signaling, NAT traversal, or relay infrastructure by itself.

## Implementation Phases

Phase 1: production plan and data contracts

- write match config schema
- write account/profile schema
- write queue and match result data model
- define client/server messages
- define direct/relay transport abstraction
- define cosmetics metadata shape enough that online match config can carry it later

Phase 2: backend skeleton

- Go backend project
- local dev config
- PostgreSQL schema
- account create/login
- session token validation
- health/status endpoint

Phase 3: queue and match config

- ranked queue API
- casual queue API
- queue cancel
- simple matchmaking loop
- signed match config returned to both clients
- result submission stub

Phase 4: client integration without final UI

- console or debug commands for login and queue
- backend connection status
- receive match config
- start deterministic online match from match config
- keep F6/F7 harness as a debug-only path

Phase 5: transport hardening

- direct UDP signaling
- NAT traversal probes
- relay fallback
- route choice and ping display
- disconnect handling

Phase 6: online hub and post-game UX

- login/create account UI
- online hub
- ranked/casual queue buttons
- queue cancel/status
- custom winner/rematch/queue-again/hub end screen

Phase 7: friends and challenges

- friend list
- friend requests
- presence
- challenge flow
- private rematch/challenge handling

## Mod And Anti-Cheat Policy

The eventual anti-cheat/mod policy should be decided before public online is enabled.

Minimum compatibility checks:

- game executable hash
- injected framework DLL hash/version
- GGPO protocol version
- enabled mod list
- loaded map checksums
- relevant config/rules checksums

Likely policy for ranked/public queue:

- disable gameplay-affecting mods
- allow cosmetic-only mods only if they do not affect rollback state
- require both clients to agree on custom maps
- reject mismatched state-affecting scripts/assets

Important: anti-cheat should not just hide the mod menu. The online mode needs deterministic compatibility checks before the match starts.

## Testing Needed

Keep the current tests and add production-style tests around them.

Existing manual tests:

1. F2 roundtrip save/load.
2. F3 loopback rollback verification.
3. F4 local callback session.
4. F6/F7 localhost host/join session.

Additional tests to add:

- automated deterministic replay test with fixed input traces
- long soak test across rooms, deaths, respawns, score changes, and match reset
- checksum diff dump on first desync
- packet loss/jitter simulation via `ggpo.net sim`
- host-authoritative state correction via `ggpo.net correction`
- artificial input delay tests
- mismatched map/version rejection tests
- mod mismatch rejection tests
- two-machine LAN test
- internet test with NAT/relay path

Desync diagnostics should report:

- frame
- local checksum
- remote checksum
- first differing rollback blob offset
- field name for that offset
- local/remote input commands
- active room
- old active room
- room lerp state
- thing count
- RNG seed
- player positions
- controller/leader/loser pointer classification

## Production Checklist

- Real GGPO session wrapper.
- Account creation/login from day one.
- Queue-first online hub and status UI.
- Ranked and casual matchmaking queues.
- Signed backend match config.
- Deterministic match-start flow.
- Version/map/mod compatibility handshake.
- Hybrid direct P2P / relay gameplay transport.
- NAT traversal and relay fallback.
- Custom online end screen with winner, rematch, queue again, and hub actions.
- Disconnect/rematch handling.
- Post-match result reporting and MMR/rating update path.
- Cosmetics/customization plan that works offline and online.
- Configurable input delay if needed.
- Packet loss/jitter testing.
- Desync dump files.
- Clear public/private mod policy.
- Release build logging that is useful but not spammy.

## Build Command

From repo root in PowerShell:

```powershell
$env:PATH='C:\msys64\mingw32\bin;' + $env:PATH
$src=@('dllmain.c','stubs.c','hooks.c','custom_maps.c','lua_manager.c','ggpo_ext.c','ggpo_loopback.c','ggpo_local.c','ggpo_net.c','font_ext.c','texture_ext.c','log.c','net_ext.c')
$libs=@('-lkernel32','-luser32','-lopengl32','-l:libluajit-5.1.dll.a','-lws2_32','-lwinhttp','-IC:\msys64\mingw32\include','-LC:\msys64\mingw32\lib')
& C:\msys64\mingw32\bin\gcc.exe -m32 -shared -o build\SDL2_test.dll @src @libs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& C:\msys64\mingw32\bin\gcc.exe -m32 -shared -o SDL2.dll @src @libs
exit $LASTEXITCODE
```

When real GGPO is added, update this command to include the GGPO source/library and any required include paths.
