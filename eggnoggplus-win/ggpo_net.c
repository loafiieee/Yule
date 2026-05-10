#include "ggpo_net.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "ggpo_ext.h"
#include "hooks.h"
#include "log.h"
#include "lua_manager.h"

#define GGPO_NET_MAGIC 0x50474E45u
#define GGPO_NET_VERSION 1u
#define GGPO_NET_HISTORY_FRAMES 256
#define GGPO_NET_PACKET_INPUTS 24
#define GGPO_NET_PACKET_CHECKSUMS 24
#define GGPO_NET_MAX_PREDICTION 16
#define GGPO_NET_MAX_FRAME_ADVANTAGE 2
#define GGPO_NET_STATE_CHUNK_BYTES 900

#define GGPO_NET_PACKET_HELLO 1u
#define GGPO_NET_PACKET_INPUT 2u
#define GGPO_NET_PACKET_BYE   3u
#define GGPO_NET_PACKET_STATE_CHUNK 4u
#define GGPO_NET_PACKET_STATE_ACK   5u

typedef struct GgpoNetInputEntry {
    int valid;
    uint32_t frame;
    uint32_t cmd;
} GgpoNetInputEntry;

typedef struct GgpoNetHistoryEntry {
    int valid;
    uint32_t frame;
    size_t state_len;
    uint32_t pre_checksum;
    uint32_t post_checksum;
    uint32_t local_cmd;
    uint32_t remote_cmd;
    int remote_predicted;
} GgpoNetHistoryEntry;

#pragma pack(push, 1)
typedef struct GgpoNetPacketPrefix {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
} GgpoNetPacketPrefix;

typedef struct GgpoNetPacketInput {
    uint32_t frame;
    uint32_t cmd;
} GgpoNetPacketInput;

typedef struct GgpoNetPacketChecksum {
    uint32_t frame;
    uint32_t checksum;
} GgpoNetPacketChecksum;

typedef struct GgpoNetPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t session_id;
    uint32_t sender_player;
    uint32_t frame;
    uint32_t state_size;
    uint32_t state_checksum;
    uint32_t last_checksum;
    uint32_t input_count;
    uint32_t checksum_count;
    GgpoNetPacketInput inputs[GGPO_NET_PACKET_INPUTS];
    GgpoNetPacketChecksum checksums[GGPO_NET_PACKET_CHECKSUMS];
} GgpoNetPacket;

typedef struct GgpoNetStateChunkPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t session_id;
    uint32_t sender_player;
    uint32_t state_size;
    uint32_t state_checksum;
    uint32_t offset;
    uint32_t chunk_size;
    uint8_t data[GGPO_NET_STATE_CHUNK_BYTES];
} GgpoNetStateChunkPacket;
#pragma pack(pop)

typedef struct GgpoNetSession {
    int active;
    int connected;
    GgpoNetMode mode;
    int local_player;
    int remote_player;
    uint16_t local_port;
    uint16_t remote_port;
    SOCKET sock;
    struct sockaddr_in peer_addr;
    int has_peer_addr;
    uint32_t session_id;
    uint32_t frame;
    uint32_t last_checksum;
    uint32_t initial_checksum;
    uint32_t remote_frame;
    int has_remote_frame;
    size_t state_size;
    uint8_t* state_blobs;
    uint8_t* initial_state;
    uint8_t* recv_state;
    uint8_t* recv_state_seen;
    size_t initial_state_len;
    size_t recv_state_len;
    uint32_t recv_state_checksum;
    uint32_t state_send_offset;
    uint32_t state_sync_chunks_sent;
    uint32_t state_sync_chunks_received;
    int state_synced;
    int remote_state_synced;
    int state_sync_announced;
    int start_state_loaded;
    int frame0_wait_announced;
    int frame_advantage_wait_announced;
    GgpoNetHistoryEntry history[GGPO_NET_HISTORY_FRAMES];
    GgpoNetInputEntry local_inputs[GGPO_NET_HISTORY_FRAMES];
    GgpoNetInputEntry remote_inputs[GGPO_NET_HISTORY_FRAMES];
    uint32_t rollback_to;
    int rollback_pending;
    uint32_t last_remote_cmd;
    int has_last_remote_cmd;
    int warned_initial_mismatch;
    int warned_prediction_limit;
    int desync_detected;
    uint32_t desync_frame;
    uint32_t desync_local_checksum;
    uint32_t desync_remote_checksum;
    uint32_t predictions;
    uint32_t rollbacks;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t late_inputs;
    uint32_t dropped_inputs;
    uint32_t desyncs;
    uint32_t frame_advantage_stalls;
} GgpoNetSession;

static GgpoNetSession g_net;
static int g_wsa_ready = 0;

static void ggpo_net_set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg ? msg : "unknown error");
}

static int ggpo_net_ensure_wsa(char* err, size_t err_cap) {
    if (!g_wsa_ready) {
        WSADATA wd;
        int rc = WSAStartup(MAKEWORD(2, 2), &wd);
        if (rc != 0) {
            ggpo_net_set_err(err, err_cap, "WSAStartup failed");
            return 0;
        }
        g_wsa_ready = 1;
    }
    return 1;
}

static uint32_t ggpo_net_make_session_id(void) {
    uint32_t seed = 0;
    lua_manager_game_rng_seed(&seed);
    return seed ^ (uint32_t)GetTickCount() ^ (uint32_t)(uintptr_t)&g_net;
}

static int ggpo_net_make_socket(uint16_t local_port, SOCKET* out_sock, char* err, size_t err_cap) {
    SOCKET s;
    struct sockaddr_in addr;
    u_long nb = 1;
    int reuse = 1;

    if (!ggpo_net_ensure_wsa(err, err_cap)) return 0;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        ggpo_net_set_err(err, err_cap, "udp socket failed");
        return 0;
    }

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    if (ioctlsocket(s, FIONBIO, &nb) != 0) {
        closesocket(s);
        ggpo_net_set_err(err, err_cap, "nonblocking udp setup failed");
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(local_port);
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        ggpo_net_set_err(err, err_cap, "udp bind failed");
        return 0;
    }

    *out_sock = s;
    return 1;
}

static int ggpo_net_resolve_peer(const char* host, uint16_t port, struct sockaddr_in* out_addr, char* err, size_t err_cap) {
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    char port_buf[16];
    int rc;

    if (!host || !host[0]) {
        ggpo_net_set_err(err, err_cap, "missing host");
        return 0;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_buf, sizeof(port_buf), "%u", (unsigned int)port);
    rc = getaddrinfo(host, port_buf, &hints, &res);
    if (rc != 0 || !res) {
        ggpo_net_set_err(err, err_cap, "peer resolve failed");
        return 0;
    }

    memcpy(out_addr, res->ai_addr, sizeof(*out_addr));
    freeaddrinfo(res);
    return 1;
}

static int ggpo_net_addr_equal(const struct sockaddr_in* a, const struct sockaddr_in* b) {
    return a && b &&
           a->sin_family == b->sin_family &&
           a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static GgpoNetInputEntry* ggpo_net_input_slot(GgpoNetInputEntry* entries, uint32_t frame) {
    return &entries[frame % GGPO_NET_HISTORY_FRAMES];
}

static GgpoNetHistoryEntry* ggpo_net_history_slot(uint32_t frame, uint8_t** out_blob) {
    int idx = (int)(frame % GGPO_NET_HISTORY_FRAMES);
    if (out_blob) *out_blob = g_net.state_blobs + ((size_t)idx * g_net.state_size);
    return &g_net.history[idx];
}

static void ggpo_net_store_input(GgpoNetInputEntry* entries, uint32_t frame, uint32_t cmd) {
    GgpoNetInputEntry* e = ggpo_net_input_slot(entries, frame);
    e->valid = 1;
    e->frame = frame;
    e->cmd = cmd;
}

static int ggpo_net_get_input(GgpoNetInputEntry* entries, uint32_t frame, uint32_t* out_cmd) {
    GgpoNetInputEntry* e = ggpo_net_input_slot(entries, frame);
    if (!e->valid || e->frame != frame) return 0;
    if (out_cmd) *out_cmd = e->cmd;
    return 1;
}

static uint32_t ggpo_net_latch_local_input(uint32_t frame, uint32_t raw_cmd) {
    uint32_t cmd = 0;
    if (ggpo_net_get_input(g_net.local_inputs, frame, &cmd)) {
        return cmd;
    }
    ggpo_net_store_input(g_net.local_inputs, frame, raw_cmd);
    return raw_cmd;
}

static void ggpo_net_mark_desync(uint32_t frame, uint32_t local_checksum, uint32_t remote_checksum, const char* why) {
    GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (g_net.desync_detected) return;
    g_net.desync_detected = 1;
    g_net.desync_frame = frame;
    g_net.desync_local_checksum = local_checksum;
    g_net.desync_remote_checksum = remote_checksum;
    g_net.desyncs++;
    LOG_ERROR("ggpo.net: desync frame=%u local=%u remote=%u reason=%s local_cmd=0x%08X remote_cmd=0x%08X remote_predicted=%d local_frame=%u remote_frame=%u",
              (unsigned int)frame,
              (unsigned int)local_checksum,
              (unsigned int)remote_checksum,
              why ? why : "checksum mismatch",
              (h->valid && h->frame == frame) ? h->local_cmd : 0u,
              (h->valid && h->frame == frame) ? h->remote_cmd : 0u,
              (h->valid && h->frame == frame) ? h->remote_predicted : -1,
              (unsigned int)g_net.frame,
              g_net.has_remote_frame ? (unsigned int)g_net.remote_frame : 0u);
}

static void ggpo_net_note_remote_input(uint32_t frame, uint32_t cmd) {
    uint32_t old_cmd = 0;
    int had_old = ggpo_net_get_input(g_net.remote_inputs, frame, &old_cmd);
    if (had_old && old_cmd == cmd) return;

    if (had_old && frame < g_net.frame) {
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (h->valid && h->frame == frame && !h->remote_predicted) {
            ggpo_net_mark_desync(frame, h->post_checksum, 0u, "remote input changed after frame finalized");
            return;
        }
    }

    ggpo_net_store_input(g_net.remote_inputs, frame, cmd);
    g_net.last_remote_cmd = cmd;
    g_net.has_last_remote_cmd = 1;

    if (frame < g_net.frame) {
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (h->valid && h->frame == frame && h->remote_predicted) {
            if (h->remote_cmd != cmd) {
                if (!g_net.rollback_pending || frame < g_net.rollback_to) {
                    g_net.rollback_to = frame;
                    g_net.rollback_pending = 1;
                }
                g_net.late_inputs++;
            } else {
                h->remote_predicted = 0;
            }
        } else if (!h->valid || h->frame != frame) {
            g_net.dropped_inputs++;
        }
    }
}

static uint32_t ggpo_net_predict_remote(uint32_t frame, int* out_predicted) {
    uint32_t cmd = 0;
    if (ggpo_net_get_input(g_net.remote_inputs, frame, &cmd)) {
        if (out_predicted) *out_predicted = 0;
        g_net.last_remote_cmd = cmd;
        g_net.has_last_remote_cmd = 1;
        return cmd;
    }

    if (out_predicted) *out_predicted = 1;
    g_net.predictions++;

    if (g_net.has_last_remote_cmd) {
        return g_net.last_remote_cmd;
    }
    return 0u;
}

static void ggpo_net_fill_packet(GgpoNetPacket* p, uint16_t type) {
    uint32_t count = 0;
    uint32_t checksum_count = 0;
    memset(p, 0, sizeof(*p));
    p->magic = GGPO_NET_MAGIC;
    p->version = GGPO_NET_VERSION;
    p->type = type;
    p->session_id = g_net.session_id;
    p->sender_player = (uint32_t)g_net.local_player;
    p->frame = g_net.frame;
    p->state_size = (uint32_t)g_net.state_size;
    p->state_checksum = g_net.initial_checksum;
    p->last_checksum = g_net.last_checksum;

    for (uint32_t i = 0; i < GGPO_NET_HISTORY_FRAMES && count < GGPO_NET_PACKET_INPUTS; i++) {
        uint32_t frame = (g_net.frame >= i) ? (g_net.frame - i) : UINT_MAX;
        uint32_t cmd = 0;
        if (frame == UINT_MAX) break;
        if (!ggpo_net_get_input(g_net.local_inputs, frame, &cmd)) continue;
        p->inputs[count].frame = frame;
        p->inputs[count].cmd = cmd;
        count++;
    }
    p->input_count = count;

    for (uint32_t i = 1; i <= GGPO_NET_HISTORY_FRAMES && checksum_count < GGPO_NET_PACKET_CHECKSUMS; i++) {
        uint32_t frame = (g_net.frame >= i) ? (g_net.frame - i) : UINT_MAX;
        GgpoNetHistoryEntry* h = NULL;
        if (frame == UINT_MAX) break;
        h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (!h->valid || h->frame != frame || h->remote_predicted) continue;
        p->checksums[checksum_count].frame = frame;
        p->checksums[checksum_count].checksum = h->post_checksum;
        checksum_count++;
    }
    p->checksum_count = checksum_count;
}

static int ggpo_net_send_packet(uint16_t type) {
    GgpoNetPacket p;
    int sent;
    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    ggpo_net_fill_packet(&p, type);
    sent = sendto(g_net.sock,
                  (const char*)&p,
                  sizeof(p),
                  0,
                  (const struct sockaddr*)&g_net.peer_addr,
                  sizeof(g_net.peer_addr));
    if (sent == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 1;
        return 0;
    }
    g_net.packets_sent++;
    return 1;
}

static int ggpo_net_send_state_ack(void) {
    GgpoNetPacket p;
    int sent;
    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    ggpo_net_fill_packet(&p, GGPO_NET_PACKET_STATE_ACK);
    sent = sendto(g_net.sock,
                  (const char*)&p,
                  sizeof(p),
                  0,
                  (const struct sockaddr*)&g_net.peer_addr,
                  sizeof(g_net.peer_addr));
    if (sent == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 1;
        return 0;
    }
    g_net.packets_sent++;
    return 1;
}

static int ggpo_net_send_state_chunk(void) {
    GgpoNetStateChunkPacket p;
    uint32_t remaining;
    uint32_t chunk;
    int sent;

    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    if (!g_net.initial_state || g_net.initial_state_len == 0) return 0;
    if (g_net.state_send_offset >= (uint32_t)g_net.initial_state_len) {
        g_net.state_send_offset = 0;
    }

    remaining = (uint32_t)g_net.initial_state_len - g_net.state_send_offset;
    chunk = remaining > GGPO_NET_STATE_CHUNK_BYTES ? GGPO_NET_STATE_CHUNK_BYTES : remaining;

    memset(&p, 0, sizeof(p));
    p.magic = GGPO_NET_MAGIC;
    p.version = GGPO_NET_VERSION;
    p.type = GGPO_NET_PACKET_STATE_CHUNK;
    p.session_id = g_net.session_id;
    p.sender_player = (uint32_t)g_net.local_player;
    p.state_size = (uint32_t)g_net.initial_state_len;
    p.state_checksum = g_net.initial_checksum;
    p.offset = g_net.state_send_offset;
    p.chunk_size = chunk;
    memcpy(p.data, g_net.initial_state + g_net.state_send_offset, chunk);

    sent = sendto(g_net.sock,
                  (const char*)&p,
                  (int)(offsetof(GgpoNetStateChunkPacket, data) + chunk),
                  0,
                  (const struct sockaddr*)&g_net.peer_addr,
                  sizeof(g_net.peer_addr));
    if (sent == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 1;
        return 0;
    }

    g_net.packets_sent++;
    g_net.state_sync_chunks_sent++;
    g_net.state_send_offset += chunk;
    if (g_net.state_send_offset >= (uint32_t)g_net.initial_state_len) {
        g_net.state_send_offset = 0;
    }
    return 1;
}

static void ggpo_net_send_state_sync_burst(void) {
    if (g_net.mode != GGPO_NET_MODE_HOST) return;
    if (!g_net.connected || g_net.remote_state_synced) return;
    for (int i = 0; i < 8; i++) {
        if (!ggpo_net_send_state_chunk()) break;
    }
}

static void ggpo_net_clear_runtime_history(void) {
    memset(g_net.history, 0, sizeof(g_net.history));
    memset(g_net.local_inputs, 0, sizeof(g_net.local_inputs));
    memset(g_net.remote_inputs, 0, sizeof(g_net.remote_inputs));
    g_net.rollback_to = 0;
    g_net.rollback_pending = 0;
    g_net.last_remote_cmd = 0;
    g_net.has_last_remote_cmd = 0;
    g_net.remote_frame = 0;
    g_net.has_remote_frame = 0;
    g_net.frame_advantage_wait_announced = 0;
}

static void ggpo_net_reset_recv_state(void) {
    free(g_net.recv_state);
    free(g_net.recv_state_seen);
    g_net.recv_state = NULL;
    g_net.recv_state_seen = NULL;
    g_net.recv_state_len = 0;
    g_net.recv_state_checksum = 0;
    g_net.state_sync_chunks_received = 0;
}

static void ggpo_net_handle_state_chunk(const GgpoNetStateChunkPacket* p, int got_len) {
    uint32_t end;
    uint32_t seen_count = 0;
    uint32_t checksum = 0;
    char err[256];

    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->type != GGPO_NET_PACKET_STATE_CHUNK) return;
    if (g_net.mode != GGPO_NET_MODE_JOIN) return;
    if ((int)p->sender_player != g_net.remote_player) return;
    if (p->state_size == 0 || p->state_size > (uint32_t)g_net.state_size) return;
    if (p->chunk_size == 0 || p->chunk_size > GGPO_NET_STATE_CHUNK_BYTES) return;
    if (got_len < (int)(offsetof(GgpoNetStateChunkPacket, data) + p->chunk_size)) return;
    if (p->offset >= p->state_size || p->offset + p->chunk_size > p->state_size) return;

    if (!g_net.recv_state || g_net.recv_state_len != p->state_size || g_net.recv_state_checksum != p->state_checksum) {
        ggpo_net_reset_recv_state();
        g_net.recv_state = (uint8_t*)calloc(1, p->state_size);
        g_net.recv_state_seen = (uint8_t*)calloc(1, p->state_size);
        if (!g_net.recv_state || !g_net.recv_state_seen) {
            ggpo_net_reset_recv_state();
            return;
        }
        g_net.recv_state_len = p->state_size;
        g_net.recv_state_checksum = p->state_checksum;
        LOG_INFO("ggpo.net: receiving host state size=%u checksum=%u",
                 (unsigned int)p->state_size,
                 (unsigned int)p->state_checksum);
    }

    memcpy(g_net.recv_state + p->offset, p->data, p->chunk_size);
    memset(g_net.recv_state_seen + p->offset, 1, p->chunk_size);
    g_net.state_sync_chunks_received++;

    end = p->state_size;
    for (uint32_t i = 0; i < end; i++) {
        if (g_net.recv_state_seen[i]) seen_count++;
    }
    if (seen_count < end) {
        return;
    }

    err[0] = '\0';
    if (!ggpo_ext_load_game_state(g_net.recv_state, g_net.recv_state_len, err, sizeof(err))) {
        LOG_ERROR("ggpo.net: host state load failed (%s)", err[0] ? err : "unknown error");
        return;
    }
    if (!lua_manager_game_state_rollback_checksum(&checksum, err, sizeof(err))) {
        LOG_ERROR("ggpo.net: host state checksum failed (%s)", err[0] ? err : "unknown error");
        return;
    }
    if (checksum != p->state_checksum) {
        ggpo_net_mark_desync(0u, checksum, p->state_checksum, "host state transfer checksum mismatch");
        return;
    }

    memcpy(g_net.initial_state, g_net.recv_state, g_net.recv_state_len);
    memcpy(g_net.state_blobs, g_net.recv_state, g_net.recv_state_len);
    ggpo_net_clear_runtime_history();
    g_net.initial_state_len = g_net.recv_state_len;
    g_net.initial_checksum = checksum;
    g_net.last_checksum = checksum;
    g_net.frame = 0;
    g_net.state_synced = 1;
    g_net.remote_state_synced = 1;
    g_net.start_state_loaded = 0;
    LOG_INFO("ggpo.net: host state synced size=%u checksum=%u chunks=%u",
             (unsigned int)g_net.initial_state_len,
             (unsigned int)g_net.initial_checksum,
             (unsigned int)g_net.state_sync_chunks_received);
    (void)ggpo_net_send_state_ack();
    ggpo_net_reset_recv_state();
}

static void ggpo_net_handle_packet(const GgpoNetPacket* p, const struct sockaddr_in* from) {
    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->sender_player > 1u || (int)p->sender_player == g_net.local_player) return;

    if (!g_net.has_peer_addr) {
        g_net.peer_addr = *from;
        g_net.has_peer_addr = 1;
        g_net.remote_port = ntohs(from->sin_port);
    } else if (!ggpo_net_addr_equal(&g_net.peer_addr, from)) {
        return;
    }

    g_net.remote_player = (int)p->sender_player;
    if (!g_net.connected) {
        g_net.connected = 1;
        LOG_INFO("ggpo.net: connected mode=%s local_player=%d remote_player=%d peer_port=%u",
                 ggpo_net_mode_name(),
                 g_net.local_player,
                 g_net.remote_player,
                 (unsigned int)ntohs(g_net.peer_addr.sin_port));
    }

    if (p->type == GGPO_NET_PACKET_STATE_ACK) {
        if (!g_net.remote_state_synced) {
            g_net.remote_state_synced = 1;
            LOG_INFO("ggpo.net: remote state sync ack received");
        }
    }

    if (!g_net.warned_initial_mismatch &&
        (p->state_size != (uint32_t)g_net.state_size || p->state_checksum != g_net.initial_checksum)) {
        LOG_WARN("ggpo.net: initial state differs; using host state local_size=%u remote_size=%u local_checksum=%u remote_checksum=%u",
                 (unsigned int)g_net.state_size,
                 (unsigned int)p->state_size,
                 (unsigned int)g_net.initial_checksum,
                 (unsigned int)p->state_checksum);
        g_net.warned_initial_mismatch = 1;
    }

    g_net.packets_received++;
    if (!g_net.has_remote_frame || p->frame > g_net.remote_frame) {
        g_net.remote_frame = p->frame;
        g_net.has_remote_frame = 1;
    }
    for (uint32_t i = 0; i < p->input_count && i < GGPO_NET_PACKET_INPUTS; i++) {
        ggpo_net_note_remote_input(p->inputs[i].frame, p->inputs[i].cmd);
    }
    for (uint32_t i = 0; i < p->checksum_count && i < GGPO_NET_PACKET_CHECKSUMS; i++) {
        uint32_t frame = p->checksums[i].frame;
        uint32_t remote_checksum = p->checksums[i].checksum;
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (!h->valid || h->frame != frame) continue;
        if (h->remote_predicted) continue;
        if (h->post_checksum != remote_checksum) {
            ggpo_net_mark_desync(frame, h->post_checksum, remote_checksum, "confirmed frame checksum mismatch");
            break;
        }
    }
}

static void ggpo_net_poll_socket(void) {
    for (;;) {
        union {
            GgpoNetPacket normal;
            GgpoNetStateChunkPacket state_chunk;
            uint8_t bytes[sizeof(GgpoNetStateChunkPacket)];
        } packet;
        const GgpoNetPacketPrefix* prefix = (const GgpoNetPacketPrefix*)packet.bytes;
        struct sockaddr_in from;
        int from_len = sizeof(from);
        int got = recvfrom(g_net.sock, (char*)packet.bytes, sizeof(packet.bytes), 0, (struct sockaddr*)&from, &from_len);
        if (got == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return;
            return;
        }
        if (got < (int)sizeof(GgpoNetPacketPrefix)) continue;
        if (prefix->magic != GGPO_NET_MAGIC || prefix->version != GGPO_NET_VERSION) continue;
        if (prefix->type == GGPO_NET_PACKET_STATE_CHUNK) {
            if (got >= (int)offsetof(GgpoNetStateChunkPacket, data)) {
                ggpo_net_handle_state_chunk(&packet.state_chunk, got);
            }
        } else if (got >= (int)offsetof(GgpoNetPacket, inputs)) {
            ggpo_net_handle_packet(&packet.normal, &from);
        }
    }
}

static int ggpo_net_build_inputs(uint32_t frame, GgpoFrameInputs* out_inputs, int* out_remote_predicted) {
    uint32_t local_cmd = 0;
    uint32_t remote_cmd = 0;
    int predicted = 0;

    if (!ggpo_net_get_input(g_net.local_inputs, frame, &local_cmd)) {
        local_cmd = 0;
    }
    remote_cmd = ggpo_net_predict_remote(frame, &predicted);

    out_inputs->player_cmd[g_net.local_player] = local_cmd;
    out_inputs->player_cmd[g_net.remote_player] = remote_cmd;
    if (out_remote_predicted) *out_remote_predicted = predicted;
    return 1;
}

static int ggpo_net_save_pre_state(uint32_t frame, GgpoNetHistoryEntry** out_history, uint8_t** out_blob, char* err, size_t err_cap) {
    GgpoNetHistoryEntry* h = NULL;
    uint8_t* blob = NULL;
    size_t state_len = 0;
    uint32_t checksum = 0;

    h = ggpo_net_history_slot(frame, &blob);
    memset(h, 0, sizeof(*h));
    if (!ggpo_ext_save_game_state(blob, g_net.state_size, &state_len, &checksum, err, err_cap)) {
        return 0;
    }
    h->valid = 1;
    h->frame = frame;
    h->state_len = state_len;
    h->pre_checksum = checksum;
    if (out_history) *out_history = h;
    if (out_blob) *out_blob = blob;
    return 1;
}

static int ggpo_net_replay_frame(uint32_t frame, int arg0, int suppress_audio, uint32_t* out_checksum, char* err, size_t err_cap) {
    GgpoFrameInputs inputs;
    int predicted = 0;
    uint32_t checksum = 0;
    int ok = 0;

    memset(&inputs, 0, sizeof(inputs));
    ggpo_net_build_inputs(frame, &inputs, &predicted);
    if (suppress_audio) {
        int old_synth_enabled = hooks_set_native_synth_enabled(0);
        ok = ggpo_ext_advance_frame(&inputs, arg0, &checksum, err, err_cap);
        hooks_set_native_synth_enabled(old_synth_enabled);
    } else {
        ok = ggpo_ext_advance_frame(&inputs, arg0, &checksum, err, err_cap);
    }
    if (!ok) return 0;
    if (out_checksum) *out_checksum = checksum;
    return 1;
}

static int ggpo_net_load_start_state_if_ready(char* err, size_t err_cap) {
    uint32_t checksum = 0;
    if (!g_net.connected || !g_net.state_synced || !g_net.remote_state_synced) return 1;
    if (g_net.start_state_loaded) return 1;
    if (!g_net.initial_state || g_net.initial_state_len == 0) {
        ggpo_net_set_err(err, err_cap, "missing synced start state");
        return 0;
    }
    if (!ggpo_ext_load_game_state(g_net.initial_state, g_net.initial_state_len, err, err_cap)) {
        return 0;
    }
    if (!lua_manager_game_state_rollback_checksum(&checksum, err, err_cap)) {
        return 0;
    }
    if (checksum != g_net.initial_checksum) {
        ggpo_net_mark_desync(0u, checksum, g_net.initial_checksum, "synced start state reload mismatch");
        ggpo_net_set_err(err, err_cap, "synced start state reload mismatch");
        return 0;
    }
    memcpy(g_net.state_blobs, g_net.initial_state, g_net.initial_state_len);
    ggpo_net_clear_runtime_history();
    g_net.frame = 0;
    g_net.last_checksum = checksum;
    g_net.start_state_loaded = 1;
    LOG_INFO("ggpo.net: start state loaded checksum=%u", (unsigned int)checksum);
    return 1;
}

static int ggpo_net_apply_rollback_if_needed(int arg0, char* err, size_t err_cap) {
    uint32_t start;
    uint32_t end;
    GgpoNetHistoryEntry* h;
    uint8_t* blob = NULL;
    uint32_t checksum = 0;

    if (!g_net.rollback_pending) return 1;
    start = g_net.rollback_to;
    end = g_net.frame;
    h = ggpo_net_history_slot(start, &blob);
    if (!h->valid || h->frame != start || !blob) {
        g_net.rollback_pending = 0;
        g_net.dropped_inputs++;
        return 1;
    }
    if (!ggpo_ext_load_game_state(blob, h->state_len, err, err_cap)) {
        return 0;
    }

    for (uint32_t f = start; f < end; f++) {
        GgpoNetHistoryEntry* rh = NULL;
        int predicted = 0;
        GgpoFrameInputs inputs;
        uint32_t local_cmd = 0;
        uint32_t remote_cmd = 0;

        if (!ggpo_net_save_pre_state(f, &rh, NULL, err, err_cap)) {
            return 0;
        }
        memset(&inputs, 0, sizeof(inputs));
        ggpo_net_get_input(g_net.local_inputs, f, &local_cmd);
        remote_cmd = ggpo_net_predict_remote(f, &predicted);
        inputs.player_cmd[g_net.local_player] = local_cmd;
        inputs.player_cmd[g_net.remote_player] = remote_cmd;
        if (!ggpo_net_replay_frame(f, arg0, 1, &checksum, err, err_cap)) {
            return 0;
        }
        rh->local_cmd = local_cmd;
        rh->remote_cmd = remote_cmd;
        rh->remote_predicted = predicted;
        rh->post_checksum = checksum;
    }

    g_net.last_checksum = checksum;
    g_net.rollbacks++;
    LOG_INFO("ggpo.net: rollback start=%u end=%u checksum=%u total=%u",
             (unsigned int)start,
             (unsigned int)(end ? end - 1u : 0u),
             (unsigned int)checksum,
             (unsigned int)g_net.rollbacks);
    g_net.rollback_pending = 0;
    return 1;
}

int ggpo_net_active(void) {
    return g_net.active ? 1 : 0;
}

int ggpo_net_connected(void) {
    return g_net.connected ? 1 : 0;
}

GgpoNetMode ggpo_net_mode(void) {
    return g_net.mode;
}

const char* ggpo_net_mode_name(void) {
    if (g_net.mode == GGPO_NET_MODE_HOST) return "host";
    if (g_net.mode == GGPO_NET_MODE_JOIN) return "join";
    return "none";
}

int ggpo_net_local_player(void) {
    return g_net.local_player;
}

int ggpo_net_remote_player(void) {
    return g_net.remote_player;
}

uint16_t ggpo_net_local_port(void) {
    return g_net.local_port;
}

uint16_t ggpo_net_remote_port(void) {
    return g_net.remote_port;
}

void ggpo_net_stop(void) {
    if (g_net.sock != INVALID_SOCKET && g_net.sock != 0) {
        if (g_net.has_peer_addr) (void)ggpo_net_send_packet(GGPO_NET_PACKET_BYE);
        closesocket(g_net.sock);
    }
    free(g_net.state_blobs);
    free(g_net.initial_state);
    free(g_net.recv_state);
    free(g_net.recv_state_seen);
    memset(&g_net, 0, sizeof(g_net));
    g_net.sock = INVALID_SOCKET;
}

static int ggpo_net_start_common(GgpoNetMode mode, uint16_t local_port, char* err, size_t err_cap) {
    SOCKET s = INVALID_SOCKET;
    size_t state_len = 0;
    uint32_t checksum = 0;

    if (g_net.active) {
        ggpo_net_set_err(err, err_cap, "net session already active");
        return 0;
    }
    memset(&g_net, 0, sizeof(g_net));
    g_net.sock = INVALID_SOCKET;

    g_net.state_size = ggpo_ext_game_state_size();
    if (g_net.state_size == 0 || g_net.state_size > (size_t)UINT_MAX) {
        ggpo_net_set_err(err, err_cap, "game state unavailable");
        return 0;
    }
    if (!ggpo_net_make_socket(local_port, &s, err, err_cap)) {
        return 0;
    }

    g_net.state_blobs = (uint8_t*)calloc(GGPO_NET_HISTORY_FRAMES, g_net.state_size);
    if (!g_net.state_blobs) {
        closesocket(s);
        ggpo_net_set_err(err, err_cap, "out of memory");
        return 0;
    }
    g_net.initial_state = (uint8_t*)malloc(g_net.state_size);
    if (!g_net.initial_state) {
        closesocket(s);
        free(g_net.state_blobs);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        ggpo_net_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!ggpo_ext_save_game_state(g_net.state_blobs, g_net.state_size, &state_len, &checksum, err, err_cap)) {
        closesocket(s);
        free(g_net.state_blobs);
        free(g_net.initial_state);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        return 0;
    }
    memcpy(g_net.initial_state, g_net.state_blobs, state_len);
    g_net.initial_state_len = state_len;

    g_net.active = 1;
    g_net.mode = mode;
    g_net.local_player = (mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    g_net.remote_player = (mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.local_port = local_port;
    g_net.sock = s;
    g_net.session_id = ggpo_net_make_session_id();
    g_net.initial_checksum = checksum;
    g_net.last_checksum = checksum;
    g_net.state_synced = (mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.remote_state_synced = (mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    return 1;
}

int ggpo_net_start_host(uint16_t local_port, char* err, size_t err_cap) {
    if (local_port == 0) local_port = GGPO_NET_DEFAULT_PORT;
    if (!ggpo_net_start_common(GGPO_NET_MODE_HOST, local_port, err, err_cap)) {
        return 0;
    }
    LOG_INFO("ggpo.net: hosting udp port=%u state_size=%u checksum=%u",
             (unsigned int)local_port,
             (unsigned int)g_net.state_size,
             (unsigned int)g_net.initial_checksum);
    return 1;
}

int ggpo_net_start_join(const char* host, uint16_t remote_port, uint16_t local_port, char* err, size_t err_cap) {
    if (remote_port == 0) remote_port = GGPO_NET_DEFAULT_PORT;
    if (!ggpo_net_start_common(GGPO_NET_MODE_JOIN, local_port, err, err_cap)) {
        return 0;
    }
    if (!ggpo_net_resolve_peer(host, remote_port, &g_net.peer_addr, err, err_cap)) {
        ggpo_net_stop();
        return 0;
    }
    g_net.has_peer_addr = 1;
    g_net.remote_port = remote_port;
    LOG_INFO("ggpo.net: joining %s:%u local_port=%u state_size=%u checksum=%u",
             host ? host : "",
             (unsigned int)remote_port,
             (unsigned int)local_port,
             (unsigned int)g_net.state_size,
             (unsigned int)g_net.initial_checksum);
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_HELLO);
    return 1;
}

int ggpo_net_advance(uint32_t raw_p0,
                     uint32_t raw_p1,
                     int arg0,
                     uint32_t* out_checksum,
                     int* out_advanced,
                     char* err,
                     size_t err_cap) {
    uint32_t local_cmd = 0;
    uint32_t remote_cmd = 0;
    int predicted = 0;
    GgpoNetHistoryEntry* h = NULL;
    uint32_t checksum = 0;

    if (out_advanced) *out_advanced = 0;
    if (!g_net.active) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }

    ggpo_net_poll_socket();
    (void)ggpo_net_send_packet(g_net.connected ? GGPO_NET_PACKET_INPUT : GGPO_NET_PACKET_HELLO);
    ggpo_net_send_state_sync_burst();

    if (g_net.desync_detected) {
        char detail[192];
        snprintf(detail,
                 sizeof(detail),
                 "desync frame=%u local=%u remote=%u",
                 (unsigned int)g_net.desync_frame,
                 (unsigned int)g_net.desync_local_checksum,
                 (unsigned int)g_net.desync_remote_checksum);
        ggpo_net_set_err(err, err_cap, detail);
        return 0;
    }

    if (g_net.connected && (!g_net.state_synced || !g_net.remote_state_synced)) {
        if (!g_net.state_sync_announced) {
            LOG_INFO("ggpo.net: waiting for host state sync local_synced=%d remote_synced=%d",
                     g_net.state_synced,
                     g_net.remote_state_synced);
            g_net.state_sync_announced = 1;
        }
        if (g_net.mode == GGPO_NET_MODE_JOIN && g_net.state_synced) {
            (void)ggpo_net_send_state_ack();
        }
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (!g_net.connected) {
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (!ggpo_net_load_start_state_if_ready(err, err_cap)) {
        return 0;
    }

    local_cmd = ggpo_net_latch_local_input(g_net.frame, (g_net.local_player == 0) ? raw_p0 : raw_p1);
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);

    if (g_net.frame == 0) {
        uint32_t tmp_remote = 0;
        if (!ggpo_net_get_input(g_net.remote_inputs, 0, &tmp_remote)) {
            if (!g_net.frame0_wait_announced) {
                LOG_INFO("ggpo.net: waiting for remote frame 0 input");
                g_net.frame0_wait_announced = 1;
            }
            if (out_checksum) *out_checksum = g_net.last_checksum;
            return 1;
        }
    }

    if (!ggpo_net_apply_rollback_if_needed(arg0, err, err_cap)) {
        return 0;
    }

    if (g_net.has_remote_frame && g_net.frame > g_net.remote_frame + GGPO_NET_MAX_FRAME_ADVANTAGE) {
        g_net.frame_advantage_stalls++;
        if (!g_net.frame_advantage_wait_announced) {
            LOG_INFO("ggpo.net: throttling local frame=%u remote_frame=%u max_advantage=%u",
                     (unsigned int)g_net.frame,
                     (unsigned int)g_net.remote_frame,
                     (unsigned int)GGPO_NET_MAX_FRAME_ADVANTAGE);
            g_net.frame_advantage_wait_announced = 1;
        }
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }
    g_net.frame_advantage_wait_announced = 0;

    if (g_net.desync_detected) {
        char detail[192];
        snprintf(detail,
                 sizeof(detail),
                 "desync frame=%u local=%u remote=%u",
                 (unsigned int)g_net.desync_frame,
                 (unsigned int)g_net.desync_local_checksum,
                 (unsigned int)g_net.desync_remote_checksum);
        ggpo_net_set_err(err, err_cap, detail);
        return 0;
    }

    remote_cmd = ggpo_net_predict_remote(g_net.frame, &predicted);
    if (predicted && !g_net.warned_prediction_limit) {
        uint32_t oldest_missing = g_net.frame;
        for (uint32_t f = (g_net.frame > GGPO_NET_MAX_PREDICTION) ? g_net.frame - GGPO_NET_MAX_PREDICTION : 0; f <= g_net.frame; f++) {
            uint32_t tmp = 0;
            if (!ggpo_net_get_input(g_net.remote_inputs, f, &tmp)) {
                oldest_missing = f;
                break;
            }
        }
        if (g_net.frame - oldest_missing >= GGPO_NET_MAX_PREDICTION) {
            LOG_WARN("ggpo.net: predicting remote input for %u+ frames (oldest_missing=%u current=%u)",
                     (unsigned int)GGPO_NET_MAX_PREDICTION,
                     (unsigned int)oldest_missing,
                     (unsigned int)g_net.frame);
            g_net.warned_prediction_limit = 1;
        }
    }

    if (!ggpo_net_save_pre_state(g_net.frame, &h, NULL, err, err_cap)) {
        return 0;
    }

    {
        GgpoFrameInputs inputs;
        memset(&inputs, 0, sizeof(inputs));
        inputs.player_cmd[g_net.local_player] = local_cmd;
        inputs.player_cmd[g_net.remote_player] = remote_cmd;
        if (!ggpo_ext_advance_frame(&inputs, arg0, &checksum, err, err_cap)) {
            return 0;
        }
    }

    h->local_cmd = local_cmd;
    h->remote_cmd = remote_cmd;
    h->remote_predicted = predicted;
    h->post_checksum = checksum;

    g_net.last_checksum = checksum;
    g_net.frame++;
    if (out_checksum) *out_checksum = checksum;
    if (out_advanced) *out_advanced = 1;
    return 1;
}

uint32_t ggpo_net_frame_count(void) {
    return g_net.frame;
}

uint32_t ggpo_net_remote_frame_count(void) {
    return g_net.has_remote_frame ? g_net.remote_frame : 0u;
}

uint32_t ggpo_net_last_checksum(void) {
    return g_net.last_checksum;
}

size_t ggpo_net_state_size(void) {
    return g_net.state_size;
}

uint32_t ggpo_net_prediction_count(void) {
    return g_net.predictions;
}

uint32_t ggpo_net_rollback_count(void) {
    return g_net.rollbacks;
}

uint32_t ggpo_net_packets_sent(void) {
    return g_net.packets_sent;
}

uint32_t ggpo_net_packets_received(void) {
    return g_net.packets_received;
}

uint32_t ggpo_net_late_input_count(void) {
    return g_net.late_inputs;
}

uint32_t ggpo_net_dropped_input_count(void) {
    return g_net.dropped_inputs;
}

uint32_t ggpo_net_frame_advantage_stall_count(void) {
    return g_net.frame_advantage_stalls;
}

uint32_t ggpo_net_desync_count(void) {
    return g_net.desyncs;
}

uint32_t ggpo_net_desync_frame(void) {
    return g_net.desync_frame;
}

uint32_t ggpo_net_desync_local_checksum(void) {
    return g_net.desync_local_checksum;
}

uint32_t ggpo_net_desync_remote_checksum(void) {
    return g_net.desync_remote_checksum;
}
