"""Guard the hook ordering that keeps online setup behind the hub countdown."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    search_from = 0
    while True:
        start = SOURCE.find(signature, search_from)
        if start < 0:
            raise AssertionError(f"missing function definition: {signature}")
        brace = SOURCE.find("{", start)
        semicolon = SOURCE.find(";", start)
        if brace >= 0 and (semicolon < 0 or brace < semicolon):
            break
        search_from = start + len(signature)
    depth = 0
    for pos in range(brace, len(SOURCE)):
        char = SOURCE[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : pos]
    raise AssertionError(f"unterminated body: {signature}")


def ordered(body: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        found = body.find(needle, cursor)
        if found < 0:
            raise AssertionError(f"missing/out-of-order prematch operation: {needle}")
        cursor = found + len(needle)


begin = function_body("static void online_server_begin_pending_match(const char* line)")
ordered(
    begin,
    "g_online_pending_match.active = 1;",
    "g_online_pending_match.setup_started_ms = GetTickCount();",
    "if (g_online_pending_match.match_id <= 0)",
    "g_online_pending_match.launch_countdown_frames = ONLINE_MATCH_COUNTDOWN_FRAMES;",
    "online_connect_start_attempt(1);",
)

service = function_body("static void online_server_update(void)")
ordered(
    service,
    "if (g_online_pending_match.active)",
    "ggpo_net_service(prematch_err, sizeof(prematch_err))",
    "online_connect_retry_tick();",
)

prepare = function_body("static int online_prepare_pending_match_state(void)")
ordered(
    prepare,
    "*g_hook_map_selector = g_online_pending_match.selector;",
    "lua_manager_game_set_rng_seed(g_online_pending_match.seed)",
    "p_game_reset();",
    "online_ensure_native_game_started();",
    "g_online_pending_match.prematch_prepared = 1;",
)

launch = function_body("static void online_match_pump_launch(void)")
ordered(
    launch,
    "ONLINE_PREMATCH_SETUP_TIMEOUT_MS",
    'online_abort_prematch_setup("prematch synchronization timed out")',
    "if (!is_online_hub_state_active())",
    "g_online_pending_match.launch_countdown_frames--;",
    "if (!ggpo_net_active() || !ggpo_net_connected()) return;",
    "if (ggpo_net_build_mismatch())",
    'online_abort_prematch_setup("opponent is using a different game or framework build")',
    "online_prepare_pending_match_state()",
    "ggpo_net_set_prematch_hold(0, err, sizeof(err))",
    "!ggpo_net_prematch_ready()",
    "ggpo_net_prepare_prematch_start(err, sizeof(err))",
    "p_state_switch((void*)(uintptr_t)ADDR_GAME_STATE);",
    "online_active_match_begin_from_pending();",
    "g_online_pending_match.active = 0;",
)

game_update = function_body("static void __cdecl hooked_game_update(int arg0)")
ordered(
    game_update,
    "if (g_online_pending_match.active)",
    "lua_manager_on_tick();",
    "online_advance_net_gameplay_tick(arg0)",
)

if "online_launch_pending_match_to_game" in SOURCE:
    raise AssertionError("legacy early-GAME prematch launcher is still present")

print("prematch_hooks_static_test: all checks passed")
