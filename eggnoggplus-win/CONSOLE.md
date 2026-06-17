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
- `Tab`: context-aware autocomplete — completes command names, and (past the command) mod ids, config keys, bind keys, and keyword/value arguments. With multiple matches it completes the common prefix and lists them; press `Tab` again to cycle (`Shift+Tab` to cycle backward).
- `Ctrl+R`: reverse-search command history — type to filter, `Ctrl+R` for older matches, `Enter` to accept into the input, `Esc` to cancel.
- Unrecognized commands print a "Did you mean: …?" suggestion.
- `Ctrl+L`: clear console output.
- `Ctrl+C`: copy current input, or copy console output if input is empty.
- `Ctrl+V`: paste clipboard text into input.
- Right mouse button: paste clipboard text into input.
- Left mouse button on input row: move cursor.
- Middle mouse button: copy console output.
- History is persisted to `mods\console_history.txt`.

## Command reference

### Core
- `help [topic]`
- `commands` (alias of help)
- `clear`
- `history [count]`
- `echo <text>`
- `console.stats`
- `console.copy [output|input]`

### Runtime and system
- `state`
- `state.last`
- `state.return [main|main_initial|options|options_paused|mods|mods_entry]`
- `state.switch <main|main_initial|options|options_paused|mods|mods_entry|online|console|return>`
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
- `mods.trace <id|all> [on|off]`
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
- `reload.mods`
- `mods.reload` (alias)
- `reload.assets`

### Online hub
- `online.hub`

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

### Rollback netplay
- `ggpo.local [toggle|on|off|status]`
- `ggpo.net host [port]`
- `ggpo.net join <host> [port] [local_port]`
- `ggpo.net delay [frames]`
- `ggpo.net advantage [frames]`
- `ggpo.net predict [frames]`
- `ggpo.net highping [frames]`
- `ggpo.net smoothping [frames]`
- `ggpo.net correction [on|off]`
- `ggpo.net sim [loss_pct] [min_delay] [max_delay]`
- `ggpo.net status`
- `ggpo.net off`
- `ggpo.selftest [frames]`
- `ggpo.roundtrip`

## Examples
- `mods.find speed`
- `state.last`
- `state.return options`
- `mods.enable all`
- `mods.toggle speedhack`
- `mods.config.find speed`
- `mods.info speedhack`
- `mods.trace speedhack on`
- `mods.config speedhack`
- `binds.list speedhack`
- `binds.set speedhack dash space`
- `mods.config.get speedhack max_speed`
- `mods.config.set speedhack max_speed 3.5`
- `mods.config.set speedhack enabled true`
- `mods.config.action speedhack reset_defaults`
- `log.tail 40`
- `console.copy output`
- `lua return 2 + 2`
- `lua.mod speedhack return config.get("max_speed")`
- `lua.file mods\\speedhack\\speedhack.lua`
- `time.scale`
- `time.scale 0.5`
- `time.scale auto`
- `input.override 0 0x10 30 0`
- `input.override 1 0x4 -1 1`
- `input.clear 1`
- `online.hub`
- `ggpo.net delay 2`
- `ggpo.net advantage 20`
- `ggpo.net predict 24`
- `ggpo.net highping 140`
- `ggpo.net smoothping 140`
- `ggpo.net correction on`
- `ggpo.net sim 5 2 8`
- `ggpo.net sim off`
- `ggpo.net host 47777`
- `ggpo.net join 127.0.0.1 47777`

## Notes
- `mods.enable` recreates the mod runtime if dependencies/conflicts allow it.
- `mods.disable` unloads the mod runtime and also disables enabled dependents that require it.
- `mods.toggle` applies the same enable/disable lifecycle immediately.
- `mods.info` includes runtime perf/memory diagnostics for the selected mod.
- `mods.trace` toggles per-mod event trace logging into `mods\modframework.log`.
- `mods.config.set` supports bool/int/float/string/options config types. For an `options` key, the value must be one of the declared choices.
- `time.scale <value>` sets a manual global timescale clamp-limited to `0.05..100`.
- `time.scale auto` returns control back to mod-driven delta-time scaling.
- `lua` and `lua.mod` return values are printed in the console output.
- For string values with spaces, quote them:
  - `mods.config.set my_mod welcome_text "hello world"`
- `input.override` mask accepts decimal or hex (`0x...`).
- `frames < 0` keeps an override active until cleared.
