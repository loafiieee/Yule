#include "ggpo_transport.h"
#include "eos_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct GgpoNativeTransport {
    SOCKET socket;
    GgpoTransportPeer candidates[6];
    int candidate_count;
    GgpoTransportPeer probe_peer;
    char probe_host[128];
    uint16_t probe_port;
    int has_probe_peer;
} GgpoNativeTransport;

static int g_native_wsa_ready;

static void native_error(char* err, size_t cap, const char* message) {
    if (err && cap) snprintf(err, cap, "%s", message);
}

static void native_sockaddr(const GgpoTransportPeer* peer,
                            struct sockaddr_in* addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = peer->id.native.ipv4_network_order;
    addr->sin_port = htons(peer->id.native.port);
}

int ggpo_transport_open_native(GgpoTransport* transport, uint16_t port,
                               uint16_t* bound_port, char* err, size_t err_cap) {
    GgpoNativeTransport* native;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    u_long nonblocking = 1;
    int reuse = 1;
    if (!transport || transport->state) return 0;
    if (!g_native_wsa_ready) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            native_error(err, err_cap, "WSAStartup failed");
            return 0;
        }
        g_native_wsa_ready = 1;
    }
    native = (GgpoNativeTransport*)calloc(1, sizeof(*native));
    if (!native) {
        native_error(err, err_cap, "out of memory");
        return 0;
    }
    native->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (native->socket == INVALID_SOCKET) {
        native_error(err, err_cap, "udp socket failed");
        free(native);
        return 0;
    }
    setsockopt(native->socket, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&reuse, sizeof(reuse));
    if (ioctlsocket(native->socket, FIONBIO, &nonblocking) != 0) {
        native_error(err, err_cap, "nonblocking udp setup failed");
        closesocket(native->socket);
        free(native);
        return 0;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(native->socket, (const struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        native_error(err, err_cap, "udp bind failed");
        closesocket(native->socket);
        free(native);
        return 0;
    }
    if (getsockname(native->socket, (struct sockaddr*)&addr, &addr_len) == 0 && bound_port) {
        *bound_port = ntohs(addr.sin_port);
    }
    transport->kind = GGPO_TRANSPORT_NATIVE_UDP;
    transport->state = native;
    return 1;
}

int ggpo_transport_resolve_native(const char* host, uint16_t port,
                                  GgpoTransportPeer* peer, char* err,
                                  size_t err_cap) {
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    char port_text[16];
    if (!host || !host[0] || !peer) {
        native_error(err, err_cap, "missing host");
        return 0;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);
    if (getaddrinfo(host, port_text, &hints, &result) != 0 || !result) {
        native_error(err, err_cap, "peer resolve failed");
        return 0;
    }
    memset(peer, 0, sizeof(*peer));
    peer->kind = GGPO_TRANSPORT_NATIVE_UDP;
    peer->id.native.ipv4_network_order =
        ((const struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
    peer->id.native.port = port;
    freeaddrinfo(result);
    return 1;
}

int ggpo_transport_add_native_candidate(GgpoTransport* transport,
                                        const GgpoTransportPeer* peer) {
    GgpoNativeTransport* native;
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state || !peer || peer->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        peer->id.native.port == 0) return 0;
    native = (GgpoNativeTransport*)transport->state;
    for (int i = 0; i < native->candidate_count; i++) {
        if (ggpo_transport_peer_equal(&native->candidates[i], peer)) return 1;
    }
    if (native->candidate_count >= (int)(sizeof(native->candidates) /
                                         sizeof(native->candidates[0]))) return 0;
    native->candidates[native->candidate_count++] = *peer;
    return 1;
}

void ggpo_transport_clear_native_candidates(GgpoTransport* transport) {
    GgpoNativeTransport* native;
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state) return;
    native = (GgpoNativeTransport*)transport->state;
    memset(native->candidates, 0, sizeof(native->candidates));
    native->candidate_count = 0;
}

int ggpo_transport_native_candidate_count(const GgpoTransport* transport) {
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state) return 0;
    return ((const GgpoNativeTransport*)transport->state)->candidate_count;
}

int ggpo_transport_native_candidate(const GgpoTransport* transport, int index,
                                    GgpoTransportPeer* out) {
    const GgpoNativeTransport* native;
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state || !out) return 0;
    native = (const GgpoNativeTransport*)transport->state;
    if (index < 0 || index >= native->candidate_count) return 0;
    *out = native->candidates[index];
    return 1;
}

int ggpo_transport_native_probe_peer(GgpoTransport* transport,
                                     const char* host, uint16_t port,
                                     GgpoTransportPeer* out,
                                     char* err, size_t err_cap) {
    GgpoNativeTransport* native;
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state || !host || !host[0] || !port || !out) return 0;
    native = (GgpoNativeTransport*)transport->state;
    if (!native->has_probe_peer || native->probe_port != port ||
        strcmp(native->probe_host, host) != 0) {
        if (!ggpo_transport_resolve_native(host, port, &native->probe_peer,
                                           err, err_cap)) return 0;
        snprintf(native->probe_host, sizeof(native->probe_host), "%s", host);
        native->probe_port = port;
        native->has_probe_peer = 1;
    }
    *out = native->probe_peer;
    return 1;
}

int ggpo_transport_peer_equal(const GgpoTransportPeer* a,
                              const GgpoTransportPeer* b) {
    if (!a || !b || a->kind != b->kind) return 0;
    if (a->kind == GGPO_TRANSPORT_NATIVE_UDP) {
        return a->id.native.ipv4_network_order == b->id.native.ipv4_network_order &&
               a->id.native.port == b->id.native.port;
    }
    return a->id.eos.channel == b->id.eos.channel &&
           strcmp(a->id.eos.puid, b->id.eos.puid) == 0 &&
           strcmp(a->id.eos.socket_name, b->id.eos.socket_name) == 0;
}

uint16_t ggpo_transport_peer_native_port(const GgpoTransportPeer* peer) {
    return peer && peer->kind == GGPO_TRANSPORT_NATIVE_UDP
        ? peer->id.native.port : 0;
}

const char* ggpo_transport_peer_format(const GgpoTransportPeer* peer,
                                       char* out, size_t cap) {
    struct in_addr ip;
    char ip_text[INET_ADDRSTRLEN];
    if (!out || !cap) return "";
    if (!peer) {
        snprintf(out, cap, "none");
    } else if (peer->kind == GGPO_TRANSPORT_NATIVE_UDP) {
        ip.s_addr = peer->id.native.ipv4_network_order;
        if (!inet_ntop(AF_INET, &ip, ip_text, sizeof(ip_text))) {
            snprintf(out, cap, "invalid native peer");
        } else {
            snprintf(out, cap, "%s:%u", ip_text, (unsigned int)peer->id.native.port);
        }
    } else {
        snprintf(out, cap, "EOS:%s/%s:%u", peer->id.eos.puid,
                 peer->id.eos.socket_name, (unsigned int)peer->id.eos.channel);
    }
    return out;
}

GgpoTransportSendResult ggpo_transport_send(GgpoTransport* transport,
                                            const void* data, int len,
                                            const GgpoTransportPeer* peer,
                                            int* os_error) {
    GgpoNativeTransport* native;
    struct sockaddr_in addr;
    int sent;
    if (os_error) *os_error = 0;
    if (transport && transport->kind == GGPO_TRANSPORT_EOS_P2P) {
        return ggpo_transport_eos_send(transport, data, len, peer, os_error);
    }
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state || !data || len <= 0 || !peer ||
        peer->kind != GGPO_TRANSPORT_NATIVE_UDP) return GGPO_TRANSPORT_SEND_ERROR;
    native = (GgpoNativeTransport*)transport->state;
    native_sockaddr(peer, &addr);
    sent = sendto(native->socket, (const char*)data, len, 0,
                  (const struct sockaddr*)&addr, sizeof(addr));
    if (sent == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (os_error) *os_error = code;
        if (code == WSAEWOULDBLOCK || code == WSAENOBUFS) {
            return GGPO_TRANSPORT_SEND_BACKPRESSURE;
        }
        return GGPO_TRANSPORT_SEND_ERROR;
    }
    if (sent != len) {
        if (os_error) *os_error = WSAEMSGSIZE;
        return GGPO_TRANSPORT_SEND_ERROR;
    }
    return GGPO_TRANSPORT_SEND_OK;
}

GgpoTransportReceiveResult ggpo_transport_receive(GgpoTransport* transport,
                                                  void* data, int cap, int* got,
                                                  GgpoTransportPeer* peer,
                                                  int* os_error) {
    GgpoNativeTransport* native;
    struct sockaddr_in from;
    int from_len = sizeof(from);
    int count;
    if (got) *got = 0;
    if (os_error) *os_error = 0;
    if (transport && transport->kind == GGPO_TRANSPORT_EOS_P2P) {
        return ggpo_transport_eos_receive(transport, data, cap, got, peer, os_error);
    }
    if (!transport || transport->kind != GGPO_TRANSPORT_NATIVE_UDP ||
        !transport->state || !data || cap <= 0 || !got || !peer) {
        return GGPO_TRANSPORT_RECEIVE_ERROR;
    }
    native = (GgpoNativeTransport*)transport->state;
    count = recvfrom(native->socket, (char*)data, cap, 0,
                     (struct sockaddr*)&from, &from_len);
    if (count == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (os_error) *os_error = code;
        if (code == WSAEWOULDBLOCK) return GGPO_TRANSPORT_RECEIVE_EMPTY;
        if (code == WSAEMSGSIZE || code == WSAECONNRESET) {
            return GGPO_TRANSPORT_RECEIVE_RETRY;
        }
        return GGPO_TRANSPORT_RECEIVE_ERROR;
    }
    memset(peer, 0, sizeof(*peer));
    peer->kind = GGPO_TRANSPORT_NATIVE_UDP;
    peer->id.native.ipv4_network_order = from.sin_addr.s_addr;
    peer->id.native.port = ntohs(from.sin_port);
    *got = count;
    return GGPO_TRANSPORT_RECEIVE_PACKET;
}

void ggpo_transport_service(GgpoTransport* transport) {
    /* Backend progress belongs to the transport service boundary. Gameplay,
     * prematch and standalone GGPO callers must all keep EOS alive. This is
     * never called from the deterministic replay callback. */
    if (transport && transport->state &&
        transport->kind == GGPO_TRANSPORT_EOS_P2P) yule_eos_tick();
}

int ggpo_transport_connected(const GgpoTransport* transport) {
    if (transport && transport->kind == GGPO_TRANSPORT_EOS_P2P) {
        return ggpo_transport_eos_connected(transport);
    }
    return transport && transport->state != NULL;
}

void ggpo_transport_close(GgpoTransport* transport) {
    if (!transport || !transport->state) return;
    if (transport->kind == GGPO_TRANSPORT_EOS_P2P) {
        ggpo_transport_eos_close(transport);
        return;
    }
    if (transport->kind == GGPO_TRANSPORT_NATIVE_UDP) {
        GgpoNativeTransport* native = (GgpoNativeTransport*)transport->state;
        closesocket(native->socket);
        free(native);
    }
    transport->state = NULL;
}

GgpoTransportSendResult ggpo_transport_send_goodbye(GgpoTransport* transport,
    const void* data, int len, const GgpoTransportPeer* peer, int* os_error) {
    if (transport && transport->kind == GGPO_TRANSPORT_EOS_P2P)
        return ggpo_transport_eos_send_goodbye(transport, data, len, peer, os_error);
    return ggpo_transport_send(transport, data, len, peer, os_error);
}

void ggpo_transport_close_graceful(GgpoTransport* transport) {
    if (transport && transport->kind == GGPO_TRANSPORT_EOS_P2P)
        ggpo_transport_eos_close_graceful(transport);
    else ggpo_transport_close(transport);
}
