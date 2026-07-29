"""Structural guards for the online-flow integrations around the UDP core."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")

# Route selection must stay deterministic. The rendezvous server already picks
# loopback/LAN/public; registering all advertised alternatives simultaneously
# allowed the two peers to pin different authenticated source paths.
assert 'online_json_get_string(line, "peer_route", peer_route' in SOURCE
assert 'server selected route=%s' in SOURCE
assert "Each attempt therefore uses exactly one symmetric route generation" in SOURCE
assert "ggpo_net_add_peer_candidate(public_host" not in SOURCE
assert "ggpo_net_add_peer_candidate(lan_host" not in SOURCE
assert "ggpo_net_link_ready()" in SOURCE
assert "#define ONLINE_MATCH_PROTOCOL_VERSION        3" in SOURCE
assert "#define ONLINE_SERVER_HEARTBEAT_MS       30000u" in SOURCE


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
# leak into match history/result records, logs, or the separate rendezvous token.
pending = struct_body("OnlinePendingMatch")
retry = struct_body("OnlineConnectRetry")
active = struct_body("OnlineActiveMatch")
assert "p2p_auth_token" in pending
assert "p2p_auth_token" in retry
assert "p2p_auth_token" not in active
for field in (
    "prematch_start_prepared",
    "server_start_reported",
    "server_committed",
):
    assert field in pending
assert "server_committed" in active
assert "winner_player" in active
assert "awaiting_native_return" in active

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
assert '"cap_private_rematch"' in protocol
assert '"cap_p2p_relay"' in protocol
assert '"cap_client_build_gate"' in protocol
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
capture_pending = function_body("online_active_match_capture_from_pending")
assert "online_p2p_auth_tokens_clear();" in capture_pending
assert "server_committed" in capture_pending

# Local READY is emitted only after frame zero is restorable, remains pending
# behind the server commit, and is latched only after an atomic control send.
send_started = function_body("online_server_send_match_started")
assert "match_started" in send_started
assert "if (online_server_send_raw(line))" in send_started
assert send_started.index("if (online_server_send_raw(line))") < send_started.index(
    "server_start_reported = 1"
)

launch = function_body("online_match_pump_launch")
launch_order = [
    "ggpo_net_prematch_ready()",
    "if (!p_state_switch)",
    "ggpo_net_prepare_prematch_start",
    "prematch_start_prepared = 1",
    "online_server_send_match_started();",
    "if (!g_online_pending_match.server_committed) return;",
    "p_state_switch((void*)(uintptr_t)ADDR_GAME_STATE);",
    "online_active_match_begin_from_pending();",
]
cursor = 0
for marker in launch_order:
    cursor = launch.index(marker, cursor) + len(marker)

# A commit and immediate forfeit/result can arrive in one TCP receive batch.
# Resolve the exact committed pending match without entering a server-deleted
# GAME or discarding the only result notification.
server_lines = function_body("online_server_handle_line")
result_start = server_lines.index('} else if (_stricmp(type, "match_result") == 0)')
result_end = server_lines.index('} else if (_stricmp(type, "match_abort") == 0', result_start)
result_branch = server_lines[result_start:result_end]
result_order = [
    "g_online_pending_match.server_committed",
    "online_active_match_capture_from_pending();",
    "g_online_pending_match.active = 0;",
    'stop_ggpo_net("online server result")',
    "online_result_prepare",
    "online_return_to_hub_after_match",
]
cursor = 0
for marker in result_order:
    cursor = result_branch.index(marker, cursor) + len(marker)
assert "ADDR_GAME_STATE" not in result_branch
assert "!g_online_result.active &&" in result_branch
assert "g_online_result.toast_visible = 1" in result_branch
assert "g_online_result.server_confirmed = 1" in result_branch

send_end = function_body("online_server_send_match_end")
assert "g_online_active_match.match_id" in send_end
assert "!g_online_active_match.server_committed" in send_end
assert "winner_player" in send_end

console_cancel = function_body("online_cancel_match_from_console")
assert "online_forfeit_active_match" in console_cancel
assert "online_result_prepare" not in console_cancel
assert "online_return_to_hub_after_match" not in console_cancel

forfeit_match = function_body("online_forfeit_active_match")
forfeit_order = [
    "g_online_active_match.result_reported = 1",
    "online_server_send_match_abort",
    "online_result_prepare(ONLINE_MATCH_RESULT_LOSS",
    "online_return_to_hub_after_match",
]
cursor = 0
for marker in forfeit_order:
    cursor = forfeit_match.index(marker, cursor) + len(marker)
assert "online_server_send_match_end" not in forfeit_match

monitor_match = function_body("online_monitor_active_match_state")
assert "online_forfeit_active_match" in monitor_match

hub_enter = function_body("online_hub_enter")
assert "online_forfeit_active_match" in hub_enter
assert "online_server_send_match_end(ONLINE_MATCH_RESULT_LOSS)" not in hub_enter

# A leader is assigned on every ordinary death. It can select an online winner
# only while the native terminal countdown is active; final score is the
# independent fallback and must also require a positive target plus a lead.
winner_detection = function_body("online_native_winner_player")
assert "*g_game_end_countdown > 0" in winner_detection
assert "loser == (uintptr_t)p_player_slots[0]" in winner_detection
assert "leader == (uintptr_t)p_player_slots[0]" in winner_detection
assert "*g_game_score_target > 0" in winner_detection
assert "s0 >= target && s0 > s1" in winner_detection
assert "s1 >= target && s1 > s0" in winner_detection

server_disconnect = function_body("online_handle_server_match_disconnect")
assert "online_clear_match_state();" in server_disconnect
assert "online_hub_open();" in server_disconnect
assert "online_result_prepare" not in server_disconnect
assert "online_return_to_hub_after_match" not in server_disconnect

# The server's receive-idle timeout must not forfeit a quiet hub or long match.
# Heartbeats share the bounded atomic send queue and reset with connection state.
heartbeat = function_body("online_server_heartbeat_tick")
assert '"ping"' in heartbeat or "ping" in heartbeat
assert "online_control_deadline_reached" in heartbeat
assert "if (online_server_send_raw(line))" in heartbeat
assert heartbeat.index("if (online_server_send_raw(line))") < heartbeat.index(
    "g_online_server_heartbeat_seq = next_seq"
)
disconnect = function_body("online_server_disconnect")
assert "online_server_heartbeat_reset();" in disconnect
handle_line = function_body("online_server_handle_line")
assert '_stricmp(type, "pong") == 0' in handle_line
assert "pong_seq == g_online_server_heartbeat_seq" in handle_line
server_update = function_body("online_server_update")
assert "online_server_heartbeat_tick(now);" in server_update

# A manifest is accepted as one bounded logical message or not sent at all;
# truncating JSON and sending the prefix is forbidden.
manifest = function_body("online_server_send_map_manifest")
assert "ONLINE_MAP_MANIFEST_MAX_BYTES" in SOURCE
assert "custom_maps_build_manifest_json(NULL, 0)" in manifest
assert "malloc" in manifest and "free" in manifest
assert "framework_version" in manifest
assert "FRAMEWORK_VERSION" in manifest
assert "control_protocol" in manifest
assert "ONLINE_CONTROL_PROTOCOL_VERSION" in manifest
assert "match_protocol" in manifest
assert "ONLINE_MATCH_PROTOCOL_VERSION" in manifest
assert "p2p_protocol" in manifest
assert "GGPO_NET_PROTOCOL_VERSION" in manifest
assert "build_id" in manifest
assert "ggpo_net_local_build_id()" in manifest
assert "game_exe_id" in manifest
assert "ggpo_net_local_exe_id()" in manifest
assert "framework_dll_id" in manifest
assert "ggpo_net_local_dll_id()" in manifest
assert "only part was sent" not in manifest
assert "static char maps_json" not in manifest

# Online results are compact bottom-right notifications only. Completion lands
# in the normal hub with main as its stable Back owner; no fullscreen result
# GameState, buttons, input branch, or stale transition remains.
assert "g_online_result_state" not in SOURCE
assert "online_open_result_screen" not in SOURCE
assert "online_result_button_metrics" not in SOURCE
assert "online_result_activate" not in SOURCE

return_to_hub = function_body("online_return_to_hub_after_match")
assert "online_hub_open();" in return_to_hub
assert "online_native_finish_is_presenting()" in return_to_hub
assert "g_online_open_pending = 0;" in return_to_hub
assert "g_online_return_state = main_state;" in return_to_hub
assert "g_online_pending_return_state = main_state;" in return_to_hub
assert "g_online_force_main_return_once = already_in_hub ? 0 : 1;" in return_to_hub
finish_match = function_body("online_finish_active_match")
assert finish_match.index("online_result_prepare") < finish_match.index(
    "online_return_to_hub_after_match"
)
native_wait = finish_match.index("if (g_online_active_match.awaiting_native_return)")
assert native_wait < finish_match.index('stop_ggpo_net("online match complete")')
assert finish_match.index("return;", native_wait) < finish_match.index(
    'stop_ggpo_net("online match complete")'
)

poll_completion = function_body("online_match_poll_completion")
assert "g_online_active_match.winner_player = winner;" in poll_completion
assert "g_online_active_match.awaiting_native_return = 1;" in poll_completion
assert poll_completion.index("winner_player = winner") < poll_completion.index(
    "online_finish_active_match"
)

state_switch = function_body("hooked_state_switch")
assert "finish_online_after_switch" in state_switch
assert "online_native_finish_is_presenting()" in state_switch
assert "online_match_poll_completion();" in state_switch
assert state_switch.index("switched = real_switch") < state_switch.index(
    'stop_ggpo_net("native win sequence complete")'
)
assert state_switch.index('stop_ggpo_net("native win sequence complete")') < (
    state_switch.index("online_return_to_hub_after_match(status)")
)

result_record = struct_body("OnlineResultToast")
assert "int active;" in result_record
assert "int toast_visible;" in result_record
assert "int match_id;" in result_record
prepare_result = function_body("online_result_prepare")
assert "g_online_result.active = 1" in prepare_result
assert "g_online_result.toast_visible = 1" in prepare_result

render_toast = function_body("online_result_render_toast")
assert "g_online_result.toast_visible" in render_toast
assert '"RESULT REPORTED"' in render_toast
assert "g_online_result.server_confirmed" in render_toast
pre_swap = function_body("hooks_online_on_pre_swap")
assert "g_online_result.active && g_online_result.toast_visible" in pre_swap
assert "online_result_render_toast();" in pre_swap
assert "g_online_open_pending && !online_native_finish_is_presenting()" in pre_swap

native_presenting = function_body("online_native_finish_is_presenting")
assert "*g_game_end_countdown > 0" in native_presenting
assert "online_native_winner_player() >= 0" in native_presenting
assert "online_state_is_ingame_menu(state)" in native_presenting

server_disconnect = function_body("online_handle_server_match_disconnect")
preserve_pos = server_disconnect.index("online_native_finish_is_presenting()")
stop_pos = server_disconnect.index("stop_ggpo_net", preserve_pos)
assert preserve_pos < stop_pos

# Dismissing or expiring a provisional toast retains its exact match identity,
# so the asynchronous confirmation is still admitted. Confirmed records may be
# retired with the toast. Requeue remains blocked until confirmation.
tick_toast = function_body("online_result_tick_toast")
assert "online_result_dismiss(1)" in tick_toast
dismiss_result = function_body("online_result_dismiss")
hide_pos = dismiss_result.index("g_online_result.toast_visible = 0")
confirmed_pos = dismiss_result.index("if (g_online_result.server_confirmed)", hide_pos)
clear_pos = dismiss_result.index("memset(&g_online_result", confirmed_pos)
assert hide_pos < confirmed_pos < clear_pos
mouse_button = function_body("hooks_online_hub_mousebutton")
assert "online_result_dismiss(1)" in mouse_button
send_queue = function_body("online_server_send_queue")
assert "g_online_result.active && !g_online_result.server_confirmed" in send_queue
assert '"Waiting for the server result before requeueing."' in send_queue

abort_branch = server_lines[result_end:]
assert "memset(&g_online_result, 0, sizeof(g_online_result));" in abort_branch

# Hub/console return targets remain sanitized even though results no longer own
# a state, and completed-match routing explicitly prevents Back from reopening
# the dead GAME state.
sanitize_return = function_body("online_hub_sanitize_return_state")
assert "g_online_hub_state" in sanitize_return
assert "ADDR_MAIN_STATE" in sanitize_return

hub_enter = function_body("online_hub_enter")
assert "if (g_online_force_main_return_once)" in hub_enter
assert "last = (void*)(uintptr_t)ADDR_MAIN_STATE;" in hub_enter
assert "online_hub_sanitize_return_state(last)" in hub_enter
assert "online_hub_sanitize_return_state(g_online_pending_return_state)" in pre_swap

close_hub = function_body("online_hub_close_to_return_state")
assert "online_hub_sanitize_return_state(g_online_return_state)" in close_hub
assert "online_hub_close_to_return_state();" in function_body("online_activate_selected")
assert "online_hub_close_to_return_state();" in function_body("hooks_online_hub_keydown")
assert "online_hub_close_to_return_state();" in function_body("hooks_online_hub_control_action")

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
