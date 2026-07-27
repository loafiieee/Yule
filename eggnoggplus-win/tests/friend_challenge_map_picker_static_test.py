"""Static contract checks for the authoritative friend-challenge map picker."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
SERVER = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")
PREFLIGHT = (ROOT / "online_server" / "check_deployment.py").read_text(
    encoding="utf-8"
)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\bfunction\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    assert match, f"missing JavaScript function {name}"
    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    assert depth == 0, f"unterminated JavaScript function {name}"
    return source[start : pos - 1]


assert "#define ONLINE_CONTROL_PROTOCOL_VERSION      3" in HOOKS
assert "const CONTROL_PROTOCOL_VERSION = 3;" in SERVER
assert "REQUIRED_CONTROL_PROTOCOL = 3" in PREFLIGHT

shared = function_body(SERVER, "sharedMapChoices")
assert "right.get(left.key)" in shared
assert "aSelector: left.selector" in shared
assert "bSelector: r.selector" in shared

request = function_body(SERVER, "handleChallengeMaps")
for message_type in (
    "challenge_maps_begin",
    "challenge_map_choice",
    "challenge_maps_end",
):
    assert message_type in request
assert "sharedMapChoices(client, targetClient)" in request
assert "request_id" in request

challenge = function_body(SERVER, "handleChallenge")
assert "sharedMapChoices(client, targetClient)" in challenge
assert "map.key === selectedKey" in challenge
assert "map_key: selectedMap.key" in challenge
assert "map_label: selectedMap.label" in challenge

accept = function_body(SERVER, "handleChallengeAccept")
assert "sharedMapChoices(fromClient, client)" in accept
assert "map.key === challenge.map_key" in accept
assert 'makeMatch(fromClient, client, "challenge", "", challenge.id, selectedMap.key)' in accept

assert "ONLINE_CHALLENGE_MAP_MAX 4096" in HOOKS
assert "online_challenge_map_picker_begin(g_online_friends[idx].name)" in HOOKS
assert '"{\\"type\\":\\"challenge_maps\\",\\"username\\":\\"%s\\",\\"request_id\\":%d}\\n"' in HOOKS
assert '"{\\"type\\":\\"challenge\\",\\"username\\":\\"%s\\",\\"map_key\\":\\"%s\\"}\\n"' in HOOKS
for message_type in (
    "challenge_maps_begin",
    "challenge_map_choice",
    "challenge_maps_end",
):
    assert f'_stricmp(type, "{message_type}")' in HOOKS
assert "count != g_online_challenge_map_picker.expected_count" in HOOKS
assert "count != g_online_challenge_map_picker.choice_count" in HOOKS
assert "ONLINE_ROW_CHALLENGE_MAP" in HOOKS
assert "ONLINE_ACTION_SEND_CHALLENGE" in HOOKS
assert "g_online_challenge_toast.map_label" in HOOKS

print("friend challenge map picker static checks: OK")
