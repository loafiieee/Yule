#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "../mod_http.h"

#include <winsock2.h>
#include <windows.h>

#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lua.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

typedef struct {
    SOCKET listener;
    HANDLE thread;
    unsigned short port;
    volatile LONG stopping;
} TestServer;

typedef struct {
    SOCKET socket;
} ClientWork;

typedef struct {
    char state[16];
    char value[256];
    int result_count;
    int status;
    int redirected;
    int redirect_count;
    char url[512];
    char content_type[128];
    char etag[128];
    char cache_control[128];
} PollResult;

static int send_all(SOCKET socket, const char *data, int length) {
    int sent = 0;
    while (sent < length) {
        int amount = send(socket, data + sent, length - sent, 0);
        if (amount <= 0) return 0;
        sent += amount;
    }
    return 1;
}

static DWORD WINAPI client_thread(LPVOID opaque) {
    ClientWork *work = (ClientWork*)opaque;
    SOCKET socket = work->socket;
    char request[4096];
    char path[512] = "/";
    int received;
    free(work);

    received = recv(socket, request, (int)sizeof(request) - 1, 0);
    if (received > 0) {
        char *start;
        char *end;
        request[received] = '\0';
        start = strstr(request, "GET ");
        if (start) {
            start += 4;
            end = strchr(start, ' ');
            if (end && (size_t)(end - start) < sizeof(path)) {
                size_t length = (size_t)(end - start);
                memcpy(path, start, length);
                path[length] = '\0';
            }
        }
    }

    if (strncmp(path, "/slow", 5) == 0) {
        Sleep(350);
    }

    if (strncmp(path, "/redirect", 9) == 0) {
        const char response[] =
            "HTTP/1.1 302 Found\r\n"
            "Location: /ok\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        send_all(socket, response, (int)strlen(response));
    } else if (strncmp(path, "/missing", 8) == 0) {
        const char response[] =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Cache-Control: no-store\r\n"
            "Content-Length: 7\r\n"
            "Connection: close\r\n\r\nmissing";
        send_all(socket, response, (int)strlen(response));
    } else {
        const char response[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "ETag: \"greg-test\"\r\n"
            "Cache-Control: no-store\r\n"
            "Content-Length: 5\r\n"
            "Connection: close\r\n\r\nhello";
        send_all(socket, response, (int)strlen(response));
    }
    shutdown(socket, SD_BOTH);
    closesocket(socket);
    return 0;
}

static DWORD WINAPI server_thread(LPVOID opaque) {
    TestServer *server = (TestServer*)opaque;
    while (InterlockedCompareExchange(&server->stopping, 0, 0) == 0) {
        SOCKET client = accept(server->listener, NULL, NULL);
        if (client == INVALID_SOCKET) break;
        ClientWork *work = (ClientWork*)malloc(sizeof(*work));
        HANDLE thread;
        if (!work) {
            closesocket(client);
            continue;
        }
        work->socket = client;
        thread = CreateThread(NULL, 0, client_thread, work, 0, NULL);
        if (!thread) {
            closesocket(client);
            free(work);
        } else {
            CloseHandle(thread);
        }
    }
    return 0;
}

static int start_server(TestServer *server) {
    struct sockaddr_in address;
    int address_size = sizeof(address);
    memset(server, 0, sizeof(*server));
    server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listener == INVALID_SOCKET) return 0;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server->listener, (struct sockaddr*)&address,
             sizeof(address)) == SOCKET_ERROR ||
        listen(server->listener, SOMAXCONN) == SOCKET_ERROR ||
        getsockname(server->listener, (struct sockaddr*)&address,
                    &address_size) == SOCKET_ERROR) {
        closesocket(server->listener);
        server->listener = INVALID_SOCKET;
        return 0;
    }
    server->port = ntohs(address.sin_port);
    server->thread = CreateThread(NULL, 0, server_thread, server, 0, NULL);
    return server->thread != NULL;
}

static void stop_server(TestServer *server) {
    if (!server) return;
    InterlockedExchange(&server->stopping, 1);
    if (server->listener != INVALID_SOCKET) {
        closesocket(server->listener);
        server->listener = INVALID_SOCKET;
    }
    if (server->thread) {
        WaitForSingleObject(server->thread, 2000);
        CloseHandle(server->thread);
        server->thread = NULL;
    }
}

static void register_api(lua_State *L, const char *name, void *owner,
                         const int *enabled) {
    mod_http_lua_push_api(L, owner, enabled);
    lua_setglobal(L, name);
}

static int call_get(lua_State *L, const char *api, const char *url,
                    char *error, size_t error_capacity) {
    int handle = 0;
    lua_settop(L, 0);
    lua_getglobal(L, api);
    lua_getfield(L, -1, "get");
    lua_remove(L, -2);
    lua_pushstring(L, url);
    if (lua_pcall(L, 1, LUA_MULTRET, 0) != 0) {
        snprintf(error, error_capacity, "%s", lua_tostring(L, -1));
        return 0;
    }
    if (lua_isnumber(L, 1)) {
        handle = (int)lua_tointeger(L, 1);
    } else if (lua_isstring(L, 2)) {
        snprintf(error, error_capacity, "%s", lua_tostring(L, 2));
    }
    return handle;
}

static void read_table_string(lua_State *L, int index, const char *field,
                              char *output, size_t output_capacity) {
    const char *value;
    lua_getfield(L, index, field);
    value = lua_tostring(L, -1);
    if (value) snprintf(output, output_capacity, "%s", value);
    lua_pop(L, 1);
}

static PollResult call_poll(lua_State *L, const char *api, int handle) {
    PollResult result;
    memset(&result, 0, sizeof(result));
    lua_settop(L, 0);
    lua_getglobal(L, api);
    lua_getfield(L, -1, "poll");
    lua_remove(L, -2);
    lua_pushinteger(L, handle);
    if (lua_pcall(L, 1, LUA_MULTRET, 0) != 0) {
        snprintf(result.state, sizeof(result.state), "lua-error");
        snprintf(result.value, sizeof(result.value), "%s",
                 lua_tostring(L, -1));
        return result;
    }
    result.result_count = lua_gettop(L);
    if (lua_isstring(L, 1)) {
        snprintf(result.state, sizeof(result.state), "%s", lua_tostring(L, 1));
    }
    if (lua_isstring(L, 2)) {
        size_t length = 0;
        const char *value = lua_tolstring(L, 2, &length);
        if (value) {
            size_t copy = length < sizeof(result.value) - 1
                ? length : sizeof(result.value) - 1;
            memcpy(result.value, value, copy);
            result.value[copy] = '\0';
        }
    }
    if (result.result_count >= 3 && lua_istable(L, 3)) {
        lua_getfield(L, 3, "status");
        result.status = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "redirected");
        result.redirected = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "redirect_count");
        result.redirect_count = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        read_table_string(L, 3, "url", result.url, sizeof(result.url));
        lua_getfield(L, 3, "headers");
        if (lua_istable(L, -1)) {
            int headers = lua_gettop(L);
            read_table_string(L, headers, "content-type",
                              result.content_type,
                              sizeof(result.content_type));
            read_table_string(L, headers, "etag",
                              result.etag, sizeof(result.etag));
            read_table_string(L, headers, "cache-control",
                              result.cache_control,
                              sizeof(result.cache_control));
        }
        lua_pop(L, 1);
    }
    return result;
}

static PollResult wait_for_terminal(lua_State *L, const char *api, int handle,
                                    DWORD timeout_ms) {
    DWORD start = GetTickCount();
    PollResult result;
    do {
        result = call_poll(L, api, handle);
        if (strcmp(result.state, "pending") != 0) return result;
        Sleep(5);
    } while (GetTickCount() - start < timeout_ms);
    snprintf(result.state, sizeof(result.state), "timeout");
    return result;
}

static void call_cancel(lua_State *L, const char *api, int handle) {
    lua_settop(L, 0);
    lua_getglobal(L, api);
    lua_getfield(L, -1, "cancel");
    lua_remove(L, -2);
    lua_pushinteger(L, handle);
    CHECK(lua_pcall(L, 1, 0, 0) == 0);
}

int main(void) {
    WSADATA winsock;
    TestServer server;
    lua_State *L;
    int owner_a = 1;
    int owner_b = 2;
    int enabled_a = 1;
    int enabled_b = 1;
    char url[256];
    char error[256] = {0};
    int handle;
    int old_handle;
    PollResult result;

    CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    CHECK(start_server(&server));
    if (!server.thread) return 1;

    L = luaL_newstate();
    CHECK(L != NULL);
    if (!L) return 1;
    register_api(L, "http_a", &owner_a, &enabled_a);
    register_api(L, "http_b", &owner_b, &enabled_b);

    snprintf(url, sizeof(url), "http://127.0.0.1:%u/redirect", server.port);
    handle = call_get(L, "http_a", url, error, sizeof(error));
    CHECK(handle > 0);
    result = wait_for_terminal(L, "http_a", handle, 5000);
    CHECK(strcmp(result.state, "done") == 0);
    CHECK(strcmp(result.value, "hello") == 0);
    CHECK(result.result_count == 3);
    CHECK(result.status == 200);
    CHECK(result.redirected);
    CHECK(result.redirect_count == 1);
    CHECK(strstr(result.url, "/ok") != NULL);
    CHECK(strcmp(result.content_type, "text/plain; charset=utf-8") == 0);
    CHECK(strcmp(result.etag, "\"greg-test\"") == 0);
    CHECK(strcmp(result.cache_control, "no-store") == 0);

    snprintf(url, sizeof(url), "http://127.0.0.1:%u/missing", server.port);
    handle = call_get(L, "http_a", url, error, sizeof(error));
    CHECK(handle > 0);
    result = wait_for_terminal(L, "http_a", handle, 5000);
    CHECK(strcmp(result.state, "error") == 0);
    CHECK(strcmp(result.value, "HTTP 404") == 0);
    CHECK(result.result_count == 3);
    CHECK(result.status == 404);
    CHECK(!result.redirected);
    CHECK(strcmp(result.content_type, "text/plain") == 0);

    /* Owner isolation, cancel, and deferred reuse: a canceled worker keeps its
     * slot until it publishes completion; another owner cannot poll it. */
    {
        int handles[8];
        for (int i = 0; i < 8; i++) {
            snprintf(url, sizeof(url), "http://127.0.0.1:%u/slow?%d",
                     server.port, i);
            handles[i] = call_get(L, i < 4 ? "http_a" : "http_b",
                                  url, error, sizeof(error));
            CHECK(handles[i] > 0);
        }
        handle = call_get(L, "http_a", url, error, sizeof(error));
        CHECK(handle == 0);
        CHECK(strstr(error, "too many concurrent") != NULL);

        result = call_poll(L, "http_b", handles[0]);
        CHECK(strcmp(result.state, "error") == 0);
        CHECK(strcmp(result.value, "invalid handle") == 0);

        call_cancel(L, "http_a", handles[0]);
        mod_http_cancel_owner(&owner_a);

        /* B owns four unaffected workers and can retire them normally. */
        for (int i = 4; i < 8; i++) {
            result = wait_for_terminal(L, "http_b", handles[i], 5000);
            CHECK(strcmp(result.state, "done") == 0);
        }
        for (int spin = 0; spin < 200; spin++) {
            mod_http_pump();
            Sleep(5);
        }

        snprintf(url, sizeof(url), "http://127.0.0.1:%u/ok", server.port);
        old_handle = handles[0];
        handle = call_get(L, "http_a", url, error, sizeof(error));
        CHECK(handle > 0);
        CHECK(handle != old_handle);
        result = call_poll(L, "http_a", old_handle);
        CHECK(strcmp(result.state, "error") == 0);
        CHECK(strcmp(result.value, "invalid handle") == 0);
        result = wait_for_terminal(L, "http_a", handle, 5000);
        CHECK(strcmp(result.state, "done") == 0);
    }

    mod_http_cancel_owner(&owner_a);
    mod_http_cancel_owner(&owner_b);
    for (int spin = 0; spin < 200; spin++) {
        mod_http_pump();
        Sleep(2);
    }
    lua_close(L);
    stop_server(&server);
    WSACleanup();

    if (failures != 0) {
        fprintf(stderr, "mod.http lifecycle tests failed: %d\n", failures);
        return 1;
    }
    puts("mod.http lifecycle tests: OK");
    return 0;
}
