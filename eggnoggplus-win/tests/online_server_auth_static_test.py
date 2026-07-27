from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")

assert "const MATCH_PROTOCOL_VERSION = 3;" in SOURCE
assert "const P2P_PROTOCOL_VERSION = 17;" in SOURCE
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
    assert "cap_p2p_relay: P2P_RELAY_ENABLED ? 1 : 0" in body
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
assert "replayTerminalMatch(client, msg)" in ended
assert 'cancelMatch(match, "premature match result")' in ended
assert "MATCH_REPORT_TIMEOUT_MS" in SOURCE
assert 'cancelMatch(match, "conflicting match reports; no contest")' in ended
assert 'cancelMatch(match, "match result was not confirmed; no contest")' in ended
assert 'finishMatch(match, [...match.results.values()][0], "single report")' not in ended
assert "if (agreed) finishMatch" in ended
assert "winner_player" in ended
assert "match.player_by_index[winnerPlayer]" in ended
assert "using ${winner}" in ended
assert "player_by_index: []" in match_factory
assert "match.player_by_index = [hostClient.username, joinClient.username]" in match_factory

finish = function_body("function finishMatch")
cancel = function_body("function cancelMatch")
replay = function_body("function replayTerminalMatch")
assert "c.last_match_terminal = terminal" in finish
assert "c.last_match_terminal = terminal" in cancel
assert "terminal.match_id !== matchId" in replay
assert "send(client, terminal)" in replay

destroy = function_body("function destroyClient")
assert 'cancelMatch(match, "opponent disconnected before gameplay")' in destroy
assert 'finishMatch(match, other, "opponent disconnected")' in destroy

# A fresh direct-path socket generation coordinates both peers onto one
# match-owned bounded UDP relay. End-to-end packet HMAC validation remains in
# the client transport.
relay_packet = function_body("function handleRelayPacket")
assert "GGPO_PACKET_MAGIC" in relay_packet
assert "P2P_PROTOCOL_VERSION" in relay_packet
assert "relayEndpointIndex" in relay_packet
assert "match.player_by_index[senderPlayer] !== owner.username" in relay_packet
assert "relayRateAllowed" in relay_packet
register_endpoint = function_body("function registerP2pEndpoint")
assert "generation >= 2" in register_endpoint
assert "match.force_relay = true" in register_endpoint
assert "match.p2p_notified = {}" in register_endpoint
assert "retainTerminalRelay(match)" in finish
assert "clearRelayEndpoints(match)" in cancel
retain_relay = function_body("function retainTerminalRelay")
assert "RELAY_FINISH_GRACE_MS" in retain_relay
assert "terminalRelayMatches.set(match.id, match)" in retain_relay
assert "clearTerminalRelay(match)" in retain_relay
relay_lookup = function_body("function relayMatch")
assert "terminalRelayMatches.get(matchId)" in relay_lookup
assert "relay_finish_expires_at <= now()" in relay_lookup

# The periodic TTL applies only to pending setup. A legitimate committed match
# is governed by result/disconnect handling and cannot be canceled at ten minutes.
assert "if (match.finished || match.committed) continue;" in SOURCE
assert "cutoff - match.created_at > MATCH_SETUP_STALE_MS" in SOURCE
assert 'cancelMatch(match, "stale match")' not in SOURCE

print("online server P2P auth static checks: OK")
