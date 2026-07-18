from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
DOC = (ROOT / "ONLINE_MULTIPLAYER.md").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    search_from = 0
    brace = -1
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


assert '#include "credential_ext.h"' in HOOKS
assert "ONLINE_SETTING_REMEMBER_ME" in HOOKS
assert "remember_me=%d" in function_body("online_hub_save")
assert 'fprintf(f, "password=' not in function_body("online_hub_save")
assert '"remember_me"' in function_body("online_hub_load")

send_auth = function_body("online_server_send_auth")
assert "credential_ext_secure_zero(pass" in send_auth
assert "credential_ext_secure_zero(line" in send_auth

handle_line = function_body("online_server_handle_line")
auth_ok = handle_line.index('"auth_ok"')
store = handle_line.index("online_store_remembered_password", auth_ok)
clear = handle_line.index("online_clear_password_memory", store)
assert auth_ok < store < clear
assert "g_online_password_from_credential" in handle_line
assert "online_delete_remembered_password" in handle_line

load_password = function_body("online_load_remembered_password")
assert "CREDENTIAL_EXT_MALFORMED_CREDENTIAL" in load_password
assert "CREDENTIAL_EXT_BUFFER_TOO_SMALL" in load_password
invalid_delete = load_password.index("online_delete_remembered_password")
invalid_clear = load_password.index("online_clear_password_memory", invalid_delete)
assert invalid_delete < invalid_clear
assert "Remembered login is unavailable" in load_password

auto_login = function_body("online_try_remembered_login")
assert "g_online_password_from_credential" in auto_login
assert "online_load_remembered_password(0)" in auto_login
assert auto_login.index("online_load_remembered_password(0)") < auto_login.index(
    "online_server_connect(0)"
)
assert "g_online_auth_pending" in auto_login

hub_enter = function_body("online_hub_enter")
assert hub_enter.index("online_try_remembered_login") < hub_enter.index(
    "online_hub_rebuild_rows"
)
gateway = function_body("online_login_gateway_active")
assert "!online_remembered_login_in_progress()" in gateway
rows = function_body("online_hub_rebuild_rows")
assert "online_remembered_login_in_progress()" in rows
assert '"Opening online hub"' in rows

assert "credential_ext_secure_zero(g_online_capture_buf" in function_body(
    "online_clear_capture_state"
)
assert "ONLINE_SETTING_REMEMBER_ME" in function_body("online_hub_rebuild_rows")
checkbox_height = function_body("online_login_checkbox_height")
assert "clampf(30.0f * s, 22.0f, 38.0f)" in checkbox_height
login_hitbox = function_body("online_login_row_at_point")
assert "online_login_gateway_metrics" in login_hitbox
assert "checkbox_h = online_login_checkbox_height(&L);" in login_hitbox
assert "form_y + row_h * 2.0f + gap * 2.0f" in login_hitbox
assert "form_y + row_h * 2.0f + gap * 2.0f + checkbox_h" in login_hitbox
assert "ONLINE_SETTING_REMEMBER_ME" in login_hitbox
login_render = function_body("online_hub_render_login_gateway")
assert "online_login_gateway_metrics" in login_render
assert "online_login_checkbox_height" in login_render
assert "checkbox_box_y = checkbox_y + (checkbox_h - checkbox_size) * 0.5f" in login_render
assert "hooks_ui_draw_line" in login_render
assert '"Remember me"' in login_render
assert '"Remember me securely"' not in login_render
assert '"Saved in Windows Credential Manager"' not in login_render

for obsolete_ui_copy in (
    "Saved password loaded from Windows Credential Manager.",
    "Invalid saved password removed; enter it manually.",
    "Logged in. Password saved in Windows Credential Manager.",
    "Remember me disabled; the saved password was removed.",
    "Remember me enabled; the password will be saved after a successful login.",
):
    assert obsolete_ui_copy not in HOOKS

assert "online_remembered_login_in_progress()" in function_body(
    "hooks_online_hub_keydown"
)
assert "online_remembered_login_in_progress()" in function_body(
    "hooks_online_hub_mousebutton"
)
assert "online_remembered_login_in_progress()" in function_body(
    "hooks_online_hub_control_action"
)

assert "credential_ext.c" in BUILD
assert "-ladvapi32" in BUILD
assert "never contains the password" in DOC
assert "Windows generic" in DOC

print("credential_hooks_static_test: all checks passed")
