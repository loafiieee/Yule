from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "lua_manager.c").read_text(encoding="utf-8")


def function_body(marker: str) -> str:
    start = SOURCE.index(marker)
    brace = SOURCE.index("{", start)
    depth = 0
    for pos in range(brace, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : pos]
    raise AssertionError(f"unterminated function: {marker}")


worker = function_body("static DWORD WINAPI http_worker_thread")
get = function_body("static int lua_http_get")
poll = function_body("static int lua_http_poll")
cancel = function_body("static int lua_http_cancel")
reap = function_body("static void http_reap_cancelled_slots")
release = function_body("static void http_release_slot")
unload = function_body("static void unload_single_mod_runtime")
frame = function_body("void lua_manager_on_frame")
table = function_body("static void push_http_api_table")

assert "#define HTTP_MAX_BODY_BYTES (8u * 1024u * 1024u)" in SOURCE
assert "LoadedMod     *owner;" in SOURCE
assert "volatile LONG  cancelled;" in SOURCE
assert "int            handle;" in SOURCE

assert "content_length > HTTP_MAX_BODY_BYTES" in worker
assert "(size_t)avail > HTTP_MAX_BODY_BYTES - len" in worker
assert "char  *buf" in worker
assert "slot->body     = buf;" in worker
assert "InterlockedExchange(&slot->done, 1)" in worker

assert "mod_from_upvalue" in get
assert "slot->owner = owner;" in get
assert "slot->handle = g_http_next_handle;" in get
assert "lua_pushinteger(L, slot->handle)" in get
assert "http_find_slot(handle, owner)" in poll
assert "http_release_slot(slot)" in poll

assert "InterlockedExchange(&slot->cancelled, 1)" in cancel
assert "CloseHandle" not in cancel
assert "free(" not in cancel
assert "slot->in_use = 0" not in cancel
assert "http_release_slot(slot)" in reap
assert "InterlockedCompareExchange(&slot->done" in reap
assert "CloseHandle(slot->thread)" in release

assert "http_cancel_owner(mod);" in unload
assert "http_reap_cancelled_slots();" in frame
assert table.count("lua_pushlightuserdata(Ls, mod)") == 3
assert table.count("lua_pushcclosure") == 3
assert "push_http_api_table(Ls, mod);" in SOURCE

print("Lua HTTP owner/cancellation/body-bound static checks: OK")
