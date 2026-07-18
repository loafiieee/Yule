"""Structural guards for the online-flow integrations around the UDP core."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    search_from = 0
    while True:
        start = SOURCE.find(marker, search_from)
        assert start >= 0, f"missing definition for {name}"
        paren = start + len(name)
        depth = 0
        end_paren = -1
        for pos in range(paren, len(SOURCE)):
            if SOURCE[pos] == "(":
                depth += 1
            elif SOURCE[pos] == ")":
                depth -= 1
                if depth == 0:
                    end_paren = pos
                    break
        assert end_paren >= 0, f"unterminated signature for {name}"
        body_start = end_paren + 1
        while body_start < len(SOURCE) and SOURCE[body_start].isspace():
            body_start += 1
        if body_start < len(SOURCE) and SOURCE[body_start] == "{":
            break
        search_from = end_paren + 1

    depth = 0
    for pos in range(body_start, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[body_start : pos + 1]
    raise AssertionError(f"unterminated body for {name}")


def struct_body(name: str) -> str:
    start = SOURCE.index(f"typedef struct {name}")
    end = SOURCE.index(f"}} {name};", start)
    return SOURCE[start:end]


# The server-issued shared packet key has short-lived owners only. It must not
# leak into match history/result state, logs, or the separate rendezvous token.
pending = struct_body("OnlinePendingMatch")
retry = struct_body("OnlineConnectRetry")
active = struct_body("OnlineActiveMatch")
assert "p2p_auth_token" in pending
assert "p2p_auth_token" in retry
assert "p2p_auth_token" not in active

begin = function_body("online_server_begin_pending_match")
assert '"p2p_auth_token"' in begin
assert "online_control_json_get_string" in begin
assert "g_online_connect.p2p_auth_token" in begin
assert "online_match_protocol_compatible" in begin

protocol = function_body("online_server_protocol_compatible")
assert '"control_protocol"' in protocol
assert '"match_protocol"' in protocol
assert '"p2p_protocol"' in protocol
assert '"cap_p2p_auth"' in protocol
assert "GGPO_NET_PROTOCOL_VERSION" in protocol

attempt = function_body("online_connect_start_attempt")
arm = attempt.index("ggpo_net_set_match_token")
host = attempt.index("start_ggpo_net_host")
join = attempt.index("start_ggpo_net_join_deferred")
assert arm < host and arm < join
assert "online_abort_prematch_setup" in attempt

clear = function_body("online_p2p_auth_tokens_clear")
assert clear.count("SecureZeroMemory") >= 2
assert "ggpo_net_clear_match_token" in clear
assert "online_p2p_auth_tokens_clear();" in function_body(
    "online_active_match_begin_from_pending"
)

# A manifest is accepted as one bounded logical message or not sent at all;
# truncating JSON and sending the prefix is forbidden.
manifest = function_body("online_server_send_map_manifest")
assert "ONLINE_MAP_MANIFEST_MAX_BYTES" in SOURCE
assert "custom_maps_build_manifest_json(NULL, 0)" in manifest
assert "malloc" in manifest and "free" in manifest
assert "only part was sent" not in manifest
assert "static char maps_json" not in manifest

# A completed match enters the actual result state. Its full UI has real mouse
# hit-testing; the small toast is only a fallback outside that state.
open_result = function_body("online_open_result_screen")
assert "g_online_result_state" in open_result
assert "p_state_switch" in open_result

render_result = function_body("online_result_render")
assert "online_result_render_toast" not in render_result
assert "online_result_button_metrics" in render_result
assert "online_hub_draw_button_box" in render_result
assert "online_result_button_at" in function_body("hooks_online_hub_mousemotion")
mouse_button = function_body("hooks_online_hub_mousebutton")
assert "online_result_button_at" in mouse_button
assert "online_result_activate" in mouse_button

pre_swap = function_body("hooks_online_on_pre_swap")
assert "state_ptr != (void*)&g_online_result_state" in pre_swap

# The login row is a conventional compact checkbox, and main-menu mode writes
# share the updater's atomic/comment-preserving config path.
login = function_body("online_hub_render_login_gateway")
assert '"Remember me"' in login
assert '"Remember me securely"' not in login
assert "Saved in Windows Credential Manager" not in login
assert "online_try_remembered_login" in function_body("online_hub_enter")
assert "!online_remembered_login_in_progress()" in function_body(
    "online_login_gateway_active"
)

menu_save = function_body("menu_mode_save")
assert "update_ext_config_set" in menu_save
assert "fopen" not in menu_save

# Direct developer sessions also fail closed, but retain an explicit usable
# workflow that never puts the shared key in console input/history or logs.
console_net = function_body("console_run_ggpo_net")
assert "SDL_GetClipboardText" in console_net
assert 'SDL_SetClipboardText("")' in console_net
assert "ggpo_net_set_match_token(clipboard" in console_net
assert "credential_ext_secure_zero(clipboard" in console_net
assert "ggpo.net key" in SOURCE

hotkeys = function_body("hooks_console_keydown")
assert "start_ggpo_net_host(GGPO_NET_DEFAULT_PORT" in hotkeys
assert 'start_ggpo_net_join("127.0.0.1"' in hotkeys

print("online flow integration static checks: OK")
