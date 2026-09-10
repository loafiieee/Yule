# Yule installer for Linux with Wine

Yule currently modifies the Windows EGGNOGG+ build. Install Wine, extract the
Linux installer ZIP, then run:

```sh
bash install-linux.sh
```

The installer asks for an existing Windows `eggnoggplus.exe`, copies that game
to `~/.local/share/yule/EGGNOGG+` by default, installs verified framework files,
and creates a desktop launcher using the selected Wine prefix. It also registers
`x-scheme-handler/yule`, so browser `yule://` links—including Greggnogg preview
links—open in that same prefix and can forward to an already-running game.

Useful noninteractive options include:

```sh
bash install-linux.sh --game-path /path/to/eggnoggplus.exe --yes
bash install-linux.sh --wine-prefix /path/to/prefix
bash install-linux.sh --skip-protocol
bash install-linux.sh --uninstall
```

`UNINSTALL-LINUX.sh` is a convenience wrapper for the final command. Uninstall
removes only files whose hashes still match the installer receipt, restores the
original `SDL2.dll`, and preserves maps, mods, saves, and locally modified files.

The installer creates `maps/` beside `mods/` in the installed game folder.
Extract each downloaded map into its own subfolder, for example
`maps/MyMap/data.map` and `maps/MyMap/data.json`. Extract ZIP archives first;
avoid an extra nested folder between `MyMap/` and these two files.
