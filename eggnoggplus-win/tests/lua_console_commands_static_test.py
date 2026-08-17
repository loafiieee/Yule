from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
HEADER = (ROOT / "lua_manager.h").read_text(encoding="utf-8")
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
DOCS = (ROOT / "MODDING.md").read_text(encoding="utf-8")
API_DOCS = (ROOT / "docs-site" / "api-data.js").read_text(encoding="utf-8")

# Registration is owner-bound, canonical, duplicate checked, and exposes a
# removable handle through the same stable-id mechanism as event callbacks.
assert 'snprintf(out, out_capacity, "mod.%s.%s", mod->id, local)' in LUA
assert "console_catalog_is_known(command.name)" in LUA
assert "mod_console_find_command(command.name, NULL)" in LUA
assert "MOD_SUBSCRIPTION_CONSOLE_COMMAND" in LUA
assert "mod_remove_console_command" in LUA
assert "mod_console_commands_clear(L, mod);" in LUA
assert 'lua_setfield(Ls, -2, "console")' in LUA

# Raw and bounded typed argument modes are both implemented. Gameplay commands
# classify their owner and public dispatch refuses a suspended owner.
for token in (
    "MOD_CONSOLE_ARGS_RAW",
    "MOD_CONSOLE_ARGS_TYPED",
    "MOD_CONSOLE_ARG_STRING",
    "MOD_CONSOLE_ARG_INTEGER",
    "MOD_CONSOLE_ARG_NUMBER",
    "MOD_CONSOLE_ARG_BOOLEAN",
    "MOD_CONSOLE_ARG_MAX 8",
    'mod_mark_gameplay_affecting(mod, "gameplay console command")',
    "mod_is_gameplay_suspended(owner)",
):
    assert token in LUA

# The native console discovers, documents, and dispatches dynamic commands.
for declaration in (
    "lua_manager_console_command_count(void)",
    "lua_manager_console_command_at(int index)",
    "lua_manager_console_command_help(const char* name",
    "lua_manager_console_execute_command(const char* name",
):
    assert declaration in HEADER
    assert declaration in LUA
assert "lua_manager_console_command_count()" in HOOKS
assert "lua_manager_console_command_help(t" in HOOKS
assert "lua_manager_console_execute_command(cmd, arg" in HOOKS

assert "## Console commands (`mod.console`)" in DOCS
assert "mod.console.register" in API_DOCS
assert "console.commands" in DOCS

print("owner-scoped Lua console command integration: OK")
