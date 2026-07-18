#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "../net_ext.h"

static int g_failures = 0;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        g_failures++; \
    } \
} while (0)

typedef struct NetTestServer {
    SOCKET listener;
    const unsigned char *expected;
    int expected_length;
    volatile LONG passed;
} NetTestServer;

static DWORD WINAPI receive_server(void *opaque) {
    NetTestServer *server = (NetTestServer*)opaque;
    SOCKET client = accept(server->listener, NULL, NULL);
    unsigned char *received;
    int offset = 0;
    char ack = 'K';
    if (client == INVALID_SOCKET) return 1;
    received = (unsigned char*)malloc((size_t)server->expected_length);
    if (!received) {
        closesocket(client);
        return 1;
    }
    while (offset < server->expected_length) {
        int got = recv(client,
                       (char*)received + offset,
                       server->expected_length - offset,
                       0);
        if (got <= 0) break;
        offset += got;
    }
    if (offset == server->expected_length &&
        memcmp(received, server->expected, (size_t)server->expected_length) == 0 &&
        send(client, &ack, 1, 0) == 1) {
        InterlockedExchange(&server->passed, 1);
    }
    free(received);
    shutdown(client, SD_BOTH);
    closesocket(client);
    return 0;
}

static SOCKET make_loopback_socket(unsigned short *port, int listen_now) {
    SOCKET fd;
    struct sockaddr_in address;
    int address_length = (int)sizeof(address);
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return INVALID_SOCKET;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR ||
        getsockname(fd, (struct sockaddr*)&address, &address_length) == SOCKET_ERROR ||
        (listen_now && listen(fd, 1) == SOCKET_ERROR)) {
        closesocket(fd);
        return INVALID_SOCKET;
    }
    *port = ntohs(address.sin_port);
    return fd;
}

static int wait_connected(int slot, int *ever_connected) {
    DWORD deadline = GetTickCount() + 5000u;
    int state = 0;
    *ever_connected = 0;
    while ((LONG)(GetTickCount() - deadline) < 0) {
        state = net_check_connect(slot);
        if (state != 0) break;
        Sleep(1);
    }
    if (state == 1) *ever_connected = 1;
    return state;
}

static void test_large_copied_send(void) {
    enum { PAYLOAD_LENGTH = 100000, OVERSIZED_LENGTH = 200000 };
    unsigned char *payload = (unsigned char*)malloc(PAYLOAD_LENGTH);
    unsigned char *expected = (unsigned char*)malloc(PAYLOAD_LENGTH);
    unsigned char *oversized = (unsigned char*)malloc(OVERSIZED_LENGTH);
    unsigned short port = 0;
    NetTestServer server;
    HANDLE thread = NULL;
    int slot = -1;
    int connected = 0;
    int i;
    char ack = '\0';

    memset(&server, 0, sizeof(server));
    server.listener = INVALID_SOCKET;
    CHECK(payload != NULL && expected != NULL && oversized != NULL);
    if (!payload || !expected || !oversized) goto cleanup;
    for (i = 0; i < PAYLOAD_LENGTH; ++i) payload[i] = (unsigned char)((i * 37 + 11) & 0xff);
    memset(oversized, 0x5a, OVERSIZED_LENGTH);
    memcpy(expected, payload, PAYLOAD_LENGTH);

    server.listener = make_loopback_socket(&port, 1);
    server.expected = expected;
    server.expected_length = PAYLOAD_LENGTH;
    CHECK(server.listener != INVALID_SOCKET);
    if (server.listener == INVALID_SOCKET) goto cleanup;
    thread = CreateThread(NULL, 0, receive_server, &server, 0, NULL);
    CHECK(thread != NULL);
    if (!thread) goto cleanup;

    slot = net_connect("127.0.0.1", (int)port);
    CHECK(slot >= 0);
    if (slot < 0) goto cleanup;
    CHECK(wait_connected(slot, &connected) == 1);
    CHECK(connected == 1);
    if (!connected) goto cleanup;

    /* Backpressure is message-atomic: an item larger than the queue is refused
     * without leaking a prefix onto the wire. */
    CHECK(net_send(slot, (const char*)oversized, OVERSIZED_LENGTH) == 0);
    CHECK(net_send(slot, (const char*)payload, PAYLOAD_LENGTH) == PAYLOAD_LENGTH);
    memset(payload, 0, PAYLOAD_LENGTH); /* net_send must own a copied lifetime. */

    {
        DWORD deadline = GetTickCount() + 5000u;
        while ((LONG)(GetTickCount() - deadline) < 0) {
            int got = net_recv(slot, &ack, 1);
            if (got == 1) break;
            if (got < 0) break;
            Sleep(1);
        }
    }
    CHECK(ack == 'K');
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CHECK(InterlockedCompareExchange(&server.passed, 0, 0) == 1);

cleanup:
    if (slot >= 0) net_close(slot);
    if (thread) {
        WaitForSingleObject(thread, 1000);
        CloseHandle(thread);
    }
    if (server.listener != INVALID_SOCKET) closesocket(server.listener);
    free(oversized);
    free(expected);
    free(payload);
}

static void test_refused_connect_never_reports_success(void) {
    unsigned short port = 0;
    SOCKET reserved = make_loopback_socket(&port, 0);
    int slot;
    int ever_connected = 0;
    CHECK(reserved != INVALID_SOCKET);
    if (reserved == INVALID_SOCKET) return;
    slot = net_connect("127.0.0.1", (int)port);
    if (slot >= 0) {
        CHECK(wait_connected(slot, &ever_connected) == -1);
        CHECK(ever_connected == 0);
        net_close(slot);
    }
    closesocket(reserved);
}

int main(void) {
    WSADATA data;
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    test_large_copied_send();
    test_refused_connect_never_reports_success();
    WSACleanup();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    puts("ALL OK");
    return 0;
}
