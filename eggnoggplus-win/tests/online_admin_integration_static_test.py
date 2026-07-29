from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT / "online_server" / "server.js").read_text(encoding="utf-8")
ADMIN = (ROOT / "online_server" / "admin_server.js").read_text(encoding="utf-8")
UPDATER = (ROOT / "online_server" / "update_server.sh").read_text(encoding="utf-8")


def function_body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    quote = None
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


login = function_body(SERVER, "function handleLogin")
shape = function_body(SERVER, "function ensureUserShape")
password = function_body(SERVER, "function adminResetPassword")
ban = function_body(SERVER, "function adminSetBan")
rating = function_body(SERVER, "function adminResetRating")
shutdown = function_body(SERVER, "async function orderlyShutdown")

assert 'require("./admin_server")' in SERVER
assert "startAdminServerFromEnv(process.env" in SERVER
assert "if (rec.ban)" in login
assert "account banned" in login
assert "delete rec.ban" in shape
assert "crypto.randomBytes(16)" in password
assert "saveDB()" in password
assert "destroyClient(client)" in password
assert "rec.ban =" in ban
assert "saveDB()" in ban
assert "destroyClient(client)" in ban
assert "setRating(username, DEFAULT_ELO, DEFAULT_MMR)" in rating
assert "saveRatings()" in rating
assert "closeListener(adminServer)" in shutdown

for requirement in (
    "ADMIN_ENABLED",
    "isPrivateBindHost(host)",
    "isPrivateBindHost(ip)",
    "SESSION_MAX",
    "ADMIN_COOKIE_SECURE",
    "SameSite=Strict",
    "HttpOnly",
    "constantTimeTextEqual",
    "MAX_BODY_BYTES",
):
    assert requirement in ADMIN
assert 'form.get("csrf")' in ADMIN
assert "ADMIN_PASSWORD" not in ADMIN
assert 'url.pathname === "/login"' not in ADMIN
assert "find \"$SOURCE_SERVER\" -type f" in UPDATER

print("online LAN admin integration static checks: OK")
