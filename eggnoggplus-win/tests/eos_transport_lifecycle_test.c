#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "../eos_runtime.h"
static DWORD clock_ms;
static DWORD fake_clock(void) { return clock_ms; }
#define GetTickCount fake_clock
#include "../ggpo_transport_eos.c"

static int ready, closed, removed, queue_pending, sends;
static EOS_EPacketReliability last_reliability;
static int (*carrier_service)(int);
YuleEosState yule_eos_state(void) { return ready ? YULE_EOS_CONNECT_READY : YULE_EOS_CONNECT_FAILED; }
void* yule_eos_sdk_module(void) { return NULL; }
void* yule_eos_p2p_handle(void) { return NULL; }
void* yule_eos_local_user_handle(void) { return NULL; }
void yule_eos_set_carrier_service(int (*service)(int)) { carrier_service = service; }
void yule_eos_tick(void) { if (carrier_service) carrier_service(0); }
void log_write(const char* level, const char* format, ...) { (void)level; (void)format; }

static EOS_EResult EOS_CALL mock_send(EOS_HP2P handle, const EOS_P2P_SendPacketOptions* options) {
    (void)handle;
    sends++;
    last_reliability = options->Reliability;
    assert(options->bAllowDelayedDelivery == EOS_TRUE);
    return EOS_Success;
}
static EOS_EResult EOS_CALL mock_close(EOS_HP2P handle, const EOS_P2P_CloseConnectionOptions* options) {
    (void)handle; (void)options;
    closed++;
    return EOS_Success;
}
static void EOS_CALL mock_remove(EOS_HP2P handle, EOS_NotificationId id) {
    (void)handle;
    assert(id != 0);
    removed++;
}
static EOS_EResult EOS_CALL mock_queue(EOS_HP2P handle,
    const EOS_P2P_GetPacketQueueInfoOptions* options, EOS_P2P_PacketQueueInfo* queue) {
    (void)handle; (void)options;
    memset(queue, 0, sizeof(*queue));
    queue->OutgoingPacketQueueCurrentPacketCount = queue_pending;
    return EOS_Success;
}
static GgpoTransport make_transport(void) {
    EosTransport* state = calloc(1, sizeof(*state));
    GgpoTransport transport = {GGPO_TRANSPORT_EOS_P2P, state};
    assert(state);
    state->peer.kind = GGPO_TRANSPORT_EOS_P2P;
    strcpy(state->peer.id.eos.puid, "01234567890123456789012345678901");
    strcpy(state->peer.id.eos.socket_name, "YuleTest");
    state->api.send = mock_send;
    state->api.close = mock_close;
    state->api.get_queue = mock_queue;
    state->api.remove_request = mock_remove;
    state->api.remove_established = mock_remove;
    state->api.remove_interrupted = mock_remove;
    state->api.remove_closed = mock_remove;
    state->api.remove_queue_full = mock_remove;
    state->request_id = 1; state->established_id = 2; state->interrupted_id = 3;
    state->closed_id = 4; state->queue_full_id = 5;
    yule_eos_set_carrier_service(service_closing);
    return transport;
}

int main(void) {
    GgpoTransport transport = make_transport();
    GgpoTransportPeer peer = ((EosTransport*)transport.state)->peer;
    int error;
    ready = 1;
    assert(ggpo_transport_send(&transport, "input", 5, &peer, &error) == GGPO_TRANSPORT_SEND_OK);
    assert(last_reliability == EOS_PR_UnreliableUnordered);
    assert(ggpo_transport_send_goodbye(&transport, "bye", 3, &peer, &error) == GGPO_TRANSPORT_SEND_OK);
    assert(last_reliability == EOS_PR_ReliableUnordered && sends == 2);
    queue_pending = 1;
    ggpo_transport_close_graceful(&transport);
    assert(!transport.state && g_closing_count == 1 && !closed && !removed);
    clock_ms = 99; yule_eos_tick(); assert(!closed);
    clock_ms = 500; yule_eos_tick(); assert(!closed); /* queued BYE retained */
    queue_pending = 0; yule_eos_tick();
    assert(closed == 1 && removed == 5 && !g_closing_count);
    transport = make_transport();
    ready = 0;
    assert(!ggpo_transport_connected(&transport));
    ggpo_transport_close(&transport); /* Connect failure must not prevent cleanup */
    assert(!transport.state && closed == 2 && removed == 10);
    transport = make_transport();
    queue_pending = 1;
    ggpo_transport_close_graceful(&transport);
    clock_ms = 1999; yule_eos_tick(); assert(closed == 2);
    clock_ms = 2000; yule_eos_tick(); assert(closed == 3 && !g_closing_count);
    for (int i = 0; i < 5; i++) { transport = make_transport(); ggpo_transport_close_graceful(&transport); }
    assert(g_closing_count <= 4);
    carrier_service(1);
    assert(!g_closing_count && removed == 5 * closed);
    puts("EOS teardown: auth-loss cleanup, terminal-only reliability, bounded nonblocking drain: PASS");
    return 0;
}
