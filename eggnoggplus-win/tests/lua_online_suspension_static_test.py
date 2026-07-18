#!/usr/bin/env python3
"""Focused source audit for the Lua online-suspension security boundary.

This is intentionally a static test: lua_manager.c is coupled to fixed native
Eggnogg+ addresses and cannot be meaningfully unit-linked outside the game.
The assertions keep every exposed deterministic mutator owner-aware and ensure
the less-obvious callback/lifecycle escape hatches retain their guards.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "lua_manager.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    start = SOURCE.find(marker)
    assert start >= 0, f"missing function {name}"
    brace = SOURCE.find("{", start)
    assert brace >= 0, f"missing body for {name}"
    depth = 0
    for pos in range(brace, len(SOURCE)):
        char = SOURCE[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : pos]
    raise AssertionError(f"unterminated body for {name}")


GAME_MUTATORS = (
    "lua_game_set_native_tick",
    "lua_game_set_rng_seed",
    "lua_game_set_player_colour_index",
    "lua_game_set_player_render_colours",
    "lua_game_set_player_body_hidden",
    "lua_game_set_player_sword_idle_offset",
    "lua_game_apply_full_state_blob",
    "lua_game_block_next_tick",
    "lua_game_simulate_ticks",
    "lua_game_register_bot_provider",
    "lua_game_register_menu_mode",
    "lua_game_arm_ai_match",
    "lua_game_start_match",
    "lua_game_set_input",
    "lua_game_input_override",
    "lua_game_input_clear",
    "lua_game_block_raw_input",
    "lua_game_apply_snapshot",
    "lua_game_apply_sword_snapshot",
    "lua_game_set_map_selector",
)

for function in GAME_MUTATORS:
    body = function_body(function)
    assert "lua_game_mutation_guard" in body[:500], (
        f"{function} must guard before parsing arguments or mutating state"
    )
    assert f"lua_pushcclosure(Ls, {function}, 1)" in SOURCE or (
        function == "lua_game_set_map_selector"
        and "lua_pushcclosure(Ls, lua_game_set_map_selector, 1)" in SOURCE
    ), f"{function} must be exported as an owner-aware closure"

for function in (
    "lua_content_begin",
    "lua_content_tx_register_tile",
    "lua_content_tx_commit",
):
    assert "lua_content_guard" in function_body(function), (
        f"{function} mutates deterministic custom-content registration"
    )
assert "mod_mark_gameplay_affecting" in function_body("lua_content_guard")


for function in (
    "lua_manager_on_delta_time",
    "lua_manager_on_tick",
    "lua_manager_on_tick_post",
    "lua_manager_on_frame",
    "lua_manager_on_event",
    "lua_manager_on_key_event",
    "lua_manager_config_trigger_action",
    "lua_manager_menu_mode_activate",
):
    assert "mod_is_runtime_active" in function_body(function), (
        f"{function} must not dispatch a suspended owner's Lua callback"
    )


for function in (
    "lua_cfg_set",
    "lua_cfg_on_action",
    "lua_input_bind",
    "lua_input_set",
    "lua_input_clear",
    "lua_mod_dofile",
    "lua_storage_set",
    "lua_storage_delete",
    "lua_storage_set_schema",
    "lua_storage_migrate",
    "lua_storage_save",
    "lua_interop_provide",
    "lua_interop_require",
):
    assert "lua_mod_change_guard" in function_body(function), (
        f"{function} needs an owner lifecycle/suspension guard"
    )


for function in (
    "lua_ui_button_invoke_ptr",
    "lua_ui_button_activate_ptr",
    "lua_ui_enter_state",
    "lua_ui_leave_state",
    "lua_ui_goto_main_menu",
):
    assert "lua_online_native_action_error" in function_body(function), (
        f"{function} can indirectly mutate native match state online"
    )


assert "call_on_unload_cb && mod->enabled && !mod_is_gameplay_suspended(mod)" in SOURCE
assert "if (g_online_suspend_active) return 0;" in function_body("lua_manager_set_mod_enabled")
assert "if (g_online_suspend_active)" in function_body("reload_mod_runtime")
assert "target gameplay mod is suspended during online play" in function_body(
    "lua_manager_console_eval_mod"
)
assert "interop_push_guarded_value" in function_body("interop_push_guarded_service_table")
assert "lua_interop_guarded_call" in function_body("interop_push_guarded_value")

scan_body = function_body("scan_and_load_mods")
reserve_pos = scan_body.find("mods_reserve_stable_slots(active_count)")
first_add_pos = scan_body.find("mods_add()")
assert 0 <= reserve_pos < first_add_pos, "owner slots must be stable before Lua closures are built"

print("lua_online_suspension_static_test: all checks passed")
