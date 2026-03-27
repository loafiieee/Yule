/* net_ext.c – non-blocking TCP client for eggnogg-online mod.
   Compiled as part of SDL2.dll (the mod proxy DLL).
   Link with -lws2_32. */

#include "net_ext.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <stdio.h>

#define NET_MAX_CONN 4

typedef struct {
    SOCKET fd;
    int    used;
    int    connecting; /* async connect in progress */
    int    connected;
} NetConn;

static NetConn g_net[NET_MAX_CONN];
static int     g_wsa_ready = 0;

static void ensure_wsa(void) {
    if (!g_wsa_ready) {
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        g_wsa_ready = 1;
    }
}

int net_connect(const char *host, int port) {
    ensure_wsa();

    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < NET_MAX_CONN; i++) {
        if (!g_net[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1; /* all slots in use */

    /* Resolve host */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    SOCKET fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd == INVALID_SOCKET) { freeaddrinfo(res); return -1; }

    /* Switch to non-blocking mode before connect so we never stall the game */
    u_long nb = 1;
    ioctlsocket(fd, FIONBIO, &nb);

    int cr = connect(fd, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);

    if (cr == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) {
            closesocket(fd);
            return -1;
        }
    }

    NetConn *c   = &g_net[slot];
    c->fd         = fd;
    c->used       = 1;
    c->connecting = (cr == SOCKET_ERROR) ? 1 : 0;
    c->connected  = (cr == 0)            ? 1 : 0;
    return slot;
}

int net_check_connect(int slot) {
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].used) return -1;
    NetConn *c = &g_net[slot];
    if (c->connected)  return 1;
    if (!c->connecting) return -1;

    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_SET(c->fd, &wfds);
    FD_ZERO(&efds); FD_SET(c->fd, &efds);
    struct timeval tv = {0, 0}; /* non-blocking poll */
    if (select(0, NULL, &wfds, &efds, &tv) <= 0) return 0;

    if (FD_ISSET(c->fd, &efds)) {
        net_close(slot);
        return -1;
    }
    if (FD_ISSET(c->fd, &wfds)) {
        c->connecting = 0;
        c->connected  = 1;
        return 1;
    }
    return 0;
}

int net_send(int slot, const char *data, int len) {
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].connected) return -1;
    int r = send(g_net[slot].fd, data, len, 0);
    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
        net_close(slot);
        return -1;
    }
    return r;
}

int net_recv(int slot, char *buf, int maxlen) {
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].connected) return -1;
    int r = recv(g_net[slot].fd, buf, maxlen, 0);
    if (r == 0) { net_close(slot); return -1; } /* clean close */
    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
        net_close(slot);
        return -1;
    }
    return r;
}

int net_connected(int slot) {
    return (slot >= 0 && slot < NET_MAX_CONN &&
            g_net[slot].used && g_net[slot].connected) ? 1 : 0;
}

int net_connecting(int slot) {
    return (slot >= 0 && slot < NET_MAX_CONN &&
            g_net[slot].used && g_net[slot].connecting) ? 1 : 0;
}

void net_close(int slot) {
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].used) return;
    closesocket(g_net[slot].fd);
    memset(&g_net[slot], 0, sizeof(g_net[slot]));
}
