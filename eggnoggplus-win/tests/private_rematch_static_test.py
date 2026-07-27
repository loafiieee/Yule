"""Static contracts for server-owned bilateral private rematches."""

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


# The capability is required before credentials are sent and by deployment
# preflight, so a mixed rollout cannot expose a queue match to an old server.
for marker in ("function sendServerInfo", "function authOk"):
    assert "cap_private_rematch: 1" in function_body(SERVER, marker)
assert 'require_exact_int(info, "cap_private_rematch", 1)' in PREFLIGHT
protocol = function_body(HOOKS, "static int online_server_protocol_compatible")
assert '"cap_private_rematch"' in protocol
assert "private_rematch == 1" in protocol

# Eligibility is created only after a committed terminal result and is bounded
# by a server-owned wall-clock deadline.
finish = function_body(SERVER, "function finishMatch")
assert "createRematchForMatch(match)" in finish
assert "rematch_available: rematch ? 1 : 0" in finish
assert "rematch_expires_in: rematch ? rematchExpiresIn(rematch) : 0" in finish
assert "rematch_unranked: 1" in finish
create = function_body(SERVER, "function createRematchForMatch")
assert "!match || !match.committed" in create
assert "connectedClient(match.a)" in create
assert "connectedClient(match.b)" in create
assert "usersBlockEachOther(match.a, match.b)" in create
assert "expires_at: now() + REMATCH_TTL_MS" in create
assert "accepted_by: new Set()" in create

# One request only offers/waits. The second explicit vote revalidates the exact
# map through the normal matcher and deliberately supplies no ranked queue.
request = function_body(SERVER, "function handleRematchRequest")
assert "rematch.accepted_by.add(client.username)" in request
assert 'type: "rematch_waiting"' in request
assert 'type: "rematch_offer"' in request
assert 'type: "rematch_starting"' in request
assert 'makeMatch(client, otherClient, "rematch", "", 0, rematch.map_key)' in request
decline = function_body(SERVER, "function handleRematchDecline")
assert '"rematch_declined"' in decline

# Queueing, blocking, disconnecting, and entering another match close the offer.
for marker in (
    "function handleJoinQueue",
    "function handleFriendBlock",
    "function destroyClient",
    "function makeMatch",
):
    body = function_body(SERVER, marker)
    assert "cancelRematch" in body, marker

# Client requests are exact-ID, and a fresh validated match clears the previous
# result only after protocol/map checks have succeeded.
send_request = function_body(HOOKS, "static int online_server_send_rematch_request")
send_decline = function_body(HOOKS, "static int online_server_send_rematch_decline")
assert "rematch_request" in send_request
assert "g_online_result.match_id" in send_request
assert "rematch_decline" in send_decline
assert "g_online_result.match_id" in send_decline
begin = function_body(HOOKS, "static void online_server_begin_pending_match")
assert "custom_maps_selector_for_key" in begin
assert begin.index("custom_maps_selector_for_key") < begin.index(
    "memset(&g_online_result, 0"
)
assert "prior_rematch_state" in HOOKS
assert "prior_rematch_deadline_ms" in HOOKS
assert "prior_rematch_state != ONLINE_REMATCH_STARTING" in HOOKS

# The compact result toast remains non-modal while exposing accessible primary
# and secondary actions to mouse, keyboard, and distinct controller buttons.
for marker in (
    "static int online_result_rematch_action_at",
    "static int online_result_rematch_activate",
    "static void online_result_rematch_button_metrics",
    "static void online_result_render_toast",
    "static void online_result_tick_toast",
):
    function_body(HOOKS, marker)
keydown = function_body(HOOKS, "int hooks_online_hub_keydown")
control = function_body(HOOKS, "int hooks_online_hub_control_action")
mouse = function_body(HOOKS, "int hooks_online_hub_mousebutton")
assert "sym == 'r'" in keydown and "sym == 'n'" in keydown
assert "action == 7" in control and "action == 8" in control
assert "online_result_rematch_action_at" in mouse
assert "online_result_dismiss(1)" in mouse
assert "SDL_CONTROLLER_BUTTON_X) action = online_hub ? 7" in DLLMAIN
assert "SDL_CONTROLLER_BUTTON_Y) action = online_hub ? 8" in DLLMAIN

print("private rematch static checks: OK")
