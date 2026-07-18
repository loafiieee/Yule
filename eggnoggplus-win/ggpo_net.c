#include "ggpo_net.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>   /* _controlfp - log FP precision/rounding mode per peer */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include "ggpo_ext.h"
#include "hooks.h"
#include "log.h"
#include "lua_manager.h"

#define GGPO_NET_MAGIC 0x50474E45u
#define GGPO_NET_VERSION GGPO_NET_PROTOCOL_VERSION
#define GGPO_NET_HISTORY_FRAMES 512
#define GGPO_NET_PACKET_INPUTS 64
#define GGPO_NET_PACKET_CHECKSUMS 32
/* Kept small on purpose: the per-frame packet is a FIXED sizeof(GgpoNetPacket)
 * sent ~60x/sec, and each summary is ~148 bytes. Keeping the whole packet under
 * the ~1472-byte UDP/MTU payload avoids IP fragmentation, which on real networks
 * (unlike loopback) turns one dropped fragment into a lost packet -> prediction
 * stalls -> "slow/laggy" online play. Summaries are only used for desync DETAIL
 * (diagnostic); detection uses the 4-byte checksums, so 2 is plenty. */
#define GGPO_NET_PACKET_SUMMARIES 2
#define GGPO_NET_DEFAULT_MAX_PREDICTION 24
#define GGPO_NET_DEFAULT_MAX_FRAME_ADVANTAGE 20
#define GGPO_NET_DEFAULT_INPUT_DELAY 1
/* Extra frames of one-shot input delay added on top of the measured one-way
 * latency, to absorb RTT jitter so the peer's input usually arrives before we
 * simulate its frame (fewer mispredictions -> fewer prediction stalls/rollbacks
 * -> less slow-mo and jitter on jittery internet links). */
#define GGPO_NET_AUTO_DELAY_JITTER_MARGIN 2u
#define GGPO_NET_TIMEOUT_TICKS 600
#define GGPO_NET_CORRECTION_TIMEOUT_TICKS 2400
#define GGPO_NET_MAX_BLOCK_TICKS 60
#define GGPO_NET_CORRECTION_BURST_CHUNKS 32
#define GGPO_NET_RESYNC_REQUEST_INTERVAL_TICKS 30
#define GGPO_NET_STATE_CHUNK_BYTES 900
#define GGPO_NET_PUNCH_HELLO_BURST 8
#define GGPO_NET_PUNCH_HELLO_INTERVAL_TICKS 3
#define GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES 900
#define GGPO_NET_COSMETIC_ASSET_BURST_CHUNKS 8
#define GGPO_NET_SIM_QUEUE_PACKETS 512
#define GGPO_NET_COSMETIC_SYNC_WAIT_TICKS 300
#define GGPO_NET_ENABLE_COSMETICS 0
#define GGPO_NET_INITIAL_STATE_EPOCH 1u
#define GGPO_NET_AUTH_KEY_BYTES 32u
#define GGPO_NET_AUTH_TAG_BYTES 16u
#define GGPO_NET_REPLAY_WINDOW 4096u

#define GGPO_NET_PACKET_HELLO 1u
#define GGPO_NET_PACKET_INPUT 2u
#define GGPO_NET_PACKET_BYE   3u
#define GGPO_NET_PACKET_STATE_CHUNK 4u
#define GGPO_NET_PACKET_STATE_ACK   5u
#define GGPO_NET_PACKET_RESYNC_REQUEST 6u
#define GGPO_NET_PACKET_COSMETICS 7u
#define GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK 8u

#define GGPO_NET_STATE_FLAG_CORRECTION 1u
#define GGPO_NET_STATE_FLAG_DELTA      2u

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
    int has_summary;
    LuaGameStateRollbackSummary summary;
} GgpoNetHistoryEntry;

#pragma pack(push, 1)
typedef struct GgpoNetPacketPrefix {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t session_id;
    uint32_t sender_player;
    uint64_t auth_sequence;
    uint8_t auth_tag[GGPO_NET_AUTH_TAG_BYTES];
} GgpoNetPacketPrefix;

typedef struct GgpoNetPacketInput {
    uint32_t frame;
    uint32_t cmd;
} GgpoNetPacketInput;

typedef struct GgpoNetPacketChecksum {
    uint32_t frame;
    uint32_t checksum;
} GgpoNetPacketChecksum;

typedef struct GgpoNetPacketStateSummary {
    uint32_t frame;
    LuaGameStateRollbackSummary summary;
} GgpoNetPacketStateSummary;

typedef struct GgpoNetPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t session_id;
    uint32_t sender_player;
    uint64_t auth_sequence;
    uint8_t auth_tag[GGPO_NET_AUTH_TAG_BYTES];
    uint64_t session_echo; /* peer session most recently seen by the sender */
    uint64_t confirmed_session_echo; /* peer session this sender has confirmed */
    uint32_t build_id;
    uint32_t exe_id;
    uint32_t dll_id;
    uint32_t correction_ack_checksum;
    uint32_t correction_id;
    uint32_t correction_ack_id;
    uint32_t correction_request_frame;
    /* The host increments state_epoch whenever it publishes a new authoritative
     * frame-0 state. A join peer echoes the epoch it actually applied. This
     * prevents delayed countdown packets from being accepted after release. */
    uint32_t state_epoch;
    uint32_t prematch_hold;
    uint32_t hold_epoch;
    uint32_t frame;
    uint32_t state_size;
    uint32_t state_checksum;
    uint32_t last_checksum;
    uint32_t input_count;
    uint32_t checksum_count;
    uint32_t summary_count;
    uint32_t send_tick;   /* sender's service tick when this packet was built */
    uint32_t tick_echo;   /* most recent send_tick the sender has seen from us */
    GgpoNetPacketInput inputs[GGPO_NET_PACKET_INPUTS];
    GgpoNetPacketChecksum checksums[GGPO_NET_PACKET_CHECKSUMS];
    GgpoNetPacketStateSummary summaries[GGPO_NET_PACKET_SUMMARIES];
} GgpoNetPacket;

typedef struct GgpoNetStateChunkPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t session_id;
    uint32_t sender_player;
    uint64_t auth_sequence;
    uint8_t auth_tag[GGPO_NET_AUTH_TAG_BYTES];
    uint32_t state_epoch;
    uint32_t build_id;
    uint32_t exe_id;
    uint32_t dll_id;
    uint32_t frame;
    uint32_t flags;
    uint32_t correction_id;
    uint32_t base_checksum;
    uint32_t full_chunk_count;
    uint32_t chunk_index;
    uint32_t chunk_count;
    uint32_t state_size;
    uint32_t state_checksum;
    uint32_t offset;
    uint32_t chunk_size;
    uint8_t data[GGPO_NET_STATE_CHUNK_BYTES];
} GgpoNetStateChunkPacket;

typedef struct GgpoNetCosmeticPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t session_id;
    uint32_t sender_player;
    uint64_t auth_sequence;
    uint8_t auth_tag[GGPO_NET_AUTH_TAG_BYTES];
    uint32_t build_id;
    uint32_t exe_id;
    uint32_t dll_id;
    uint32_t profile_revision;
    uint32_t asset_ack_revision;
    uint32_t profile_len;
    char profile[GGPO_NET_COSMETIC_PROFILE_BYTES];
} GgpoNetCosmeticPacket;

typedef struct GgpoNetCosmeticAssetChunkPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t session_id;
    uint32_t sender_player;
    uint64_t auth_sequence;
    uint8_t auth_tag[GGPO_NET_AUTH_TAG_BYTES];
    uint32_t build_id;
    uint32_t exe_id;
    uint32_t dll_id;
    uint32_t asset_revision;
    uint32_t asset_len;
    uint32_t chunk_index;
    uint32_t chunk_count;
    uint32_t chunk_size;
    char asset_id[GGPO_NET_COSMETIC_ASSET_ID_BYTES];
    uint8_t data[GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES];
} GgpoNetCosmeticAssetChunkPacket;
#pragma pack(pop)

#define GGPO_NET_MAX2(a, b) ((sizeof(a) > sizeof(b)) ? sizeof(a) : sizeof(b))
#define GGPO_NET_MAX_PACKET_BYTES \
    (GGPO_NET_MAX2(GgpoNetPacket, GgpoNetStateChunkPacket) > GGPO_NET_MAX2(GgpoNetCosmeticPacket, GgpoNetCosmeticAssetChunkPacket) ? \
        GGPO_NET_MAX2(GgpoNetPacket, GgpoNetStateChunkPacket) : \
        GGPO_NET_MAX2(GgpoNetCosmeticPacket, GgpoNetCosmeticAssetChunkPacket))

/* Keep the fixed 60 Hz input packet comfortably below the typical 1472-byte
 * IPv4 UDP payload. A protocol edit that crosses this boundary must fail the
 * build instead of silently introducing fragmentation on real networks. */
typedef char GgpoNetInputPacketMustFitMtu[(sizeof(GgpoNetPacket) <= 1400u) ? 1 : -1];

typedef struct GgpoNetQueuedPacket {
    int valid;
    uint32_t send_tick;
    int len;
    struct sockaddr_in addr;
    uint8_t bytes[GGPO_NET_MAX_PACKET_BYTES];
} GgpoNetQueuedPacket;

typedef struct GgpoNetSession {
    int active;
    int connected;          /* valid peer traffic received (one-way/raw) */
    int session_confirmed;  /* peer echoed this socket attempt's session id */
    int peer_confirmed_session; /* peer reported confirming our current session */
    GgpoNetMode mode;
    int local_player;
    int remote_player;
    uint16_t local_port;
    uint16_t remote_port;
    SOCKET sock;
    struct sockaddr_in peer_addr;
    int has_peer_addr;
    /* ICE-style hole-punch candidates (LAN + public endpoints). Until connected we
     * send handshake HELLOs to ALL of them; whichever address actually replies is
     * adopted as peer_addr (ggpo_net_accept_packet_source). This is what makes
     * cross-network play work: the server hands us both the peer's LAN address
     * (works same-LAN) and its public/NAT address (works cross-network). */
    struct sockaddr_in candidates[6];
    int candidate_count;
    struct sockaddr_in probe_server_addr;
    int has_probe_server_addr;
    char probe_server_host[128];
    uint16_t probe_server_port;
    uint64_t session_id;
    uint64_t remote_session_id;
    int has_remote_session_id;
    uint64_t retired_remote_session_ids[4];
    int retired_remote_session_count;
    /* Every peer datagram is HMAC authenticated before endpoint/session state is
     * consulted. The receive window accepts bounded UDP reordering once and
     * rejects duplicates or packets replayed from older attempts. */
    uint8_t auth_root_key[GGPO_NET_AUTH_KEY_BYTES];
    uint8_t auth_send_key[GGPO_NET_AUTH_KEY_BYTES];
    uint8_t auth_remote_key[GGPO_NET_AUTH_KEY_BYTES];
    int auth_ready;
    int auth_remote_key_valid;
    uint64_t auth_remote_key_session;
    uint32_t auth_remote_key_player;
    uint64_t auth_send_sequence;
    uint64_t auth_rx_session;
    uint64_t auth_rx_highest_sequence;
    uint64_t auth_rx_seen[GGPO_NET_REPLAY_WINDOW];
    uint32_t auth_rejected_packets;
    uint32_t service_tick;
    uint32_t last_rx_tick;
    uint32_t last_handshake_burst_tick;
    uint32_t alternate_endpoint_updates;
    /* Prematch transport lifecycle. While prematch_hold is set, socket
     * servicing and handshake/RTT traffic continue, but state and gameplay
     * traffic are quarantined. */
    int prematch_hold;
    int remote_prematch_hold;
    int remote_prematch_hold_known;
    int prematch_used;
    uint32_t hold_epoch;
    uint32_t remote_hold_epoch;
    uint32_t state_epoch;
    uint32_t remote_state_epoch_seen;
    uint32_t hold_remote_epoch_floor;
    uint32_t held_state_chunks_dropped;
    uint32_t stale_state_chunks_dropped;
    uint32_t held_control_packets_dropped;
    uint32_t frame;
    uint32_t last_checksum;
    uint32_t initial_checksum;
    uint32_t remote_frame;
    int has_remote_frame;
    size_t state_size;
    uint8_t* state_blobs;
    uint8_t* initial_state;
    uint8_t* correction_state;
    uint8_t* correction_base_state;
    uint8_t* recv_state;
    uint8_t* recv_state_chunks_seen;
    size_t initial_state_len;
    size_t correction_state_len;
    size_t correction_base_state_len;
    size_t recv_state_len;
    uint32_t recv_state_checksum;
    uint32_t recv_state_frame;
    uint32_t recv_state_flags;
    uint32_t recv_state_epoch;
    uint32_t recv_state_id;
    uint32_t recv_state_base_checksum;
    uint32_t recv_state_seen_chunk_count;
    uint32_t recv_state_chunk_count;
    uint32_t recv_state_chunks_complete;
    uint32_t state_send_offset;
    uint32_t correction_send_offset;
    uint32_t correction_send_next_chunk;
    uint32_t correction_send_chunk_count;
    uint32_t correction_send_base_checksum;
    uint32_t state_sync_chunks_sent;
    uint32_t state_sync_chunks_received;
    uint32_t correction_chunks_sent;
    uint32_t correction_chunks_received;
    uint32_t correction_id;
    uint32_t correction_frame;
    uint32_t correction_checksum;
    uint32_t correction_request_frame;
    uint32_t correction_wait_start_tick;
    uint32_t correction_base_checksum;
    uint32_t last_correction_ack_checksum;
    uint32_t last_correction_ack_id;
    uint32_t last_correction_applied_checksum;
    uint32_t last_correction_applied_id;
    uint32_t last_resync_request_tick;
    int correction_active;
    int correction_send_delta;
    int awaiting_correction;
    int correction_wait_cap_announced;
    int correction_enabled;
    int state_synced;
    int remote_state_synced;
    int state_sync_announced;
    int start_state_loaded;
    int frame0_wait_announced;
    int frame_advantage_wait_announced;
    int prediction_limit_wait_announced;
    int frame_advantage_wait_cap_announced;
    int prediction_wait_cap_announced;
    int peer_disconnected;
    uint32_t input_delay;
    uint32_t max_frame_advantage;
    uint32_t max_prediction;
    /* RTT estimation (in service ticks ~= frames) for adaptive input delay.
     * peer_last_send_tick is the newest send_tick we have seen from the peer,
     * which we echo back so the peer can compute its own round trip. */
    uint32_t peer_last_send_tick;
    uint32_t rtt_ema_ticks;
    uint32_t rtt_last_ticks;
    uint32_t rtt_sample_count;
    int auto_input_delay_applied;
    GgpoNetHistoryEntry history[GGPO_NET_HISTORY_FRAMES];
    GgpoNetInputEntry local_inputs[GGPO_NET_HISTORY_FRAMES];
    GgpoNetInputEntry remote_inputs[GGPO_NET_HISTORY_FRAMES];
    uint32_t rollback_to;
    int rollback_pending;
    /* Sim-only gameplay seed: snapshot of _mrand_seed taken right after each
     * confirmed tick (and after rollback replay). Restored before the next tick's
     * pre-state save so out-of-tick draws (render between ticks, round/stage
     * transitions) can't drift the seed between peers. */
    uint32_t clean_mrand_seed;
    float clean_camera_x;
    float clean_camera_y;
    int have_clean_mrand_seed;
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
    uint32_t corrections_sent;
    uint32_t corrections_received;
    uint32_t correction_requests;
    uint32_t stale_correction_requests;
    uint32_t duplicate_state_chunks;
    uint32_t correction_delta_chunks_sent;
    uint32_t correction_delta_chunks_received;
    uint32_t correction_full_chunks_sent;
    uint32_t correction_full_chunks_received;
    uint32_t frame_advantage_stalls;
    uint32_t prediction_stalls;
    uint32_t frame_advantage_wait_start_tick;
    uint32_t prediction_wait_start_tick;
    uint32_t sim_loss_percent;
    uint32_t sim_delay_min_ticks;
    uint32_t sim_delay_max_ticks;
    uint32_t sim_rng;
    uint32_t sim_packets_dropped;
    uint32_t sim_packets_delayed;
    uint32_t sim_queue_drops;
    GgpoNetQueuedPacket sim_queue[GGPO_NET_SIM_QUEUE_PACKETS];
    uint32_t local_build_id;
    uint32_t local_exe_id;
    uint32_t local_dll_id;
    uint32_t remote_build_id;
    uint32_t remote_exe_id;
    uint32_t remote_dll_id;
    int has_remote_fingerprint;
    int warned_fingerprint_mismatch;
    char local_cosmetic_profile[GGPO_NET_COSMETIC_PROFILE_BYTES];
    uint32_t local_cosmetic_profile_len;
    uint32_t local_cosmetic_profile_revision;
    uint32_t last_cosmetic_profile_send_tick;
    char local_cosmetic_asset_id[GGPO_NET_COSMETIC_ASSET_ID_BYTES];
    uint8_t* local_cosmetic_asset;
    uint32_t local_cosmetic_asset_len;
    uint32_t local_cosmetic_asset_revision;
    uint32_t local_cosmetic_asset_next_chunk;
    uint32_t local_cosmetic_asset_last_send_tick;
    uint32_t local_cosmetic_asset_peer_applied_revision;
    char remote_cosmetic_profile[GGPO_NET_COSMETIC_PROFILE_BYTES];
    uint32_t remote_cosmetic_profile_len;
    uint32_t remote_cosmetic_profile_revision;
    uint32_t remote_cosmetic_profile_applied_revision;
    char remote_cosmetic_asset_id[GGPO_NET_COSMETIC_ASSET_ID_BYTES];
    uint8_t* remote_cosmetic_asset;
    uint8_t* remote_cosmetic_asset_chunks_seen;
    uint32_t remote_cosmetic_asset_len;
    uint32_t remote_cosmetic_asset_revision;
    uint32_t remote_cosmetic_asset_applied_revision;
    uint32_t remote_cosmetic_asset_chunk_count;
    uint32_t remote_cosmetic_asset_seen_chunk_count;
    int remote_cosmetic_asset_complete;
    uint32_t cosmetic_wait_start_tick;
    int cosmetic_wait_cap_announced;
} GgpoNetSession;

static GgpoNetSession g_net;
static int g_wsa_ready = 0;
static BCRYPT_ALG_HANDLE g_net_auth_hmac_alg = NULL;
static ULONG g_net_auth_hash_object_bytes = 0u;
static uint8_t g_net_pending_auth_root[GGPO_NET_AUTH_KEY_BYTES];
static int g_net_pending_auth_ready = 0;
#ifdef GGPO_NET_TEST
static int g_net_test_tamper_outgoing = 0;
static int g_net_test_replay_outgoing = 0;
#endif
static uint32_t g_net_config_input_delay = GGPO_NET_DEFAULT_INPUT_DELAY;
/* When set, measured RTT may raise the effective input delay above the
 * configured value to cut prediction/rollback on high-latency links. It never
 * lowers it below the configured value, and each peer adapts independently
 * (input delay is local-only and asymmetric-safe). */
static int g_net_config_auto_input_delay = 1;
/* Diagnostic: when on, every LIVE (non-rollback) frame that draws RNG logs a
 * compact per-frame trace (frame, local seed before->after, draw count, caller
 * addresses). Two instances on one machine share modframework.log, so the first
 * frame whose p=0 and p=1 lines differ is the process-local RNG divergence and
 * its first differing caller is the offender. Off by default; toggle with the
 * console: "ggpo.net rngtrace on". */
static int g_net_config_rng_trace = 0;
static uint32_t g_net_config_max_frame_advantage = GGPO_NET_DEFAULT_MAX_FRAME_ADVANTAGE;
static uint32_t g_net_config_max_prediction = GGPO_NET_DEFAULT_MAX_PREDICTION;
static uint32_t g_net_config_sim_loss_percent = 0;
static uint32_t g_net_config_sim_delay_min_ticks = 0;
static uint32_t g_net_config_sim_delay_max_ticks = 0;
static int g_net_config_correction_enabled = 1;
static uint32_t g_net_local_build_id = 0;
static uint32_t g_net_local_exe_id = 0;
static uint32_t g_net_local_dll_id = 0;

static int ggpo_net_send_packet(uint16_t type);
static void ggpo_net_clear_rollback_history(void);
static void ggpo_net_reset_authoritative_state_bookkeeping(void);

/* Per-frame RNG trace ring (diagnostic, populated only when rngtrace is on).
 * Stored in RAM with no I/O during play; dumped to the log when a desync fires
 * so two same-machine instances can be diffed frame-by-frame. */
/* Sized to span a whole round (rounds run >1024 frames; a smaller ring is partly
 * overwritten by rollback/replay before the dump runs, so the dump came out sparse
 * and never covered a round's start on BOTH peers -> couldn't diff the first
 * divergence). 4096 frames * ~352B = ~1.4MB, only touched when rngtrace is on. */
#define GGPO_NET_RNG_RING        4096u
#define GGPO_NET_RNG_FRAME_DRAWS 64u
typedef struct GgpoNetRngFrame {
    int valid;
    uint32_t frame;
    uint32_t seed_before;
    uint32_t seed_after;
    uint32_t local_cmd;
    uint32_t remote_cmd;
    uint32_t count;
    uint32_t stored;
    uint8_t kinds[GGPO_NET_RNG_FRAME_DRAWS];
    uint32_t callers[GGPO_NET_RNG_FRAME_DRAWS];
} GgpoNetRngFrame;
static GgpoNetRngFrame g_rng_ring[GGPO_NET_RNG_RING];
static uint32_t g_rng_dump_count;
/* Last desync frame whose ring we dumped, so the dump fires at most once per
 * frame even though recoverable_desync may be reached on both the detecting AND
 * the awaiting-correction peer (we now dump before the awaiting-correction
 * early-return guard so BOTH peers' rings get logged for the same round - needed
 * to diff p=0 vs p=1 and pin a leaking RNG call site). */
static uint32_t g_rng_last_dump_frame = 0xFFFFFFFFu;
static void ggpo_net_record_rng_frame(uint32_t frame, uint32_t local_cmd, uint32_t remote_cmd,
                                      uint32_t seed_before, uint32_t seed_after, const HooksRngTrace* tr);
static void ggpo_net_dump_rng_ring(uint32_t desync_frame);

static uint32_t ggpo_net_state_chunk_count(uint32_t state_size) {
    if (state_size == 0u) return 0u;
    return (state_size + (GGPO_NET_STATE_CHUNK_BYTES - 1u)) / GGPO_NET_STATE_CHUNK_BYTES;
}

static uint32_t ggpo_net_cosmetic_asset_chunk_count(uint32_t asset_size) {
    if (asset_size == 0u) return 0u;
    return (asset_size + (GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES - 1u)) / GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES;
}

static uint32_t ggpo_net_cosmetic_asset_chunk_offset(uint32_t chunk_index) {
    return chunk_index * GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES;
}

static uint32_t ggpo_net_cosmetic_asset_chunk_size(uint32_t asset_len, uint32_t chunk_index) {
    uint32_t offset = ggpo_net_cosmetic_asset_chunk_offset(chunk_index);
    uint32_t remaining = 0;
    if (asset_len == 0u || offset >= asset_len) return 0u;
    remaining = asset_len - offset;
    return remaining > GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES ? GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES : remaining;
}

static uint32_t ggpo_net_state_chunk_offset(uint32_t chunk_index) {
    return chunk_index * GGPO_NET_STATE_CHUNK_BYTES;
}

static uint32_t ggpo_net_state_chunk_size(size_t state_len, uint32_t chunk_index) {
    uint32_t offset = ggpo_net_state_chunk_offset(chunk_index);
    uint32_t remaining = 0;
    if (state_len == 0 || state_len > (size_t)UINT_MAX || offset >= (uint32_t)state_len) return 0u;
    remaining = (uint32_t)state_len - offset;
    return remaining > GGPO_NET_STATE_CHUNK_BYTES ? GGPO_NET_STATE_CHUNK_BYTES : remaining;
}

static int ggpo_net_state_chunk_differs(const uint8_t* a, const uint8_t* b, size_t state_len, uint32_t chunk_index) {
    uint32_t offset = ggpo_net_state_chunk_offset(chunk_index);
    uint32_t chunk = ggpo_net_state_chunk_size(state_len, chunk_index);
    if (!a || !b || chunk == 0u) return 0;
    return memcmp(a + offset, b + offset, chunk) != 0;
}

static uint32_t ggpo_net_count_changed_chunks(const uint8_t* base, const uint8_t* state, size_t state_len) {
    uint32_t full_chunks = ggpo_net_state_chunk_count((uint32_t)state_len);
    uint32_t changed = 0;
    if (!base || !state || state_len == 0 || state_len > (size_t)UINT_MAX) return full_chunks;
    for (uint32_t i = 0; i < full_chunks; i++) {
        if (ggpo_net_state_chunk_differs(base, state, state_len, i)) {
            changed++;
        }
    }
    return changed;
}

static int ggpo_net_set_correction_base(const uint8_t* state, size_t state_len, uint32_t checksum) {
    if (!g_net.correction_base_state || !state || state_len == 0 || state_len > g_net.state_size) return 0;
    memcpy(g_net.correction_base_state, state, state_len);
    g_net.correction_base_state_len = state_len;
    g_net.correction_base_checksum = checksum;
    return 1;
}

static uint32_t ggpo_net_next_correction_id(void) {
    uint32_t id = g_net.correction_id + 1u;
    return id ? id : 1u;
}

static void ggpo_net_set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg ? msg : "unknown error");
}

static void ggpo_net_auth_reject(void) {
    if (g_net.auth_rejected_packets != UINT32_MAX) {
        g_net.auth_rejected_packets++;
    }
}

static int ggpo_net_auth_ensure_hmac(void) {
    NTSTATUS status;
    ULONG result_bytes = 0u;
    ULONG hash_bytes = 0u;
    if (g_net_auth_hmac_alg) return 1;
    status = BCryptOpenAlgorithmProvider(&g_net_auth_hmac_alg,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status < 0) goto fail;
    status = BCryptGetProperty(g_net_auth_hmac_alg,
                               BCRYPT_OBJECT_LENGTH,
                               (PUCHAR)&g_net_auth_hash_object_bytes,
                               sizeof(g_net_auth_hash_object_bytes),
                               &result_bytes,
                               0);
    if (status < 0 || result_bytes != sizeof(g_net_auth_hash_object_bytes) ||
        g_net_auth_hash_object_bytes == 0u) {
        goto fail;
    }
    status = BCryptGetProperty(g_net_auth_hmac_alg,
                               BCRYPT_HASH_LENGTH,
                               (PUCHAR)&hash_bytes,
                               sizeof(hash_bytes),
                               &result_bytes,
                               0);
    if (status < 0 || result_bytes != sizeof(hash_bytes) ||
        hash_bytes != GGPO_NET_AUTH_KEY_BYTES) {
        goto fail;
    }
    return 1;

fail:
    if (g_net_auth_hmac_alg) {
        BCryptCloseAlgorithmProvider(g_net_auth_hmac_alg, 0);
        g_net_auth_hmac_alg = NULL;
    }
    g_net_auth_hash_object_bytes = 0u;
    return 0;
}

static int ggpo_net_auth_hmac_parts(const uint8_t* key,
                                    ULONG key_len,
                                    const void* const* parts,
                                    const ULONG* part_lens,
                                    size_t part_count,
                                    uint8_t out[GGPO_NET_AUTH_KEY_BYTES]) {
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t* object = NULL;
    NTSTATUS status;
    size_t i;
    int ok = 0;
    if (!key || key_len == 0u || !parts || !part_lens || !out ||
        !ggpo_net_auth_ensure_hmac()) {
        return 0;
    }
    object = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, g_net_auth_hash_object_bytes);
    if (!object) return 0;
    status = BCryptCreateHash(g_net_auth_hmac_alg,
                              &hash,
                              object,
                              g_net_auth_hash_object_bytes,
                              (PUCHAR)(uintptr_t)key,
                              key_len,
                              0);
    if (status < 0) goto done;
    for (i = 0; i < part_count; i++) {
        if (part_lens[i] == 0u) continue;
        if (!parts[i]) goto done;
        status = BCryptHashData(hash,
                                (PUCHAR)(uintptr_t)parts[i],
                                part_lens[i],
                                0);
        if (status < 0) goto done;
    }
    status = BCryptFinishHash(hash, out, GGPO_NET_AUTH_KEY_BYTES, 0);
    if (status >= 0) ok = 1;

done:
    if (hash) BCryptDestroyHash(hash);
    if (object) {
        SecureZeroMemory(object, g_net_auth_hash_object_bytes);
        HeapFree(GetProcessHeap(), 0, object);
    }
    if (!ok) SecureZeroMemory(out, GGPO_NET_AUTH_KEY_BYTES);
    return ok;
}

static int ggpo_net_auth_derive_direction_key(
    const uint8_t root[GGPO_NET_AUTH_KEY_BYTES],
    uint32_t sender_player,
    uint64_t session_id,
    uint8_t out[GGPO_NET_AUTH_KEY_BYTES]) {
    static const uint8_t domain[] = "EGGNOGG+ GGPO v16 direction key";
    const uint16_t version = GGPO_NET_VERSION;
    const void* parts[] = {domain, &version, &sender_player, &session_id};
    const ULONG lens[] = {
        (ULONG)(sizeof(domain) - 1u),
        (ULONG)sizeof(version),
        (ULONG)sizeof(sender_player),
        (ULONG)sizeof(session_id),
    };
    return ggpo_net_auth_hmac_parts(root,
                                    GGPO_NET_AUTH_KEY_BYTES,
                                    parts,
                                    lens,
                                    sizeof(parts) / sizeof(parts[0]),
                                    out);
}

static int ggpo_net_auth_packet_tag(const uint8_t key[GGPO_NET_AUTH_KEY_BYTES],
                                    const void* packet,
                                    int packet_len,
                                    uint8_t out[GGPO_NET_AUTH_KEY_BYTES]) {
    static const uint8_t domain[] = "EGGNOGG+ GGPO v16 packet tag";
    uint32_t wire_len;
    const void* parts[3];
    ULONG lens[3];
    if (!packet || packet_len < (int)sizeof(GgpoNetPacketPrefix)) return 0;
    wire_len = (uint32_t)packet_len;
    parts[0] = domain;
    parts[1] = &wire_len;
    parts[2] = packet;
    lens[0] = (ULONG)(sizeof(domain) - 1u);
    lens[1] = (ULONG)sizeof(wire_len);
    lens[2] = (ULONG)packet_len;
    return ggpo_net_auth_hmac_parts(key,
                                    GGPO_NET_AUTH_KEY_BYTES,
                                    parts,
                                    lens,
                                    sizeof(parts) / sizeof(parts[0]),
                                    out);
}

static int ggpo_net_auth_constant_time_equal(const uint8_t* a,
                                             const uint8_t* b,
                                             size_t len) {
    volatile uint8_t difference = 0u;
    size_t i;
    if (!a || !b) return 0;
    for (i = 0; i < len; i++) difference |= (uint8_t)(a[i] ^ b[i]);
    return difference == 0u;
}

static int ggpo_net_hex_nibble(char c, uint8_t* out) {
    if (c >= '0' && c <= '9') *out = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') *out = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') *out = (uint8_t)(c - 'A' + 10);
    else return 0;
    return 1;
}

int ggpo_net_set_match_token(const char* token_hex, char* err, size_t err_cap) {
    static const uint8_t domain[] = "EGGNOGG+ GGPO v16 match auth root";
    uint8_t token[GGPO_NET_AUTH_KEY_BYTES];
    uint8_t high;
    uint8_t low;
    const void* parts[] = {domain};
    const ULONG lens[] = {(ULONG)(sizeof(domain) - 1u)};
    size_t i;
    if (g_net.active) {
        ggpo_net_set_err(err, err_cap, "cannot change match token during an active session");
        return 0;
    }
    /* A failed replacement must not leave an older match key armed. */
    SecureZeroMemory(g_net_pending_auth_root, sizeof(g_net_pending_auth_root));
    g_net_pending_auth_ready = 0;
    if (!token_hex || strlen(token_hex) != GGPO_NET_MATCH_TOKEN_HEX_BYTES) {
        ggpo_net_set_err(err, err_cap, "match token must be exactly 64 hexadecimal characters");
        return 0;
    }
    memset(token, 0, sizeof(token));
    for (i = 0; i < sizeof(token); i++) {
        if (!ggpo_net_hex_nibble(token_hex[i * 2u], &high) ||
            !ggpo_net_hex_nibble(token_hex[i * 2u + 1u], &low)) {
            SecureZeroMemory(token, sizeof(token));
            ggpo_net_set_err(err, err_cap, "match token contains a non-hexadecimal character");
            return 0;
        }
        token[i] = (uint8_t)((high << 4) | low);
    }
    if (!ggpo_net_auth_hmac_parts(token,
                                  (ULONG)sizeof(token),
                                  parts,
                                  lens,
                                  sizeof(parts) / sizeof(parts[0]),
                                  g_net_pending_auth_root)) {
        SecureZeroMemory(token, sizeof(token));
        ggpo_net_set_err(err, err_cap, "Windows packet authentication setup failed");
        return 0;
    }
    SecureZeroMemory(token, sizeof(token));
    g_net_pending_auth_ready = 1;
    return 1;
}

void ggpo_net_clear_match_token(void) {
    if (g_net.active) return;
    SecureZeroMemory(g_net_pending_auth_root, sizeof(g_net_pending_auth_root));
    g_net_pending_auth_ready = 0;
}

#ifdef GGPO_NET_TEST
void ggpo_net_test_set_tamper_outgoing(int enabled) {
    g_net_test_tamper_outgoing = enabled ? 1 : 0;
}

void ggpo_net_test_set_replay_outgoing(int enabled) {
    g_net_test_replay_outgoing = enabled ? 1 : 0;
}
#endif

static void ggpo_net_json_escape(char* out, size_t out_cap, const char* s) {
    size_t pos = 0;
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!s) return;
    while (*s && pos + 1 < out_cap) {
        unsigned char c = (unsigned char)*s++;
        if ((c == '"' || c == '\\') && pos + 2 < out_cap) {
            out[pos++] = '\\';
            out[pos++] = (char)c;
        } else if (c >= 32 && c < 127) {
            out[pos++] = (char)c;
        }
    }
    out[pos] = '\0';
}

static uint32_t ggpo_net_hash_mix_u32(uint32_t h, uint32_t v) {
    h ^= v;
    h *= 16777619u;
    return h ? h : 2166136261u;
}

static uint32_t ggpo_net_file_hash(const char* path) {
    FILE* f;
    uint8_t buf[4096];
    size_t n;
    uint32_t h = 2166136261u;
    if (!path || !path[0]) return 0u;
    f = fopen(path, "rb");
    if (!f) return 0u;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= (uint32_t)buf[i];
            h *= 16777619u;
        }
    }
    fclose(f);
    return h ? h : 1u;
}

static void ggpo_net_ensure_local_fingerprint(void) {
    char path[MAX_PATH];
    HMODULE self_mod;
    uint32_t exe_id = g_net_local_exe_id;
    uint32_t dll_id = g_net_local_dll_id;
    uint32_t build_id = g_net_local_build_id;

    if (build_id != 0u) return;

    path[0] = '\0';
    if (GetModuleFileNameA(NULL, path, (DWORD)sizeof(path)) > 0) {
        exe_id = ggpo_net_file_hash(path);
    }

    path[0] = '\0';
    self_mod = GetModuleHandleA("SDL2.dll");
    if (self_mod && GetModuleFileNameA(self_mod, path, (DWORD)sizeof(path)) > 0) {
        dll_id = ggpo_net_file_hash(path);
    }

    build_id = 2166136261u;
    build_id = ggpo_net_hash_mix_u32(build_id, GGPO_NET_MAGIC);
    build_id = ggpo_net_hash_mix_u32(build_id, GGPO_NET_VERSION);
    build_id = ggpo_net_hash_mix_u32(build_id, GGPO_NET_HISTORY_FRAMES);
    build_id = ggpo_net_hash_mix_u32(build_id, GGPO_NET_PACKET_INPUTS);
    build_id = ggpo_net_hash_mix_u32(build_id, GGPO_NET_PACKET_CHECKSUMS);
    build_id = ggpo_net_hash_mix_u32(build_id, (uint32_t)ggpo_ext_game_state_size());
    build_id = ggpo_net_hash_mix_u32(build_id, exe_id);
    build_id = ggpo_net_hash_mix_u32(build_id, dll_id);

    g_net_local_exe_id = exe_id;
    g_net_local_dll_id = dll_id;
    g_net_local_build_id = build_id ? build_id : 1u;
}

static void ggpo_net_note_remote_fingerprint(uint32_t build_id, uint32_t exe_id, uint32_t dll_id) {
    if (!build_id && !exe_id && !dll_id) return;
    if (!g_net.has_remote_fingerprint) {
        g_net.remote_build_id = build_id;
        g_net.remote_exe_id = exe_id;
        g_net.remote_dll_id = dll_id;
        g_net.has_remote_fingerprint = 1;
    }
    if (g_net.remote_build_id == build_id &&
        g_net.remote_exe_id == exe_id &&
        g_net.remote_dll_id == dll_id) {
        if (!g_net.warned_fingerprint_mismatch &&
            g_net.local_build_id != 0u &&
            g_net.local_build_id == build_id &&
            g_net.local_exe_id == exe_id &&
            g_net.local_dll_id == dll_id) {
            return;
        }
    }

    g_net.remote_build_id = build_id;
    g_net.remote_exe_id = exe_id;
    g_net.remote_dll_id = dll_id;
    if (!g_net.warned_fingerprint_mismatch &&
        (g_net.local_build_id != build_id ||
         g_net.local_exe_id != exe_id ||
         g_net.local_dll_id != dll_id)) {
        LOG_WARN("ggpo.net: peer build fingerprint differs local build=%08X exe=%08X dll=%08X remote build=%08X exe=%08X dll=%08X",
                 (unsigned int)g_net.local_build_id,
                 (unsigned int)g_net.local_exe_id,
                 (unsigned int)g_net.local_dll_id,
                 (unsigned int)build_id,
                 (unsigned int)exe_id,
                 (unsigned int)dll_id);
        g_net.warned_fingerprint_mismatch = 1;
    }
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

static int ggpo_net_make_session_id(uint64_t* out_session_id) {
    uint64_t value = 0u;
    int attempt;
    if (!out_session_id) return 0;
    for (attempt = 0; attempt < 4 && value == 0u; attempt++) {
        if (BCryptGenRandom(NULL,
                            (PUCHAR)&value,
                            (ULONG)sizeof(value),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
            SecureZeroMemory(&value, sizeof(value));
            return 0;
        }
    }
    if (value == 0u) return 0;
    *out_session_id = value;
    return 1;
}

static int ggpo_net_make_socket(uint16_t local_port, SOCKET* out_sock, uint16_t* out_bound_port, char* err, size_t err_cap) {
    SOCKET s;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
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
    if (getsockname(s, (struct sockaddr*)&addr, &addr_len) == 0 && out_bound_port) {
        *out_bound_port = ntohs(addr.sin_port);
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

/* Register a hole-punch candidate endpoint (deduped). Handshake HELLOs are sent
 * to every candidate until the peer replies from one of them. */
static void ggpo_net_add_candidate_addr(const struct sockaddr_in* addr) {
    int i;
    int cap = (int)(sizeof(g_net.candidates) / sizeof(g_net.candidates[0]));
    if (!addr || addr->sin_port == 0) return;
    for (i = 0; i < g_net.candidate_count; i++) {
        if (ggpo_net_addr_equal(&g_net.candidates[i], addr)) return;
    }
    if (g_net.candidate_count < cap) {
        g_net.candidates[g_net.candidate_count++] = *addr;
    }
}

static uint32_t ggpo_net_rand_u32(void) {
    if (g_net.sim_rng == 0u) {
        g_net.sim_rng = (uint32_t)g_net.session_id ^
                        (uint32_t)(g_net.session_id >> 32) ^
                        0xA5C31F27u;
        if (g_net.sim_rng == 0u) g_net.sim_rng = 1u;
    }
    g_net.sim_rng ^= g_net.sim_rng << 13;
    g_net.sim_rng ^= g_net.sim_rng >> 17;
    g_net.sim_rng ^= g_net.sim_rng << 5;
    return g_net.sim_rng;
}

static uint32_t ggpo_net_rand_range(uint32_t count) {
    if (count == 0u) return 0u;
    return ggpo_net_rand_u32() % count;
}

static int ggpo_net_tick_reached(uint32_t now, uint32_t then) {
    return (int32_t)(now - then) >= 0;
}

static uint32_t ggpo_net_sim_pending_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < GGPO_NET_SIM_QUEUE_PACKETS; i++) {
        if (g_net.sim_queue[i].valid) count++;
    }
    return count;
}

static int ggpo_net_send_raw_bytes(const void* data, int len, const struct sockaddr_in* addr) {
    int sent;
    if (!data || len <= 0 || !addr || g_net.sock == INVALID_SOCKET) return 0;
    sent = sendto(g_net.sock,
                  (const char*)data,
                  len,
                  0,
                  (const struct sockaddr*)addr,
                  sizeof(*addr));
    if (sent == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 1;
        return 0;
    }
    g_net.packets_sent++;
    return 1;
}

static int ggpo_net_queue_sim_packet(const void* data, int len, const struct sockaddr_in* addr, uint32_t delay_ticks) {
    for (int i = 0; i < GGPO_NET_SIM_QUEUE_PACKETS; i++) {
        GgpoNetQueuedPacket* q = &g_net.sim_queue[i];
        if (q->valid) continue;
        q->valid = 1;
        q->send_tick = g_net.service_tick + delay_ticks;
        q->len = len;
        q->addr = *addr;
        memcpy(q->bytes, data, (size_t)len);
        g_net.sim_packets_delayed++;
        return 1;
    }
    g_net.sim_queue_drops++;
    return 1;
}

static void ggpo_net_flush_sim_queue(void) {
    for (int i = 0; i < GGPO_NET_SIM_QUEUE_PACKETS; i++) {
        GgpoNetQueuedPacket* q = &g_net.sim_queue[i];
        if (!q->valid) continue;
        if (!ggpo_net_tick_reached(g_net.service_tick, q->send_tick)) continue;
        if (ggpo_net_send_raw_bytes(q->bytes, q->len, &q->addr)) {
            q->valid = 0;
        }
    }
}

static int ggpo_net_sign_packet(void* data, int len) {
    GgpoNetPacketPrefix* prefix = (GgpoNetPacketPrefix*)data;
    uint8_t full_tag[GGPO_NET_AUTH_KEY_BYTES];
    int ok;
    if (!data || len < (int)sizeof(*prefix) || !g_net.auth_ready) return 0;
    if (prefix->magic != GGPO_NET_MAGIC || prefix->version != GGPO_NET_VERSION ||
        prefix->session_id != g_net.session_id ||
        prefix->sender_player != (uint32_t)g_net.local_player) {
        return 0;
    }
    if (g_net.auth_send_sequence == UINT64_MAX) return 0;
    prefix->auth_sequence = ++g_net.auth_send_sequence;
    memset(prefix->auth_tag, 0, sizeof(prefix->auth_tag));
    ok = ggpo_net_auth_packet_tag(g_net.auth_send_key, data, len, full_tag);
    if (ok) memcpy(prefix->auth_tag, full_tag, sizeof(prefix->auth_tag));
    SecureZeroMemory(full_tag, sizeof(full_tag));
    return ok;
}

static int ggpo_net_send_bytes(const void* data, int len, const struct sockaddr_in* addr, int simulate) {
    uint8_t signed_packet[GGPO_NET_MAX_PACKET_BYTES];
    uint32_t delay = 0;
    if (!data || len <= 0 || len > (int)GGPO_NET_MAX_PACKET_BYTES || !addr) return 0;
    memcpy(signed_packet, data, (size_t)len);
    if (!ggpo_net_sign_packet(signed_packet, len)) return 0;
#ifdef GGPO_NET_TEST
    if (g_net_test_tamper_outgoing && len > (int)sizeof(GgpoNetPacketPrefix)) {
        signed_packet[len - 1] ^= 0x01u;
    }
#endif

    if (simulate && g_net.sim_loss_percent > 0u) {
        if (ggpo_net_rand_range(100u) < g_net.sim_loss_percent) {
            g_net.sim_packets_dropped++;
            return 1;
        }
    }

    if (simulate && g_net.sim_delay_max_ticks > 0u) {
        uint32_t min_delay = g_net.sim_delay_min_ticks;
        uint32_t max_delay = g_net.sim_delay_max_ticks;
        if (max_delay < min_delay) max_delay = min_delay;
        delay = min_delay + ggpo_net_rand_range((max_delay - min_delay) + 1u);
        if (delay > 0u) {
            return ggpo_net_queue_sim_packet(signed_packet, len, addr, delay);
        }
    }

    {
        int sent = ggpo_net_send_raw_bytes(signed_packet, len, addr);
#ifdef GGPO_NET_TEST
        if (sent && g_net_test_replay_outgoing) {
            (void)ggpo_net_send_raw_bytes(signed_packet, len, addr);
        }
#endif
        return sent;
    }
}

static int ggpo_net_link_confirmed(void) {
    return g_net.connected &&
           g_net.session_confirmed &&
           g_net.peer_confirmed_session;
}

/* A peer may legitimately replace its UDP socket while both clients are still
 * behind the prematch barrier.  In particular, the matchmaking retry machine
 * rotates the NAT mapping after a timeout.  Once gameplay has loaded frame 0,
 * however, accepting a new session or endpoint would splice two matches
 * together, so recovery is deliberately limited to this narrow window. */
static int ggpo_net_prematch_recovery_allowed(void) {
    return g_net.active &&
           g_net.prematch_used &&
           g_net.frame == 0u &&
           !g_net.start_state_loaded;
}

static int ggpo_net_remote_session_retired(uint64_t session_id) {
    for (int i = 0; i < g_net.retired_remote_session_count; i++) {
        if (g_net.retired_remote_session_ids[i] == session_id) return 1;
    }
    return 0;
}

static void ggpo_net_retire_remote_session(uint64_t session_id) {
    int cap = (int)(sizeof(g_net.retired_remote_session_ids) /
                    sizeof(g_net.retired_remote_session_ids[0]));
    if (session_id == 0u || ggpo_net_remote_session_retired(session_id)) return;
    if (g_net.retired_remote_session_count < cap) {
        g_net.retired_remote_session_ids[g_net.retired_remote_session_count++] = session_id;
        return;
    }
    memmove(&g_net.retired_remote_session_ids[0],
            &g_net.retired_remote_session_ids[1],
            (size_t)(cap - 1) * sizeof(g_net.retired_remote_session_ids[0]));
    g_net.retired_remote_session_ids[cap - 1] = session_id;
}

/* A peer may restart with a fresh socket/session while neither side has completed
 * the confirmation exchange. No gameplay or state transfer is allowed before
 * confirmation, so only handshake-scoped state needs to be re-armed here. */
static void ggpo_net_reset_unconfirmed_peer_attempt(void) {
    g_net.connected = 0;
    g_net.session_confirmed = 0;
    g_net.peer_confirmed_session = 0;
    g_net.peer_disconnected = 0;
    g_net.last_rx_tick = 0u;
    g_net.last_handshake_burst_tick = 0u;
    g_net.peer_last_send_tick = 0u;
    g_net.rtt_ema_ticks = 0u;
    g_net.rtt_last_ticks = 0u;
    g_net.rtt_sample_count = 0u;
    g_net.auto_input_delay_applied = 0;
    g_net.remote_build_id = 0u;
    g_net.remote_exe_id = 0u;
    g_net.remote_dll_id = 0u;
    g_net.has_remote_fingerprint = 0;
    g_net.warned_fingerprint_mismatch = 0;
    g_net.remote_prematch_hold = 0;
    g_net.remote_prematch_hold_known = 0;
    g_net.remote_hold_epoch = 0u;
    g_net.remote_state_epoch_seen = 0u;
    memset(g_net.history, 0, sizeof(g_net.history));
    memset(g_net.local_inputs, 0, sizeof(g_net.local_inputs));
    memset(g_net.remote_inputs, 0, sizeof(g_net.remote_inputs));
    g_net.frame = 0u;
    g_net.remote_frame = 0u;
    g_net.has_remote_frame = 0;
    g_net.rollback_to = 0u;
    g_net.rollback_pending = 0;
    g_net.last_remote_cmd = 0u;
    g_net.has_last_remote_cmd = 0;
    g_net.warned_initial_mismatch = 0;
    g_net.state_synced = (g_net.mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.remote_state_synced = (g_net.mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    g_net.state_sync_announced = 0;
    g_net.start_state_loaded = 0;
    g_net.frame0_wait_announced = 0;
    g_net.state_send_offset = 0u;
    g_net.state_sync_chunks_sent = 0u;
    memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
}

/* A fully confirmed peer can still rotate its socket while the match remains
 * behind the frame-0 barrier. Unlike the lightweight pre-confirmation reset,
 * this discards every partially assembled/acknowledged state generation. The
 * surviving host keeps its captured authoritative state; a surviving join
 * forgets the old host epoch and waits for the restarted host's fresh epoch 2. */
static void ggpo_net_reset_confirmed_prematch_peer_attempt(void) {
    ggpo_net_reset_unconfirmed_peer_attempt();
    ggpo_net_reset_authoritative_state_bookkeeping();
    if (g_net.mode == GGPO_NET_MODE_HOST) {
        g_net.state_synced = 1;
        g_net.remote_state_synced = 0;
        if (g_net.initial_state && g_net.initial_state_len > 0u) {
            (void)ggpo_net_set_correction_base(g_net.initial_state,
                                               g_net.initial_state_len,
                                               g_net.initial_checksum);
        }
    } else {
        g_net.state_epoch = 0u;
        g_net.state_synced = 0;
        g_net.remote_state_synced = 1;
        g_net.remote_state_epoch_seen = 0u;
        g_net.hold_remote_epoch_floor = GGPO_NET_INITIAL_STATE_EPOCH;
    }
}

/* Adopt session id and source atomically. A different session id is accepted only
 * from a HELLO-equivalent caller while the pre-frame-0 link is still unconfirmed;
 * delayed packets from retired attempts can therefore never flip the session back. */
static int ggpo_net_accept_packet_source(const struct sockaddr_in* from,
                                         uint64_t session_id,
                                         const char* kind,
                                         int allow_attempt_migration) {
    int source_matches;
    int can_migrate;
    int recovered_confirmed_prematch = 0;
    if (!from || session_id == 0u) return 0;
    if (ggpo_net_remote_session_retired(session_id)) return 0;

    source_matches = g_net.has_peer_addr && ggpo_net_addr_equal(&g_net.peer_addr, from);
    can_migrate = allow_attempt_migration &&
                  g_net.frame == 0u &&
                  !g_net.start_state_loaded &&
                  (!ggpo_net_link_confirmed() ||
                   (ggpo_net_prematch_recovery_allowed() &&
                    g_net.has_remote_session_id &&
                    g_net.remote_session_id != session_id));

    if (!g_net.has_remote_session_id) {
        if (!allow_attempt_migration) return 0;
        g_net.remote_session_id = session_id;
        g_net.has_remote_session_id = 1;
    } else if (g_net.remote_session_id != session_id) {
        uint64_t old_session;
        if (!can_migrate) return 0;
        recovered_confirmed_prematch = ggpo_net_link_confirmed();
        old_session = g_net.remote_session_id;
        ggpo_net_retire_remote_session(old_session);
        if (recovered_confirmed_prematch) {
            ggpo_net_reset_confirmed_prematch_peer_attempt();
        } else {
            ggpo_net_reset_unconfirmed_peer_attempt();
        }
        g_net.remote_session_id = session_id;
        g_net.has_remote_session_id = 1;
        LOG_INFO("ggpo.net: %s peer session old=%016llX new=%016llX",
                 recovered_confirmed_prematch
                    ? "recovered confirmed prematch"
                    : "migrated unconfirmed",
                 (unsigned long long)old_session,
                 (unsigned long long)session_id);
    }

    if (!g_net.has_peer_addr || source_matches) {
        if (!g_net.has_peer_addr) {
            g_net.peer_addr = *from;
            g_net.has_peer_addr = 1;
            g_net.remote_port = ntohs(from->sin_port);
            g_net.last_handshake_burst_tick = 0u;
        }
        return 1;
    }

    if (can_migrate) {
        g_net.peer_addr = *from;
        g_net.remote_port = ntohs(from->sin_port);
        g_net.last_handshake_burst_tick = 0u;
        g_net.alternate_endpoint_updates++;
        LOG_INFO("ggpo.net: accepted alternate peer endpoint from %s packet port=%u updates=%u",
                 kind ? kind : "unknown",
                 (unsigned int)g_net.remote_port,
                 (unsigned int)g_net.alternate_endpoint_updates);
        return 1;
    }

    return 0;
}

/* Verify cryptographic identity before any packet handler can adopt a source or
 * peer session. The sender role and random session are part of the direction-key
 * derivation as well as the authenticated bytes, so a packet cannot be reflected
 * into its sender or transplanted between socket attempts. */
static int ggpo_net_authenticate_received(void* data,
                                          int len,
                                          const struct sockaddr_in* from) {
    GgpoNetPacketPrefix* prefix = (GgpoNetPacketPrefix*)data;
    uint8_t saved_tag[GGPO_NET_AUTH_TAG_BYTES];
    uint8_t direction_key[GGPO_NET_AUTH_KEY_BYTES];
    uint8_t full_tag[GGPO_NET_AUTH_KEY_BYTES];
    uint64_t* replay_slot;
    int can_migrate;
    int ok = 0;

    memset(direction_key, 0, sizeof(direction_key));
    memset(full_tag, 0, sizeof(full_tag));
    if (!data || len < (int)sizeof(*prefix) || !from || !g_net.auth_ready ||
        prefix->magic != GGPO_NET_MAGIC || prefix->version != GGPO_NET_VERSION ||
        prefix->session_id == 0u || prefix->auth_sequence == 0u ||
        prefix->sender_player != (uint32_t)g_net.remote_player) {
        ggpo_net_auth_reject();
        goto done;
    }
    if (ggpo_net_remote_session_retired(prefix->session_id)) {
        ggpo_net_auth_reject();
        goto done;
    }
    /* Once the bidirectional link is confirmed, even a correctly authenticated
     * datagram cannot move the endpoint. Retry-time endpoint migration remains
     * available to authenticated HELLO packets below. */
    if (ggpo_net_link_confirmed() &&
        !(prefix->type == GGPO_NET_PACKET_HELLO &&
          ggpo_net_prematch_recovery_allowed() &&
          g_net.has_remote_session_id &&
          prefix->session_id != g_net.remote_session_id) &&
        (!g_net.has_peer_addr || !ggpo_net_addr_equal(&g_net.peer_addr, from))) {
        ggpo_net_auth_reject();
        goto done;
    }

    if (g_net.auth_remote_key_valid &&
        g_net.auth_remote_key_session == prefix->session_id &&
        g_net.auth_remote_key_player == prefix->sender_player) {
        memcpy(direction_key, g_net.auth_remote_key, sizeof(direction_key));
    } else if (!ggpo_net_auth_derive_direction_key(g_net.auth_root_key,
                                                   prefix->sender_player,
                                                   prefix->session_id,
                                                   direction_key)) {
        ggpo_net_auth_reject();
        goto done;
    }

    memcpy(saved_tag, prefix->auth_tag, sizeof(saved_tag));
    memset(prefix->auth_tag, 0, sizeof(prefix->auth_tag));
    if (!ggpo_net_auth_packet_tag(direction_key, data, len, full_tag)) {
        memcpy(prefix->auth_tag, saved_tag, sizeof(saved_tag));
        ggpo_net_auth_reject();
        goto done;
    }
    memcpy(prefix->auth_tag, saved_tag, sizeof(saved_tag));
    if (!ggpo_net_auth_constant_time_equal(saved_tag, full_tag, sizeof(saved_tag))) {
        ggpo_net_auth_reject();
        goto done;
    }

    can_migrate = prefix->type == GGPO_NET_PACKET_HELLO &&
                  g_net.frame == 0u &&
                  !g_net.start_state_loaded &&
                  (!ggpo_net_link_confirmed() ||
                   (ggpo_net_prematch_recovery_allowed() &&
                    g_net.has_remote_session_id &&
                    prefix->session_id != g_net.remote_session_id));
    if (g_net.auth_rx_session == 0u) {
        /* Session and replay state begin only from an authenticated HELLO, just
         * like endpoint adoption in the packet handler. */
        if (!can_migrate) {
            ggpo_net_auth_reject();
            goto done;
        }
        g_net.auth_rx_session = prefix->session_id;
        g_net.auth_rx_highest_sequence = 0u;
        memset(g_net.auth_rx_seen, 0, sizeof(g_net.auth_rx_seen));
    } else if (g_net.auth_rx_session != prefix->session_id) {
        if (!can_migrate) {
            ggpo_net_auth_reject();
            goto done;
        }
        g_net.auth_rx_session = prefix->session_id;
        g_net.auth_rx_highest_sequence = 0u;
        memset(g_net.auth_rx_seen, 0, sizeof(g_net.auth_rx_seen));
    }

    if (g_net.auth_rx_highest_sequence != 0u &&
        prefix->auth_sequence <= g_net.auth_rx_highest_sequence &&
        g_net.auth_rx_highest_sequence - prefix->auth_sequence >=
            (uint64_t)GGPO_NET_REPLAY_WINDOW) {
        ggpo_net_auth_reject();
        goto done;
    }
    replay_slot = &g_net.auth_rx_seen[
        (size_t)(prefix->auth_sequence % (uint64_t)GGPO_NET_REPLAY_WINDOW)];
    if (*replay_slot == prefix->auth_sequence) {
        ggpo_net_auth_reject();
        goto done;
    }
    *replay_slot = prefix->auth_sequence;
    if (prefix->auth_sequence > g_net.auth_rx_highest_sequence) {
        g_net.auth_rx_highest_sequence = prefix->auth_sequence;
    }
    memcpy(g_net.auth_remote_key, direction_key, sizeof(g_net.auth_remote_key));
    g_net.auth_remote_key_session = prefix->session_id;
    g_net.auth_remote_key_player = prefix->sender_player;
    g_net.auth_remote_key_valid = 1;
    ok = 1;

done:
    SecureZeroMemory(saved_tag, sizeof(saved_tag));
    SecureZeroMemory(direction_key, sizeof(direction_key));
    SecureZeroMemory(full_tag, sizeof(full_tag));
    return ok;
}

static GgpoNetInputEntry* ggpo_net_input_slot(GgpoNetInputEntry* entries, uint32_t frame) {
    return &entries[frame % GGPO_NET_HISTORY_FRAMES];
}

static GgpoNetHistoryEntry* ggpo_net_history_slot(uint32_t frame, uint8_t** out_blob) {
    int idx = (int)(frame % GGPO_NET_HISTORY_FRAMES);
    if (out_blob) *out_blob = g_net.state_blobs + ((size_t)idx * g_net.state_size);
    return &g_net.history[idx];
}

/* Frame-accurate tilemap dump for diagnosing non-RNG tilemap desyncs. The
 * confirmed-frame checksum for frame N is taken over the POST-tick state, which
 * is the saved PRE-state of frame N+1. So dumping state_blobs[N+1] gives the exact
 * tilemap the checksum saw for frame N - no render-phase animation skew (the live
 * tilemap moves between the two peers' dump moments). Gated by rngtrace. */
static void ggpo_net_dump_frame_tilemap(uint32_t checksum_frame) {
    uint32_t blob_frame = checksum_frame + 1u;
    uint8_t* blob = NULL;
    GgpoNetHistoryEntry* hh;
    if (!g_net_config_rng_trace) return;
    hh = ggpo_net_history_slot(blob_frame, &blob);
    if (hh && hh->valid && hh->frame == blob_frame && blob) {
        lua_manager_game_dump_tilemap_blob(g_net.local_player, checksum_frame, blob, hh->state_len);
    }
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

static uint32_t ggpo_net_queue_local_input(uint32_t frame, uint32_t raw_cmd) {
    uint32_t cmd = 0;
    if (ggpo_net_get_input(g_net.local_inputs, frame, &cmd)) {
        return cmd;
    }
    ggpo_net_store_input(g_net.local_inputs, frame, raw_cmd);
    return raw_cmd;
}

static void ggpo_net_seed_local_input_delay_from(uint32_t start_frame) {
    for (uint32_t i = 0; i < g_net.input_delay; i++) {
        uint32_t frame = start_frame + i;
        uint32_t tmp = 0;
        if (!ggpo_net_get_input(g_net.local_inputs, frame, &tmp)) {
            ggpo_net_store_input(g_net.local_inputs, frame, 0u);
        }
    }
}

static void ggpo_net_seed_local_input_delay(void) {
    ggpo_net_seed_local_input_delay_from(0u);
}

static uint32_t ggpo_net_now_tick(void) {
    return g_net.service_tick ? g_net.service_tick : 1u;
}

/* Fold one peer packet's timing into the RTT estimate. send_tick is the peer's
 * clock when it built the packet (echoed back to it next send); tick_echo is
 * the newest send_tick of OURS the peer had seen, so service_tick - tick_echo
 * is our measured round trip in service ticks (~frames at 60Hz). */
static void ggpo_net_record_peer_timing(uint32_t peer_send_tick, uint32_t our_tick_echo) {
    uint32_t sample;
    if (peer_send_tick) g_net.peer_last_send_tick = peer_send_tick;
    if (!our_tick_echo || g_net.service_tick < our_tick_echo) return;
    sample = g_net.service_tick - our_tick_echo;
    if (sample > 600u) return; /* >10s: stale echo, ignore */
    g_net.rtt_last_ticks = sample;
    if (g_net.rtt_sample_count == 0u) {
        g_net.rtt_ema_ticks = sample;
    } else {
        /* EMA, weight 1/4 toward the new sample */
        g_net.rtt_ema_ticks = (g_net.rtt_ema_ticks * 3u + sample) / 4u;
    }
    if (g_net.rtt_sample_count < 0xFFFFFFFFu) g_net.rtt_sample_count++;
}

/* One-shot at match start: raise the local input delay to cover the full
 * measured one-way latency plus a small jitter margin, so the peer's inputs land
 * before we simulate their frame. This keeps prediction (and the prediction
 * stalls + rollbacks it triggers) rare, which is the main cause of the
 * cross-network slow-mo (both peers stalling on prediction) and the jitter (the
 * behind peer eating rollbacks). Previously it only covered ~RTT/4, far too low
 * for internet links. Stays ONE-SHOT (never changed mid-match, to avoid the
 * input-hole desync), never lowers below the configured value, and is clamped to
 * GGPO_NET_MAX_INPUT_DELAY. */
static void ggpo_net_apply_auto_input_delay(void) {
    uint32_t one_way;
    uint32_t target;
    if (g_net.auto_input_delay_applied) return;
    if (!g_net_config_auto_input_delay) return;
    if (g_net.rtt_sample_count == 0u) return; /* no RTT yet -> keep configured value */
    g_net.auto_input_delay_applied = 1;
    one_way = (g_net.rtt_ema_ticks + 1u) / 2u;
    target = one_way + GGPO_NET_AUTO_DELAY_JITTER_MARGIN;
    if (target > (uint32_t)GGPO_NET_MAX_INPUT_DELAY) target = (uint32_t)GGPO_NET_MAX_INPUT_DELAY;
    if (target > g_net.input_delay) {
        LOG_INFO("ggpo.net: auto input delay %u->%u (rtt~%u ticks, one_way~%u, samples=%u)",
                 (unsigned int)g_net.input_delay,
                 (unsigned int)target,
                 (unsigned int)g_net.rtt_ema_ticks,
                 (unsigned int)one_way,
                 (unsigned int)g_net.rtt_sample_count);
    }
    if (target > g_net.input_delay) g_net.input_delay = target;
}

static void ggpo_net_begin_awaiting_correction(uint32_t request_frame) {
    g_net.awaiting_correction = 1;
    g_net.correction_request_frame = request_frame;
    if (g_net.correction_wait_start_tick == 0u) {
        g_net.correction_wait_start_tick = ggpo_net_now_tick();
        g_net.correction_wait_cap_announced = 0;
    }
}

static uint32_t ggpo_net_correction_wait_ticks(void) {
    if (!g_net.awaiting_correction || g_net.correction_wait_start_tick == 0u) return 0u;
    return g_net.service_tick - g_net.correction_wait_start_tick;
}

static int ggpo_net_request_host_correction(const char* reason, int force) {
    if (!g_net.correction_enabled || g_net.mode != GGPO_NET_MODE_JOIN) return 0;
    if (!force &&
        g_net.last_resync_request_tick != 0u &&
        g_net.service_tick - g_net.last_resync_request_tick < GGPO_NET_RESYNC_REQUEST_INTERVAL_TICKS) {
        return 0;
    }
    if (g_net.correction_request_frame == 0u) {
        g_net.correction_request_frame = g_net.frame;
    }
    g_net.last_resync_request_tick = ggpo_net_now_tick();
    g_net.correction_requests++;
    if (!ggpo_net_send_packet(GGPO_NET_PACKET_RESYNC_REQUEST)) {
        return 0;
    }
    if (force) {
        LOG_WARN("ggpo.net: requested host correction frame=%u request_frame=%u reason=%s total=%u",
                 (unsigned int)g_net.frame,
                 (unsigned int)g_net.correction_request_frame,
                 reason ? reason : "desync",
                 (unsigned int)g_net.correction_requests);
    } else {
        LOG_DEBUG("ggpo.net: repeated host correction request frame=%u request_frame=%u total=%u",
                  (unsigned int)g_net.frame,
                  (unsigned int)g_net.correction_request_frame,
                  (unsigned int)g_net.correction_requests);
    }
    return 1;
}

static int ggpo_net_prepare_host_correction(const char* reason) {
    size_t state_len = 0;
    uint32_t checksum = 0;
    char err[256];
    if (!g_net.correction_enabled || g_net.mode != GGPO_NET_MODE_HOST) return 0;
    if (!g_net.correction_state || g_net.state_size == 0) return 0;
    if (g_net.correction_active) return 1;

    err[0] = '\0';
    if (!ggpo_ext_save_game_state(g_net.correction_state,
                                  g_net.state_size,
                                  &state_len,
                                  &checksum,
                                  err,
                                  sizeof(err))) {
        LOG_ERROR("ggpo.net: failed to capture correction state (%s)", err[0] ? err : "unknown error");
        return 0;
    }
    g_net.correction_state_len = state_len;
    g_net.correction_checksum = checksum;
    g_net.correction_id = ggpo_net_next_correction_id();
    g_net.correction_frame = g_net.frame;
    g_net.correction_send_offset = 0;
    g_net.correction_send_next_chunk = 0;
    g_net.correction_send_chunk_count = ggpo_net_state_chunk_count((uint32_t)state_len);
    g_net.correction_send_base_checksum = 0;
    g_net.correction_send_delta = 0;
    if (g_net.correction_base_state &&
        g_net.correction_base_state_len == state_len &&
        g_net.correction_base_checksum != 0u) {
        uint32_t full_chunks = ggpo_net_state_chunk_count((uint32_t)state_len);
        uint32_t changed_chunks = ggpo_net_count_changed_chunks(g_net.correction_base_state,
                                                                g_net.correction_state,
                                                                state_len);
        if (changed_chunks > 0u && changed_chunks < full_chunks) {
            g_net.correction_send_delta = 1;
            g_net.correction_send_chunk_count = changed_chunks;
            g_net.correction_send_base_checksum = g_net.correction_base_checksum;
        }
    }
    g_net.correction_active = 1;
    g_net.corrections_sent++;
    ggpo_net_clear_rollback_history();
    LOG_WARN("ggpo.net: host correction queued id=%u frame=%u checksum=%u chunks=%u/%u mode=%s reason=%s",
             (unsigned int)g_net.correction_id,
             (unsigned int)g_net.correction_frame,
             (unsigned int)g_net.correction_checksum,
             (unsigned int)g_net.correction_send_chunk_count,
             (unsigned int)ggpo_net_state_chunk_count((uint32_t)state_len),
             g_net.correction_send_delta ? "delta" : "full",
             reason ? reason : "desync");
    return 1;
}

static const LuaGameStateRollbackSummary* ggpo_net_packet_summary_for_frame(const GgpoNetPacket* p, uint32_t frame) {
    uint32_t count = p ? p->summary_count : 0u;
    if (!p) return NULL;
    if (count > GGPO_NET_PACKET_SUMMARIES) count = GGPO_NET_PACKET_SUMMARIES;
    for (uint32_t i = 0; i < count; i++) {
        if (p->summaries[i].frame == frame) {
            return &p->summaries[i].summary;
        }
    }
    return NULL;
}

static void ggpo_net_append_component(char* dst, size_t dst_cap, const char* name) {
    size_t len = 0;
    if (!dst || dst_cap == 0 || !name || !name[0]) return;
    len = strlen(dst);
    if (len + 1 >= dst_cap) return;
    snprintf(dst + len, dst_cap - len, "%s%s", len ? "," : "", name);
}

static void ggpo_net_log_desync_summary(uint32_t frame, const GgpoNetHistoryEntry* h, const LuaGameStateRollbackSummary* remote) {
    const LuaGameStateRollbackSummary* local = (h && h->valid && h->frame == frame && h->has_summary) ? &h->summary : NULL;
    char changed[192];
    char player_detail[32];
    char thing_detail[128];
    changed[0] = '\0';
    player_detail[0] = '\0';
    thing_detail[0] = '\0';

    if (!local && !remote) {
        LOG_WARN("ggpo.net: desync detail frame=%u no local or remote rollback summaries available",
                 (unsigned int)frame);
        return;
    }
    if (!local) {
        LOG_WARN("ggpo.net: desync detail frame=%u no local rollback summary; remote full=%u header=%u transient=%u players=%u things=%u tilemap=%u",
                 (unsigned int)frame,
                 (unsigned int)remote->full_crc,
                 (unsigned int)remote->header_crc,
                 (unsigned int)remote->transient_crc,
                 (unsigned int)remote->players_crc,
                 (unsigned int)remote->things_crc,
                 (unsigned int)remote->tilemap_crc);
        return;
    }
    if (!remote) {
        LOG_WARN("ggpo.net: desync detail frame=%u no remote rollback summary; local full=%u header=%u transient=%u players=%u things=%u tilemap=%u room=%u ticks=%u rng=%u thing_count=%u map=%u score=%u-%u round_end=%u",
                 (unsigned int)frame,
                 (unsigned int)local->full_crc,
                 (unsigned int)local->header_crc,
                 (unsigned int)local->transient_crc,
                 (unsigned int)local->players_crc,
                 (unsigned int)local->things_crc,
                 (unsigned int)local->tilemap_crc,
                 (unsigned int)local->active_room,
                 (unsigned int)local->native_game_ticks,
                 (unsigned int)local->rng_seed,
                 (unsigned int)local->thing_count,
                 (unsigned int)local->map_selector,
                 (unsigned int)local->score_p0,
                 (unsigned int)local->score_p1,
                 (unsigned int)local->round_end_any);
        return;
    }

#define GGPO_NET_NOTE_DIFF(field, label) \
    do { if (local->field != remote->field) ggpo_net_append_component(changed, sizeof(changed), label); } while (0)
    GGPO_NET_NOTE_DIFF(header_crc, "header");
    GGPO_NET_NOTE_DIFF(transient_crc, "transient");
    GGPO_NET_NOTE_DIFF(thing_info_crc, "thing_info");
    GGPO_NET_NOTE_DIFF(room_info_crc, "room_info");
    GGPO_NET_NOTE_DIFF(particle_crc, "particles");
    GGPO_NET_NOTE_DIFF(players_crc, "players");
    GGPO_NET_NOTE_DIFF(things_crc, "things");
    GGPO_NET_NOTE_DIFF(tilemap_crc, "tilemap");
#undef GGPO_NET_NOTE_DIFF
    if (!changed[0]) snprintf(changed, sizeof(changed), "unknown/full-only");

    if (local->player0_crc != remote->player0_crc) ggpo_net_append_component(player_detail, sizeof(player_detail), "p0");
    if (local->player1_crc != remote->player1_crc) ggpo_net_append_component(player_detail, sizeof(player_detail), "p1");
    if (local->thing_count != remote->thing_count) ggpo_net_append_component(thing_detail, sizeof(thing_detail), "count");
    {
        uint32_t count = local->thing_count < remote->thing_count ? local->thing_count : remote->thing_count;
        if (count > LUA_ROLLBACK_SUMMARY_THING_SLOTS) count = LUA_ROLLBACK_SUMMARY_THING_SLOTS;
        for (uint32_t i = 0; i < count; i++) {
            if (local->thing_slot_crc[i] != remote->thing_slot_crc[i]) {
                char slot[12];
                snprintf(slot, sizeof(slot), "%u", (unsigned int)i);
                ggpo_net_append_component(thing_detail, sizeof(thing_detail), slot);
            }
        }
    }
    if (!player_detail[0]) snprintf(player_detail, sizeof(player_detail), "-");
    if (!thing_detail[0]) snprintf(thing_detail, sizeof(thing_detail), "-");

    LOG_ERROR("ggpo.net: desync detail frame=%u changed=%s player_diff=%s thing_slots=%s local{full=%u header=%u transient=%u thing_info=%u room_info=%u particles=%u players=%u p0=%u p1=%u things=%u tilemap=%u room=%u ticks=%u rng=%u seed=%u thing_count=%u map=%u score=%u-%u round_end=%u} remote{full=%u header=%u transient=%u thing_info=%u room_info=%u particles=%u players=%u p0=%u p1=%u things=%u tilemap=%u room=%u ticks=%u rng=%u seed=%u thing_count=%u map=%u score=%u-%u round_end=%u}",
              (unsigned int)frame,
              changed,
              player_detail,
              thing_detail,
              (unsigned int)local->full_crc,
              (unsigned int)local->header_crc,
              (unsigned int)local->transient_crc,
              (unsigned int)local->thing_info_crc,
              (unsigned int)local->room_info_crc,
              (unsigned int)local->particle_crc,
              (unsigned int)local->players_crc,
              (unsigned int)local->player0_crc,
              (unsigned int)local->player1_crc,
              (unsigned int)local->things_crc,
              (unsigned int)local->tilemap_crc,
              (unsigned int)local->active_room,
              (unsigned int)local->native_game_ticks,
              (unsigned int)local->rng_seed,
              (unsigned int)local->seed,
              (unsigned int)local->thing_count,
              (unsigned int)local->map_selector,
              (unsigned int)local->score_p0,
              (unsigned int)local->score_p1,
              (unsigned int)local->round_end_any,
              (unsigned int)remote->full_crc,
              (unsigned int)remote->header_crc,
              (unsigned int)remote->transient_crc,
              (unsigned int)remote->thing_info_crc,
              (unsigned int)remote->room_info_crc,
              (unsigned int)remote->particle_crc,
              (unsigned int)remote->players_crc,
              (unsigned int)remote->player0_crc,
              (unsigned int)remote->player1_crc,
              (unsigned int)remote->things_crc,
              (unsigned int)remote->tilemap_crc,
              (unsigned int)remote->active_room,
              (unsigned int)remote->native_game_ticks,
              (unsigned int)remote->rng_seed,
              (unsigned int)remote->seed,
              (unsigned int)remote->thing_count,
              (unsigned int)remote->map_selector,
              (unsigned int)remote->score_p0,
              (unsigned int)remote->score_p1,
              (unsigned int)remote->round_end_any);

    /* Mirror the diff to the append-mode dump file (the main log is "w"-truncated
     * and gets clobbered by the second client). This is what tells us WHICH
     * component diverged when the RNG stream itself is in sync. Gated behind
     * rngtrace so NORMAL play does zero diagnostic file I/O - the ungated version
     * stalled the sim thread on every desync (incl. initial-sync mismatches on
     * match entry -> the "freeze on joining a match" + extra mispredictions). */
    if (!g_net_config_rng_trace) return;
    log_dump_line("ggpo.desync detail f=%u p=%d fpcw=0x%04X changed=%s pdiff=%s slots=%s "
                  "L[hdr=%u tr=%u tinfo=%u rinfo=%u part=%u pl=%u p0=%u p1=%u th=%u tile=%u room=%u ticks=%u rng=%u] "
                  "R[hdr=%u tr=%u tinfo=%u rinfo=%u part=%u pl=%u p0=%u p1=%u th=%u tile=%u room=%u ticks=%u rng=%u]",
                  (unsigned int)frame, g_net.local_player,
                  (unsigned int)(_controlfp(0, 0) & 0xFFFFu),
                  changed, player_detail, thing_detail,
                  (unsigned int)local->header_crc, (unsigned int)local->transient_crc,
                  (unsigned int)local->thing_info_crc, (unsigned int)local->room_info_crc,
                  (unsigned int)local->particle_crc, (unsigned int)local->players_crc,
                  (unsigned int)local->player0_crc, (unsigned int)local->player1_crc,
                  (unsigned int)local->things_crc, (unsigned int)local->tilemap_crc,
                  (unsigned int)local->active_room, (unsigned int)local->native_game_ticks,
                  (unsigned int)local->rng_seed,
                  (unsigned int)remote->header_crc, (unsigned int)remote->transient_crc,
                  (unsigned int)remote->thing_info_crc, (unsigned int)remote->room_info_crc,
                  (unsigned int)remote->particle_crc, (unsigned int)remote->players_crc,
                  (unsigned int)remote->player0_crc, (unsigned int)remote->player1_crc,
                  (unsigned int)remote->things_crc, (unsigned int)remote->tilemap_crc,
                  (unsigned int)remote->active_room, (unsigned int)remote->native_game_ticks,
                  (unsigned int)remote->rng_seed);
    /* For a tilemap divergence (the non-RNG class), dump per-row/per-col tilemap
     * CRCs so both peers' dumps can be diffed to the exact diverging cell. */
    if (local->tilemap_crc != remote->tilemap_crc) {
        /* Detector's frame-accurate post-tick raw tilemap (gated by rngtrace). */
        ggpo_net_dump_frame_tilemap(frame);
    }
    log_dump_flush();
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

static void ggpo_net_recoverable_desync(uint32_t frame, uint32_t local_checksum, uint32_t remote_checksum, const char* why) {
    GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    /* Dump the RNG ring BEFORE the awaiting-correction guard below, so the peer
     * that is already awaiting a correction (because the other peer detected the
     * same mismatch first) still logs its ring. This guarantees BOTH peers dump
     * for the same desync round -> a clean p=0/p=1 diff. Dedup per frame. */
    if (frame != g_rng_last_dump_frame) {
        g_rng_last_dump_frame = frame;
        ggpo_net_dump_rng_ring(frame);
    }
    if (g_net.correction_enabled && (g_net.correction_active || g_net.awaiting_correction)) {
        return;
    }
    g_net.desync_frame = frame;
    g_net.desync_local_checksum = local_checksum;
    g_net.desync_remote_checksum = remote_checksum;
    g_net.desyncs++;
    LOG_ERROR("ggpo.net: recoverable desync frame=%u local=%u remote=%u reason=%s local_cmd=0x%08X remote_cmd=0x%08X remote_predicted=%d local_frame=%u remote_frame=%u corr_id=%u corr_frame=%u applied_id=%u",
              (unsigned int)frame,
              (unsigned int)local_checksum,
              (unsigned int)remote_checksum,
              why ? why : "checksum mismatch",
              (h->valid && h->frame == frame) ? h->local_cmd : 0u,
              (h->valid && h->frame == frame) ? h->remote_cmd : 0u,
              (h->valid && h->frame == frame) ? h->remote_predicted : -1,
              (unsigned int)g_net.frame,
              g_net.has_remote_frame ? (unsigned int)g_net.remote_frame : 0u,
              (unsigned int)g_net.correction_id,
              (unsigned int)g_net.correction_frame,
              (unsigned int)g_net.last_correction_applied_id);

    /* (ring dump moved above the awaiting-correction guard so both peers dump) */

    if (!g_net.correction_enabled) {
        ggpo_net_mark_desync(frame, local_checksum, remote_checksum, why);
        return;
    }
    if (g_net.mode == GGPO_NET_MODE_HOST) {
        (void)ggpo_net_prepare_host_correction(why);
    } else {
        ggpo_net_begin_awaiting_correction(g_net.frame);
        (void)ggpo_net_request_host_correction(why, 1);
    }
}

static void ggpo_net_note_remote_input(uint32_t frame, uint32_t cmd) {
    uint32_t old_cmd = 0;
    int had_old = ggpo_net_get_input(g_net.remote_inputs, frame, &old_cmd);
    if (had_old && old_cmd == cmd) return;

    if (had_old && frame < g_net.frame) {
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (h->valid && h->frame == frame && !h->remote_predicted) {
            ggpo_net_recoverable_desync(frame, h->post_checksum, 0u, "remote input changed after frame finalized");
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

static void ggpo_net_capture_history_summary(GgpoNetHistoryEntry* h) {
    char err[128];
    if (!h) return;
    /* The rollback summary (a full extra state-save + canonicalize + ~a dozen
     * CRC32s over the whole ~170KB state) is computed EVERY confirmed frame AND
     * again for every rollback-replayed frame, but it is ONLY used for the desync
     * DETAIL log (changed=... breakdown). Desync DETECTION uses the 4-byte
     * post_checksum, not the summary. So gate it behind rngtrace: normal online
     * play skips this per-frame cost entirely (big win on slower PCs, where the
     * extra work dragged the sim below 60fps and both peers into slow-mo), and
     * debugging (rngtrace on) still gets the full detail. Also stops the summary
     * from being packed into every packet (has_summary stays 0). */
    if (!g_net_config_rng_trace) {
        h->has_summary = 0;
        return;
    }
    err[0] = '\0';
    if (lua_manager_game_state_rollback_summary(&h->summary, err, sizeof(err))) {
        h->has_summary = 1;
        return;
    }
    h->has_summary = 0;
    LOG_DEBUG("ggpo.net: rollback summary unavailable frame=%u (%s)",
              (unsigned int)h->frame,
              err[0] ? err : "unknown error");
}

static void ggpo_net_fill_packet(GgpoNetPacket* p, uint16_t type) {
    uint32_t count = 0;
    uint32_t checksum_count = 0;
    uint32_t summary_count = 0;
    uint32_t latest_input_frame = g_net.frame + g_net.input_delay;
    memset(p, 0, sizeof(*p));
    p->magic = GGPO_NET_MAGIC;
    p->version = GGPO_NET_VERSION;
    p->type = type;
    p->session_id = g_net.session_id;
    p->session_echo = g_net.has_remote_session_id ? g_net.remote_session_id : 0u;
    p->confirmed_session_echo =
        (g_net.session_confirmed && g_net.has_remote_session_id)
            ? g_net.remote_session_id
            : 0u;
    p->sender_player = (uint32_t)g_net.local_player;
    p->build_id = g_net.local_build_id;
    p->exe_id = g_net.local_exe_id;
    p->dll_id = g_net.local_dll_id;
    p->correction_ack_checksum = g_net.last_correction_applied_checksum;
    p->correction_id = g_net.correction_id;
    p->correction_ack_id = g_net.last_correction_applied_id;
    p->correction_request_frame = g_net.awaiting_correction ? g_net.correction_request_frame : 0u;
    p->state_epoch = g_net.state_epoch;
    p->prematch_hold = g_net.prematch_hold ? 1u : 0u;
    p->hold_epoch = g_net.hold_epoch;
    p->frame = g_net.frame;
    p->state_size = (uint32_t)g_net.state_size;
    p->state_checksum = g_net.initial_checksum;
    p->last_checksum = g_net.last_checksum;

    p->send_tick = g_net.service_tick;
    p->tick_echo = g_net.peer_last_send_tick;

    /* HELLO is also the transport-only heartbeat used during prematch. Keep all
     * frame input/checksum payloads off it so servicing a held session cannot
     * accidentally seed frame 0 with countdown/menu input. */
    if (type == GGPO_NET_PACKET_INPUT &&
        !g_net.prematch_hold &&
        !(g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) {
        for (uint32_t i = 0; i < GGPO_NET_HISTORY_FRAMES && count < GGPO_NET_PACKET_INPUTS; i++) {
            uint32_t frame = (latest_input_frame >= i) ? (latest_input_frame - i) : UINT_MAX;
            uint32_t cmd = 0;
            if (frame == UINT_MAX) break;
            if (!ggpo_net_get_input(g_net.local_inputs, frame, &cmd)) continue;
            p->inputs[count].frame = frame;
            p->inputs[count].cmd = cmd;
            count++;
        }
        p->input_count = count;

        if (!g_net.correction_active && !g_net.awaiting_correction) {
            for (uint32_t i = 1; i <= GGPO_NET_HISTORY_FRAMES && checksum_count < GGPO_NET_PACKET_CHECKSUMS; i++) {
                uint32_t frame = (g_net.frame >= i) ? (g_net.frame - i) : UINT_MAX;
                GgpoNetHistoryEntry* h = NULL;
                if (frame == UINT_MAX) break;
                if (g_net.last_correction_applied_checksum != 0u && frame < g_net.correction_frame) continue;
                h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
                if (!h->valid || h->frame != frame || h->remote_predicted) continue;
                p->checksums[checksum_count].frame = frame;
                p->checksums[checksum_count].checksum = h->post_checksum;
                checksum_count++;
                if (h->has_summary && summary_count < GGPO_NET_PACKET_SUMMARIES) {
                    p->summaries[summary_count].frame = frame;
                    p->summaries[summary_count].summary = h->summary;
                    summary_count++;
                }
            }
        }
    }
    p->checksum_count = checksum_count;
    p->summary_count = summary_count;
}

static int ggpo_net_send_packet_to(uint16_t type, const struct sockaddr_in* addr) {
    GgpoNetPacket p;
    if (!addr || g_net.sock == INVALID_SOCKET) return 0;
    ggpo_net_fill_packet(&p, type);
    return ggpo_net_send_bytes(&p, (int)sizeof(p), addr, type != GGPO_NET_PACKET_BYE);
}

static int ggpo_net_send_packet(uint16_t type) {
    if (!g_net.has_peer_addr) return 0;
    return ggpo_net_send_packet_to(type, &g_net.peer_addr);
}

/* Before a connection exists, hole-punch by sending HELLO to EVERY candidate
 * endpoint (LAN + public). Whichever one the peer's packets come back from is
 * adopted as peer_addr. Once connected we only talk to the working peer_addr. */
static void ggpo_net_send_handshake_burst(uint32_t count) {
    if (!g_net.active || ggpo_net_link_confirmed() ||
        g_net.sock == INVALID_SOCKET) return;
    if (count == 0u) count = 1u;
    for (uint32_t i = 0; i < count; i++) {
        int sent_any = 0;
        for (int c = 0; c < g_net.candidate_count; c++) {
            if (ggpo_net_send_packet_to(GGPO_NET_PACKET_HELLO, &g_net.candidates[c])) sent_any = 1;
        }
        /* Fall back to peer_addr if no candidates were registered (older path). */
        if (!sent_any && g_net.has_peer_addr) {
            (void)ggpo_net_send_packet_to(GGPO_NET_PACKET_HELLO, &g_net.peer_addr);
        }
    }
}

static void ggpo_net_send_periodic_handshake_burst(void) {
    if (!g_net.active || ggpo_net_link_confirmed() || !g_net.has_peer_addr) return;
    if (g_net.last_handshake_burst_tick != 0u &&
        g_net.service_tick - g_net.last_handshake_burst_tick < GGPO_NET_PUNCH_HELLO_INTERVAL_TICKS) {
        return;
    }
    g_net.last_handshake_burst_tick = ggpo_net_now_tick();
    ggpo_net_send_handshake_burst(GGPO_NET_PUNCH_HELLO_BURST);
}

static int ggpo_net_send_cosmetic_profile(void) {
#if !GGPO_NET_ENABLE_COSMETICS
    return 0;
#endif
    GgpoNetCosmeticPacket p;
    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    if (g_net.local_cosmetic_profile_len == 0u ||
        g_net.local_cosmetic_profile_len > GGPO_NET_COSMETIC_PROFILE_BYTES) {
        return 0;
    }

    memset(&p, 0, sizeof(p));
    p.magic = GGPO_NET_MAGIC;
    p.version = GGPO_NET_VERSION;
    p.type = GGPO_NET_PACKET_COSMETICS;
    p.session_id = g_net.session_id;
    p.sender_player = (uint32_t)g_net.local_player;
    p.build_id = g_net.local_build_id;
    p.exe_id = g_net.local_exe_id;
    p.dll_id = g_net.local_dll_id;
    p.profile_revision = g_net.local_cosmetic_profile_revision;
    p.asset_ack_revision = g_net.remote_cosmetic_asset_applied_revision;
    p.profile_len = g_net.local_cosmetic_profile_len;
    memcpy(p.profile, g_net.local_cosmetic_profile, g_net.local_cosmetic_profile_len);
    g_net.last_cosmetic_profile_send_tick = g_net.service_tick;
    return ggpo_net_send_bytes(&p,
                               (int)(offsetof(GgpoNetCosmeticPacket, profile) + p.profile_len),
                               &g_net.peer_addr,
                               1);
}

static void ggpo_net_send_cosmetic_profile_periodic(void) {
#if !GGPO_NET_ENABLE_COSMETICS
    return;
#endif
    if (!g_net.active || !g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return;
    if (g_net.local_cosmetic_profile_len == 0u) return;
    if (g_net.last_cosmetic_profile_send_tick != 0u &&
        g_net.service_tick - g_net.last_cosmetic_profile_send_tick < 30u) {
        return;
    }
    (void)ggpo_net_send_cosmetic_profile();
}

static int ggpo_net_send_cosmetic_asset_chunk(uint32_t chunk_index) {
#if !GGPO_NET_ENABLE_COSMETICS
    (void)chunk_index;
    return 0;
#endif
    GgpoNetCosmeticAssetChunkPacket p;
    uint32_t chunk_count = 0;
    uint32_t chunk_size = 0;
    uint32_t offset = 0;
    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    if (!g_net.local_cosmetic_asset || g_net.local_cosmetic_asset_len == 0u) return 0;
    if (g_net.local_cosmetic_asset_peer_applied_revision == g_net.local_cosmetic_asset_revision &&
        g_net.local_cosmetic_asset_revision != 0u) {
        return 0;
    }

    chunk_count = ggpo_net_cosmetic_asset_chunk_count(g_net.local_cosmetic_asset_len);
    if (chunk_count == 0u) return 0;
    chunk_index %= chunk_count;
    chunk_size = ggpo_net_cosmetic_asset_chunk_size(g_net.local_cosmetic_asset_len, chunk_index);
    if (chunk_size == 0u) return 0;
    offset = ggpo_net_cosmetic_asset_chunk_offset(chunk_index);

    memset(&p, 0, sizeof(p));
    p.magic = GGPO_NET_MAGIC;
    p.version = GGPO_NET_VERSION;
    p.type = GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK;
    p.session_id = g_net.session_id;
    p.sender_player = (uint32_t)g_net.local_player;
    p.build_id = g_net.local_build_id;
    p.exe_id = g_net.local_exe_id;
    p.dll_id = g_net.local_dll_id;
    p.asset_revision = g_net.local_cosmetic_asset_revision;
    p.asset_len = g_net.local_cosmetic_asset_len;
    p.chunk_index = chunk_index;
    p.chunk_count = chunk_count;
    p.chunk_size = chunk_size;
    memcpy(p.asset_id, g_net.local_cosmetic_asset_id, sizeof(p.asset_id));
    memcpy(p.data, g_net.local_cosmetic_asset + offset, chunk_size);
    return ggpo_net_send_bytes(&p,
                               (int)(offsetof(GgpoNetCosmeticAssetChunkPacket, data) + chunk_size),
                               &g_net.peer_addr,
                               1);
}

static void ggpo_net_send_cosmetic_asset_periodic(void) {
#if !GGPO_NET_ENABLE_COSMETICS
    return;
#endif
    uint32_t chunk_count = 0;
    if (!g_net.active || !g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return;
    if (!g_net.local_cosmetic_asset || g_net.local_cosmetic_asset_len == 0u) return;
    if (g_net.local_cosmetic_asset_peer_applied_revision == g_net.local_cosmetic_asset_revision &&
        g_net.local_cosmetic_asset_revision != 0u) {
        return;
    }
    if (g_net.local_cosmetic_asset_last_send_tick != 0u &&
        g_net.service_tick - g_net.local_cosmetic_asset_last_send_tick < 2u) {
        return;
    }

    chunk_count = ggpo_net_cosmetic_asset_chunk_count(g_net.local_cosmetic_asset_len);
    if (chunk_count == 0u) return;
    for (uint32_t i = 0; i < GGPO_NET_COSMETIC_ASSET_BURST_CHUNKS; i++) {
        (void)ggpo_net_send_cosmetic_asset_chunk(g_net.local_cosmetic_asset_next_chunk);
        g_net.local_cosmetic_asset_next_chunk = (g_net.local_cosmetic_asset_next_chunk + 1u) % chunk_count;
    }
    g_net.local_cosmetic_asset_last_send_tick = g_net.service_tick;
}

static int ggpo_net_cosmetic_profiles_ready(void) {
    if (g_net.local_cosmetic_profile_len == 0u) return 1;
    if (g_net.remote_cosmetic_profile_len == 0u || g_net.remote_cosmetic_profile_revision == 0u) return 0;
    if (g_net.remote_cosmetic_profile_applied_revision != g_net.remote_cosmetic_profile_revision) return 0;
    return 1;
}

static int ggpo_net_wait_for_cosmetic_profiles(uint32_t* out_checksum) {
#if !GGPO_NET_ENABLE_COSMETICS
    (void)out_checksum;
    return 0;
#endif
    if (ggpo_net_cosmetic_profiles_ready()) {
        g_net.cosmetic_wait_start_tick = 0u;
        g_net.cosmetic_wait_cap_announced = 0;
        return 0;
    }

    if (g_net.cosmetic_wait_start_tick == 0u) {
        g_net.cosmetic_wait_start_tick = ggpo_net_now_tick();
    }
    ggpo_net_send_cosmetic_profile_periodic();
    ggpo_net_send_cosmetic_asset_periodic();

    if (g_net.service_tick - g_net.cosmetic_wait_start_tick < GGPO_NET_COSMETIC_SYNC_WAIT_TICKS) {
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (!g_net.cosmetic_wait_cap_announced) {
        LOG_WARN("ggpo.net: cosmetic profile sync wait exceeded %u ticks; starting without confirmed cosmetics local_bytes=%u remote_bytes=%u remote_rev=%u applied_rev=%u",
                 (unsigned int)GGPO_NET_COSMETIC_SYNC_WAIT_TICKS,
                 (unsigned int)g_net.local_cosmetic_profile_len,
                 (unsigned int)g_net.remote_cosmetic_profile_len,
                 (unsigned int)g_net.remote_cosmetic_profile_revision,
                 (unsigned int)g_net.remote_cosmetic_profile_applied_revision);
        g_net.cosmetic_wait_cap_announced = 1;
    }
    return 0;
}

static int ggpo_net_send_state_ack(void) {
    GgpoNetPacket p;
    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    ggpo_net_fill_packet(&p, GGPO_NET_PACKET_STATE_ACK);
    return ggpo_net_send_bytes(&p, (int)sizeof(p), &g_net.peer_addr, 1);
}

static int ggpo_net_send_state_chunk_from(const uint8_t* state,
                                          size_t state_len,
                                          uint32_t state_checksum,
                                          uint32_t state_frame,
                                          uint32_t flags,
                                          uint32_t state_id,
                                          uint32_t* inout_offset) {
    GgpoNetStateChunkPacket p;
    uint32_t chunk;
    uint32_t offset;
    uint32_t chunk_index;
    uint32_t chunk_count;

    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    if (!state || state_len == 0 || state_len > (size_t)UINT_MAX || !inout_offset) return 0;
    if (*inout_offset >= (uint32_t)state_len) {
        *inout_offset = 0;
    }

    offset = *inout_offset;
    chunk_index = offset / GGPO_NET_STATE_CHUNK_BYTES;
    chunk_count = ggpo_net_state_chunk_count((uint32_t)state_len);
    chunk = ggpo_net_state_chunk_size(state_len, chunk_index);
    if (chunk == 0u) return 0;

    memset(&p, 0, sizeof(p));
    p.magic = GGPO_NET_MAGIC;
    p.version = GGPO_NET_VERSION;
    p.type = GGPO_NET_PACKET_STATE_CHUNK;
    p.session_id = g_net.session_id;
    p.state_epoch = g_net.state_epoch;
    p.sender_player = (uint32_t)g_net.local_player;
    p.build_id = g_net.local_build_id;
    p.exe_id = g_net.local_exe_id;
    p.dll_id = g_net.local_dll_id;
    p.frame = state_frame;
    p.flags = flags;
    p.correction_id = state_id;
    p.base_checksum = 0u;
    p.full_chunk_count = chunk_count;
    p.chunk_index = chunk_index;
    p.chunk_count = chunk_count;
    p.state_size = (uint32_t)state_len;
    p.state_checksum = state_checksum;
    p.offset = offset;
    p.chunk_size = chunk;
    memcpy(p.data, state + offset, chunk);

    if (!ggpo_net_send_bytes(&p, (int)(offsetof(GgpoNetStateChunkPacket, data) + chunk), &g_net.peer_addr, 1)) {
        return 0;
    }

    *inout_offset = offset + chunk;
    if (*inout_offset >= (uint32_t)state_len) {
        *inout_offset = 0;
    }
    return 1;
}

static int ggpo_net_send_delta_state_chunk_from(const uint8_t* base,
                                                const uint8_t* state,
                                                size_t state_len,
                                                uint32_t state_checksum,
                                                uint32_t base_checksum,
                                                uint32_t state_frame,
                                                uint32_t state_id,
                                                uint32_t changed_chunk_count,
                                                uint32_t* inout_next_chunk) {
    GgpoNetStateChunkPacket p;
    uint32_t full_chunk_count;
    uint32_t chunk_index = UINT_MAX;
    uint32_t offset = 0;
    uint32_t chunk = 0;

    if (!g_net.has_peer_addr || g_net.sock == INVALID_SOCKET) return 0;
    if (!base || !state || state_len == 0 || state_len > (size_t)UINT_MAX || !inout_next_chunk) return 0;
    full_chunk_count = ggpo_net_state_chunk_count((uint32_t)state_len);
    if (full_chunk_count == 0u || changed_chunk_count == 0u || changed_chunk_count > full_chunk_count) return 0;
    if (*inout_next_chunk >= full_chunk_count) {
        *inout_next_chunk = 0;
    }

    for (uint32_t scanned = 0; scanned < full_chunk_count; scanned++) {
        uint32_t candidate = (*inout_next_chunk + scanned) % full_chunk_count;
        if (ggpo_net_state_chunk_differs(base, state, state_len, candidate)) {
            chunk_index = candidate;
            break;
        }
    }
    if (chunk_index == UINT_MAX) return 0;

    offset = ggpo_net_state_chunk_offset(chunk_index);
    chunk = ggpo_net_state_chunk_size(state_len, chunk_index);
    if (chunk == 0u) return 0;

    memset(&p, 0, sizeof(p));
    p.magic = GGPO_NET_MAGIC;
    p.version = GGPO_NET_VERSION;
    p.type = GGPO_NET_PACKET_STATE_CHUNK;
    p.session_id = g_net.session_id;
    p.state_epoch = g_net.state_epoch;
    p.sender_player = (uint32_t)g_net.local_player;
    p.build_id = g_net.local_build_id;
    p.exe_id = g_net.local_exe_id;
    p.dll_id = g_net.local_dll_id;
    p.frame = state_frame;
    p.flags = GGPO_NET_STATE_FLAG_CORRECTION | GGPO_NET_STATE_FLAG_DELTA;
    p.correction_id = state_id;
    p.base_checksum = base_checksum;
    p.full_chunk_count = full_chunk_count;
    p.chunk_index = chunk_index;
    p.chunk_count = changed_chunk_count;
    p.state_size = (uint32_t)state_len;
    p.state_checksum = state_checksum;
    p.offset = offset;
    p.chunk_size = chunk;
    memcpy(p.data, state + offset, chunk);

    if (!ggpo_net_send_bytes(&p, (int)(offsetof(GgpoNetStateChunkPacket, data) + chunk), &g_net.peer_addr, 1)) {
        return 0;
    }

    *inout_next_chunk = chunk_index + 1u;
    if (*inout_next_chunk >= full_chunk_count) {
        *inout_next_chunk = 0;
    }
    return 1;
}

static int ggpo_net_send_state_chunk(void) {
    if (!g_net.initial_state || g_net.initial_state_len == 0) return 0;
    if (!ggpo_net_send_state_chunk_from(g_net.initial_state,
                                        g_net.initial_state_len,
                                        g_net.initial_checksum,
                                        0u,
                                        0u,
                                        0u,
                                        &g_net.state_send_offset)) {
        return 0;
    }
    g_net.state_sync_chunks_sent++;
    return 1;
}

static int ggpo_net_send_correction_chunk(void) {
    if (!g_net.correction_active || !g_net.correction_state || g_net.correction_state_len == 0) return 0;
    if (g_net.correction_send_delta) {
        if (!ggpo_net_send_delta_state_chunk_from(g_net.correction_base_state,
                                                  g_net.correction_state,
                                                  g_net.correction_state_len,
                                                  g_net.correction_checksum,
                                                  g_net.correction_send_base_checksum,
                                                  g_net.correction_frame,
                                                  g_net.correction_id,
                                                  g_net.correction_send_chunk_count,
                                                  &g_net.correction_send_next_chunk)) {
            return 0;
        }
        g_net.correction_chunks_sent++;
        g_net.correction_delta_chunks_sent++;
        return 1;
    }
    if (!ggpo_net_send_state_chunk_from(g_net.correction_state,
                                        g_net.correction_state_len,
                                        g_net.correction_checksum,
                                        g_net.correction_frame,
                                        GGPO_NET_STATE_FLAG_CORRECTION,
                                        g_net.correction_id,
                                        &g_net.correction_send_offset)) {
        return 0;
    }
    g_net.correction_chunks_sent++;
    g_net.correction_full_chunks_sent++;
    return 1;
}

static void ggpo_net_send_state_sync_burst(void) {
    if (g_net.mode != GGPO_NET_MODE_HOST) return;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return;
    if (!ggpo_net_link_confirmed() || g_net.remote_state_synced) return;
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
    g_net.prediction_limit_wait_announced = 0;
    g_net.frame_advantage_wait_start_tick = 0;
    g_net.prediction_wait_start_tick = 0;
    g_net.frame_advantage_wait_cap_announced = 0;
    g_net.prediction_wait_cap_announced = 0;
    /* Drop the sim-only seed snapshot: after a state reset the next tick must use
     * the freshly loaded/synced seed, not a stale pre-reset snapshot. */
    g_net.have_clean_mrand_seed = 0;
}

static void ggpo_net_clear_rollback_history(void) {
    memset(g_net.history, 0, sizeof(g_net.history));
    g_net.rollback_to = 0;
    g_net.rollback_pending = 0;
    g_net.frame_advantage_wait_announced = 0;
    g_net.prediction_limit_wait_announced = 0;
    g_net.frame_advantage_wait_start_tick = 0;
    g_net.prediction_wait_start_tick = 0;
    g_net.frame_advantage_wait_cap_announced = 0;
    g_net.prediction_wait_cap_announced = 0;
    g_net.warned_prediction_limit = 0;
    g_net.desync_detected = 0;
}

static void ggpo_net_reset_recv_state(void) {
    free(g_net.recv_state);
    free(g_net.recv_state_chunks_seen);
    g_net.recv_state = NULL;
    g_net.recv_state_chunks_seen = NULL;
    g_net.recv_state_len = 0;
    g_net.recv_state_checksum = 0;
    g_net.recv_state_frame = 0;
    g_net.recv_state_flags = 0;
    g_net.recv_state_epoch = 0;
    g_net.recv_state_id = 0;
    g_net.recv_state_base_checksum = 0;
    g_net.recv_state_seen_chunk_count = 0;
    g_net.recv_state_chunk_count = 0;
    g_net.recv_state_chunks_complete = 0;
    g_net.state_sync_chunks_received = 0;
}

/* Reset every piece of gameplay/state-transfer bookkeeping that is scoped to
 * one authoritative frame-0 state, while preserving the live socket/session,
 * endpoint candidates, handshake proof, RTT estimate and user configuration. */
static void ggpo_net_reset_authoritative_state_bookkeeping(void) {
    ggpo_net_reset_recv_state();
    ggpo_net_clear_runtime_history();
    g_net.frame = 0u;
    g_net.state_send_offset = 0u;
    g_net.state_sync_chunks_sent = 0u;
    g_net.correction_state_len = 0u;
    g_net.correction_base_state_len = 0u;
    g_net.correction_base_checksum = 0u;
    g_net.correction_send_offset = 0u;
    g_net.correction_send_next_chunk = 0u;
    g_net.correction_send_chunk_count = 0u;
    g_net.correction_send_base_checksum = 0u;
    g_net.correction_id = 0u;
    g_net.correction_frame = 0u;
    g_net.correction_checksum = 0u;
    g_net.correction_request_frame = 0u;
    g_net.correction_wait_start_tick = 0u;
    g_net.last_correction_ack_checksum = 0u;
    g_net.last_correction_ack_id = 0u;
    g_net.last_correction_applied_checksum = 0u;
    g_net.last_correction_applied_id = 0u;
    g_net.last_resync_request_tick = 0u;
    g_net.correction_active = 0;
    g_net.correction_send_delta = 0;
    g_net.awaiting_correction = 0;
    g_net.correction_wait_cap_announced = 0;
    g_net.state_sync_announced = 0;
    g_net.start_state_loaded = 0;
    g_net.frame0_wait_announced = 0;
    g_net.warned_initial_mismatch = 0;
    g_net.warned_prediction_limit = 0;
    g_net.desync_detected = 0;
    g_net.desync_frame = 0u;
    g_net.desync_local_checksum = 0u;
    g_net.desync_remote_checksum = 0u;
    g_net.cosmetic_wait_start_tick = 0u;
    g_net.cosmetic_wait_cap_announced = 0;
    g_net.auto_input_delay_applied = 0;
    memset(g_rng_ring, 0, sizeof(g_rng_ring));
    g_rng_dump_count = 0u;
    g_rng_last_dump_frame = 0xFFFFFFFFu;
}

static int ggpo_net_recapture_host_start_state(char* err, size_t err_cap) {
    size_t state_len = 0;
    uint32_t checksum = 0;
    uint32_t next_epoch;

    if (g_net.mode != GGPO_NET_MODE_HOST) {
        ggpo_net_set_err(err, err_cap, "only the host can publish a start state");
        return 0;
    }
    if (g_net.state_epoch == UINT_MAX) {
        ggpo_net_set_err(err, err_cap, "state epoch exhausted");
        return 0;
    }
    next_epoch = g_net.state_epoch + 1u;
    if (next_epoch <= GGPO_NET_INITIAL_STATE_EPOCH) {
        next_epoch = GGPO_NET_INITIAL_STATE_EPOCH + 1u;
    }

    /* Save into history storage first. Until this succeeds, the old published
     * state and the engaged hold remain untouched, making release transactional. */
    if (!ggpo_ext_save_game_state(g_net.state_blobs,
                                  g_net.state_size,
                                  &state_len,
                                  &checksum,
                                  err,
                                  err_cap)) {
        return 0;
    }
    if (state_len == 0u || state_len > g_net.state_size) {
        ggpo_net_set_err(err, err_cap, "captured start state has invalid size");
        return 0;
    }

    memcpy(g_net.initial_state, g_net.state_blobs, state_len);
    g_net.initial_state_len = state_len;
    g_net.initial_checksum = checksum;
    g_net.last_checksum = checksum;
    g_net.state_epoch = next_epoch;
    ggpo_net_reset_authoritative_state_bookkeeping();
    g_net.state_synced = 1;
    g_net.remote_state_synced = 0;
    if (!ggpo_net_set_correction_base(g_net.initial_state,
                                      g_net.initial_state_len,
                                      checksum)) {
        ggpo_net_set_err(err, err_cap, "failed to seed correction base");
        return 0;
    }
    return 1;
}

static int ggpo_net_prematch_invariants_ok(const char* where) {
    int ok = 1;
    if (!g_net.active) return 1;
    if (g_net.prematch_hold &&
        (g_net.frame != 0u || g_net.start_state_loaded || g_net.recv_state != NULL)) {
        ok = 0;
    }
    if (g_net.prematch_used &&
        !g_net.prematch_hold &&
        g_net.mode == GGPO_NET_MODE_HOST &&
        g_net.state_epoch <= GGPO_NET_INITIAL_STATE_EPOCH) {
        ok = 0;
    }
    if (g_net.prematch_used &&
        !g_net.prematch_hold &&
        g_net.mode == GGPO_NET_MODE_JOIN &&
        !g_net.state_synced &&
        g_net.state_epoch != 0u) {
        ok = 0;
    }
    if (g_net.prematch_used &&
        g_net.mode == GGPO_NET_MODE_JOIN &&
        g_net.state_synced &&
        g_net.state_epoch <= g_net.hold_remote_epoch_floor) {
        ok = 0;
    }
    if (!ok) {
        LOG_ERROR("ggpo.net: prematch invariant failed at %s mode=%s hold=%d frame=%u loaded=%d state_synced=%d state_epoch=%u floor=%u recv=%s",
                  where ? where : "unknown",
                  ggpo_net_mode_name(),
                  g_net.prematch_hold,
                  (unsigned int)g_net.frame,
                  g_net.start_state_loaded,
                  g_net.state_synced,
                  (unsigned int)g_net.state_epoch,
                  (unsigned int)g_net.hold_remote_epoch_floor,
                  g_net.recv_state ? "yes" : "no");
    }
    return ok;
}

static void ggpo_net_reset_remote_cosmetic_asset(void) {
    free(g_net.remote_cosmetic_asset);
    free(g_net.remote_cosmetic_asset_chunks_seen);
    g_net.remote_cosmetic_asset = NULL;
    g_net.remote_cosmetic_asset_chunks_seen = NULL;
    memset(g_net.remote_cosmetic_asset_id, 0, sizeof(g_net.remote_cosmetic_asset_id));
    g_net.remote_cosmetic_asset_len = 0;
    g_net.remote_cosmetic_asset_revision = 0;
    g_net.remote_cosmetic_asset_applied_revision = 0;
    g_net.remote_cosmetic_asset_chunk_count = 0;
    g_net.remote_cosmetic_asset_seen_chunk_count = 0;
    g_net.remote_cosmetic_asset_complete = 0;
}

static void ggpo_net_handle_state_chunk(const GgpoNetStateChunkPacket* p, int got_len, const struct sockaddr_in* from) {
    uint32_t checksum = 0;
    uint32_t flags = 0;
    uint32_t chunks_received = 0;
    uint32_t expected_full_chunk_count = 0;
    uint32_t expected_offset = 0;
    uint32_t expected_chunk_size = 0;
    int is_correction = 0;
    int is_delta = 0;
    const uint8_t* delta_base = NULL;
    char err[256];

    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->type != GGPO_NET_PACKET_STATE_CHUNK) return;
    if (g_net.mode != GGPO_NET_MODE_JOIN) return;
    if ((int)p->sender_player != g_net.remote_player) return;
    if (!ggpo_net_accept_packet_source(from, p->session_id, "state", 0)) return;
    if (!ggpo_net_link_confirmed()) return;
    if (p->state_epoch == 0u) return;
    if (p->state_size == 0 || p->state_size > (uint32_t)g_net.state_size) return;
    if (p->chunk_size == 0 || p->chunk_size > GGPO_NET_STATE_CHUNK_BYTES) return;
    if (got_len < (int)(offsetof(GgpoNetStateChunkPacket, data) + p->chunk_size)) return;
    if (p->offset >= p->state_size || p->offset + p->chunk_size > p->state_size) return;
    expected_full_chunk_count = ggpo_net_state_chunk_count(p->state_size);
    if (p->full_chunk_count != expected_full_chunk_count) return;
    if (p->chunk_index >= expected_full_chunk_count) return;
    expected_offset = p->chunk_index * GGPO_NET_STATE_CHUNK_BYTES;
    expected_chunk_size = p->state_size - expected_offset;
    if (expected_chunk_size > GGPO_NET_STATE_CHUNK_BYTES) expected_chunk_size = GGPO_NET_STATE_CHUNK_BYTES;
    if (p->offset != expected_offset || p->chunk_size != expected_chunk_size) return;

    ggpo_net_note_remote_fingerprint(p->build_id, p->exe_id, p->dll_id);
    g_net.last_rx_tick = g_net.service_tick;
    g_net.packets_received++;

    /* A held join peer must not even assemble a transfer: the final map/reset
     * state does not exist yet, and a completed old transfer would mutate live
     * hub memory. The host repeats fresh chunks after both hold flags clear. */
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) {
        g_net.held_state_chunks_dropped++;
        if (g_net.held_state_chunks_dropped == 1u ||
            (g_net.held_state_chunks_dropped % 128u) == 0u) {
            LOG_DEBUG("ggpo.net: quarantined prematch state chunk epoch=%u index=%u dropped=%u local_hold=%d remote_hold=%d",
                      (unsigned int)p->state_epoch,
                      (unsigned int)p->chunk_index,
                      (unsigned int)g_net.held_state_chunks_dropped,
                      g_net.prematch_hold,
                      g_net.remote_prematch_hold_known ? g_net.remote_prematch_hold : -1);
        }
        return;
    }

    flags = p->flags & (GGPO_NET_STATE_FLAG_CORRECTION | GGPO_NET_STATE_FLAG_DELTA);
    is_correction = (flags & GGPO_NET_STATE_FLAG_CORRECTION) ? 1 : 0;
    is_delta = (flags & GGPO_NET_STATE_FLAG_DELTA) ? 1 : 0;
    if (is_correction) {
        /* Corrections are valid only within the already-applied authoritative
         * epoch. An old correction can otherwise overwrite a newly published
         * prematch frame-0 state. */
        if (!g_net.state_synced ||
            g_net.state_epoch == 0u ||
            p->state_epoch != g_net.state_epoch) {
            g_net.stale_state_chunks_dropped++;
            return;
        }
    } else {
        /* Prematch release requires a strictly newer host epoch than the one
         * that existed when the hold began. Also reject any datagram older than
         * a host epoch already advertised by a normal handshake packet. */
        if (p->state_epoch <= g_net.hold_remote_epoch_floor ||
            (g_net.remote_state_epoch_seen != 0u &&
             p->state_epoch < g_net.remote_state_epoch_seen) ||
            (g_net.state_synced && g_net.state_epoch != p->state_epoch) ||
            (g_net.state_epoch != 0u && p->state_epoch < g_net.state_epoch)) {
            g_net.stale_state_chunks_dropped++;
            if (g_net.stale_state_chunks_dropped == 1u ||
                (g_net.stale_state_chunks_dropped % 128u) == 0u) {
                LOG_DEBUG("ggpo.net: rejected stale state chunk epoch=%u accepted=%u seen=%u floor=%u dropped=%u",
                          (unsigned int)p->state_epoch,
                          (unsigned int)g_net.state_epoch,
                          (unsigned int)g_net.remote_state_epoch_seen,
                          (unsigned int)g_net.hold_remote_epoch_floor,
                          (unsigned int)g_net.stale_state_chunks_dropped);
            }
            return;
        }
        if (p->state_epoch > g_net.remote_state_epoch_seen) {
            g_net.remote_state_epoch_seen = p->state_epoch;
        }
    }
    if (is_correction && !g_net.correction_enabled) return;
    if (is_delta && !is_correction) return;
    if (is_correction &&
        g_net.last_correction_applied_id != 0u &&
        p->correction_id <= g_net.last_correction_applied_id) {
        (void)ggpo_net_send_state_ack();
        return;
    }
    if (is_delta) {
        if (p->chunk_count == 0u || p->chunk_count > expected_full_chunk_count) return;
        if (p->base_checksum == g_net.correction_base_checksum &&
            g_net.correction_base_state &&
            g_net.correction_base_state_len == p->state_size) {
            delta_base = g_net.correction_base_state;
        } else if (p->base_checksum == g_net.initial_checksum &&
                   g_net.initial_state &&
                   g_net.initial_state_len == p->state_size) {
            delta_base = g_net.initial_state;
        } else {
            LOG_WARN("ggpo.net: cannot apply delta correction id=%u base=%u local_base=%u initial=%u; requesting full correction",
                     (unsigned int)p->correction_id,
                     (unsigned int)p->base_checksum,
                     (unsigned int)g_net.correction_base_checksum,
                     (unsigned int)g_net.initial_checksum);
            ggpo_net_begin_awaiting_correction(g_net.frame);
            (void)ggpo_net_request_host_correction("delta base mismatch", 1);
            return;
        }
    } else {
        if (p->chunk_count != expected_full_chunk_count) return;
    }

    if (!is_correction &&
        g_net.state_synced &&
        p->state_epoch == g_net.state_epoch &&
        p->state_checksum == g_net.initial_checksum) {
        (void)ggpo_net_send_state_ack();
        return;
    }

    if (!g_net.recv_state ||
        g_net.recv_state_len != p->state_size ||
        g_net.recv_state_checksum != p->state_checksum ||
        g_net.recv_state_frame != p->frame ||
        g_net.recv_state_flags != flags ||
        g_net.recv_state_epoch != p->state_epoch ||
        g_net.recv_state_id != p->correction_id ||
        g_net.recv_state_base_checksum != p->base_checksum ||
        g_net.recv_state_seen_chunk_count != expected_full_chunk_count ||
        g_net.recv_state_chunk_count != p->chunk_count) {
        ggpo_net_reset_recv_state();
        g_net.recv_state = (uint8_t*)calloc(1, p->state_size);
        g_net.recv_state_chunks_seen = (uint8_t*)calloc(1, expected_full_chunk_count);
        if (!g_net.recv_state || !g_net.recv_state_chunks_seen) {
            ggpo_net_reset_recv_state();
            return;
        }
        if (is_delta) {
            memcpy(g_net.recv_state, delta_base, p->state_size);
        }
        g_net.recv_state_len = p->state_size;
        g_net.recv_state_checksum = p->state_checksum;
        g_net.recv_state_frame = p->frame;
        g_net.recv_state_flags = flags;
        g_net.recv_state_epoch = p->state_epoch;
        g_net.recv_state_id = p->correction_id;
        g_net.recv_state_base_checksum = p->base_checksum;
        g_net.recv_state_seen_chunk_count = expected_full_chunk_count;
        g_net.recv_state_chunk_count = p->chunk_count;
        g_net.recv_state_chunks_complete = 0;
        LOG_INFO("ggpo.net: receiving %s%s state epoch=%u id=%u frame=%u size=%u checksum=%u chunks=%u/%u",
                 is_correction ? "correction" : "host",
                 is_delta ? " delta" : "",
                 (unsigned int)p->state_epoch,
                 (unsigned int)p->correction_id,
                 (unsigned int)p->frame,
                 (unsigned int)p->state_size,
                 (unsigned int)p->state_checksum,
                 (unsigned int)p->chunk_count,
                 (unsigned int)expected_full_chunk_count);
    }

    if (g_net.recv_state_chunks_seen[p->chunk_index]) {
        g_net.duplicate_state_chunks++;
        return;
    }
    memcpy(g_net.recv_state + p->offset, p->data, p->chunk_size);
    g_net.recv_state_chunks_seen[p->chunk_index] = 1;
    g_net.recv_state_chunks_complete++;
    if (is_correction) {
        g_net.correction_chunks_received++;
        if (is_delta) {
            g_net.correction_delta_chunks_received++;
        } else {
            g_net.correction_full_chunks_received++;
        }
    } else {
        g_net.state_sync_chunks_received++;
    }

    if (g_net.recv_state_chunks_complete < g_net.recv_state_chunk_count) {
        return;
    }

    err[0] = '\0';
    if (!ggpo_ext_load_game_state(g_net.recv_state, g_net.recv_state_len, err, sizeof(err))) {
        LOG_ERROR("ggpo.net: host state load failed (%s)", err[0] ? err : "unknown error");
        /* A completed receive buffer otherwise becomes a permanent dead end:
         * every repeated host chunk is classified as a duplicate, so the load
         * is never attempted again and prematch readiness can wait forever.
         * Drop the failed assembly and let the host's cyclic state burst build
         * a fresh transfer on the next service ticks. */
        if (!is_correction) {
            g_net.state_synced = 0;
            g_net.start_state_loaded = 0;
            g_net.state_sync_announced = 0;
        }
        ggpo_net_reset_recv_state();
        return;
    }
    if (!lua_manager_game_state_rollback_checksum(&checksum, err, sizeof(err))) {
        LOG_ERROR("ggpo.net: host state checksum failed (%s)", err[0] ? err : "unknown error");
        if (!is_correction) {
            g_net.state_synced = 0;
            g_net.start_state_loaded = 0;
            g_net.state_sync_announced = 0;
        }
        ggpo_net_reset_recv_state();
        return;
    }
    if (checksum != p->state_checksum) {
        if (is_correction) {
            LOG_ERROR("ggpo.net: correction transfer checksum mismatch id=%u local=%u remote=%u frame=%u",
                      (unsigned int)p->correction_id,
                      (unsigned int)checksum,
                      (unsigned int)p->state_checksum,
                      (unsigned int)p->frame);
            ggpo_net_begin_awaiting_correction(g_net.frame);
            (void)ggpo_net_request_host_correction("correction transfer checksum mismatch", 1);
            ggpo_net_reset_recv_state();
            return;
        }
        LOG_WARN("ggpo.net: host state transfer checksum mismatch local=%u remote=%u frame=%u; waiting for resend",
                 (unsigned int)checksum,
                 (unsigned int)p->state_checksum,
                 (unsigned int)p->frame);
        g_net.state_synced = 0;
        g_net.start_state_loaded = 0;
        g_net.state_sync_announced = 0;
        ggpo_net_reset_recv_state();
        return;
    }

    if (is_correction) {
        chunks_received = g_net.recv_state_chunks_complete;
        /* Responder-side ring dump: if we are applying a correction we did NOT
         * request (the host detected the desync and pushed it), we never ran our
         * own detect/dump. Dump our ring before clearing rollback history so both
         * peers' rings for this round reach mods/desync_dump.log. (If we DID
         * request it, awaiting_correction is set and recoverable_desync already
         * dumped.) */
        if (!g_net.awaiting_correction && p->frame != g_rng_last_dump_frame) {
            g_rng_last_dump_frame = p->frame;
            ggpo_net_dump_rng_ring(p->frame);
        }
        ggpo_net_clear_rollback_history();
        /* A correction loads a fresh synced state; drop the sim-only seed snapshot
         * so the next tick uses the corrected seed, not a stale pre-correction one. */
        g_net.have_clean_mrand_seed = 0;
        g_net.frame = p->frame;
        g_net.correction_id = p->correction_id;
        g_net.correction_frame = p->frame;
        ggpo_net_seed_local_input_delay_from(g_net.frame);
        g_net.last_checksum = checksum;
        g_net.awaiting_correction = 0;
        g_net.correction_request_frame = 0;
        g_net.correction_wait_start_tick = 0;
        g_net.correction_wait_cap_announced = 0;
        g_net.last_resync_request_tick = 0;
        g_net.last_correction_applied_checksum = checksum;
        g_net.last_correction_applied_id = p->correction_id;
        (void)ggpo_net_set_correction_base(g_net.recv_state, g_net.recv_state_len, checksum);
        g_net.corrections_received++;
        LOG_WARN("ggpo.net: correction applied id=%u frame=%u checksum=%u chunks=%u/%u mode=%s total=%u dup=%u",
                 (unsigned int)p->correction_id,
                 (unsigned int)g_net.frame,
                 (unsigned int)checksum,
                 (unsigned int)chunks_received,
                 (unsigned int)g_net.recv_state_chunk_count,
                 is_delta ? "delta" : "full",
                 (unsigned int)g_net.corrections_received,
                 (unsigned int)g_net.duplicate_state_chunks);
        (void)ggpo_net_send_state_ack();
        ggpo_net_reset_recv_state();
        return;
    }

    memcpy(g_net.initial_state, g_net.recv_state, g_net.recv_state_len);
    memcpy(g_net.state_blobs, g_net.recv_state, g_net.recv_state_len);
    ggpo_net_clear_runtime_history();
    g_net.initial_state_len = g_net.recv_state_len;
    g_net.initial_checksum = checksum;
    g_net.last_checksum = checksum;
    g_net.state_epoch = p->state_epoch;
    if (p->state_epoch > g_net.remote_state_epoch_seen) {
        g_net.remote_state_epoch_seen = p->state_epoch;
    }
    (void)ggpo_net_set_correction_base(g_net.initial_state, g_net.initial_state_len, checksum);
    g_net.frame = 0;
    g_net.state_synced = 1;
    g_net.remote_state_synced = 1;
    g_net.start_state_loaded = 0;
    LOG_INFO("ggpo.net: host state synced epoch=%u size=%u checksum=%u chunks=%u",
             (unsigned int)g_net.state_epoch,
             (unsigned int)g_net.initial_state_len,
             (unsigned int)g_net.initial_checksum,
             (unsigned int)g_net.state_sync_chunks_received);
    (void)ggpo_net_send_state_ack();
    ggpo_net_reset_recv_state();
    (void)ggpo_net_prematch_invariants_ok("state-sync-complete");
}

static void ggpo_net_handle_cosmetic_packet(const GgpoNetCosmeticPacket* p, int got_len, const struct sockaddr_in* from) {
#if !GGPO_NET_ENABLE_COSMETICS
    (void)p;
    (void)got_len;
    (void)from;
    return;
#endif
    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->type != GGPO_NET_PACKET_COSMETICS) return;
    if (p->sender_player > 1u || (int)p->sender_player == g_net.local_player) return;
    if (p->profile_len > GGPO_NET_COSMETIC_PROFILE_BYTES) return;
    if (got_len < (int)(offsetof(GgpoNetCosmeticPacket, profile) + p->profile_len)) return;

    if (!ggpo_net_accept_packet_source(from, p->session_id, "cosmetic", 0)) {
        return;
    }
    if (!ggpo_net_link_confirmed()) return;

    ggpo_net_note_remote_fingerprint(p->build_id, p->exe_id, p->dll_id);
    g_net.remote_player = (int)p->sender_player;
    g_net.last_rx_tick = g_net.service_tick;
    g_net.packets_received++;
    if (p->asset_ack_revision == g_net.local_cosmetic_asset_revision) {
        g_net.local_cosmetic_asset_peer_applied_revision = p->asset_ack_revision;
    }

    if (!g_net.connected) {
        g_net.connected = 1;
        LOG_INFO("ggpo.net: connected mode=%s local_player=%d remote_player=%d peer_port=%u",
                 ggpo_net_mode_name(),
                 g_net.local_player,
                 g_net.remote_player,
                 (unsigned int)ntohs(g_net.peer_addr.sin_port));
    }

    if (p->profile_revision != g_net.remote_cosmetic_profile_revision ||
        p->profile_len != g_net.remote_cosmetic_profile_len ||
        memcmp(g_net.remote_cosmetic_profile, p->profile, p->profile_len) != 0) {
        memset(g_net.remote_cosmetic_profile, 0, sizeof(g_net.remote_cosmetic_profile));
        if (p->profile_len > 0u) {
            memcpy(g_net.remote_cosmetic_profile, p->profile, p->profile_len);
        }
        g_net.remote_cosmetic_profile_len = p->profile_len;
        g_net.remote_cosmetic_profile_revision = p->profile_revision ? p->profile_revision : (g_net.remote_cosmetic_profile_revision + 1u);
        if (g_net.remote_cosmetic_profile_revision == 0u) g_net.remote_cosmetic_profile_revision = 1u;
        if (g_net.remote_cosmetic_profile_applied_revision != g_net.remote_cosmetic_profile_revision) {
            g_net.remote_cosmetic_profile_applied_revision = 0u;
        }
        LOG_DEBUG("ggpo.net: remote cosmetic profile updated player=%u rev=%u bytes=%u",
                  (unsigned int)p->sender_player,
                  (unsigned int)g_net.remote_cosmetic_profile_revision,
                  (unsigned int)p->profile_len);
    }
}

static void ggpo_net_handle_cosmetic_asset_chunk(const GgpoNetCosmeticAssetChunkPacket* p, int got_len, const struct sockaddr_in* from) {
#if !GGPO_NET_ENABLE_COSMETICS
    (void)p;
    (void)got_len;
    (void)from;
    return;
#endif
    uint32_t expected_chunk_count = 0;
    uint32_t expected_chunk_size = 0;
    uint32_t expected_offset = 0;
    int new_asset = 0;
    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->type != GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK) return;
    if (p->sender_player > 1u || (int)p->sender_player == g_net.local_player) return;
    if (p->asset_revision == 0u || p->asset_len == 0u || p->asset_len > GGPO_NET_COSMETIC_ASSET_MAX_BYTES) return;
    if (p->chunk_size > GGPO_NET_COSMETIC_ASSET_CHUNK_BYTES) return;
    if (got_len < (int)(offsetof(GgpoNetCosmeticAssetChunkPacket, data) + p->chunk_size)) return;

    expected_chunk_count = ggpo_net_cosmetic_asset_chunk_count(p->asset_len);
    if (expected_chunk_count == 0u || p->chunk_count != expected_chunk_count || p->chunk_index >= expected_chunk_count) return;
    expected_chunk_size = ggpo_net_cosmetic_asset_chunk_size(p->asset_len, p->chunk_index);
    expected_offset = ggpo_net_cosmetic_asset_chunk_offset(p->chunk_index);
    if (p->chunk_size != expected_chunk_size) return;
    if (expected_offset + expected_chunk_size > p->asset_len) return;

    if (!ggpo_net_accept_packet_source(from, p->session_id, "asset", 0)) {
        return;
    }
    if (!ggpo_net_link_confirmed()) return;

    ggpo_net_note_remote_fingerprint(p->build_id, p->exe_id, p->dll_id);
    g_net.remote_player = (int)p->sender_player;
    g_net.last_rx_tick = g_net.service_tick;
    g_net.packets_received++;

    if (!g_net.connected) {
        g_net.connected = 1;
        LOG_INFO("ggpo.net: connected mode=%s local_player=%d remote_player=%d peer_port=%u",
                 ggpo_net_mode_name(),
                 g_net.local_player,
                 g_net.remote_player,
                 (unsigned int)ntohs(g_net.peer_addr.sin_port));
    }

    new_asset = (g_net.remote_cosmetic_asset_revision != p->asset_revision ||
                 g_net.remote_cosmetic_asset_len != p->asset_len ||
                 strncmp(g_net.remote_cosmetic_asset_id, p->asset_id, GGPO_NET_COSMETIC_ASSET_ID_BYTES) != 0);
    if (new_asset) {
        ggpo_net_reset_remote_cosmetic_asset();
        g_net.remote_cosmetic_asset = (uint8_t*)malloc(p->asset_len);
        g_net.remote_cosmetic_asset_chunks_seen = (uint8_t*)calloc(1, expected_chunk_count);
        if (!g_net.remote_cosmetic_asset || !g_net.remote_cosmetic_asset_chunks_seen) {
            ggpo_net_reset_remote_cosmetic_asset();
            return;
        }
        memcpy(g_net.remote_cosmetic_asset_id, p->asset_id, sizeof(g_net.remote_cosmetic_asset_id));
        g_net.remote_cosmetic_asset_id[GGPO_NET_COSMETIC_ASSET_ID_BYTES - 1] = '\0';
        g_net.remote_cosmetic_asset_len = p->asset_len;
        g_net.remote_cosmetic_asset_revision = p->asset_revision;
        g_net.remote_cosmetic_asset_chunk_count = expected_chunk_count;
        g_net.remote_cosmetic_asset_seen_chunk_count = 0;
        g_net.remote_cosmetic_asset_complete = 0;
        LOG_DEBUG("ggpo.net: receiving cosmetic asset id=%s rev=%u bytes=%u chunks=%u",
                  g_net.remote_cosmetic_asset_id,
                  (unsigned int)p->asset_revision,
                  (unsigned int)p->asset_len,
                  (unsigned int)expected_chunk_count);
    }

    if (!g_net.remote_cosmetic_asset || !g_net.remote_cosmetic_asset_chunks_seen) return;
    if (g_net.remote_cosmetic_asset_complete) return;
    memcpy(g_net.remote_cosmetic_asset + expected_offset, p->data, expected_chunk_size);
    if (!g_net.remote_cosmetic_asset_chunks_seen[p->chunk_index]) {
        g_net.remote_cosmetic_asset_chunks_seen[p->chunk_index] = 1;
        g_net.remote_cosmetic_asset_seen_chunk_count++;
        if (g_net.remote_cosmetic_asset_seen_chunk_count >= g_net.remote_cosmetic_asset_chunk_count) {
            g_net.remote_cosmetic_asset_complete = 1;
            LOG_DEBUG("ggpo.net: cosmetic asset complete id=%s rev=%u bytes=%u",
                      g_net.remote_cosmetic_asset_id,
                      (unsigned int)g_net.remote_cosmetic_asset_revision,
                      (unsigned int)g_net.remote_cosmetic_asset_len);
        }
    }
}

static void ggpo_net_handle_packet(const GgpoNetPacket* p, const struct sockaddr_in* from) {
    int is_handshake;
    int was_confirmed;
    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->sender_player > 1u || (int)p->sender_player == g_net.local_player) return;
    is_handshake = (p->type == GGPO_NET_PACKET_HELLO || p->type == GGPO_NET_PACKET_INPUT);

    if (!ggpo_net_accept_packet_source(from,
                                       p->session_id,
                                       is_handshake ? "handshake" : "control",
                                       p->type == GGPO_NET_PACKET_HELLO)) {
        return;
    }

    /* BYE is terminal only for a fully confirmed current attempt. It must never
     * complete the confirmation exchange by itself. */
    if (p->type == GGPO_NET_PACKET_BYE) {
        if (ggpo_net_link_confirmed()) {
            g_net.peer_disconnected = 1;
            LOG_INFO("ggpo.net: peer disconnected");
        }
        return;
    }

    was_confirmed = ggpo_net_link_confirmed();
    if (is_handshake) {
        if (!g_net.connected) {
            g_net.connected = 1;
            LOG_INFO("ggpo.net: peer traffic received mode=%s local_player=%d peer_port=%u; confirming bidirectional link",
                     ggpo_net_mode_name(),
                     g_net.local_player,
                     (unsigned int)ntohs(g_net.peer_addr.sin_port));
        }
        if (p->session_echo == g_net.session_id) {
            g_net.session_confirmed = 1;
        }
        if (p->session_echo == g_net.session_id &&
            p->confirmed_session_echo == g_net.session_id) {
            g_net.peer_confirmed_session = 1;
        }
        g_net.last_rx_tick = g_net.service_tick;
        g_net.packets_received++;
    }

    if (!ggpo_net_link_confirmed()) return;

    if (!was_confirmed) {
        g_net.state_send_offset = 0u;
        g_net.state_sync_announced = 0;
        LOG_INFO("ggpo.net: bidirectional link confirmed mode=%s local_player=%d remote_player=%u peer_port=%u",
                 ggpo_net_mode_name(),
                 g_net.local_player,
                 (unsigned int)p->sender_player,
                 (unsigned int)ntohs(g_net.peer_addr.sin_port));
    }

    ggpo_net_note_remote_fingerprint(p->build_id, p->exe_id, p->dll_id);
    ggpo_net_record_peer_timing(p->send_tick, p->tick_echo);
    g_net.remote_player = (int)p->sender_player;
    g_net.last_rx_tick = g_net.service_tick;
    if (!g_net.remote_prematch_hold_known || p->hold_epoch > g_net.remote_hold_epoch) {
        int remote_hold = p->prematch_hold ? 1 : 0;
        LOG_INFO("ggpo.net: peer prematch hold %s hold_epoch=%u state_epoch=%u",
                 remote_hold ? "engaged" : "released",
                 (unsigned int)p->hold_epoch,
                 (unsigned int)p->state_epoch);
        g_net.remote_prematch_hold = remote_hold;
        g_net.remote_prematch_hold_known = 1;
        g_net.remote_hold_epoch = p->hold_epoch;
    } else if (p->hold_epoch == g_net.remote_hold_epoch &&
               (p->prematch_hold ? 1 : 0) != g_net.remote_prematch_hold) {
        /* Conflicting same-generation flags are malformed. Keep the first
         * authenticated value rather than letting packet order toggle gates. */
        LOG_DEBUG("ggpo.net: ignored conflicting peer hold flag hold_epoch=%u",
                  (unsigned int)p->hold_epoch);
    }
    if (g_net.mode == GGPO_NET_MODE_JOIN &&
        p->state_epoch > g_net.remote_state_epoch_seen) {
        g_net.remote_state_epoch_seen = p->state_epoch;
    }

    /* Handshake, endpoint confirmation and RTT estimation deliberately continue
     * while held; every state/correction/input control path below is quarantined. */
    if (g_net.prematch_hold || g_net.remote_prematch_hold) {
        if (!is_handshake) {
            g_net.held_control_packets_dropped++;
        }
        return;
    }

    if (g_net.mode == GGPO_NET_MODE_HOST &&
        g_net.correction_active &&
        p->state_epoch == g_net.state_epoch &&
        p->correction_ack_id == g_net.correction_id &&
        p->correction_ack_checksum != 0u &&
        p->correction_ack_checksum == g_net.correction_checksum) {
        g_net.last_correction_ack_checksum = p->correction_ack_checksum;
        g_net.last_correction_ack_id = p->correction_ack_id;
        (void)ggpo_net_set_correction_base(g_net.correction_state,
                                           g_net.correction_state_len,
                                           g_net.correction_checksum);
        g_net.correction_active = 0;
        g_net.correction_state_len = 0;
        g_net.correction_send_offset = 0;
        g_net.correction_send_next_chunk = 0;
        g_net.correction_send_chunk_count = 0;
        g_net.correction_send_base_checksum = 0;
        g_net.correction_send_delta = 0;
        LOG_INFO("ggpo.net: correction ack received id=%u checksum=%u frame=%u",
                 (unsigned int)p->correction_ack_id,
                 (unsigned int)p->correction_ack_checksum,
                 (unsigned int)g_net.correction_frame);
    }
    if (p->type == GGPO_NET_PACKET_STATE_ACK &&
        g_net.mode == GGPO_NET_MODE_HOST &&
        p->state_epoch == g_net.state_epoch &&
        p->state_size == (uint32_t)g_net.state_size &&
        p->state_checksum == g_net.initial_checksum) {
        if (!g_net.remote_state_synced) {
            g_net.remote_state_synced = 1;
            LOG_INFO("ggpo.net: remote state sync ack received");
        }
    }
    if (p->type == GGPO_NET_PACKET_RESYNC_REQUEST &&
        g_net.mode == GGPO_NET_MODE_HOST &&
        p->state_epoch == g_net.state_epoch) {
        uint32_t request_frame = p->correction_request_frame ? p->correction_request_frame : p->frame;
        int stale_request = 0;
        if (g_net.correction_active) {
            stale_request = 1;
        } else if (g_net.last_correction_ack_id != 0u && request_frame <= g_net.correction_frame) {
            stale_request = 1;
        }
        if (stale_request) {
            g_net.stale_correction_requests++;
            LOG_DEBUG("ggpo.net: ignored stale resync request request_frame=%u remote_frame=%u corr_frame=%u corr_id=%u ack_id=%u stale=%u",
                      (unsigned int)request_frame,
                      (unsigned int)p->frame,
                      (unsigned int)g_net.correction_frame,
                      (unsigned int)g_net.correction_id,
                      (unsigned int)p->correction_ack_id,
                      (unsigned int)g_net.stale_correction_requests);
        } else {
            g_net.correction_requests++;
            LOG_WARN("ggpo.net: peer requested correction request_frame=%u remote_frame=%u last_corr_frame=%u ack_id=%u total=%u",
                     (unsigned int)request_frame,
                     (unsigned int)p->frame,
                     (unsigned int)g_net.correction_frame,
                     (unsigned int)p->correction_ack_id,
                     (unsigned int)g_net.correction_requests);
            /* Responder-side ring dump: the peer requesting this correction is the
             * one that DETECTED the desync (it dumped in recoverable_desync). We
             * (host) skipped our own checksum compare because we entered correction
             * handling, so dump our ring here too -> both peers' rings for the same
             * round land in mods/desync_dump.log for a clean p=0/p=1 diff. */
            if (request_frame != g_rng_last_dump_frame) {
                g_rng_last_dump_frame = request_frame;
                ggpo_net_dump_rng_ring(request_frame);
            }
            (void)ggpo_net_prepare_host_correction("peer resync request");
        }
    }

    if (!g_net.prematch_used &&
        !g_net.warned_initial_mismatch &&
        (p->state_size != (uint32_t)g_net.state_size || p->state_checksum != g_net.initial_checksum)) {
        LOG_WARN("ggpo.net: initial state differs; using host state local_size=%u remote_size=%u local_checksum=%u remote_checksum=%u",
                 (unsigned int)g_net.state_size,
                 (unsigned int)p->state_size,
                 (unsigned int)g_net.initial_checksum,
                 (unsigned int)p->state_checksum);
        g_net.warned_initial_mismatch = 1;
    }

    if (!is_handshake) g_net.packets_received++;

    /* Only an INPUT packet from the currently applied authoritative epoch may
     * populate frame history. HELLO/ACK packets are transport/control only, and
     * delayed pre-release INPUTs carry an older epoch. */
    if (p->type != GGPO_NET_PACKET_INPUT) return;
    if (p->state_epoch == 0u ||
        p->state_epoch != g_net.state_epoch ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) {
        g_net.held_control_packets_dropped++;
        return;
    }
    if (!g_net.has_remote_frame || p->frame > g_net.remote_frame) {
        g_net.remote_frame = p->frame;
        g_net.has_remote_frame = 1;
    }
    for (uint32_t i = 0; i < p->input_count && i < GGPO_NET_PACKET_INPUTS; i++) {
        uint32_t cmd = p->inputs[i].cmd;
        if (g_net.prematch_used &&
            !g_net.start_state_loaded &&
            p->inputs[i].frame == 0u) {
            cmd = 0u;
        }
        ggpo_net_note_remote_input(p->inputs[i].frame, cmd);
    }
    if (g_net.correction_active || g_net.awaiting_correction) {
        return;
    }
    for (uint32_t i = 0; i < p->checksum_count && i < GGPO_NET_PACKET_CHECKSUMS; i++) {
        uint32_t frame = p->checksums[i].frame;
        uint32_t remote_checksum = p->checksums[i].checksum;
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (g_net.last_correction_applied_checksum != 0u && frame < g_net.correction_frame) continue;
        if (!h->valid || h->frame != frame) continue;
        if (h->remote_predicted) continue;
        if (h->post_checksum != remote_checksum) {
            ggpo_net_log_desync_summary(frame, h, ggpo_net_packet_summary_for_frame(p, frame));
            ggpo_net_recoverable_desync(frame, h->post_checksum, remote_checksum, "confirmed frame checksum mismatch");
            break;
        }
    }
}

static void ggpo_net_poll_socket(void) {
    for (;;) {
        union {
            GgpoNetPacket normal;
            GgpoNetStateChunkPacket state_chunk;
            GgpoNetCosmeticPacket cosmetics;
            GgpoNetCosmeticAssetChunkPacket cosmetic_asset;
            uint8_t bytes[GGPO_NET_MAX_PACKET_BYTES];
        } packet;
        const GgpoNetPacketPrefix* prefix = (const GgpoNetPacketPrefix*)packet.bytes;
        struct sockaddr_in from;
        int from_len = sizeof(from);
        int recognized = 0;
        int got = recvfrom(g_net.sock, (char*)packet.bytes, sizeof(packet.bytes), 0, (struct sockaddr*)&from, &from_len);
        if (got == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return;
            return;
        }
        if (got < (int)sizeof(GgpoNetPacketPrefix)) continue;
        if (prefix->magic != GGPO_NET_MAGIC || prefix->version != GGPO_NET_VERSION) continue;
        if (prefix->type == GGPO_NET_PACKET_STATE_CHUNK) {
            recognized = got >= (int)offsetof(GgpoNetStateChunkPacket, data);
        } else if (prefix->type == GGPO_NET_PACKET_COSMETICS) {
            recognized = got >= (int)offsetof(GgpoNetCosmeticPacket, profile);
        } else if (prefix->type == GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK) {
            recognized = got >= (int)offsetof(GgpoNetCosmeticAssetChunkPacket, data);
        } else if ((prefix->type == GGPO_NET_PACKET_HELLO ||
                    prefix->type == GGPO_NET_PACKET_INPUT ||
                    prefix->type == GGPO_NET_PACKET_BYE ||
                    prefix->type == GGPO_NET_PACKET_STATE_ACK ||
                    prefix->type == GGPO_NET_PACKET_RESYNC_REQUEST) &&
                   got == (int)sizeof(GgpoNetPacket)) {
            recognized = 1;
        }
        if (!recognized ||
            !ggpo_net_authenticate_received(packet.bytes, got, &from)) {
            continue;
        }
        if (prefix->type == GGPO_NET_PACKET_STATE_CHUNK) {
            ggpo_net_handle_state_chunk(&packet.state_chunk, got, &from);
        } else if (prefix->type == GGPO_NET_PACKET_COSMETICS) {
            ggpo_net_handle_cosmetic_packet(&packet.cosmetics, got, &from);
        } else if (prefix->type == GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK) {
            ggpo_net_handle_cosmetic_asset_chunk(&packet.cosmetic_asset, got, &from);
        } else {
            ggpo_net_handle_packet(&packet.normal, &from);
        }
    }
}

static void ggpo_net_send_correction_burst(void) {
    if (g_net.mode != GGPO_NET_MODE_HOST) return;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return;
    if (!ggpo_net_link_confirmed() || !g_net.correction_active) return;
    for (int i = 0; i < GGPO_NET_CORRECTION_BURST_CHUNKS; i++) {
        if (!ggpo_net_send_correction_chunk()) break;
    }
}

/* Advance transport time without advancing game time. This is shared by the
 * public prematch service API and the normal gameplay advance path so timeout,
 * handshake, simulated-network, cosmetics, state-sync and correction behavior
 * cannot drift between the two call sites. */
static int ggpo_net_service_transport(uint16_t requested_heartbeat,
                                      char* err,
                                      size_t err_cap) {
    uint16_t heartbeat = requested_heartbeat;
    if (!g_net.active) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }

    g_net.service_tick++;
    ggpo_net_flush_sim_queue();
    ggpo_net_poll_socket();
    if (g_net.peer_disconnected) {
        ggpo_net_set_err(err, err_cap, "peer disconnected");
        return 0;
    }
    if (g_net.connected && g_net.last_rx_tick != 0u) {
        uint32_t timeout_ticks = (g_net.correction_active ||
                                  g_net.awaiting_correction ||
                                  g_net.recv_state)
            ? GGPO_NET_CORRECTION_TIMEOUT_TICKS
            : GGPO_NET_TIMEOUT_TICKS;
        if (g_net.service_tick - g_net.last_rx_tick > timeout_ticks) {
            g_net.peer_disconnected = 1;
            ggpo_net_set_err(err, err_cap, "peer timeout");
            return 0;
        }
    }

    ggpo_net_send_periodic_handshake_burst();
    if (!ggpo_net_link_confirmed() ||
        g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold) ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) {
        heartbeat = GGPO_NET_PACKET_HELLO;
    }
    (void)ggpo_net_send_packet(heartbeat);
    ggpo_net_send_cosmetic_profile_periodic();
    ggpo_net_send_cosmetic_asset_periodic();
    ggpo_net_send_state_sync_burst();
    ggpo_net_send_correction_burst();
    return 1;
}

static int ggpo_net_prediction_stall_needed(uint32_t frame, uint32_t* out_oldest_missing) {
    uint32_t tmp = 0;
    uint32_t oldest_missing = frame;
    uint32_t max_prediction = g_net.max_prediction ? g_net.max_prediction : GGPO_NET_DEFAULT_MAX_PREDICTION;
    uint32_t start = (frame > max_prediction) ? (frame - max_prediction) : 0u;

    if (ggpo_net_get_input(g_net.remote_inputs, frame, &tmp)) {
        if (out_oldest_missing) *out_oldest_missing = frame;
        return 0;
    }

    for (uint32_t f = start; f <= frame; f++) {
        if (!ggpo_net_get_input(g_net.remote_inputs, f, &tmp)) {
            oldest_missing = f;
            break;
        }
    }

    if (out_oldest_missing) *out_oldest_missing = oldest_missing;
    return (frame - oldest_missing) >= max_prediction;
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
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 1;
    if (!ggpo_net_link_confirmed() || !g_net.state_synced || !g_net.remote_state_synced) return 1;
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
    ggpo_net_apply_auto_input_delay();
    ggpo_net_seed_local_input_delay();
    /* Seed the sim-only snapshot from the just-loaded synced start state so frame 0
     * restores the synced seed even if a render frame perturbs it before the first
     * tick (both peers loaded the same initial seed). */
    {
        uint32_t init_seed = 0;
        float cx = 0.0f, cy = 0.0f;
        if (lua_manager_game_rng_seed(&init_seed)) {
            g_net.clean_mrand_seed = init_seed;
            (void)lua_manager_game_camera(&cx, &cy);
            g_net.clean_camera_x = cx;
            g_net.clean_camera_y = cy;
            g_net.have_clean_mrand_seed = 1;
        }
    }
    g_net.last_checksum = checksum;
    g_net.start_state_loaded = 1;
    LOG_INFO("ggpo.net: start state loaded checksum=%u input_delay=%u",
             (unsigned int)checksum,
             (unsigned int)g_net.input_delay);
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
    /* Snapshot the live waterfall ambience so the state-load + muted replay
     * below cannot leave the audio handles pointing at a stale voice slot
     * (the "waterfall in rooms with no waterfall" bug). Restored on every exit
     * path after this point. Audio-only: canonicalized out of the checksum. */
    hooks_waterfall_audio_save();

    int rb_ok = 1;
    if (!ggpo_ext_load_game_state(blob, h->state_len, err, err_cap)) {
        rb_ok = 0;
    } else {
        for (uint32_t f = start; f < end; f++) {
            GgpoNetHistoryEntry* rh = NULL;
            int predicted = 0;
            GgpoFrameInputs inputs;
            uint32_t local_cmd = 0;
            uint32_t remote_cmd = 0;

            if (!ggpo_net_save_pre_state(f, &rh, NULL, err, err_cap)) {
                rb_ok = 0;
                break;
            }
            memset(&inputs, 0, sizeof(inputs));
            ggpo_net_get_input(g_net.local_inputs, f, &local_cmd);
            remote_cmd = ggpo_net_predict_remote(f, &predicted);
            inputs.player_cmd[g_net.local_player] = local_cmd;
            inputs.player_cmd[g_net.remote_player] = remote_cmd;
            {
                /* Record the rngtrace for REPLAYED frames too. The trace previously
                 * recorded only LIVE advances, so the heavy-mispredicting peer (which
                 * does most of its work via rollback/replay) had a stale, ~sparse
                 * ring and couldn't be diffed against the other peer. Re-recording
                 * here re-stamps each frame with the latest (increasingly confirmed)
                 * inputs, so both peers' rings line up for a first-divergence diff.
                 * Diagnostic only: no-op unless rngtrace is on. */
                int trace = g_net_config_rng_trace;
                uint32_t seed_before = 0;
                if (trace) { (void)lua_manager_game_rng_seed(&seed_before); hooks_rng_trace_begin(f, 0u); }
                if (!ggpo_net_replay_frame(f, arg0, 1, &checksum, err, err_cap)) {
                    if (trace) hooks_rng_trace_end();
                    rb_ok = 0;
                    break;
                }
                if (trace) {
                    HooksRngTrace tr;
                    uint32_t seed_after = 0;
                    hooks_rng_trace_copy(&tr);
                    hooks_rng_trace_end();
                    (void)lua_manager_game_rng_seed(&seed_after);
                    ggpo_net_record_rng_frame(f, local_cmd, remote_cmd, seed_before, seed_after, &tr);
                }
            }
            rh->local_cmd = local_cmd;
            rh->remote_cmd = remote_cmd;
            rh->remote_predicted = predicted;
            rh->post_checksum = checksum;
            ggpo_net_capture_history_summary(rh);
        }
    }

    hooks_waterfall_audio_restore();

    if (!rb_ok) return 0;

    /* Sim-only seed: the post-replay seed is the new clean baseline (replay has no
     * render/transition draws between its ticks), so the next live tick restores
     * THIS, not a pre-rollback value. */
    {
        uint32_t post_seed = 0;
        float cx = 0.0f, cy = 0.0f;
        if (lua_manager_game_rng_seed(&post_seed)) {
            g_net.clean_mrand_seed = post_seed;
            (void)lua_manager_game_camera(&cx, &cy);
            g_net.clean_camera_x = cx;
            g_net.clean_camera_y = cy;
            g_net.have_clean_mrand_seed = 1;
        }
    }

    g_net.last_checksum = checksum;
    g_net.rollbacks++;
    LOG_DEBUG("ggpo.net: rollback start=%u end=%u checksum=%u total=%u",
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
    /* Publicly, "connected" means this attempt has proved both directions: a
     * current-session packet reached the peer and its echo reached us. The
     * stricter private link gate also waits for the peer to acknowledge that
     * proof before state sync/gameplay begins. Stopping retries at local proof
     * prevents one side rolling its socket during the final acknowledgement. */
    return (g_net.connected && g_net.session_confirmed) ? 1 : 0;
}

int ggpo_net_link_ready(void) {
    return (g_net.active && ggpo_net_link_confirmed()) ? 1 : 0;
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

uint32_t ggpo_net_input_delay(void) {
    return g_net.active ? g_net.input_delay : g_net_config_input_delay;
}

int ggpo_net_set_input_delay(uint32_t frames) {
    if (frames > GGPO_NET_MAX_INPUT_DELAY) return 0;
    g_net_config_input_delay = frames;
    if (g_net.active) {
        g_net.input_delay = frames;
        for (uint32_t f = g_net.frame; f < g_net.frame + frames; f++) {
            uint32_t tmp = 0;
            if (!ggpo_net_get_input(g_net.local_inputs, f, &tmp)) {
                ggpo_net_store_input(g_net.local_inputs, f, 0u);
            }
        }
    }
    return 1;
}

int ggpo_net_auto_input_delay(void) {
    return g_net_config_auto_input_delay ? 1 : 0;
}

int ggpo_net_rng_trace(void) {
    return g_net_config_rng_trace ? 1 : 0;
}

int ggpo_net_set_rng_trace(int enabled) {
    g_net_config_rng_trace = enabled ? 1 : 0;
    return 1;
}

int ggpo_net_set_auto_input_delay(int enabled) {
    g_net_config_auto_input_delay = enabled ? 1 : 0;
    return 1;
}

/* Smoothed round-trip estimate in service ticks (~frames at 60Hz); 0 if no
 * samples yet or no active session. */
uint32_t ggpo_net_rtt_ticks(void) {
    return (g_net.active && g_net.rtt_sample_count) ? g_net.rtt_ema_ticks : 0u;
}

uint32_t ggpo_net_max_frame_advantage(void) {
    return g_net.active ? g_net.max_frame_advantage : g_net_config_max_frame_advantage;
}

int ggpo_net_set_max_frame_advantage(uint32_t frames) {
    if (frames > GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT) return 0;
    g_net_config_max_frame_advantage = frames;
    if (g_net.active) {
        g_net.max_frame_advantage = frames;
    }
    return 1;
}

uint32_t ggpo_net_max_prediction(void) {
    return g_net.active ? g_net.max_prediction : g_net_config_max_prediction;
}

int ggpo_net_set_max_prediction(uint32_t frames) {
    if (frames == 0u || frames > GGPO_NET_MAX_PREDICTION_LIMIT || frames >= GGPO_NET_HISTORY_FRAMES) return 0;
    g_net_config_max_prediction = frames;
    if (g_net.active) {
        g_net.max_prediction = frames;
    }
    return 1;
}

int ggpo_net_set_network_sim(uint32_t loss_percent, uint32_t min_delay_ticks, uint32_t max_delay_ticks) {
    if (loss_percent > 100u) return 0;
    if (min_delay_ticks > GGPO_NET_SIM_MAX_DELAY_TICKS || max_delay_ticks > GGPO_NET_SIM_MAX_DELAY_TICKS) return 0;
    if (min_delay_ticks > max_delay_ticks) return 0;

    g_net_config_sim_loss_percent = loss_percent;
    g_net_config_sim_delay_min_ticks = min_delay_ticks;
    g_net_config_sim_delay_max_ticks = max_delay_ticks;

    if (g_net.active) {
        g_net.sim_loss_percent = loss_percent;
        g_net.sim_delay_min_ticks = min_delay_ticks;
        g_net.sim_delay_max_ticks = max_delay_ticks;
        if (max_delay_ticks == 0u) {
            memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
        }
    }
    return 1;
}

uint32_t ggpo_net_sim_loss_percent(void) {
    return g_net.active ? g_net.sim_loss_percent : g_net_config_sim_loss_percent;
}

uint32_t ggpo_net_sim_delay_min_ticks(void) {
    return g_net.active ? g_net.sim_delay_min_ticks : g_net_config_sim_delay_min_ticks;
}

uint32_t ggpo_net_sim_delay_max_ticks(void) {
    return g_net.active ? g_net.sim_delay_max_ticks : g_net_config_sim_delay_max_ticks;
}

uint32_t ggpo_net_sim_dropped_packets(void) {
    return g_net.sim_packets_dropped;
}

uint32_t ggpo_net_sim_delayed_packets(void) {
    return g_net.sim_packets_delayed;
}

uint32_t ggpo_net_sim_queue_drop_count(void) {
    return g_net.sim_queue_drops;
}

uint32_t ggpo_net_sim_pending_packets(void) {
    return g_net.active ? ggpo_net_sim_pending_count() : 0u;
}

int ggpo_net_correction_enabled(void) {
    return g_net.active ? (g_net.correction_enabled ? 1 : 0) : (g_net_config_correction_enabled ? 1 : 0);
}

int ggpo_net_set_correction_enabled(int enabled) {
    g_net_config_correction_enabled = enabled ? 1 : 0;
    if (g_net.active) {
        g_net.correction_enabled = g_net_config_correction_enabled;
        if (!g_net.correction_enabled) {
            g_net.correction_active = 0;
            g_net.awaiting_correction = 0;
            g_net.correction_state_len = 0;
            g_net.correction_send_offset = 0;
            g_net.correction_send_next_chunk = 0;
            g_net.correction_send_chunk_count = 0;
            g_net.correction_send_base_checksum = 0;
            g_net.correction_send_delta = 0;
            g_net.correction_request_frame = 0;
            g_net.correction_wait_start_tick = 0;
            g_net.correction_wait_cap_announced = 0;
        }
    }
    return 1;
}

int ggpo_net_awaiting_correction(void) {
    return g_net.awaiting_correction ? 1 : 0;
}

int ggpo_net_correction_active(void) {
    return g_net.correction_active ? 1 : 0;
}

uint32_t ggpo_net_corrections_sent(void) {
    return g_net.corrections_sent;
}

uint32_t ggpo_net_corrections_received(void) {
    return g_net.corrections_received;
}

uint32_t ggpo_net_correction_request_count(void) {
    return g_net.correction_requests;
}

uint32_t ggpo_net_correction_id(void) {
    return g_net.correction_id;
}

uint32_t ggpo_net_last_correction_applied_id(void) {
    return g_net.last_correction_applied_id;
}

uint32_t ggpo_net_stale_correction_request_count(void) {
    return g_net.stale_correction_requests;
}

uint32_t ggpo_net_duplicate_state_chunk_count(void) {
    return g_net.duplicate_state_chunks;
}

uint32_t ggpo_net_local_build_id(void) {
    ggpo_net_ensure_local_fingerprint();
    return g_net.active ? g_net.local_build_id : g_net_local_build_id;
}

uint32_t ggpo_net_local_exe_id(void) {
    ggpo_net_ensure_local_fingerprint();
    return g_net.active ? g_net.local_exe_id : g_net_local_exe_id;
}

uint32_t ggpo_net_local_dll_id(void) {
    ggpo_net_ensure_local_fingerprint();
    return g_net.active ? g_net.local_dll_id : g_net_local_dll_id;
}

uint32_t ggpo_net_remote_build_id(void) {
    return g_net.remote_build_id;
}

uint32_t ggpo_net_remote_exe_id(void) {
    return g_net.remote_exe_id;
}

uint32_t ggpo_net_remote_dll_id(void) {
    return g_net.remote_dll_id;
}

int ggpo_net_build_mismatch(void) {
    if (!g_net.active || !ggpo_net_link_confirmed() || !g_net.has_remote_fingerprint) return 0;
    return (g_net.local_build_id != g_net.remote_build_id ||
            g_net.local_exe_id != g_net.remote_exe_id ||
            g_net.local_dll_id != g_net.remote_dll_id) ? 1 : 0;
}

int ggpo_net_set_local_cosmetic_profile(const char* profile, size_t profile_len) {
#if !GGPO_NET_ENABLE_COSMETICS
    (void)profile;
    (void)profile_len;
    memset(g_net.local_cosmetic_profile, 0, sizeof(g_net.local_cosmetic_profile));
    g_net.local_cosmetic_profile_len = 0;
    return 1;
#endif
    if (!profile) profile_len = 0;
    if (profile_len > GGPO_NET_COSMETIC_PROFILE_BYTES) return 0;
    if (profile_len == g_net.local_cosmetic_profile_len &&
        (profile_len == 0 || memcmp(g_net.local_cosmetic_profile, profile, profile_len) == 0)) {
        return 1;
    }

    memset(g_net.local_cosmetic_profile, 0, sizeof(g_net.local_cosmetic_profile));
    if (profile_len > 0) {
        memcpy(g_net.local_cosmetic_profile, profile, profile_len);
    }
    g_net.local_cosmetic_profile_len = (uint32_t)profile_len;
    g_net.local_cosmetic_profile_revision++;
    if (g_net.local_cosmetic_profile_revision == 0u) g_net.local_cosmetic_profile_revision = 1u;
    g_net.last_cosmetic_profile_send_tick = 0u;
    if (g_net.active && g_net.has_peer_addr) {
        (void)ggpo_net_send_cosmetic_profile();
    }
    return 1;
}

const char* ggpo_net_remote_cosmetic_profile(size_t* out_len, uint32_t* out_revision) {
    if (out_len) *out_len = (size_t)g_net.remote_cosmetic_profile_len;
    if (out_revision) *out_revision = g_net.remote_cosmetic_profile_revision;
    if (g_net.remote_cosmetic_profile_len == 0u) return NULL;
    return g_net.remote_cosmetic_profile;
}

void ggpo_net_mark_remote_cosmetic_profile_applied(uint32_t revision) {
    if (revision != 0u && revision == g_net.remote_cosmetic_profile_revision) {
        g_net.remote_cosmetic_profile_applied_revision = revision;
    }
}

uint32_t ggpo_net_local_cosmetic_profile_revision(void) {
    return g_net.local_cosmetic_profile_revision;
}

uint32_t ggpo_net_remote_cosmetic_profile_revision(void) {
    return g_net.remote_cosmetic_profile_revision;
}

uint32_t ggpo_net_remote_cosmetic_profile_applied_revision(void) {
    return g_net.remote_cosmetic_profile_applied_revision;
}

int ggpo_net_set_local_cosmetic_asset(const char* asset_id, const void* data, size_t data_len) {
#if !GGPO_NET_ENABLE_COSMETICS
    (void)asset_id;
    (void)data;
    (void)data_len;
    free(g_net.local_cosmetic_asset);
    g_net.local_cosmetic_asset = NULL;
    memset(g_net.local_cosmetic_asset_id, 0, sizeof(g_net.local_cosmetic_asset_id));
    g_net.local_cosmetic_asset_len = 0;
    return 1;
#endif
    size_t id_len = asset_id ? strlen(asset_id) : 0;
    if (!asset_id || id_len == 0 || !data || data_len == 0) {
        free(g_net.local_cosmetic_asset);
        g_net.local_cosmetic_asset = NULL;
        memset(g_net.local_cosmetic_asset_id, 0, sizeof(g_net.local_cosmetic_asset_id));
        g_net.local_cosmetic_asset_len = 0;
        g_net.local_cosmetic_asset_revision++;
        if (g_net.local_cosmetic_asset_revision == 0u) g_net.local_cosmetic_asset_revision = 1u;
        g_net.local_cosmetic_asset_next_chunk = 0;
        g_net.local_cosmetic_asset_last_send_tick = 0;
        g_net.local_cosmetic_asset_peer_applied_revision = 0;
        return 1;
    }
    if (id_len >= GGPO_NET_COSMETIC_ASSET_ID_BYTES) return 0;
    if (data_len > GGPO_NET_COSMETIC_ASSET_MAX_BYTES) return 0;
    if (g_net.local_cosmetic_asset &&
        data_len == g_net.local_cosmetic_asset_len &&
        strcmp(g_net.local_cosmetic_asset_id, asset_id) == 0 &&
        memcmp(g_net.local_cosmetic_asset, data, data_len) == 0) {
        return 1;
    }

    {
        uint8_t* copy = (uint8_t*)malloc(data_len);
        if (!copy) return 0;
        memcpy(copy, data, data_len);
        free(g_net.local_cosmetic_asset);
        g_net.local_cosmetic_asset = copy;
    }

    memset(g_net.local_cosmetic_asset_id, 0, sizeof(g_net.local_cosmetic_asset_id));
    memcpy(g_net.local_cosmetic_asset_id, asset_id, id_len);
    g_net.local_cosmetic_asset_len = (uint32_t)data_len;
    g_net.local_cosmetic_asset_revision++;
    if (g_net.local_cosmetic_asset_revision == 0u) g_net.local_cosmetic_asset_revision = 1u;
    g_net.local_cosmetic_asset_next_chunk = 0;
    g_net.local_cosmetic_asset_last_send_tick = 0;
    g_net.local_cosmetic_asset_peer_applied_revision = 0;
    if (g_net.active && g_net.has_peer_addr) {
        ggpo_net_send_cosmetic_asset_periodic();
    }
    return 1;
}

const void* ggpo_net_remote_cosmetic_asset(const char** out_id, size_t* out_len, uint32_t* out_revision) {
    if (out_id) *out_id = g_net.remote_cosmetic_asset_id;
    if (out_len) *out_len = (size_t)g_net.remote_cosmetic_asset_len;
    if (out_revision) *out_revision = g_net.remote_cosmetic_asset_revision;
    if (!g_net.remote_cosmetic_asset_complete || !g_net.remote_cosmetic_asset || g_net.remote_cosmetic_asset_len == 0u) {
        return NULL;
    }
    return g_net.remote_cosmetic_asset;
}

void ggpo_net_mark_remote_cosmetic_asset_applied(uint32_t revision) {
    if (revision != 0u && revision == g_net.remote_cosmetic_asset_revision) {
        g_net.remote_cosmetic_asset_applied_revision = revision;
    }
}

uint32_t ggpo_net_local_cosmetic_asset_revision(void) {
    return g_net.local_cosmetic_asset_revision;
}

uint32_t ggpo_net_remote_cosmetic_asset_revision(void) {
    return g_net.remote_cosmetic_asset_revision;
}

uint32_t ggpo_net_remote_cosmetic_asset_applied_revision(void) {
    return g_net.remote_cosmetic_asset_applied_revision;
}

int ggpo_net_start_state_loaded(void) {
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 0;
    return g_net.start_state_loaded ? 1 : 0;
}

int ggpo_net_state_synced(void) {
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 0;
    return g_net.state_synced ? 1 : 0;
}

int ggpo_net_remote_state_synced(void) {
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 0;
    return g_net.remote_state_synced ? 1 : 0;
}

int ggpo_net_has_peer(void) {
    return (g_net.active && g_net.has_peer_addr) ? 1 : 0;
}

int ggpo_net_prematch_hold(void) {
    return (g_net.active && g_net.prematch_hold) ? 1 : 0;
}

int ggpo_net_peer_prematch_hold(void) {
    if (!g_net.active || !g_net.remote_prematch_hold_known) return 0;
    return g_net.remote_prematch_hold ? 1 : 0;
}

uint32_t ggpo_net_state_epoch(void) {
    return g_net.active ? g_net.state_epoch : 0u;
}

int ggpo_net_set_prematch_hold(int enabled, char* err, size_t err_cap) {
    uint32_t floor;
    enabled = enabled ? 1 : 0;
    if (!g_net.active) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }
    if (g_net.prematch_hold == enabled) return 1;
    if (g_net.frame != 0u || g_net.start_state_loaded) {
        ggpo_net_set_err(err, err_cap, "prematch hold cannot change after gameplay starts");
        return 0;
    }
    if (g_net.hold_epoch == UINT_MAX) {
        ggpo_net_set_err(err, err_cap, "prematch hold epoch exhausted");
        return 0;
    }

    if (enabled) {
        /* Quarantine anything already queued by an earlier setup path before the
         * first held heartbeat can leave this socket. */
        memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
        ggpo_net_reset_authoritative_state_bookkeeping();
        if (g_net.mode == GGPO_NET_MODE_HOST) {
            g_net.state_synced = 1;
            g_net.remote_state_synced = 0;
        } else {
            floor = g_net.remote_state_epoch_seen;
            if (g_net.state_epoch > floor) floor = g_net.state_epoch;
            /* A freshly started v16 host owns epoch 1 even if no HELLO has
             * arrived yet; requiring >1 rejects its constructor-time snapshot. */
            if (floor < GGPO_NET_INITIAL_STATE_EPOCH) {
                floor = GGPO_NET_INITIAL_STATE_EPOCH;
            }
            g_net.hold_remote_epoch_floor = floor;
            g_net.state_epoch = 0u;
            g_net.state_synced = 0;
            g_net.remote_state_synced = 1;
        }
        g_net.hold_epoch++;
        g_net.prematch_hold = 1;
        g_net.prematch_used = 1;
        (void)ggpo_net_send_packet(GGPO_NET_PACKET_HELLO);
        LOG_INFO("ggpo.net: prematch hold engaged mode=%s hold_epoch=%u state_epoch=%u remote_floor=%u",
                 ggpo_net_mode_name(),
                 (unsigned int)g_net.hold_epoch,
                 (unsigned int)g_net.state_epoch,
                 (unsigned int)g_net.hold_remote_epoch_floor);
        (void)ggpo_net_prematch_invariants_ok("hold-engage");
        return 1;
    }

    /* Release is asymmetric by design: the host publishes one freshly captured
     * post-map/reset state, while the join peer discards all constructor/countdown
     * sync state and waits for that newer epoch. */
    if (g_net.mode == GGPO_NET_MODE_HOST) {
        if (!ggpo_net_recapture_host_start_state(err, err_cap)) {
            LOG_ERROR("ggpo.net: prematch host release capture failed (%s)",
                      (err && err[0]) ? err : "unknown error");
            return 0;
        }
    } else {
        ggpo_net_reset_authoritative_state_bookkeeping();
        g_net.state_epoch = 0u;
        g_net.state_synced = 0;
        g_net.remote_state_synced = 1;
    }
    memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
    g_net.hold_epoch++;
    g_net.prematch_hold = 0;
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_HELLO);
    LOG_INFO("ggpo.net: prematch hold released mode=%s hold_epoch=%u state_epoch=%u remote_floor=%u checksum=%u",
             ggpo_net_mode_name(),
             (unsigned int)g_net.hold_epoch,
             (unsigned int)g_net.state_epoch,
             (unsigned int)g_net.hold_remote_epoch_floor,
             (unsigned int)g_net.initial_checksum);
    (void)ggpo_net_prematch_invariants_ok("hold-release");
    return 1;
}

/* Once the final authoritative state exists, exchange a deliberately neutral
 * frame-0 input while still in the hub. Besides avoiding a frozen first visible
 * gameplay frame, receiving this INPUT is a useful second-phase readiness
 * proof: INPUT packets are accepted only after both sides have released their
 * hold and completed state sync. */
static void ggpo_net_prime_prematch_frame0(void) {
    uint32_t cmd = 0u;
    if (!g_net.active || !g_net.prematch_used || g_net.frame != 0u) return;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return;
    if (!ggpo_net_link_confirmed() ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) return;
    if (ggpo_net_wait_for_cosmetic_profiles(NULL)) return;
    if (!ggpo_net_get_input(g_net.local_inputs, 0u, &cmd)) {
        ggpo_net_store_input(g_net.local_inputs, 0u, 0u);
    }
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
}

int ggpo_net_service(char* err, size_t err_cap) {
    if (!ggpo_net_service_transport(GGPO_NET_PACKET_HELLO, err, err_cap)) {
        return 0;
    }
    ggpo_net_prime_prematch_frame0();
    return 1;
}

int ggpo_net_prematch_ready(void) {
    uint32_t local_cmd = 0u;
    uint32_t remote_cmd = 0u;
    if (!g_net.active || !g_net.prematch_used || g_net.frame != 0u) return 0;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 0;
    if (!ggpo_net_link_confirmed() ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) return 0;
    if (!ggpo_net_cosmetic_profiles_ready() &&
        !g_net.cosmetic_wait_cap_announced) return 0;
    if (!ggpo_net_get_input(g_net.local_inputs, 0u, &local_cmd) ||
        !ggpo_net_get_input(g_net.remote_inputs, 0u, &remote_cmd)) return 0;
    /* The countdown never supplies gameplay input. Treat anything else as not
     * ready rather than letting held menu input leak into frame zero. */
    return (local_cmd == 0u && remote_cmd == 0u) ? 1 : 0;
}

int ggpo_net_prepare_prematch_start(char* err, size_t err_cap) {
    uint32_t local_cmd = 0u;
    uint32_t remote_cmd = 0u;
    if (!ggpo_net_prematch_ready()) {
        ggpo_net_set_err(err, err_cap, "prematch state is not ready");
        return 0;
    }
    (void)ggpo_net_get_input(g_net.local_inputs, 0u, &local_cmd);
    (void)ggpo_net_get_input(g_net.remote_inputs, 0u, &remote_cmd);
    if (!ggpo_net_load_start_state_if_ready(err, err_cap)) {
        return 0;
    }
    /* Loading the authoritative start state intentionally clears every old
     * runtime history entry. Restore only the neutral readiness inputs proven
     * above so the first visible tick can advance immediately. */
    ggpo_net_store_input(g_net.local_inputs, 0u, local_cmd);
    ggpo_net_store_input(g_net.remote_inputs, 0u, remote_cmd);
    g_net.last_remote_cmd = remote_cmd;
    g_net.has_last_remote_cmd = 1;
    g_net.remote_frame = 0u;
    g_net.has_remote_frame = 1;
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
    LOG_INFO("ggpo.net: prematch ready; synchronized frame 0 prepared before gameplay");
    return 1;
}

static void ggpo_net_stop_internal(int notify_peer) {
    if (g_net.sock != INVALID_SOCKET && g_net.sock != 0) {
        if (notify_peer && g_net.has_peer_addr && ggpo_net_link_confirmed()) {
            (void)ggpo_net_send_packet(GGPO_NET_PACKET_BYE);
        }
        closesocket(g_net.sock);
    }
    free(g_net.state_blobs);
    free(g_net.initial_state);
    free(g_net.correction_state);
    free(g_net.correction_base_state);
    free(g_net.recv_state);
    free(g_net.recv_state_chunks_seen);
    free(g_net.local_cosmetic_asset);
    free(g_net.remote_cosmetic_asset);
    free(g_net.remote_cosmetic_asset_chunks_seen);
    SecureZeroMemory(g_net.auth_root_key, sizeof(g_net.auth_root_key));
    SecureZeroMemory(g_net.auth_send_key, sizeof(g_net.auth_send_key));
    SecureZeroMemory(g_net.auth_remote_key, sizeof(g_net.auth_remote_key));
    memset(&g_net, 0, sizeof(g_net));
    g_net.sock = INVALID_SOCKET;
}

void ggpo_net_stop(void) {
    ggpo_net_stop_internal(1);
}

void ggpo_net_stop_for_retry(void) {
    ggpo_net_stop_internal(0);
}

static int ggpo_net_start_common(GgpoNetMode mode, uint16_t local_port, char* err, size_t err_cap) {
    SOCKET s = INVALID_SOCKET;
    uint16_t bound_port = local_port;
    size_t state_len = 0;
    uint32_t checksum = 0;
    uint64_t new_session_id = 0u;

    if (g_net.active) {
        ggpo_net_set_err(err, err_cap, "net session already active");
        return 0;
    }
    if (!g_net_pending_auth_ready) {
        ggpo_net_set_err(err, err_cap, "authenticated match token is required before starting P2P");
        return 0;
    }
    if (!ggpo_net_make_session_id(&new_session_id)) {
        ggpo_net_set_err(err, err_cap, "secure P2P session generation failed");
        return 0;
    }
    memset(&g_net, 0, sizeof(g_net));
    g_net.sock = INVALID_SOCKET;

    g_net.state_size = ggpo_ext_game_state_size();
    if (g_net.state_size == 0 || g_net.state_size > (size_t)UINT_MAX) {
        ggpo_net_set_err(err, err_cap, "game state unavailable");
        return 0;
    }
    if (!ggpo_net_make_socket(local_port, &s, &bound_port, err, err_cap)) {
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
    g_net.correction_state = (uint8_t*)malloc(g_net.state_size);
    if (!g_net.correction_state) {
        closesocket(s);
        free(g_net.state_blobs);
        free(g_net.initial_state);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        ggpo_net_set_err(err, err_cap, "out of memory");
        return 0;
    }
    g_net.correction_base_state = (uint8_t*)malloc(g_net.state_size);
    if (!g_net.correction_base_state) {
        closesocket(s);
        free(g_net.state_blobs);
        free(g_net.initial_state);
        free(g_net.correction_state);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        ggpo_net_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!ggpo_ext_save_game_state(g_net.state_blobs, g_net.state_size, &state_len, &checksum, err, err_cap)) {
        closesocket(s);
        free(g_net.state_blobs);
        free(g_net.initial_state);
        free(g_net.correction_state);
        free(g_net.correction_base_state);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        return 0;
    }
    memcpy(g_net.initial_state, g_net.state_blobs, state_len);
    g_net.initial_state_len = state_len;
    (void)ggpo_net_set_correction_base(g_net.initial_state, g_net.initial_state_len, checksum);
    ggpo_net_ensure_local_fingerprint();

    g_net.active = 1;
    g_net.mode = mode;
    g_net.local_player = (mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    g_net.remote_player = (mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.local_port = bound_port;
    g_net.sock = s;
    g_net.session_id = new_session_id;
    memcpy(g_net.auth_root_key,
           g_net_pending_auth_root,
           sizeof(g_net.auth_root_key));
    if (!ggpo_net_auth_derive_direction_key(g_net.auth_root_key,
                                            (uint32_t)g_net.local_player,
                                            g_net.session_id,
                                            g_net.auth_send_key)) {
        ggpo_net_set_err(err, err_cap, "P2P packet authentication setup failed");
        ggpo_net_stop_internal(0);
        return 0;
    }
    g_net.auth_ready = 1;
    SecureZeroMemory(g_net_pending_auth_root, sizeof(g_net_pending_auth_root));
    g_net_pending_auth_ready = 0;
    g_net.input_delay = g_net_config_input_delay;
    g_net.peer_last_send_tick = 0u;
    g_net.rtt_ema_ticks = 0u;
    g_net.rtt_last_ticks = 0u;
    g_net.rtt_sample_count = 0u;
    g_net.auto_input_delay_applied = 0;
    memset(g_rng_ring, 0, sizeof(g_rng_ring));
    g_rng_dump_count = 0u;
    g_rng_last_dump_frame = 0xFFFFFFFFu;
    g_net.max_frame_advantage = g_net_config_max_frame_advantage;
    g_net.max_prediction = g_net_config_max_prediction;
    g_net.sim_loss_percent = g_net_config_sim_loss_percent;
    g_net.sim_delay_min_ticks = g_net_config_sim_delay_min_ticks;
    g_net.sim_delay_max_ticks = g_net_config_sim_delay_max_ticks;
    g_net.correction_enabled = g_net_config_correction_enabled ? 1 : 0;
    g_net.local_build_id = g_net_local_build_id;
    g_net.local_exe_id = g_net_local_exe_id;
    g_net.local_dll_id = g_net_local_dll_id;
    g_net.sim_rng = (uint32_t)g_net.session_id ^
                    (uint32_t)(g_net.session_id >> 32) ^
                    0x75BCD15u;
    if (g_net.sim_rng == 0u) g_net.sim_rng = 1u;
    g_net.initial_checksum = checksum;
    g_net.last_checksum = checksum;
    g_net.state_epoch = (mode == GGPO_NET_MODE_HOST) ? GGPO_NET_INITIAL_STATE_EPOCH : 0u;
    g_net.state_synced = (mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.remote_state_synced = (mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    LOG_INFO("ggpo.net: local build fingerprint build=%08X exe=%08X dll=%08X correction=%s",
             (unsigned int)g_net.local_build_id,
             (unsigned int)g_net.local_exe_id,
             (unsigned int)g_net.local_dll_id,
             g_net.correction_enabled ? "on" : "off");
    return 1;
}

int ggpo_net_start_host(uint16_t local_port, char* err, size_t err_cap) {
    if (!ggpo_net_start_common(GGPO_NET_MODE_HOST, local_port, err, err_cap)) {
        return 0;
    }
    LOG_INFO("ggpo.net: hosting udp port=%u input_delay=%u max_advantage=%u max_prediction=%u state_size=%u checksum=%u",
             (unsigned int)g_net.local_port,
             (unsigned int)g_net.input_delay,
             (unsigned int)g_net.max_frame_advantage,
             (unsigned int)g_net.max_prediction,
             (unsigned int)g_net.state_size,
             (unsigned int)g_net.initial_checksum);
    return 1;
}

int ggpo_net_start_join_deferred(uint16_t local_port, char* err, size_t err_cap) {
    if (!ggpo_net_start_common(GGPO_NET_MODE_JOIN, local_port, err, err_cap)) {
        return 0;
    }
    LOG_INFO("ggpo.net: joining deferred local_port=%u input_delay=%u max_advantage=%u max_prediction=%u state_size=%u checksum=%u",
             (unsigned int)g_net.local_port,
             (unsigned int)g_net.input_delay,
             (unsigned int)g_net.max_frame_advantage,
             (unsigned int)g_net.max_prediction,
             (unsigned int)g_net.state_size,
             (unsigned int)g_net.initial_checksum);
    return 1;
}

int ggpo_net_set_peer(const char* host, uint16_t remote_port, char* err, size_t err_cap) {
    struct sockaddr_in addr;
    int changed = 0;
    int replace_stale_prematch_candidates = 0;
    if (!g_net.active || g_net.sock == INVALID_SOCKET) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }
    if (remote_port == 0) {
        ggpo_net_set_err(err, err_cap, "missing peer port");
        return 0;
    }
    if (!ggpo_net_resolve_peer(host, remote_port, &addr, err, err_cap)) {
        return 0;
    }
    /* A server endpoint update during prematch means the peer rotated to a new
     * socket/NAT mapping.  The old candidate generation is dead; retaining all
     * three routes for every retry both directs heartbeats at stale sockets and
     * eventually exhausts the fixed candidate array before attempt three. */
    replace_stale_prematch_candidates =
        g_net.has_peer_addr &&
        !ggpo_net_addr_equal(&g_net.peer_addr, &addr) &&
        ggpo_net_prematch_recovery_allowed();
    if (replace_stale_prematch_candidates) {
        memset(g_net.candidates, 0, sizeof(g_net.candidates));
        g_net.candidate_count = 0;
        g_net.last_handshake_burst_tick = 0u;
        changed = 1;
        LOG_INFO("ggpo.net: replaced stale prematch peer candidates with %s:%u",
                 host ? host : "",
                 (unsigned int)remote_port);
    }

    /* Always register as a hole-punch candidate. */
    ggpo_net_add_candidate_addr(&addr);
    if (g_net.has_peer_addr) {
        if (ggpo_net_addr_equal(&g_net.peer_addr, &addr)) {
            /* already our primary; just (re)punch. */
        } else if (ggpo_net_link_confirmed() &&
                   !ggpo_net_prematch_recovery_allowed()) {
            ggpo_net_set_err(err, err_cap, "already connected to a different peer");
            return 0;
        } else {
            /* Not connected yet: keep peer_addr as-is (it may have been learned
             * from an actual reply via accept_packet_source, which is
             * authoritative). This new endpoint is now a candidate we also punch. */
            changed = 1;
        }
    } else {
        g_net.peer_addr = addr;
        g_net.has_peer_addr = 1;
        g_net.remote_port = remote_port;
    }
    LOG_INFO("ggpo.net: %s peer endpoint %s:%u mode=%s local_port=%u candidates=%d",
             changed ? "candidate" : "set",
             host ? host : "",
             (unsigned int)remote_port,
             ggpo_net_mode_name(),
             (unsigned int)g_net.local_port,
             g_net.candidate_count);
    g_net.last_handshake_burst_tick = 0u;
    if (replace_stale_prematch_candidates && ggpo_net_link_confirmed()) {
        /* Keep the confirmed endpoint pinned until this replacement proves a
         * fresh authenticated peer session. Probe only the server-published
         * address; same-session packets from it remain rejected. */
        for (uint32_t i = 0; i < GGPO_NET_PUNCH_HELLO_BURST * 2u; i++) {
            (void)ggpo_net_send_packet_to(GGPO_NET_PACKET_HELLO, &addr);
        }
    } else {
        ggpo_net_send_handshake_burst(GGPO_NET_PUNCH_HELLO_BURST * 2u);
    }
    return 1;
}

int ggpo_net_add_peer_candidate(const char* host, uint16_t remote_port, char* err, size_t err_cap) {
    struct sockaddr_in addr;
    if (!g_net.active || g_net.sock == INVALID_SOCKET) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }
    if (remote_port == 0 || !host || !host[0]) return 0;
    if (!ggpo_net_resolve_peer(host, remote_port, &addr, err, err_cap)) return 0;
    ggpo_net_add_candidate_addr(&addr);
    /* Seed peer_addr if we don't have one yet (so sends have a default target). */
    if (!g_net.has_peer_addr) {
        g_net.peer_addr = addr;
        g_net.has_peer_addr = 1;
        g_net.remote_port = remote_port;
    }
    g_net.last_handshake_burst_tick = 0u;
    ggpo_net_send_handshake_burst(GGPO_NET_PUNCH_HELLO_BURST);
    return 1;
}

int ggpo_net_start_join(const char* host, uint16_t remote_port, uint16_t local_port, char* err, size_t err_cap) {
    if (remote_port == 0) remote_port = GGPO_NET_DEFAULT_PORT;
    if (!ggpo_net_start_common(GGPO_NET_MODE_JOIN, local_port, err, err_cap)) {
        return 0;
    }
    if (!ggpo_net_set_peer(host, remote_port, err, err_cap)) {
        ggpo_net_stop();
        return 0;
    }
    LOG_INFO("ggpo.net: joining %s:%u local_port=%u input_delay=%u max_advantage=%u max_prediction=%u state_size=%u checksum=%u",
             host ? host : "",
             (unsigned int)remote_port,
             (unsigned int)g_net.local_port,
             (unsigned int)g_net.input_delay,
             (unsigned int)g_net.max_frame_advantage,
             (unsigned int)g_net.max_prediction,
             (unsigned int)g_net.state_size,
             (unsigned int)g_net.initial_checksum);
    return 1;
}

int ggpo_net_send_server_probe(const char* host,
                               uint16_t port,
                               int match_id,
                               const char* username,
                               const char* token,
                               char* err,
                               size_t err_cap) {
    char user_json[96];
    char token_json[128];
    char line[384];
    int len;
    if (!g_net.active || g_net.sock == INVALID_SOCKET) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }
    if (!host || !host[0] || port == 0) {
        ggpo_net_set_err(err, err_cap, "missing probe server");
        return 0;
    }
    if (match_id <= 0 || !username || !username[0] || !token || !token[0]) {
        ggpo_net_set_err(err, err_cap, "missing probe identity");
        return 0;
    }
    if (!g_net.has_probe_server_addr ||
        g_net.probe_server_port != port ||
        strcmp(g_net.probe_server_host, host) != 0) {
        if (!ggpo_net_resolve_peer(host, port, &g_net.probe_server_addr, err, err_cap)) {
            return 0;
        }
        snprintf(g_net.probe_server_host, sizeof(g_net.probe_server_host), "%s", host);
        g_net.probe_server_port = port;
        g_net.has_probe_server_addr = 1;
    }
    ggpo_net_json_escape(user_json, sizeof(user_json), username);
    ggpo_net_json_escape(token_json, sizeof(token_json), token);
    len = snprintf(line,
                   sizeof(line),
                   "{\"type\":\"p2p_probe\",\"match_id\":%d,\"username\":\"%s\",\"token\":\"%s\",\"local_port\":%u}\n",
                   match_id,
                   user_json,
                   token_json,
                   (unsigned int)g_net.local_port);
    if (len <= 0 || len >= (int)sizeof(line)) {
        ggpo_net_set_err(err, err_cap, "probe packet too large");
        return 0;
    }
    if (!ggpo_net_send_raw_bytes(line, len, &g_net.probe_server_addr)) {
        ggpo_net_set_err(err, err_cap, "probe send failed");
        return 0;
    }
    return 1;
}

/* Cheap: copy a compact signature of this frame's RNG draws into the ring. No
 * formatting or I/O happens here, so it is safe to run every live frame. */
static void ggpo_net_record_rng_frame(uint32_t frame, uint32_t local_cmd, uint32_t remote_cmd,
                                      uint32_t seed_before, uint32_t seed_after, const HooksRngTrace* tr) {
    GgpoNetRngFrame* slot = &g_rng_ring[frame % GGPO_NET_RNG_RING];
    uint32_t n = tr ? tr->count : 0u;
    uint32_t cap = (n > GGPO_NET_RNG_FRAME_DRAWS) ? GGPO_NET_RNG_FRAME_DRAWS : n;
    uint32_t i;
    slot->valid = 1;
    slot->frame = frame;
    slot->seed_before = seed_before;
    slot->seed_after = seed_after;
    slot->local_cmd = local_cmd;
    slot->remote_cmd = remote_cmd;
    slot->count = n;
    slot->stored = cap;
    for (i = 0; i < cap; i++) {
        slot->kinds[i] = (uint8_t)tr->events[i].kind;
        slot->callers[i] = (uint32_t)tr->events[i].caller;
    }
}

/* Dump the recent ring (frames that drew RNG) around a desync. Written to the
 * dedicated APPEND-mode file mods/desync_dump.log (NOT the "w"-truncated main
 * log), tagged p=0/p=1, so both clients sharing a folder accumulate their dumps
 * without clobbering. Align two peers' lines by f= and seed0; the first frame
 * whose seed_after diverges (with matching seed_before) is the offending draw,
 * identifiable by its caller list. Both peers now dump the same round (detector
 * via recoverable_desync; responder via prepare_host_correction / correction
 * apply), so a same-seed0 p=0/p=1 pair is always available to diff. */
static void ggpo_net_dump_rng_ring(uint32_t desync_frame) {
    uint32_t window = GGPO_NET_RNG_RING - 8u;
    uint32_t newest = g_net.frame;
    uint32_t oldest = (newest > window) ? newest - window : 0u;
    uint32_t f;
    if (!g_net_config_rng_trace) return;
    if (g_rng_dump_count >= 4u) return; /* cap per session: ~2 rounds of p0+p1, so a
                                         * correction storm can't cause a cascade of
                                         * heavy ring-dump freezes (was 16). */
    g_rng_dump_count++;
    LOG_INFO("ggpo.rngtrace DUMP p=%d desync_frame=%u window=%u..%u -> mods/desync_dump.log",
             g_net.local_player, (unsigned int)desync_frame,
             (unsigned int)oldest, (unsigned int)newest);
    log_dump_line("ggpo.rngtrace DUMP p=%d desync_frame=%u sid=%016llX fpcw=0x%04X window=%u..%u",
                   g_net.local_player, (unsigned int)desync_frame,
                   (unsigned long long)g_net.session_id,
                  (unsigned int)(_controlfp(0, 0) & 0xFFFFu),
                  (unsigned int)oldest, (unsigned int)newest);
    for (f = oldest; f <= newest; f++) {
        GgpoNetRngFrame* slot = &g_rng_ring[f % GGPO_NET_RNG_RING];
        char buf[640];
        int off = 0;
        uint32_t i;
        if (!slot->valid || slot->frame != f || slot->count == 0u) continue;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                        "ggpo.rngtrace f=%u d=%u p=%d cmd=%08X/%08X s=%u->%u n=%u%s:",
                        (unsigned int)f,
                        (unsigned int)desync_frame,
                        g_net.local_player,
                        (unsigned int)slot->local_cmd,
                        (unsigned int)slot->remote_cmd,
                        (unsigned int)slot->seed_before,
                        (unsigned int)slot->seed_after,
                        (unsigned int)slot->count,
                        (slot->count > slot->stored) ? "+" : "");
        for (i = 0; i < slot->stored && off < (int)sizeof(buf) - 16; i++) {
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                            " %u@%08X",
                            (unsigned int)slot->kinds[i],
                            (unsigned int)slot->callers[i]);
        }
        log_dump_line("%s", buf);
    }
    log_dump_flush();
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
    int correction_wait_expired = 0;
    GgpoNetHistoryEntry* h = NULL;
    uint32_t checksum = 0;

    if (out_advanced) *out_advanced = 0;
    if (!ggpo_net_service_transport(GGPO_NET_PACKET_INPUT, err, err_cap)) {
        return 0;
    }

    /* Defensive misuse guard: even if a caller accidentally invokes advance
     * from the countdown/hub, a held local or peer session remains transport-
     * only and reports a successful no-op simulation tick. */
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) {
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (g_net.awaiting_correction) {
        uint32_t wait_ticks = 0;
        if (g_net.awaiting_correction && g_net.mode == GGPO_NET_MODE_JOIN) {
            (void)ggpo_net_request_host_correction("awaiting correction", 0);
        }
        if (g_net.correction_wait_start_tick == 0u) {
            g_net.correction_wait_start_tick = ggpo_net_now_tick();
        }
        wait_ticks = ggpo_net_correction_wait_ticks();
        if (wait_ticks < GGPO_NET_MAX_BLOCK_TICKS) {
            if (out_checksum) *out_checksum = g_net.last_checksum;
            return 1;
        }
        if (!g_net.correction_wait_cap_announced) {
            LOG_WARN("ggpo.net: correction wait exceeded %u ticks; resuming prediction while state transfer continues",
                     (unsigned int)GGPO_NET_MAX_BLOCK_TICKS);
            g_net.correction_wait_cap_announced = 1;
        }
        correction_wait_expired = 1;
    }

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

    if (ggpo_net_link_confirmed() && (!g_net.state_synced || !g_net.remote_state_synced)) {
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

    if (!ggpo_net_link_confirmed()) {
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (g_net.frame == 0u && !g_net.start_state_loaded) {
        if (ggpo_net_wait_for_cosmetic_profiles(out_checksum)) {
            return 1;
        }
    }

    if (!ggpo_net_load_start_state_if_ready(err, err_cap)) {
        return 0;
    }

    {
        uint32_t sampled_cmd = (g_net.local_player == 0) ? raw_p0 : raw_p1;
        uint32_t input_frame = g_net.frame + g_net.input_delay;
        (void)ggpo_net_queue_local_input(input_frame, sampled_cmd);
    }
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
    ggpo_net_send_cosmetic_profile_periodic();
    ggpo_net_send_cosmetic_asset_periodic();

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

    if (!g_net.correction_active &&
        !correction_wait_expired &&
        g_net.has_remote_frame &&
        g_net.frame > g_net.remote_frame + g_net.max_frame_advantage) {
        uint32_t wait_ticks = 0;
        if (g_net.frame_advantage_wait_start_tick == 0u) {
            g_net.frame_advantage_wait_start_tick = ggpo_net_now_tick();
            g_net.frame_advantage_wait_cap_announced = 0;
        }
        wait_ticks = g_net.service_tick - g_net.frame_advantage_wait_start_tick;
        if (wait_ticks < GGPO_NET_MAX_BLOCK_TICKS) {
            g_net.frame_advantage_stalls++;
            if (!g_net.frame_advantage_wait_announced) {
                LOG_DEBUG("ggpo.net: throttling local frame=%u remote_frame=%u max_advantage=%u",
                          (unsigned int)g_net.frame,
                          (unsigned int)g_net.remote_frame,
                          (unsigned int)g_net.max_frame_advantage);
                g_net.frame_advantage_wait_announced = 1;
            }
            if (out_checksum) *out_checksum = g_net.last_checksum;
            return 1;
        }
        if (!g_net.frame_advantage_wait_cap_announced) {
            LOG_WARN("ggpo.net: frame advantage wait exceeded %u ticks; resuming prediction local_frame=%u remote_frame=%u",
                     (unsigned int)GGPO_NET_MAX_BLOCK_TICKS,
                     (unsigned int)g_net.frame,
                     (unsigned int)g_net.remote_frame);
            g_net.frame_advantage_wait_cap_announced = 1;
        }
    } else {
        g_net.frame_advantage_wait_start_tick = 0;
        g_net.frame_advantage_wait_cap_announced = 0;
        g_net.frame_advantage_wait_announced = 0;
    }

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

    if (!ggpo_net_get_input(g_net.local_inputs, g_net.frame, &local_cmd)) {
        local_cmd = 0u;
    }

    {
        uint32_t oldest_missing = 0;
        if (!correction_wait_expired && ggpo_net_prediction_stall_needed(g_net.frame, &oldest_missing)) {
            uint32_t wait_ticks = 0;
            if (g_net.prediction_wait_start_tick == 0u) {
                g_net.prediction_wait_start_tick = ggpo_net_now_tick();
                g_net.prediction_wait_cap_announced = 0;
            }
            wait_ticks = g_net.service_tick - g_net.prediction_wait_start_tick;
            if (wait_ticks < GGPO_NET_MAX_BLOCK_TICKS) {
                g_net.prediction_stalls++;
                if (!g_net.prediction_limit_wait_announced) {
                    LOG_DEBUG("ggpo.net: stalling at frame=%u to keep prediction within %u frames (oldest_missing=%u)",
                              (unsigned int)g_net.frame,
                              (unsigned int)g_net.max_prediction,
                              (unsigned int)oldest_missing);
                    g_net.prediction_limit_wait_announced = 1;
                }
                if (out_checksum) *out_checksum = g_net.last_checksum;
                return 1;
            }
            if (!g_net.prediction_wait_cap_announced) {
                LOG_WARN("ggpo.net: prediction wait exceeded %u ticks; continuing past predcap=%u oldest_missing=%u current=%u",
                         (unsigned int)GGPO_NET_MAX_BLOCK_TICKS,
                         (unsigned int)g_net.max_prediction,
                         (unsigned int)oldest_missing,
                         (unsigned int)g_net.frame);
                g_net.prediction_wait_cap_announced = 1;
            }
        } else {
            g_net.prediction_wait_start_tick = 0;
            g_net.prediction_wait_cap_announced = 0;
            g_net.prediction_limit_wait_announced = 0;
        }
    }

    remote_cmd = ggpo_net_predict_remote(g_net.frame, &predicted);
    if (predicted && !g_net.warned_prediction_limit) {
        uint32_t oldest_missing = g_net.frame;
        uint32_t max_prediction = g_net.max_prediction ? g_net.max_prediction : GGPO_NET_DEFAULT_MAX_PREDICTION;
        for (uint32_t f = (g_net.frame > max_prediction) ? g_net.frame - max_prediction : 0; f <= g_net.frame; f++) {
            uint32_t tmp = 0;
            if (!ggpo_net_get_input(g_net.remote_inputs, f, &tmp)) {
                oldest_missing = f;
                break;
            }
        }
        if (g_net.frame - oldest_missing >= max_prediction) {
            LOG_WARN("ggpo.net: predicting remote input for %u+ frames (oldest_missing=%u current=%u)",
                     (unsigned int)max_prediction,
                     (unsigned int)oldest_missing,
                     (unsigned int)g_net.frame);
            g_net.warned_prediction_limit = 1;
        }
    }

    /* Sim-only seed: undo any out-of-tick perturbation (render since the last tick,
     * or a round/stage transition) by restoring the post-last-tick seed BEFORE we
     * snapshot the pre-state. Otherwise save_pre_state bakes the non-synced
     * perturbation into the checksummed state -> desync next confirmed frame. */
    if (g_net.have_clean_mrand_seed) {
        (void)lua_manager_game_set_rng_seed(g_net.clean_mrand_seed);
        (void)lua_manager_game_set_camera(g_net.clean_camera_x, g_net.clean_camera_y);
    }

    if (!ggpo_net_save_pre_state(g_net.frame, &h, NULL, err, err_cap)) {
        return 0;
    }

    {
        GgpoFrameInputs inputs;
        int trace = g_net_config_rng_trace;
        uint32_t seed_before = 0;
        memset(&inputs, 0, sizeof(inputs));
        inputs.player_cmd[g_net.local_player] = local_cmd;
        inputs.player_cmd[g_net.remote_player] = remote_cmd;
        if (trace) {
            (void)lua_manager_game_rng_seed(&seed_before);
            hooks_rng_trace_begin(g_net.frame, 0u);
        }
        if (!ggpo_ext_advance_frame(&inputs, arg0, &checksum, err, err_cap)) {
            if (trace) hooks_rng_trace_end();
            return 0;
        }
        if (trace) {
            HooksRngTrace tr;
            uint32_t seed_after = 0;
            hooks_rng_trace_copy(&tr);
            hooks_rng_trace_end();
            (void)lua_manager_game_rng_seed(&seed_after);
            ggpo_net_record_rng_frame(g_net.frame, local_cmd, remote_cmd, seed_before, seed_after, &tr);
        }
    }

    h->local_cmd = local_cmd;
    h->remote_cmd = remote_cmd;
    h->remote_predicted = predicted;
    h->post_checksum = checksum;
    ggpo_net_capture_history_summary(h);

    /* Sim-only seed: snapshot the post-tick seed so the next tick can restore it
     * (above), nullifying any out-of-tick draws that happen before then. */
    {
        uint32_t post_seed = 0;
        float cx = 0.0f, cy = 0.0f;
        if (lua_manager_game_rng_seed(&post_seed)) {
            g_net.clean_mrand_seed = post_seed;
            (void)lua_manager_game_camera(&cx, &cy);
            g_net.clean_camera_x = cx;
            g_net.clean_camera_y = cy;
            g_net.have_clean_mrand_seed = 1;
        }
    }

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

int ggpo_net_catchup_pending(void) {
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 0;
    if (!g_net.active || !ggpo_net_link_confirmed() || !g_net.start_state_loaded) return 0;
    if (g_net.desync_detected || g_net.peer_disconnected) return 0;
    if (g_net.correction_active || g_net.awaiting_correction) return 0;
    if (!g_net.has_remote_frame) return 0;
    return (g_net.frame + 1u < g_net.remote_frame) ? 1 : 0;
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

uint32_t ggpo_net_auth_rejected_packets(void) {
    return g_net.auth_rejected_packets;
}

static const char* ggpo_net_addr_str(const struct sockaddr_in* a, char* buf, size_t cap) {
    /* single-threaded netcode; inet_ntoa's static buffer is fine here. */
    snprintf(buf, cap, "%s:%u", inet_ntoa(a->sin_addr), (unsigned int)ntohs(a->sin_port));
    return buf;
}

/* Human-readable P2P connection troubleshooter. Writes a multi-line report into
 * `out` describing the hole-punch state and a plain-English verdict on what is
 * blocking the connection. Console exposes this as `net.diag`. */
void ggpo_net_format_diag(char* out, size_t cap) {
    char line[192];
    char addr[64];
    size_t len = 0;
    int i;

#define DIAG_LINE(...) do { \
        int _n = snprintf(line, sizeof(line), __VA_ARGS__); \
        if (_n < 0) _n = 0; \
        if (len + (size_t)_n + 1 < cap) { \
            memcpy(out + len, line, (size_t)_n); len += (size_t)_n; \
            out[len++] = '\n'; out[len] = '\0'; \
        } \
    } while (0)

    if (!out || cap == 0) return;
    out[0] = '\0';

    if (!g_net.active) {
        DIAG_LINE("P2P: no session active (not in an online match).");
        return;
    }

    DIAG_LINE("session: connected=%s mode=%s local_player=%d",
              ggpo_net_link_confirmed() ? "YES" : (g_net.connected ? "one-way" : "no"),
              g_net.mode == GGPO_NET_MODE_HOST ? "host" : "join",
              g_net.local_player);
    DIAG_LINE("auth:    HMAC-SHA-256/128 replay_window=%u rejected=%u",
              (unsigned int)GGPO_NET_REPLAY_WINDOW,
              (unsigned int)g_net.auth_rejected_packets);
    DIAG_LINE("local:   udp_port=%u", (unsigned int)g_net.local_port);
    DIAG_LINE("prematch: local=%s peer=%s hold_epoch=%u/%u state_epoch=%u seen=%u floor=%u",
              g_net.prematch_hold ? "HELD" : "released",
              !g_net.remote_prematch_hold_known ? "unknown" :
                  (g_net.remote_prematch_hold ? "HELD" : "released"),
              (unsigned int)g_net.hold_epoch,
              (unsigned int)g_net.remote_hold_epoch,
              (unsigned int)g_net.state_epoch,
              (unsigned int)g_net.remote_state_epoch_seen,
              (unsigned int)g_net.hold_remote_epoch_floor);
    if (g_net.held_state_chunks_dropped != 0u ||
        g_net.stale_state_chunks_dropped != 0u ||
        g_net.held_control_packets_dropped != 0u) {
        DIAG_LINE("quarantine: held_state=%u stale_state=%u control=%u",
                  (unsigned int)g_net.held_state_chunks_dropped,
                  (unsigned int)g_net.stale_state_chunks_dropped,
                  (unsigned int)g_net.held_control_packets_dropped);
    }

    if (g_net.has_peer_addr) {
        DIAG_LINE("peer:    %s", ggpo_net_addr_str(&g_net.peer_addr, addr, sizeof(addr)));
    } else {
        DIAG_LINE("peer:    (none adopted yet)");
    }

    DIAG_LINE("candidates: %d", g_net.candidate_count);
    for (i = 0; i < g_net.candidate_count; i++) {
        int adopted = g_net.has_peer_addr &&
                      ggpo_net_addr_equal(&g_net.candidates[i], &g_net.peer_addr);
        ggpo_net_addr_str(&g_net.candidates[i], addr, sizeof(addr));
        DIAG_LINE("  %s %s", adopted ? "->" : "  ", addr);
    }

    if (g_net.packets_received == 0) {
        DIAG_LINE("traffic: sent=%u recv=0 (nothing received) alt_endpoint=%u",
                  (unsigned int)g_net.packets_sent,
                  (unsigned int)g_net.alternate_endpoint_updates);
    } else {
        uint32_t age = (g_net.service_tick >= g_net.last_rx_tick)
                     ? (g_net.service_tick - g_net.last_rx_tick) : 0u;
        DIAG_LINE("traffic: sent=%u recv=%u last_recv=%ut ago alt_endpoint=%u",
                  (unsigned int)g_net.packets_sent,
                  (unsigned int)g_net.packets_received,
                  (unsigned int)age,
                  (unsigned int)g_net.alternate_endpoint_updates);
    }

    /* Verdict */
    if (ggpo_net_link_confirmed()) {
        DIAG_LINE("VERDICT: connected - the P2P link is up.");
    } else if (g_net.candidate_count == 0) {
        DIAG_LINE("VERDICT: no opponent address yet from the matchmaking server");
        DIAG_LINE("         (match just started, or a server/matchmaking problem).");
    } else if (g_net.packets_received == 0) {
        DIAG_LINE("VERDICT: sending to the opponent but 0 packets received back.");
        DIAG_LINE("         Their UDP isn't reaching you - most likely a strict or");
        DIAG_LINE("         symmetric NAT, or a firewall blocking inbound UDP.");
        DIAG_LINE("         Try: allow the game in Windows Firewall, enable UPnP on");
        DIAG_LINE("         the router, or test on a different network.");
    } else {
        DIAG_LINE("VERDICT: receiving packets but the handshake did not finish -");
        DIAG_LINE("         possible build/version mismatch (both PCs need the same");
        DIAG_LINE("         SDL2.dll) or a session mismatch.");
    }
    if (g_net.alternate_endpoint_updates > 0) {
        DIAG_LINE("note: opponent's live port differed from the server's guess (NAT");
        DIAG_LINE("      remap) - adopted the real source %u time(s).",
                  (unsigned int)g_net.alternate_endpoint_updates);
    }

#undef DIAG_LINE
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

uint32_t ggpo_net_prediction_stall_count(void) {
    return g_net.prediction_stalls;
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

uint32_t ggpo_net_peer_silence_ticks(void) {
    if (!g_net.active || !ggpo_net_link_confirmed() || g_net.last_rx_tick == 0) return 0u;
    return g_net.service_tick - g_net.last_rx_tick;
}
