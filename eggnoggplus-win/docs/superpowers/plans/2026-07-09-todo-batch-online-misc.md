# TODO Batch (Online Reliability + Misc) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement six approved TODO items: `-borderless` launch arg, in-game version check/updater, automatic suspension of gameplay-affecting mods during online play, P2P connect retry, prematch hole-punch during the countdown, and online viewport-shift instrumentation.

**Architecture:** All changes live in the injected SDL2-proxy framework DLL (C, mingw32, 32-bit). New `update_ext.c/h` owns the release-channel updater; everything else modifies `hooks.c` (online match lifecycle, menus, launch args), `lua_manager.c` (mod API gating), and `ggpo_net.c` (gameplay hold gate). No server (`online_server/server.js`) changes.

**Tech Stack:** C (gcc -m32, MSYS2 mingw32), WinHttp (already linked), bcrypt (new link for SHA-256), LuaJIT C API, custom UDP rollback netcode.

**Spec:** `docs/superpowers/specs/2026-07-09-todo-batch-online-misc-design.md`

## Global Constraints

- **No changes to in-match netcode semantics** (2026-06-18 revert): input delay stays one-shot, stalls must guarantee progress, no timesync changes. Only the pre-connect phase may change.
- All work happens in `eggnoggplus-win/` (repo-relative paths below assume that directory).
- The deterministic match-start path (seed + map selector applied in `online_launch_pending_match_to_game`) must not move.
- Build must stay warning-clean enough to produce both `build/SDL2_test.dll` and `SDL2.dll`. `SDL2.dll` is locked while the game runs — always build `build/SDL2_test.dll` first to validate, and only copy to `SDL2.dll` when the game is closed.
- Every task ends with a commit.

**Standard build command** (PowerShell, from `eggnoggplus-win/`); after Task 2 the file list includes `update_ext.c` and libs include `-lbcrypt`:

```powershell
$env:PATH='C:\msys64\mingw32\bin;' + $env:PATH
$src=@('dllmain.c','stubs.c','hooks.c','custom_maps.c','lua_manager.c','ggpo_ext.c','ggpo_loopback.c','ggpo_local.c','ggpo_net.c','font_ext.c','texture_ext.c','log.c','net_ext.c','update_ext.c')
$libs=@('-lkernel32','-luser32','-lopengl32','-l:libluajit-5.1.dll.a','-lws2_32','-lwinhttp','-lbcrypt','-lcomdlg32','-lshell32','-lole32','-IC:\msys64\mingw32\include','-LC:\msys64\mingw32\lib')
& C:\msys64\mingw32\bin\gcc.exe -m32 -shared -o build\SDL2_test.dll @src @libs
```

Expected: exit code 0, `build/SDL2_test.dll` produced.

**Two-instance localhost test** (used by Tasks 5–7): copy the game folder to a second directory (or run two instances from the same folder with `-windowed`), start the online server locally (`node online_server/server.js`), point both clients' hub Settings at `127.0.0.1:47778`, log in with two accounts, and use Friends → challenge to create a match.

---

### Task 1: `-borderless` launch arg (borderless fullscreen)

**Files:**
- Modify: `hooks.c` (`hooks_apply_window_launch_args`, ~line 12601; externs near line 597)

**Interfaces:**
- Consumes: `p_SDL_SetWindowBordered`, `p_SDL_SetWindowSize`, `p_SDL_SetWindowPosition`, `p_SDL_GetDisplayBounds` (exported pointers in `stubs.c`), `g_proxy_sdl_window`, `p_main_set_fullscreen` / `p_main_is_fullscreen` (existing game-routine pointers).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Verify the SDL pointer exports exist in stubs.c**

Run: `grep -n "p_SDL_SetWindowBordered\|p_SDL_SetWindowPosition\|p_SDL_GetDisplayBounds" stubs.c`
Expected: pointer declarations + `GetProcAddress` assignments for all three (`SetWindowBordered` at ~416/955, `GetDisplayBounds` at ~142/678; `SetWindowPosition` should appear similarly — if missing, add it to stubs.c following the exact pattern of `p_SDL_SetWindowSize`).

- [ ] **Step 2: Add externs in hooks.c**

Next to `extern void* g_proxy_sdl_window;` (line ~597) add:

```c
extern void* p_SDL_SetWindowBordered;
extern void* p_SDL_SetWindowSize;
extern void* p_SDL_SetWindowPosition;
extern void* p_SDL_GetDisplayBounds;
```

- [ ] **Step 3: Add the borderless branch**

In `hooks_apply_window_launch_args`, extend the `-windowed` `else if` chain (the game's own SIZE_CHANGED handler — SDL event 0x200/6 in the exe — re-reads the drawable size and recenters, which keeps the GL viewport correct; that's why plain SDL calls are safe here):

```c
    } else if (hooks_cmdline_has_flag("-borderless") || hooks_cmdline_has_flag("--borderless")) {
        /* Borderless fullscreen: stay in windowed mode, strip the border, size to
         * the desktop. The game's own SIZE_CHANGED handling updates the drawable
         * size, so the GL viewport/letterbox stays correct without the native
         * exclusive-fullscreen path. -fullscreen wins if both are passed. */
        typedef struct { int x, y, w, h; } SdlRect;
        typedef void (__cdecl *fn_set_bordered_t)(void*, int);
        typedef void (__cdecl *fn_set_size_t)(void*, int, int);
        typedef void (__cdecl *fn_set_pos_t)(void*, int, int);
        typedef int  (__cdecl *fn_get_bounds_t)(int, SdlRect*);
        SdlRect bounds = {0, 0, 0, 0};
        if (p_main_set_fullscreen && p_main_is_fullscreen && p_main_is_fullscreen()) {
            p_main_set_fullscreen(0);
        }
        if (p_SDL_GetDisplayBounds && p_SDL_SetWindowBordered && p_SDL_SetWindowSize &&
            p_SDL_SetWindowPosition &&
            ((fn_get_bounds_t)p_SDL_GetDisplayBounds)(0, &bounds) == 0 &&
            bounds.w > 0 && bounds.h > 0) {
            ((fn_set_bordered_t)p_SDL_SetWindowBordered)(g_proxy_sdl_window, 0);
            ((fn_set_size_t)p_SDL_SetWindowSize)(g_proxy_sdl_window, bounds.w, bounds.h);
            ((fn_set_pos_t)p_SDL_SetWindowPosition)(g_proxy_sdl_window, bounds.x, bounds.y);
            LOG_INFO("launch: -borderless applied %dx%d at %d,%d", bounds.w, bounds.h, bounds.x, bounds.y);
        } else {
            LOG_WARN("launch: -borderless unavailable (SDL window fns/bounds missing)");
        }
    }
```

- [ ] **Step 4: Build**

Run the standard build command (without `update_ext.c`/`-lbcrypt` — those arrive in Task 2).
Expected: exit 0.

- [ ] **Step 5: Runtime verify (manual, game closed → copy DLL)**

Copy `build/SDL2_test.dll` over `SDL2.dll`, run `eggnoggplus.exe -borderless -log`.
Expected: desktop-sized window without a border at 0,0; gameplay/menus not cropped; `launch: -borderless applied WxH` in the console. Also verify `eggnoggplus.exe -fullscreen -borderless` picks fullscreen.

- [ ] **Step 6: Commit**

```bash
git add eggnoggplus-win/hooks.c eggnoggplus-win/stubs.c
git commit -m "feat(launch): -borderless arg = borderless fullscreen window"
```

---

### Task 2: `update_ext` — version check, download, verify, swap

**Files:**
- Create: `update_ext.h`, `update_ext.c`
- Modify: `compile.sh` (add `update_ext.c`, `-lbcrypt`), `ONLINE_MULTIPLAYER.md` (§Build Command, same change), `hooks.c` (call `update_ext_boot()` at the end of `hooks_init`, include `update_ext.h`)

**Interfaces:**
- Consumes: WinHttp (pattern already proven in `lua_manager.c:10860+`), `log.h` LOG_* macros, `mods/modframework.cfg` (new key `auto_update`, default 1; same parse style as `log.c:15-57`).
- Produces (consumed by Task 3):
  - `#define FRAMEWORK_VERSION "1.0"`
  - `typedef enum UpdateStatus { UPDATE_IDLE, UPDATE_CHECKING, UPDATE_UP_TO_DATE, UPDATE_AVAILABLE, UPDATE_APPLYING, UPDATE_RESTART_PENDING, UPDATE_ERROR } UpdateStatus;`
  - `void update_ext_boot(void);` — delete leftover `*.old`, read cfg, spawn async check thread (auto-applies when auto is on)
  - `UpdateStatus update_ext_status(void);`
  - `const char* update_ext_latest_version(void);`
  - `const char* update_ext_status_line(void);` — short human string for a menu row (e.g. "1.1 available - press to update", "restart to finish", "up to date")
  - `int update_ext_auto(void);` / `void update_ext_set_auto(int enabled);` (persists cfg)
  - `void update_ext_begin_apply(void);` — async; no-op unless status is UPDATE_AVAILABLE
  - `int update_ext_notice_active(void);` / `void update_ext_dismiss_notice(void);` — launch toast flag
  - Internal but testable: `int update_version_cmp(const char* a, const char* b);` (dot-separated numeric compare: <0, 0, >0), `int update_sha256_hex(const void* data, size_t len, char out_hex[65]);`, `int update_json_get_string(const char* json, const char* key, char* out, size_t cap);`, `int update_json_scan_files(...)`.

Behavior contract (from the installer spec, rev 3): fetch `https://loafiieee.com/yule/releases/latest.json` (override with cfg key `update_channel_url` for testing); update available iff `update_version_cmp(remote, FRAMEWORK_VERSION) > 0`. Apply = for each file in `files[]`: download `base + path` to `mods/update_staging/<path>`, verify sha256 (one retry), then swap all files: `MoveFileExA("<path>", "<path>.old", MOVEFILE_REPLACE_EXISTING)` then `MoveFileExA(staged, "<path>", MOVEFILE_REPLACE_EXISTING)`; any failure → restore already-swapped files from `.old` and set UPDATE_ERROR. Success → UPDATE_RESTART_PENDING. Never blocks launch; all network work on one background thread (CreateThread), state behind a CRITICAL_SECTION, status reads lock-free via volatile int.

- [ ] **Step 1: Write the standalone test main first (in update_ext.c behind `#ifdef UPDATE_EXT_TEST`)**

```c
#ifdef UPDATE_EXT_TEST
#include <assert.h>
int main(void) {
    char hex[65]; char buf[128];
    /* version compare */
    assert(update_version_cmp("1.0", "1.0") == 0);
    assert(update_version_cmp("1.1", "1.0") > 0);
    assert(update_version_cmp("1.0", "1.1") < 0);
    assert(update_version_cmp("1.10", "1.9") > 0);
    assert(update_version_cmp("2.0", "1.99.99") > 0);
    assert(update_version_cmp("1.0.1", "1.0") > 0);
    /* sha256 test vector: sha256("abc") */
    assert(update_sha256_hex("abc", 3, hex));
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    /* json field extraction (same relaxed style as hooks.c online_json_get_*) */
    {
        const char* j = "{ \"channel_version\": 1, \"version\": \"1.1\", "
                        "\"base\": \"https://x/y/1.1/\", \"files\": ["
                        "{ \"path\": \"SDL2.dll\", \"sha256\": \"aa\", \"size\": 5 }] }";
        assert(update_json_get_string(j, "version", buf, sizeof(buf)) && strcmp(buf, "1.1") == 0);
        assert(update_json_get_string(j, "base", buf, sizeof(buf)) && strcmp(buf, "https://x/y/1.1/") == 0);
    }
    printf("ALL OK\n");
    return 0;
}
#endif
```

- [ ] **Step 2: Run to verify it fails**

Run: `C:\msys64\mingw32\bin\gcc.exe -m32 -DUPDATE_EXT_TEST update_ext.c -o build/update_test.exe -lwinhttp -lbcrypt`
Expected: FAIL — undefined references (functions not yet implemented).

- [ ] **Step 3: Implement update_ext.h + update_ext.c**

Header exactly as in Interfaces above. Implementation notes (all code self-contained in update_ext.c):
- `update_version_cmp`: walk both strings with `strtoul` on dot-separated segments; missing segments are 0.
- `update_sha256_hex`: bcrypt one-shot — `BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)`, `BCryptCreateHash`, `BCryptHashData`, `BCryptFinishHash` into 32 bytes, hex-encode lowercase, close handles. Returns 0 on any NTSTATUS failure.
- `update_json_get_string`/`_int`: copy the proven relaxed scanners from `hooks.c` (`online_json_get_string` / `online_json_get_int`) — search for `"key"` then skip `: "` and copy until unescaped quote. Files array: a dedicated scanner that iterates `{...}` objects inside `"files": [` and extracts `path`/`sha256` per object (cap 8 files, path ≤ 64 chars, sha256 = 64 hex chars).
- HTTP GET: WinHttp with 8s timeouts, TLS, follows the exact call sequence in `lua_manager.c:10860-10990` (CrackUrl → Open → Connect → OpenRequest with WINHTTP_FLAG_SECURE when https → Send/Receive → read loop, cap 8 MB).
- cfg: `update_read_cfg_int("auto_update", 1)` / `update_write_cfg_int("auto_update", v)` and `update_read_cfg_str("update_channel_url", ...)` mirroring `log.c:15-57` (preserve unrelated lines; create `mods/` first).
- Boot: `DeleteFileA` of `SDL2.dll.old`, `lua51.dll.old`, `libgcc_s_dw2-1.dll.old`, `libwinpthread-1.dll.old`, `SDL2_mixer.dll.old` (ignore failures), then start the check thread. Check thread: fetch+parse; if newer → status UPDATE_AVAILABLE + notice flag; if `update_ext_auto()` → immediately run the apply routine on the same thread (ends at UPDATE_RESTART_PENDING, notice text says "updated - restart to apply").
- Apply routine refuses to run while an online match is live? Not needed — file swap doesn't touch the loaded image. Keep it simple.

- [ ] **Step 4: Run the standalone tests**

Run: `C:\msys64\mingw32\bin\gcc.exe -m32 -DUPDATE_EXT_TEST update_ext.c -o build/update_test.exe -lwinhttp -lbcrypt && ./build/update_test.exe`
Expected: `ALL OK`.

- [ ] **Step 5: Wire into the framework**

- `hooks.c`: `#include "update_ext.h"`; at the end of `hooks_init` (near the `hooks_init: custom MODS menu ready` log, ~line 13355) add `update_ext_boot();`.
- `compile.sh` + `ONLINE_MULTIPLAYER.md` build command: add `update_ext.c` and `-lbcrypt`.

- [ ] **Step 6: Build the DLL**

Run the standard build command (now with update_ext.c + -lbcrypt).
Expected: exit 0.

- [ ] **Step 7: End-to-end verify against a local channel**

- `mkdir -p /tmp/chan/1.1` (in the scratchpad or a temp dir), copy the 5 channel DLLs there, write `latest.json` with `"version": "1.1"`, correct sha256s (`sha256sum`), and `"base"` pointing at `http://127.0.0.1:8788/1.1/`; serve with `python -m http.server 8788`.
- Add `update_channel_url=http://127.0.0.1:8788/latest.json` to `mods/modframework.cfg`, set `auto_update=0`.
- Run the game with `-log`: expect `update: 1.1 available (local 1.0)` style log, no download.
- Set `auto_update=1`, relaunch: expect download logs, sha256-verified swap, `.old` files present, status restart-pending; relaunch again: `.old` files deleted.
- Corrupt one sha256 in latest.json, clear staging, relaunch: expect verify-fail + retry + UPDATE_ERROR log, game still boots, original DLLs untouched.
- Remove the cfg override afterward.

- [ ] **Step 8: Commit**

```bash
git add eggnoggplus-win/update_ext.c eggnoggplus-win/update_ext.h eggnoggplus-win/hooks.c eggnoggplus-win/compile.sh eggnoggplus-win/ONLINE_MULTIPLAYER.md
git commit -m "feat(update): in-game version check + stage-and-swap updater on the yule release channel"
```

---

### Task 3: Update UI — mods-menu rows + launch notice

**Files:**
- Modify: `hooks.c` (FW_SETTING defines ~line 316, `rebuild_rows` ~2794, `adjust_selected` ~3002, `activate_selected` ~3042, mods-menu per-frame tick, toast render call site)

**Interfaces:**
- Consumes: everything `update_ext.h` produces (Task 2).
- Produces: nothing consumed later.

- [ ] **Step 1: Add FW settings + rows**

```c
#define FW_SETTING_LOG_CONSOLE 0
#define FW_SETTING_AUTO_UPDATE 1
#define FW_SETTING_UPDATE_NOW  2
```

In `rebuild_rows` after the log-console row:

```c
    rows_add(ROW_FW_TOGGLE, 1, -1, FW_SETTING_AUTO_UPDATE,
             "  Auto-update", update_ext_auto() ? "ON" : "OFF");
    {
        UpdateStatus us = update_ext_status();
        if (us != UPDATE_IDLE && us != UPDATE_CHECKING && us != UPDATE_UP_TO_DATE) {
            rows_add(ROW_FW_TOGGLE, us == UPDATE_AVAILABLE ? 1 : 0, -1, FW_SETTING_UPDATE_NOW,
                     "  Framework update", update_ext_status_line());
        }
    }
```

- [ ] **Step 2: Handle activation/adjust**

In BOTH `adjust_selected` (ROW_FW_TOGGLE branch, ~3002) and `activate_selected` (~3042):

```c
        } else if (row->cfg_index == FW_SETTING_AUTO_UPDATE) {
            update_ext_set_auto(!update_ext_auto());       /* adjust: use delta like log console */
        } else if (row->cfg_index == FW_SETTING_UPDATE_NOW) {
            update_ext_begin_apply();
        }
```

(adjust variant mirrors the log-console `delta > 0 ? 1 : (delta < 0 ? 0 : !...)` shape for the auto toggle.)

- [ ] **Step 3: Live row refresh + notice dismiss**

In the mods menu per-frame update (the function that runs while `g_mods_state` is active — the same place `mods_cursor_tick()` is driven): keep a `static UpdateStatus s_last;` — when `update_ext_status()` changes, call `rebuild_rows()`. On mods-menu entry (`MODS: entering mods menu` log site, ~11594) call `update_ext_dismiss_notice()`.

- [ ] **Step 4: Launch toast**

Find the call site of `online_challenge_toast_render()` (declared hooks.c:822) and add `update_notice_render();` next to it. Implement `update_notice_render` in hooks.c by mirroring `online_challenge_toast_render`'s drawing calls (~9397+): when `update_ext_notice_active()`, draw a small one-line panel bottom-right: `"Yule <ver> available - MODS menu to update"` (or `"Yule updated to <ver> - restart to apply"` when status is UPDATE_RESTART_PENDING). Non-interactive, fades after ~8 seconds using a static frame counter, then `update_ext_dismiss_notice()`.

- [ ] **Step 5: Build + verify**

Standard build; run with the Task 2 local channel (auto off): expect toast on the main menu, Framework rows in MODS menu, Update row triggers download → row text flips to "restart to finish".

- [ ] **Step 6: Commit**

```bash
git add eggnoggplus-win/hooks.c
git commit -m "feat(update): mods-menu auto-update toggle + update row + launch notice"
```

---

### Task 4: Automatic gameplay-mod suspension during online play

**Files:**
- Modify: `lua_manager.c` (LoadedMod struct ~539, guard + suspension machinery, MUTATING bindings), `lua_manager.h` (new API), `hooks.c` (mods menu lock/labels; the begin/end call sites land in Tasks 5–6 but wire the clear-path calls here)

**Interfaces:**
- Consumes: `LoadedMod` closures (`mod_from_upvalue`, lua_manager.c:2992), existing `enabled` dispatch checks.
- Produces (consumed by Tasks 5–6):
  - `void lua_manager_online_suspend_begin(void);` — suspend every enabled mod with `uses_gameplay_api || bot_provider`; sets the online window flag
  - `void lua_manager_online_suspend_end(void);` — clear window + resume all
  - `int lua_manager_online_suspension_active(void);`
  - `int lua_manager_get_mod_suspended(int mod_index);`

- [ ] **Step 1: Struct + window flag + guard**

Add to `struct LoadedMod` (next to `enabled`): `int suspended;` and `int uses_gameplay_api;`. Add module-level `static int g_online_suspension = 0;` and:

```c
static int mod_is_active(const LoadedMod* m) {
    return m && m->enabled && !m->suspended;
}

/* Top of every gameplay-MUTATING Lua binding. Marks the calling mod as
 * gameplay-affecting (sticky for the session); while an online match is live
 * the call is blocked BEFORE it can touch native state and the mod is
 * suspended on the spot, so determinism holds. */
static int mod_gameplay_blocked(lua_State* Ls, LoadedMod* mod, const char* fn) {
    if (!mod) return 0;
    mod->uses_gameplay_api = 1;
    if (!g_online_suspension) return 0;
    if (!mod->suspended) {
        mod->suspended = 1;
        LOG_WARN("mods: '%s' suspended for online play (called %s mid-match)", mod->id, fn);
    }
    return luaL_error(Ls, "yule: %s is unavailable during online play", fn);
}
```

And the four public functions per the Interfaces block (begin loops enabled mods, suspends `uses_gameplay_api || bot_provider` with an INFO log each; end resumes all suspended with an INFO log; both idempotent).

- [ ] **Step 2: Gate dispatch on `mod_is_active`**

Run: `grep -n "mod->enabled\|g_mods\[mi\].enabled\|m->enabled" lua_manager.c`
Replace the check with `mod_is_active(...)` at every **dispatch** site — callbacks (on_frame/on_tick/on_tick_post/on_event/on_layout), draw/UI dispatch, input binds, console-command dispatch, menu-mode info (12396/13198/13256/13287/13342/13392/13462/13505 region), bot provider (13647), asset sheets (3793). Do NOT touch: enable/disable machinery (13580–13629), the mods-menu info push (6670 — see Step 4), storage/config persistence.

- [ ] **Step 3: Guard the MUTATING bindings**

Insert after the `LoadedMod* mod = mod_from_upvalue(Ls);` line of each of these bindings (locate each with `grep -n 'lua_setfield(Ls, -2, "<name>")' lua_manager.c` and walk up to the C function):

`set_input`, `input_override`, `input_clear`, `block_raw_input`, `block_next_tick`, `set_rng_seed`, `set_native_tick`, `simulate_ticks`, `apply_snapshot`, `apply_full_state_blob`, `apply_sword_snapshot`, `set_map_selector`, `start_match`, `arm_ai_match`, `register_bot_provider`, `reload_all`, `goto_main_menu`, `enter_state`, `leave_state`, `set_player_colour_index`, `set_player_color_index`, and `room_tile` **iff** it writes tiles (check the C body; skip if read-only).

```c
    if (mod_gameplay_blocked(Ls, mod, "game.set_input")) return 0;
```

(the string names the binding; `luaL_error` longjmps so the `return 0` is shape only.)

**Audit criteria** for anything not on the list: guard it iff it writes native sim state, injects/blocks input, changes flow into/out of gameplay, or re-registers code mid-session. Render-only setters (`set_player_render_colours`/`colors`, `set_player_body_hidden`, `set_player_sword_idle_offset`), draw/UI/audio/storage/http/fs/log stay unguarded.

- [ ] **Step 4: Mods-menu labels + lock**

- `lua_manager.h`/`.c`: add `int lua_manager_get_mod_suspended(int mod_index);`.
- `hooks.c` `rebuild_rows`: for ROW_MOD_TOGGLE rows, when `lua_manager_get_mod_suspended(mi)` show right text `"SUSPENDED (online)"` instead of ON/OFF.
- `adjust_selected` + `activate_selected` ROW_MOD_TOGGLE branches: `if (lua_manager_online_suspension_active()) return;` — no enable/disable changes during a match.
- `hooks.c` `online_clear_match_state`: add `lua_manager_online_suspend_end();` (covers finish/abort). The `begin` call is wired in Task 6's start path (and nothing calls it until then, so this task is inert online — safe to land).

- [ ] **Step 5: Build + offline sanity**

Standard build. Run offline with ai_opponent enabled and play a VS AI round: everything works (no suspension window). Grep `modframework.log` for zero `suspended` lines.

- [ ] **Step 6: Commit**

```bash
git add eggnoggplus-win/lua_manager.c eggnoggplus-win/lua_manager.h eggnoggplus-win/hooks.c
git commit -m "feat(mods): auto-classify gameplay mods via API usage; suspend them during online play"
```

---

### Task 5: P2P connect retry (fresh socket per attempt)

**Files:**
- Modify: `hooks.c` (new retry state; replace the single-shot start at ~12170–12204; replace the connect-timeout block in `online_monitor_active_match_state` ~12029; extend `online_pump_p2p_probe` ~7133; stash endpoints in the `p2p_peer` handler ~7085)

**Interfaces:**
- Consumes: `ggpo_net_stop/start_host/start_join_deferred/set_peer/add_peer_candidate/active/connected/local_port` (ggpo_net.h), existing `stop_ggpo_net`/`start_ggpo_net_host`/`start_ggpo_net_join_deferred` wrappers (hooks.c ~5308–5378), `online_abort_connect_timeout`, `online_build_net_diag`.
- Produces (consumed by Task 6): `static void online_connect_start_attempt(int first);`, `static void online_connect_retry_tick(void);`, `static OnlineConnectRetry g_online_connect;` (fields below), `online_connect_reset()`.

- [ ] **Step 1: Retry state + constants**

Replace `#define ONLINE_CONNECT_TIMEOUT_TICKS 720` with:

```c
/* Hole-punch success is NAT-port-mapping luck (~25%/attempt); N attempts with
 * FRESH sockets (new local port => new NAT mapping) multiply the odds. The
 * server re-relays our new endpoint to the peer automatically when the probe
 * pump re-announces from the new socket - no server change needed. */
#define ONLINE_CONNECT_ATTEMPT_TICKS 480   /* ~8s per attempt */
#define ONLINE_CONNECT_MAX_ATTEMPTS 3

typedef struct OnlineConnectRetry {
    int attempts;            /* attempts started (1-based once connecting) */
    int wait_ticks;          /* ticks waiting in the current attempt */
    char p2p_role[8];        /* "host" / "join" */
    int local_port;          /* configured port; attempt 1 only, retries bind 0 */
    char peer_host[64];  int peer_port;
    char public_host[64]; int public_port;
    char lan_host[64];    int lan_port;
    int probe_cooldown, probe_logged, probe_warned;  /* moved from active match */
} OnlineConnectRetry;
static OnlineConnectRetry g_online_connect;
static void online_connect_reset(void) { memset(&g_online_connect, 0, sizeof(g_online_connect)); }
```

Move the probe bookkeeping: delete `p2p_probe_cooldown/logged/warned` from the active-match struct (~1095) and switch `online_pump_p2p_probe` to the `g_online_connect` copies.

- [ ] **Step 2: Stash peer endpoints as they arrive**

In `online_server_begin_pending_match` (~7020): `online_connect_reset();` then copy `p2p_role` (after `online_normalize_pending_match_ports`), `local_port`, `peer_host/port` into `g_online_connect`. In the `p2p_peer` handler (~7085–7129), alongside the existing `ggpo_net_set_peer`/`add_peer_candidate` calls, copy primary/public/lan host:port into `g_online_connect`.

- [ ] **Step 3: The attempt starter**

```c
static void online_connect_start_attempt(int first) {
    char err[256]; err[0] = '\0';
    int port = first ? g_online_connect.local_port : 0;
    if (!first && ggpo_net_active()) stop_ggpo_net("connect retry");
    g_online_connect.attempts++;
    g_online_connect.wait_ticks = 0;
    if (_stricmp(g_online_connect.p2p_role, "host") == 0) {
        start_ggpo_net_host((uint16_t)port, "online server match");
    } else {
        start_ggpo_net_join_deferred((uint16_t)port, "online server match");
    }
    if (!ggpo_net_active()) {
        online_hub_set_status("P2P session failed to start.");
        return;
    }
    if (g_online_pending_match.active) ggpo_net_hold_gameplay();  /* Task 6; before Task 6 lands this call does not exist yet - add it there */
    if (g_online_connect.peer_host[0] && g_online_connect.peer_port > 0)
        (void)ggpo_net_set_peer(g_online_connect.peer_host, (uint16_t)g_online_connect.peer_port, err, sizeof(err));
    if (g_online_connect.public_host[0] && g_online_connect.public_port > 0)
        (void)ggpo_net_add_peer_candidate(g_online_connect.public_host, (uint16_t)g_online_connect.public_port, err, sizeof(err));
    if (g_online_connect.lan_host[0] && g_online_connect.lan_port > 0)
        (void)ggpo_net_add_peer_candidate(g_online_connect.lan_host, (uint16_t)g_online_connect.lan_port, err, sizeof(err));
    /* fresh socket => force the probe pump to re-announce the new mapping */
    g_online_connect.probe_cooldown = 0;
    g_online_connect.probe_logged = 0;
    g_online_connect.probe_warned = 0;
    if (g_online_connect.attempts > 1) {
        char status[96];
        snprintf(status, sizeof(status), "Connecting... (attempt %d/%d)",
                 g_online_connect.attempts, ONLINE_CONNECT_MAX_ATTEMPTS);
        online_hub_set_status(status);
        LOG_INFO("online.p2p: retry attempt %d/%d fresh socket local_udp=%u",
                 g_online_connect.attempts, ONLINE_CONNECT_MAX_ATTEMPTS,
                 (unsigned int)ggpo_net_local_port());
    }
}
```

In this task (pre-Task-6), the original start block at ~12170–12204 is replaced by: populate nothing new (already stashed), then `online_connect_start_attempt(g_online_connect.attempts == 0);` followed by the existing active-match begin when `ggpo_net_active()`.

- [ ] **Step 4: The retry tick**

```c
static void online_connect_retry_tick(void) {
    if (!ggpo_net_active() || ggpo_net_connected()) { g_online_connect.wait_ticks = 0; return; }
    if (!g_online_active_match.active && !g_online_pending_match.active) return;
    if (g_online_connect.attempts == 0) return;
    if (++g_online_connect.wait_ticks < ONLINE_CONNECT_ATTEMPT_TICKS) return;
    if (g_online_connect.attempts >= ONLINE_CONNECT_MAX_ATTEMPTS) {
        online_abort_connect_timeout();
        return;
    }
    LOG_WARN("online.p2p: attempt %d did not connect in %d ticks; retrying",
             g_online_connect.attempts, ONLINE_CONNECT_ATTEMPT_TICKS);
    online_connect_start_attempt(0);
}
```

Replace the `connect_wait_ticks` block in `online_monitor_active_match_state` (12035–12044) with `online_connect_retry_tick();` (delete the `connect_wait_ticks` field), and also call `online_connect_retry_tick()` right after `online_pump_p2p_probe()` in BOTH pump sites (~7351 and ~12548) so it ticks during the pending phase too. Update `online_abort_connect_timeout`'s log/message to say `"P2P did not connect after %d attempts"`.

- [ ] **Step 5: Build + two-instance verify**

Standard build; two-instance localhost match. Normal case: connects on attempt 1, no status text. Forced-retry case: add a temporary `if (g_online_connect.attempts < 2) return 0;` at the top of `ggpo_net_send_handshake_burst`... simpler and non-invasive: block UDP between the peers for the first attempt by setting `ggpo.net sim 100 0 0` loss on one client from the console before the match, then `ggpo.net sim 0 0 0` after seeing "attempt 2/3" — expect: retry log with a NEW local port each attempt, server log shows fresh `p2p_peer` re-send (endpoint changed), connect succeeds on the clean attempt, match plays. Exhaustion case: leave loss at 100 — expect abort to hub after 3 attempts (~24s) with net.diag lines in the log.

- [ ] **Step 6: Commit**

```bash
git add eggnoggplus-win/hooks.c
git commit -m "feat(online): retry the hole punch with fresh sockets (3 attempts) instead of one-and-done"
```

---

### Task 6: Prematch punch during the countdown (gameplay hold gate)

**Files:**
- Modify: `ggpo_net.c` / `ggpo_net.h` (hold gate), `hooks.c` (start P2P at match assignment; release in game state; probe pump pending-phase support; cancel paths; suspension begin)

**Interfaces:**
- Consumes: Task 5's `online_connect_start_attempt` / `g_online_connect`; Task 4's `lua_manager_online_suspend_begin/end`.
- Produces:
  - `void ggpo_net_hold_gameplay(void);` — session punches/handshakes but sends **no** state sync and never advances gameplay
  - `int ggpo_net_gameplay_held(void);`
  - `int ggpo_net_release_gameplay(char* err, size_t err_cap);` — host re-captures the CURRENT game state as the start state and opens state sync; join just clears the hold

- [ ] **Step 1: ggpo_net hold gate**

Add `int hold_gameplay;` to the `g_net` struct. Implement the three functions:

```c
void ggpo_net_hold_gameplay(void) {
    if (g_net.active) g_net.hold_gameplay = 1;
}

int ggpo_net_gameplay_held(void) {
    return g_net.active && g_net.hold_gameplay;
}

/* Re-arm the start state from the CURRENT native game state and open the
 * state-sync/input path. Called when the client reaches game state, which is
 * where the old flow used to START the session - so the deterministic start
 * (seed + map applied at countdown end, then mapgen) is identical. */
int ggpo_net_release_gameplay(char* err, size_t err_cap) {
    if (!g_net.active) { ggpo_net_set_err(err, err_cap, "net session is not active"); return 0; }
    if (!g_net.hold_gameplay) return 1;
    if (g_net.mode == GGPO_NET_MODE_HOST) {
        size_t state_len = 0; uint32_t checksum = 0;
        if (!ggpo_ext_save_game_state(g_net.state_blobs, g_net.state_size, &state_len, &checksum, err, err_cap)) return 0;
        memcpy(g_net.initial_state, g_net.state_blobs, state_len);
        g_net.initial_state_len = state_len;
        g_net.initial_checksum = checksum;
        g_net.last_checksum = checksum;
        (void)ggpo_net_set_correction_base(g_net.initial_state, g_net.initial_state_len, checksum);
        g_net.state_synced = 1;
        g_net.remote_state_synced = 0;    /* force a fresh sync burst to the joiner */
        g_net.state_sync_announced = 0;
    }
    g_net.hold_gameplay = 0;
    LOG_INFO("ggpo.net: gameplay released mode=%s frame=%u", ggpo_net_mode_name(), (unsigned int)g_net.frame);
    return 1;
}
```

In `ggpo_net_advance`:
- gate the burst at ~3530: `if (!g_net.hold_gameplay) ggpo_net_send_state_sync_burst();`
- insert the early-out between the desync check (~3564) and the state-sync wait (~3566):

```c
    if (g_net.hold_gameplay) {
        /* Prematch: punch/handshake/keepalive only. No state sync, no inputs,
         * no native ticks until hooks releases gameplay in game state. */
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }
```

`ggpo_net_stop` already `memset`s g_net — hold clears with it.

- [ ] **Step 2: Start the punch at match assignment**

In `online_server_begin_pending_match`, after the input-delay set (~7061) and the Task 5 stash, add:

```c
    lua_manager_online_suspend_begin();
    online_connect_start_attempt(1);
```

and in `online_connect_start_attempt` (Task 5 placeholder) enable the line `if (g_online_pending_match.active) ggpo_net_hold_gameplay();`.

The menu net tick (hooks.c ~12660: `ggpo_net_active() && state != GAME` → `online_advance_net_gameplay_tick`) already services the held socket every frame while the hub shows the countdown; under hold, `ggpo_net_advance` returns before any sim work, `out_advanced` stays 0, and the normal `hooks_finish_game_tick()` fallback runs — no double ticking.

- [ ] **Step 3: Release in game state**

Rewrite the `online_match_pump_launch` game-state branches (~12126–12204). The early-connected branch (12131) and the old start branch (12170) merge into:

```c
    if (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE && !g_online_pending_match.p2p_started) {
        char err[256]; err[0] = '\0';
        online_ensure_native_game_started();
        if (g_online_pending_match.launch_delay_frames > 0) {
            g_online_pending_match.launch_delay_frames--;
            return;
        }
        if (!ggpo_net_active()) {
            /* Early start failed or was torn down - start (an attempt) here, held,
             * then release below on the next pump pass. */
            online_connect_start_attempt(g_online_connect.attempts == 0);
            if (!ggpo_net_active()) { online_hub_set_status("P2P session failed to start."); return; }
        }
        if (!ggpo_net_release_gameplay(err, sizeof(err))) {
            LOG_ERROR("online.p2p: gameplay release failed (%s)", err[0] ? err : "unknown");
            stop_ggpo_net("release failed");
            online_hub_set_status("P2P session failed to start.");
            return;
        }
        g_online_pending_match.p2p_started = 1;
        online_active_match_begin_from_pending();
        g_online_pending_match.active = 0;
        online_hub_set_status("");
        return;
    }
```

Retries after release (mid-game-state, still unconnected) must not re-hold: the `g_online_pending_match.active` condition in `online_connect_start_attempt` handles that; but a retry DOES need re-release — after a retry start in the released phase, call `ggpo_net_release_gameplay` immediately in `online_connect_start_attempt` when `!g_online_pending_match.active`:

```c
    if (!g_online_pending_match.active) {
        char rerr[256]; rerr[0] = '\0';
        ggpo_net_hold_gameplay();
        if (!ggpo_net_release_gameplay(rerr, sizeof(rerr))) {
            LOG_ERROR("online.p2p: re-release after retry failed (%s)", rerr);
        }
    }
```

(hold+release = re-capture on host so a retried session still ships the correct start state.)

- [ ] **Step 4: Probe pump during pending + cancel paths**

- `online_pump_p2p_probe`: take token/match_id from `g_online_active_match` when active, else from `g_online_pending_match` when `active && ggpo_net_active()`, else return (guards at 7135–7140 otherwise unchanged; cooldown fields now on `g_online_connect` per Task 5).
- Every path that clears a pending match without launching (`match_cancel` handler ~7338 memset, hub-close/user-cancel paths — find with `grep -n "memset(&g_online_pending_match" hooks.c`): add before the memset:

```c
        if (ggpo_net_active() && !g_online_active_match.active) stop_ggpo_net("match cancelled");
        lua_manager_online_suspend_end();
        online_connect_reset();
```

- [ ] **Step 5: Build + two-instance verify**

Standard build; two-instance localhost match via challenge. Expect in logs, in order: match assignment → `suspended for online play` lines (ai_opponent etc.) → punch + `connected` BEFORE the countdown ends → game state → `gameplay released` → `host state synced` → match plays. Verify: seed/map identical on both clients (no desync dump), mods menu shows ai_opponent as `SUSPENDED (online)` mid-match and restored after, match end → `resumed after online play` logs, replaying a second match works (state fully reset). Cancel case: let a challenge match get cancelled during the countdown → session stops, mods resume, hub usable. Combined with Task 5: retry during countdown shows attempt 2 status while still in the hub.

- [ ] **Step 6: Commit**

```bash
git add eggnoggplus-win/ggpo_net.c eggnoggplus-win/ggpo_net.h eggnoggplus-win/hooks.c
git commit -m "feat(online): punch during the prematch countdown; gameplay held until game state (host re-captures start state on release)"
```

---

### Task 7: Online viewport-shift instrumentation

**Files:**
- Modify: `hooks.c` (snapshot helper + call sites)

**Interfaces:**
- Consumes: `p_SDL_GetWindowSize` (stubs.c export), `glGetIntegerv` (opengl32, already linked), `p_main_is_fullscreen`, Task 6's release point.
- Produces: log lines for the eventual root-cause fix (separate follow-up once the user reproduces).

- [ ] **Step 1: Snapshot helper**

```c
#define GL_VIEWPORT_ENUM 0x0BA2
void __stdcall glGetIntegerv(unsigned int pname, int* params);

/* Diagnosis for the intermittent "online gameplay viewport is shifted" report:
 * log window vs GL viewport whenever we enter online gameplay and whenever
 * either changes mid-match, so the first repro pinpoints who moved it. */
static void online_log_viewport_snapshot(const char* tag) {
    int ww = -1, wh = -1;
    int vp[4] = {-1, -1, -1, -1};
    typedef void (__cdecl *fn_get_size_t)(void*, int*, int*);
    extern void* p_SDL_GetWindowSize;
    if (p_SDL_GetWindowSize && g_proxy_sdl_window)
        ((fn_get_size_t)p_SDL_GetWindowSize)(g_proxy_sdl_window, &ww, &wh);
    glGetIntegerv(GL_VIEWPORT_ENUM, vp);
    LOG_INFO("online.viewport[%s]: window=%dx%d gl_viewport=%d,%d %dx%d fullscreen=%d",
             tag, ww, wh, vp[0], vp[1], vp[2], vp[3],
             (p_main_is_fullscreen && p_main_is_fullscreen()) ? 1 : 0);
}
```

(If `glGetIntegerv` collides with the proxy's own SDL/GL exports at link time, call it via `wglGetProcAddress`-free direct import — it links from `-lopengl32`; a naked-stub collision would surface as a duplicate-symbol build error, in which case rename the local declaration and `GetProcAddress(GetModuleHandleA("opengl32.dll"), "glGetIntegerv")` into a function pointer instead.)

- [ ] **Step 2: Call sites**

- In the Task 6 release branch, right after `online_active_match_begin_from_pending();`: `online_log_viewport_snapshot("match-start");`
- In `online_monitor_active_match_state`, when the match is active and connected: keep `static int s_ww, s_wh, s_vp[4];` of the last snapshot; re-read each tick (cheap — two calls) and on any change log `online_log_viewport_snapshot("changed");` (seed the statics at match-start).
- On match end (`online_clear_match_state`): `online_log_viewport_snapshot("match-end");`

- [ ] **Step 3: Build + verify + hand off**

Standard build; two-instance localhost match: expect exactly one `online.viewport[match-start]` line per client with sane values (e.g. `window=1280x720 gl_viewport=0,0 1280x720`), no `changed` spam during a static-window match, one `match-end` line. Resize mid-match (F1) → `changed` lines appear. The actual fix waits for a user repro with these logs (root cause + defensive guard land as a follow-up).

- [ ] **Step 4: Commit**

```bash
git add eggnoggplus-win/hooks.c
git commit -m "diag(online): viewport/window snapshot logging for the online viewport-shift report"
```

---

### Task 8: Docs, TODO, release notes, final build

**Files:**
- Modify: `TODO.txt`, `MODDING.md` (document auto-suspension + that manifests need no flag), `ONLINE_MULTIPLAYER.md` (retry + prematch punch + updater notes)

- [ ] **Step 1: Update TODO.txt**

Remove lines 9–11 (version check, auto-update, -borderless), 15 (mods disabled online), 18 (P2P retry), 19 (prematch countdown). Replace line 14 with: `- online viewport shift: instrumentation shipped (online.viewport log lines); needs a repro capture to root-cause`.

- [ ] **Step 2: Document**

- `MODDING.md`: new section "Online play and mods" — mods that call gameplay-mutating API functions (input injection, state/RNG/tile/tick writes, match control, bot providers) are auto-suspended during online matches and resume after; render/UI/audio/storage/http APIs stay live; no manifest flag exists or is needed; mid-match mutating calls raise `yule: <fn> is unavailable during online play`.
- `ONLINE_MULTIPLAYER.md`: under Online Hub / Server Status, note the connect retry (3 attempts, fresh socket each, server re-relay), the countdown-overlapped punch + gameplay hold gate, and the in-game updater (channel contract from the install-script spec).

- [ ] **Step 3: Final build of both DLLs (game closed)**

Standard build to `build/SDL2_test.dll`, then the same command with `-o SDL2.dll`. Expected: both exit 0.

- [ ] **Step 4: Commit**

```bash
git add eggnoggplus-win/TODO.txt eggnoggplus-win/MODDING.md eggnoggplus-win/ONLINE_MULTIPLAYER.md
git commit -m "docs: TODO batch landed - updater, mod auto-suspension, punch retry, prematch punch, -borderless, viewport diag"
```
