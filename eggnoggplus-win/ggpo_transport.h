#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum GgpoTransportKind {
    GGPO_TRANSPORT_EOS_P2P = 0,
    GGPO_TRANSPORT_NATIVE_UDP = 1
} GgpoTransportKind;

/* Addressing is transport-specific. A PUID is never interpreted as an IP. */
typedef struct GgpoTransportPeer {
    GgpoTransportKind kind;
    union {
        struct {
            uint32_t ipv4_network_order;
            uint16_t port;
        } native;
        struct {
            char puid[65];
            char socket_name[33];
            uint8_t channel;
        } eos;
    } id;
} GgpoTransportPeer;

typedef struct GgpoTransport {
    GgpoTransportKind kind;
    void* state;
} GgpoTransport;

typedef enum GgpoTransportSendResult {
    GGPO_TRANSPORT_SEND_ERROR = -1,
    GGPO_TRANSPORT_SEND_BACKPRESSURE = 0,
    GGPO_TRANSPORT_SEND_OK = 1
} GgpoTransportSendResult;

typedef enum GgpoTransportReceiveResult {
    GGPO_TRANSPORT_RECEIVE_ERROR = -1,
    GGPO_TRANSPORT_RECEIVE_EMPTY = 0,
    GGPO_TRANSPORT_RECEIVE_PACKET = 1,
    GGPO_TRANSPORT_RECEIVE_RETRY = 2
} GgpoTransportReceiveResult;

int ggpo_transport_open_native(GgpoTransport* transport, uint16_t port,
                               uint16_t* bound_port, char* err, size_t err_cap);
int ggpo_transport_open_eos(GgpoTransport* transport,
                            const GgpoTransportPeer* peer, int force_relay,
                            char* err, size_t err_cap);
GgpoTransportSendResult ggpo_transport_eos_send(GgpoTransport* transport,
                                                const void* data, int len,
                                                const GgpoTransportPeer* peer,
                                                int* os_error);
GgpoTransportReceiveResult ggpo_transport_eos_receive(GgpoTransport* transport,
                                                      void* data, int cap, int* got,
                                                      GgpoTransportPeer* peer,
                                                      int* os_error);
int ggpo_transport_eos_connected(const GgpoTransport* transport);
void ggpo_transport_eos_close(GgpoTransport* transport);
const char* ggpo_transport_eos_route(const GgpoTransport* transport);
int ggpo_transport_resolve_native(const char* host, uint16_t port,
                                  GgpoTransportPeer* peer, char* err,
                                  size_t err_cap);
int ggpo_transport_add_native_candidate(GgpoTransport* transport,
                                        const GgpoTransportPeer* peer);
void ggpo_transport_clear_native_candidates(GgpoTransport* transport);
int ggpo_transport_native_candidate_count(const GgpoTransport* transport);
int ggpo_transport_native_candidate(const GgpoTransport* transport, int index,
                                    GgpoTransportPeer* out);
int ggpo_transport_native_probe_peer(GgpoTransport* transport,
                                     const char* host, uint16_t port,
                                     GgpoTransportPeer* out,
                                     char* err, size_t err_cap);
int ggpo_transport_peer_equal(const GgpoTransportPeer* a,
                              const GgpoTransportPeer* b);
uint16_t ggpo_transport_peer_native_port(const GgpoTransportPeer* peer);
const char* ggpo_transport_peer_format(const GgpoTransportPeer* peer,
                                       char* out, size_t cap);
GgpoTransportSendResult ggpo_transport_send(GgpoTransport* transport,
                                            const void* data, int len,
                                            const GgpoTransportPeer* peer,
                                            int* os_error);
GgpoTransportReceiveResult ggpo_transport_receive(GgpoTransport* transport,
                                                  void* data, int cap, int* got,
                                                  GgpoTransportPeer* peer,
                                                  int* os_error);
void ggpo_transport_service(GgpoTransport* transport);
int ggpo_transport_connected(const GgpoTransport* transport);
void ggpo_transport_close(GgpoTransport* transport);
GgpoTransportSendResult ggpo_transport_send_goodbye(GgpoTransport* transport,
    const void* data, int len, const GgpoTransportPeer* peer, int* os_error);
void ggpo_transport_close_graceful(GgpoTransport* transport);
GgpoTransportSendResult ggpo_transport_eos_send_goodbye(GgpoTransport* transport,
    const void* data, int len, const GgpoTransportPeer* peer, int* os_error);
void ggpo_transport_eos_close_graceful(GgpoTransport* transport);
