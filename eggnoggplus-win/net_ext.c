/* net_ext.c – non-blocking TCP client for eggnogg-online mod.
   Compiled as part of SDL2.dll (the mod proxy DLL).
   Link with -lws2_32. */

#include "net_ext.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>

#define NET_MAX_CONN 4
#define NET_SEND_QUEUE_CAPACITY (128 * 1024)

typedef struct {
    SOCKET fd;
    int    used;
    int    connecting; /* async connect in progress */
    int    connected;
    int    send_offset;
    int    send_length;
    char   send_queue[NET_SEND_QUEUE_CAPACITY];
} NetConn;

static NetConn g_net[NET_MAX_CONN];
static int     g_wsa_ready = 0;

/* Drain as much copied output as Winsock currently accepts.  Keeping the
 * bytes in our own bounded queue makes one logical net_send() atomic from the
 * caller's perspective: a short non-blocking send can no longer truncate a
 * JSON line after the caller's temporary buffer has gone out of scope. */
static int net_flush_send_queue(int slot) {
    NetConn *c;
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].connected) return -1;
    c = &g_net[slot];
    while (c->send_length > 0) {
        int r = send(c->fd, c->send_queue + c->send_offset, c->send_length, 0);
        if (r == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) return 1;
            closesocket(c->fd);
            SecureZeroMemory(c, sizeof(*c));
            return -1;
        }
        if (r <= 0) return 1;
        c->send_offset += r;
        c->send_length -= r;
    }
    c->send_offset = 0;
    return 1;
}

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

    if (FD_ISSET(c->fd, &efds) || FD_ISSET(c->fd, &wfds)) {
        int socket_error = 0;
        int socket_error_len = (int)sizeof(socket_error);
        if (getsockopt(c->fd,
                       SOL_SOCKET,
                       SO_ERROR,
                       (char*)&socket_error,
                       &socket_error_len) == SOCKET_ERROR ||
            socket_error != 0) {
            net_close(slot);
            return -1;
        }
        c->connecting = 0;
        c->connected  = 1;
        return 1;
    }
    return 0;
}

int net_send(int slot, const char *data, int len) {
    NetConn *c;
    int free_space;
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].connected) return -1;
    if (len < 0 || (len > 0 && !data)) return -1;
    if (len == 0) return 0;
    if (net_flush_send_queue(slot) < 0) return -1;
    c = &g_net[slot];

    /* Compact only when needed; this keeps the common path free of memmove. */
    free_space = NET_SEND_QUEUE_CAPACITY - (c->send_offset + c->send_length);
    if (free_space < len && c->send_offset > 0) {
        memmove(c->send_queue,
                c->send_queue + c->send_offset,
                (size_t)c->send_length);
        c->send_offset = 0;
        free_space = NET_SEND_QUEUE_CAPACITY - c->send_length;
    }
    /* A logical message is accepted in full or not at all.  In particular,
     * online_server_send_raw() must never report success for a partial line. */
    if (free_space < len) return 0;
    memcpy(c->send_queue + c->send_offset + c->send_length, data, (size_t)len);
    c->send_length += len;
    if (net_flush_send_queue(slot) < 0) return -1;
    return len;
}

int net_recv(int slot, char *buf, int maxlen) {
    if (slot < 0 || slot >= NET_MAX_CONN || !g_net[slot].connected) return -1;
    if (net_flush_send_queue(slot) < 0) return -1;
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
    /* The copied queue may still contain authentication JSON. Ensure teardown
     * is not optimized away as a dead ordinary memset. */
    SecureZeroMemory(&g_net[slot], sizeof(g_net[slot]));
}

int net_local_ipv4(char *buf, int buflen) {
    char host[256];
    struct addrinfo hints, *res = NULL;
    int ok = 0;
    if (!buf || buflen <= 0) return 0;
    buf[0] = '\0';
    ensure_wsa();
    if (gethostname(host, sizeof(host)) != 0) return 0;
    host[sizeof(host) - 1] = '\0';
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        struct sockaddr_in *addr;
        const unsigned char *ip;
        if (!it->ai_addr || it->ai_addrlen < (int)sizeof(struct sockaddr_in)) continue;
        addr = (struct sockaddr_in*)it->ai_addr;
        ip = (const unsigned char*)&addr->sin_addr.s_addr;
        if (ip[0] == 127 || ip[0] == 0) continue;
        if (inet_ntop(AF_INET, &addr->sin_addr, buf, (socklen_t)buflen)) {
            ok = 1;
            break;
        }
    }
    if (!ok && res && res->ai_addr) {
        struct sockaddr_in *addr = (struct sockaddr_in*)res->ai_addr;
        if (inet_ntop(AF_INET, &addr->sin_addr, buf, (socklen_t)buflen)) ok = 1;
    }
    freeaddrinfo(res);
    return ok;
}
