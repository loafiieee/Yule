# Yule framework source

Yule is the native Eggnogg+ mod framework and its companion online services.
This repository contains the framework, map/content systems, Lua runtime,
rollback transport, updater/installer source, documentation site, automated
tests, matchmaking server, LAN admin service, Discord LFG bridge, and deep-link
redirect service.

This source tree intentionally does not include the Eggnogg game executable,
redistributable runtime DLLs, generated builds, player configuration, account
databases, ratings, logs, crash/desync dumps, or deployment secrets. You need a
legitimate Eggnogg installation to run the framework.

## Repository map

- Native framework: root `*.c` and `*.h`
- Lua/mod API and examples: `mods/`
- V1/V2 map examples: `maps/`
- Online services: `online_server/`
- Installer/updater: `installer/`, `update_ext.c`, `updater_helper.c`
- Developer reference: `docs-site/`
- Detailed design/reference documents: root Markdown files and `docs/`
- Guarded tests: `tests/`

## Build

The current native target is 32-bit Windows. Install MSYS2 with the MinGW32 C
toolchain plus the dependencies named by `compile.sh`, then run:

```bash
bash compile.sh
```

Do not launch test executables from `build/` directly. The PowerShell runners
prepare the required 32-bit runtime DLL search path and fail cleanly when a
dependency is unavailable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_core_native_tests.ps1
python tests/prematch_net_test.py
python tests/online_server_match_protocol_test.py
```

The game itself is never part of automated source tests.

## Online server

The Node services have no third-party runtime dependencies:

```bash
cd online_server
node server.js
```

Production setup, systemd units, the LAN-only admin service, Discord LFG bridge,
reverse proxy, UDP relay, safe updates, backups, and deployment probes are
documented in `online_server/README.md` and `DISCORD_LFG_BOT.md`.

Copy `config/server.env.example` to a root-owned deployment environment file
outside the repository. Never commit tokens, passwords, server secrets,
`users.json`, or `ratings.json`.

## Documentation

Serve `docs-site/` as static files. Start with its installation, architecture,
API reference, maps/content, online server, updater, testing, and security
sections. `MODDING.md`, `MAP_FORMAT.md`, `ONLINE_MULTIPLAYER.md`, and
`UPDATER.md` are the detailed source-adjacent specifications.

## Public-source provenance

`SOURCE_SNAPSHOT.json` records the private development commit from which this
sanitized tree was exported. This repository starts with fresh history so
private runtime files cannot survive in old commits. See `PUBLIC_SOURCE.md` for
the release procedure.

## License

A project license has not yet been selected. Until a license is added, the
source is available for inspection but no permission to copy, modify, or
redistribute it is granted.
