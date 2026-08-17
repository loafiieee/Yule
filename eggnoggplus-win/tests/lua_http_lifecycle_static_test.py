from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "mod_http.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
HEADER = (ROOT / "mod_http.h").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")


def function_body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated function: {marker}")


worker = function_body(SOURCE, "static DWORD WINAPI http_worker_thread")
get = function_body(SOURCE, "static int lua_http_get")
poll = function_body(SOURCE, "static int lua_http_poll")
cancel = function_body(SOURCE, "static int lua_http_cancel")
reap = function_body(SOURCE, "void mod_http_pump")
release = function_body(SOURCE, "static void http_release_slot")
unload = function_body(MANAGER, "static void unload_single_mod_runtime")
frame = function_body(MANAGER, "void lua_manager_on_frame")
table = function_body(SOURCE, "void mod_http_lua_push_api")

assert "#define HTTP_MAX_BODY_BYTES (8u * 1024u * 1024u)" in SOURCE
assert "void          *owner;" in SOURCE
assert "LoadedMod" not in SOURCE
assert "volatile LONG  cancelled;" in SOURCE
assert "int            handle;" in SOURCE

assert "content_length > HTTP_MAX_BODY_BYTES" in worker
assert "(size_t)avail > HTTP_MAX_BODY_BYTES - len" in worker
assert "char  *buf" in worker
assert "slot->body     = buf;" in worker
assert "InterlockedExchange(&slot->done, 1)" in worker
assert "uc.lpszExtraInfo     = extra;" in worker
assert "fragment = wcschr(extra, L'#');" in worker
assert "request_target + path_len" in worker
assert "URL scheme must be http or https" in worker
assert "URL credentials are not supported" in worker
assert "WinHttpOpenRequest(conn, L\"GET\"" in worker
assert "request_target," in worker
assert "HTTP_MAX_REDIRECTS 5u" in SOURCE
assert "WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS" in worker
assert "WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP" in worker
assert "http_capture_response_metadata(req, slot);" in worker
assert "slot->status_code != 200" in worker
assert "WINHTTP_CALLBACK_STATUS_REDIRECT" in SOURCE
assert 'lua_setfield(L, -2, "redirect_count")' in SOURCE
assert 'lua_setfield(L, -2, "headers")' in SOURCE
for header in (
    "content-type",
    "content-length",
    "etag",
    "last-modified",
    "cache-control",
    "location",
):
    assert f'"{header}"' in SOURCE

assert "owner_enabled" in get
assert "!*owner_enabled" in get
assert "luaL_checklstring" in get
assert "strlen(url_utf8) != url_bytes" in get
assert "MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS" in get
assert "slot->owner = owner;" in get
assert "slot->handle = http_allocate_handle();" in get
assert "lua_pushinteger(L, slot->handle)" in get
assert "http_find_slot(handle, owner)" in poll
assert "http_release_slot(slot)" in poll
assert "lua_http_push_response(L, slot)" in poll

assert "InterlockedExchange(&slot->cancelled, 1)" in cancel
assert "CloseHandle" not in cancel
assert "free(" not in cancel
assert "slot->in_use = 0" not in cancel
assert "http_release_slot(slot)" in reap
assert "InterlockedCompareExchange(&slot->done" in reap
assert "CloseHandle(slot->thread)" in release

assert "mod_http_cancel_owner(mod);" in unload
assert "mod_http_pump();" in frame
assert "lua_pushcclosure(L, lua_http_get, 2)" in table
assert 'http_register_owner_function(L, owner, lua_http_poll, "poll")' in table
assert 'http_register_owner_function(L, owner, lua_http_cancel, "cancel")' in table
assert "mod_http_lua_push_api(Ls, mod, &mod->enabled);" in MANAGER
assert "void mod_http_lua_push_api" in HEADER
assert "mod_http.c" in BUILD

print("Lua HTTP owner/cancellation/body-bound static checks: OK")
