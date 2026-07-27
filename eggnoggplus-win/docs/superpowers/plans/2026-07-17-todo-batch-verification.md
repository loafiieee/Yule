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
| Yule installer | Filtered `eggnoggplus.exe` selection, safe-path/hash/size channel sync, incomplete fresh-adoption rollback, v2 managed-file receipt, and conservative vanilla/shortcut/Steam uninstall that preserves modified or user-owned files | Isolated loopback install/uninstall lifecycle covers verified adoption, manifest ownership, modified/user-file preservation, missing-forwarder fail-closed behavior, and offline first-install rollback; packaging static check requires `UNINSTALL.bat` | Manual real Start Menu/Steam multi-account install and uninstall, moved/cross-volume game, and production HTTPS release package |
| Strict control channel | Message-atomic copied send queue, partial-write/backpressure handling, strict flat JSON/framing, pre-auth server capability handshake, repeated auth/match version validation, connect/auth deadlines, secret cleanup, two-pass complete map manifest | `net_ext_test`, `online_control_test`, hook/static/server tests and public deployment probe | TLS/certificate validation, durable session auth, production rate limiting/operations |
| P2P v17 + retry | Per-match HMAC authentication with v17 domains, replay window, CSPRNG sessions, fail-closed one-shot keys; three fresh-socket symmetric attempts; fixed 1,292-byte ordinary packet under a 1,400-byte guard | Paired loopback runner covers no key, normal, load retry, replay, wrong key, tamper, and exact v17 input/checksum ACK semantics | The live public service still advertises v16; coordinated v17 deploy/restart/preflight is pending. Real LAN/home NAT/high-loss retry, relay, and IPv6 remain future |
| Rollback correctness | Post-content transactional layout freeze/proof; wrap-safe rings; immutable same-frame input; monotonic contiguous-remote and peer-ACKed-local horizons; 512-bit SACK; live-edge/oldest-hole resend; hard prediction recovery; generation-scoped cumulative checksum ACK/go-back-N retry; dual-proof retirement stall; transactional received-state apply; failure-atomic live-tick pre-state restoration; mutually confirmed correction replay/release; post-stall once-per-frame local-input commit; checksum-authoritative gameplay RNG/camera X/Y/shake amplitude/decay, `_game_w`, `_game_h`, `_resumed`, and the `_mine_anim` same-tick guard; authoritative online clean-simulation pinning across local layout or rumble writes with peer-local render restoration; coherent peer-local room/mine palette sidecars across active rollback/correction; canonical x87/MXCSR native-tick controls; and a control-first UDP backpressure baseline with retryable blocked state cursors, measured hard failures, and eight-datagram bulk caps are implemented. A standalone bounded canonical rollback envelope is also implemented as a foundation, not as the production serializer | The ordinary paired chaos case runs changing nonzero inputs through frame 2,047 under 20% loss and variable delay/reordering, reuses all 512 history slots across four generations, exercises prediction/rollback on both roles, byte-compares all 2,048 finalized canonical pre-frame states and post-frame checksums, and drains bilateral checksum ACKs. A second authenticated 2,048-frame mode begins at `UINT32_MAX - 1023`, crosses frame zero, repeats the exact state/checksum comparison through frame 1,023, and drains bilateral proof after the wrap. Private structurally parsed traces report the exact first frame/byte disagreement and are deleted afterward. The runner drains both child pipes concurrently so debug output cannot deadlock a peer. Its correction cases force asymmetric correction under loss, delay/reordering, authenticated duplicate replay, and an exact state-chunk would-block, then prove the blocked send advances neither the physical-send count nor full/delta cursor before matching correction ID/snapshot/resume/transcript and drained checksum ACKs. The long case enables chaos from frame one, injects at frame 600 after ring reuse, and continues both roles with prediction/rollback through a checksum-confirmed frame 2,047. Its input-sampling case holds one peer transport-only, proves stale wall-tick samples remain unassigned through a hard prediction stall, commits only the fresh recovery sample, and checksum-confirms both peers. It gives the peers different local aspect ratios, perturbs both layout dimensions and shake after authoritative setup, requires the host simulation values at pre-state capture/native tick, and requires local dimensions afterward; a separate paired injection fails post-tick geometry capture and proves exact frame-zero restoration before abort. The guarded core runner exercises the production v9 serializer's raw roundtrip, canonical validation/pointer rejection, canonical load, preflight no-mutation, RNG/camera/shake/dimensions/resume/mine-guard checksum sensitivity and restoration, same-tick second-mine suppression, invalid/read-only camera/geometry atomicity, atomic palette sidecar restore and invalid/NaN rejection, static simulation/start/failure/palette ordering, socket classification/queue/cursor/priority invariants, the standalone envelope's bounded identity/invariant/atomic-decode contract, and hostile/nested/failure FP-control restoration. These are not native long-run determinism or completion of the production typed adapter | Finish the native typed capture/reconstruction/transaction adapter, real-native per-frame entity/map lifecycle assertions, protocol-phase disconnects, remaining checksum-field audit, selective correction-chunk reliability, shared byte budgeting, and repeated/burst backpressure matrix; then pass live differing-prior-map setup, first-divergence diagnostics, native gameplay soaks, and two-client room/mine palette visual acceptance |
| Prematch runtime | Connection, native reset, state sync, and neutral frame zero stay behind countdown; GAME opens only when the first tick can advance | Prematch hook/static and paired process tests | Full hub flow, slow setup, attempt rollover, abort/disconnect on two machines |
| Result toast | Compact bottom-right provisional/confirmed notification; completion returns to a stable hub and requeue waits for server confirmation | `online_flow_integration_static_test.py` | Two-client normal/loss/disconnect/rating/toast/requeue acceptance; server result trust still needs hardening |
| Menu-time sim / controls | Pause/options/console/Mods service rollback with neutral menu input; both local control sets merge into assigned player | Prematch and Lua suspension static boundaries | Long overlay soak; both player assignments and mixed devices; arbitrary unaudited native screens remain future |
| Gameplay Lua boundary | API-use classification, full match-window suspension, mutator/callback guards, transient neutralization, locked lifecycle/UI | `lua_online_suspension_static_test.py` | Mixed gameplay/cosmetic normal/retry/abort soak; native/FFI tamper and complete compatibility enforcement remain future |
| Window modes/viewport | F1 3:2 presets, F11, maximize normalization, safe-boundary recreation, display-aware native cache, borderless/fullscreen/windowed args, local viewport preservation | Strict policy C test and `window_runtime_static_test.py` | Unlike monitors, DPI 100–200%, move/resize/maximize/failure fallback and mouse alignment |
| V2 maps/content bridge | One inherited map-level sheet/grid, optional native-prefix reskin for all unlisted vanilla cells with appended custom sprites, explicit per-tile exceptions, native collision presets, and a separate deterministic `map.lua` VM with contact/tick callbacks, typed state/RNG, explicit player/dead-body/sword/verified-K-hazard profiles, exact-cell or binding-union contacts, mirrored cell data, temporary sprites, kind/lifecycle-safe temporary velocity limits, pinned identity, fail-closed online readiness, and rollback POD; selectable demo uses calibrated equal-height spring and mirrored-fan behavior | One-command registry/tiles/parser/package/VM suite covers map defaults, zero-definition native layouts, minimum-prefix safety with flexible grid geometry, draw-time atlas base swap/restore, sandbox escapes/budgets/pow rejection, source/binding identity, exact cells and binding seams, enter/stay/leave, velocity-limit validation/replacement/expiry/replay, checked-in corpse exclusion, sword/hazard slot replay, snapshot restore/mismatch, sprite expiry, rollback/static hook owners, online script gate, rendering, and generation lifetime | Documented live demo whole-map visual/physics/script/reload/rollback pass, including ~53 px launches on both native gravity scales, dead bodies settling through native respawn, and delayed-kick suppression after sensor exit; safe custom spawning and broad weapons/entities/editor API remain future |
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
  'tests\custom_maps_retirement_static_test.py',
  'tests\content_force_hooks_static_test.py',
  'tests\map_script_hooks_static_test.py',
  'tests\map_script_rollback_static_test.py'
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

Never launch a `build/*.exe` directly. Windows test binaries must be compiled and
executed only through a guarded runner that prepends the repository, MinGW32, and
MSYS runtime directories to the child `PATH` and preflights every required runtime
DLL. The current guarded entry points are:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_v2_map_test.ps1
powershell -ExecutionPolicy Bypass -File .\tests\run_map_script_test.ps1
powershell -ExecutionPolicy Bypass -File .\tests\run_updater_test.ps1
powershell -ExecutionPolicy Bypass -File .\tests\run_core_native_tests.ps1
python .\tests\prematch_net_test.py
```

The V2 runner owns the strict content-registry, tile, bridge, map-script, spring,
and custom-map C suites. The core runner owns credential, TCP/control, window policy,
production rollback-serializer, standalone canonical-envelope, native FP-control,
installer lifecycle, and expanded-player-colour suites plus their static integration
checks. The serializer/static slice also pins the single canonical frame-boundary capture:
one scratch canonicalization supplies retained history, checksum, and optional component
diagnostics across live, rollback, and correction paths. If another standalone C suite needs to be
executed, add or extend a guarded runner first; a successful compile alone is not
permission to launch its executable.

`prematch_net_test.py` owns its C build, deterministic v17 ACK/SACK/checksum frame-ring
preflight, received-state transaction and asymmetric prematch-skew modes, 2,048-frame
four-generation ordinary and uint32-wrap seeded chaos soaks, paired correction-barrier
loss/reorder/duplicate coverage in both short and ring-reusing 2,048-frame modes, and the
paired hard-stall input-sampling boundary. Its
supervisor drains both peer pipes concurrently and byte-compares every finalized chaos
state record before deleting its private traces. The guarded core suite also runs the
streaming peer-trace diff CLI regression, including exact uint32-wrap equality, every
generic divergence class, safety bounds, and malformed streams. The credential executable skips its real Windows Credential
Manager mutation unless the opt-in environment variable documented in the credential spec
is set.

Finish every source batch with the canonical integrated link. It links once into
`build/SDL2_test.dll`, then installs that exact byte-identical artifact as `SDL2.dll`:

```bash
bash compile.sh
```

When `gcc` is not already visible to the invoking Bash, `compile.sh` prepends the
standard `C:\msys64\mingw32\bin` location itself and otherwise fails with one explicit
toolchain diagnostic before touching the installed proxy.

The PowerShell runners prepend the repository, MinGW32, and MSYS runtime
directories, preflight their required runtime DLLs, and launch linked tests
synchronously. The Python prematch runner passes the same prepared environment
to every child. Do not launch a freshly linked test `.exe` directly: use these
runners so a missing dependency becomes a normal terminal error instead of a
Windows loader-dialog storm.

The focused suites and a normal full 32-bit DLL link passed on 2026-07-17. Rerun the link
after any later source edit; strict focused tests are intentionally separate from legacy
GNU forwarding-stub warnings in a hypothetical whole-project ISO-C `-Werror` build.

## Runtime acceptance order

1. Install the freshly linked DLL and fully restart the game. At 480x320, inspect login,
   Settings, Friends, queue/match cards, challenge/result toasts, updater toast,
   and an active-match nametag case. Compare directly with the vanilla main menu and confirm
   identical global scale, black shadow, red/yellow pulse, hotspot, and atlas artwork. It
   must appear once, remain visible while idle, and stay above every overlay.
2. Confirm the Remember line is a compact square checkbox rather than a setting card.
   Exercise mouse, keyboard, and controller selection; opt-out, successful save/restart,
   identity change, rejected saved secret, malformed record, and deletion failure. Inspect
   config/history/logs for plaintext passwords.
3. Run two hub clients through casual and competitive assignment. Delay setup beyond the
   visible countdown and confirm the hub card continues reporting work, GAME is never
   exposed frozen, and its first visible tick advances immediately. Start the clients from
   different prior maps, then confirm both finalize the selected map's same layout/capacity.
4. Force attempt rollover and verify a fresh local endpoint and attempt status; exhaust all
   attempts and confirm a clean server loss/release and return to the hub.
5. Finish win/loss/disconnect paths and verify server confirmation/rating copy, disabled
   pre-confirmation Requeue, successful Requeue, Hub, and responsive mouse hitboxes.
   Challenge an online friend whose installed map set only partially overlaps: cycle the
   complete compatible list with keyboard/controller and mouse, cancel once, then select
   a custom map and verify both clients show its label and launch their local selector for
   that exact key. Change one manifest before accepting and confirm the challenge fails
   closed instead of silently selecting a different map.
6. After the production native typed adapter and remaining checksum-field audit land, run
   long nonzero-input paired sessions through seeded loss, burst loss, delay, reorder,
   duplicate, socket backpressure, prediction/history boundaries, correction, rooms,
   deaths, scoring, reset, and custom maps. Require identical
   per-frame canonical state or a deterministic first-field repro bundle, never an
   unexplained correction loop or permanent input hole.
7. Force ordinary rollback and coordinated correction during room transitions and mine
   tints. Confirm the next room palette always settles and the mine tint always returns;
   presentation colours must remain excluded from cross-peer gameplay comparisons.
8. Leave pause/options/console/Mods open while the peer moves. Confirm simulation continues
   and menu input stays neutral. Test both control sets under both player assignments with
   mixed keyboard/controllers.
9. Run a gameplay mod and cosmetic-only mod through assignment, retries, normal completion,
   and aborts. Confirm only gameplay owners suspend/guard, then resume exactly once with
   neutral overrides and unchanged persisted enabled/config state.
10. Test all launch flags/conflicts, F1/F11, maximize, move/resize, unlike monitors, and DPI
   levels. Confirm fullscreen chooses the destination display and UI/mouse/viewport geometry
   remains local and aligned during rollback.
11. Load the V2 demo and verify its unlisted native `@` terrain comes from the complete
    map-level sheet, solid `$` cells inherit the same declared-once atlas, scripted vertical
    `>` spring with one-cell temporary sprite, `-4.0` living-player and `-2.8`
    sword/K-hazard targets reaching approximately the same 53-pixel height, dead bodies
    ignored so they settle and respawn through native logic, and an
    eight-tick rollback-owned upward limit that rejects a delayed unarmed kick after
    leaving the sensor. Also verify opposite-direction scripted mirrored `}` fans,
    player center/foot and sword center-only contacts, recycled-sword lifecycle
    separation, valid/missing/changed atlases, center/left/right
   animation/tint/transforms/layers, invalid-script last-known-good behavior, online
   bind/start abort messaging, rollback during contacts/overrides, and repeated generation
   retirement.
12. Test updater offline, loopback channel, corrupt size/digest, malicious path, interrupted
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
- V2 `map.lua` is deterministic pinned map content with a fixed rollback snapshot and
  restricted VM; do not describe it as ordinary mod Lua or as completion of spawned
  entities/weapons/the broad custom-content API.
- F1/F11, borderless, maximize normalization, and display-aware recreation are implemented;
  physical multi-monitor/DPI acceptance is unfinished.
- Gameplay-mod suspension is a framework-managed Lua determinism boundary, not process
  anti-cheat. Current peer-reported build/map gates are compatibility hints, not integrity.
- Remember me protects local at-rest storage through Windows Credential Manager; raw TCP
  authentication remains insecure against an on-path observer until TLS lands.
- Direct P2P retry/authentication is independent from the still-open relay and IPv6 work.
- Post-content structural layout proof is implemented. It originally reused a
  receiver-unused v16 field; v17 retains that structural field while adding a 512-bit SACK,
  monotonic two-direction input horizons, selective resend, and generation-scoped reliable
  checksum comparison ACK in a version-locked 1,292-byte packet. Prediction and checksum
  retirement now hard-stall before unrecoverable overwrite, and received state is validated
  and applied transactionally with exact restore on failure. Mutually confirmed correction
  replay/release and canonical x87/MXCSR native-tick controls are implemented. Each
  simulated tick now captures one canonical post-state as the next retained frame boundary;
  its checksum and optional component summary come from the same scratch blob. Correction
  replay takes exact commands from immutable role-local input rings, with finalized history
  used only as a retired-generation fallback, and consumes an actionable transition before
  applying the no-progress deadline. The
  standalone canonical rollback envelope is a bounded, identity-pinned codec foundation;
  production still uses the v9 raw serializer and needs the native typed
  capture/reconstruction/transaction adapter plus the remaining checksum-field audit.
  None of these structural proofs is map-content identity or a complete compatibility
  manifest.
- Toast-only result routing is implemented; server-side result trust and private rematch are not.
- Exact native cursor effects are implemented and statically guarded; visual completion
  requires the current DLL to be restarted and accepted in the running game.
