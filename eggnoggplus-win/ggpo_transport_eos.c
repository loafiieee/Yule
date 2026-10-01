#include "ggpo_transport.h"
#include "eos_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef YULE_ENABLE_EOS

int ggpo_transport_open_eos(GgpoTransport* t, const GgpoTransportPeer* p,
                            int force, char* err, size_t cap) {
    (void)t; (void)p; (void)force;
    if (err && cap) snprintf(err, cap, "EOS SDK is not included in this build");
    return 0;
}
GgpoTransportSendResult ggpo_transport_eos_send(GgpoTransport* t,
                                                const void* data, int len,
                                                const GgpoTransportPeer* p, int* code) {
    (void)t; (void)data; (void)len; (void)p;
    if (code) *code = 0;
    return GGPO_TRANSPORT_SEND_ERROR;
}
GgpoTransportReceiveResult ggpo_transport_eos_receive(GgpoTransport* t,
                                                      void* data, int cap, int* got,
                                                      GgpoTransportPeer* p, int* code) {
    (void)t; (void)data; (void)cap; (void)p;
    if (got) *got = 0;
    if (code) *code = 0;
    return GGPO_TRANSPORT_RECEIVE_ERROR;
}
int ggpo_transport_eos_connected(const GgpoTransport* t) { (void)t; return 0; }
void ggpo_transport_eos_close(GgpoTransport* t) { if (t) t->state = NULL; }
const char* ggpo_transport_eos_route(const GgpoTransport* t) {
    (void)t; return "unknown";
}
GgpoTransportSendResult ggpo_transport_eos_send_goodbye(GgpoTransport* t,
    const void* data, int len, const GgpoTransportPeer* peer, int* code) {
    return ggpo_transport_eos_send(t, data, len, peer, code);
}
void ggpo_transport_eos_close_graceful(GgpoTransport* t) { ggpo_transport_eos_close(t); }

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "eos_sdk_loader.h"
#include "eos_sdk.h"
#include "eos_p2p.h"
#include "log.h"

typedef struct EosP2pApi {
    __typeof__(EOS_P2P_SendPacket)* send;
    __typeof__(EOS_P2P_ReceivePacket)* receive;
    __typeof__(EOS_P2P_AcceptConnection)* accept;
    __typeof__(EOS_P2P_CloseConnection)* close;
    __typeof__(EOS_P2P_SetRelayControl)* set_relay;
    __typeof__(EOS_P2P_SetPacketQueueSize)* set_queue;
    __typeof__(EOS_P2P_GetPacketQueueInfo)* get_queue;
    __typeof__(EOS_P2P_AddNotifyIncomingPacketQueueFull)* add_queue_full;
    __typeof__(EOS_P2P_RemoveNotifyIncomingPacketQueueFull)* remove_queue_full;
    __typeof__(EOS_P2P_AddNotifyPeerConnectionRequest)* add_request;
    __typeof__(EOS_P2P_RemoveNotifyPeerConnectionRequest)* remove_request;
    __typeof__(EOS_P2P_AddNotifyPeerConnectionEstablished)* add_established;
    __typeof__(EOS_P2P_RemoveNotifyPeerConnectionEstablished)* remove_established;
    __typeof__(EOS_P2P_AddNotifyPeerConnectionInterrupted)* add_interrupted;
    __typeof__(EOS_P2P_RemoveNotifyPeerConnectionInterrupted)* remove_interrupted;
    __typeof__(EOS_P2P_AddNotifyPeerConnectionClosed)* add_closed;
    __typeof__(EOS_P2P_RemoveNotifyPeerConnectionClosed)* remove_closed;
    __typeof__(EOS_ProductUserId_FromString)* puid_from_string;
    __typeof__(EOS_ProductUserId_IsValid)* puid_is_valid;
    __typeof__(EOS_ProductUserId_ToString)* puid_to_string;
} EosP2pApi;

typedef struct EosTransport {
    EOS_HP2P handle;
    EOS_ProductUserId local;
    EOS_ProductUserId remote;
    EOS_P2P_SocketId socket;
    GgpoTransportPeer peer;
    EosP2pApi api;
    EOS_NotificationId request_id;
    EOS_NotificationId established_id;
    EOS_NotificationId interrupted_id;
    EOS_NotificationId closed_id;
    EOS_NotificationId queue_full_id;
    uint32_t queue_full_events;
    uint32_t send_backpressure_events;
    int established;
    int interrupted;
    int close_reason;
    int route;
    DWORD closing_started_ms;
    struct EosTransport* closing_next;
} EosTransport;
static EosTransport* g_closing;
static unsigned int g_closing_count;
static int service_closing(int force);

static void eos_error(char* err, size_t cap, const char* message) {
    if (err && cap) snprintf(err, cap, "%s", message);
}

static int known_peer(const EosTransport* state, EOS_ProductUserId user,
                      const EOS_P2P_SocketId* socket) {
    char puid[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
    int32_t length = (int32_t)sizeof(puid);
    return state && user &&
           state->api.puid_to_string(user, puid, &length) == EOS_Success &&
           strcmp(puid, state->peer.id.eos.puid) == 0 && socket &&
           socket->ApiVersion == EOS_P2P_SOCKETID_API_LATEST &&
           strncmp(socket->SocketName, state->socket.SocketName,
                   sizeof(socket->SocketName)) == 0;
}

static void EOS_CALL on_request(const EOS_P2P_OnIncomingConnectionRequestInfo* info) {
    EosTransport* state = info ? (EosTransport*)info->ClientData : NULL;
    EOS_P2P_AcceptConnectionOptions options;
    if (!state || !known_peer(state, info->RemoteUserId, info->SocketId)) return;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    options.LocalUserId = state->local;
    options.RemoteUserId = state->remote;
    options.SocketId = &state->socket;
    (void)state->api.accept(state->handle, &options);
}

static void EOS_CALL on_established(const EOS_P2P_OnPeerConnectionEstablishedInfo* info) {
    EosTransport* state = info ? (EosTransport*)info->ClientData : NULL;
    if (!state || !known_peer(state, info->RemoteUserId, info->SocketId)) return;
    state->established = 1;
    state->interrupted = 0;
    state->close_reason = 0;
    state->route = info->NetworkType;
    LOG_INFO("ggpo.eos: peer established route=%s",
             state->route == EOS_NCT_DirectConnection ? "direct" :
             state->route == EOS_NCT_RelayedConnection ? "relay" : "unknown");
}

static void EOS_CALL on_interrupted(const EOS_P2P_OnPeerConnectionInterruptedInfo* info) {
    EosTransport* state = info ? (EosTransport*)info->ClientData : NULL;
    if (!state || !known_peer(state, info->RemoteUserId, info->SocketId)) return;
    state->interrupted = 1;
    state->established = 0;
    state->route = EOS_NCT_NoConnection;
    LOG_WARN("ggpo.eos: peer connection interrupted");
}

static void EOS_CALL on_closed(const EOS_P2P_OnRemoteConnectionClosedInfo* info) {
    EosTransport* state = info ? (EosTransport*)info->ClientData : NULL;
    if (!state || !known_peer(state, info->RemoteUserId, info->SocketId)) return;
    state->established = 0;
    state->interrupted = 0;
    state->route = EOS_NCT_NoConnection;
    state->close_reason = info->Reason;
    LOG_WARN("ggpo.eos: peer connection closed reason=%d", state->close_reason);
}

static void EOS_CALL on_queue_full(const EOS_P2P_OnIncomingPacketQueueFullInfo* info) {
    EosTransport* state = info ? (EosTransport*)info->ClientData : NULL;
    if (!state || info->OverflowPacketLocalUserId != state->local ||
        info->OverflowPacketChannel != state->peer.id.eos.channel) return;
    state->queue_full_events++;
    if (state->queue_full_events == 1 || state->queue_full_events % 100u == 0u) {
        LOG_WARN("ggpo.eos: incoming queue full events=%u bytes=%llu/%llu overflow_packet=%u",
                 state->queue_full_events,
                 (unsigned long long)info->PacketQueueCurrentSizeBytes,
                 (unsigned long long)info->PacketQueueMaxSizeBytes,
                 (unsigned)info->OverflowPacketSizeBytes);
    }
}

int ggpo_transport_open_eos(GgpoTransport* transport,
                            const GgpoTransportPeer* peer, int force_relay,
                            char* err, size_t err_cap) {
    EosTransport* state;
    HMODULE library = (HMODULE)yule_eos_sdk_module();
    EOS_P2P_SetRelayControlOptions relay;
    EOS_P2P_SetPacketQueueSizeOptions queue;
    EOS_P2P_AddNotifyPeerConnectionRequestOptions request;
    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions established;
    EOS_P2P_AddNotifyPeerConnectionInterruptedOptions interrupted;
    EOS_P2P_AddNotifyPeerConnectionClosedOptions closed;
    EOS_P2P_AddNotifyIncomingPacketQueueFullOptions queue_full;
    EOS_P2P_AcceptConnectionOptions accept;
    if (!transport || transport->state || !peer ||
        peer->kind != GGPO_TRANSPORT_EOS_P2P || !library ||
        !yule_eos_p2p_handle() || !yule_eos_local_user_handle() ||
        strlen(peer->id.eos.puid) != EOS_PRODUCTUSERID_MAX_LENGTH ||
        !peer->id.eos.socket_name[0] ||
        strlen(peer->id.eos.socket_name) > 32) {
        eos_error(err, err_cap, "EOS P2P identity or runtime is unavailable");
        return 0;
    }
    state = (EosTransport*)calloc(1, sizeof(*state));
    if (!state) { eos_error(err, err_cap, "out of memory"); return 0; }
#define LOAD(member, symbol, argument_bytes) do { \
    FARPROC proc = yule_eos_sdk_proc(library, #symbol, argument_bytes); \
    _Static_assert(sizeof(proc) == sizeof(state->api.member), "EOS function pointer size"); \
    memcpy(&state->api.member, &proc, sizeof(proc)); \
    if (!state->api.member) { eos_error(err, err_cap, "EOS P2P entry point missing"); \
        free(state); return 0; } \
} while (0)
    LOAD(send, EOS_P2P_SendPacket, 8);
    LOAD(receive, EOS_P2P_ReceivePacket, 28);
    LOAD(accept, EOS_P2P_AcceptConnection, 8);
    LOAD(close, EOS_P2P_CloseConnection, 8);
    LOAD(set_relay, EOS_P2P_SetRelayControl, 8);
    LOAD(set_queue, EOS_P2P_SetPacketQueueSize, 8);
    LOAD(get_queue, EOS_P2P_GetPacketQueueInfo, 12);
    LOAD(add_queue_full, EOS_P2P_AddNotifyIncomingPacketQueueFull, 16);
    LOAD(remove_queue_full, EOS_P2P_RemoveNotifyIncomingPacketQueueFull, 12);
    LOAD(add_request, EOS_P2P_AddNotifyPeerConnectionRequest, 16);
    LOAD(remove_request, EOS_P2P_RemoveNotifyPeerConnectionRequest, 12);
    LOAD(add_established, EOS_P2P_AddNotifyPeerConnectionEstablished, 16);
    LOAD(remove_established, EOS_P2P_RemoveNotifyPeerConnectionEstablished, 12);
    LOAD(add_interrupted, EOS_P2P_AddNotifyPeerConnectionInterrupted, 16);
    LOAD(remove_interrupted, EOS_P2P_RemoveNotifyPeerConnectionInterrupted, 12);
    LOAD(add_closed, EOS_P2P_AddNotifyPeerConnectionClosed, 16);
    LOAD(remove_closed, EOS_P2P_RemoveNotifyPeerConnectionClosed, 12);
    LOAD(puid_from_string, EOS_ProductUserId_FromString, 4);
    LOAD(puid_is_valid, EOS_ProductUserId_IsValid, 4);
    LOAD(puid_to_string, EOS_ProductUserId_ToString, 12);
#undef LOAD
    state->handle = (EOS_HP2P)yule_eos_p2p_handle();
    state->local = (EOS_ProductUserId)yule_eos_local_user_handle();
    state->remote = state->api.puid_from_string(peer->id.eos.puid);
    if (!state->remote || state->api.puid_is_valid(state->remote) != EOS_TRUE) {
        eos_error(err, err_cap, "invalid matched EOS peer PUID");
        free(state);
        return 0;
    }
    state->peer = *peer;
    state->socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    snprintf(state->socket.SocketName, sizeof(state->socket.SocketName), "%s",
             peer->id.eos.socket_name);
    memset(&relay, 0, sizeof(relay));
    relay.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
    relay.RelayControl = force_relay ? EOS_RC_ForceRelays : EOS_RC_AllowRelays;
    if (state->api.set_relay(state->handle, &relay) != EOS_Success) {
        eos_error(err, err_cap, "EOS relay policy failed");
        free(state);
        return 0;
    }
    memset(&queue, 0, sizeof(queue));
    queue.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
    queue.IncomingPacketQueueMaxSizeBytes = 2u * 1024u * 1024u;
    queue.OutgoingPacketQueueMaxSizeBytes = 2u * 1024u * 1024u;
    if (state->api.set_queue(state->handle, &queue) != EOS_Success) {
        eos_error(err, err_cap, "EOS packet queue configuration failed");
        free(state);
        return 0;
    }
    memset(&request, 0, sizeof(request));
    request.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    request.LocalUserId = state->local;
    request.SocketId = &state->socket;
    state->request_id = state->api.add_request(state->handle, &request, state, on_request);
    memset(&established, 0, sizeof(established));
    established.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
    established.LocalUserId = state->local;
    established.SocketId = &state->socket;
    state->established_id = state->api.add_established(state->handle, &established, state, on_established);
    memset(&interrupted, 0, sizeof(interrupted));
    interrupted.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST;
    interrupted.LocalUserId = state->local;
    interrupted.SocketId = &state->socket;
    state->interrupted_id = state->api.add_interrupted(state->handle, &interrupted, state, on_interrupted);
    memset(&closed, 0, sizeof(closed));
    closed.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
    closed.LocalUserId = state->local;
    closed.SocketId = &state->socket;
    state->closed_id = state->api.add_closed(state->handle, &closed, state, on_closed);
    memset(&queue_full, 0, sizeof(queue_full));
    queue_full.ApiVersion = EOS_P2P_ADDNOTIFYINCOMINGPACKETQUEUEFULL_API_LATEST;
    state->queue_full_id = state->api.add_queue_full(state->handle, &queue_full,
                                                     state, on_queue_full);
    if (!state->request_id || !state->established_id ||
        !state->interrupted_id || !state->closed_id ||
        !state->queue_full_id) {
        transport->kind = GGPO_TRANSPORT_EOS_P2P;
        transport->state = state;
        ggpo_transport_eos_close(transport);
        eos_error(err, err_cap, "EOS P2P notification setup failed");
        return 0;
    }
    memset(&accept, 0, sizeof(accept));
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = state->local;
    accept.RemoteUserId = state->remote;
    accept.SocketId = &state->socket;
    if (state->api.accept(state->handle, &accept) != EOS_Success) {
        transport->kind = GGPO_TRANSPORT_EOS_P2P;
        transport->state = state;
        ggpo_transport_eos_close(transport);
        eos_error(err, err_cap, "EOS P2P accept failed");
        return 0;
    }
    transport->kind = GGPO_TRANSPORT_EOS_P2P;
    transport->state = state;
    yule_eos_set_carrier_service(service_closing);
    LOG_INFO("ggpo.eos: matched P2P socket opened relay_policy=%s",
             force_relay ? "force" : "auto");
    return 1;
}

static GgpoTransportSendResult eos_send(GgpoTransport* transport,
                                                const void* data, int len,
                                                const GgpoTransportPeer* peer,
                                                int* os_error, int terminal) {
    EosTransport* state;
    EOS_P2P_SendPacketOptions options;
    EOS_EResult result;
    if (os_error) *os_error = 0;
    if (!transport || transport->kind != GGPO_TRANSPORT_EOS_P2P ||
        !transport->state || !data || len <= 0 || !peer ||
        !ggpo_transport_peer_equal(peer, &((EosTransport*)transport->state)->peer)) {
        return GGPO_TRANSPORT_SEND_ERROR;
    }
    if (len > EOS_P2P_MAX_PACKET_SIZE) {
        if (os_error) *os_error = EOS_LimitExceeded;
        LOG_WARN("ggpo.eos: oversized packet rejected bytes=%d", len);
        return GGPO_TRANSPORT_SEND_ERROR;
    }
    state = (EosTransport*)transport->state;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    options.LocalUserId = state->local;
    options.RemoteUserId = state->remote;
    options.SocketId = &state->socket;
    options.Channel = state->peer.id.eos.channel;
    options.DataLengthBytes = (uint32_t)len;
    options.Data = data;
    options.bAllowDelayedDelivery = EOS_TRUE;
    /* Only terminal BYE is reliable: it cannot block a future gameplay packet
     * in this session. INPUT, HELLO, state/correction and palette stay UDP-like. */
    options.Reliability = terminal ? EOS_PR_ReliableUnordered : EOS_PR_UnreliableUnordered;
    /* Auto-accept is scoped to the server-matched RemoteUserId and socket.
     * It lets EOS retry a relay negotiation after a transient close; the
     * incoming request callback still filters that peer, and Yule HMAC/replay
     * checks every payload before any session state is trusted. */
    options.bDisableAutoAcceptConnection = EOS_FALSE;
    result = state->api.send(state->handle, &options);
    if (os_error) *os_error = result;
    if (result == EOS_Success) return GGPO_TRANSPORT_SEND_OK;
    if (result == EOS_LimitExceeded || result == EOS_TooManyRequests) {
        state->send_backpressure_events++;
        if (state->send_backpressure_events == 1 ||
            state->send_backpressure_events % 100u == 0u) {
            EOS_P2P_GetPacketQueueInfoOptions query;
            EOS_P2P_PacketQueueInfo sizes;
            memset(&query, 0, sizeof(query));
            query.ApiVersion = EOS_P2P_GETPACKETQUEUEINFO_API_LATEST;
            memset(&sizes, 0, sizeof(sizes));
            if (state->api.get_queue(state->handle, &query, &sizes) == EOS_Success) {
                LOG_WARN("ggpo.eos: send backpressure events=%u queued=%llu/%llu bytes packets=%llu result=%d",
                         state->send_backpressure_events,
                         (unsigned long long)sizes.OutgoingPacketQueueCurrentSizeBytes,
                         (unsigned long long)sizes.OutgoingPacketQueueMaxSizeBytes,
                         (unsigned long long)sizes.OutgoingPacketQueueCurrentPacketCount,
                         (int)result);
            }
        }
        return GGPO_TRANSPORT_SEND_BACKPRESSURE;
    }
    LOG_WARN("ggpo.eos: send failed eos_result=%d", (int)result);
    return GGPO_TRANSPORT_SEND_ERROR;
}

GgpoTransportSendResult ggpo_transport_eos_send(GgpoTransport* transport,
    const void* data, int len, const GgpoTransportPeer* peer, int* os_error) {
    return eos_send(transport, data, len, peer, os_error, 0);
}
GgpoTransportSendResult ggpo_transport_eos_send_goodbye(GgpoTransport* transport,
    const void* data, int len, const GgpoTransportPeer* peer, int* os_error) {
    return eos_send(transport, data, len, peer, os_error, 1);
}

GgpoTransportReceiveResult ggpo_transport_eos_receive(GgpoTransport* transport,
                                                      void* data, int cap, int* got,
                                                      GgpoTransportPeer* peer,
                                                      int* os_error) {
    EosTransport* state;
    EOS_P2P_ReceivePacketOptions options;
    EOS_P2P_SocketId socket;
    EOS_ProductUserId sender = NULL;
    uint8_t channel = 0;
    uint8_t wire[EOS_P2P_MAX_PACKET_SIZE];
    uint32_t written = 0;
    EOS_EResult result;
    if (got) *got = 0;
    if (os_error) *os_error = 0;
    if (!transport || transport->kind != GGPO_TRANSPORT_EOS_P2P ||
        !transport->state || !data || cap <= 0 ||
        !got || !peer) return GGPO_TRANSPORT_RECEIVE_ERROR;
    state = (EosTransport*)transport->state;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId = state->local;
    options.MaxDataSizeBytes = (uint32_t)sizeof(wire);
    options.RequestedChannel = &state->peer.id.eos.channel;
    memset(&socket, 0, sizeof(socket));
    socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    result = state->api.receive(state->handle, &options, &sender, &socket,
                                &channel, wire, &written);
    if (result == EOS_NotFound) return GGPO_TRANSPORT_RECEIVE_EMPTY;
    if (os_error) *os_error = result;
    if (result != EOS_Success) {
        LOG_WARN("ggpo.eos: receive failed eos_result=%d", (int)result);
        return GGPO_TRANSPORT_RECEIVE_ERROR;
    }
    if (!known_peer(state, sender, &socket) ||
        channel != state->peer.id.eos.channel || !written ||
        written > (uint32_t)cap) {
        return GGPO_TRANSPORT_RECEIVE_RETRY;
    }
    memcpy(data, wire, written);
    *peer = state->peer;
    *got = (int)written;
    return GGPO_TRANSPORT_RECEIVE_PACKET;
}

int ggpo_transport_eos_connected(const GgpoTransport* transport) {
    return transport && transport->kind == GGPO_TRANSPORT_EOS_P2P &&
           transport->state && yule_eos_state() == YULE_EOS_CONNECT_READY;
}

const char* ggpo_transport_eos_route(const GgpoTransport* transport) {
    const EosTransport* state = transport && transport->kind == GGPO_TRANSPORT_EOS_P2P
        ? (const EosTransport*)transport->state : NULL;
    if (!state) return "unknown";
    if (state->interrupted) return "interrupted";
    if (state->route == EOS_NCT_DirectConnection) return "direct";
    if (state->route == EOS_NCT_RelayedConnection) return "relay";
    return state->close_reason ? "closed" : "connecting";
}

void ggpo_transport_eos_close(GgpoTransport* transport) {
    EosTransport* state = transport && transport->kind == GGPO_TRANSPORT_EOS_P2P
        ? (EosTransport*)transport->state : NULL;
    EOS_P2P_CloseConnectionOptions options;
    if (!state) return;
    if (state->request_id) state->api.remove_request(state->handle, state->request_id);
    if (state->established_id) state->api.remove_established(state->handle, state->established_id);
    if (state->interrupted_id) state->api.remove_interrupted(state->handle, state->interrupted_id);
    if (state->closed_id) state->api.remove_closed(state->handle, state->closed_id);
    if (state->queue_full_id) state->api.remove_queue_full(state->handle, state->queue_full_id);
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
    options.LocalUserId = state->local;
    options.RemoteUserId = state->remote;
    options.SocketId = &state->socket;
    (void)state->api.close(state->handle, &options);
    free(state);
    transport->state = NULL;
}

static int service_closing(int force) {
    EosTransport** cursor = &g_closing;
    while (*cursor) {
        EosTransport* state = *cursor;
        EOS_P2P_GetPacketQueueInfoOptions query = {0};
        EOS_P2P_PacketQueueInfo queue = {0};
        DWORD age = GetTickCount() - state->closing_started_ms;
        int drained;
        query.ApiVersion = EOS_P2P_GETPACKETQUEUEINFO_API_LATEST;
        drained = age >= 100u &&
            state->api.get_queue(state->handle, &query, &queue) == EOS_Success &&
            queue.OutgoingPacketQueueCurrentPacketCount == 0;
        if (force || drained || age >= 1500u) {
            GgpoTransport transport = {GGPO_TRANSPORT_EOS_P2P, state};
            *cursor = state->closing_next;
            g_closing_count--;
            ggpo_transport_eos_close(&transport);
        } else cursor = &state->closing_next;
    }
    return (int)g_closing_count;
}

void ggpo_transport_eos_close_graceful(GgpoTransport* transport) {
    EosTransport* state = transport && transport->kind == GGPO_TRANSPORT_EOS_P2P
        ? (EosTransport*)transport->state : NULL;
    if (!state) return;
    /* Bound retained resources even if callers stop/restart without ticking. */
    if (g_closing_count >= 4u) (void)service_closing(1);
    state->closing_started_ms = GetTickCount();
    state->closing_next = g_closing;
    g_closing = state;
    g_closing_count++;
    transport->state = NULL;
}

#endif
