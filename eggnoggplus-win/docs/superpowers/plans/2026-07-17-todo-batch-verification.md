# TODO Batch Verification and Remaining Work Plan

**Date:** 2026-07-17  
**Status:** Current implementation/acceptance handoff  
**Related specs:** all `2026-07-17-*-design.md` files in
`docs/superpowers/specs`

## Purpose and status language

This plan separates implemented source, automated verification, live acceptance, and
future product/security work. “Implemented” never means a fixed-address rendering path,
Credential Manager lifecycle, two-client flow, or physical display behavior has been
visually accepted. `TODO.txt` keeps those runtime checks open even when focused tests pass.

## Implementation and acceptance matrix

| Area | Implemented in source | Focused automated boundary | Live or future boundary |
|---|---|---|---|
| Native framework cursor | Online, automatic custom-state, and no-option Lua cursors call native `cursor_draw` twice with the live global scale, shadow, pulse color, hotspot, timeout/current/home-state restore, and one final online hook | `online_cursor_static_test.py` pins fixed addresses/ABI, pass order, state preconditions/restoration, removed approximation, shared Lua path, predicates, and last-render ordering | Restart and compare scale/shadow/pulse/hotspot against vanilla on every online screen, mode, DPI, and replacement atlas |
| Remember me | Compact 22–38 px checkbox line with backend-neutral copy; exact server/account Windows Generic Credential; save after `auth_ok`; guarded auto-auth skips the form and enters authenticated menus; delete/zero on every lifecycle edge | Credential C tests plus `credential_hooks_static_test.py` pin storage, generic UI copy, auto-auth source gating, gateway suppression, and cleanup; live store mutation is opt-in | Small-window/hitbox pass and real save/load/auto-login/delete/reject cycle; raw control TCP still needs TLS |
| Updater/config writer | Async check, default-on verified transaction, V2 crash journal, full-set verification, rollback/recovery, mutex, atomic comment-preserving config updates | Strict embedded suite plus one-command isolated loopback download/apply/recovery wrapper | In-game status/toggle/no-freeze, production HTTPS, loaded/mapped DLL, forced crash phases, and multi-instance release-machine pass |
| Strict control channel | Message-atomic copied send queue, partial-write/backpressure handling, strict flat JSON/framing, pre-auth server capability handshake, repeated auth/match version validation, connect/auth deadlines, secret cleanup, two-pass complete map manifest | `net_ext_test`, `online_control_test`, hook/static/server tests and public deployment probe | TLS/certificate validation, durable session auth, production rate limiting/operations |
| P2P v16 + retry | Per-match HMAC authentication, replay window, CSPRNG sessions, fail-closed one-shot keys; three fresh-socket symmetric attempts | Paired loopback runner covers no key, normal, load retry, replay, wrong key, tamper | Real LAN/home NAT/high-loss retry; relay and IPv6 remain future |
| Rollback correctness | Prototype prediction/history/correction/serialization paths exist, but audited map-layout, monotonic-input, recoverability, correction-barrier, schema, checksum, and FP hazards remain open | Current paired runner proves authenticated prematch and a minimal synchronized start, not long-run determinism | Complete every P0 netcode item in `TODO.txt`, then pass long nonzero-input paired sessions, seeded chaos boundaries, first-divergence diagnostics, and native gameplay soaks |
| Prematch runtime | Connection, native reset, state sync, and neutral frame zero stay behind countdown; GAME opens only when the first tick can advance | Prematch hook/static and paired process tests | Full hub flow, slow setup, attempt rollover, abort/disconnect on two machines |
| Result screen | Full state with outcome/opponent/map/rating/status and responsive Requeue/Hub; requeue waits for server confirmation | `online_flow_integration_static_test.py` | Two-client normal/loss/disconnect/rating/requeue/hub acceptance; server result trust still needs hardening |
| Menu-time sim / controls | Pause/options/console/Mods service rollback with neutral menu input; both local control sets merge into assigned player | Prematch and Lua suspension static boundaries | Long overlay soak; both player assignments and mixed devices; arbitrary unaudited native screens remain future |
| Gameplay Lua boundary | API-use classification, full match-window suspension, mutator/callback guards, transient neutralization, locked lifecycle/UI | `lua_online_suspension_static_test.py` | Mixed gameplay/cosmetic normal/retry/abort soak; native/FFI tamper and complete compatibility enforcement remain future |
| Window modes/viewport | F1 3:2 presets, F11, maximize normalization, safe-boundary recreation, display-aware native cache, borderless/fullscreen/windowed args, local viewport preservation | Strict policy C test and `window_runtime_static_test.py` | Unlike monitors, DPI 100–200%, move/resize/maximize/failure fallback and mouse alignment |
| V2 maps/content bridge | Strict symbolic parser, owner registry, per-map atlases, native cell binding/detours, animation/tint/transforms/layers, atomic reload/fallback and generation retirement; selectable symbolic demo fixture | Registry, tiles, V2, bridge C tests plus retirement static test and one-command packaged-fixture test | Documented live demo visual/reload/failure pass; broad weapons/entities/callback/editor API and online content fingerprint are future |
| Online map identity | Complete capped manifests, stable map key, local selector, exact nonempty server key, current build-fingerprint gate | Control/online-flow/server tests | Dynamic two-client mismatch/custom-map tests; friend picker and complete compatibility manifest remain future |

## Canonical automated verification

Run from the repository root with 32-bit MSYS2 first on `PATH`:

```powershell
$env:PATH='C:\msys64\mingw32\bin;' + $env:PATH

$pythonTests = @(
  'tests\compile_sources_static_test.py',
  'tests\online_cursor_static_test.py',
  'tests\credential_hooks_static_test.py',
  'tests\lua_online_suspension_static_test.py',
  'tests\prematch_hooks_static_test.py',
  'tests\prematch_net_test.py',
  'tests\online_control_hooks_static_test.py',
  'tests\online_flow_integration_static_test.py',
  'tests\online_server_auth_static_test.py',
  'tests\online_server_match_protocol_test.py',
  'tests\window_runtime_static_test.py',
  'tests\custom_maps_retirement_static_test.py'
)
foreach ($test in $pythonTests) {
  python $test
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
node --check online_server\server.js
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
python online_server\check_deployment.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
powershell -ExecutionPolicy Bypass -File .\tests\run_v2_map_test.ps1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
powershell -ExecutionPolicy Bypass -File .\tests\run_updater_test.ps1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Strict focused C suites:

```powershell
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\content_registry_test.c content_registry.c -o build\content_registry_final_test.exe -lbcrypt
.\build\content_registry_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\content_tiles_test.c content_tiles.c content_registry.c -o build\content_tiles_final_test.exe -lbcrypt
.\build\content_tiles_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\custom_maps_v2_test.c custom_maps.c content_registry.c log.c -o build\custom_maps_v2_final_test.exe -lbcrypt
.\build\custom_maps_v2_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\content_bridge_test.c content_bridge.c content_tiles.c content_registry.c custom_maps.c log.c -o build\content_bridge_final_test.exe -lbcrypt
.\build\content_bridge_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\credential_ext_test.c credential_ext.c -o build\credential_ext_final_test.exe -ladvapi32
.\build\credential_ext_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\online_control_test.c online_control.c -o build\online_control_final_test.exe
.\build\online_control_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -D_WIN32_WINNT=0x0601 tests\net_ext_test.c net_ext.c -o build\net_ext_final_test.exe -lws2_32
.\build\net_ext_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic tests\window_policy_test.c -o build\window_policy_final_test.exe
.\build\window_policy_final_test.exe
gcc -m32 -std=c11 -Wall -Wextra -Werror -pedantic -DUPDATE_EXT_TEST update_ext.c -o build\update_ext_final_test.exe -lwinhttp -lbcrypt -lws2_32
.\build\update_ext_final_test.exe
```

`prematch_net_test.py` owns its C build and paired processes. The credential executable
skips its real Windows Credential Manager mutation unless the opt-in environment variable
documented in the credential spec is set.

Finish every source batch with the canonical integrated link. It links once into
`build/SDL2_test.dll`, then installs that exact byte-identical artifact as `SDL2.dll`:

```bash
bash compile.sh
```

The focused suites and a normal full 32-bit DLL link passed on 2026-07-17. Rerun the link
after any later source edit; strict focused tests are intentionally separate from legacy
GNU forwarding-stub warnings in a hypothetical whole-project ISO-C `-Werror` build.

## Runtime acceptance order

1. Install the freshly linked DLL and fully restart the game. At 480x320, inspect login,
   Settings, Friends, queue/match cards, Game Over, challenge/result toasts, updater toast,
   and an active-match nametag case. Compare directly with the vanilla main menu and confirm
   identical global scale, black shadow, red/yellow pulse, hotspot, and atlas artwork. It
   must appear once, remain visible while idle, and stay above every overlay.
2. Confirm the Remember line is a compact square checkbox rather than a setting card.
   Exercise mouse, keyboard, and controller selection; opt-out, successful save/restart,
   identity change, rejected saved secret, malformed record, and deletion failure. Inspect
   config/history/logs for plaintext passwords.
3. Run two hub clients through casual and competitive assignment. Delay setup beyond the
   visible countdown and confirm the hub card continues reporting work, GAME is never
   exposed frozen, and its first visible tick advances immediately.
4. Force attempt rollover and verify a fresh local endpoint and attempt status; exhaust all
   attempts and confirm a clean server loss/release and return to the hub.
5. Finish win/loss/disconnect paths and verify server confirmation/rating copy, disabled
   pre-confirmation Requeue, successful Requeue, Hub, and responsive mouse hitboxes.
6. After the P0 netcode repairs land, run long nonzero-input paired sessions through seeded
   loss, burst loss, delay, reorder, duplicate, socket backpressure, prediction/history
   boundaries, correction, rooms, deaths, scoring, reset, and custom maps. Require identical
   per-frame canonical state or a deterministic first-field repro bundle, never an
   unexplained correction loop or permanent input hole.
7. Leave pause/options/console/Mods open while the peer moves. Confirm simulation continues
   and menu input stays neutral. Test both control sets under both player assignments with
   mixed keyboard/controllers.
8. Run a gameplay mod and cosmetic-only mod through assignment, retries, normal completion,
   and aborts. Confirm only gameplay owners suspend/guard, then resume exactly once with
   neutral overrides and unchanged persisted enabled/config state.
9. Test all launch flags/conflicts, F1/F11, maximize, move/resize, unlike monitors, and DPI
   levels. Confirm fullscreen chooses the destination display and UI/mouse/viewport geometry
   remains local and aligned during rollback.
10. Load valid, missing, changed, and differently sized V2 atlases. Inspect center/left/
   mirrored rooms, animation/tint/transforms/layers, atomic reload failure fallback, and
   repeated generation retirement.
11. Test updater offline, loopback channel, corrupt size/digest, malicious path, interrupted
    journal phases, restart, multi-instance contention, production HTTPS, and replacement of
    a DLL mapped by the running game.

## Release blockers and later roadmap

Do not claim live completion until the cursor/checkbox, two-client online flow, content
render bridge, window/DPI, mod suspension, and updater release-machine checks above pass.

Independent production blockers are the P0 deterministic-netcode repairs, TLS/session
security, complete compatibility/content policy, trusted result handling, durable
operations, relay, IPv6, and production GGPO.
Later product features include a friend map picker, private rematch, color sync, social
integrations/deep links, broader content/UI/audio APIs, and the map editor.

## Documentation invariants

- V2 custom art is wired through the native map/content bridge; describe live visual QA,
  not the bridge itself, as unfinished.
- F1/F11, borderless, maximize normalization, and display-aware recreation are implemented;
  physical multi-monitor/DPI acceptance is unfinished.
- Gameplay-mod suspension is a framework-managed Lua determinism boundary, not process
  anti-cheat. Current peer-reported build/map gates are compatibility hints, not integrity.
- Remember me protects local at-rest storage through Windows Credential Manager; raw TCP
  authentication remains insecure against an on-path observer until TLS lands.
- Direct P2P retry/authentication is independent from the still-open relay and IPv6 work.
- Authenticated packets and bounded socket retry do not prove rollback correctness; the P0
  state/input/correction hazards in `TODO.txt` remain release blockers.
- The result screen is implemented; server-side result trust and private rematch are not.
- Exact native cursor effects are implemented and statically guarded; visual completion
  requires the current DLL to be restarted and accepted in the running game.
