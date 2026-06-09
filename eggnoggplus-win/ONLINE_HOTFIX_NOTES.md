# Online Hotfix Notes - server freeze at game start

This hotfix fixes a regression in the previous `online_server/server.js`.

## What was freezing

The previous server immediately sent `peer_host = peer LAN host` and `peer_port = peer local UDP port` whenever both clients' UDP probes came from the same public IP.  That route is useful for a rebuilt LAN-route-aware client, but the packaged runtime client only understands one legacy `peer_host` / `peer_port` endpoint and has no candidate/fallback layer.  If the LAN hint was wrong, blocked by Windows firewall, or represented two clients on the same PC via the LAN adapter instead of loopback, both clients entered the game and then paused at frame 0 waiting for GGPO HELLO/state sync.

## Fix

- Legacy clients now get the old safe endpoint behavior by default.
- Source builds now advertise `route_version: 2` in the map manifest.
- The server only uses the newer LAN-local-port route when both clients advertise route version 2.
- For route-v2 clients on the same machine, the server sends `127.0.0.1:<peer local port>` instead of the machine's LAN IP.
- The server still includes route metadata fields (`peer_route`, `peer_public_host`, `peer_public_port`, `peer_lan_host`, `peer_lan_port`) for diagnostics/future clients.

## Validation

- `node --check online_server/server.js` passes.
- A local protocol smoke test was run against the Node TCP/UDP server:
  - legacy clients receive legacy-compatible peer endpoints
  - route-v2 same-LAN clients receive LAN-local endpoints
  - route-v2 same-PC clients receive loopback endpoints

The C source still needs a normal Windows/MinGW rebuild to activate source-only changes that were not part of the targeted SDL2.dll hotpatch.
