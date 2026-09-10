# Yule installer for Windows

Extract the complete Windows installer ZIP, close EGGNOGG+, and run
`INSTALL.bat`. The installer locates or asks for the game, installs the verified
framework release, registers `yule://` links for Greggnogg previews, and records
the files it owns.

Run `UNINSTALL.bat` for a conservative uninstall. Maps, mods, saves, and files
changed after installation are preserved.

The installer creates `maps/` beside `mods/` in the installed game folder.
Extract each downloaded map into its own subfolder, for example
`maps/MyMap/data.map` and `maps/MyMap/data.json`. Extract ZIP archives first;
avoid an extra nested folder between `MyMap/` and these two files.
