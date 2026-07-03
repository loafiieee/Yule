# EGGNOGG+ Framework Install Script — Design

**Date:** 2026-07-02
**Status:** Draft for user review
**Context:** First public release of the mod framework. The AI opponent mod ships later;
this installer is mod-agnostic and must not need changes when new mods are added to the zip.

## Goal

A double-clickable installer shipped inside the release zip that (1) relocates the game out
of fragile locations like Downloads, (2) makes it launchable from the Windows search bar,
(3) adds it to Steam as a non-Steam game with custom artwork, (4) turns off the framework
log console by default, and (5) records a machine-readable manifest that all future
patch/update scripts use to find and service the install automatically.

## Non-goals

- No uninstaller in v1 (manifest records enough to build one later).
- No auto-update/patch script in v1 — but its **contract** (the manifest) is defined here.
- No code signing / no compiled exe.
- No game-content changes; the installer only places files and writes configs.

## Release zip layout

```
EGGNOGG+_vX.Y.zip
  EGGNOGG+/            <- the game folder (exe, SDL2.dll framework, SDL2_real.dll,
                          mods/, maps/, data/, docs...)
  INSTALL.bat          <- double-click entry point
  install.ps1          <- the actual installer (images embedded as base64)
  README.txt
```

`INSTALL.bat` is one line of boilerplate:
`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"` — this exists
purely to bypass PowerShell's execution policy for double-clickers.

## Installer flow (interactive, Y/n per step, defaults = yes)

0. **Re-exec from temp.** The script copies itself (and nothing else) to `%TEMP%` and
   re-launches from there. This frees it to *move* the folder it shipped in — otherwise
   the open `.bat`/`.ps1` files lock the source folder and the move fails.
1. **Locate the game.** The game folder is `EGGNOGG+/` next to the original script path
   (passed to the re-exec'd copy as an argument). Sanity check: `eggnoggplus.exe` and
   `SDL2.dll` exist; abort with a clear message if not.
2. **[Y/n] Move to the official spot:** `%LOCALAPPDATA%\EGGNOGG+`.
   - Same volume → `Move-Item`; cross-volume → copy, verify (file count + total bytes),
     then delete source.
   - Already installed there (manifest exists) → offer **Update in place** (see re-run
     behavior) instead of a second copy.
   - Destination exists but no manifest → ask before overwriting anything.
   - Declining the move keeps the game where it is; all later steps use that path.
3. **[Y/n] Start Menu shortcut** (this is what makes it show up in the Windows search
   bar): create `%APPDATA%\Microsoft\Windows\Start Menu\Programs\EGGNOGG+.lnk` via the
   `WScript.Shell` COM object — target `eggnoggplus.exe`, working directory = game
   folder, icon = the exe's own icon.
4. **[Y/n] Add to Steam** (auto-skipped with a note if no Steam installation is found in
   the registry):
   - If Steam is running: ask, then close it gracefully (`steam://exit` / `-shutdown`,
     wait for process exit, hard-kill only after a timeout with a second confirmation).
   - For **every** account folder under `<Steam>\userdata\<accountid>\config\`:
     - **Back up** `shortcuts.vdf` to `shortcuts.vdf.bak-<timestamp>` first.
     - Append a shortcut entry (binary VDF): AppName `EGGNOGG+`, exe + start dir =
       install path, icon = exe. If an `EGGNOGG+` entry already exists, replace it
       (keeps re-runs idempotent).
     - Compute the non-Steam **appid** (CRC32 of `"<exe>""<appname>"` with the high bit
       set — the standard shortcut appid algorithm) and write the artwork into
       `userdata\<id>\config\grid\`:
       | slot | file | asset |
       |---|---|---|
       | landscape capsule | `<appid>.png` | user-provided |
       | portrait capsule | `<appid>p.png` | user-provided |
       | hero banner | `<appid>_hero.png` | user-provided |
       | logo overlay | `<appid>_logo.png` | user-provided |
       | icon | set in the VDF entry | user-provided or exe icon |
   - Artwork ships **embedded in install.ps1** as a base64 table:
     ```powershell
     $Artwork = @{
       grid   = '<BASE64>'   # 920x430 or 460x215 png
       gridp  = '<BASE64>'   # 600x900 png
       hero   = '<BASE64>'   # 1920x620 png
       logo   = '<BASE64>'   # transparent png
       icon   = '<BASE64>'   # .ico or 256x256 png ('' = use exe icon)
     }
     ```
     Placeholder `''` values mean "skip that slot" so the script works before the final
     art exists. (Owner provides the images; a tiny helper line in the README of the
     repo documents `[Convert]::ToBase64String([IO.File]::ReadAllBytes('x.png'))`.)
   - Relaunch Steam afterward if the script closed it.
5. **Log console off:** write/replace `show_log_console=0` in `mods\modframework.cfg`
   (preserving other lines — same line-based format the framework already uses).
   Additionally, the **release build flips the code default** in `log.c`
   (`g_console_visible` 1 → 0) so even a wiped cfg stays quiet; developers re-enable via
   the cfg or the mods menu toggle.
6. **Write the manifest** (the update contract, see below), then print a summary of
   everything done + the final install path, and pause for a keypress.

Failure policy: every step is independently skippable and failure-isolated — a Steam
hiccup must never abort the move or the manifest write. Errors print plainly and the
script continues to the next step, recording per-step status in the manifest.

## The manifest (contract for future patch/update scripts)

Fixed, install-location-independent path — this is the ONLY thing a future script needs
to know in advance:

```
%LOCALAPPDATA%\EggnoggPlusFramework\install.json
```

```json
{
  "manifest_version": 1,
  "install_dir": "C:\\Users\\me\\AppData\\Local\\EGGNOGG+",
  "framework_version": "X.Y",
  "installed_at": "2026-07-02T09:41:00",
  "installer_version": 1,
  "moved_by_installer": true,
  "start_menu_shortcut": "C:\\...\\Programs\\EGGNOGG+.lnk",
  "steam": {
    "applied": true,
    "appid": 3123456789,
    "accounts": ["12345678"]
  },
  "steps": { "move": "ok", "shortcut": "ok", "steam": "ok", "log_console": "ok" }
}
```

- A copy is also written inside the game folder (`install.json`) as a fallback so a
  patch script dropped next to the exe can work even if the global file is gone.
- Patch/update scripts: read `install_dir`, verify `eggnoggplus.exe` exists there, then
  operate (swap `SDL2.dll`, add/update `mods/*`, bump `framework_version`, append to
  `steps`/history). If the manifest is missing → fall back to asking the user for the
  folder, then **recreate** the manifest.
- `manifest_version` only increments on breaking schema changes; fields are add-only
  otherwise.

## Re-run behavior (idempotence)

Running the installer when a manifest already exists switches to maintenance mode:
"Found existing install at `<path>`" and offers the same Y/n steps — copying the shipped
game folder's contents **over** the existing install (preserving `mods/*/storage.cfg`,
`*.cfg` user configs, and `maps/` additions), refreshing the shortcut, re-applying Steam
entry/artwork, rewriting the manifest. This doubles as the v1 "update script" until a
dedicated one ships.

## Key risks & mitigations

- **shortcuts.vdf corruption** (binary format, hand-rolled writer): always back up
  first; parse minimally (find the `shortcuts` map, append/replace one entry, rewrite);
  on any parse anomaly, restore the backup, skip the account, and continue.
- **Deleting the folder the installer started from**: solved by the temp re-exec (step
  0); if the source folder still can't be fully removed (e.g., user has it open in
  Explorer/a terminal), leave it with a `MOVED - SAFE TO DELETE.txt` marker instead of
  failing.
- **The game is running during install**: detect `eggnoggplus` process and ask the user
  to close it before the move step (SDL2.dll lock would break the copy).
- **OneDrive-redirected %LOCALAPPDATA%** (rare): manifest still works since the path is
  recorded, not assumed.

## Testing checklist (manual, on the dev machine)

1. Fresh install from a zip extracted in Downloads: all four steps yes → game runs from
   `%LOCALAPPDATA%\EGGNOGG+`, appears in Windows search, appears in Steam with artwork,
   no log console on launch, both manifests written, Downloads copy gone.
2. Each step declined individually → later steps still work, manifest `steps` reflects
   skips.
3. Re-run over an existing install → maintenance mode, user cfg/storage preserved.
4. Steam not installed / Steam running / two-account Steam.
5. Same-volume and cross-volume (second drive) moves.
6. Patch-script simulation: a 5-line PS snippet reads the manifest, swaps `SDL2.dll`,
   bumps the version — documents the contract works.
