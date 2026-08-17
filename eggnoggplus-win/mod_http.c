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
#define HTTP_MAX_REDIRECTS 5u
#define HTTP_FINAL_URL_WCHARS 4096
#define HTTP_FINAL_URL_BYTES (HTTP_FINAL_URL_WCHARS * 4u)
#define HTTP_HEADER_VALUE_BYTES 1024

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
    DWORD          status_code;
    volatile LONG  redirect_count;
    char           final_url[HTTP_FINAL_URL_BYTES];
    char           status_text[128];
    char           content_type[HTTP_HEADER_VALUE_BYTES];
    char           content_length[64];
    char           etag[HTTP_HEADER_VALUE_BYTES];
    char           last_modified[HTTP_HEADER_VALUE_BYTES];
    char           cache_control[HTTP_HEADER_VALUE_BYTES];
    char           location[HTTP_HEADER_VALUE_BYTES];
} HttpSlot;

static HttpSlot g_http_slots[HTTP_MAX_SLOTS];
static int g_http_next_handle = 1;

static int http_handle_in_use(int handle) {
    for (int i = 0; i < HTTP_MAX_SLOTS; i++) {
        if (g_http_slots[i].in_use && g_http_slots[i].handle == handle) {
            return 1;
        }
    }
    return 0;
}

static int http_allocate_handle(void) {
    /* At most eight handles are live, so nine probes are sufficient even at
     * the INT_MAX wrap boundary. Do not collide with a concurrent request. */
    for (int probe = 0; probe <= HTTP_MAX_SLOTS; probe++) {
        int handle = g_http_next_handle;
        g_http_next_handle =
            (g_http_next_handle == INT_MAX) ? 1 : g_http_next_handle + 1;
        if (!http_handle_in_use(handle)) return handle;
    }
    return 0;
}

static void CALLBACK http_status_callback(
    HINTERNET handle,
    DWORD_PTR context,
    DWORD status,
    LPVOID status_info,
    DWORD status_info_length
) {
    HttpSlot *slot = (HttpSlot*)context;
    (void)handle;
    (void)status_info;
    (void)status_info_length;
    if (slot && status == WINHTTP_CALLBACK_STATUS_REDIRECT) {
        InterlockedIncrement(&slot->redirect_count);
    }
}

static int http_wide_to_utf8(const wchar_t *wide, char *output,
                             size_t output_capacity) {
    int result;
    if (!wide || !output || output_capacity == 0 ||
        output_capacity > (size_t)INT_MAX) {
        return 0;
    }
    output[0] = '\0';
    result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                 output, (int)output_capacity,
                                 NULL, NULL);
    if (result <= 0) {
        output[0] = '\0';
        return 0;
    }
    return 1;
}

static void http_query_header_utf8(HINTERNET request, DWORD query,
                                   char *output, size_t output_capacity) {
    wchar_t wide[HTTP_HEADER_VALUE_BYTES];
    DWORD bytes = sizeof(wide);
    if (!output || output_capacity == 0) return;
    output[0] = '\0';
    memset(wide, 0, sizeof(wide));
    if (!WinHttpQueryHeaders(request, query,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             wide, &bytes, WINHTTP_NO_HEADER_INDEX)) {
        return;
    }
    wide[(sizeof(wide) / sizeof(wide[0])) - 1] = L'\0';
    http_wide_to_utf8(wide, output, output_capacity);
}

static void http_capture_response_metadata(HINTERNET request, HttpSlot *slot) {
    wchar_t final_url[HTTP_FINAL_URL_WCHARS];
    DWORD final_url_bytes = sizeof(final_url);
    DWORD status_size = sizeof(slot->status_code);
    if (!request || !slot) return;

    slot->status_code = 0;
    WinHttpQueryHeaders(request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &slot->status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

    memset(final_url, 0, sizeof(final_url));
    if (WinHttpQueryOption(request, WINHTTP_OPTION_URL,
                           final_url, &final_url_bytes)) {
        final_url[(sizeof(final_url) / sizeof(final_url[0])) - 1] = L'\0';
        http_wide_to_utf8(final_url, slot->final_url,
                          sizeof(slot->final_url));
    }
    if (!slot->final_url[0]) {
        http_wide_to_utf8(slot->url, slot->final_url,
                          sizeof(slot->final_url));
    }

    http_query_header_utf8(request, WINHTTP_QUERY_STATUS_TEXT,
                           slot->status_text, sizeof(slot->status_text));
    http_query_header_utf8(request, WINHTTP_QUERY_CONTENT_TYPE,
                           slot->content_type, sizeof(slot->content_type));
    http_query_header_utf8(request, WINHTTP_QUERY_CONTENT_LENGTH,
                           slot->content_length, sizeof(slot->content_length));
    http_query_header_utf8(request, WINHTTP_QUERY_ETAG,
                           slot->etag, sizeof(slot->etag));
    http_query_header_utf8(request, WINHTTP_QUERY_LAST_MODIFIED,
                           slot->last_modified, sizeof(slot->last_modified));
    http_query_header_utf8(request, WINHTTP_QUERY_CACHE_CONTROL,
                           slot->cache_control, sizeof(slot->cache_control));
    http_query_header_utf8(request, WINHTTP_QUERY_LOCATION,
                           slot->location, sizeof(slot->location));
}

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

    {
        DWORD_PTR context = (DWORD_PTR)slot;
        DWORD redirect_policy =
            WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        DWORD max_redirects = HTTP_MAX_REDIRECTS;
        WinHttpSetOption(req, WINHTTP_OPTION_CONTEXT_VALUE,
                         &context, sizeof(context));
        WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect_policy, sizeof(redirect_policy));
        WinHttpSetOption(req, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                         &max_redirects, sizeof(max_redirects));
        WinHttpSetStatusCallback(req, http_status_callback,
                                 WINHTTP_CALLBACK_FLAG_REDIRECT, 0);
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

    /* Capture a deliberately bounded metadata subset before deciding whether
     * this response is successful. Non-200 terminal polls receive the same
     * status/URL/header table as successful polls. */
    http_capture_response_metadata(req, slot);
    if (slot->status_code != 200) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "HTTP %lu", slot->status_code);
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
    slot->handle = http_allocate_handle();
    if (slot->handle == 0) {
        http_release_slot(slot);
        lua_pushnil(L);
        lua_pushstring(L, "HTTP handle space exhausted");
        return 2;
    }
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

static void lua_http_set_header(lua_State *L, const char *name,
                                const char *value) {
    if (!value || !value[0]) return;
    lua_pushstring(L, value);
    lua_setfield(L, -2, name);
}

static void lua_http_push_response(lua_State *L, const HttpSlot *slot) {
    LONG redirects = slot->redirect_count;
    lua_createtable(L, 0, 6);

    lua_pushinteger(L, (lua_Integer)slot->status_code);
    lua_setfield(L, -2, "status");
    if (slot->status_text[0]) {
        lua_pushstring(L, slot->status_text);
        lua_setfield(L, -2, "status_text");
    }
    lua_pushstring(L, slot->final_url[0] ? slot->final_url : "");
    lua_setfield(L, -2, "url");
    lua_pushboolean(L, redirects > 0);
    lua_setfield(L, -2, "redirected");
    lua_pushinteger(L, (lua_Integer)redirects);
    lua_setfield(L, -2, "redirect_count");

    lua_createtable(L, 0, 6);
    lua_http_set_header(L, "content-type", slot->content_type);
    lua_http_set_header(L, "content-length", slot->content_length);
    lua_http_set_header(L, "etag", slot->etag);
    lua_http_set_header(L, "last-modified", slot->last_modified);
    lua_http_set_header(L, "cache-control", slot->cache_control);
    lua_http_set_header(L, "location", slot->location);
    lua_setfield(L, -2, "headers");
}

/* mod.http.poll(handle) -> "pending" |
 *   "done", body, response |
 *   "error", message [, response] */
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
        lua_http_push_response(L, slot);
        http_release_slot(slot);
        return 3;
    }
    lua_pushstring(L, "error");
    lua_pushstring(L, slot->error_msg[0]
        ? slot->error_msg
        : "request failed");
    if (slot->status_code != 0) {
        lua_http_push_response(L, slot);
        http_release_slot(slot);
        return 3;
    }
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
