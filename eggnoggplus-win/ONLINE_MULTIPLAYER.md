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

1. Matchmaking/lobby picks host/session owner.
2. Both clients verify version, assets, map, rules, and mod policy.
3. Host chooses or confirms seed and match parameters.
4. Both clients enter the match through the same deterministic start path.
5. GGPO begins at frame 0 after both clients are synchronized.

Host full-state sync can remain as a debug or recovery tool, but production online should prefer deterministic match initialization over transferring arbitrary live state.

## Online UX Needed

Add a proper online menu instead of relying on F6/F7:

- Queueing system (ranked with elo/mmr and casual)
- Cancel connection
- Connection status
- Ping display
- rollback/prediction counters
- disconnect reason
- rematch
- return to local play cleanly
- Login/Friend system with the ability to challenge friends

For development, keep console commands available.

## Matchmaking / Transport Needed

For real internet play, direct UDP localhost testing is not enough.

Needed pieces:

- signaling server or lobby service
- NAT traversal strategy
- relay fallback for strict NATs
- session tokens
- player identity
- timeout and reconnect policy
- basic rate limiting and packet validation

GGPO handles the rollback protocol, but it does not provide matchmaking, account identity, lobbies, or relay infrastructure by itself.

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
- packet loss/jitter simulation
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
- Online menu and status UI.
- Deterministic match-start flow.
- Version/map/mod compatibility handshake.
- NAT traversal or relay.
- Disconnect/rematch handling.
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
