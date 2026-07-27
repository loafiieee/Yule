import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")
BOT = (ROOT / "online_server" / "discord_lfg_bot.js").read_text(encoding="utf-8")
REDIRECT = (ROOT / "online_server" / "lfg_redirect.js").read_text(encoding="utf-8")

assert 'require("./discord_lfg_bot")' in SERVER
assert 'require("./lfg_redirect")' in SERVER
assert "createDiscordLfgBotFromEnv(process.env" in SERVER
assert "startRedirectServerFromEnv(process.env)" in SERVER
assert "const matchSweepTimer = setInterval" in SERVER
assert 'process.once("SIGINT"' in SERVER
assert 'process.once("SIGTERM"' in SERVER
assert "clearInterval(matchSweepTimer)" in SERVER
assert "for (const client of [...clients]) destroyClient(client)" in SERVER
assert "lfgBot.close({ retire: true })" in SERVER
assert "closeListener(lfgRedirectServer)" in SERVER

join = re.search(
    r"function handleJoinQueue\(client, msg\).*?^}",
    SERVER,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "tryMatchmaking();" in join
assert "if (client.queue === queue) lfgBot.queueJoined(client.username, queue);" in join
assert join.index("tryMatchmaking();") < join.index("lfgBot.queueJoined")
for forbidden in ("p2p_token", "auth_token", "peer_host", "peer_port"):
    assert forbidden not in join[join.index("lfgBot.queueJoined"):]

remove = re.search(
    r"function removeFromQueues\(client, reason = \"left\"\).*?^}",
    SERVER,
    re.DOTALL | re.MULTILINE,
).group(0)
assert "lfgBot.queueLeft(client.username, reason)" in remove
assert 'removeFromQueues(a, "matched")' in SERVER
assert 'removeFromQueues(client, "disconnected")' in SERVER

assert "allowed_mentions: { parse: [], users: [], roles: [], replied_user: false }" in BOT
assert "retry_after" in BOT and "response.status === 429" in BOT
assert "DEFAULT_POST_DELAY_MS = 2000" in BOT
assert "DEFAULT_MATCH_DELETE_MS = 24 * 60 * 60 * 1000" in BOT
assert '"PATCH"' in BOT
assert '"DELETE"' in BOT
assert 'if (reason === "matched") this.scheduleMatchedDelete(messageId)' in BOT
assert "DISCORD_LFG_MATCH_DELETE_MS" in BOT
assert "PENDING_CREATE_MAX = 4096" in BOT
assert "pending-request limit reached" in BOT
assert "Authorization" in BOT and "`Bot ${token}`" in BOT
assert "console.log" not in BOT
assert "p2p_auth_token" not in BOT and "match_id" not in BOT

assert "yule://challenge/" in REDIRECT
assert "yule://queue/" in REDIRECT
assert "yule://match" not in REDIRECT
assert "yule://join" not in REDIRECT
assert '"Cache-Control": "no-store"' in REDIRECT
assert '"Referrer-Policy": "no-referrer"' in REDIRECT
assert 'host !== "127.0.0.1" && host !== "::1"' in REDIRECT

print("Discord LFG server/bot privacy lifecycle checks: OK")
