from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    search_from = 0
    while True:
        start = HOOKS.find(marker, search_from)
        assert start >= 0, f"missing definition for {name}"
        paren = start + len(name)
        depth = 0
        end_paren = -1
        for pos in range(paren, len(HOOKS)):
            if HOOKS[pos] == "(":
                depth += 1
            elif HOOKS[pos] == ")":
                depth -= 1
                if depth == 0:
                    end_paren = pos
                    break
        assert end_paren >= 0, f"unterminated signature for {name}"
        after = end_paren + 1
        while after < len(HOOKS) and HOOKS[after].isspace():
            after += 1
        if after < len(HOOKS) and HOOKS[after] == "{":
            brace = after
            break
        search_from = end_paren + 1

    depth = 0
    for pos in range(brace, len(HOOKS)):
        if HOOKS[pos] == "{":
            depth += 1
        elif HOOKS[pos] == "}":
            depth -= 1
            if depth == 0:
                return HOOKS[brace : pos + 1]
    raise AssertionError(f"unterminated body for {name}")


assert '#include "online_control.h"' in HOOKS
assert "online_control.c" in BUILD
assert "online_json_find_key" not in HOOKS
assert "ONLINE_SERVER_CONNECT_TIMEOUT_MS" in HOOKS
assert "ONLINE_SERVER_AUTH_TIMEOUT_MS" in HOOKS

get_string = function_body("online_json_get_string")
get_int = function_body("online_json_get_int")
assert "online_control_json_get_string" in get_string
assert "ONLINE_CONTROL_JSON_OK" in get_string
assert "online_control_json_get_int" in get_int

connect = function_body("online_server_connect")
assert "g_online_auth_pending = 1" in connect
assert "online_control_deadline_after" in connect
assert "ONLINE_SERVER_CONNECT_TIMEOUT_MS" in connect
assert connect.index("g_online_auth_pending = 1") < connect.index("net_connect(")

disconnect = function_body("online_server_disconnect")
assert "discard_auth_secret" in disconnect
assert "g_online_server_deadline_ms = 0" in disconnect
assert "discard_auth_secret || !is_online_hub_state_active()" in disconnect
assert "online_clear_password_memory" in disconnect

update = function_body("online_server_update")
assert "ONLINE_SERVER_AUTH_TIMEOUT_MS" in update
assert update.count("online_control_deadline_reached") >= 2
assert 'online_server_disconnect("Server connection timed out.")' in update
assert '"Authentication timed out."' in update
assert "g_online_server_info_pending" in update
assert "capability handshake timed out" in update
assert 'online_server_disconnect("Server message too large; disconnected.")' in update
assert 'online_server_send_raw("{\\\"type\\\":\\\"server_info\\\"}\\n")' in update
assert "g_online_server_info_pending = 1" in update
assert "memchr(chunk, '\\0', (size_t)got)" in update
assert "memchr(g_online_recv_buf, '\\0', line_len)" in update
assert "g_online_recv_len >= ONLINE_SERVER_LINE_CAP" in update
assert "line_len = sizeof(line) - 1" not in update

handle = function_body("online_server_handle_line")
validate = handle.index("online_control_json_validate")
extract_type = handle.index('online_control_json_get_string(line, "type"')
auth_ok = handle.index('"auth_ok"')
canonical = handle.index("online_control_username_is_canonical", auth_ok)
authed = handle.index("g_online_authed = 1", auth_ok)
assert validate < extract_type < auth_ok < canonical < authed
assert 'online_server_disconnect("Server sent an invalid message.")' in handle
assert '"server_info"' in handle
assert "online_server_protocol_compatible" in handle
assert "online_server_send_auth(g_online_register_after_connect)" in handle
assert "Online server update required" in handle
assert "server rejected the required capability handshake" in handle

begin_match = function_body("online_server_begin_pending_match")
assert "OnlineControlJsonResult map_key_result" in begin_match
assert "map_key_result < ONLINE_CONTROL_JSON_NOT_FOUND" in begin_match
assert "online_match_protocol_compatible" in begin_match
lookup = begin_match.index("custom_maps_selector_for_key")
unknown_abort = begin_match.index(
    'online_abort_prematch_setup("selected map is not installed or changed since matchmaking")'
)
assert lookup < unknown_abort

load_credential = function_body("online_load_remembered_password")
malformed = load_credential.index("CREDENTIAL_EXT_MALFORMED_CREDENTIAL")
too_small = load_credential.index("CREDENTIAL_EXT_BUFFER_TOO_SMALL")
delete = load_credential.index("online_delete_remembered_password", too_small)
clear = load_credential.index("online_clear_password_memory", delete)
assert malformed < delete
assert too_small < delete < clear
assert "Remembered login is unavailable" in load_credential

print("online_control_hooks_static_test: all checks passed")
