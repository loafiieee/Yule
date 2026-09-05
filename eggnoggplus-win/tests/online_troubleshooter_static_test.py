"""Structural guards for the active online connectivity troubleshooter."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
NET = (ROOT / "net_ext.c").read_text(encoding="utf-8")
NET_H = (ROOT / "net_ext.h").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
TEST_RUNNER = (ROOT / "tests" / "run_core_native_tests.ps1").read_text(
    encoding="utf-8"
)


def function_body(source: str, name: str) -> str:
    marker = f"{name}("
    search_from = 0
    while True:
        start = source.find(marker, search_from)
        assert start >= 0, f"missing definition for {name}"
        paren = start + len(name)
        depth = 0
        end_paren = -1
        for pos in range(paren, len(source)):
            if source[pos] == "(":
                depth += 1
            elif source[pos] == ")":
                depth -= 1
                if depth == 0:
                    end_paren = pos
                    break
        assert end_paren >= 0, f"unterminated signature for {name}"
        brace = end_paren + 1
        while brace < len(source) and source[brace].isspace():
            brace += 1
        if brace < len(source) and source[brace] == "{":
            break
        search_from = end_paren + 1
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : pos + 1]
    raise AssertionError(f"unterminated body for {name}")


start = function_body(HOOKS, "console_run_online_troubleshooter")
pump = function_body(HOOKS, "online_troubleshooter_pump")
finish = function_body(HOOKS, "online_troubleshooter_finish")
diag = function_body(HOOKS, "online_build_net_diag")
main_update = function_body(HOOKS, "hooked_main_update_with_buttons")

assert "net_connect(g_online_cfg.server_host" in start
assert "net_udp_probe_start(g_online_cfg.server_host" in start
load_config = start.index("online_hub_load();")
validate_target = start.index("!g_online_cfg.server_host[0]")
profile_target = start.index("net_network_profile(g_online_cfg.server_host")
tcp_target = start.index("net_connect(g_online_cfg.server_host")
udp_target = start.index("net_udp_probe_start(g_online_cfg.server_host")
assert load_config < validate_target < profile_target < tcp_target < udp_target
assert "net_check_connect(test->tcp_slot)" in pump
assert "net_udp_probe_poll(&test->udp_result" in pump
assert "online_troubleshooter_pump();" in main_update
assert "CGNAT cannot be proven from this PC alone" in finish
assert "VPN evidence:" in finish
assert "server sees %s:%u" in finish
assert "console_run_net_diag();" in finish
assert "g_online_cfg.password" not in start + pump + finish + diag
assert "p2p_auth_token" not in start + pump + finish + diag

assert "GetAdaptersAddresses" in NET
assert "GetBestInterface" in NET
assert "IF_TYPE_TUNNEL" in NET
assert "route_adapter ? route_adapter" in NET
assert "best_physical ? best_physical : best_any" in NET
assert "primary_vpn_suspected" in NET
assert "UINT32_C(0x64400000)" in NET
assert '\\"type\\":\\"udp_ping\\"' in NET
assert 'online_control_json_get_string(response, "type"' in NET
assert 'strcmp(type, "udp_pong")' in NET
assert 'online_control_json_get_uint32(response, "seq"' in NET
assert "NetNetworkProfile" in NET_H
assert "NetUdpProbeResult" in NET_H
assert "-liphlpapi" in BUILD
assert "'-liphlpapi'" in TEST_RUNNER

assert '"online.troubleshoot"' in HOOKS
assert '"net.trouble"' in HOOKS

print("online active troubleshooter static checks: OK")
