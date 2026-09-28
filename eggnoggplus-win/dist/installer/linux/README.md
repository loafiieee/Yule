# Yule installer for Linux (Wine)

Yule currently runs the Windows EGGNOGG+ game through Wine. Extract the complete
Linux installer ZIP and run `bash install-linux.sh`. Python 3 and Wine are required.
The installer finds or asks for `eggnoggplus.exe`, verifies the release before
changing the game, preserves the original `SDL2.dll`, installs `mods/` and `maps/`,
and offers application-menu, `yule://`, and Steam integration.

The final summary says `ok`, `declined`, `skipped-switch`, `no-steam`, `failed`, or
`unsupported` for each integration. Requested integration failures print a
specific reason, are recorded in `~/.local/state/yule/install-linux.json` (or
`$XDG_STATE_HOME/yule/install-linux.json`), and cause exit status 2 even when the
framework itself installed successfully. A successful registration checks the
desktop file and the active `xdg-mime` handler. Browser and Steam launches still
need an actual user-session check; registration cannot prove every desktop
environment will launch a game.

Native Steam userdata is supported. Flatpak and Snap Steam are reported as
`unsupported` because a host Wine launcher cannot be verified inside their
sandboxes. No shortcut is silently written to those installations. Close Steam
when asked; the installer does not force-kill it. A Steam account must have a
`userdata/<account>/config` directory before its shortcut can be installed.

Useful options:

```sh
bash install-linux.sh --game-path /path/to/eggnoggplus.exe --yes
bash install-linux.sh --wine-prefix /path/to/prefix --wine-bin wine
bash install-linux.sh --skip-launcher --skip-protocol --skip-steam
bash install-linux.sh --uninstall
```

The installer makes `maps/` beside `mods/`. Extract a map into a folder such as
`maps/MyMap/data.map` and `maps/MyMap/data.json`. Uninstall removes only
installer-owned files whose hashes still match and preserves maps, mods, saves,
and modified files. `UNINSTALL-LINUX.sh` runs the same uninstall path.
