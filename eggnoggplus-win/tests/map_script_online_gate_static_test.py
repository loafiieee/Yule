"""Guard the fail-closed online gate for packages that declare map.lua."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
CUSTOM_MAPS = (ROOT / "custom_maps.c").read_text(encoding="utf-8")
CUSTOM_MAPS_H = (ROOT / "custom_maps.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    search_from = 0
    while True:
        start = source.find(signature, search_from)
        if start < 0:
            raise AssertionError(f"missing function definition: {signature}")
        brace = source.find("{", start)
        semicolon = source.find(";", start)
        if brace >= 0 and (semicolon < 0 or brace < semicolon):
            break
        search_from = start + len(signature)
    depth = 0
    for pos in range(brace, len(source)):
        char = source[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated body: {signature}")


def ordered(body: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        found = body.find(needle, cursor)
        if found < 0:
            raise AssertionError(f"missing/out-of-order online script gate: {needle}")
        cursor = found + len(needle)


# The readiness gate reads identity from the generation pinned by mapgen_init;
# it must not rescan mutable package files between native setup and validation.
assert "custom_maps_pinned_script_id(int selector" in CUSTOM_MAPS_H
pinned_id = function_body(
    CUSTOM_MAPS, "int custom_maps_pinned_script_id(int selector"
)
ordered(
    pinned_id,
    "*out_script_id = 0;",
    "selector < VANILLA_MAP_COUNT",
    "selector != g_engine_pinned_selector",
    "g_engine_pinned_generation == 0",
    "*out_script_id = g_engine_pinned_map->script_id;",
)
assert "reload" not in pinned_id

# A declared script is ready only when the exact pinned identity is active and
# non-faulted. Missing pins and unexpected stale VMs also fail closed.
validate = function_body(
    HOOKS, "static int online_validate_pinned_map_script(int selector"
)
ordered(
    validate,
    "custom_maps_pinned_script_id(selector, &expected_script_id)",
    "if (pinned < 0)",
    "if (expected_script_id == 0)",
    "if (!map_script_is_active() || map_script_is_faulted() ||",
    "active_script_id != expected_script_id",
    "return 0;",
)
assert "required map.lua failed to bind or start" in validate

# Reset clears any prior VM. The current native build/start must establish the
# pinned script postcondition before rollback layout publication.
prepare = function_body(
    HOOKS,
    "static int online_prepare_pending_match_state(char* err, size_t err_cap)",
)
ordered(
    prepare,
    "custom_maps_deactivate_script();",
    "p_game_reset();",
    "online_ensure_native_game_started();",
    "online_validate_pinned_map_script(g_online_pending_match.selector",
    "ggpo_net_finalize_state_layout(err, err_cap)",
    "g_online_pending_match.prematch_prepared = 1;",
)

# Both bind failures and activation failures leave the VM inactive, making the
# postcondition above terminal online while preserving the offline hook's
# existing native-visual fallback.
map_build = function_body(HOOKS, "static void __cdecl hooked_mapgen_build_map(void)")
bind_failure = map_build[
    map_build.index("if (!content_bridge_bind_selector") :
    map_build.index("hooks_map_native_tileset_configure(selector)")
]
assert "custom_maps_deactivate_script();" in bind_failure
activate = function_body(
    CUSTOM_MAPS, "int custom_maps_activate_script_for_selector(int selector"
)
activation_failure = activate[activate.index("if (!map_script_activate") :]
assert "map_script_deactivate();" in activation_failure
assert "return 0;" in activation_failure

# The precise preparation error is sent through the existing terminal abort,
# retained across the deferred hub handoff, and shown to the player.
launch = function_body(HOOKS, "static void online_match_pump_launch(void)")
ordered(
    launch,
    "online_prepare_pending_match_state(err, sizeof(err))",
    "online_abort_prematch_setup(",
    'err[0] ? err : "could not initialize native match state"',
)
abort = function_body(
    HOOKS, "static void online_abort_prematch_setup(const char* reason)"
)
assert "g_online_pending_connect_fail_reason" in abort
assert "online_server_send_match_abort" in abort
assert "online_hub_set_status(g_online_pending_connect_fail_reason)" in abort
assert "g_online_pending_connect_fail_status = 4;" in abort
hub_enter = function_body(HOOKS, "static void __cdecl online_hub_enter(void)")
assert "g_online_pending_connect_fail_reason[0]" in hub_enter
assert "online_hub_set_status(g_online_pending_connect_fail_reason[0]" in hub_enter

print("map_script_online_gate_static_test: all checks passed")
