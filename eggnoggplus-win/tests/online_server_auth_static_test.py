from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")

assert "const MATCH_PROTOCOL_VERSION = 3;" in SOURCE
assert 'process.env.CLIENT_IDLE_TIMEOUT_MS || "120000"' in SOURCE
assert "socket.setTimeout(CLIENT_IDLE_TIMEOUT_MS);" in SOURCE
assert 'case "ping": send(client, { type: "pong", seq: msg.seq || 0 }); break;' in SOURCE


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

# Match protocol 3 is a two-client commit barrier. All client lifecycle input
# is bound to the exact positive current match ID; pre-commit exits cancel with
# no result, while post-commit abort/disconnect is a forfeit.
match_factory = function_body("function makeMatch")
assert "started_by: new Set()" in match_factory
assert "committed: false" in match_factory
assert "created_at: now()" in match_factory

lookup = function_body("function matchForClientMessage")
assert "Number.isInteger(matchId)" in lookup
assert "matchId <= 0" in lookup
assert "matchId !== client.match_id" in lookup
assert "matchHasUser(match, client.username)" in lookup

started = function_body("function handleMatchStarted")
assert "match.started_by.add(client.username)" in started
assert "match.started_by.size < 2" in started
assert "match.committed = true" in started
assert "committed: 1" in started

aborted = function_body("function handleMatchAbort")
assert "if (!match.committed)" in aborted
assert "cancelMatch(match" in aborted
assert "finishMatch(match, winner" in aborted

ended = function_body("function handleMatchEnd")
assert "if (!match.committed)" in ended
assert 'cancelMatch(match, "premature match result")' in ended
assert "MATCH_REPORT_TIMEOUT_MS" in SOURCE
assert 'cancelMatch(match, "conflicting match reports; no contest")' in ended
assert 'cancelMatch(match, "match result was not confirmed; no contest")' in ended
assert 'finishMatch(match, [...match.results.values()][0], "single report")' not in ended
assert "if (agreed) finishMatch" in ended

destroy = function_body("function destroyClient")
assert 'cancelMatch(match, "opponent disconnected before gameplay")' in destroy
assert 'finishMatch(match, other, "opponent disconnected")' in destroy

# The periodic TTL applies only to pending setup. A legitimate committed match
# is governed by result/disconnect handling and cannot be canceled at ten minutes.
assert "if (match.finished || match.committed) continue;" in SOURCE
assert "cutoff - match.created_at > MATCH_SETUP_STALE_MS" in SOURCE
assert 'cancelMatch(match, "stale match")' not in SOURCE

print("online server P2P auth static checks: OK")
