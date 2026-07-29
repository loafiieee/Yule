import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "discord_rpc_ext.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "discord_rpc_ext.c").read_text(encoding="utf-8")
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(encoding="utf-8")

assert re.search(
    r"void\s+discord_rpc_ext_pump\s*\(\s*DiscordRpcActivity\s+activity\s*\)",
    HEADER,
), "presence API must accept only the closed activity enum"
assert "const char*" not in re.search(
    r"typedef enum DiscordRpcActivity.*?} DiscordRpcActivity;",
    HEADER,
    re.DOTALL,
).group(0), "activity enum must not carry arbitrary text"

for forbidden_json_key in (
    '\\"secrets\\"',
    '\\"party\\"',
    '\\"buttons\\"',
    '\\"username\\"',
    '\\"opponent\\"',
    '\\"match_id\\"',
    '\\"token\\"',
    '\\"endpoint\\"',
    '\\"password\\"',
):
    assert forbidden_json_key not in SOURCE, (
        f"presence implementation must not serialize {forbidden_json_key}"
    )

assert "FILE_FLAG_OVERLAPPED" in SOURCE
assert "GetOverlappedResult" in SOURCE and "FALSE" in SOURCE
assert "WaitNamedPipe" not in SOURCE and "Sleep(" not in SOURCE
assert "DISCORD_RPC_UPDATE_INTERVAL_MS 4100u" in SOURCE
assert 'L"\\\\\\\\?\\\\pipe\\\\discord-ipc-%d"' in SOURCE
assert "DISCORD_RPC_MAX_PAYLOAD (64u * 1024u)" in SOURCE
assert '#define EGGNOGGPLUS_DISCORD_APPLICATION_ID "1531027934004117664"' in SOURCE
assert "invalid overrides are ignored" in SOURCE

assert "discord_rpc_ext_pump(online_discord_activity());" in HOOKS
assert "g_online_active_match.opponent" not in re.search(
    r"static DiscordRpcActivity online_discord_activity\(void\).*?^}",
    HOOKS,
    re.DOTALL | re.MULTILINE,
).group(0), "runtime activity mapping must not read opponent identity"
assert "ONLINE_SETTING_DISCORD_PRESENCE" in HOOKS
assert "FW_SETTING_DISCORD_PRESENCE" in HOOKS
assert '"  Discord Rich Presence"' in HOOKS
assert 'update_ext_config_set("discord_presence"' in HOOKS
assert '"discord.app"' in HOOKS
assert 'update_ext_config_set("discord_application_id"' in HOOKS
assert "discord_rpc_ext_set_application_id(application_id)" in HOOKS
assert "discord_rpc_ext_application_id()" in HOOKS
assert "hooks_runtime_shutdown();" in (ROOT / "dllmain.c").read_text(encoding="utf-8")

assert "discord_rpc_ext.c" in BUILD
assert "discord_rpc_ext.c" in RUNNER
assert "discord_presence_static_test.py" in RUNNER

print("Discord Rich Presence privacy/runtime wiring checks: OK")
