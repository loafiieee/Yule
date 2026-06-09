# Online / Rollback Patch Notes

This build focuses on direct-P2P online readiness without relay fallback.

## Desync fixes

- Fixed rollback transient-state capture so it stops at `_things` (`0x542080`) instead of overlapping the first 512 bytes of the canonical thing array.
  - Source: `TRANSIENT_GAME_STATE_SIZE` is now derived from `ADDR_THINGS - ADDR_TRANSIENT_GAME_STATE`.
  - Packaged `SDL2.dll` also contains a binary hotpatch for the same runtime-copy/read/write sizes.
- Bumped the source rollback blob version to `5` for rebuilt DLLs.
- Added source-side remapping for process-local gameplay pointers after rollback/state load:
  - `_player[0]`, `_player[1]`
  - `_leader`
  - `_loser`
  - `_controller`
  - `_waterfall_fx`
  - `_current`
- Added source-side canonicalization of per-frame scratch that should not participate in rollback checksums:
  - `_current`
  - `_danger_count` / `_danger_things`
- Removed runtime state size from the netplay build fingerprint so identical builds do not warn as mismatched just because peers hashed at different map/state moments.
  - Packaged `SDL2.dll` also hotpatches this fingerprint behavior.

## LAN / direct-internet tuning

- Default input delay changed from `1` to `2`.
- Default frame-advantage cap changed from `20` to `12`.
- Default prediction cap changed from `24` to `16`.
- Netplay block-wait cap changed from `60` ticks to `15` ticks in source and in the packaged `SDL2.dll` hotpatch.
- The online matchmaking server now chooses the LAN address plus the peer's bound local UDP port when both players probe from the same public IP. This fixes the same-LAN case where the server previously mixed a LAN address with a NAT-mapped public port.
- The client keeps sending P2P probes to the signaling server until the GGPO session connects, even after a peer endpoint is known. This keeps NAT mappings fresh during direct UDP hole punching.
- GGPO source now sends repeated HELLO bursts before connection and accepts one authenticated endpoint migration before frame 0/start-state load. This helps direct UDP when the first real packet arrives from a different NAT-mapped port than the signaling server initially observed.

## Determinism guardrails

- If audio RNG detours fail, online matches disable native synth so audio RNG cannot perturb gameplay RNG.
- Manual GGPO net sessions also force native synth off when those detours are unavailable.

## Cross-network note

No relay fallback was added. Direct UDP hole punching can work across many home NATs and with port forwarding, but it cannot guarantee connectivity through strict/symmetric NAT or CGNAT. Those cases require either user port forwarding/UPnP-style mapping or a relay/TURN-style fallback.

## Build note

The source tree is patched. The packaged `SDL2.dll` also includes targeted binary hotpatches for the high-impact runtime fixes because this environment did not include a Windows/MinGW toolchain (`windows.h`, Winsock headers, and 32-bit Windows import libraries were unavailable). Rebuilding from source with the project's normal Windows toolchain will include the fuller source-level fixes.

## Hotfix: server start-game freeze

The previous `server.js` sent the new LAN-local-port route to every client.  That could strand the packaged legacy runtime at frame 0 if the LAN hint was not reachable.  This build gates LAN-local routing behind `route_version: 2`, keeps legacy clients on the older endpoint behavior, and uses `127.0.0.1` for route-v2 clients when both peers report the same LAN host.
