"""Static contracts for persistent, private block/mute social controls."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
DLLMAIN = (ROOT / "dllmain.c").read_text(encoding="utf-8")
SERVER = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")
PREFLIGHT = (ROOT / "online_server" / "check_deployment.py").read_text(
    encoding="utf-8"
)


def function_body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    quote: str | None = None
    escaped = False
    for pos in range(brace, len(source)):
        char = source[pos]
        if quote is not None:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in ('"', "'", "`"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated function: {marker}")


shape = function_body(SERVER, "function ensureUserShape")
for field in ("blocked_users", "muted_users"):
    assert f"rec.{field} = sanitizeUserList" in shape

snapshot = function_body(SERVER, "function sendFriendSnapshot")
assert snapshot.count("!usersBlockEachOther") >= 3
assert "muted: rec.muted_users.includes(challenge.from) ? 1 : 0" in snapshot
assert "muted: rec.muted_users.includes(friend) ? 1 : 0" in snapshot
assert 'send(client, { type: "blocked_user", username: blocked })' in snapshot
blocked_send = snapshot.index('send(client, { type: "blocked_user"')
assert "elo:" not in snapshot[blocked_send : blocked_send + 120]
assert "online:" not in snapshot[blocked_send : blocked_send + 120]
assert "presence: friendPresence(friend)" in snapshot

presence = function_body(SERVER, "function friendPresence")
for state in (
    '"offline"',
    '"in_match"',
    '"match_setup"',
    '"queue_competitive"',
    '"queue_casual"',
    '"online"',
):
    assert state in presence
assert "msg." not in presence

for marker in (
    "function makeMatch",
    "function handleJoinQueue",
    "function handleLeaveQueue",
    "function handleMatchStarted",
    "function finishMatch",
    "function cancelMatch",
    "function destroyClient",
):
    assert "refreshFriendsFor" in function_body(SERVER, marker), marker
destroy = function_body(SERVER, "function destroyClient")
assert destroy.index("onlineByUser.delete(client.username)") < destroy.index(
    "refreshFriendsFor(client.username)"
)

block = function_body(SERVER, "function handleFriendBlock")
for contract in (
    "mine.blocked_users.push(target)",
    "mine.muted_users = mine.muted_users.filter",
    "mine.friends = mine.friends.filter",
    "other.friends = other.friends.filter",
    "mine.friend_requests = mine.friend_requests.filter",
    "other.friend_requests = other.friend_requests.filter",
    "expireChallengesBetween(client.username, target)",
    "saveDB()",
    'action: "blocked"',
    "sendSocialSnapshots(client.username, target)",
):
    assert contract in block

mute = function_body(SERVER, "function handleFriendMute")
assert "mine.friends.includes(target)" in mute
assert "usersBlockEachOther(client.username, target)" in mute
assert "mine.muted_users.push(target)" in mute
assert "saveDB()" in mute
assert 'type: "social_update"' in mute

for marker in (
    "function handleFriendRequest",
    "function handleFriendAccept",
    "function handleChallenge",
    "function handleChallengeMaps",
):
    assert "usersBlockEachOther" in function_body(SERVER, marker), marker
for marker in ("function handleChallenge", "function handleChallengeMaps"):
    busy = function_body(SERVER, marker)
    assert "client.match_id" in busy
    assert "targetClient.match_id" in busy
assert "usersBlockEachOther(challenge.from, challenge.to)" in function_body(
    SERVER, "function handleChallengeAccept"
)
assert "client.match_id || fromClient.match_id" in function_body(
    SERVER, "function handleChallengeAccept"
)
assert "usersBlockEachOther(casualQueue[i], casualQueue[j])" in function_body(
    SERVER, "function tryCasualMatchmaking"
)
assert "usersBlockEachOther(a, b)" in function_body(
    SERVER, "function canCompetitiveMatch"
)
assert "usersBlockEachOther(a, b)" in function_body(SERVER, "function makeMatch")

for body in (
    function_body(SERVER, "function sendServerInfo"),
    function_body(SERVER, "function authOk"),
):
    assert "cap_social_controls: 1" in body
assert 'require_exact_int(info, "cap_social_controls", 1)' in PREFLIGHT
assert '"cap_social_controls"' in function_body(
    HOOKS, "static int online_server_protocol_compatible"
)

assert '_stricmp(type, "blocked_user")' in HOOKS
assert 'online_json_get_int(line, "muted", &value)' in HOOKS
assert 'online_json_get_string(line, "presence"' in HOOKS
assert "if (!ch->muted)" in HOOKS
assert '_stricmp(type, "social_update")' in HOOKS
assert '{\\"type\\":\\"friend_mute\\"' in HOOKS
for label in (
    'label = fr->muted ? "Unmute" : "Mute"',
    'label = "Block"',
    'label = "Unfriend"',
    'label = "Unblock"',
):
    assert label in HOOKS
assert "online_context_move_selection" in HOOKS
assert "online_open_selected_friend_context" in HOOKS
for presence_label in (
    '"casual queue"',
    '"competitive queue"',
    '"setting up match"',
    '"in match"',
):
    assert presence_label in function_body(
        HOOKS, "static const char* online_friend_presence_label"
    )
assert "online_friend_can_challenge" in HOOKS
assert "C / X  SOCIAL MENU" in HOOKS
assert "online_hub ? 7 : 5" in DLLMAIN
assert "online_hub ? 6 : 5" in DLLMAIN

print("social controls static checks: OK")
