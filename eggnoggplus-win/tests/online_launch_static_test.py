from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
DLLMAIN = (ROOT / "dllmain.c").read_text(encoding="utf-8")
PARSER = (ROOT / "launch_request.c").read_text(encoding="utf-8")
IPC = (ROOT / "launch_ipc.c").read_text(encoding="utf-8")
CUSTOM_MAPS = (ROOT / "custom_maps.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
INSTALLER = (ROOT / "dist" / "installer" / "windows" / "install.ps1").read_text(encoding="utf-8")


def body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated body: {marker}")


parse_process = body(HOOKS, "static void online_launch_parse_process_args")
launch_pump = body(HOOKS, "static int online_launch_pump")
main_update = body(HOOKS, "static int __cdecl hooked_main_update_with_buttons")
hub_update = body(HOOKS, "static void __cdecl online_hub_update(void) {")
process_attach = body(DLLMAIN, "BOOL WINAPI DllMain")
hub_close = body(HOOKS, "static void online_hub_close_to_return_state(void) {")
disconnect = body(HOOKS, "static void online_server_disconnect(const char* reason) {")
handle_line = body(HOOKS, "static void online_server_handle_line(const char* line) {")

# Command-line tokenization and Shell allocation cannot run from loader lock.
assert "CommandLineToArgvW" not in DLLMAIN
assert "normalize_process_working_directory();" in process_attach
assert process_attach.index("normalize_process_working_directory();") < (
    process_attach.index("install_crash_handler();")
)
assert "if (online_launch_pump())" in main_update
assert main_update.index("if (online_launch_pump())") < main_update.index(
    "int result = real_update"
)
assert main_update.index("if (online_launch_pump())") < main_update.index(
    "return 0;"
)
# Opening the custom hub leaves the native menu update path. The hub must keep
# ownership of the pending intent and resume it immediately after auth traffic
# is processed, including when the user had to enter credentials manually.
assert "online_server_update();" in hub_update
assert "if (online_launch_pump()) return;" in hub_update
assert hub_update.index("online_server_update();") < hub_update.index(
    "if (online_launch_pump())"
)
assert "CommandLineToArgvW(GetCommandLineW(), &argc)" in parse_process
assert "argc > 128" in parse_process
assert "launch_request_parse_args" in parse_process
assert "LocalFree(wide_args)" in parse_process

# Online intents route through existing authenticated UI actions. The separate
# V1 preview intent is decoded, loader-validated, and started locally before
# any hub navigation. No intent accepts endpoints, credentials, or paths.
assert "online_server_send_queue" in launch_pump
assert "online_try_send_friend_challenge" in launch_pump
assert "online_hub_open();" in launch_pump
assert "LAUNCH_REQUEST_PREVIEW_V1" in launch_pump
assert "launch_request_decode_preview" in launch_pump
assert "custom_maps_install_preview_text" in launch_pump
assert "hooks_start_native_match(selector)" in launch_pump
assert "return 1;" in launch_pump
assert launch_pump.index("LAUNCH_REQUEST_PREVIEW_V1") < launch_pump.index(
    "online_hub_open();"
)
assert '"online.launch: completed requests intent"' in launch_pump
assert '"online.launch: completed %s intent"' in launch_pump
assert "start_ggpo_net_join" not in launch_pump
assert "p2p_token" not in launch_pump
assert "password" not in launch_pump
assert "uri_has_forbidden_syntax" in PARSER
for forbidden in ("'?'","'#'","'%'","'\\\\'","'@'"):
    assert forbidden in PARSER
assert '"yule://join/' not in PARSER
assert '"yule://queue/' not in PARSER  # parsed structurally, never via loose substring
assert '"preview/v1/"' in PARSER
assert '"preview/v1z/"' in PARSER
assert "launch_request_preview_target_valid" in IPC
assert "launch_request_preview_packed_target_valid" in IPC
assert "packed preview data is corrupt or truncated" in PARSER
assert "preview files cannot contain NUL bytes" in PARSER
assert '"_greggnogg_preview"' in CUSTOM_MAPS
assert "if (map->is_preview) continue;" in CUSTOM_MAPS
assert "parse_v2_tileset" not in body(
    CUSTOM_MAPS, "int custom_maps_install_preview_text"
)

# Pending intents are bounded and user-cancellable; challenge execution waits
# for the server's complete friend snapshot.
assert "ONLINE_LAUNCH_TIMEOUT_MS = 120000" in HOOKS
assert "online_control_deadline_reached" in launch_pump
assert "online_launch_clear();" in hub_close
assert "g_online_friend_snapshot_complete = 0;" in disconnect
assert '"friend_snapshot_begin"' in handle_line
assert '"friend_snapshot_end"' in handle_line
assert "g_online_friend_snapshot_complete = 1;" in handle_line
assert "if (!g_online_friend_snapshot_complete)" in launch_pump

# Protocol activations are forwarded into one existing process before SDL
# creates a second game window. Ordinary direct launches stay multi-instance.
assert "launch_ipc.c" in BUILD
assert "launch_ipc_initialize()" in DLLMAIN
assert DLLMAIN.index("launch_ipc_initialize()") < DLLMAIN.index(
    "real_Init ? real_Init(flags)"
)
assert "ExitProcess(0u)" in DLLMAIN
assert "CreateMutexW" in IPC
assert "HWND_MESSAGE" in IPC
assert "WM_COPYDATA" in IPC
assert "SendMessageTimeoutW" in IPC
assert "launch_ipc_request_valid" in IPC
assert "AllowSetForegroundWindow" in IPC
assert "launch_ipc_poll(&request)" in HOOKS
assert "g_online_pending_match.active || g_online_active_match.active" in launch_pump

# The installer must own and conservatively remove the per-user URI handler.
assert "HKCU:\\Software\\Classes\\yule" in INSTALLER
assert "URL Protocol" in INSTALLER
assert "deep_link_protocol" in INSTALLER
assert "$UpdaterName = 'YuleUpdater.exe'" in INSTALLER
assert "$exe = Join-Path $gameDir $ExeName" in INSTALLER
assert "$lnk.TargetPath = Join-Path $gameDir $ExeName" in INSTALLER
assert "$exeQuoted = '\"' + (Join-Path $gameDir $ExeName) + '\"'" in INSTALLER
assert "if ($installedManifest.launcher_executable)" in INSTALLER

print("online launch/deep-link static checks: OK")
