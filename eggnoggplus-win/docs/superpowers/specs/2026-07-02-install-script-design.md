# EGGNOGG+ Framework (Yule) Install Script — Design

**Date:** 2026-07-02 (rev 3 — stripped DLL-only channel, loafiieee.com/yule URL)
**Status:** Approved direction; rev 3 incorporates stripped-release + URL feedback
**Context:** First public release of the mod framework. The AI opponent mod ships later;
this installer is mod-agnostic. The release channel hosts ONLY the framework DLLs —
verified: the framework defaults/creates everything under `mods/` at runtime, so nothing
else needs shipping. The same hosted contract later powers patch scripts and eventually
fully in-game updating (the framework DLL already links winhttp).

## Goal

A double-clickable installer that (1) installs the framework onto an existing EGGNOGG+
copy and relocates it out of fragile locations like Downloads, (2) makes it launchable
from the Windows search bar, (3) adds it to Steam as a non-Steam game with the owner's
existing artwork, (4) turns off the framework log console by default, and (5) records a
machine-readable manifest under `%LOCALAPPDATA%\Yule\` that all future update mechanisms
use to find and service the install automatically.

## Non-goals

- No uninstaller in v1 (manifest records enough to build one later).
- No in-game updater in v1 — but the hosted **release channel contract** is defined here
  so the C-side updater can adopt it unchanged later.
- No hosting of the game itself — the channel carries framework files only; the
  installer requires an existing EGGNOGG+ copy (vanilla or already-framework'd).
- No code signing / no compiled exe.

## Distribution model

One tiny artifact:

```
EGGNOGG+_framework_installer.zip     <- what users download
  INSTALL.bat        <- one-liner: powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
  install.ps1        <- everything (Steam artwork embedded as base64)
```

The installer needs an existing game copy: either it sits next to one (folder containing
`eggnoggplus.exe`, or an `EGGNOGG+/` subfolder), or it finds one via the Yule manifest,
or the user is prompted for the folder. No game found → friendly message telling the
user to get EGGNOGG+ first, then exit.

## Hosted release channel (the long-term contract)

Everything below `https://loafiieee.com/yule/releases/` (plain static HTTPS hosting; the
URL is a single `$ChannelUrl` variable at the top of every consumer script):

```
yule/releases/
  latest.json
  <version>/SDL2.dll
  <version>/lua51.dll
  <version>/libgcc_s_dw2-1.dll
  <version>/SDL2_mixer.dll
```

**SUPER stripped — the channel is just the framework DLLs** (runtime-import set verified
with objdump on the built proxy):

| file | why |
|---|---|
| `SDL2.dll` | the framework (SDL proxy) |
| `lua51.dll` | LuaJIT, imported by the proxy |
| `libgcc_s_dw2-1.dll` | mingw runtime, imported by proxy + lua51 |
| `libwinpthread-1.dll` | mingw runtime, transitive import of libgcc (fresh installs fail to boot without it) |
| `SDL2_mixer.dll` | optional, LoadLibrary'd for file-based mod SFX/music |

Everything else is either the user's game (`eggnoggplus.exe`, `data/`, `SDL2_real.dll`
via rename — see flow) or auto-created by the framework at runtime (`mods/` tree,
`modframework.log`/`.cfg`, `online_hub.cfg`, per-mod storage — all default/regenerate
when missing; **one code fix required**: `log_init` must `CreateDirectoryA("mods")`
before opening the log file, else file-logging is silently dead on a bare install).

`latest.json`:
```json
{
  "channel_version": 1,
  "version": "1.0",
  "notes": "first public release",
  "base": "https://loafiieee.com/yule/releases/1.0/",
  "files": [
    { "path": "SDL2.dll",            "sha256": "...", "size": 1206053, "overwrite": true },
    { "path": "lua51.dll",           "sha256": "...", "size": 400000,  "overwrite": true },
    { "path": "libgcc_s_dw2-1.dll",  "sha256": "...", "size": 120000,  "overwrite": true },
    { "path": "SDL2_mixer.dll",      "sha256": "...", "size": 150000,  "overwrite": true }
  ]
}
```

- `overwrite: false` remains in the contract for future use (e.g., shipping a default
  config), but v1 ships none.
- User data is never listed and never touched.
- Consumers (install script now; patch script and in-game updater later) all do the
  same loop: fetch `latest.json`, compare each file's sha256 against disk, download
  mismatches, verify sha256 after download, retry once, else report.
- **Publisher helper** (owner-side, `tools/build_release.ps1`): takes the 4 built files
  + a version string → emits the uploadable `releases/<version>/` tree + `latest.json`
  with hashes/sizes.

## Installer flow (interactive, Y/n per step, defaults = yes)

0. **Re-exec from `%TEMP%`** so the script can move/delete the folder it shipped in.
1. **Locate the game** (in order): folder next to the original script path (itself or an
   `EGGNOGG+/` subfolder containing `eggnoggplus.exe`) → existing Yule manifest's
   `install_dir` → prompt the user for a path. Nothing found → "get EGGNOGG+ first"
   message, exit. Game running → prompt to close it (DLL locks).
2. **[Y/n] Move to the official spot:** `%LOCALAPPDATA%\EGGNOGG+` (skipped if it's
   already there). Same-volume `Move-Item`, else copy+verify+delete; unremovable source
   gets a `MOVED - SAFE TO DELETE.txt` marker. Declining keeps the current folder.
3. **Install/update the framework from the channel:**
   - **Vanilla adoption:** if `SDL2_real.dll` is missing AND the existing `SDL2.dll`
     is not ours (detected by scanning it for the `modframework` marker string), rename
     it to `SDL2_real.dll` first — that's the game's real SDL2 the proxy forwards to.
     If `SDL2_real.dll` is missing and `SDL2.dll` IS ours → broken half-install: abort
     this step with a clear message (game would not boot), leave everything untouched.
   - Then the standard channel sync: fetch `latest.json`, hash-compare, download
     mismatches, verify. Offline → step reports "skipped: offline" and the install
     continues (adopted framework copies still work).
4. **[Y/n] Start Menu shortcut** → `%APPDATA%\Microsoft\Windows\Start Menu\Programs\EGGNOGG+.lnk`
   via `WScript.Shell` (target exe, workdir = game folder, icon = exe). This is what
   makes it appear in the Windows search bar.
5. **[Y/n] Add to Steam** (auto-skip with a note if no Steam in registry):
   - If Steam is running: ask, close gracefully (`steam://exit`, wait, hard-kill only
     after timeout + second confirmation), relaunch after.
   - For **every** `userdata\<accountid>\config\`: back up `shortcuts.vdf` to
     `shortcuts.vdf.bak-<timestamp>`, append-or-replace the `EGGNOGG+` entry (binary
     VDF; exe + start dir = install path), compute the shortcut appid (CRC32 of
     `"<exe>""<appname>"`, high bit set) and write artwork to `config\grid\`.
   - **Artwork = the owner's existing set**, embedded in install.ps1 at build time from
     `userdata\1423819074\config\grid\` on the dev machine: `2231133229.png`
     (landscape), `2231133229p.png` (portrait), `2231133229_hero.png`,
     `2231133229_logo.png`, and `2231133229.json` (logo position), renamed to the
     computed appid at install. Icon: exe's own icon unless the source VDF entry names
     one.
6. **Log console off:** write/replace `show_log_console=0` in `mods\modframework.cfg`
   (creating `mods/` if needed, preserving other lines). Additionally the **release
   build flips the code default** in `log.c` (`g_console_visible` 1 → 0) so a wiped cfg
   stays quiet; devs re-enable via cfg. (Same `log.c` change adds the missing
   `CreateDirectoryA("mods")` in `log_init`.)
7. **Write the manifest**, print a summary + install path, pause for a keypress.

Failure policy: steps are independently skippable and failure-isolated — a Steam hiccup
never aborts the file sync or manifest write; per-step status lands in the manifest.

## The Yule manifest (contract for updaters)

Fixed path, independent of install location — the only thing any future tool needs to
know in advance:

```
%LOCALAPPDATA%\Yule\install.json
```

```json
{
  "manifest_version": 1,
  "install_dir": "C:\\Users\\me\\AppData\\Local\\EGGNOGG+",
  "framework_version": "1.0",
  "channel_url": "https://loafiieee.com/yule/releases/latest.json",
  "installed_at": "2026-07-02T09:41:00",
  "installer_version": 1,
  "moved_by_installer": true,
  "adopted_vanilla": true,
  "start_menu_shortcut": "C:\\...\\Programs\\EGGNOGG+.lnk",
  "steam": { "applied": true, "appid": 3123456789, "accounts": ["1423819074"] },
  "steps": { "move": "ok", "sync": "ok", "shortcut": "ok", "steam": "ok", "log_console": "ok" }
}
```

- Fallback copy written inside the game folder (`install.json`).
- Updaters: read `install_dir` + `channel_url`, verify `eggnoggplus.exe` exists, run the
  channel sync loop, bump `framework_version`. Missing manifest → ask the user for the
  folder, recreate it. `manifest_version` bumps only on breaking changes; fields are
  add-only otherwise.
- The eventual in-game updater reads the same file and channel; nothing else changes.

## Re-run behavior (idempotence)

Manifest exists → maintenance mode: "Found existing install at `<path>`", then the same
Y/n steps — channel sync (this IS the update path until a dedicated script ships),
shortcut refresh, Steam re-apply, manifest rewrite. User data untouched (channel lists
only DLLs).

## Key risks & mitigations

- **Wrongly renaming our own proxy to SDL2_real.dll** (would chain the proxy to itself):
  the `modframework` marker-string check prevents it; the half-install case aborts
  loudly instead of guessing.
- **shortcuts.vdf corruption**: back up first; minimal parse (append/replace one entry);
  on parse anomaly restore the backup, skip that account, continue.
- **Download integrity / server down**: sha256 verify + one retry per file; offline
  adopt-mode installs still complete, reporting exactly which files are stale.
- **Self-move locks**: `%TEMP%` re-exec + marker-file fallback.
- **Game running during install**: process check before move/sync, prompt to close.
- **HTTPS on old PowerShell**: force TLS 1.2 before any download.

## Testing checklist (manual, dev machine)

1. **Vanilla adoption**: fresh unmodded EGGNOGG+ folder in Downloads → real SDL2 renamed
   to `SDL2_real.dll`, 4 channel files installed, game boots with framework, `mods/`
   tree self-creates on first run, no log console, search bar finds it, Steam entry +
   artwork on both local accounts, both manifests written, Downloads copy gone/marked.
2. **Framework'd adoption**: current dev folder → no rename, DLLs synced to channel
   versions, user cfg/storage untouched.
3. Each step declined individually; offline install (sync skipped, rest works).
4. Re-run → maintenance mode; stale `SDL2.dll` replaced; user data survives.
5. Steam variants: not installed / running (graceful close + relaunch) / two accounts.
6. Half-install guard: delete `SDL2_real.dll` from a framework'd copy → step 3 aborts
   with the clear message, nothing modified.
7. Publisher helper: build a channel tree from the 4 files, serve locally, point
   `$ChannelUrl` at it, verify end-to-end; then the same against loafiieee.com/yule.
8. Patch simulation: 5-line PS snippet reads the Yule manifest, runs the sync loop,
   bumps version — proves the contract.
