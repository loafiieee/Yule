# Preview Eggnogg color mismatch

The registered `yule://` command targets
`C:\Users\potato\AppData\Local\EGGNOGG+\eggnoggplus.exe`, not the development
folder. Its SDL2.dll had no Eggnogg-color hook or parser strings. The development
folder's installed DLL was also older; prior work had built separate candidates
without installing them. Updating Greggnogg alone therefore exported a setting
that the launched framework did not implement.

The installed and development game executables have identical SHA-256 hashes.
The current source builds to `build/SDL2_preview_color_fix.dll`. The guarded V2
suite passed (`build/preview_color_fix_tests.log`); full production linkage is
recorded in `build/preview_color_fix_link.log`. Installation hash checks and backup
locations are recorded in `build/preview_color_install.log`.

Map generation now logs a configured Eggnogg RGB triple after pinning its map
generation. Greggnogg Help explains which installed copy Preview opens. These
make an old-build mismatch easier to distinguish from a rendering issue.

The same build adds `map.tile_bindings()` under Map API version 8. It returns
detached symbol/key records in deterministic manifest order for reusable script
registration. Guarded tests cover mutation isolation, order, empty lists, bad
arguments, and rollback replay. Both online clients need matching builds.

No game was launched by the automated checks. Visual confirmation remains: click
Preview again and verify E/^ and goal particles use the chosen color. If they do
not, inspect the installed game's `mods/modframework.log` for the new
`Eggnogg color override RGB=` line.
