from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
HELPER = (ROOT / "updater_helper.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
INSTALLER = (ROOT / "installer" / "install.ps1").read_text(encoding="utf-8")

# The game remains the normal entry point. The updater is launched only after
# a verified transaction reaches restart-pending and only from a safe menu.
assert 'L"\\\\YuleUpdater.exe"' in HOOKS
assert 'L"--wait-pid=%lu"' in HOOKS
assert "CreateProcessW(updater_path" in HOOKS
assert "status == UPDATE_RESTART_PENDING" in HOOKS
assert "update_handoff_safe_state()" in HOOKS
assert "ADDR_GAME_STATE" not in HOOKS[
    HOOKS.index("static int update_handoff_safe_state"):
    HOOKS.index("static int update_handoff_launch")
]
assert "update_handoff_ephemeral_arg" in HOOKS

# The one-shot helper opens the still-live parent first, requests an orderly
# WM_CLOSE, waits for process exit, then services the transaction and relaunches.
close_at = HELPER.index("updater_close_and_wait(wait_pid)")
apply_at = HELPER.index("update_ext_helper_service(")
launch_at = HELPER.index("CreateProcessW(game")
assert close_at < apply_at < launch_at
assert "OpenProcess(SYNCHRONIZE" in HELPER
assert "EnumWindows(updater_close_window" in HELPER
assert "PostMessageW(window, WM_CLOSE" in HELPER
assert "WaitForSingleObject(process, UPDATER_WAIT_TIMEOUT_MS)" in HELPER
assert "TerminateProcess" not in HELPER
assert "updater_same_game_process_running(game)" in HELPER
assert "CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS" in HELPER
assert "QueryFullProcessImageNameW" in HELPER
assert HELPER.index("updater_same_game_process_running(game)") < apply_at

assert "-o build/YuleUpdater.exe updater_helper.c update_ext.c" in BUILD
assert "$UpdaterName = 'YuleUpdater.exe'" in INSTALLER
assert "$lnk.TargetPath = Join-Path $gameDir $ExeName" in INSTALLER
assert "$exe = Join-Path $gameDir $ExeName" in INSTALLER

print("one-shot updater handoff static checks: OK")
