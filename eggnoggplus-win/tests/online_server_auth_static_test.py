from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")


def function_body(marker: str) -> str:
    start = SOURCE.index(marker)
    brace = SOURCE.index("{", start)
    depth = 0
    quote = None
    escaped = False
    for pos in range(brace, len(SOURCE)):
        ch = SOURCE[pos]
        if quote is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            continue
        if ch in ('"', "'", "`"):
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : pos]
    raise AssertionError(f"unterminated function: {marker}")


token_factory = function_body("function makeP2pAuthToken")
match_factory = function_body("function makeMatch")
server_info = function_body("function sendServerInfo")
auth_ok = function_body("function authOk")

assert 'crypto.randomBytes(32).toString("hex")' in token_factory
assert "p2p_auth_token: makeP2pAuthToken()" in match_factory
assert match_factory.count("p2p_auth_token: match.p2p_auth_token") == 2
assert match_factory.count("match_protocol: MATCH_PROTOCOL_VERSION") == 2
assert match_factory.count("p2p_protocol: P2P_PROTOCOL_VERSION") == 2

# Deployment compatibility is observable without an account and repeated in
# auth_ok. All fields stay scalar for the strict flat client JSON parser.
for body in (server_info, auth_ok):
    assert "control_protocol: CONTROL_PROTOCOL_VERSION" in body
    assert "match_protocol: MATCH_PROTOCOL_VERSION" in body
    assert "p2p_protocol: P2P_PROTOCOL_VERSION" in body
    assert "cap_p2p_auth: 1" in body
assert 'case "server_info": sendServerInfo(client); break;' in SOURCE
assert "sendServerInfo(client);" in SOURCE[SOURCE.index("const server = net.createServer") :]

# Rendezvous authorization remains per-user and distinct from the shared peer
# packet key. Both roles must still receive their own p2p_token entry.
assert match_factory.count("p2p_token: match.p2p_tokens[") == 2
assert "p2p_auth_token=${" not in SOURCE

print("online server P2P auth static checks: OK")
