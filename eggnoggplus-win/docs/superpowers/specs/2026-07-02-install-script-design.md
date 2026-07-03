# EGGNOGG+ Framework (Yule) Install Script — Design

**Date:** 2026-07-02 (rev 2 — hosted distribution, Yule manifest dir, owner's Steam artwork)
**Status:** Draft for user review
**Context:** First public release of the mod framework. The AI opponent mod ships later;
this installer is mod-agnostic. Release files are hosted on the owner's server
(`eggnogg.loafiieee.com`, which already runs the online hub backend) and downloaded by
the script — the same hosted contract later powers patch scripts and eventually fully
in-game updating (the framework DLL already links winhttp).

## Goal

A double-clickable installer that (1) downloads (or adopts) the game into a location
safe from Downloads purges, (2) makes it launchable from the Windows search bar, (3)
adds it to Steam as a non-Steam game with the owner's existing artwork, (4) turns off
the framework log console by default, and (5) records a machine-readable manifest under
`%LOCALAPPDATA%\Yule\` that all future update mechanisms use to find and service the
install automatically.

## Non-goals

- No uninstaller in v1 (manifest records enough to build one later).
- No in-game updater in v1 — but the hosted **release channel contract** is defined here
  so the C-side updater can adopt it unchanged later.
- No code signing / no compiled exe.

## Distribution model

Two artifacts, both tiny:

```
EGGNOGG+_installer.zip     <- what users download
  INSTALL.bat              <- one-liner: powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
  install.ps1              <- everything (Steam artwork embedded as base64)
```

Game + framework files are **downloaded from the release channel** at install time.
If an `EGGNOGG+/` folder (or `eggnoggplus.exe` alongside) sits next to the script, the
installer instead **adopts** that local copy first and then updates it from the channel
— this covers existing users who already have the game in Downloads, and offline/dev
testing.

## Hosted release channel (the long-term contract)

Everything below `https://eggnogg.loafiieee.com/releases/`:

```
releases/
  latest.json
  <version>/<path...>      (e.g. 1.0/SDL2.dll, 1.0/eggnoggplus.exe, 1.0/mods/_official_cosmetics/main.lua)
```

`latest.json`:
```json
{
  "channel_version": 1,
  "version": "1.0",
  "notes": "first public release",
  "base": "https://eggnogg.loafiieee.com/releases/1.0/",
  "files": [
    { "path": "eggnoggplus.exe",          "sha256": "...", "size": 4046848, "overwrite": true },
    { "path": "SDL2.dll",                 "sha256": "...", "size": 1206053, "overwrite": true },
    { "path": "SDL2_real.dll",            "sha256": "...", "size": 1400000, "overwrite": true },
    { "path": "mods/modframework.cfg",    "sha256": "...", "size": 64,      "overwrite": false },
    { "path": "mods/<mod>/config.cfg",    "sha256": "...", "size": 120,     "overwrite": false }
  ]
}
```

- `overwrite: false` = install-if-missing (user-editable configs keep user edits).
- User data is never listed: `mods/*/storage.cfg`, logs, `install.json` stay untouched.
- Consumers (install script now; patch script and in-game updater later) all do the
  same thing: fetch `latest.json`, compare each file's sha256 against disk, download
  only mismatches into place, verify sha256 after download, retry once, else report.
- **Publisher helper** (owner-side deliverable, `tools/build_release.ps1` in the repo):
  takes a built game folder + version string → emits the uploadable `releases/<version>/`
  tree + `latest.json` with hashes/sizes, with an exclude list (logs, storage,
  desync dumps, ghidra/, docs/, build/, *.c/*.h sources) and the overwrite-false list.

## Installer flow (interactive, Y/n per step, defaults = yes)

0. **Re-exec from `%TEMP%`** so the script can move/delete the folder it shipped in
   (the open `.bat` otherwise locks it). Original location passed as an argument.
1. **Locate/obtain the game.**
   - Local `EGGNOGG+/` or `eggnoggplus.exe` next to the original script → **adopt** it.
   - Otherwise → fresh install: create the install dir and download everything from the
     channel. (No local copy + no internet → clear abort message.)
2. **[Y/n] Official spot:** `%LOCALAPPDATA%\EGGNOGG+`.
   - Adopted local copy: move it there (same-volume `Move-Item`, else copy+verify+delete;
     leftover source that can't be removed gets a `MOVED - SAFE TO DELETE.txt` marker).
   - Fresh download: downloads straight into it (no move step needed).
   - Existing manifest found → **maintenance mode** (see re-run behavior).
   - Declining keeps/uses the current folder; later steps use that path.
3. **Update from channel:** fetch `latest.json`, sync files per the contract above
   (adopted copies get patched to current; fresh downloads verify clean). Game process
   must not be running (detect `eggnoggplus`, prompt to close — SDL2.dll locks).
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
   (preserving other lines). Additionally the **release build flips the code default**
   in `log.c` (`g_console_visible` 1 → 0) so a wiped cfg stays quiet; devs re-enable via
   cfg.
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
  "channel_url": "https://eggnogg.loafiieee.com/releases/latest.json",
  "installed_at": "2026-07-02T09:41:00",
  "installer_version": 1,
  "moved_by_installer": true,
  "start_menu_shortcut": "C:\\...\\Programs\\EGGNOGG+.lnk",
  "steam": { "applied": true, "appid": 3123456789, "accounts": ["1423819074"] },
  "steps": { "obtain": "adopted", "move": "ok", "sync": "ok", "shortcut": "ok", "steam": "ok", "log_console": "ok" }
}
```

- Fallback copy written inside the game folder (`install.json`).
- Updaters: read `install_dir` + `channel_url`, verify `eggnoggplus.exe` exists, sync
  files per the channel contract, bump `framework_version`. Missing manifest → ask the
  user for the folder, then recreate it. `manifest_version` bumps only on breaking
  changes; fields are add-only otherwise.
- The eventual in-game updater reads the same file and channel; nothing else changes.

## Re-run behavior (idempotence)

Manifest exists → maintenance mode: "Found existing install at `<path>`", then the same
Y/n steps — channel sync (this IS the update path until a dedicated script ships),
shortcut refresh, Steam re-apply, manifest rewrite. User data preserved per the
channel's `overwrite` flags plus the never-listed patterns.

## Key risks & mitigations

- **shortcuts.vdf corruption**: always back up first; minimal parse (append/replace one
  entry); on parse anomaly restore backup, skip that account, continue.
- **Download integrity / server down**: sha256 verify + one retry per file; on failure,
  keep whatever local copy exists and say exactly which files are stale. Adopt-mode
  installs still complete offline (sync step reports "skipped: offline").
- **Self-move locks**: solved by `%TEMP%` re-exec + marker-file fallback.
- **Game running during install**: process check before sync/move, prompt to close.
- **HTTPS on old PowerShell**: force TLS 1.2 (`[Net.ServicePointManager]::SecurityProtocol`)
  before any download.

## Testing checklist (manual, dev machine)

1. Fresh online install (no local game): downloads to `%LOCALAPPDATA%\EGGNOGG+`, search
   bar finds it, Steam entry + artwork on both local accounts, no log console, both
   manifests written.
2. Adopt-mode from a zip in Downloads: game moved, Downloads leftover gone (or marked),
   then patched to channel-current.
3. Each step declined individually; offline adopt-mode install.
4. Re-run → maintenance mode; user cfg/storage survive; edited `config.cfg` not clobbered
   (overwrite-false), stale `SDL2.dll` replaced (overwrite-true).
5. Steam variants: not installed / running (graceful close + relaunch) / two accounts.
6. Publisher helper: build a channel tree from the current game folder, serve it
   locally (`python -m http.server` or the live server), point `channel_url` at it,
   verify end-to-end.
7. Patch simulation: 5-line PS snippet reads the Yule manifest, syncs the channel,
   bumps version — proves the contract.
