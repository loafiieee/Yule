#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif

#include "mod_http.h"

#include <windows.h>
#include <winhttp.h>

#include <luajit-2.1/lauxlib.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ---- mod.http: async HTTPS-capable GET via WinHTTP + worker thread ------- */

#define HTTP_MAX_SLOTS 8
#define HTTP_MAX_BODY_BYTES (8u * 1024u * 1024u)

typedef struct {
    int            in_use;
    int            handle;
    void          *owner;
    HANDLE         thread;
    volatile LONG  done;      /* 0=pending, 1=ok, -1=error; worker-owned write */
    volatile LONG  cancelled; /* cancellation is observed by the worker/reaper */
    char          *body;
    size_t         body_len;
    char           error_msg[256];
    wchar_t        url[2048];
} HttpSlot;

static HttpSlot g_http_slots[HTTP_MAX_SLOTS];
static int g_http_next_handle = 1;

static void http_release_slot(HttpSlot *slot) {
    if (!slot) return;
    if (slot->thread) {
        CloseHandle(slot->thread);
        slot->thread = NULL;
    }
    free(slot->body);
    memset(slot, 0, sizeof(*slot));
}

void mod_http_pump(void) {
    for (int i = 0; i < HTTP_MAX_SLOTS; i++) {
        HttpSlot *slot = &g_http_slots[i];
        if (!slot->in_use ||
            InterlockedCompareExchange(&slot->cancelled, 0, 0) == 0 ||
            InterlockedCompareExchange(&slot->done, 0, 0) == 0) {
            continue;
        }
        http_release_slot(slot);
    }
}

static HttpSlot *http_find_slot(int handle, void *owner) {
    if (handle <= 0 || !owner) return NULL;
    for (int i = 0; i < HTTP_MAX_SLOTS; i++) {
        HttpSlot *slot = &g_http_slots[i];
        if (slot->in_use && slot->handle == handle && slot->owner == owner) {
            return slot;
        }
    }
    return NULL;
}

void mod_http_cancel_owner(void *owner) {
    if (!owner) return;
    for (int i = 0; i < HTTP_MAX_SLOTS; i++) {
        HttpSlot *slot = &g_http_slots[i];
        if (slot->in_use && slot->owner == owner) {
            InterlockedExchange(&slot->cancelled, 1);
        }
    }
    mod_http_pump();
}

static DWORD WINAPI http_worker_thread(LPVOID param) {
    HttpSlot *slot = (HttpSlot *)param;

    URL_COMPONENTS uc;
    wchar_t host[512] = {0};
    wchar_t path[2048] = {0};
    wchar_t extra[2048] = {0};
    wchar_t user[256] = {0};
    wchar_t password[256] = {0};
    wchar_t request_target[4096] = {0};
    size_t path_len;
    size_t extra_len;
    wchar_t* fragment;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = (DWORD)(sizeof(host) / sizeof(host[0]));
    uc.lpszUrlPath       = path;
    uc.dwUrlPathLength   = (DWORD)(sizeof(path) / sizeof(path[0]));
    uc.lpszExtraInfo     = extra;
    uc.dwExtraInfoLength = (DWORD)(sizeof(extra) / sizeof(extra[0]));
    uc.lpszUserName      = user;
    uc.dwUserNameLength  = (DWORD)(sizeof(user) / sizeof(user[0]));
    uc.lpszPassword      = password;
    uc.dwPasswordLength  = (DWORD)(sizeof(password) / sizeof(password[0]));

    if (!WinHttpCrackUrl(slot->url, 0, 0, &uc)) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "bad URL (WinHttpCrackUrl err %lu)", GetLastError());
        InterlockedExchange(&slot->done, -1);
        return 0;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP &&
        uc.nScheme != INTERNET_SCHEME_HTTPS) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
                  "URL scheme must be http or https");
        InterlockedExchange(&slot->done, -1);
        return 0;
    }
    if (user[0] || password[0]) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
                  "URL credentials are not supported");
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* WinHttpCrackUrl exposes the query/fragment through lpszExtraInfo rather
     * than lpszUrlPath. Preserve the query in the request target and strip the
     * client-only fragment so GET URLs behave exactly as authored. */
    fragment = wcschr(extra, L'#');
    if (fragment) *fragment = L'\0';
    path_len = wcslen(path);
    extra_len = wcslen(extra);
    if (path_len == 0) {
        request_target[0] = L'/';
        path_len = 1;
    } else {
        memcpy(request_target, path, (path_len + 1u) * sizeof(wchar_t));
    }
    if (path_len + extra_len + 1u >
        sizeof(request_target) / sizeof(request_target[0])) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
                  "URL request target is too long");
        InterlockedExchange(&slot->done, -1);
        return 0;
    }
    if (extra_len != 0) {
        memcpy(request_target + path_len, extra,
               (extra_len + 1u) * sizeof(wchar_t));
    }

    HINTERNET session = WinHttpOpen(
        L"EggnoggPlus/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "WinHttpOpen failed %lu", GetLastError());
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* 10-second resolve+connect timeout */
    DWORD timeout_ms = 10000;
    WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));
    WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));
    WinHttpSetOption(session, WINHTTP_OPTION_SEND_TIMEOUT,       &timeout_ms, sizeof(timeout_ms));
    WinHttpSetOption(session, WINHTTP_OPTION_RESOLVE_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));

    INTERNET_PORT port = uc.nPort
        ? uc.nPort
        : (uc.nScheme == INTERNET_SCHEME_HTTPS
            ? INTERNET_DEFAULT_HTTPS_PORT
            : INTERNET_DEFAULT_HTTP_PORT);

    HINTERNET conn = WinHttpConnect(session, host, port, 0);
    if (!conn) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "WinHttpConnect failed %lu", GetLastError());
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    DWORD req_flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET",
        request_target,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
    if (!req) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "WinHttpOpenRequest failed %lu", GetLastError());
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "request failed %lu", GetLastError());
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* Verify HTTP status code */
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status_code != 200) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "HTTP %lu", status_code);
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* Reject declared oversized bodies before allocating them. Some servers
     * omit Content-Length, so the streaming loop enforces the same ceiling. */
    {
        DWORD content_length = 0;
        DWORD content_length_size = sizeof(content_length);
        if (WinHttpQueryHeaders(req,
                                WINHTTP_QUERY_CONTENT_LENGTH |
                                    WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &content_length,
                                &content_length_size,
                                WINHTTP_NO_HEADER_INDEX) &&
            content_length > HTTP_MAX_BODY_BYTES) {
            _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
                      "response body exceeds %u bytes",
                      (unsigned int)HTTP_MAX_BODY_BYTES);
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            WinHttpCloseHandle(session);
            InterlockedExchange(&slot->done, -1);
            return 0;
        }
    }

    /* Read body incrementally. Keep the buffer worker-local until the final
     * publication so cancel/poll never races realloc or free. */
    size_t cap = 8192, len = 0;
    char  *buf = (char *)malloc(cap);
    int    ok  = (buf != NULL);
    int    too_large = 0;

    while (ok) {
        DWORD avail = 0;
        if (InterlockedCompareExchange(&slot->cancelled, 0, 0) != 0) {
            ok = 0;
            break;
        }
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            ok = 0;
            break;
        }
        if (avail == 0) break;
        if ((size_t)avail > HTTP_MAX_BODY_BYTES - len) {
            too_large = 1;
            ok = 0;
            break;
        }
        if (len + avail + 1 > cap) {
            size_t newcap = (len + avail + 1) * 2;
            if (newcap > HTTP_MAX_BODY_BYTES + 1u) {
                newcap = HTTP_MAX_BODY_BYTES + 1u;
            }
            char *tmp = (char *)realloc(buf, newcap);
            if (!tmp) { ok = 0; break; }
            buf = tmp;
            cap = newcap;
        }
        DWORD nread = 0;
        if (!WinHttpReadData(req, buf + len, avail, &nread)) { ok = 0; break; }
        len += nread;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (ok && buf &&
        InterlockedCompareExchange(&slot->cancelled, 0, 0) == 0) {
        buf[len]      = '\0';
        slot->body     = buf;
        slot->body_len = len;
        InterlockedExchange(&slot->done, 1);
    } else {
        free(buf);
        if (too_large) {
            _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
                      "response body exceeds %u bytes",
                      (unsigned int)HTTP_MAX_BODY_BYTES);
        } else if (InterlockedCompareExchange(&slot->cancelled, 0, 0) == 0) {
            _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
                      "failed reading response body");
        }
        InterlockedExchange(&slot->done, -1);
    }
    return 0;
}

/* mod.http.get(url_string) -> handle_int or nil, errmsg */
static int lua_http_get(lua_State *L) {
    void *owner = lua_touserdata(L, lua_upvalueindex(1));
    const int *owner_enabled =
        (const int*)lua_touserdata(L, lua_upvalueindex(2));
    size_t url_bytes = 0;
    const char *url_utf8 = luaL_checklstring(L, 1, &url_bytes);

    if (!owner || !owner_enabled || !*owner_enabled) {
        lua_pushnil(L);
        lua_pushstring(L, "mod is not active");
        return 2;
    }
    mod_http_pump();

    int idx = -1;
    for (int i = 0; i < HTTP_MAX_SLOTS; i++) {
        if (!g_http_slots[i].in_use) { idx = i; break; }
    }
    if (idx < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "too many concurrent HTTP requests");
        return 2;
    }

    HttpSlot *slot = &g_http_slots[idx];
    memset(slot, 0, sizeof(*slot));

    if (url_bytes == 0 || strlen(url_utf8) != url_bytes ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url_utf8, -1,
                             slot->url,
                             (int)(sizeof(slot->url) / sizeof(slot->url[0])))) {
        lua_pushnil(L);
        lua_pushstring(L, "URL too long or invalid UTF-8");
        return 2;
    }

    slot->in_use = 1;
    slot->owner = owner;
    slot->handle = g_http_next_handle;
    g_http_next_handle =
        (g_http_next_handle == INT_MAX) ? 1 : g_http_next_handle + 1;
    slot->done   = 0;
    slot->cancelled = 0;
    slot->thread = CreateThread(NULL, 0, http_worker_thread, slot, 0, NULL);
    if (!slot->thread) {
        http_release_slot(slot);
        lua_pushnil(L);
        lua_pushstring(L, "CreateThread failed");
        return 2;
    }

    lua_pushinteger(L, slot->handle);
    return 1;
}

/* mod.http.poll(handle) -> "pending" | "done", body | "error", msg */
static int lua_http_poll(lua_State *L) {
    void *owner = lua_touserdata(L, lua_upvalueindex(1));
    int handle = (int)luaL_checkinteger(L, 1);
    HttpSlot *slot;
    mod_http_pump();
    slot = http_find_slot(handle, owner);
    if (!slot ||
        InterlockedCompareExchange(&slot->cancelled, 0, 0) != 0) {
        lua_pushstring(L, "error");
        lua_pushstring(L, "invalid handle");
        return 2;
    }
    LONG d = InterlockedCompareExchange(&slot->done, 0, 0);  /* atomic read */
    if (d == 0) {
        lua_pushstring(L, "pending");
        return 1;
    }
    if (d == 1) {
        lua_pushstring(L, "done");
        lua_pushlstring(L, slot->body ? slot->body : "", slot->body_len);
        http_release_slot(slot);
        return 2;
    }
    lua_pushstring(L, "error");
    lua_pushstring(L, slot->error_msg[0]
        ? slot->error_msg
        : "request failed");
    http_release_slot(slot);
    return 2;
}

/* mod.http.cancel(handle) */
static int lua_http_cancel(lua_State *L) {
    void *owner = lua_touserdata(L, lua_upvalueindex(1));
    int handle = (int)luaL_checkinteger(L, 1);
    HttpSlot *slot;
    mod_http_pump();
    slot = http_find_slot(handle, owner);
    if (slot) {
        /* Do not free or reuse the slot until the worker has published done.
         * Closing the thread HANDLE does not stop the thread. */
        InterlockedExchange(&slot->cancelled, 1);
        mod_http_pump();
    }
    return 0;
}

static void http_register_owner_function(lua_State *L, void *owner,
                                         lua_CFunction function,
                                         const char *name) {
    lua_pushlightuserdata(L, owner);
    lua_pushcclosure(L, function, 1);
    lua_setfield(L, -2, name);
}

void mod_http_lua_push_api(lua_State *L, void *owner,
                           const int *owner_enabled) {
    lua_newtable(L);

    lua_pushlightuserdata(L, owner);
    lua_pushlightuserdata(L, (void*)owner_enabled);
    lua_pushcclosure(L, lua_http_get, 2);
    lua_setfield(L, -2, "get");

    http_register_owner_function(L, owner, lua_http_poll, "poll");
    http_register_owner_function(L, owner, lua_http_cancel, "cancel");
}
