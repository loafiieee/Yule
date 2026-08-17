from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
CALLBACKS = (ROOT / "mod_callbacks.c").read_text(encoding="utf-8")
HEADER = (ROOT / "mod_callbacks.h").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(
    encoding="utf-8"
)


for kind in (
    "MOD_SUBSCRIPTION_FRAME",
    "MOD_SUBSCRIPTION_TICK",
    "MOD_SUBSCRIPTION_TICK_POST",
    "MOD_SUBSCRIPTION_EVENT",
    "MOD_SUBSCRIPTION_LAYOUT",
    "MOD_SUBSCRIPTION_CONFIG_ACTION",
):
    assert kind in MANAGER

assert 'lua_setfield(Ls, -2, "remove")' in MANAGER
assert "lua_pushboolean(Ls, removed)" in MANAGER
assert "mod_remove_layout_subscription" in MANAGER
assert "mod_remove_config_subscription" in MANAGER
assert "return lua_register_subscription" in MANAGER
assert "h->subscription_id = mod_next_subscription_id(mod);" in MANAGER
assert "mod_callback_list_ref(&mod->on_frame, ids[i])" in MANAGER
assert "mod_callback_list_ref(&mod->on_event, ids[i])" in MANAGER
assert "mod_callback_list_clear(Ls, list);" in MANAGER

assert "memmove" in CALLBACKS
assert "luaL_unref" in CALLBACKS
assert "mod_callback_list_snapshot" in CALLBACKS
assert "mod_callback_list_remove" in HEADER
assert "mod_callbacks.c" in BUILD
assert "tests\\mod_callbacks_test.c" in RUNNER

print("removable Lua subscription integration static checks: OK")
