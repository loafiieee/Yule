# Developer Console

## Open and close
- Press `` ` `` (backtick) to open or close the console.
- `Esc`, `exit`, or `quit` closes the console.

## Input controls
- `Enter`: run current command.
- `Up` / `Down`: browse command history.
- `Left` / `Right`: move cursor.
- `Home` / `End`: jump to start/end of line.
- `Backspace` / `Delete`: edit text at cursor.
- `PageUp` / `PageDown`: scroll console output.
- Mouse wheel: scroll console output.
- `Tab`: autocomplete command names.
- `Ctrl+L`: clear console output.
- History is persisted to `mods\console_history.txt`.

## Command reference

### Core
- `help [topic]`
- `commands` (alias of help)
- `clear`
- `history [count]`
- `echo <text>`
- `console.stats`

### Runtime and system
- `state`
- `state.last`
- `state.return [main|main_initial|options|options_paused|mods|mods_entry]`
- `state.switch <main|main_initial|options|options_paused|mods|mods_entry|console|return>`
- `sys.info`
- `ui.size`
- `time.scale [value|auto]`
- `framework.api`
- `log.level [debug|info|warn|error]`

### Mods
- `mods.count`
- `mods.list`
- `mods.find <text>`
- `mods.info <id>`
- `mods.enable <id|all>`
- `mods.disable <id|all>`
- `mods.toggle <id|all>`
- `mods.config <id>` (alias: `mods.cfg <id>`)
- `mods.config.find <text>` (alias: `mods.cfg.find`)
- `mods.config.get <id> <key>` (alias: `mods.cfg.get`)
- `mods.config.set <id> <key> <value>` (alias: `mods.cfg.set`)
- `mods.config.action <id> <key>` (alias: `mods.cfg.action`)
- `binds.list [id]`
- `binds.find <text>`
- `binds.set <id> <key> <value>`
- `binds.clear <id> <key>`
- `profiles.list`
- `profiles.save <name>`
- `profiles.load <name>`
- `profiles.delete <name>`
- `profiles.current`
- `reload.mods`
- `mods.reload` (alias)
- `reload.assets`

### Lua execution
- `lua <code>` (alias: `eval <code>`)
- `lua.mod <id> <code>` (alias: `eval.mod`)
- `lua.file <path>`

### Logging
- `log.level [debug|info|warn|error]`
- `log.tail [lines]`

### Input override
- `input.show [player]`
- `input.override <player> <mask> [frames] [replace]`
- `input.clear <player>`

## Examples
- `mods.find speed`
- `state.last`
- `state.return options`
- `mods.enable all`
- `mods.toggle speedhack`
- `mods.config.find speed`
- `mods.info speedhack`
- `mods.config speedhack`
- `binds.list speedhack`
- `binds.set speedhack dash space`
- `profiles.save pvp`
- `profiles.load pvp`
- `mods.config.get speedhack max_speed`
- `mods.config.set speedhack max_speed 3.5`
- `mods.config.set speedhack enabled true`
- `mods.config.action speedhack reset_defaults`
- `log.tail 40`
- `lua return 2 + 2`
- `lua.mod speedhack return config.get("max_speed")`
- `lua.file mods\\speedhack\\speedhack.lua`
- `time.scale`
- `time.scale 0.5`
- `time.scale auto`
- `input.override 0 0x10 30 0`
- `input.override 1 0x4 -1 1`
- `input.clear 1`

## Notes
- `mods.enable` and `mods.disable` change runtime event participation for that mod.
- `mods.toggle` flips enabled state immediately.
- `mods.config.set` supports bool/int/float/string config types.
- `time.scale <value>` sets a manual global timescale clamp-limited to `0.05..100`.
- `time.scale auto` returns control back to mod-driven delta-time scaling.
- `lua` and `lua.mod` return values are printed in the console output.
- For string values with spaces, quote them:
  - `mods.config.set my_mod welcome_text "hello world"`
- `input.override` mask accepts decimal or hex (`0x...`).
- `frames < 0` keeps an override active until cleared.
