/* net_ext.c – non-blocking TCP client for eggnogg-online mod.
   Compiled as part of SDL2.dll (the mod proxy DLL).
   Link with -lws2_32. */

#include "net_ext.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

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

typedef struct NetUdpProbe {
    SOCKET fd;
    struct sockaddr_in target;
    uint32_t sequence;
    DWORD started_ms;
    DWORD deadline_ms;
    int active;
} NetUdpProbe;

static NetUdpProbe g_udp_probe;

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

static int net_ipv4_is_cgnat(uint32_t network_order) {
    uint32_t value = ntohl(network_order);
    return (value & UINT32_C(0xffc00000)) == UINT32_C(0x64400000);
}

static int net_ipv4_is_private(uint32_t network_order) {
    uint32_t value = ntohl(network_order);
    return (value & UINT32_C(0xff000000)) == UINT32_C(0x0a000000) ||
           (value & UINT32_C(0xfff00000)) == UINT32_C(0xac100000) ||
           (value & UINT32_C(0xffff0000)) == UINT32_C(0xc0a80000) ||
           net_ipv4_is_cgnat(network_order);
}

static void net_adapter_name(char *out, size_t cap,
                             const IP_ADAPTER_ADDRESSES *adapter) {
    const wchar_t *wide;
    int wrote;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!adapter) return;
    wide = (adapter->FriendlyName && adapter->FriendlyName[0])
        ? adapter->FriendlyName
        : adapter->Description;
    if (!wide || !wide[0]) return;
    wrote = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)cap,
                                NULL, NULL);
    if (wrote <= 0) out[0] = '\0';
    out[cap - 1] = '\0';
}

static int net_adapter_looks_vpn(const IP_ADAPTER_ADDRESSES *adapter) {
    static const char *const words[] = {
        "vpn", "wireguard", "wintun", "openvpn", "tap-windows",
        "tailscale", "zerotier", "hamachi", "nordlynx", "mullvad",
        "proton", "surfshark", "expressvpn", "cloudflare warp"
    };
    char name[256];
    size_t i;
    if (!adapter) return 0;
    if (adapter->IfType == IF_TYPE_TUNNEL ||
        adapter->IfType == IF_TYPE_PPP) {
        return 1;
    }
    net_adapter_name(name, sizeof(name), adapter);
    for (i = 0; name[i]; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x80) name[i] = (char)tolower(ch);
    }
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (strstr(name, words[i])) return 1;
    }
    return 0;
}

static ULONG net_target_interface_index(const char *host) {
    struct addrinfo hints;
    struct addrinfo *resolved = NULL;
    ULONG index = 0;
    DWORD status;
    if (!host || !host[0]) return 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(host, NULL, &hints, &resolved) != 0 ||
        !resolved || !resolved->ai_addr ||
        resolved->ai_addrlen < (int)sizeof(struct sockaddr_in)) {
        if (resolved) freeaddrinfo(resolved);
        return 0;
    }
    status = GetBestInterface(
        ((const struct sockaddr_in*)resolved->ai_addr)->sin_addr.s_addr,
        &index);
    freeaddrinfo(resolved);
    return status == NO_ERROR ? index : 0;
}

static int net_adapter_better(const IP_ADAPTER_ADDRESSES *adapter,
                              int has_gateway,
                              const IP_ADAPTER_ADDRESSES *current,
                              int current_has_gateway,
                              ULONG current_metric) {
    if (!current) return 1;
    if (has_gateway != current_has_gateway) {
        return has_gateway > current_has_gateway;
    }
    return adapter->Ipv4Metric < current_metric;
}

int net_network_profile(const char *target_host, NetNetworkProfile *out) {
    IP_ADAPTER_ADDRESSES *adapters = NULL;
    IP_ADAPTER_ADDRESSES *adapter;
    IP_ADAPTER_ADDRESSES *route_adapter = NULL;
    struct sockaddr_in *route_address = NULL;
    IP_ADAPTER_ADDRESSES *best_physical = NULL;
    struct sockaddr_in *best_physical_address = NULL;
    IP_ADAPTER_ADDRESSES *best_any = NULL;
    struct sockaddr_in *best_any_address = NULL;
    ULONG size = 16384;
    ULONG status;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG route_index;
    ULONG best_physical_metric = ULONG_MAX;
    ULONG best_any_metric = ULONG_MAX;
    int best_physical_has_gateway = 0;
    int best_any_has_gateway = 0;
    IP_ADAPTER_ADDRESSES *best;
    struct sockaddr_in *best_address;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    route_index = net_target_interface_index(target_host);
    adapters = (IP_ADAPTER_ADDRESSES*)malloc(size);
    if (!adapters) return 0;
    status = GetAdaptersAddresses(AF_INET, flags, NULL, adapters, &size);
    if (status == ERROR_BUFFER_OVERFLOW) {
        IP_ADAPTER_ADDRESSES *larger =
            (IP_ADAPTER_ADDRESSES*)realloc(adapters, size);
        if (!larger) {
            free(adapters);
            return 0;
        }
        adapters = larger;
        status = GetAdaptersAddresses(AF_INET, flags, NULL, adapters, &size);
    }
    if (status != NO_ERROR) {
        free(adapters);
        return 0;
    }
    for (adapter = adapters; adapter; adapter = adapter->Next) {
        IP_ADAPTER_UNICAST_ADDRESS *unicast;
        struct sockaddr_in *candidate = NULL;
        int has_gateway;
        int looks_vpn;
        if (adapter->OperStatus != IfOperStatusUp ||
            adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (unicast = adapter->FirstUnicastAddress;
             unicast;
             unicast = unicast->Next) {
            struct sockaddr_in *address;
            uint32_t host;
            if (!unicast->Address.lpSockaddr ||
                unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            address = (struct sockaddr_in*)unicast->Address.lpSockaddr;
            host = ntohl(address->sin_addr.s_addr);
            if ((host >> 24) == 127u || host == 0u) continue;
            candidate = address;
            break;
        }
        if (!candidate) continue;
        out->active_ipv4_adapters++;
        looks_vpn = net_adapter_looks_vpn(adapter);
        if (looks_vpn) out->vpn_suspected = 1;
        has_gateway = adapter->FirstGatewayAddress != NULL;
        if (route_index != 0 && adapter->IfIndex == route_index) {
            route_adapter = adapter;
            route_address = candidate;
        }
        if (!looks_vpn &&
            net_adapter_better(adapter, has_gateway,
                               best_physical,
                               best_physical_has_gateway,
                               best_physical_metric)) {
            best_physical = adapter;
            best_physical_address = candidate;
            best_physical_metric = adapter->Ipv4Metric;
            best_physical_has_gateway = has_gateway;
        }
        if (net_adapter_better(adapter, has_gateway,
                               best_any,
                               best_any_has_gateway,
                               best_any_metric)) {
            best_any = adapter;
            best_any_address = candidate;
            best_any_metric = adapter->Ipv4Metric;
            best_any_has_gateway = has_gateway;
        }
    }
    best = route_adapter ? route_adapter :
           best_physical ? best_physical : best_any;
    best_address = route_address ? route_address :
                   best_physical ? best_physical_address : best_any_address;
    if (!best || !best_address) {
        free(adapters);
        return 0;
    }
    (void)inet_ntop(AF_INET, &best_address->sin_addr,
                    out->primary_ipv4,
                    (socklen_t)sizeof(out->primary_ipv4));
    net_adapter_name(out->primary_adapter,
                     sizeof(out->primary_adapter), best);
    out->route_matched = route_adapter != NULL;
    out->primary_vpn_suspected = net_adapter_looks_vpn(best);
    out->primary_is_cgnat = net_ipv4_is_cgnat(best_address->sin_addr.s_addr);
    out->primary_is_private = net_ipv4_is_private(best_address->sin_addr.s_addr);
    free(adapters);
    return out->primary_ipv4[0] != '\0';
}

static void net_probe_error(char *out, size_t cap, const char *message,
                            int code) {
    if (!out || cap == 0) return;
    if (code != 0) {
        snprintf(out, cap, "%s (Winsock %d)", message, code);
    } else {
        snprintf(out, cap, "%s", message);
    }
    out[cap - 1] = '\0';
}

void net_udp_probe_cancel(void) {
    if (g_udp_probe.active && g_udp_probe.fd != INVALID_SOCKET) {
        closesocket(g_udp_probe.fd);
    }
    memset(&g_udp_probe, 0, sizeof(g_udp_probe));
    g_udp_probe.fd = INVALID_SOCKET;
}

int net_udp_probe_active(void) {
    return g_udp_probe.active ? 1 : 0;
}

int net_udp_probe_start(const char *host, uint16_t port, uint32_t sequence,
                        uint32_t timeout_ms, char *error, size_t error_cap) {
    struct addrinfo hints;
    struct addrinfo *resolved = NULL;
    char port_text[16];
    char request[96];
    int request_len;
    u_long nonblocking = 1;
    int sent;
    if (error && error_cap) error[0] = '\0';
    if (!host || !host[0] || port == 0) {
        net_probe_error(error, error_cap, "invalid UDP probe target", 0);
        return 0;
    }
    if (timeout_ms < 250u) timeout_ms = 250u;
    if (timeout_ms > 10000u) timeout_ms = 10000u;
    net_udp_probe_cancel();
    ensure_wsa();
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);
    if (getaddrinfo(host, port_text, &hints, &resolved) != 0 ||
        !resolved || !resolved->ai_addr ||
        resolved->ai_addrlen < (int)sizeof(struct sockaddr_in)) {
        if (resolved) freeaddrinfo(resolved);
        net_probe_error(error, error_cap,
                        "server hostname did not resolve to IPv4", 0);
        return 0;
    }
    g_udp_probe.fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_probe.fd == INVALID_SOCKET) {
        int code = WSAGetLastError();
        freeaddrinfo(resolved);
        net_probe_error(error, error_cap, "could not create UDP probe socket",
                        code);
        return 0;
    }
    memcpy(&g_udp_probe.target, resolved->ai_addr,
           sizeof(g_udp_probe.target));
    freeaddrinfo(resolved);
    if (ioctlsocket(g_udp_probe.fd, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        int code = WSAGetLastError();
        closesocket(g_udp_probe.fd);
        g_udp_probe.fd = INVALID_SOCKET;
        net_probe_error(error, error_cap,
                        "could not make UDP probe non-blocking", code);
        return 0;
    }
    request_len = snprintf(request, sizeof(request),
                           "{\"type\":\"udp_ping\",\"seq\":%u}",
                           (unsigned int)sequence);
    if (request_len <= 0 || request_len >= (int)sizeof(request)) {
        closesocket(g_udp_probe.fd);
        g_udp_probe.fd = INVALID_SOCKET;
        net_probe_error(error, error_cap, "could not encode UDP probe", 0);
        return 0;
    }
    sent = sendto(g_udp_probe.fd, request, request_len, 0,
                  (const struct sockaddr*)&g_udp_probe.target,
                  (int)sizeof(g_udp_probe.target));
    if (sent != request_len) {
        int code = sent == SOCKET_ERROR ? WSAGetLastError() : 0;
        closesocket(g_udp_probe.fd);
        g_udp_probe.fd = INVALID_SOCKET;
        net_probe_error(error, error_cap, "could not send UDP probe", code);
        return 0;
    }
    g_udp_probe.sequence = sequence;
    g_udp_probe.started_ms = GetTickCount();
    g_udp_probe.deadline_ms = g_udp_probe.started_ms + timeout_ms;
    g_udp_probe.active = 1;
    return 1;
}

static int net_probe_json_uint(const char *json, const char *key,
                               uint32_t *out) {
    const char *found;
    char *end;
    unsigned long value;
    found = strstr(json, key);
    if (!found) return 0;
    found += strlen(key);
    value = strtoul(found, &end, 10);
    if (end == found || value > UINT32_MAX) return 0;
    if (out) *out = (uint32_t)value;
    return 1;
}

static int net_probe_json_host(const char *json, char *out, size_t cap) {
    const char *key = "\"observed_host\":\"";
    const char *found = strstr(json, key);
    size_t len = 0;
    if (!found || !out || cap == 0) return 0;
    found += strlen(key);
    while (found[len] && found[len] != '"' && len + 1 < cap) {
        unsigned char ch = (unsigned char)found[len];
        if (!(isdigit(ch) || ch == '.' || ch == ':')) return 0;
        len++;
    }
    if (len == 0 || found[len] != '"') return 0;
    memcpy(out, found, len);
    out[len] = '\0';
    return 1;
}

int net_udp_probe_poll(NetUdpProbeResult *out, char *error,
                       size_t error_cap) {
    char response[2049];
    struct sockaddr_in source;
    int source_len;
    int received;
    DWORD current;
    if (error && error_cap) error[0] = '\0';
    if (!g_udp_probe.active || g_udp_probe.fd == INVALID_SOCKET) {
        net_probe_error(error, error_cap, "no UDP probe is active", 0);
        return -1;
    }
    for (;;) {
        uint32_t sequence = 0;
        uint32_t observed_port = 0;
        NetUdpProbeResult result;
        source_len = (int)sizeof(source);
        received = recvfrom(g_udp_probe.fd, response,
                            (int)sizeof(response) - 1, 0,
                            (struct sockaddr*)&source, &source_len);
        if (received == SOCKET_ERROR) {
            int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK) break;
            net_udp_probe_cancel();
            net_probe_error(error, error_cap, "UDP receive failed", code);
            return -1;
        }
        if (received <= 0 ||
            source.sin_addr.s_addr != g_udp_probe.target.sin_addr.s_addr ||
            source.sin_port != g_udp_probe.target.sin_port) {
            continue;
        }
        response[received] = '\0';
        memset(&result, 0, sizeof(result));
        if (!strstr(response, "\"type\":\"udp_pong\"") ||
            !net_probe_json_uint(response, "\"seq\":", &sequence) ||
            sequence != g_udp_probe.sequence ||
            !net_probe_json_host(response, result.observed_host,
                                 sizeof(result.observed_host)) ||
            !net_probe_json_uint(response, "\"observed_port\":",
                                 &observed_port) ||
            observed_port == 0 || observed_port > 65535u) {
            continue;
        }
        result.observed_port = (uint16_t)observed_port;
        result.round_trip_ms = GetTickCount() - g_udp_probe.started_ms;
        net_udp_probe_cancel();
        if (out) *out = result;
        return 1;
    }
    current = GetTickCount();
    if ((LONG)(current - g_udp_probe.deadline_ms) >= 0) {
        net_udp_probe_cancel();
        net_probe_error(error, error_cap,
                        "no UDP reply arrived before the timeout", 0);
        return -1;
    }
    return 0;
}
