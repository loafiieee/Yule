#include "ggpo_net.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include "ggpo_ext.h"
#include "fp_control.h"
#include "hooks.h"
#include "log.h"
#include "lua_manager.h"

#define GGPO_NET_MAGIC 0x50474E45u
#define GGPO_NET_VERSION GGPO_NET_PROTOCOL_VERSION
#define GGPO_NET_HISTORY_FRAMES 512u
#define GGPO_NET_PACKET_INPUTS 64
#define GGPO_NET_PACKET_CHECKSUMS 32
/* Kept small on purpose: the per-frame packet is a FIXED sizeof(GgpoNetPacket)
 * sent ~60x/sec, and each summary is ~148 bytes. Keeping the whole packet under
 * the ~1472-byte UDP/MTU payload avoids IP fragmentation, which on real networks
 * (unlike loopback) turns one dropped fragment into a lost packet -> prediction
 * stalls -> "slow/laggy" online play. Summaries are only used for desync DETAIL
 * (diagnostic); detection uses the 4-byte checksums, so 2 is plenty. */
#define GGPO_NET_PACKET_SUMMARIES 2
#define GGPO_NET_INPUT_ACK_WORDS 16u
#define GGPO_NET_INPUT_ACK_BITS (GGPO_NET_INPUT_ACK_WORDS * 32u)
#define GGPO_NET_CHAOS_EVENT_CAP 8192u
#define GGPO_NET_INPUT_ACK_VALID 0x01u
#define GGPO_NET_CONFIRMED_FRAME_VALID 0x02u
#define GGPO_NET_INPUT_ACK_KNOWN_FLAGS \
    (GGPO_NET_INPUT_ACK_VALID | GGPO_NET_CONFIRMED_FRAME_VALID)
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
#define GGPO_NET_CHECKSUM_STALL_TIMEOUT_TICKS GGPO_NET_TIMEOUT_TICKS
#define GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS 3u
#define GGPO_NET_MAX_BLOCK_TICKS 60
/* Keep bulk state traffic bounded behind the fresh per-service control packet.
 * A large burst can fill the UDP send buffer and starve the next INPUT/ACK. */
#define GGPO_NET_CORRECTION_BURST_CHUNKS 8
/* Count every receive attempt, including unauthenticated/malformed traffic and
 * recoverable socket errors, so inbound traffic cannot monopolize a tick. */
#define GGPO_NET_RECEIVE_BUDGET 64u
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
/* Keep the complete admitted receive interval narrower than one ring
 * generation. The peer may legitimately queue up to MAX_INPUT_DELAY frames
 * beyond its simulation position and the configured prediction cap may be as
 * high as MAX_PREDICTION_LIMIT. Reserving that much future space leaves more
 * than a full maximum rollback window behind the local frame, while ensuring
 * two admitted frame numbers can never alias the same 512-entry slot. */
#define GGPO_NET_REMOTE_LEAD_LIMIT \
    ((GGPO_NET_MAX_PREDICTION_LIMIT > GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT) \
        ? GGPO_NET_MAX_PREDICTION_LIMIT \
        : GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT)
#define GGPO_NET_INPUT_FUTURE_FRAMES \
    (GGPO_NET_REMOTE_LEAD_LIMIT + GGPO_NET_MAX_INPUT_DELAY)
#define GGPO_NET_INPUT_RETENTION_FRAMES \
    (GGPO_NET_HISTORY_FRAMES - GGPO_NET_INPUT_FUTURE_FRAMES - 1u)
#if GGPO_NET_INPUT_FUTURE_FRAMES >= GGPO_NET_HISTORY_FRAMES
#error "GGPO receive window must be smaller than the frame-history ring"
#endif
#if GGPO_NET_INPUT_RETENTION_FRAMES < GGPO_NET_MAX_PREDICTION_LIMIT
#error "GGPO receive retention must cover the maximum rollback prediction window"
#endif
#define GGPO_NET_REPLAY_WINDOW 4096u

#define GGPO_NET_PACKET_HELLO 1u
#define GGPO_NET_PACKET_INPUT 2u
#define GGPO_NET_PACKET_BYE   3u
#define GGPO_NET_PACKET_STATE_CHUNK 4u
#define GGPO_NET_PACKET_STATE_ACK   5u
#define GGPO_NET_PACKET_RESYNC_REQUEST 6u
#define GGPO_NET_PACKET_COSMETICS 7u
#define GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK 8u
#define GGPO_NET_PACKET_PALETTE 9u

#define GGPO_NET_STATE_FLAG_CORRECTION 1u
#define GGPO_NET_STATE_FLAG_DELTA      2u

/* Correction control is a repeated, authenticated state machine carried by
 * every ordinary packet.  The numeric values are wire ABI for v17. */
enum {
    GGPO_NET_CORRECTION_NONE = 0u,
    GGPO_NET_CORRECTION_REQUEST = 1u,
    GGPO_NET_CORRECTION_OFFER = 2u,
    GGPO_NET_CORRECTION_RECEIVING = 3u,
    GGPO_NET_CORRECTION_READY = 4u,
    GGPO_NET_CORRECTION_COMMIT = 5u,
    GGPO_NET_CORRECTION_APPLIED = 6u,
    GGPO_NET_CORRECTION_RELEASE = 7u,
    GGPO_NET_CORRECTION_RELEASE_ACK = 8u
};

typedef struct GgpoNetInputEntry {
    int valid;
    uint32_t frame;
    uint32_t cmd;
    /* Local-input entries retain selective peer receipt independently of a
     * rebased packet bitmap. Unused for remote_inputs. */
    int peer_acked;
} GgpoNetInputEntry;

typedef struct GgpoNetRemoteChecksumEntry {
    int valid;
    int verified;
    int has_summary;
    uint32_t frame;
    uint32_t checksum;
    LuaGameStateRollbackSummary summary;
} GgpoNetRemoteChecksumEntry;

typedef struct GgpoNetHistoryEntry {
    int valid;
    uint32_t frame;
    size_t state_len;
    uint32_t pre_checksum;
    uint32_t post_checksum;
    uint32_t local_cmd;
    uint32_t remote_cmd;
    int remote_predicted;
    /* Set once this exact finalized checksum has been offered in an INPUT
     * packet. A cumulative checksum ACK may never claim an unpublished frame. */
    int checksum_published;
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
    /* A correction tuple is (state_epoch, correction_id, snapshot_frame,
     * resume_frame).  `phase` and the phase-specific checksum above are
     * repeated until the peer advances the state machine. */
    uint32_t correction_phase;
    uint32_t correction_snapshot_frame;
    uint32_t correction_resume_frame;
    /* The host increments state_epoch whenever it publishes a new authoritative
     * frame-0 state. A join peer echoes the epoch it actually applied. This
     * prevents delayed countdown packets from being accepted after release. */
    uint32_t state_epoch;
    uint32_t prematch_hold;
    uint32_t hold_epoch;
    uint32_t frame;
    uint32_t state_size;
    uint32_t state_checksum;
    /* Receiver-unused in the original v16 layout. It now carries the frozen
     * rollback schema/map-layout identity without changing the packed wire ABI. */
    uint32_t state_layout_id;
    uint32_t input_count;
    uint32_t checksum_count;
    uint32_t summary_count;
    uint32_t send_tick;   /* sender's service tick when this packet was built */
    uint32_t tick_echo;   /* most recent send_tick the sender has seen from us */
    /* ACK_VALID means input_ack_base is the highest input received contiguously
     * from frame 0. Without it, SACK bit i names frame i. With it, bit i names
     * input_ack_base + 1 + i. CONFIRMED_FRAME_VALID gates checksum payloads. */
    uint32_t input_ack_flags;
    uint32_t input_ack_base;
    uint32_t confirmed_frame;
    /* First checksum frame from this (state_epoch, correction_id) generation
     * that the sender has not yet compared successfully. This cumulative
     * go-back-N ACK needs no sentinel because both peers know the generation's
     * start frame (zero, or the authoritative correction frame). */
    uint32_t checksum_ack_next;
    uint32_t input_ack_bits[GGPO_NET_INPUT_ACK_WORDS];
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

/* Deliberately separate from the disabled generic cosmetic payload. Palette
 * choices are two bounded presentation IDs plus an exact peer echo; no JSON,
 * assets or mod-defined bytes cross this channel. */
typedef struct GgpoNetPalettePacket {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t session_id;
    uint32_t sender_player;
    uint64_t auth_sequence;
    uint8_t auth_tag[GGPO_NET_AUTH_TAG_BYTES];
    uint32_t palette_count;
    uint32_t skin_index;
    uint32_t clothing_index;
    uint32_t ack_valid;
    uint32_t ack_palette_count;
    uint32_t ack_skin_index;
    uint32_t ack_clothing_index;
} GgpoNetPalettePacket;
#pragma pack(pop)

#define GGPO_NET_MAX2(a, b) ((sizeof(a) > sizeof(b)) ? sizeof(a) : sizeof(b))
#define GGPO_NET_MAX_PACKET_BYTES \
    (GGPO_NET_MAX2(GgpoNetPacket, GgpoNetStateChunkPacket) > GGPO_NET_MAX2(GgpoNetCosmeticPacket, GgpoNetCosmeticAssetChunkPacket) ? \
        (GGPO_NET_MAX2(GgpoNetPacket, GgpoNetStateChunkPacket) > sizeof(GgpoNetPalettePacket) ? \
            GGPO_NET_MAX2(GgpoNetPacket, GgpoNetStateChunkPacket) : sizeof(GgpoNetPalettePacket)) : \
        (GGPO_NET_MAX2(GgpoNetCosmeticPacket, GgpoNetCosmeticAssetChunkPacket) > sizeof(GgpoNetPalettePacket) ? \
            GGPO_NET_MAX2(GgpoNetCosmeticPacket, GgpoNetCosmeticAssetChunkPacket) : sizeof(GgpoNetPalettePacket)))

/* Keep the fixed 60 Hz input packet comfortably below the typical 1472-byte
 * IPv4 UDP payload. A protocol edit that crosses this boundary must fail the
 * build instead of silently introducing fragmentation on real networks. */
typedef char GgpoNetInputPacketMustFitMtu[(sizeof(GgpoNetPacket) <= 1400u) ? 1 : -1];
typedef char GgpoNetInputPacketSizeIsV17[(sizeof(GgpoNetPacket) == 1292u) ? 1 : -1];
typedef char GgpoNetPalettePacketSizeIsFixed[(sizeof(GgpoNetPalettePacket) == 72u) ? 1 : -1];
typedef char GgpoNetAckMustCoverHistory[
    (GGPO_NET_INPUT_ACK_BITS >= GGPO_NET_HISTORY_FRAMES) ? 1 : -1];

typedef struct GgpoNetQueuedPacket {
    int valid;
    uint32_t send_tick;
    int len;
    struct sockaddr_in addr;
    uint8_t bytes[GGPO_NET_MAX_PACKET_BYTES];
} GgpoNetQueuedPacket;

enum {
    GGPO_NET_CHAOS_DROP = 1u,
    GGPO_NET_CHAOS_DELAY = 2u,
    GGPO_NET_CHAOS_QUEUE_DROP = 3u
};

typedef struct GgpoNetChaosEvent {
    uint32_t sequence;
    uint32_t service_tick;
    uint32_t value;
    uint16_t kind;
    uint16_t packet_type;
} GgpoNetChaosEvent;

typedef enum GgpoNetRawSendResult {
    GGPO_NET_RAW_SEND_HARD_ERROR = -1,
    GGPO_NET_RAW_SEND_WOULD_BLOCK = 0,
    GGPO_NET_RAW_SEND_SENT = 1
} GgpoNetRawSendResult;

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
    int frame_counter_wrapped;
    uint32_t last_checksum;
    uint32_t initial_checksum;
    uint32_t remote_frame;
    int has_remote_frame;
    uint32_t remote_contiguous_input_frame;
    int has_remote_contiguous_input_frame;
    uint32_t peer_acked_local_input_frame;
    int has_peer_acked_local_input_frame;
    int has_peer_input_ack;
    uint32_t highest_local_input_frame;
    int has_highest_local_input_frame;
    /* Checksums form a reliable ordered stream inside one authoritative state
     * generation. remote_checksum_ack_next is our outgoing cumulative ACK;
     * peer_checksum_ack_next is the peer's proof for our local history. */
    uint32_t checksum_stream_start_frame;
    uint32_t remote_checksum_ack_next;
    uint32_t peer_checksum_ack_next;
    uint32_t checksum_wait_start_tick;
    uint32_t checksum_wait_frame;
    int checksum_wait_announced;
    size_t state_size;
    size_t remote_state_size;
    uint32_t state_layout_id;
    uint32_t remote_state_layout_id;
    int state_layout_finalized;
    int state_layout_conflict;
    uint8_t* state_blobs;
    uint8_t* initial_state;
    uint8_t* correction_state;
    uint8_t* correction_base_state;
    /* Scratch storage for applying an untrusted network snapshot. The exact
     * live serializer image is backed up before the first mutating load and
     * independently re-captured after a restore, so a failed correction can
     * never leave a partially written game state behind. */
    uint8_t* apply_backup_state;
    uint8_t* apply_verify_state;
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
    uint32_t correction_progress_tick;
    uint32_t correction_phase;
    uint32_t correction_peer_phase;
    /* Terminal diagnostics retain the exact authenticated barrier carried by
     * a BYE even if the local peer consumed RELEASE_ACK one service tick
     * earlier and already cleared the active correction state. */
    uint32_t disconnect_correction_phase;
    uint32_t disconnect_correction_peer_phase;
    uint32_t correction_resume_frame;
    uint32_t correction_transcript;
    uint32_t correction_expected_transcript;
    uint32_t correction_input_count;
    uint32_t correction_inputs[GGPO_NET_HISTORY_FRAMES][2];
    uint32_t correction_apply_attempts;
    uint32_t correction_base_checksum;
    uint32_t last_correction_ack_checksum;
    uint32_t last_correction_ack_id;
    uint32_t last_correction_applied_checksum;
    uint32_t last_correction_applied_id;
    uint32_t last_resync_request_tick;
    int correction_active;
    int correction_send_delta;
    int awaiting_correction;
    int correction_snapshot_ready;
    int correction_local_applied;
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
    GgpoNetRemoteChecksumEntry remote_checksums[GGPO_NET_HISTORY_FRAMES];
    uint32_t rollback_to;
    int rollback_pending;
    /* Clean simulation snapshot taken immediately after each successful tick
     * (and after rollback/correction replay). It is restored before the next
     * pre-state save so out-of-tick render/layout work cannot perturb native
     * simulation inputs. Shake belongs here because it consumes gameplay RNG
     * and writes camera X/Y. Both logical dimensions belong here because
     * adjust_layout rewrites them from local mad_w/mad_h, while native gameplay
     * uses width for respawn/clamping and height for camera/map-RNG branches. */
    uint32_t clean_mrand_seed;
    float clean_camera_x;
    float clean_camera_y;
    float clean_camera_shake;
    float clean_camera_shake_decay;
    float clean_game_w;
    float clean_game_h;
    int have_clean_sim_state;
    float local_render_game_w;
    float local_render_game_h;
    int have_local_render_geometry;
    uint32_t last_remote_cmd;
    uint32_t last_remote_cmd_frame;
    int has_last_remote_cmd;
    int warned_initial_mismatch;
    int warned_prediction_limit;
    int desync_detected;
    int desync_repro_captured;
    uint32_t desync_frame;
    uint32_t desync_local_checksum;
    uint32_t desync_remote_checksum;
    uint32_t predictions;
    uint32_t rollbacks;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t socket_would_block_events;
    uint32_t socket_send_errors;
    uint32_t socket_send_work_deferred;
    uint32_t socket_last_send_error;
    int socket_backpressured_this_tick;
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
    uint32_t checksum_stalls;
    uint32_t frame_advantage_wait_start_tick;
    uint32_t prediction_wait_start_tick;
    uint32_t sim_loss_percent;
    uint32_t sim_delay_min_ticks;
    uint32_t sim_delay_max_ticks;
    uint32_t sim_seed;
    uint32_t sim_rng;
    uint32_t sim_packets_dropped;
    uint32_t sim_packets_delayed;
    uint32_t sim_queue_drops;
    uint32_t sim_chaos_event_total;
    GgpoNetChaosEvent sim_chaos_events[GGPO_NET_CHAOS_EVENT_CAP];
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
    uint32_t local_palette_count;
    uint32_t local_palette_skin;
    uint32_t local_palette_clothing;
    uint32_t remote_palette_count;
    uint32_t remote_palette_skin;
    uint32_t remote_palette_clothing;
    uint32_t last_palette_send_tick;
    int local_palette_valid;
    int remote_palette_valid;
    int local_palette_acked;
    int palette_conflict;
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
static int g_net_test_suppress_checksum_payload = 0;
static uint32_t g_net_test_state_chunk_would_block_remaining = 0u;
static int g_net_test_duplicate_pressure_calls = -1;
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
static uint32_t g_net_config_palette_count = 0u;
static uint32_t g_net_config_palette_skin = 0u;
static uint32_t g_net_config_palette_clothing = 0u;
static int g_net_config_palette_valid = 0;

static int ggpo_net_send_packet(uint16_t type);
static void ggpo_net_clear_rollback_history(void);
static void ggpo_net_reset_authoritative_state_bookkeeping(void);
static void ggpo_net_reset_checksum_channel(uint32_t start_frame);
static int ggpo_net_progress_correction(int arg0, char* err, size_t err_cap);

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
static HANDLE g_desync_repro_worker = NULL;
static void ggpo_net_record_rng_frame(uint32_t frame, uint32_t local_cmd, uint32_t remote_cmd,
                                      uint32_t seed_before, uint32_t seed_after, const HooksRngTrace* tr);
static void ggpo_net_dump_rng_ring(uint32_t desync_frame);
static void ggpo_net_capture_first_desync_repro(uint32_t frame,
                                                uint32_t local_checksum,
                                                uint32_t remote_checksum,
                                                int remote_checksum_known);

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

static int ggpo_net_set_correction_base(const uint8_t* state, size_t state_len, uint32_t checksum) {
    if (!g_net.correction_base_state || !state || state_len == 0 || state_len > g_net.state_size) return 0;
    memcpy(g_net.correction_base_state, state, state_len);
    g_net.correction_base_state_len = state_len;
    g_net.correction_base_checksum = checksum;
    return 1;
}

static uint32_t ggpo_net_next_correction_id(void) {
    if (g_net.correction_id == UINT32_MAX) return 0u;
    return g_net.correction_id + 1u;
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

static void ggpo_net_saturating_increment(uint32_t* value) {
    if (value && *value != UINT32_MAX) (*value)++;
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
    static const uint8_t domain[] = "EGGNOGG+ GGPO v17 direction key";
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
    static const uint8_t domain[] = "EGGNOGG+ GGPO v17 packet tag";
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
    static const uint8_t domain[] = "EGGNOGG+ GGPO v17 match auth root";
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

void ggpo_net_test_set_suppress_checksum_payload(int enabled) {
    g_net_test_suppress_checksum_payload = enabled ? 1 : 0;
}

void ggpo_net_test_force_state_chunk_would_block(uint32_t count) {
    g_net_test_state_chunk_would_block_remaining = count;
}

uint32_t ggpo_net_test_correction_phase(void) {
    if (g_net.peer_disconnected &&
        g_net.disconnect_correction_phase != GGPO_NET_CORRECTION_NONE) {
        return g_net.disconnect_correction_phase;
    }
    return g_net.correction_phase;
}

uint32_t ggpo_net_test_correction_peer_phase(void) {
    if (g_net.peer_disconnected &&
        g_net.disconnect_correction_peer_phase !=
            GGPO_NET_CORRECTION_NONE) {
        return g_net.disconnect_correction_peer_phase;
    }
    return g_net.correction_peer_phase;
}

uint32_t ggpo_net_test_correction_snapshot_frame(void) {
    return g_net.correction_frame;
}

uint32_t ggpo_net_test_correction_resume_frame(void) {
    return g_net.correction_resume_frame;
}

uint32_t ggpo_net_test_correction_transcript(void) {
    return g_net.correction_transcript;
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
    build_id = ggpo_net_hash_mix_u32(build_id, (uint32_t)sizeof(GgpoNetPacket));
    /* State capacity is map-dependent and is finalized later in held prematch.
     * The immutable build identity must not depend on whichever map happened to
     * be live when the process first cached this value. */
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

static uint32_t ggpo_net_make_chaos_seed(uint64_t session_id) {
    uint32_t seed = 0u;
    if (BCryptGenRandom(NULL,
                        (PUCHAR)&seed,
                        (ULONG)sizeof(seed),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0 &&
        seed != 0u) {
        return seed;
    }
    /* Chaos is a local diagnostic facility, not a security primitive. Keep
     * the fallback one-way instead of exposing a direct XOR of session bits. */
    seed = ggpo_net_hash_mix_u32(
        2166136261u, (uint32_t)session_id);
    seed = ggpo_net_hash_mix_u32(
        seed, (uint32_t)(session_id >> 32));
    seed = ggpo_net_hash_mix_u32(seed, 0x75BCD15u);
    return seed ? seed : 1u;
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
        g_net.sim_rng = g_net.sim_seed;
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

static void ggpo_net_note_chaos_event(uint16_t kind,
                                      const void* data,
                                      int len,
                                      uint32_t value) {
    GgpoNetChaosEvent* event;
    const GgpoNetPacketPrefix* prefix =
        (const GgpoNetPacketPrefix*)data;
    uint32_t sequence = g_net.sim_chaos_event_total;
    if (!data || len < (int)sizeof(*prefix) ||
        sequence == UINT32_MAX) {
        return;
    }
    event = &g_net.sim_chaos_events[
        sequence % GGPO_NET_CHAOS_EVENT_CAP];
    event->sequence = sequence;
    event->service_tick = g_net.service_tick;
    event->value = value;
    event->kind = kind;
    event->packet_type = prefix->type;
    g_net.sim_chaos_event_total = sequence + 1u;
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

static GgpoNetRawSendResult ggpo_net_note_socket_send_error(int error_code) {
    if (error_code == WSAEWOULDBLOCK || error_code == WSAENOBUFS) {
        ggpo_net_saturating_increment(&g_net.socket_would_block_events);
        g_net.socket_backpressured_this_tick = 1;
        return GGPO_NET_RAW_SEND_WOULD_BLOCK;
    }
    ggpo_net_saturating_increment(&g_net.socket_send_errors);
    g_net.socket_last_send_error = (uint32_t)error_code;
    return GGPO_NET_RAW_SEND_HARD_ERROR;
}

static int ggpo_net_test_state_chunk_block_matches(const void* data, int len) {
#ifdef GGPO_NET_TEST
    const GgpoNetPacketPrefix* prefix = (const GgpoNetPacketPrefix*)data;
    return g_net_test_state_chunk_would_block_remaining != 0u &&
           data && len >= (int)sizeof(*prefix) &&
           prefix->magic == GGPO_NET_MAGIC &&
           prefix->version == GGPO_NET_VERSION &&
           prefix->type == GGPO_NET_PACKET_STATE_CHUNK;
#else
    (void)data;
    (void)len;
    return 0;
#endif
}

static int ggpo_net_test_should_block_state_chunk(const void* data, int len) {
    if (!ggpo_net_test_state_chunk_block_matches(data, len)) return 0;
#ifdef GGPO_NET_TEST
    g_net_test_state_chunk_would_block_remaining--;
#endif
    return 1;
}

static GgpoNetRawSendResult ggpo_net_send_raw_bytes(const void* data,
                                                     int len,
                                                     const struct sockaddr_in* addr) {
    int sent;
#ifdef GGPO_NET_TEST
    if (g_net_test_duplicate_pressure_calls >= 0) {
        if (++g_net_test_duplicate_pressure_calls == 2) {
            return ggpo_net_note_socket_send_error(WSAEWOULDBLOCK);
        }
        return GGPO_NET_RAW_SEND_SENT;
    }
#endif
    if (!data || len <= 0 || !addr || g_net.sock == INVALID_SOCKET) {
        return GGPO_NET_RAW_SEND_HARD_ERROR;
    }
    if (ggpo_net_test_should_block_state_chunk(data, len)) {
        return ggpo_net_note_socket_send_error(WSAEWOULDBLOCK);
    }
    sent = sendto(g_net.sock,
                  (const char*)data,
                  len,
                  0,
                  (const struct sockaddr*)addr,
                  sizeof(*addr));
    if (sent == SOCKET_ERROR) {
        return ggpo_net_note_socket_send_error(WSAGetLastError());
    }
    if (sent != len) {
        /* UDP is all-or-nothing on Winsock. Treat an impossible short datagram
         * as a hard local drop instead of advancing a transfer cursor. */
        return ggpo_net_note_socket_send_error(WSAEMSGSIZE);
    }
    g_net.packets_sent++;
    return GGPO_NET_RAW_SEND_SENT;
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
        ggpo_net_note_chaos_event(
            GGPO_NET_CHAOS_DELAY, data, len, delay_ticks);
        return 1;
    }
    g_net.sim_queue_drops++;
    ggpo_net_note_chaos_event(
        GGPO_NET_CHAOS_QUEUE_DROP, data, len, delay_ticks);
    return 1;
}

static void ggpo_net_flush_sim_queue(void) {
    if (g_net.socket_backpressured_this_tick) {
        if (ggpo_net_sim_pending_count() != 0u) {
            ggpo_net_saturating_increment(&g_net.socket_send_work_deferred);
        }
        return;
    }
    for (int i = 0; i < GGPO_NET_SIM_QUEUE_PACKETS; i++) {
        GgpoNetQueuedPacket* q = &g_net.sim_queue[i];
        GgpoNetRawSendResult result;
        if (!q->valid) continue;
        if (!ggpo_net_tick_reached(g_net.service_tick, q->send_tick)) continue;
        result = ggpo_net_send_raw_bytes(q->bytes, q->len, &q->addr);
#ifdef GGPO_NET_TEST
        /* Replay the identical authenticated datagram after delay/reordering
         * too; duplicating only immediate sends misses most chaos traffic. */
        if (result == GGPO_NET_RAW_SEND_SENT && g_net_test_replay_outgoing) {
            (void)ggpo_net_send_raw_bytes(q->bytes, q->len, &q->addr);
        }
#endif
        if (result == GGPO_NET_RAW_SEND_SENT ||
            result == GGPO_NET_RAW_SEND_HARD_ERROR) {
            q->valid = 0;
        }
        if (result == GGPO_NET_RAW_SEND_WOULD_BLOCK ||
            g_net.socket_backpressured_this_tick) {
            ggpo_net_saturating_increment(&g_net.socket_send_work_deferred);
            break;
        }
    }
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_delayed_duplicate_backpressure(void) {
    int old_replay = g_net_test_replay_outgoing;
    int old_pressure = g_net.socket_backpressured_this_tick;
    uint32_t old_blocks = g_net.socket_would_block_events;
    uint32_t old_deferred = g_net.socket_send_work_deferred;
    int ok;
    if (g_net.active || ggpo_net_sim_pending_count() != 0u) return 0;
    memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
    for (int i = 0; i < 2; i++) {
        g_net.sim_queue[i].valid = 1;
        g_net.sim_queue[i].send_tick = g_net.service_tick;
        g_net.sim_queue[i].len = sizeof(GgpoNetPacketPrefix);
    }
    g_net.socket_backpressured_this_tick = 0;
    g_net_test_replay_outgoing = 1;
    g_net_test_duplicate_pressure_calls = 0;
    ggpo_net_flush_sim_queue();
    ok = g_net_test_duplicate_pressure_calls == 2 &&
         !g_net.sim_queue[0].valid && g_net.sim_queue[1].valid &&
         g_net.socket_backpressured_this_tick &&
         g_net.socket_would_block_events == old_blocks + 1u &&
         g_net.socket_send_work_deferred == old_deferred + 1u;
    g_net_test_duplicate_pressure_calls = -1;
    g_net_test_replay_outgoing = old_replay;
    g_net.socket_backpressured_this_tick = old_pressure;
    g_net.socket_would_block_events = old_blocks;
    g_net.socket_send_work_deferred = old_deferred;
    memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
    return ok;
}
#endif

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
    int bypass_test_sim = 0;
    if (!data || len <= 0 || len > (int)GGPO_NET_MAX_PACKET_BYTES || !addr) return 0;
    memcpy(signed_packet, data, (size_t)len);
    if (!ggpo_net_sign_packet(signed_packet, len)) return 0;
#ifdef GGPO_NET_TEST
    if (g_net_test_tamper_outgoing && len > (int)sizeof(GgpoNetPacketPrefix)) {
        signed_packet[len - 1] ^= 0x01u;
    }
    /* A forced physical socket result must reach the same raw-send classifier
     * even when the paired chaos test has loss/delay enabled. */
    bypass_test_sim =
        ggpo_net_test_state_chunk_block_matches(signed_packet, len);
#endif

    if (!bypass_test_sim && simulate && g_net.sim_loss_percent > 0u) {
        if (ggpo_net_rand_range(100u) < g_net.sim_loss_percent) {
            g_net.sim_packets_dropped++;
            ggpo_net_note_chaos_event(
                GGPO_NET_CHAOS_DROP, signed_packet, len, 0u);
            return 1;
        }
    }

    if (!bypass_test_sim && simulate && g_net.sim_delay_max_ticks > 0u) {
        uint32_t min_delay = g_net.sim_delay_min_ticks;
        uint32_t max_delay = g_net.sim_delay_max_ticks;
        if (max_delay < min_delay) max_delay = min_delay;
        delay = min_delay + ggpo_net_rand_range((max_delay - min_delay) + 1u);
        if (delay > 0u) {
            return ggpo_net_queue_sim_packet(signed_packet, len, addr, delay);
        }
    }

    {
        GgpoNetRawSendResult sent =
            ggpo_net_send_raw_bytes(signed_packet, len, addr);
#ifdef GGPO_NET_TEST
        if (sent == GGPO_NET_RAW_SEND_SENT && g_net_test_replay_outgoing) {
            (void)ggpo_net_send_raw_bytes(signed_packet, len, addr);
        }
#endif
        return sent == GGPO_NET_RAW_SEND_SENT;
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
    g_net.remote_state_size = 0u;
    g_net.remote_state_layout_id = 0u;
    g_net.state_layout_conflict = 0;
    g_net.remote_prematch_hold = 0;
    g_net.remote_prematch_hold_known = 0;
    g_net.remote_hold_epoch = 0u;
    g_net.remote_state_epoch_seen = 0u;
    g_net.remote_palette_count = 0u;
    g_net.remote_palette_skin = 0u;
    g_net.remote_palette_clothing = 0u;
    g_net.last_palette_send_tick = 0u;
    g_net.remote_palette_valid = 0;
    g_net.local_palette_acked = 0;
    g_net.palette_conflict = 0;
    memset(g_net.history, 0, sizeof(g_net.history));
    memset(g_net.local_inputs, 0, sizeof(g_net.local_inputs));
    memset(g_net.remote_inputs, 0, sizeof(g_net.remote_inputs));
    ggpo_net_reset_checksum_channel(0u);
    g_net.frame = 0u;
    g_net.frame_counter_wrapped = 0;
    g_net.remote_frame = 0u;
    g_net.has_remote_frame = 0;
    g_net.remote_contiguous_input_frame = 0u;
    g_net.has_remote_contiguous_input_frame = 0;
    g_net.peer_acked_local_input_frame = 0u;
    g_net.has_peer_acked_local_input_frame = 0;
    g_net.has_peer_input_ack = 0;
    g_net.highest_local_input_frame = 0u;
    g_net.has_highest_local_input_frame = 0;
    g_net.rollback_to = 0u;
    g_net.rollback_pending = 0;
    g_net.last_remote_cmd = 0u;
    g_net.last_remote_cmd_frame = 0u;
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

/* RFC-1982-style serial ordering for the uint32_t frame space. Comparisons are
 * valid because every admitted receive frame is kept far inside the half-range
 * ambiguity boundary. Unsigned subtraction also remains correct at UINT32_MAX
 * -> 0 wrap. */
static int ggpo_net_frame_before(uint32_t lhs, uint32_t rhs) {
    uint32_t distance = rhs - lhs;
    return distance != 0u && distance < 0x80000000u;
}

static int ggpo_net_frame_after(uint32_t lhs, uint32_t rhs) {
    return ggpo_net_frame_before(rhs, lhs);
}

static int ggpo_net_frame_at_or_before(uint32_t frame, uint32_t horizon) {
    return frame == horizon || ggpo_net_frame_before(frame, horizon);
}

static int ggpo_net_ack_bit(const uint32_t bits[GGPO_NET_INPUT_ACK_WORDS],
                            uint32_t index) {
    if (!bits || index >= GGPO_NET_INPUT_ACK_BITS) return 0;
    return (bits[index / 32u] & (1u << (index % 32u))) ? 1 : 0;
}

static void ggpo_net_set_ack_bit(uint32_t bits[GGPO_NET_INPUT_ACK_WORDS],
                                 uint32_t index) {
    if (!bits || index >= GGPO_NET_INPUT_ACK_BITS) return;
    bits[index / 32u] |= 1u << (index % 32u);
}

static int ggpo_net_frame_receive_admissible(uint32_t frame) {
    if (frame == g_net.frame) return 1;
    if (ggpo_net_frame_before(frame, g_net.frame)) {
        return (g_net.frame - frame) <= GGPO_NET_INPUT_RETENTION_FRAMES;
    }
    if (ggpo_net_frame_after(frame, g_net.frame)) {
        return (frame - g_net.frame) <= GGPO_NET_INPUT_FUTURE_FRAMES;
    }
    return 0;
}

static int ggpo_net_history_frame_retained(uint32_t frame) {
    return ggpo_net_frame_before(frame, g_net.frame) &&
           (g_net.frame - frame) < GGPO_NET_HISTORY_FRAMES;
}

static int ggpo_net_ring_generation_may_replace(int valid,
                                                 uint32_t existing_frame,
                                                 uint32_t incoming_frame) {
    return !valid || existing_frame == incoming_frame ||
           ggpo_net_frame_after(incoming_frame, existing_frame);
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

static int ggpo_net_store_input(GgpoNetInputEntry* entries, uint32_t frame, uint32_t cmd) {
    GgpoNetInputEntry* e = ggpo_net_input_slot(entries, frame);
    if (!ggpo_net_ring_generation_may_replace(e->valid, e->frame, frame)) {
        return 0;
    }
    if (!e->valid || e->frame != frame) e->peer_acked = 0;
    e->valid = 1;
    e->frame = frame;
    e->cmd = cmd;
    return 1;
}

static int ggpo_net_local_input_acknowledged(const GgpoNetInputEntry* e) {
    if (!e || !e->valid) return 1;
    if (e->peer_acked) return 1;
    return g_net.has_peer_acked_local_input_frame &&
           ggpo_net_frame_at_or_before(e->frame,
                                       g_net.peer_acked_local_input_frame);
}

static int ggpo_net_local_input_retirable(const GgpoNetInputEntry* e) {
    if (!e || !e->valid) return 1;
    return g_net.has_peer_acked_local_input_frame &&
           ggpo_net_frame_at_or_before(e->frame,
                                       g_net.peer_acked_local_input_frame);
}

static int ggpo_net_get_input(GgpoNetInputEntry* entries, uint32_t frame, uint32_t* out_cmd) {
    GgpoNetInputEntry* e = ggpo_net_input_slot(entries, frame);
    if (!e->valid || e->frame != frame) return 0;
    if (out_cmd) *out_cmd = e->cmd;
    return 1;
}

static int ggpo_net_store_local_input(uint32_t frame,
                                      uint32_t cmd,
                                      char* err,
                                      size_t err_cap) {
    GgpoNetInputEntry* slot = ggpo_net_input_slot(g_net.local_inputs, frame);
    if (slot->valid && slot->frame == frame) {
        if (slot->cmd == cmd) return 1;
        LOG_ERROR("ggpo.net: refused to change committed local input frame=%u first=0x%08X later=0x%08X",
                  (unsigned int)frame,
                  (unsigned int)slot->cmd,
                  (unsigned int)cmd);
        ggpo_net_set_err(err, err_cap, "local input was already committed for this frame");
        return 0;
    }
    if (slot->valid && slot->frame != frame &&
        ggpo_net_frame_after(frame, slot->frame) &&
        !ggpo_net_local_input_retirable(slot)) {
        LOG_ERROR("ggpo.net: refusing to retire unacknowledged local input frame=%u for generation=%u",
                  (unsigned int)slot->frame,
                  (unsigned int)frame);
        ggpo_net_set_err(err, err_cap,
                         "unacknowledged local input reached the history boundary");
        return 0;
    }
    if (ggpo_net_store_input(g_net.local_inputs, frame, cmd)) {
        if (!g_net.has_highest_local_input_frame ||
            ggpo_net_frame_after(frame, g_net.highest_local_input_frame)) {
            g_net.highest_local_input_frame = frame;
            g_net.has_highest_local_input_frame = 1;
        }
        return 1;
    }
    LOG_ERROR("ggpo.net: refused stale local input generation frame=%u local_frame=%u",
              (unsigned int)frame,
              (unsigned int)g_net.frame);
    ggpo_net_set_err(err, err_cap, "stale local input would overwrite a newer ring generation");
    return 0;
}

static int ggpo_net_queue_local_input(uint32_t frame,
                                      uint32_t raw_cmd,
                                      uint32_t* out_cmd,
                                      char* err,
                                      size_t err_cap) {
    uint32_t cmd = 0;
    if (ggpo_net_get_input(g_net.local_inputs, frame, &cmd)) {
        if (out_cmd) *out_cmd = cmd;
        return 1;
    }
    if (!ggpo_net_store_local_input(frame, raw_cmd, err, err_cap)) return 0;
    if (out_cmd) *out_cmd = raw_cmd;
    return 1;
}

static int ggpo_net_seed_local_input_delay_from(uint32_t start_frame,
                                                char* err,
                                                size_t err_cap) {
    for (uint32_t i = 0; i < g_net.input_delay; i++) {
        uint32_t frame = start_frame + i;
        uint32_t tmp = 0;
        if (!ggpo_net_get_input(g_net.local_inputs, frame, &tmp)) {
            if (!ggpo_net_store_local_input(frame, 0u, err, err_cap)) return 0;
        }
    }
    return 1;
}

static int ggpo_net_seed_local_input_delay(char* err, size_t err_cap) {
    return ggpo_net_seed_local_input_delay_from(0u, err, err_cap);
}

static void ggpo_net_advance_remote_contiguous_input(void) {
    uint32_t next = g_net.has_remote_contiguous_input_frame
        ? g_net.remote_contiguous_input_frame + 1u
        : 0u;
    uint32_t ignored = 0u;
    uint32_t advanced = 0u;

    /* The exact-input ring contains one admitted generation. The bound makes a
     * corrupt ring incapable of turning uint32 wrap into an infinite loop. */
    while (advanced < GGPO_NET_HISTORY_FRAMES &&
           ggpo_net_get_input(g_net.remote_inputs, next, &ignored)) {
        g_net.remote_contiguous_input_frame = next;
        g_net.has_remote_contiguous_input_frame = 1;
        next++;
        advanced++;
    }
}

static void ggpo_net_build_input_ack(uint32_t* out_flags,
                                     uint32_t* out_base,
                                     uint32_t out_bits[GGPO_NET_INPUT_ACK_WORDS]) {
    uint32_t start = g_net.has_remote_contiguous_input_frame
        ? g_net.remote_contiguous_input_frame + 1u
        : 0u;
    uint32_t ignored = 0u;

    if (out_flags) {
        *out_flags = g_net.has_remote_contiguous_input_frame
            ? GGPO_NET_INPUT_ACK_VALID
            : 0u;
    }
    if (out_base) {
        *out_base = g_net.has_remote_contiguous_input_frame
            ? g_net.remote_contiguous_input_frame
            : 0u;
    }
    if (!out_bits) return;
    memset(out_bits, 0, sizeof(uint32_t) * GGPO_NET_INPUT_ACK_WORDS);
    for (uint32_t i = 0u; i < GGPO_NET_INPUT_ACK_BITS; i++) {
        if (ggpo_net_get_input(g_net.remote_inputs, start + i, &ignored)) {
            ggpo_net_set_ack_bit(out_bits, i);
        }
    }
}

static int ggpo_net_validate_peer_input_ack(
    uint32_t flags,
    uint32_t base,
    const uint32_t bits[GGPO_NET_INPUT_ACK_WORDS]) {
    int ack_valid;
    uint32_t start;

    if (!bits || (flags & ~GGPO_NET_INPUT_ACK_KNOWN_FLAGS) != 0u) return 0;
    ack_valid = (flags & GGPO_NET_INPUT_ACK_VALID) ? 1 : 0;
    if (!ack_valid && (base != 0u || ggpo_net_ack_bit(bits, 0u))) return 0;
    if (ack_valid) {
        if (!g_net.has_highest_local_input_frame ||
            ggpo_net_frame_after(base, g_net.highest_local_input_frame)) {
            return 0;
        }
        if ((!g_net.has_peer_acked_local_input_frame ||
             ggpo_net_frame_after(base, g_net.peer_acked_local_input_frame)) &&
            !ggpo_net_get_input(g_net.local_inputs, base, NULL)) {
            return 0;
        }
        if (!g_net.has_peer_acked_local_input_frame ||
            ggpo_net_frame_after(base, g_net.peer_acked_local_input_frame)) {
            uint32_t first = g_net.has_peer_acked_local_input_frame
                ? g_net.peer_acked_local_input_frame + 1u
                : 0u;
            uint32_t claim_count = base - first + 1u;
            if (claim_count == 0u || claim_count > GGPO_NET_HISTORY_FRAMES) {
                return 0;
            }
            for (uint32_t i = 0u; i < claim_count; i++) {
                if (!ggpo_net_get_input(g_net.local_inputs, first + i, NULL)) {
                    return 0;
                }
            }
        }
    }

    start = ack_valid ? base + 1u : 0u;
    for (uint32_t i = 0u; i < GGPO_NET_INPUT_ACK_BITS; i++) {
        uint32_t frame;
        GgpoNetInputEntry* entry;
        if (!ggpo_net_ack_bit(bits, i)) continue;
        frame = start + i;
        entry = ggpo_net_input_slot(g_net.local_inputs, frame);
        if (entry->valid && entry->frame == frame) {
            continue;
        }
        /* A reordered ACK may name data already covered by a newer cumulative
         * base and retired. Anything else claims receipt of unsent data. */
        if (!g_net.has_peer_acked_local_input_frame ||
            !ggpo_net_frame_at_or_before(frame,
                                         g_net.peer_acked_local_input_frame)) {
            return 0;
        }
    }
    return 1;
}

static int ggpo_net_note_peer_input_ack(uint32_t flags,
                                        uint32_t base,
                                        const uint32_t bits[GGPO_NET_INPUT_ACK_WORDS]) {
    int ack_valid = (flags & GGPO_NET_INPUT_ACK_VALID) ? 1 : 0;
    uint32_t start = ack_valid ? base + 1u : 0u;

    if (!ggpo_net_validate_peer_input_ack(flags, base, bits)) return 0;

    for (uint32_t i = 0u; i < GGPO_NET_INPUT_ACK_BITS; i++) {
        uint32_t frame;
        GgpoNetInputEntry* entry;
        if (!ggpo_net_ack_bit(bits, i)) continue;
        frame = start + i;
        entry = ggpo_net_input_slot(g_net.local_inputs, frame);
        if (entry->valid && entry->frame == frame) entry->peer_acked = 1;
    }

    g_net.has_peer_input_ack = 1;
    if (ack_valid &&
        (!g_net.has_peer_acked_local_input_frame ||
         ggpo_net_frame_after(base, g_net.peer_acked_local_input_frame))) {
        g_net.peer_acked_local_input_frame = base;
        g_net.has_peer_acked_local_input_frame = 1;
    }

    /* Merge SACKs monotonically by their absolute local ring entries. If the
     * first frame after the cumulative base is now known received, fold it in. */
    while (g_net.has_peer_acked_local_input_frame) {
        uint32_t next = g_net.peer_acked_local_input_frame + 1u;
        GgpoNetInputEntry* entry = ggpo_net_input_slot(g_net.local_inputs, next);
        if (!entry->valid || entry->frame != next || !entry->peer_acked) break;
        g_net.peer_acked_local_input_frame = next;
    }
    return 1;
}

static int ggpo_net_mutual_input_horizon(uint32_t* out_frame) {
    uint32_t frame;
    if (!g_net.has_remote_contiguous_input_frame ||
        !g_net.has_peer_acked_local_input_frame) {
        return 0;
    }
    frame = g_net.remote_contiguous_input_frame;
    if (ggpo_net_frame_before(g_net.peer_acked_local_input_frame, frame)) {
        frame = g_net.peer_acked_local_input_frame;
    }
    if (out_frame) *out_frame = frame;
    return 1;
}

static int ggpo_net_checksum_horizon(uint32_t* out_frame) {
    uint32_t horizon;
    uint32_t last_simulated;

    if (g_net.rollback_pending || g_net.correction_active ||
        g_net.awaiting_correction ||
        !ggpo_net_mutual_input_horizon(&horizon)) return 0;
    if (g_net.frame == 0u && !g_net.frame_counter_wrapped) return 0;
    last_simulated = g_net.frame - 1u;
    if (ggpo_net_frame_before(last_simulated, horizon)) horizon = last_simulated;

    for (uint32_t age = 0u; age < GGPO_NET_HISTORY_FRAMES; age++) {
        uint32_t frame = horizon - age;
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (h->valid && h->frame == frame && !h->remote_predicted) {
            if (out_frame) *out_frame = frame;
            return 1;
        }
    }
    return 0;
}

static int ggpo_net_checksum_epoch_matches(const GgpoNetPacket* p) {
    return p &&
           p->state_epoch == g_net.state_epoch &&
           p->correction_id == g_net.correction_id;
}

static void ggpo_net_note_checksum_progress(void) {
    g_net.checksum_wait_start_tick = 0u;
    g_net.checksum_wait_frame = 0u;
    g_net.checksum_wait_announced = 0;
}

/* checksum_ack_next is a cumulative proof for every frame beginning at the
 * current stream start. Validate the complete newly claimed range before any
 * packet field mutates session state. */
static int ggpo_net_validate_peer_checksum_ack(uint32_t ack_next) {
    uint32_t current = g_net.peer_checksum_ack_next;
    uint32_t claim_count;

    if (ack_next == current || ggpo_net_frame_before(ack_next, current)) return 1;
    if (!ggpo_net_frame_after(ack_next, current)) return 0;
    claim_count = ack_next - current;
    if (claim_count == 0u || claim_count > GGPO_NET_HISTORY_FRAMES) return 0;
    for (uint32_t i = 0u; i < claim_count; i++) {
        uint32_t frame = current + i;
        GgpoNetHistoryEntry* h =
            &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (!h->valid || h->frame != frame || h->remote_predicted ||
            !h->checksum_published) {
            return 0;
        }
    }
    return 1;
}

static int ggpo_net_note_peer_checksum_ack(uint32_t ack_next) {
    if (!ggpo_net_validate_peer_checksum_ack(ack_next)) return 0;
    if (ggpo_net_frame_after(ack_next, g_net.peer_checksum_ack_next)) {
        g_net.peer_checksum_ack_next = ack_next;
        ggpo_net_note_checksum_progress();
    }
    return 1;
}

static int ggpo_net_history_checksum_retirable(
    const GgpoNetHistoryEntry* h) {
    if (!h || !h->valid) return 1;
    return ggpo_net_frame_before(h->frame, g_net.peer_checksum_ack_next) &&
           ggpo_net_frame_before(h->frame, g_net.remote_checksum_ack_next);
}

static void ggpo_net_advance_remote_checksum_ack(void) {
    uint32_t advanced = 0u;
    while (advanced < GGPO_NET_HISTORY_FRAMES) {
        uint32_t frame = g_net.remote_checksum_ack_next;
        GgpoNetRemoteChecksumEntry* entry =
            &g_net.remote_checksums[frame % GGPO_NET_HISTORY_FRAMES];
        if (!entry->valid || entry->frame != frame || !entry->verified) break;
        g_net.remote_checksum_ack_next = frame + 1u;
        advanced++;
    }
    if (advanced != 0u) ggpo_net_note_checksum_progress();
}

static uint32_t ggpo_net_now_tick(void) {
    /* Zero is the inactive-timer sentinel. At clock wrap choose the preceding
     * tick, not a future tick: 0 - 1 would look like UINT32_MAX elapsed. */
    return g_net.service_tick ? g_net.service_tick : UINT32_MAX;
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
    if (g_net.correction_phase == GGPO_NET_CORRECTION_NONE) {
        g_net.correction_phase = GGPO_NET_CORRECTION_REQUEST;
        g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
        g_net.correction_frame = request_frame;
        g_net.correction_resume_frame = request_frame;
        g_net.correction_input_count = 0u;
        g_net.correction_snapshot_ready = 0;
        g_net.correction_local_applied = 0;
        g_net.correction_apply_attempts = 0u;
    }
    if (g_net.correction_wait_start_tick == 0u) {
        g_net.correction_wait_start_tick = ggpo_net_now_tick();
        g_net.correction_progress_tick = g_net.correction_wait_start_tick;
        g_net.correction_wait_cap_announced = 0;
    }
}

static void ggpo_net_begin_host_correction_request(uint32_t request_frame) {
    g_net.correction_active = 1;
    g_net.awaiting_correction = 0;
    g_net.correction_phase = GGPO_NET_CORRECTION_REQUEST;
    g_net.correction_peer_phase = GGPO_NET_CORRECTION_REQUEST;
    g_net.correction_request_frame = request_frame;
    g_net.correction_frame = request_frame;
    g_net.correction_resume_frame = request_frame;
    g_net.correction_input_count = 0u;
    g_net.correction_snapshot_ready = 0;
    g_net.correction_local_applied = 0;
    g_net.correction_apply_attempts = 0u;
    g_net.correction_transcript = 0u;
    g_net.correction_expected_transcript = 0u;
    g_net.correction_wait_start_tick = ggpo_net_now_tick();
    g_net.correction_progress_tick = g_net.correction_wait_start_tick;
    g_net.correction_wait_cap_announced = 0;
}

static int ggpo_net_request_host_correction(const char* reason, int force) {
    if (!g_net.correction_enabled || g_net.mode != GGPO_NET_MODE_JOIN) return 0;
    if (!force &&
        g_net.last_resync_request_tick != 0u &&
        g_net.service_tick - g_net.last_resync_request_tick < GGPO_NET_RESYNC_REQUEST_INTERVAL_TICKS) {
        return 0;
    }
    if (!ggpo_net_send_packet(GGPO_NET_PACKET_RESYNC_REQUEST)) {
        return 0;
    }
    g_net.last_resync_request_tick = ggpo_net_now_tick();
    ggpo_net_saturating_increment(&g_net.correction_requests);
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

static void ggpo_net_note_correction_progress(void) {
    g_net.correction_progress_tick = ggpo_net_now_tick();
}

/* A correction packet can make the barrier immediately actionable on the same
 * service tick that reaches its deadline.  Transport polling happens before
 * gameplay-side replay, so consume that valid transition instead of timing out
 * merely because the scheduler delivered it on the boundary. */
static int ggpo_net_correction_actionable(void) {
    if (g_net.mode == GGPO_NET_MODE_HOST) {
        return g_net.correction_phase == GGPO_NET_CORRECTION_REQUEST ||
               (g_net.correction_phase == GGPO_NET_CORRECTION_OFFER &&
                g_net.correction_peer_phase == GGPO_NET_CORRECTION_READY);
    }
    if (g_net.mode == GGPO_NET_MODE_JOIN) {
        return (g_net.correction_phase == GGPO_NET_CORRECTION_RECEIVING &&
                g_net.correction_snapshot_ready) ||
               (g_net.correction_phase == GGPO_NET_CORRECTION_READY &&
                g_net.correction_peer_phase == GGPO_NET_CORRECTION_COMMIT);
    }
    return 0;
}

static int ggpo_net_correction_span(uint32_t first,
                                    uint32_t resume,
                                    uint32_t* out_span) {
    uint32_t span = resume - first;
    if (span == 0u || span > GGPO_NET_HISTORY_FRAMES) return 0;
    if (out_span) *out_span = span;
    return 1;
}

/* Resolve an exact role-stable input pair.  The immutable input rings are the
 * primary source; retained finalized history is a fallback for an ACKed input
 * whose modulo slot was legitimately reused before correction negotiation. */
static int ggpo_net_correction_inputs_for_frame(uint32_t frame,
                                                 uint32_t out_cmd[2]) {
    uint32_t local_cmd = 0u;
    uint32_t remote_cmd = 0u;
    int have_local = ggpo_net_get_input(g_net.local_inputs, frame, &local_cmd);
    int have_remote = ggpo_net_get_input(g_net.remote_inputs, frame, &remote_cmd);
    GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];

    if ((!have_local || !have_remote) &&
        h->valid && h->frame == frame && !h->remote_predicted) {
        /* Correction deliberately rebuilds history from the immutable input
         * rings. A retained entry may still describe the pre-rollback
         * simulation that triggered correction, so use it only to recover an
         * input whose acknowledged ring generation was legitimately retired;
         * never let stale history veto exact commands still present in both
         * rings. The replay transcript proves both peers selected the same
         * role-stable pair. */
        if (!have_local) {
            local_cmd = h->local_cmd;
            have_local = 1;
        }
        if (!have_remote) {
            remote_cmd = h->remote_cmd;
            have_remote = 1;
        }
    }
    if (!have_local || !have_remote) return 0;
    out_cmd[g_net.local_player] = local_cmd;
    out_cmd[g_net.remote_player] = remote_cmd;
    return 1;
}

static int ggpo_net_pin_correction_inputs(uint32_t first,
                                           uint32_t resume) {
    uint32_t span = 0u;
    if (!ggpo_net_correction_span(first, resume, &span)) {
        return 0;
    }
    /* The host bounds `resume` by its mutually acknowledged horizon before it
     * offers the tuple.  The receiver need only prove that it owns the same
     * exact role-local commands. Requiring its independently delayed view of
     * the peer's cumulative ACK here can deadlock an otherwise complete
     * correction after the snapshot and every replay input have arrived. */
    for (uint32_t i = 0u; i < span; i++) {
        uint32_t cmd[2] = {0u, 0u};
        if (!ggpo_net_correction_inputs_for_frame(first + i, cmd)) {
            g_net.correction_input_count = 0u;
            return 0;
        }
        g_net.correction_inputs[i][0] = cmd[0];
        g_net.correction_inputs[i][1] = cmd[1];
    }
    g_net.correction_input_count = span;
    return 1;
}

static int ggpo_net_prepare_host_correction(const char* reason) {
    uint32_t divergence;
    uint32_t horizon;
    uint32_t last_simulated;
    uint32_t resume;
    uint32_t validated_checksum = 0u;
    GgpoNetHistoryEntry* snapshot_history;
    GgpoNetRemoteChecksumEntry* remote_checksum;
    uint8_t* snapshot_blob = NULL;
    char err[256];
    if (!g_net.correction_enabled || g_net.mode != GGPO_NET_MODE_HOST) return 0;
    if (!g_net.correction_state || g_net.state_size == 0) return 0;
    if (g_net.correction_active &&
        g_net.correction_phase != GGPO_NET_CORRECTION_REQUEST) return 1;
    if (g_net.rollback_pending) return 0;
    if (g_net.frame == 0u && !g_net.frame_counter_wrapped) return 0;

    divergence = g_net.correction_request_frame;
    snapshot_history = ggpo_net_history_slot(divergence, &snapshot_blob);
    if (!snapshot_history || !snapshot_history->valid ||
        snapshot_history->frame != divergence ||
        snapshot_history->state_len == 0u ||
        snapshot_history->state_len > g_net.state_size ||
        !snapshot_blob || snapshot_history->remote_predicted) {
        LOG_ERROR("ggpo.net: correction boundary is not retained/finalized frame=%u local_frame=%u",
                  (unsigned int)divergence,
                  (unsigned int)g_net.frame);
        g_net.peer_disconnected = 1;
        return 0;
    }
    remote_checksum =
        &g_net.remote_checksums[divergence % GGPO_NET_HISTORY_FRAMES];
    /* The requester has already selected this divergent boundary, but only it
     * necessarily possesses both checksums and passes through
     * recoverable_desync(). Preserve the host's opposite canonical boundary
     * before correction replaces history. Its copy of the requester's checksum
     * is optional because the correction request can outrun checksum delivery. */
    ggpo_net_capture_first_desync_repro(
        divergence,
        snapshot_history->post_checksum,
        (remote_checksum->valid && remote_checksum->frame == divergence)
            ? remote_checksum->checksum
            : 0u,
        remote_checksum->valid && remote_checksum->frame == divergence);
    if (!ggpo_net_mutual_input_horizon(&horizon)) return 0;
    last_simulated = g_net.frame - 1u;
    if (ggpo_net_frame_before(last_simulated, horizon)) horizon = last_simulated;
    if (!ggpo_net_frame_at_or_before(divergence, horizon) ||
        (horizon - divergence) >= GGPO_NET_HISTORY_FRAMES) {
        return 0;
    }

    /* Prefer the newest exact common horizon, but never fabricate a command.
     * If the tail is still provisional, shorten it to the last fully pinnable
     * frame while retaining the confirmed divergence boundary itself. */
    for (;;) {
        resume = horizon + 1u;
        if (ggpo_net_pin_correction_inputs(divergence, resume)) break;
        if (horizon == divergence) return 0;
        horizon--;
    }

    err[0] = '\0';
    if (!ggpo_ext_validate_rollback_transport_blob(snapshot_blob,
                                                    snapshot_history->state_len,
                                                    &validated_checksum,
                                                    err,
                                                    sizeof(err)) ||
        validated_checksum != snapshot_history->pre_checksum) {
        LOG_ERROR("ggpo.net: retained correction boundary failed validation frame=%u (%s)",
                  (unsigned int)divergence,
                  err[0] ? err : "checksum mismatch");
        g_net.peer_disconnected = 1;
        return 0;
    }
    memcpy(g_net.correction_state, snapshot_blob, snapshot_history->state_len);
    g_net.correction_state_len = snapshot_history->state_len;
    g_net.correction_checksum = validated_checksum;
    g_net.correction_id = ggpo_net_next_correction_id();
    if (g_net.correction_id == 0u) {
        LOG_ERROR("ggpo.net: correction generation exhausted for state epoch=%u",
                  (unsigned int)g_net.state_epoch);
        g_net.peer_disconnected = 1;
        return 0;
    }
    g_net.correction_frame = divergence;
    g_net.correction_resume_frame = resume;
    g_net.correction_send_offset = 0;
    g_net.correction_send_next_chunk = 0;
    g_net.correction_send_chunk_count =
        ggpo_net_state_chunk_count((uint32_t)g_net.correction_state_len);
    g_net.correction_send_base_checksum = 0;
    /* Correctness first: a full staged snapshot has one unambiguous base.  The
     * old opportunistic delta path can be reintroduced later with an explicit
     * NEED_FULL phase, but may not restart a correction inside this barrier. */
    g_net.correction_send_delta = 0;
    g_net.correction_active = 1;
    g_net.awaiting_correction = 0;
    g_net.correction_phase = GGPO_NET_CORRECTION_OFFER;
    g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_transcript = 0u;
    g_net.correction_expected_transcript = 0u;
    g_net.correction_apply_attempts = 0u;
    g_net.correction_snapshot_ready = 1;
    g_net.correction_local_applied = 0;
    g_net.correction_wait_start_tick = ggpo_net_now_tick();
    ggpo_net_note_correction_progress();
    g_net.corrections_sent++;
    LOG_WARN("ggpo.net: host correction offered id=%u snapshot=%u resume=%u checksum=%u inputs=%u chunks=%u reason=%s",
             (unsigned int)g_net.correction_id,
             (unsigned int)g_net.correction_frame,
             (unsigned int)g_net.correction_resume_frame,
             (unsigned int)g_net.correction_checksum,
             (unsigned int)g_net.correction_input_count,
             (unsigned int)g_net.correction_send_chunk_count,
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
    log_dump_line("ggpo.desync detail f=%u p=%d x87=0x%04X mxcsr=0x%08X changed=%s pdiff=%s slots=%s "
                  "L[hdr=%u tr=%u tinfo=%u rinfo=%u part=%u pl=%u p0=%u p1=%u th=%u tile=%u room=%u ticks=%u rng=%u] "
                  "R[hdr=%u tr=%u tinfo=%u rinfo=%u part=%u pl=%u p0=%u p1=%u th=%u tile=%u room=%u ticks=%u rng=%u]",
                  (unsigned int)frame, g_net.local_player,
                  (unsigned int)fp_control_get_x87_control(),
                  (unsigned int)fp_control_get_mxcsr(),
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

typedef struct GgpoNetDesyncReproTask {
    char directory[MAX_PATH];
    uint32_t pair_tag;
    uint32_t player;
    uint32_t desync_frame;
    uint32_t state_boundary_frame;
    uint32_t state_len;
    uint32_t local_checksum;
    uint32_t remote_checksum;
    int remote_checksum_known;
    uint32_t local_cmd;
    uint32_t remote_cmd;
    uint32_t remote_predicted;
    uint32_t state_epoch;
    uint32_t local_build_id;
    uint32_t local_exe_id;
    uint32_t local_dll_id;
    uint32_t remote_build_id;
    uint32_t remote_exe_id;
    uint32_t remote_dll_id;
    int remote_fingerprint_known;
    uint32_t state_layout_id;
    uint32_t remote_state_layout_id;
    uint32_t state_size;
    uint32_t deterministic_config_id;
    uint32_t x87_control;
    uint32_t mxcsr;
    uint32_t input_delay;
    uint32_t max_frame_advantage;
    uint32_t max_prediction;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t predictions;
    uint32_t rollbacks;
    uint32_t frame_advantage_stalls;
    uint32_t prediction_stalls;
    uint32_t checksum_stalls;
    uint32_t corrections_sent;
    uint32_t corrections_received;
    uint32_t correction_requests;
    uint32_t sim_loss_percent;
    uint32_t sim_delay_min_ticks;
    uint32_t sim_delay_max_ticks;
    uint32_t sim_dropped_packets;
    uint32_t sim_delayed_packets;
    uint32_t sim_seed;
    uint32_t chaos_event_total;
    uint32_t chaos_event_count;
    GgpoNetChaosEvent chaos_events[GGPO_NET_CHAOS_EVENT_CAP];
    uint8_t state[1];
} GgpoNetDesyncReproTask;

static uint32_t ggpo_net_desync_pair_tag(void) {
    uint64_t ids[2];
    uint64_t hash = 1469598103934665603ull;
    size_t i;
    ids[0] = g_net.session_id;
    ids[1] = g_net.remote_session_id;
    if (ids[1] < ids[0]) {
        uint64_t swap = ids[0];
        ids[0] = ids[1];
        ids[1] = swap;
    }
    for (i = 0u; i < sizeof(ids); i++) {
        hash ^= ((const uint8_t*)ids)[i];
        hash *= 1099511628211ull;
    }
    hash ^= (uint64_t)g_net.state_epoch;
    hash *= 1099511628211ull;
    return (uint32_t)(hash ^ (hash >> 32));
}

static uint32_t ggpo_net_desync_config_id(void) {
    uint32_t hash = 2166136261u;
    hash = ggpo_net_hash_mix_u32(hash, GGPO_NET_VERSION);
    hash = ggpo_net_hash_mix_u32(hash, GGPO_NET_HISTORY_FRAMES);
    hash = ggpo_net_hash_mix_u32(hash, g_net.state_layout_id);
    hash = ggpo_net_hash_mix_u32(hash, (uint32_t)g_net.state_size);
    hash = ggpo_net_hash_mix_u32(hash, g_net.input_delay);
    hash = ggpo_net_hash_mix_u32(hash, g_net.max_frame_advantage);
    hash = ggpo_net_hash_mix_u32(hash, g_net.max_prediction);
    hash = ggpo_net_hash_mix_u32(
        hash, g_net.correction_enabled ? 1u : 0u);
    return hash;
}

static DWORD WINAPI ggpo_net_desync_repro_worker(LPVOID opaque) {
    GgpoNetDesyncReproTask* task = (GgpoNetDesyncReproTask*)opaque;
    char trace_path[MAX_PATH];
    char trace_tmp[MAX_PATH];
    char meta_path[MAX_PATH];
    char meta_tmp[MAX_PATH];
    FILE* file = NULL;
    uint32_t header[3];
    DWORD pid = GetCurrentProcessId();
    int trace_ok = 0;
    int meta_ok = 0;
    if (!task) return 1u;
    CreateDirectoryA("mods", NULL);
    CreateDirectoryA(task->directory, NULL);
    {
        int trace_path_len = snprintf(trace_path, sizeof(trace_path),
                 "%s\\trace_%08X_f%08X_p%u.bin",
                 task->directory,
                 (unsigned int)task->pair_tag,
                 (unsigned int)task->desync_frame,
                 (unsigned int)task->player);
        int trace_tmp_len = snprintf(trace_tmp, sizeof(trace_tmp),
                 "%s.tmp-%lu", trace_path, (unsigned long)pid);
        int meta_path_len = snprintf(meta_path, sizeof(meta_path),
                 "%s\\meta_%08X_f%08X_p%u.txt",
                 task->directory,
                 (unsigned int)task->pair_tag,
                 (unsigned int)task->desync_frame,
                 (unsigned int)task->player);
        int meta_tmp_len = snprintf(meta_tmp, sizeof(meta_tmp),
                 "%s.tmp-%lu", meta_path, (unsigned long)pid);
        if (trace_path_len >= 0 &&
            trace_path_len < (int)sizeof(trace_path) &&
            trace_tmp_len >= 0 &&
            trace_tmp_len < (int)sizeof(trace_tmp) &&
            meta_path_len >= 0 &&
            meta_path_len < (int)sizeof(meta_path) &&
            meta_tmp_len >= 0 &&
            meta_tmp_len < (int)sizeof(meta_tmp)) {
        header[0] = task->desync_frame;
        header[1] = task->state_len;
        header[2] = task->local_checksum;
        file = fopen(trace_tmp, "wb");
        if (file) {
            trace_ok =
                fwrite(header, sizeof(header), 1u, file) == 1u &&
                fwrite(task->state, task->state_len, 1u, file) == 1u &&
                fflush(file) == 0;
            if (fclose(file) != 0) trace_ok = 0;
            file = NULL;
        }
        if (trace_ok) {
            trace_ok = MoveFileExA(trace_tmp, trace_path,
                                   MOVEFILE_REPLACE_EXISTING |
                                   MOVEFILE_WRITE_THROUGH) != 0;
        }
        if (!trace_ok) DeleteFileA(trace_tmp);

        file = fopen(meta_tmp, "wb");
        if (file) {
            meta_ok = fprintf(file,
                    "format=eggnoggplus-desync-repro-v1\n"
                    "pair_tag=%08X\n"
                    "player=%u\n"
                    "desync_frame=%u\n"
                    "state_boundary_frame=%u\n"
                    "local_checksum=%u\n"
                    "remote_checksum=%u\n"
                    "remote_checksum_known=%d\n"
                    "local_cmd=%08X\n"
                    "remote_cmd=%08X\n"
                    "remote_predicted=%u\n"
                    "state_epoch=%u\n"
                    "local_build_id=%08X\n"
                    "local_exe_id=%08X\n"
                    "local_dll_id=%08X\n"
                    "remote_build_id=%08X\n"
                    "remote_exe_id=%08X\n"
                    "remote_dll_id=%08X\n"
                    "remote_fingerprint_known=%d\n"
                    "state_layout_id=%08X\n"
                    "remote_state_layout_id=%08X\n"
                    "state_size=%u\n"
                    "deterministic_config_id=%08X\n"
                    "x87_control=%04X\n"
                    "mxcsr=%08X\n"
                    "input_delay=%u\n"
                    "max_frame_advantage=%u\n"
                    "max_prediction=%u\n"
                    "packets_sent=%u\n"
                    "packets_received=%u\n"
                    "predictions=%u\n"
                    "rollbacks=%u\n"
                    "frame_advantage_stalls=%u\n"
                    "prediction_stalls=%u\n"
                    "checksum_stalls=%u\n"
                    "corrections_sent=%u\n"
                    "corrections_received=%u\n"
                    "correction_requests=%u\n"
                    "sim_loss_percent=%u\n"
                    "sim_delay_min_ticks=%u\n"
                    "sim_delay_max_ticks=%u\n"
                    "sim_dropped_packets=%u\n"
                    "sim_delayed_packets=%u\n"
                    "sim_seed=%08X\n"
                    "chaos_event_total=%u\n"
                    "chaos_event_count=%u\n",
                    (unsigned int)task->pair_tag,
                    (unsigned int)task->player,
                    (unsigned int)task->desync_frame,
                    (unsigned int)task->state_boundary_frame,
                    (unsigned int)task->local_checksum,
                    (unsigned int)task->remote_checksum,
                    task->remote_checksum_known,
                    (unsigned int)task->local_cmd,
                    (unsigned int)task->remote_cmd,
                    (unsigned int)task->remote_predicted,
                    (unsigned int)task->state_epoch,
                    (unsigned int)task->local_build_id,
                    (unsigned int)task->local_exe_id,
                    (unsigned int)task->local_dll_id,
                    (unsigned int)task->remote_build_id,
                    (unsigned int)task->remote_exe_id,
                    (unsigned int)task->remote_dll_id,
                    task->remote_fingerprint_known,
                    (unsigned int)task->state_layout_id,
                    (unsigned int)task->remote_state_layout_id,
                    (unsigned int)task->state_size,
                    (unsigned int)task->deterministic_config_id,
                    (unsigned int)task->x87_control,
                    (unsigned int)task->mxcsr,
                    (unsigned int)task->input_delay,
                    (unsigned int)task->max_frame_advantage,
                    (unsigned int)task->max_prediction,
                    (unsigned int)task->packets_sent,
                    (unsigned int)task->packets_received,
                    (unsigned int)task->predictions,
                    (unsigned int)task->rollbacks,
                    (unsigned int)task->frame_advantage_stalls,
                    (unsigned int)task->prediction_stalls,
                    (unsigned int)task->checksum_stalls,
                    (unsigned int)task->corrections_sent,
                    (unsigned int)task->corrections_received,
                    (unsigned int)task->correction_requests,
                    (unsigned int)task->sim_loss_percent,
                    (unsigned int)task->sim_delay_min_ticks,
                    (unsigned int)task->sim_delay_max_ticks,
                    (unsigned int)task->sim_dropped_packets,
                    (unsigned int)task->sim_delayed_packets,
                    (unsigned int)task->sim_seed,
                    (unsigned int)task->chaos_event_total,
                    (unsigned int)task->chaos_event_count) > 0;
            for (uint32_t i = 0u; meta_ok &&
                 i < task->chaos_event_count; i++) {
                const GgpoNetChaosEvent* event =
                    &task->chaos_events[i];
                meta_ok = fprintf(
                    file,
                    "chaos_event_%04u=%u,%u,%u,%u,%u\n",
                    (unsigned int)i,
                    (unsigned int)event->sequence,
                    (unsigned int)event->service_tick,
                    (unsigned int)event->kind,
                    (unsigned int)event->packet_type,
                    (unsigned int)event->value) > 0;
            }
            if (meta_ok && fflush(file) != 0) meta_ok = 0;
            if (fclose(file) != 0) meta_ok = 0;
            file = NULL;
        }
        if (meta_ok) {
            meta_ok = MoveFileExA(meta_tmp, meta_path,
                                  MOVEFILE_REPLACE_EXISTING |
                                  MOVEFILE_WRITE_THROUGH) != 0;
        }
        if (!meta_ok) DeleteFileA(meta_tmp);
        }
    }
    SecureZeroMemory(task->state, task->state_len);
    free(task);
    return (trace_ok && meta_ok) ? 0u : 1u;
}

static void ggpo_net_wait_desync_repro_worker(DWORD timeout_ms) {
    HANDLE worker = g_desync_repro_worker;
    DWORD wait_result;
    DWORD exit_code = STILL_ACTIVE;
    if (!worker) return;
    wait_result = WaitForSingleObject(worker, timeout_ms);
    if (wait_result == WAIT_TIMEOUT && timeout_ms == 0u) {
        return;
    }
    if (wait_result == WAIT_OBJECT_0 &&
        GetExitCodeThread(worker, &exit_code)) {
        if (exit_code == 0u) {
            LOG_INFO("ggpo.net: first desync snapshot files completed");
        } else {
            LOG_WARN("ggpo.net: first desync snapshot file write failed");
        }
    } else if (wait_result == WAIT_TIMEOUT) {
        LOG_WARN("ggpo.net: first desync snapshot writer exceeded teardown deadline");
    } else {
        LOG_WARN("ggpo.net: could not observe first desync snapshot writer");
    }
    CloseHandle(worker);
    g_desync_repro_worker = NULL;
}

static void ggpo_net_capture_first_desync_repro(uint32_t frame,
                                                uint32_t local_checksum,
                                                uint32_t remote_checksum,
                                                int remote_checksum_known) {
    uint32_t boundary_frame = frame + 1u;
    size_t boundary_index = (size_t)(boundary_frame % GGPO_NET_HISTORY_FRAMES);
    size_t tick_index = (size_t)(frame % GGPO_NET_HISTORY_FRAMES);
    GgpoNetHistoryEntry* boundary = &g_net.history[boundary_index];
    GgpoNetHistoryEntry* tick = &g_net.history[tick_index];
    const uint8_t* blob;
    GgpoNetDesyncReproTask* task;
    size_t allocation;
    const char* directory = "mods\\desync_repros";
#ifdef GGPO_NET_TEST
    directory = getenv("EGGNOGGPLUS_TEST_REPRO_DIR");
    if (!directory || !directory[0]) return;
#endif
    if (g_net.desync_repro_captured) return;
    if (!g_net.session_confirmed || !g_net.has_remote_session_id ||
        !g_net.state_blobs || g_net.state_size == 0u ||
        !boundary->valid || boundary->frame != boundary_frame ||
        boundary->state_len == 0u ||
        boundary->state_len > g_net.state_size ||
        boundary->state_len > UINT32_MAX ||
        boundary_index > SIZE_MAX / g_net.state_size ||
        strlen(directory) >= MAX_PATH) {
        LOG_WARN("ggpo.net: first desync snapshot was no longer retainable");
        return;
    }
    if (boundary->state_len > SIZE_MAX -
            offsetof(GgpoNetDesyncReproTask, state)) {
        return;
    }
    allocation = offsetof(GgpoNetDesyncReproTask, state) +
                 boundary->state_len;
    task = (GgpoNetDesyncReproTask*)calloc(1u, allocation);
    if (!task) {
        LOG_WARN("ggpo.net: could not allocate first desync snapshot");
        return;
    }
    blob = g_net.state_blobs + boundary_index * g_net.state_size;
    snprintf(task->directory, sizeof(task->directory), "%s", directory);
    task->pair_tag = ggpo_net_desync_pair_tag();
    task->player = (uint32_t)g_net.local_player;
    task->desync_frame = frame;
    task->state_boundary_frame = boundary_frame;
    task->state_len = (uint32_t)boundary->state_len;
    task->local_checksum = local_checksum;
    task->remote_checksum = remote_checksum;
    task->remote_checksum_known = remote_checksum_known ? 1 : 0;
    if (tick->valid && tick->frame == frame) {
        task->local_cmd = tick->local_cmd;
        task->remote_cmd = tick->remote_cmd;
        task->remote_predicted = tick->remote_predicted ? 1u : 0u;
    }
    task->state_epoch = g_net.state_epoch;
    task->local_build_id = g_net.local_build_id;
    task->local_exe_id = g_net.local_exe_id;
    task->local_dll_id = g_net.local_dll_id;
    task->remote_build_id = g_net.remote_build_id;
    task->remote_exe_id = g_net.remote_exe_id;
    task->remote_dll_id = g_net.remote_dll_id;
    task->remote_fingerprint_known = g_net.has_remote_fingerprint ? 1 : 0;
    task->state_layout_id = g_net.state_layout_id;
    task->remote_state_layout_id = g_net.remote_state_layout_id;
    task->state_size = (uint32_t)g_net.state_size;
    task->deterministic_config_id = ggpo_net_desync_config_id();
    task->x87_control = fp_control_get_x87_control();
    task->mxcsr = fp_control_get_mxcsr();
    task->input_delay = g_net.input_delay;
    task->max_frame_advantage = g_net.max_frame_advantage;
    task->max_prediction = g_net.max_prediction;
    task->packets_sent = g_net.packets_sent;
    task->packets_received = g_net.packets_received;
    task->predictions = g_net.predictions;
    task->rollbacks = g_net.rollbacks;
    task->frame_advantage_stalls = g_net.frame_advantage_stalls;
    task->prediction_stalls = g_net.prediction_stalls;
    task->checksum_stalls = g_net.checksum_stalls;
    task->corrections_sent = g_net.corrections_sent;
    task->corrections_received = g_net.corrections_received;
    task->correction_requests = g_net.correction_requests;
    task->sim_loss_percent = g_net.sim_loss_percent;
    task->sim_delay_min_ticks = g_net.sim_delay_min_ticks;
    task->sim_delay_max_ticks = g_net.sim_delay_max_ticks;
    task->sim_dropped_packets = g_net.sim_packets_dropped;
    task->sim_delayed_packets = g_net.sim_packets_delayed;
    task->sim_seed = g_net.sim_seed;
    task->chaos_event_total = g_net.sim_chaos_event_total;
    task->chaos_event_count =
        g_net.sim_chaos_event_total < GGPO_NET_CHAOS_EVENT_CAP
            ? g_net.sim_chaos_event_total
            : GGPO_NET_CHAOS_EVENT_CAP;
    if (task->chaos_event_count != 0u) {
        uint32_t first =
            task->chaos_event_total - task->chaos_event_count;
        for (uint32_t i = 0u; i < task->chaos_event_count; i++) {
            task->chaos_events[i] = g_net.sim_chaos_events[
                (first + i) % GGPO_NET_CHAOS_EVENT_CAP];
        }
    }
    memcpy(task->state, blob, boundary->state_len);
    ggpo_net_wait_desync_repro_worker(0u);
    g_net.desync_repro_captured = 1;
    LOG_WARN("ggpo.net: queueing first desync snapshot pair=%08X frame=%u player=%u",
             (unsigned int)task->pair_tag,
             (unsigned int)frame,
             (unsigned int)task->player);
    g_desync_repro_worker = CreateThread(
        NULL, 0u, ggpo_net_desync_repro_worker, task, 0u, NULL);
    if (!g_desync_repro_worker) {
        g_net.desync_repro_captured = 0;
        SecureZeroMemory(task->state, task->state_len);
        free(task);
        LOG_WARN("ggpo.net: could not start first desync snapshot writer");
        return;
    }
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
    ggpo_net_capture_first_desync_repro(
        frame, local_checksum, remote_checksum, 1);
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
    g_net.correction_request_frame = frame;
    if (g_net.mode == GGPO_NET_MODE_HOST) {
        ggpo_net_begin_host_correction_request(frame);
        (void)ggpo_net_prepare_host_correction(why);
    } else {
        ggpo_net_begin_awaiting_correction(frame);
        (void)ggpo_net_request_host_correction(why, 1);
    }
}

static void ggpo_net_track_remote_cmd(uint32_t frame, uint32_t cmd) {
    if (!g_net.has_last_remote_cmd ||
        ggpo_net_frame_after(frame, g_net.last_remote_cmd_frame)) {
        g_net.last_remote_cmd = cmd;
        g_net.last_remote_cmd_frame = frame;
        g_net.has_last_remote_cmd = 1;
    }
}

static int ggpo_net_note_remote_frame(uint32_t frame) {
    if (!ggpo_net_frame_receive_admissible(frame)) {
        g_net.dropped_inputs++;
        return 0;
    }
    if (!g_net.has_remote_frame || ggpo_net_frame_after(frame, g_net.remote_frame)) {
        g_net.remote_frame = frame;
        g_net.has_remote_frame = 1;
    }
    return 1;
}

static int ggpo_net_note_remote_input(uint32_t frame, uint32_t cmd) {
    uint32_t old_cmd = 0;
    if (!ggpo_net_frame_receive_admissible(frame)) {
        g_net.dropped_inputs++;
        return 0;
    }
    int had_old = ggpo_net_get_input(g_net.remote_inputs, frame, &old_cmd);
    if (had_old) {
        if (old_cmd != cmd) {
            g_net.dropped_inputs++;
            g_net.peer_disconnected = 1;
            LOG_ERROR("ggpo.net: peer equivocated input frame=%u first=0x%08X later=0x%08X",
                      (unsigned int)frame,
                      (unsigned int)old_cmd,
                      (unsigned int)cmd);
            return 0;
        }
        ggpo_net_track_remote_cmd(frame, cmd);
        ggpo_net_advance_remote_contiguous_input();
        return 1;
    }

    if (!ggpo_net_store_input(g_net.remote_inputs, frame, cmd)) {
        /* A delayed packet from an older 512-frame generation must never evict
         * the newer entry already occupying this modulo slot. */
        g_net.dropped_inputs++;
        return 0;
    }
    ggpo_net_track_remote_cmd(frame, cmd);
    ggpo_net_advance_remote_contiguous_input();

    if (ggpo_net_frame_before(frame, g_net.frame)) {
        GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
        if (h->valid && h->frame == frame && h->remote_predicted) {
            if (h->remote_cmd != cmd) {
                if (!g_net.rollback_pending || ggpo_net_frame_before(frame, g_net.rollback_to)) {
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
    return 1;
}

static uint32_t ggpo_net_predict_remote(uint32_t frame, int* out_predicted) {
    uint32_t cmd = 0;
    if (ggpo_net_get_input(g_net.remote_inputs, frame, &cmd)) {
        if (out_predicted) *out_predicted = 0;
        ggpo_net_track_remote_cmd(frame, cmd);
        return cmd;
    }

    if (out_predicted) *out_predicted = 1;
    g_net.predictions++;

    /* Packets carry newest-to-oldest redundant inputs and may arrive out of
     * order. Predict from the nearest known input at or before this frame, never
     * from a future input merely because it was processed most recently. */
    for (uint32_t age = 1u; age <= GGPO_NET_INPUT_RETENTION_FRAMES; age++) {
        if (ggpo_net_get_input(g_net.remote_inputs, frame - age, &cmd)) {
            return cmd;
        }
    }
    if (g_net.has_last_remote_cmd &&
        ggpo_net_frame_after(frame, g_net.last_remote_cmd_frame)) {
        return g_net.last_remote_cmd;
    }
    return 0u;
}

#ifdef GGPO_NET_TEST
void ggpo_net_test_reset_frame_rings(uint32_t local_frame) {
    memset(g_net.history, 0, sizeof(g_net.history));
    memset(g_net.local_inputs, 0, sizeof(g_net.local_inputs));
    memset(g_net.remote_inputs, 0, sizeof(g_net.remote_inputs));
    ggpo_net_reset_checksum_channel(0u);
    g_net.frame = local_frame;
    g_net.frame_counter_wrapped = 0;
    g_net.remote_frame = 0u;
    g_net.has_remote_frame = 0;
    g_net.remote_contiguous_input_frame = 0u;
    g_net.has_remote_contiguous_input_frame = 0;
    g_net.peer_acked_local_input_frame = 0u;
    g_net.has_peer_acked_local_input_frame = 0;
    g_net.has_peer_input_ack = 0;
    g_net.highest_local_input_frame = 0u;
    g_net.has_highest_local_input_frame = 0;
    g_net.rollback_to = 0u;
    g_net.rollback_pending = 0;
    g_net.last_remote_cmd = 0u;
    g_net.last_remote_cmd_frame = 0u;
    g_net.has_last_remote_cmd = 0;
    g_net.predictions = 0u;
    g_net.late_inputs = 0u;
    g_net.dropped_inputs = 0u;
    g_net.peer_disconnected = 0;
    g_net.correction_active = 0;
    g_net.awaiting_correction = 0;
    g_net.correction_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_input_count = 0u;
    g_net.correction_snapshot_ready = 0;
    g_net.correction_local_applied = 0;
    g_net.correction_id = 0u;
}

void ggpo_net_test_set_local_frame(uint32_t local_frame) {
    if (local_frame < g_net.frame &&
        ggpo_net_frame_after(local_frame, g_net.frame)) {
        g_net.frame_counter_wrapped = 1;
    }
    g_net.frame = local_frame;
}

int ggpo_net_test_receive_remote_frame(uint32_t frame) {
    return ggpo_net_note_remote_frame(frame);
}

int ggpo_net_test_receive_remote_input(uint32_t frame, uint32_t cmd) {
    return ggpo_net_note_remote_input(frame, cmd);
}

int ggpo_net_test_get_remote_input(uint32_t frame, uint32_t* out_cmd) {
    return ggpo_net_get_input(g_net.remote_inputs, frame, out_cmd);
}

uint32_t ggpo_net_test_predict_remote_input(uint32_t frame) {
    int predicted = 0;
    return ggpo_net_predict_remote(frame, &predicted);
}

int ggpo_net_test_latest_remote_input(uint32_t* out_frame, uint32_t* out_cmd) {
    if (!g_net.has_last_remote_cmd) return 0;
    if (out_frame) *out_frame = g_net.last_remote_cmd_frame;
    if (out_cmd) *out_cmd = g_net.last_remote_cmd;
    return 1;
}

int ggpo_net_test_store_history_marker(uint32_t frame, uint32_t marker) {
    GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!ggpo_net_ring_generation_may_replace(h->valid, h->frame, frame)) {
        return 0;
    }
    memset(h, 0, sizeof(*h));
    h->valid = 1;
    h->frame = frame;
    h->post_checksum = marker;
    return 1;
}

int ggpo_net_test_get_history_marker(uint32_t frame, uint32_t* out_marker) {
    GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!h->valid || h->frame != frame) return 0;
    if (out_marker) *out_marker = h->post_checksum;
    return 1;
}

int ggpo_net_test_copy_history_state(uint32_t frame, void* dst, size_t dst_cap,
                                     size_t* out_len,
                                     uint32_t* out_post_checksum) {
    size_t index = (size_t)(frame % GGPO_NET_HISTORY_FRAMES);
    GgpoNetHistoryEntry* h = &g_net.history[index];
    const uint8_t* blob;
    if (!dst || !g_net.state_blobs || !h->valid || h->frame != frame ||
        h->state_len == 0u || h->state_len > dst_cap ||
        g_net.state_size == 0u ||
        index > SIZE_MAX / g_net.state_size) {
        return 0;
    }
    blob = g_net.state_blobs + index * g_net.state_size;
    memcpy(dst, blob, h->state_len);
    if (out_len) *out_len = h->state_len;
    if (out_post_checksum) *out_post_checksum = h->post_checksum;
    return 1;
}
#endif

static int ggpo_net_packet_has_input(const GgpoNetPacket* p,
                                     uint32_t count,
                                     uint32_t frame) {
    for (uint32_t i = 0u; i < count; i++) {
        if (p->inputs[i].frame == frame) return 1;
    }
    return 0;
}

static void ggpo_net_packet_add_local_input(GgpoNetPacket* p,
                                            uint32_t frame,
                                            uint32_t* io_count) {
    uint32_t cmd = 0u;
    GgpoNetInputEntry* entry;
    if (!p || !io_count || *io_count >= GGPO_NET_PACKET_INPUTS ||
        ggpo_net_packet_has_input(p, *io_count, frame)) return;
    entry = ggpo_net_input_slot(g_net.local_inputs, frame);
    if (!entry->valid || entry->frame != frame ||
        ggpo_net_local_input_acknowledged(entry) ||
        !ggpo_net_get_input(g_net.local_inputs, frame, &cmd)) return;
    p->inputs[*io_count].frame = frame;
    p->inputs[*io_count].cmd = cmd;
    (*io_count)++;
}

static int ggpo_net_packet_has_checksum(const GgpoNetPacket* p,
                                        uint32_t count,
                                        uint32_t frame) {
    for (uint32_t i = 0u; i < count; i++) {
        if (p->checksums[i].frame == frame) return 1;
    }
    return 0;
}

static void ggpo_net_packet_add_local_checksum(GgpoNetPacket* p,
                                               uint32_t frame,
                                               uint32_t* io_count,
                                               uint32_t* io_summary_count) {
    GgpoNetHistoryEntry* h;
    if (!p || !io_count || !io_summary_count ||
        *io_count >= GGPO_NET_PACKET_CHECKSUMS ||
        ggpo_net_packet_has_checksum(p, *io_count, frame) ||
        ggpo_net_frame_before(frame, g_net.peer_checksum_ack_next)) {
        return;
    }
    h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!h->valid || h->frame != frame || h->remote_predicted) return;
    p->checksums[*io_count].frame = frame;
    p->checksums[*io_count].checksum = h->post_checksum;
    (*io_count)++;
    h->checksum_published = 1;
    if (h->has_summary && *io_summary_count < GGPO_NET_PACKET_SUMMARIES) {
        p->summaries[*io_summary_count].frame = frame;
        p->summaries[*io_summary_count].summary = h->summary;
        (*io_summary_count)++;
    }
}

static void ggpo_net_fill_packet(GgpoNetPacket* p, uint16_t type) {
    uint32_t count = 0;
    uint32_t checksum_count = 0;
    uint32_t summary_count = 0;
    uint32_t latest_input_frame = g_net.has_highest_local_input_frame
        ? g_net.highest_local_input_frame
        : g_net.frame + g_net.input_delay;
    uint32_t checksum_horizon = 0u;
    int has_checksum_horizon = 0;
    int allow_checksum_payload = 1;
#ifdef GGPO_NET_TEST
    allow_checksum_payload = g_net_test_suppress_checksum_payload ? 0 : 1;
#endif
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
    p->correction_phase = g_net.correction_phase;
    if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE) {
        p->correction_ack_id = g_net.correction_id;
        p->correction_request_frame = g_net.correction_request_frame;
        p->correction_snapshot_frame = g_net.correction_frame;
        p->correction_resume_frame = g_net.correction_resume_frame;
        if (g_net.correction_phase == GGPO_NET_CORRECTION_REQUEST) {
            p->correction_ack_checksum = g_net.desync_local_checksum;
        } else if (g_net.correction_phase == GGPO_NET_CORRECTION_OFFER ||
                   g_net.correction_phase == GGPO_NET_CORRECTION_RECEIVING ||
                   g_net.correction_phase == GGPO_NET_CORRECTION_READY) {
            p->correction_ack_checksum = g_net.correction_checksum;
        } else {
            p->correction_ack_checksum = g_net.correction_transcript;
        }
    }
    p->state_epoch = g_net.state_epoch;
    p->prematch_hold = g_net.prematch_hold ? 1u : 0u;
    p->hold_epoch = g_net.hold_epoch;
    p->frame = g_net.frame;
    p->state_size = (uint32_t)g_net.state_size;
    p->state_checksum = g_net.initial_checksum;
    p->state_layout_id = g_net.state_layout_finalized ? g_net.state_layout_id : 0u;

    p->send_tick = g_net.service_tick;
    p->tick_echo = g_net.peer_last_send_tick;
    ggpo_net_build_input_ack(&p->input_ack_flags,
                             &p->input_ack_base,
                             p->input_ack_bits);
    p->checksum_ack_next = g_net.remote_checksum_ack_next;
    has_checksum_horizon = ggpo_net_checksum_horizon(&checksum_horizon);
    if (has_checksum_horizon) {
        p->input_ack_flags |= GGPO_NET_CONFIRMED_FRAME_VALID;
        p->confirmed_frame = checksum_horizon;
    }

    /* HELLO is also the transport-only heartbeat used during prematch. Keep all
     * frame input/checksum payloads off it so servicing a held session cannot
     * accidentally seed frame 0 with countdown/menu input. */
    if (type == GGPO_NET_PACKET_INPUT &&
        !g_net.prematch_hold &&
        !(g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) {
        /* Keep the live edge moving, then spend the rest of the packet on the
         * oldest unresolved holes. Unlike the former newest-64 window, a hole
         * remains selectable for the full recoverable ring lifetime. */
        ggpo_net_packet_add_local_input(p, latest_input_frame, &count);
        if (g_net.has_peer_input_ack) {
            uint32_t first_missing = g_net.has_peer_acked_local_input_frame
                ? g_net.peer_acked_local_input_frame + 1u
                : 0u;
            for (uint32_t i = 0u;
                 i < GGPO_NET_INPUT_ACK_BITS && count < GGPO_NET_PACKET_INPUTS;
                 i++) {
                ggpo_net_packet_add_local_input(p, first_missing + i, &count);
            }
        }
        /* Before the first ACK, and as a defensive tail beyond a stale ACK
         * window, preserve newest-to-oldest redundancy for every remaining slot. */
        for (uint32_t i = 0u;
             i < GGPO_NET_HISTORY_FRAMES && count < GGPO_NET_PACKET_INPUTS;
             i++) {
            ggpo_net_packet_add_local_input(p, latest_input_frame - i, &count);
        }
        p->input_count = count;

        if (has_checksum_horizon && allow_checksum_payload) {
            uint32_t first = g_net.peer_checksum_ack_next;
            /* Preserve low-latency desync detection at the live edge, then use
             * every remaining slot as cumulative go-back-N recovery beginning
             * at the peer's first unverified checksum. */
            ggpo_net_packet_add_local_checksum(p,
                                               checksum_horizon,
                                               &checksum_count,
                                               &summary_count);
            if (first == checksum_horizon ||
                ggpo_net_frame_before(first, checksum_horizon)) {
                uint32_t span = checksum_horizon - first;
                if (span < GGPO_NET_HISTORY_FRAMES) {
                    for (uint32_t i = 0u;
                         i <= span && checksum_count < GGPO_NET_PACKET_CHECKSUMS;
                         i++) {
                        uint32_t frame = first + i;
                        GgpoNetHistoryEntry* h =
                            &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
                        /* A cumulative stream cannot skip a required frame. A
                         * retention guard will stall before such a gap can be
                         * overwritten; stop here rather than hiding it. */
                        if (!h->valid || h->frame != frame ||
                            h->remote_predicted) break;
                        ggpo_net_packet_add_local_checksum(p,
                                                           frame,
                                                           &checksum_count,
                                                           &summary_count);
                    }
                }
            }
        }
    }
    p->checksum_count = checksum_count;
    p->summary_count = summary_count;
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_store_local_input(uint32_t frame, uint32_t cmd) {
    char err[128];
    err[0] = '\0';
    return ggpo_net_store_local_input(frame, cmd, err, sizeof(err));
}

int ggpo_net_test_get_local_input(uint32_t frame, uint32_t* out_cmd) {
    return ggpo_net_get_input(g_net.local_inputs, frame, out_cmd);
}

int ggpo_net_test_get_remote_ack(
    uint32_t* out_valid,
    uint32_t* out_frame,
    uint32_t out_bits[GGPO_NET_TEST_ACK_WORDS]) {
    uint32_t flags = 0u;
    if (!out_bits) return 0;
    ggpo_net_build_input_ack(&flags, out_frame, out_bits);
    if (out_valid) *out_valid = (flags & GGPO_NET_INPUT_ACK_VALID) ? 1u : 0u;
    return 1;
}

int ggpo_net_test_apply_peer_ack(
    uint32_t valid,
    uint32_t frame,
    const uint32_t bits[GGPO_NET_TEST_ACK_WORDS]) {
    return ggpo_net_note_peer_input_ack(valid ? GGPO_NET_INPUT_ACK_VALID : 0u,
                                        frame,
                                        bits);
}

int ggpo_net_test_get_peer_ack(uint32_t* out_valid, uint32_t* out_frame) {
    if (out_valid) *out_valid = g_net.has_peer_acked_local_input_frame ? 1u : 0u;
    if (out_frame) {
        *out_frame = g_net.has_peer_acked_local_input_frame
            ? g_net.peer_acked_local_input_frame
            : 0u;
    }
    return g_net.has_peer_input_ack ? 1 : 0;
}

uint32_t ggpo_net_test_build_input_packet(uint32_t* out_frames,
                                          uint32_t* out_cmds,
                                          uint32_t capacity) {
    GgpoNetPacket packet;
    ggpo_net_fill_packet(&packet, GGPO_NET_PACKET_INPUT);
    if (capacity > packet.input_count) capacity = packet.input_count;
    for (uint32_t i = 0u; i < capacity; i++) {
        if (out_frames) out_frames[i] = packet.inputs[i].frame;
        if (out_cmds) out_cmds[i] = packet.inputs[i].cmd;
    }
    return packet.input_count;
}

uint32_t ggpo_net_test_build_checksum_packet(uint32_t* out_frames,
                                             uint32_t* out_checksums,
                                             uint32_t capacity) {
    GgpoNetPacket packet;
    ggpo_net_fill_packet(&packet, GGPO_NET_PACKET_INPUT);
    if (capacity > packet.checksum_count) capacity = packet.checksum_count;
    for (uint32_t i = 0u; i < capacity; i++) {
        if (out_frames) out_frames[i] = packet.checksums[i].frame;
        if (out_checksums) out_checksums[i] = packet.checksums[i].checksum;
    }
    return packet.checksum_count;
}

int ggpo_net_test_checksum_horizon(uint32_t* out_frame) {
    return ggpo_net_checksum_horizon(out_frame);
}

void ggpo_net_test_set_remote_contiguous(uint32_t valid, uint32_t frame) {
    g_net.has_remote_contiguous_input_frame = valid ? 1 : 0;
    g_net.remote_contiguous_input_frame = valid ? frame : 0u;
}

void ggpo_net_test_set_peer_ack(uint32_t valid, uint32_t frame) {
    g_net.has_peer_input_ack = 1;
    g_net.has_peer_acked_local_input_frame = valid ? 1 : 0;
    g_net.peer_acked_local_input_frame = valid ? frame : 0u;
}

void ggpo_net_test_set_rollback_pending(int pending) {
    g_net.rollback_pending = pending ? 1 : 0;
}

int ggpo_net_test_seed_history(uint32_t frame,
                               uint32_t remote_cmd,
                               int remote_predicted,
                               uint32_t post_checksum) {
    GgpoNetHistoryEntry* h = &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!ggpo_net_ring_generation_may_replace(h->valid, h->frame, frame)) return 0;
    memset(h, 0, sizeof(*h));
    h->valid = 1;
    h->frame = frame;
    h->remote_cmd = remote_cmd;
    h->remote_predicted = remote_predicted ? 1 : 0;
    h->post_checksum = post_checksum;
    return 1;
}
#endif

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
            if (g_net.socket_backpressured_this_tick) break;
            if (ggpo_net_send_packet_to(GGPO_NET_PACKET_HELLO, &g_net.candidates[c])) sent_any = 1;
        }
        if (g_net.socket_backpressured_this_tick) break;
        /* Fall back to peer_addr if no candidates were registered (older path). */
        if (!sent_any && g_net.has_peer_addr) {
            (void)ggpo_net_send_packet_to(GGPO_NET_PACKET_HELLO, &g_net.peer_addr);
        }
        if (g_net.socket_backpressured_this_tick) break;
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

static int ggpo_net_palette_tuple_valid(uint32_t count,
                                        uint32_t skin,
                                        uint32_t clothing) {
    return count > 0u &&
           count <= GGPO_NET_PALETTE_MAX_ENTRIES &&
           skin < count &&
           clothing < count;
}

static int ggpo_net_palette_ready_internal(void) {
    if (!g_net.local_palette_valid) return 1;
    return !g_net.palette_conflict &&
           g_net.remote_palette_valid &&
           g_net.local_palette_acked;
}

static int ggpo_net_send_palette(void) {
    GgpoNetPalettePacket p;
    if (!g_net.local_palette_valid || g_net.palette_conflict ||
        !g_net.has_peer_addr || g_net.sock == INVALID_SOCKET ||
        !ggpo_net_link_confirmed()) {
        return 0;
    }
    memset(&p, 0, sizeof(p));
    p.magic = GGPO_NET_MAGIC;
    p.version = GGPO_NET_VERSION;
    p.type = GGPO_NET_PACKET_PALETTE;
    p.session_id = g_net.session_id;
    p.sender_player = (uint32_t)g_net.local_player;
    p.palette_count = g_net.local_palette_count;
    p.skin_index = g_net.local_palette_skin;
    p.clothing_index = g_net.local_palette_clothing;
    if (g_net.remote_palette_valid) {
        p.ack_valid = 1u;
        p.ack_palette_count = g_net.remote_palette_count;
        p.ack_skin_index = g_net.remote_palette_skin;
        p.ack_clothing_index = g_net.remote_palette_clothing;
    }
    if (!ggpo_net_send_bytes(&p, (int)sizeof(p), &g_net.peer_addr, 1)) {
        return 0;
    }
    g_net.last_palette_send_tick = g_net.service_tick;
    return 1;
}

static void ggpo_net_send_palette_periodic(void) {
    if (!g_net.local_palette_valid || g_net.palette_conflict ||
        !ggpo_net_link_confirmed() || g_net.frame != 0u) {
        return;
    }
    if (g_net.last_palette_send_tick != 0u &&
        g_net.service_tick - g_net.last_palette_send_tick < 10u) {
        return;
    }
    (void)ggpo_net_send_palette();
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
    if (!ggpo_net_send_bytes(&p,
                             (int)(offsetof(GgpoNetCosmeticPacket, profile) + p.profile_len),
                             &g_net.peer_addr,
                             1)) {
        return 0;
    }
    g_net.last_cosmetic_profile_send_tick = g_net.service_tick;
    return 1;
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
        if (g_net.socket_backpressured_this_tick ||
            !ggpo_net_send_cosmetic_asset_chunk(
                g_net.local_cosmetic_asset_next_chunk)) {
            break;
        }
        g_net.local_cosmetic_asset_next_chunk = (g_net.local_cosmetic_asset_next_chunk + 1u) % chunk_count;
        g_net.local_cosmetic_asset_last_send_tick = g_net.service_tick;
    }
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
    p.base_checksum = (flags & GGPO_NET_STATE_FLAG_CORRECTION)
        ? 0u
        : g_net.state_layout_id;
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

#ifdef GGPO_NET_TEST
int ggpo_net_test_send_correction_chunk_once(void) {
    return ggpo_net_send_correction_chunk();
}

void ggpo_net_test_get_correction_send_cursor(uint32_t* out_full_offset,
                                               uint32_t* out_delta_next) {
    if (out_full_offset) *out_full_offset = g_net.correction_send_offset;
    if (out_delta_next) *out_delta_next = g_net.correction_send_next_chunk;
}
#endif

static void ggpo_net_send_state_sync_burst(void) {
    if (g_net.mode != GGPO_NET_MODE_HOST) return;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return;
    if (!ggpo_net_link_confirmed() || g_net.remote_state_synced) return;
    if (g_net.socket_backpressured_this_tick) {
        ggpo_net_saturating_increment(&g_net.socket_send_work_deferred);
        return;
    }
    for (int i = 0; i < 8; i++) {
        if (!ggpo_net_send_state_chunk()) break;
    }
}

static void ggpo_net_reset_checksum_channel(uint32_t start_frame) {
    memset(g_net.remote_checksums, 0, sizeof(g_net.remote_checksums));
    g_net.checksum_stream_start_frame = start_frame;
    g_net.remote_checksum_ack_next = start_frame;
    g_net.peer_checksum_ack_next = start_frame;
    g_net.checksum_wait_start_tick = 0u;
    g_net.checksum_wait_frame = 0u;
    g_net.checksum_wait_announced = 0;
}

static void ggpo_net_clear_runtime_history(void) {
    memset(g_net.history, 0, sizeof(g_net.history));
    memset(g_net.local_inputs, 0, sizeof(g_net.local_inputs));
    memset(g_net.remote_inputs, 0, sizeof(g_net.remote_inputs));
    ggpo_net_reset_checksum_channel(0u);
    g_net.rollback_to = 0;
    g_net.rollback_pending = 0;
    g_net.last_remote_cmd = 0;
    g_net.last_remote_cmd_frame = 0;
    g_net.has_last_remote_cmd = 0;
    g_net.remote_frame = 0;
    g_net.has_remote_frame = 0;
    g_net.remote_contiguous_input_frame = 0;
    g_net.has_remote_contiguous_input_frame = 0;
    g_net.peer_acked_local_input_frame = 0;
    g_net.has_peer_acked_local_input_frame = 0;
    g_net.has_peer_input_ack = 0;
    g_net.highest_local_input_frame = 0;
    g_net.has_highest_local_input_frame = 0;
    g_net.frame_counter_wrapped = 0;
    g_net.frame_advantage_wait_announced = 0;
    g_net.prediction_limit_wait_announced = 0;
    g_net.frame_advantage_wait_start_tick = 0;
    g_net.prediction_wait_start_tick = 0;
    g_net.frame_advantage_wait_cap_announced = 0;
    g_net.prediction_wait_cap_announced = 0;
    /* After a state reset the next tick must use the freshly loaded/synced
     * simulation state, never a stale pre-reset snapshot. */
    g_net.have_clean_sim_state = 0;
}

static int ggpo_net_capture_clean_sim_state(char* err, size_t err_cap) {
    uint32_t seed = 0u;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float camera_shake = 0.0f;
    float camera_shake_decay = 0.0f;
    float game_w = 0.0f;
    float game_h = 0.0f;
    if (!lua_manager_game_rng_seed(&seed) ||
        !lua_manager_game_camera(&camera_x, &camera_y) ||
        !lua_manager_game_camera_shake(&camera_shake,
                                       &camera_shake_decay) ||
        !lua_manager_game_width(&game_w) ||
        !lua_manager_game_height(&game_h)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to capture clean native simulation state");
        return 0;
    }
    g_net.clean_mrand_seed = seed;
    g_net.clean_camera_x = camera_x;
    g_net.clean_camera_y = camera_y;
    g_net.clean_camera_shake = camera_shake;
    g_net.clean_camera_shake_decay = camera_shake_decay;
    g_net.clean_game_w = game_w;
    g_net.clean_game_h = game_h;
    g_net.have_clean_sim_state = 1;
    return 1;
}

static int ggpo_net_capture_local_render_geometry(char* err, size_t err_cap) {
    float game_w = 0.0f;
    float game_h = 0.0f;
    if (!lua_manager_game_width(&game_w) ||
        !lua_manager_game_height(&game_h)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to capture local render geometry");
        return 0;
    }
    g_net.local_render_game_w = game_w;
    g_net.local_render_game_h = game_h;
    g_net.have_local_render_geometry = 1;
    return 1;
}

static int ggpo_net_restore_local_render_geometry(char* err, size_t err_cap) {
    if (!g_net.have_local_render_geometry) {
        ggpo_net_set_err(err, err_cap,
                         "local render geometry is unavailable");
        return 0;
    }
    if (!lua_manager_game_set_geometry(g_net.local_render_game_w,
                                       g_net.local_render_game_h)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to restore local render geometry");
        return 0;
    }
    return 1;
}

static int ggpo_net_restore_clean_sim_state(char* err, size_t err_cap) {
    if (!g_net.have_clean_sim_state) {
        ggpo_net_set_err(err, err_cap,
                         "clean native simulation state is unavailable");
        return 0;
    }
    if (!lua_manager_game_set_sim_state(g_net.clean_mrand_seed,
                                        g_net.clean_camera_x,
                                        g_net.clean_camera_y,
                                        g_net.clean_camera_shake,
                                        g_net.clean_camera_shake_decay,
                                        g_net.clean_game_w,
                                        g_net.clean_game_h)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to restore clean native simulation state");
        return 0;
    }
    return 1;
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
    g_net.correction_progress_tick = 0u;
    g_net.correction_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_resume_frame = 0u;
    g_net.correction_transcript = 0u;
    g_net.correction_expected_transcript = 0u;
    g_net.correction_input_count = 0u;
    g_net.correction_apply_attempts = 0u;
    g_net.last_correction_ack_checksum = 0u;
    g_net.last_correction_ack_id = 0u;
    g_net.last_correction_applied_checksum = 0u;
    g_net.last_correction_applied_id = 0u;
    g_net.last_resync_request_tick = 0u;
    g_net.correction_active = 0;
    g_net.correction_send_delta = 0;
    g_net.awaiting_correction = 0;
    g_net.correction_snapshot_ready = 0;
    g_net.correction_local_applied = 0;
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

static void ggpo_net_discard_state_storage(void) {
    ggpo_net_reset_recv_state();
    free(g_net.state_blobs);
    free(g_net.initial_state);
    free(g_net.correction_state);
    free(g_net.correction_base_state);
    free(g_net.apply_backup_state);
    free(g_net.apply_verify_state);
    g_net.state_blobs = NULL;
    g_net.initial_state = NULL;
    g_net.correction_state = NULL;
    g_net.correction_base_state = NULL;
    g_net.apply_backup_state = NULL;
    g_net.apply_verify_state = NULL;
    g_net.state_size = 0u;
    g_net.initial_state_len = 0u;
    g_net.correction_state_len = 0u;
    g_net.correction_base_state_len = 0u;
    g_net.initial_checksum = 0u;
    g_net.last_checksum = 0u;
    g_net.correction_base_checksum = 0u;
    g_net.state_layout_id = 0u;
    g_net.state_layout_finalized = 0;
}

int ggpo_net_finalize_state_layout(char* err, size_t err_cap) {
    size_t final_size;
    size_t final_len = 0u;
    uint32_t final_layout_id;
    uint32_t final_checksum = 0u;
    uint8_t* new_blobs = NULL;
    uint8_t* new_initial = NULL;
    uint8_t* new_correction = NULL;
    uint8_t* new_correction_base = NULL;
    uint8_t* new_apply_backup = NULL;
    uint8_t* new_apply_verify = NULL;

    if (err && err_cap > 0u) err[0] = '\0';

    if (!g_net.active) {
        ggpo_net_set_err(err, err_cap, "net session is not active");
        return 0;
    }
    if (!g_net.prematch_hold || g_net.frame != 0u || g_net.start_state_loaded) {
        ggpo_net_set_err(err, err_cap,
                         "state layout can only be finalized during held prematch");
        return 0;
    }

    final_size = ggpo_ext_game_state_size();
    final_layout_id = ggpo_ext_game_state_layout_fingerprint();
    if (final_size == 0u || final_size > (size_t)UINT_MAX ||
        final_size > SIZE_MAX / (size_t)GGPO_NET_HISTORY_FRAMES ||
        final_layout_id == 0u) {
        ggpo_net_set_err(err, err_cap, "final rollback state layout is unavailable or too large");
        return 0;
    }
    if (g_net.state_layout_finalized) {
        if (g_net.state_size == final_size && g_net.state_layout_id == final_layout_id) {
            return 1;
        }
        ggpo_net_set_err(err, err_cap, "rollback state layout changed after it was frozen");
        return 0;
    }

    /* Allocate and capture entirely off to the side. A failure leaves the held
     * session/socket intact and no partially installed capacity can leak into a
     * HELLO or state transfer. */
    new_blobs = (uint8_t*)calloc((size_t)GGPO_NET_HISTORY_FRAMES, final_size);
    new_initial = (uint8_t*)malloc(final_size);
    new_correction = (uint8_t*)malloc(final_size);
    new_correction_base = (uint8_t*)malloc(final_size);
    new_apply_backup = (uint8_t*)malloc(final_size);
    new_apply_verify = (uint8_t*)malloc(final_size);
    if (!new_blobs || !new_initial || !new_correction || !new_correction_base ||
        !new_apply_backup || !new_apply_verify) {
        free(new_blobs);
        free(new_initial);
        free(new_correction);
        free(new_correction_base);
        free(new_apply_backup);
        free(new_apply_verify);
        ggpo_net_set_err(err, err_cap, "out of memory finalizing rollback state layout");
        return 0;
    }
    if (!ggpo_ext_save_game_state(new_blobs,
                                  final_size,
                                  &final_len,
                                  &final_checksum,
                                  err,
                                  err_cap) ||
        final_len == 0u || final_len > final_size) {
        free(new_blobs);
        free(new_initial);
        free(new_correction);
        free(new_correction_base);
        free(new_apply_backup);
        free(new_apply_verify);
        if (!err || !err[0]) {
            ggpo_net_set_err(err, err_cap, "failed to capture final rollback state");
        }
        return 0;
    }
    if (ggpo_ext_game_state_size() != final_size ||
        ggpo_ext_game_state_layout_fingerprint() != final_layout_id) {
        free(new_blobs);
        free(new_initial);
        free(new_correction);
        free(new_correction_base);
        free(new_apply_backup);
        free(new_apply_verify);
        ggpo_net_set_err(err, err_cap, "rollback state layout changed while being captured");
        return 0;
    }

    memcpy(new_initial, new_blobs, final_len);
    memcpy(new_correction_base, new_blobs, final_len);
    ggpo_net_discard_state_storage();
    g_net.state_blobs = new_blobs;
    g_net.initial_state = new_initial;
    g_net.correction_state = new_correction;
    g_net.correction_base_state = new_correction_base;
    g_net.apply_backup_state = new_apply_backup;
    g_net.apply_verify_state = new_apply_verify;
    g_net.state_size = final_size;
    g_net.initial_state_len = final_len;
    g_net.correction_base_state_len = final_len;
    g_net.initial_checksum = final_checksum;
    g_net.last_checksum = final_checksum;
    g_net.correction_base_checksum = final_checksum;
    g_net.state_layout_id = final_layout_id;
    g_net.state_layout_finalized = 1;
    ggpo_net_clear_runtime_history();
    g_net.state_synced = (g_net.mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.remote_state_synced = (g_net.mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_HELLO);
    LOG_INFO("ggpo.net: finalized rollback layout mode=%s schema=%08X capacity=%u snapshot=%u checksum=%u",
             ggpo_net_mode_name(),
             (unsigned int)g_net.state_layout_id,
             (unsigned int)g_net.state_size,
             (unsigned int)g_net.initial_state_len,
             (unsigned int)g_net.initial_checksum);
    return 1;
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
    if (g_net.prematch_used && !g_net.prematch_hold &&
        (!g_net.state_layout_finalized || !ggpo_net_state_layout_ready() ||
         g_net.state_size == 0u || !g_net.state_blobs || !g_net.initial_state ||
         !g_net.correction_state || !g_net.correction_base_state ||
         !g_net.apply_backup_state || !g_net.apply_verify_state)) {
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

/* Apply a network-provided rollback blob as a transaction. Validation and the
 * advertised checksum are checked before any live write. If the rollback load
 * itself fails or produces the wrong canonical state, restore the exact raw
 * serializer image captured immediately before it and verify that restoration
 * byte-for-byte. A failed restore is unrecoverable: continuing would simulate
 * from an unknown, partially written state. */
static int ggpo_net_apply_received_state_transaction(const void* candidate,
                                                     size_t candidate_len,
                                                     uint32_t expected_checksum,
                                                     uint32_t* out_checksum,
                                                     int* out_restored_failure,
                                                     char* err,
                                                     size_t err_cap) {
    uint32_t candidate_checksum = 0u;
    uint32_t backup_checksum = 0u;
    uint32_t live_checksum = 0u;
    uint32_t verify_checksum = 0u;
    size_t backup_len = 0u;
    size_t verify_len = 0u;
    char cause[256];
    char restore_err[256];

    if (err && err_cap > 0u) err[0] = '\0';
    if (out_checksum) *out_checksum = 0u;
    if (out_restored_failure) *out_restored_failure = 0;
    if (!candidate || candidate_len == 0u ||
        candidate_len > g_net.state_size ||
        !g_net.apply_backup_state || !g_net.apply_verify_state) {
        ggpo_net_set_err(err, err_cap, "state-apply transaction storage is unavailable");
        return 0;
    }

    if (!ggpo_ext_validate_rollback_transport_blob(candidate,
                                                   candidate_len,
                                                   &candidate_checksum,
                                                   err,
                                                   err_cap)) {
        return 0;
    }
    if (candidate_checksum != expected_checksum) {
        if (err && err_cap > 0u) {
            snprintf(err, err_cap,
                     "state blob checksum mismatch (validated=%u expected=%u)",
                     (unsigned int)candidate_checksum,
                     (unsigned int)expected_checksum);
        }
        return 0;
    }

    if (!ggpo_ext_save_game_state_raw(g_net.apply_backup_state,
                                      g_net.state_size,
                                      &backup_len,
                                      err,
                                      err_cap) ||
        backup_len == 0u || backup_len > g_net.state_size) {
        if (!err || !err[0]) {
            ggpo_net_set_err(err, err_cap, "failed to capture pre-apply game state");
        }
        return 0;
    }
    if (!ggpo_ext_validate_rollback_blob(g_net.apply_backup_state,
                                         backup_len,
                                         &backup_checksum,
                                         err,
                                         err_cap)) {
        return 0;
    }

    cause[0] = '\0';
    if (!ggpo_ext_load_game_state(candidate, candidate_len, cause, sizeof(cause))) {
        if (!cause[0]) snprintf(cause, sizeof(cause), "rollback state load failed");
    } else if (!lua_manager_game_state_rollback_checksum(&live_checksum,
                                                         cause,
                                                         sizeof(cause))) {
        if (!cause[0]) snprintf(cause, sizeof(cause), "post-load checksum failed");
    } else if (live_checksum != expected_checksum) {
        snprintf(cause, sizeof(cause),
                 "post-load checksum mismatch (live=%u expected=%u)",
                 (unsigned int)live_checksum,
                 (unsigned int)expected_checksum);
    } else {
        if (out_checksum) *out_checksum = live_checksum;
        return 1;
    }

    restore_err[0] = '\0';
    if (!ggpo_ext_load_game_state_raw(g_net.apply_backup_state,
                                      backup_len,
                                      restore_err,
                                      sizeof(restore_err)) ||
        !ggpo_ext_save_game_state_raw(g_net.apply_verify_state,
                                      g_net.state_size,
                                      &verify_len,
                                      restore_err,
                                      sizeof(restore_err)) ||
        verify_len != backup_len ||
        memcmp(g_net.apply_verify_state,
               g_net.apply_backup_state,
               backup_len) != 0 ||
        !ggpo_ext_validate_rollback_blob(g_net.apply_verify_state,
                                         verify_len,
                                         &verify_checksum,
                                         restore_err,
                                         sizeof(restore_err)) ||
        verify_checksum != backup_checksum) {
        g_net.peer_disconnected = 1;
        if (err && err_cap > 0u) {
            snprintf(err, err_cap,
                     "fatal state restore failure after %s (%s)",
                     cause[0] ? cause : "candidate apply failure",
                     restore_err[0] ? restore_err : "restored bytes/checksum did not match");
        }
        return 0;
    }

    if (err && err_cap > 0u) {
        snprintf(err, err_cap, "%s; previous state restored",
                 cause[0] ? cause : "candidate apply failed");
    }
    if (out_restored_failure) *out_restored_failure = 1;
    return 0;
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_apply_state_transaction(const void* candidate,
                                          size_t candidate_len,
                                          uint32_t expected_checksum,
                                          uint32_t* out_checksum,
                                          int* out_fatal_restore,
                                          char* err,
                                          size_t err_cap) {
    uint8_t* old_backup = g_net.apply_backup_state;
    uint8_t* old_verify = g_net.apply_verify_state;
    size_t old_state_size = g_net.state_size;
    int old_peer_disconnected = g_net.peer_disconnected;
    size_t test_state_size = ggpo_ext_game_state_size();
    int ok;

    if (out_fatal_restore) *out_fatal_restore = 0;
    if (test_state_size == 0u) {
        ggpo_net_set_err(err, err_cap, "test state layout is unavailable");
        return 0;
    }
    g_net.apply_backup_state = (uint8_t*)malloc(test_state_size);
    g_net.apply_verify_state = (uint8_t*)malloc(test_state_size);
    if (!g_net.apply_backup_state || !g_net.apply_verify_state) {
        free(g_net.apply_backup_state);
        free(g_net.apply_verify_state);
        g_net.apply_backup_state = old_backup;
        g_net.apply_verify_state = old_verify;
        ggpo_net_set_err(err, err_cap, "out of memory preparing state transaction test");
        return 0;
    }
    g_net.state_size = test_state_size;
    g_net.peer_disconnected = 0;
    ok = ggpo_net_apply_received_state_transaction(candidate,
                                                   candidate_len,
                                                   expected_checksum,
                                                   out_checksum,
                                                   NULL,
                                                   err,
                                                   err_cap);
    if (out_fatal_restore) *out_fatal_restore = g_net.peer_disconnected ? 1 : 0;
    free(g_net.apply_backup_state);
    free(g_net.apply_verify_state);
    g_net.apply_backup_state = old_backup;
    g_net.apply_verify_state = old_verify;
    g_net.state_size = old_state_size;
    g_net.peer_disconnected = old_peer_disconnected;
    return ok;
}
#endif

static void ggpo_net_handle_state_chunk(const GgpoNetStateChunkPacket* p, int got_len, const struct sockaddr_in* from) {
    uint32_t checksum = 0;
    uint32_t flags = 0;
    uint32_t expected_full_chunk_count = 0;
    uint32_t expected_offset = 0;
    uint32_t expected_chunk_size = 0;
    int is_correction = 0;
    int is_delta = 0;
    char err[256];

    if (!p || p->magic != GGPO_NET_MAGIC || p->version != GGPO_NET_VERSION) return;
    if (p->type != GGPO_NET_PACKET_STATE_CHUNK) return;
    if (g_net.mode != GGPO_NET_MODE_JOIN) return;
    if ((int)p->sender_player != g_net.remote_player) return;
    if (!ggpo_net_accept_packet_source(from, p->session_id, "state", 0)) return;
    if (!ggpo_net_link_confirmed()) return;
    if (p->state_epoch == 0u) return;
    if (!g_net.state_layout_finalized ||
        !ggpo_net_state_layout_ready() ||
        ggpo_net_state_layout_mismatch()) return;
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
    if (flags != p->flags) {
        g_net.stale_state_chunks_dropped++;
        return;
    }
    is_correction = (flags & GGPO_NET_STATE_FLAG_CORRECTION) ? 1 : 0;
    is_delta = (flags & GGPO_NET_STATE_FLAG_DELTA) ? 1 : 0;
    /* Frame-zero state is immutable once accepted. Only the coordinated
     * correction barrier may replace live state; a conflicting retransmission
     * must never reset an already running joiner to frame zero. */
    if (!is_correction &&
        (p->frame != 0u || p->correction_id != 0u || is_delta ||
         (g_net.state_synced &&
          (p->state_checksum != g_net.initial_checksum ||
           p->state_size != g_net.initial_state_len)))) {
        g_net.stale_state_chunks_dropped++;
        return;
    }
    if (!is_correction && p->base_checksum != g_net.state_layout_id) {
        g_net.stale_state_chunks_dropped++;
        return;
    }
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
    if (is_correction && p->correction_id == 0u) return;
    if (is_correction &&
        (!g_net.awaiting_correction ||
         (g_net.correction_phase != GGPO_NET_CORRECTION_RECEIVING &&
          g_net.correction_phase != GGPO_NET_CORRECTION_READY) ||
         p->correction_id != g_net.correction_id ||
         p->frame != g_net.correction_frame ||
         p->state_checksum != g_net.correction_checksum)) return;
    if (is_delta) {
        /* The coordinated v17 barrier intentionally stages a complete
         * snapshot.  A delta without an explicit base-negotiation phase is not
         * safe to compose with READY/COMMIT, so reject it without mutation. */
        return;
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
    if (is_correction) ggpo_net_note_correction_progress();
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

    if (is_correction) {
        uint32_t validated_checksum = 0u;
        err[0] = '\0';
        if (!ggpo_ext_validate_rollback_transport_blob(g_net.recv_state,
                                                        g_net.recv_state_len,
                                                        &validated_checksum,
                                                        err,
                                                        sizeof(err)) ||
            validated_checksum != g_net.correction_checksum) {
            LOG_ERROR("ggpo.net: staged correction rejected id=%u frame=%u (%s)",
                      (unsigned int)p->correction_id,
                      (unsigned int)p->frame,
                      err[0] ? err : "checksum mismatch");
            g_net.peer_disconnected = 1;
            return;
        }
        g_net.correction_snapshot_ready = 1;
        ggpo_net_note_correction_progress();
        /* Input ACKs keep moving while the snapshot transfers.  READY is
         * promoted by ggpo_net_progress_correction only after this peer proves
         * and pins every exact input through resume-1. */
        return;
    }

    err[0] = '\0';
    if (!ggpo_net_capture_local_render_geometry(err, sizeof(err))) {
        LOG_ERROR("ggpo.net: host state could not preserve local render geometry (%s)",
                  err[0] ? err : "geometry unavailable");
        g_net.peer_disconnected = 1;
        ggpo_net_reset_recv_state();
        return;
    }
    if (!ggpo_net_apply_received_state_transaction(g_net.recv_state,
                                                   g_net.recv_state_len,
                                                   p->state_checksum,
                                                   &checksum,
                                                   NULL,
                                                   err,
                                                   sizeof(err))) {
        LOG_ERROR("ggpo.net: %s state transaction rejected id=%u frame=%u (%s)",
                  is_correction ? "correction" : "host",
                  (unsigned int)p->correction_id,
                  (unsigned int)p->frame,
                  err[0] ? err : "unknown error");
        if (g_net.peer_disconnected) {
            ggpo_net_reset_recv_state();
            return;
        }
        /* A completed receive buffer otherwise becomes a permanent dead end:
         * repeated host chunks are duplicates. Drop the failed assembly so the
         * cyclic host burst can retry from a clean buffer. */
        g_net.state_synced = 0;
        g_net.start_state_loaded = 0;
        g_net.state_sync_announced = 0;
        ggpo_net_reset_recv_state();
        return;
    }
    if (!ggpo_net_restore_local_render_geometry(err, sizeof(err))) {
        LOG_ERROR("ggpo.net: host state could not restore local render geometry (%s)",
                  err[0] ? err : "geometry unavailable");
        g_net.peer_disconnected = 1;
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

#ifdef GGPO_NET_TEST
int ggpo_net_test_initial_state_chunk_rejected(uint32_t checksum_xor,
                                               uint32_t frame,
                                               uint32_t correction_id,
                                               uint32_t flags) {
    GgpoNetStateChunkPacket packet;
    uint32_t old_frame = g_net.frame;
    uint32_t old_epoch = g_net.state_epoch;
    uint32_t old_dropped = g_net.stale_state_chunks_dropped;
    if (g_net.mode != GGPO_NET_MODE_JOIN || !g_net.state_synced ||
        !ggpo_net_link_confirmed() || g_net.recv_state ||
        !g_net.initial_state || !g_net.initial_state_len) return 0;
    memset(&packet, 0, sizeof(packet));
    packet.magic = GGPO_NET_MAGIC;
    packet.version = GGPO_NET_VERSION;
    packet.type = GGPO_NET_PACKET_STATE_CHUNK;
    packet.sender_player = (uint32_t)g_net.remote_player;
    packet.session_id = g_net.remote_session_id;
    packet.state_epoch = g_net.state_epoch;
    packet.state_size = (uint32_t)g_net.initial_state_len;
    packet.state_checksum = g_net.initial_checksum ^ checksum_xor;
    packet.base_checksum = g_net.state_layout_id;
    packet.full_chunk_count = ggpo_net_state_chunk_count(packet.state_size);
    packet.chunk_count = packet.full_chunk_count;
    packet.chunk_size = ggpo_net_state_chunk_size(g_net.initial_state_len, 0u);
    packet.frame = frame;
    packet.correction_id = correction_id;
    packet.flags = flags;
    memcpy(packet.data, g_net.initial_state, packet.chunk_size);
    ggpo_net_handle_state_chunk(&packet,
        (int)(offsetof(GgpoNetStateChunkPacket, data) + packet.chunk_size),
        &g_net.peer_addr);
    return !g_net.recv_state && g_net.frame == old_frame &&
           g_net.state_epoch == old_epoch && g_net.state_synced &&
           g_net.stale_state_chunks_dropped == old_dropped + 1u &&
           !g_net.peer_disconnected;
}
#endif

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

static void ggpo_net_note_palette_conflict(const char* reason,
                                           const GgpoNetPalettePacket* p) {
    if (!g_net.palette_conflict) {
        LOG_ERROR("ggpo.net: palette negotiation rejected (%s) local=%u:%u/%u remote=%u:%u/%u",
                  (reason && reason[0]) ? reason : "invalid tuple",
                  (unsigned int)g_net.local_palette_count,
                  (unsigned int)g_net.local_palette_skin,
                  (unsigned int)g_net.local_palette_clothing,
                  (unsigned int)(p ? p->palette_count : 0u),
                  (unsigned int)(p ? p->skin_index : 0u),
                  (unsigned int)(p ? p->clothing_index : 0u));
    }
    g_net.palette_conflict = 1;
}

static void ggpo_net_handle_palette_packet(const GgpoNetPalettePacket* p,
                                           const struct sockaddr_in* from) {
    int newly_received = 0;
    if (!p || !from || p->magic != GGPO_NET_MAGIC ||
        p->version != GGPO_NET_VERSION ||
        p->type != GGPO_NET_PACKET_PALETTE ||
        p->sender_player > 1u ||
        (int)p->sender_player != g_net.remote_player) {
        return;
    }
    if (!ggpo_net_accept_packet_source(from,
                                       p->session_id,
                                       "palette",
                                       0) ||
        !ggpo_net_link_confirmed()) {
        return;
    }
    g_net.last_rx_tick = g_net.service_tick;
    g_net.packets_received++;
    if (!g_net.local_palette_valid) {
        ggpo_net_note_palette_conflict("local preference was not configured", p);
        return;
    }
    if (!ggpo_net_palette_tuple_valid(p->palette_count,
                                      p->skin_index,
                                      p->clothing_index) ||
        p->palette_count != g_net.local_palette_count) {
        ggpo_net_note_palette_conflict("entry count or index is out of range", p);
        return;
    }
    if (g_net.remote_palette_valid &&
        (p->palette_count != g_net.remote_palette_count ||
         p->skin_index != g_net.remote_palette_skin ||
         p->clothing_index != g_net.remote_palette_clothing)) {
        ggpo_net_note_palette_conflict("peer changed its fixed preference", p);
        return;
    }
    if (!g_net.remote_palette_valid) {
        g_net.remote_palette_valid = 1;
        g_net.remote_palette_count = p->palette_count;
        g_net.remote_palette_skin = p->skin_index;
        g_net.remote_palette_clothing = p->clothing_index;
        g_net.last_palette_send_tick = 0u;
        newly_received = 1;
        LOG_INFO("ggpo.net: remote palette received player=%u skin=%u clothing=%u entries=%u",
                 (unsigned int)p->sender_player,
                 (unsigned int)p->skin_index,
                 (unsigned int)p->clothing_index,
                 (unsigned int)p->palette_count);
    }
    if (p->ack_valid &&
        p->ack_palette_count == g_net.local_palette_count &&
        p->ack_skin_index == g_net.local_palette_skin &&
        p->ack_clothing_index == g_net.local_palette_clothing) {
        if (!g_net.local_palette_acked) {
            LOG_INFO("ggpo.net: local palette acknowledged by peer");
        }
        g_net.local_palette_acked = 1;
    }
    if (newly_received) {
        /* Schedule an exact echo after socket polling. Sending directly from the
         * receive loop could create an unbounded ping-pong on loopback. */
        g_net.last_palette_send_tick = 0u;
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

static int ggpo_net_store_remote_checksum(
    uint32_t frame,
    uint32_t checksum,
    const LuaGameStateRollbackSummary* summary) {
    GgpoNetRemoteChecksumEntry* entry =
        &g_net.remote_checksums[frame % GGPO_NET_HISTORY_FRAMES];
    if (ggpo_net_frame_before(frame, g_net.remote_checksum_ack_next)) {
        /* Already verified cumulatively. Retained exact entries still detect
         * equivocation; an older generation that has left the ring is inert. */
        if (entry->valid && entry->frame == frame &&
            entry->checksum != checksum) {
            LOG_ERROR("ggpo.net: peer equivocated acknowledged checksum frame=%u first=%u later=%u",
                      (unsigned int)frame,
                      (unsigned int)entry->checksum,
                      (unsigned int)checksum);
            g_net.peer_disconnected = 1;
            return 0;
        }
        return 1;
    }
    if (entry->valid && entry->frame == frame) {
        if (entry->checksum == checksum) {
            if (summary && !entry->has_summary) {
                entry->summary = *summary;
                entry->has_summary = 1;
            }
            return 1;
        }
        LOG_ERROR("ggpo.net: peer equivocated checksum frame=%u first=%u later=%u",
                  (unsigned int)frame,
                  (unsigned int)entry->checksum,
                  (unsigned int)checksum);
        g_net.peer_disconnected = 1;
        return 0;
    }
    if (!ggpo_net_ring_generation_may_replace(entry->valid,
                                               entry->frame,
                                               frame)) {
        return 0;
    }
    entry->valid = 1;
    entry->verified = 0;
    entry->has_summary = summary ? 1 : 0;
    entry->frame = frame;
    entry->checksum = checksum;
    if (summary) entry->summary = *summary;
    return 1;
}

static int ggpo_net_process_remote_checksum(
    uint32_t frame,
    const LuaGameStateRollbackSummary* remote_summary) {
    uint32_t horizon = 0u;
    GgpoNetRemoteChecksumEntry* remote =
        &g_net.remote_checksums[frame % GGPO_NET_HISTORY_FRAMES];
    GgpoNetHistoryEntry* local =
        &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    const LuaGameStateRollbackSummary* effective_summary = remote_summary;

    if (ggpo_net_frame_before(frame, g_net.remote_checksum_ack_next)) return 1;
    if (!remote->valid || remote->frame != frame ||
        !ggpo_net_checksum_horizon(&horizon) ||
        !ggpo_net_frame_at_or_before(frame, horizon)) {
        return 0;
    }
    if (remote->verified) {
        ggpo_net_advance_remote_checksum_ack();
        return 1;
    }
    if (!ggpo_net_history_frame_retained(frame)) {
        LOG_ERROR("ggpo.net: checksum frame=%u outlived rollback history local_frame=%u",
                  (unsigned int)frame,
                  (unsigned int)g_net.frame);
        g_net.peer_disconnected = 1;
        return -1;
    }
    if (!local->valid || local->frame != frame || local->remote_predicted) return 0;
    if (!effective_summary && remote->has_summary) {
        effective_summary = &remote->summary;
    }
    if (local->post_checksum != remote->checksum) {
        ggpo_net_log_desync_summary(frame, local, effective_summary);
        ggpo_net_recoverable_desync(frame,
                                    local->post_checksum,
                                    remote->checksum,
                                    "contiguously confirmed frame checksum mismatch");
        return -1;
    }
    remote->verified = 1;
    ggpo_net_advance_remote_checksum_ack();
    return 1;
}

static void ggpo_net_process_deferred_checksums(const GgpoNetPacket* packet) {
    uint32_t horizon = 0u;
    uint32_t start;
    uint32_t span;
    if (!ggpo_net_checksum_horizon(&horizon)) return;
    /* Oldest-first preserves the earliest observable divergence. Entries that
     * are still above the local finalized horizon remain cached. */
    start = g_net.remote_checksum_ack_next;
    if (ggpo_net_frame_after(start, horizon)) return;
    span = horizon - start;
    if (span >= GGPO_NET_HISTORY_FRAMES) {
        LOG_ERROR("ggpo.net: unverified checksum span exceeded history start=%u horizon=%u",
                  (unsigned int)start,
                  (unsigned int)horizon);
        g_net.peer_disconnected = 1;
        return;
    }
    for (uint32_t i = 0u; i <= span; i++) {
        uint32_t frame = start + i;
        if (ggpo_net_process_remote_checksum(
                frame,
                packet ? ggpo_net_packet_summary_for_frame(packet, frame) : NULL) < 0) {
            break;
        }
    }
}

static uint32_t ggpo_net_packet_effective_input_cmd(const GgpoNetPacket* p,
                                                     uint32_t index) {
    uint32_t cmd = p->inputs[index].cmd;
    if (g_net.prematch_used && !g_net.start_state_loaded &&
        p->inputs[index].frame == 0u) {
        cmd = 0u;
    }
    return cmd;
}

static int ggpo_net_packet_has_checksum_frame(const GgpoNetPacket* p,
                                              uint32_t frame) {
    for (uint32_t i = 0u; i < p->checksum_count; i++) {
        if (p->checksums[i].frame == frame) return 1;
    }
    return 0;
}

static int ggpo_net_validate_input_packet_envelope(const GgpoNetPacket* p) {
    int checksum_epoch_current =
        ggpo_net_checksum_epoch_matches(p) &&
        !g_net.correction_active && !g_net.awaiting_correction;
    if (!p || p->input_count > GGPO_NET_PACKET_INPUTS ||
        p->checksum_count > GGPO_NET_PACKET_CHECKSUMS ||
        p->summary_count > GGPO_NET_PACKET_SUMMARIES ||
        (p->input_ack_flags & ~GGPO_NET_INPUT_ACK_KNOWN_FLAGS) != 0u ||
        (!(p->input_ack_flags & GGPO_NET_INPUT_ACK_VALID) &&
         (p->input_ack_base != 0u || ggpo_net_ack_bit(p->input_ack_bits, 0u))) ||
        ((p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) &&
         !(p->input_ack_flags & GGPO_NET_INPUT_ACK_VALID)) ||
        (!(p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) &&
         (p->confirmed_frame != 0u || p->checksum_count != 0u ||
           p->summary_count != 0u)) ||
        ((p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) &&
         !ggpo_net_frame_at_or_before(p->confirmed_frame,
                                      p->input_ack_base)) ||
        ((p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) &&
         !ggpo_net_frame_before(p->confirmed_frame, p->frame)) ||
        !ggpo_net_validate_peer_input_ack(p->input_ack_flags,
                                          p->input_ack_base,
                                          p->input_ack_bits)) {
        return 0;
    }
    if (checksum_epoch_current &&
        !ggpo_net_validate_peer_checksum_ack(p->checksum_ack_next)) {
        return 0;
    }

    for (uint32_t i = 0u; i < p->input_count; i++) {
        uint32_t frame = p->inputs[i].frame;
        uint32_t cmd = ggpo_net_packet_effective_input_cmd(p, i);
        uint32_t old_cmd = 0u;
        if (ggpo_net_get_input(g_net.remote_inputs, frame, &old_cmd) &&
            old_cmd != cmd) {
            return 0;
        }
        for (uint32_t j = 0u; j < i; j++) {
            if (p->inputs[j].frame == frame &&
                ggpo_net_packet_effective_input_cmd(p, j) != cmd) {
                return 0;
            }
        }
    }
    for (uint32_t i = 0u; i < p->checksum_count; i++) {
        uint32_t frame = p->checksums[i].frame;
        GgpoNetRemoteChecksumEntry* existing =
            &g_net.remote_checksums[frame % GGPO_NET_HISTORY_FRAMES];
        if (!(p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) ||
            !ggpo_net_frame_at_or_before(frame, p->confirmed_frame)) {
            return 0;
        }
        if (checksum_epoch_current &&
            ggpo_net_frame_before(frame, g_net.checksum_stream_start_frame)) {
            return 0;
        }
        if (checksum_epoch_current &&
            existing->valid && existing->frame == frame &&
            existing->checksum != p->checksums[i].checksum) {
            return 0;
        }
        for (uint32_t j = 0u; j < i; j++) {
            if (p->checksums[j].frame == frame &&
                p->checksums[j].checksum != p->checksums[i].checksum) {
                return 0;
            }
        }
    }
    for (uint32_t i = 0u; i < p->summary_count; i++) {
        GgpoNetRemoteChecksumEntry* existing =
            &g_net.remote_checksums[
                p->summaries[i].frame % GGPO_NET_HISTORY_FRAMES];
        if (!(p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) ||
            !ggpo_net_frame_at_or_before(p->summaries[i].frame,
                                         p->confirmed_frame) ||
            !ggpo_net_packet_has_checksum_frame(p, p->summaries[i].frame)) {
            return 0;
        }
        if (checksum_epoch_current && existing->valid &&
            existing->frame == p->summaries[i].frame &&
            existing->has_summary &&
            memcmp(&existing->summary,
                   &p->summaries[i].summary,
                   sizeof(existing->summary)) != 0) {
            return 0;
        }
        for (uint32_t j = 0u; j < i; j++) {
            if (p->summaries[j].frame == p->summaries[i].frame &&
                memcmp(&p->summaries[j].summary,
                       &p->summaries[i].summary,
                       sizeof(p->summaries[i].summary)) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int ggpo_net_validate_correction_envelope(const GgpoNetPacket* p) {
    uint32_t span = 0u;
    if (!p || p->correction_phase > GGPO_NET_CORRECTION_RELEASE_ACK) {
        return 0;
    }
    if (p->correction_phase == GGPO_NET_CORRECTION_NONE) {
        return p->correction_snapshot_frame == 0u &&
               p->correction_resume_frame == 0u &&
               p->correction_request_frame == 0u;
    }
    if (p->checksum_count != 0u || p->summary_count != 0u ||
        (p->input_ack_flags & GGPO_NET_CONFIRMED_FRAME_VALID) != 0u) {
        return 0;
    }
    if (p->correction_phase == GGPO_NET_CORRECTION_REQUEST) {
        return p->correction_snapshot_frame == p->correction_request_frame &&
               p->correction_resume_frame == p->correction_snapshot_frame;
    }
    if (p->correction_id == 0u ||
        p->correction_ack_id != p->correction_id ||
        p->correction_snapshot_frame != p->correction_request_frame ||
        !ggpo_net_correction_span(p->correction_snapshot_frame,
                                  p->correction_resume_frame,
                                  &span)) {
        return 0;
    }
    return span <= GGPO_NET_HISTORY_FRAMES;
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_validate_checksum_envelope(uint32_t input_ack_valid,
                                             uint32_t input_ack_base,
                                             uint32_t confirmed_valid,
                                             uint32_t confirmed_frame,
                                             uint32_t packet_frame) {
    GgpoNetPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = GGPO_NET_PACKET_INPUT;
    packet.frame = packet_frame;
    packet.input_ack_base = input_ack_base;
    packet.confirmed_frame = confirmed_frame;
    if (input_ack_valid) packet.input_ack_flags |= GGPO_NET_INPUT_ACK_VALID;
    if (confirmed_valid) {
        packet.input_ack_flags |= GGPO_NET_CONFIRMED_FRAME_VALID;
    }
    return ggpo_net_validate_input_packet_envelope(&packet);
}

void ggpo_net_test_set_checksum_epoch(uint32_t state_epoch,
                                      uint32_t correction_id,
                                      uint32_t start_frame) {
    g_net.state_epoch = state_epoch;
    g_net.correction_id = correction_id;
    g_net.correction_active = 0;
    g_net.awaiting_correction = 0;
    ggpo_net_reset_checksum_channel(start_frame);
}

void ggpo_net_test_get_checksum_ack_next(uint32_t* out_remote_next,
                                         uint32_t* out_peer_next) {
    if (out_remote_next) *out_remote_next = g_net.remote_checksum_ack_next;
    if (out_peer_next) *out_peer_next = g_net.peer_checksum_ack_next;
}

void ggpo_net_test_set_checksum_ack_next(uint32_t remote_next,
                                         uint32_t peer_next) {
    g_net.remote_checksum_ack_next = remote_next;
    g_net.peer_checksum_ack_next = peer_next;
}

int ggpo_net_test_mark_checksum_published(uint32_t frame) {
    GgpoNetHistoryEntry* h =
        &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!h->valid || h->frame != frame || h->remote_predicted) return 0;
    h->checksum_published = 1;
    return 1;
}

int ggpo_net_test_apply_peer_checksum_ack(uint32_t state_epoch,
                                          uint32_t correction_id,
                                          uint32_t ack_next) {
    if (state_epoch != g_net.state_epoch ||
        correction_id != g_net.correction_id) {
        return 1; /* Delayed prior-generation checksum metadata is inert. */
    }
    return ggpo_net_note_peer_checksum_ack(ack_next);
}

int ggpo_net_test_validate_checksum_ack_envelope(uint32_t state_epoch,
                                                 uint32_t correction_id,
                                                 uint32_t ack_next) {
    GgpoNetPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = GGPO_NET_PACKET_INPUT;
    packet.frame = g_net.frame;
    packet.state_epoch = state_epoch;
    packet.correction_id = correction_id;
    packet.checksum_ack_next = ack_next;
    return ggpo_net_validate_input_packet_envelope(&packet);
}

int ggpo_net_test_receive_remote_checksum(uint32_t frame, uint32_t checksum) {
    if (!ggpo_net_store_remote_checksum(frame, checksum, NULL)) return -1;
    return ggpo_net_process_remote_checksum(frame, NULL);
}

void ggpo_net_test_process_deferred_checksums(void) {
    ggpo_net_process_deferred_checksums(NULL);
}

int ggpo_net_test_checksum_history_retirable(uint32_t frame) {
    GgpoNetHistoryEntry* h =
        &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!h->valid || h->frame != frame) return 0;
    return ggpo_net_history_checksum_retirable(h);
}
#endif

static int ggpo_net_correction_tuple_matches_packet(const GgpoNetPacket* p) {
    return p &&
           p->state_epoch == g_net.state_epoch &&
           p->correction_id == g_net.correction_id &&
           p->correction_ack_id == g_net.correction_id &&
           p->correction_request_frame == g_net.correction_frame &&
           p->correction_snapshot_frame == g_net.correction_frame &&
           p->correction_resume_frame == g_net.correction_resume_frame;
}

static void ggpo_net_correction_protocol_error(const GgpoNetPacket* p,
                                               const char* why) {
    LOG_ERROR("ggpo.net: correction protocol error local_phase=%u peer_phase=%u local_id=%u peer_id=%u local=%u..%u peer=%u..%u (%s)",
              (unsigned int)g_net.correction_phase,
              p ? (unsigned int)p->correction_phase : 0u,
              (unsigned int)g_net.correction_id,
              p ? (unsigned int)p->correction_id : 0u,
              (unsigned int)g_net.correction_frame,
              (unsigned int)g_net.correction_resume_frame,
              p ? (unsigned int)p->correction_snapshot_frame : 0u,
              p ? (unsigned int)p->correction_resume_frame : 0u,
              why ? why : "invalid correction transition");
    g_net.peer_disconnected = 1;
}

static int ggpo_net_finish_host_correction(void) {
    if (!ggpo_net_set_correction_base(g_net.correction_state,
                                      g_net.correction_state_len,
                                      g_net.correction_checksum)) {
        ggpo_net_correction_protocol_error(NULL,
                                           "host correction base was unavailable at release");
        return 0;
    }
    g_net.last_correction_ack_checksum = g_net.correction_transcript;
    g_net.last_correction_ack_id = g_net.correction_id;
    g_net.last_correction_applied_checksum = g_net.correction_transcript;
    g_net.last_correction_applied_id = g_net.correction_id;
    g_net.correction_active = 0;
    g_net.awaiting_correction = 0;
    g_net.correction_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_state_len = 0u;
    g_net.correction_send_offset = 0u;
    g_net.correction_send_next_chunk = 0u;
    g_net.correction_send_chunk_count = 0u;
    g_net.correction_send_base_checksum = 0u;
    g_net.correction_send_delta = 0;
    g_net.correction_wait_start_tick = 0u;
    g_net.correction_progress_tick = 0u;
    LOG_INFO("ggpo.net: mutually released correction id=%u snapshot=%u resume=%u transcript=%u",
             (unsigned int)g_net.correction_id,
             (unsigned int)g_net.correction_frame,
             (unsigned int)g_net.correction_resume_frame,
             (unsigned int)g_net.correction_transcript);
    return 1;
}

static int ggpo_net_finish_join_correction(void) {
    if (!ggpo_net_set_correction_base(g_net.recv_state,
                                      g_net.recv_state_len,
                                      g_net.correction_checksum)) {
        ggpo_net_correction_protocol_error(NULL,
                                           "join correction base was unavailable at release");
        return 0;
    }
    g_net.last_correction_ack_checksum = g_net.correction_transcript;
    g_net.last_correction_ack_id = g_net.correction_id;
    g_net.last_correction_applied_checksum = g_net.correction_transcript;
    g_net.last_correction_applied_id = g_net.correction_id;
    g_net.corrections_received++;
    g_net.correction_active = 0;
    g_net.awaiting_correction = 0;
    g_net.correction_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
    g_net.correction_wait_start_tick = 0u;
    g_net.correction_progress_tick = 0u;
    ggpo_net_reset_recv_state();
    LOG_INFO("ggpo.net: mutually released correction id=%u snapshot=%u resume=%u transcript=%u",
             (unsigned int)g_net.correction_id,
             (unsigned int)g_net.correction_frame,
             (unsigned int)g_net.correction_resume_frame,
             (unsigned int)g_net.correction_transcript);
    return 1;
}

/* Consume the authenticated, repeated correction control tuple. Every state
 * transition is idempotent: older phases are harmless under UDP reordering,
 * while a conflicting tuple in the current generation is terminal. */
static void ggpo_net_handle_correction_control(const GgpoNetPacket* p) {
    uint32_t next_id;
    if (!p || p->state_epoch != g_net.state_epoch) return;

    if (p->correction_phase == GGPO_NET_CORRECTION_NONE) {
        if (g_net.mode == GGPO_NET_MODE_JOIN &&
            g_net.correction_phase == GGPO_NET_CORRECTION_RELEASE_ACK &&
            p->correction_id == g_net.correction_id &&
            p->correction_ack_id == g_net.correction_id &&
            p->correction_ack_checksum == g_net.correction_transcript) {
            (void)ggpo_net_finish_join_correction();
        }
        return;
    }

    if (p->correction_phase == GGPO_NET_CORRECTION_REQUEST) {
        if (g_net.mode != GGPO_NET_MODE_HOST) return;
        if (p->correction_id != g_net.correction_id ||
            p->correction_ack_id != p->correction_id) {
            g_net.stale_correction_requests++;
            return;
        }
        if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE) {
            if (g_net.correction_phase != GGPO_NET_CORRECTION_REQUEST ||
                p->correction_snapshot_frame != g_net.correction_frame) {
                g_net.stale_correction_requests++;
            }
            return;
        }
        if (g_net.last_correction_ack_id != 0u &&
            ggpo_net_frame_at_or_before(p->correction_snapshot_frame,
                                        g_net.correction_frame)) {
            g_net.stale_correction_requests++;
            return;
        }
        g_net.correction_requests++;
        g_net.desync_remote_checksum = p->correction_ack_checksum;
        ggpo_net_begin_host_correction_request(p->correction_snapshot_frame);
        LOG_WARN("ggpo.net: peer requested coordinated correction frame=%u id=%u total=%u",
                 (unsigned int)p->correction_snapshot_frame,
                 (unsigned int)p->correction_id,
                 (unsigned int)g_net.correction_requests);
        if (p->correction_snapshot_frame != g_rng_last_dump_frame) {
            g_rng_last_dump_frame = p->correction_snapshot_frame;
            ggpo_net_dump_rng_ring(p->correction_snapshot_frame);
        }
        return;
    }

    if (p->correction_phase == GGPO_NET_CORRECTION_OFFER) {
        if (g_net.mode != GGPO_NET_MODE_JOIN) return;
        if (p->correction_id == g_net.correction_id &&
            (g_net.correction_phase == GGPO_NET_CORRECTION_RECEIVING ||
             g_net.correction_phase == GGPO_NET_CORRECTION_READY ||
             g_net.correction_phase == GGPO_NET_CORRECTION_APPLIED ||
             g_net.correction_phase == GGPO_NET_CORRECTION_RELEASE_ACK)) {
            if (!ggpo_net_correction_tuple_matches_packet(p) ||
                p->correction_ack_checksum != g_net.correction_checksum) {
                ggpo_net_correction_protocol_error(p,
                                                   "conflicting duplicate correction offer");
            }
            return;
        }
        if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE &&
            g_net.correction_phase != GGPO_NET_CORRECTION_REQUEST) {
            return;
        }
        next_id = ggpo_net_next_correction_id();
        if (next_id == 0u || p->correction_id != next_id) {
            /* A duplicate offer from the just-completed generation is stale;
             * any other jump would fork correction history. */
            if (p->correction_id == g_net.last_correction_applied_id) return;
            ggpo_net_correction_protocol_error(p,
                                               "correction offer skipped a generation");
            return;
        }
        if (g_net.correction_phase == GGPO_NET_CORRECTION_REQUEST &&
            p->correction_snapshot_frame != g_net.correction_request_frame) {
            ggpo_net_correction_protocol_error(p,
                                               "host changed the requested divergence frame");
            return;
        }
        /* An unsolicited host offer can arrive before our checksum detector.
         * Preserve our divergent boundary before correction replay clears it. */
        {
            uint32_t divergence = p->correction_snapshot_frame;
            GgpoNetHistoryEntry* tick =
                &g_net.history[divergence % GGPO_NET_HISTORY_FRAMES];
            GgpoNetRemoteChecksumEntry* remote =
                &g_net.remote_checksums[divergence % GGPO_NET_HISTORY_FRAMES];
            if (tick->valid && tick->frame == divergence) {
                ggpo_net_capture_first_desync_repro(
                    divergence, tick->post_checksum,
                    (remote->valid && remote->frame == divergence)
                        ? remote->checksum : 0u,
                    remote->valid && remote->frame == divergence);
            }
            if (divergence != g_rng_last_dump_frame) {
                g_rng_last_dump_frame = divergence;
                ggpo_net_dump_rng_ring(divergence);
            }
        }
        ggpo_net_reset_recv_state();
        g_net.correction_id = p->correction_id;
        g_net.correction_request_frame = p->correction_snapshot_frame;
        g_net.correction_frame = p->correction_snapshot_frame;
        g_net.correction_resume_frame = p->correction_resume_frame;
        g_net.correction_checksum = p->correction_ack_checksum;
        g_net.correction_input_count = 0u;
        g_net.correction_transcript = 0u;
        g_net.correction_expected_transcript = 0u;
        g_net.correction_snapshot_ready = 0;
        g_net.correction_local_applied = 0;
        g_net.correction_apply_attempts = 0u;
        g_net.correction_active = 0;
        g_net.awaiting_correction = 1;
        g_net.correction_phase = GGPO_NET_CORRECTION_RECEIVING;
        g_net.correction_peer_phase = GGPO_NET_CORRECTION_OFFER;
        g_net.correction_wait_start_tick = ggpo_net_now_tick();
        ggpo_net_note_correction_progress();
        LOG_WARN("ggpo.net: accepted correction offer id=%u snapshot=%u resume=%u checksum=%u",
                 (unsigned int)g_net.correction_id,
                 (unsigned int)g_net.correction_frame,
                 (unsigned int)g_net.correction_resume_frame,
                 (unsigned int)g_net.correction_checksum);
        return;
    }

    if (!ggpo_net_correction_tuple_matches_packet(p)) {
        if (p->correction_id == g_net.last_correction_applied_id ||
            p->correction_id != g_net.correction_id) {
            return;
        }
        ggpo_net_correction_protocol_error(p,
                                           "current correction tuple changed");
        return;
    }

    if (g_net.mode == GGPO_NET_MODE_HOST) {
        switch (p->correction_phase) {
            case GGPO_NET_CORRECTION_RECEIVING:
                if (p->correction_ack_checksum != g_net.correction_checksum) {
                    ggpo_net_correction_protocol_error(p,
                                                       "receiver snapshot checksum changed");
                    return;
                }
                if (g_net.correction_phase == GGPO_NET_CORRECTION_OFFER &&
                    g_net.correction_peer_phase != GGPO_NET_CORRECTION_RECEIVING) {
                    g_net.correction_peer_phase = GGPO_NET_CORRECTION_RECEIVING;
                    ggpo_net_note_correction_progress();
                }
                return;
            case GGPO_NET_CORRECTION_READY:
                if (p->correction_ack_checksum != g_net.correction_checksum) {
                    ggpo_net_correction_protocol_error(p,
                                                       "ready snapshot checksum changed");
                    return;
                }
                if (g_net.correction_phase == GGPO_NET_CORRECTION_OFFER) {
                    if (g_net.correction_peer_phase != GGPO_NET_CORRECTION_READY) {
                        g_net.correction_peer_phase = GGPO_NET_CORRECTION_READY;
                        ggpo_net_note_correction_progress();
                    }
                }
                return;
            case GGPO_NET_CORRECTION_APPLIED:
                if (p->correction_ack_checksum != g_net.correction_transcript) {
                    ggpo_net_correction_protocol_error(p,
                                                       "applied transcript did not match commit");
                    return;
                }
                if (g_net.correction_phase == GGPO_NET_CORRECTION_COMMIT) {
                    g_net.correction_peer_phase = GGPO_NET_CORRECTION_APPLIED;
                    g_net.correction_phase = GGPO_NET_CORRECTION_RELEASE;
                    ggpo_net_note_correction_progress();
                }
                return;
            case GGPO_NET_CORRECTION_RELEASE_ACK:
                if (p->correction_ack_checksum != g_net.correction_transcript) {
                    ggpo_net_correction_protocol_error(p,
                                                       "release acknowledgement transcript changed");
                    return;
                }
                if (g_net.correction_phase == GGPO_NET_CORRECTION_RELEASE) {
                    g_net.correction_peer_phase = GGPO_NET_CORRECTION_RELEASE_ACK;
                    ggpo_net_note_correction_progress();
                    (void)ggpo_net_finish_host_correction();
                }
                return;
            default:
                return;
        }
    }

    if (g_net.mode == GGPO_NET_MODE_JOIN) {
        switch (p->correction_phase) {
            case GGPO_NET_CORRECTION_COMMIT:
                if (g_net.correction_phase == GGPO_NET_CORRECTION_READY) {
                    g_net.correction_expected_transcript =
                        p->correction_ack_checksum;
                    g_net.correction_peer_phase = GGPO_NET_CORRECTION_COMMIT;
                    ggpo_net_note_correction_progress();
                } else if ((g_net.correction_phase == GGPO_NET_CORRECTION_APPLIED ||
                            g_net.correction_phase == GGPO_NET_CORRECTION_RELEASE_ACK) &&
                           p->correction_ack_checksum != g_net.correction_transcript) {
                    ggpo_net_correction_protocol_error(p,
                                                       "duplicate commit transcript changed");
                }
                return;
            case GGPO_NET_CORRECTION_RELEASE:
                if (p->correction_ack_checksum != g_net.correction_transcript) {
                    ggpo_net_correction_protocol_error(p,
                                                       "release transcript did not match applied replay");
                    return;
                }
                if (g_net.correction_phase == GGPO_NET_CORRECTION_APPLIED) {
                    g_net.correction_peer_phase = GGPO_NET_CORRECTION_RELEASE;
                    g_net.correction_phase = GGPO_NET_CORRECTION_RELEASE_ACK;
                    ggpo_net_note_correction_progress();
                }
                return;
            default:
                return;
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
            uint32_t disconnect_phase = g_net.correction_phase;
            uint32_t disconnect_peer_phase = g_net.correction_peer_phase;
            /* The host may process RELEASE_ACK, finalize the barrier, then
             * receive the joiner's immediately adjacent BYE. The authenticated
             * BYE still carries the exact terminal tuple, so preserve that
             * attribution without resurrecting correction-active state. */
            if (g_net.mode == GGPO_NET_MODE_HOST &&
                p->correction_phase == GGPO_NET_CORRECTION_RELEASE_ACK &&
                ggpo_net_correction_tuple_matches_packet(p) &&
                ((disconnect_phase == GGPO_NET_CORRECTION_RELEASE &&
                  p->correction_ack_checksum ==
                      g_net.correction_transcript) ||
                 (disconnect_phase == GGPO_NET_CORRECTION_NONE &&
                  p->correction_id ==
                      g_net.last_correction_applied_id &&
                  p->correction_ack_checksum ==
                      g_net.last_correction_applied_checksum))) {
                if (disconnect_phase == GGPO_NET_CORRECTION_NONE) {
                    disconnect_phase = GGPO_NET_CORRECTION_RELEASE;
                }
                disconnect_peer_phase =
                    GGPO_NET_CORRECTION_RELEASE_ACK;
            }
            g_net.disconnect_correction_phase = disconnect_phase;
            g_net.disconnect_correction_peer_phase =
                disconnect_peer_phase;
            g_net.peer_disconnected = 1;
            LOG_INFO("ggpo.net: peer disconnected correction_phase=%u peer_phase=%u staged=%u/%u correction_chunks=%u auth_rejected=%u remote_contiguous=%s%u peer_acked=%s%u resume=%u",
                     (unsigned int)disconnect_phase,
                     (unsigned int)disconnect_peer_phase,
                     (unsigned int)g_net.recv_state_chunks_complete,
                     (unsigned int)g_net.recv_state_chunk_count,
                     (unsigned int)g_net.correction_chunks_received,
                     (unsigned int)g_net.auth_rejected_packets,
                     g_net.has_remote_contiguous_input_frame ? "" : "none/",
                     (unsigned int)g_net.remote_contiguous_input_frame,
                     g_net.has_peer_acked_local_input_frame ? "" : "none/",
                     (unsigned int)g_net.peer_acked_local_input_frame,
                     (unsigned int)g_net.correction_resume_frame);
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
        uint32_t prior_layout_id = g_net.remote_state_layout_id;
        size_t prior_state_size = g_net.remote_state_size;
        int had_prior_layout = g_net.remote_prematch_hold_known && prior_layout_id != 0u;
        LOG_INFO("ggpo.net: peer prematch hold %s hold_epoch=%u state_epoch=%u",
                 remote_hold ? "engaged" : "released",
                 (unsigned int)p->hold_epoch,
                 (unsigned int)p->state_epoch);
        if (!remote_hold && g_net.prematch_used &&
            (p->state_layout_id == 0u ||
             (had_prior_layout &&
              (prior_layout_id != p->state_layout_id ||
               prior_state_size != (size_t)p->state_size)))) {
            if (!g_net.state_layout_conflict) {
                LOG_ERROR("ggpo.net: peer released with invalid rollback layout prior=%08X/%u release=%08X/%u",
                          (unsigned int)prior_layout_id,
                          (unsigned int)prior_state_size,
                          (unsigned int)p->state_layout_id,
                          (unsigned int)p->state_size);
            }
            g_net.state_layout_conflict = 1;
        }
        g_net.remote_prematch_hold = remote_hold;
        g_net.remote_prematch_hold_known = 1;
        g_net.remote_hold_epoch = p->hold_epoch;
        /* Layout identity is generation-scoped. A newer hold (including the
         * transition into held setup) invalidates any provisional prior-map
         * identity; release packets retain the final nonzero identity. */
        if (remote_hold || p->state_layout_id != 0u) {
            g_net.remote_state_layout_id = p->state_layout_id;
            g_net.remote_state_size = p->state_layout_id ? (size_t)p->state_size : 0u;
        }
    } else if (p->hold_epoch == g_net.remote_hold_epoch &&
               (p->prematch_hold ? 1 : 0) != g_net.remote_prematch_hold) {
        /* Conflicting same-generation flags are malformed. Keep the first
         * authenticated value rather than letting packet order toggle gates. */
        LOG_DEBUG("ggpo.net: ignored conflicting peer hold flag hold_epoch=%u",
                  (unsigned int)p->hold_epoch);
    }
    if (p->hold_epoch == g_net.remote_hold_epoch &&
        (p->prematch_hold ? 1 : 0) == g_net.remote_prematch_hold &&
        p->state_layout_id != 0u) {
        if (g_net.remote_state_layout_id == 0u) {
            g_net.remote_state_layout_id = p->state_layout_id;
            g_net.remote_state_size = (size_t)p->state_size;
            LOG_INFO("ggpo.net: peer finalized rollback layout schema=%08X capacity=%u hold_epoch=%u",
                     (unsigned int)p->state_layout_id,
                     (unsigned int)p->state_size,
                     (unsigned int)p->hold_epoch);
        } else if (g_net.remote_state_layout_id != p->state_layout_id ||
                   g_net.remote_state_size != (size_t)p->state_size) {
            if (!g_net.state_layout_conflict) {
                LOG_ERROR("ggpo.net: conflicting peer rollback layout old=%08X/%u new=%08X/%u hold_epoch=%u",
                          (unsigned int)g_net.remote_state_layout_id,
                          (unsigned int)g_net.remote_state_size,
                          (unsigned int)p->state_layout_id,
                          (unsigned int)p->state_size,
                          (unsigned int)p->hold_epoch);
            }
            g_net.state_layout_conflict = 1;
        }
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
    if (g_net.prematch_used && !ggpo_net_state_layout_ready()) {
        g_net.held_control_packets_dropped++;
        return;
    }

    if (!ggpo_net_validate_correction_envelope(p)) {
        LOG_ERROR("ggpo.net: malformed correction envelope phase=%u id=%u ack_id=%u request=%u snapshot=%u resume=%u",
                  (unsigned int)p->correction_phase,
                  (unsigned int)p->correction_id,
                  (unsigned int)p->correction_ack_id,
                  (unsigned int)p->correction_request_frame,
                  (unsigned int)p->correction_snapshot_frame,
                  (unsigned int)p->correction_resume_frame);
        g_net.peer_disconnected = 1;
        return;
    }
    if (p->type == GGPO_NET_PACKET_STATE_ACK &&
        g_net.mode == GGPO_NET_MODE_HOST &&
        p->state_epoch == g_net.state_epoch &&
        p->state_layout_id == g_net.state_layout_id &&
        p->state_size == (uint32_t)g_net.state_size &&
        p->state_checksum == g_net.initial_checksum) {
        if (!g_net.remote_state_synced) {
            g_net.remote_state_synced = 1;
            LOG_INFO("ggpo.net: remote state sync ack received");
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

    /* INPUT and REQUEST packets share the authenticated input/ACK envelope so
     * the exact-input horizon can finish converging while gameplay is frozen.
     * Other normal packet types may repeat control state, but never populate
     * frame history. */
    if (p->type != GGPO_NET_PACKET_INPUT &&
        p->type != GGPO_NET_PACKET_RESYNC_REQUEST) {
        ggpo_net_handle_correction_control(p);
        return;
    }
    if (p->state_epoch == 0u ||
        p->state_epoch != g_net.state_epoch ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) {
        g_net.held_control_packets_dropped++;
        return;
    }
    if (!ggpo_net_validate_input_packet_envelope(p)) {
        LOG_ERROR("ggpo.net: malformed input ACK/checksum envelope flags=0x%X inputs=%u checksums=%u summaries=%u",
                  (unsigned int)p->input_ack_flags,
                  (unsigned int)p->input_count,
                  (unsigned int)p->checksum_count,
                  (unsigned int)p->summary_count);
        g_net.peer_disconnected = 1;
        return;
    }
    (void)ggpo_net_note_peer_input_ack(p->input_ack_flags,
                                       p->input_ack_base,
                                       p->input_ack_bits);
    if (p->type == GGPO_NET_PACKET_INPUT) {
        (void)ggpo_net_note_remote_frame(p->frame);
        for (uint32_t i = 0; i < p->input_count; i++) {
            uint32_t cmd = ggpo_net_packet_effective_input_cmd(p, i);
            if (!ggpo_net_note_remote_input(p->inputs[i].frame, cmd) &&
                g_net.peer_disconnected) {
                return;
            }
        }
    }
    ggpo_net_handle_correction_control(p);
    if (g_net.peer_disconnected || p->type != GGPO_NET_PACKET_INPUT) return;
    if (!ggpo_net_checksum_epoch_matches(p) ||
        g_net.correction_active || g_net.awaiting_correction ||
        g_net.correction_phase != GGPO_NET_CORRECTION_NONE ||
        p->correction_phase != GGPO_NET_CORRECTION_NONE) {
        return;
    }
    if (!ggpo_net_note_peer_checksum_ack(p->checksum_ack_next)) {
        LOG_ERROR("ggpo.net: checksum ACK changed after envelope validation next=%u current=%u",
                  (unsigned int)p->checksum_ack_next,
                  (unsigned int)g_net.peer_checksum_ack_next);
        g_net.peer_disconnected = 1;
        return;
    }
    /* Store the complete authenticated set before comparing anything. Packet
     * order is live-edge-first; comparison below is deliberately oldest-first
     * so the first observable divergence wins. */
    for (uint32_t i = 0; i < p->checksum_count; i++) {
        uint32_t frame = p->checksums[i].frame;
        uint32_t remote_checksum = p->checksums[i].checksum;
        if (!ggpo_net_store_remote_checksum(
                frame,
                remote_checksum,
                ggpo_net_packet_summary_for_frame(p, frame))) {
            if (g_net.peer_disconnected) break;
            continue;
        }
    }
    if (!g_net.peer_disconnected) ggpo_net_process_deferred_checksums(p);
}

static uint32_t ggpo_net_poll_socket(void) {
    for (uint32_t received = 0u; received < GGPO_NET_RECEIVE_BUDGET; received++) {
        union {
            GgpoNetPacket normal;
            GgpoNetStateChunkPacket state_chunk;
            GgpoNetCosmeticPacket cosmetics;
            GgpoNetCosmeticAssetChunkPacket cosmetic_asset;
            GgpoNetPalettePacket palette;
            uint8_t bytes[GGPO_NET_MAX_PACKET_BYTES];
        } packet;
        const GgpoNetPacketPrefix* prefix = (const GgpoNetPacketPrefix*)packet.bytes;
        struct sockaddr_in from;
        int from_len = sizeof(from);
        int recognized = 0;
        int got = recvfrom(g_net.sock, (char*)packet.bytes, sizeof(packet.bytes), 0, (struct sockaddr*)&from, &from_len);
        if (got == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return received;
            /* Winsock consumes an oversized UDP datagram and may surface an
             * earlier ICMP error here. Neither should hide the next packet. */
            if (e == WSAEMSGSIZE || e == WSAECONNRESET) continue;
            return received;
        }
        if (got < (int)sizeof(GgpoNetPacketPrefix)) continue;
        if (prefix->magic != GGPO_NET_MAGIC || prefix->version != GGPO_NET_VERSION) continue;
        if (prefix->type == GGPO_NET_PACKET_STATE_CHUNK) {
            recognized = got >= (int)offsetof(GgpoNetStateChunkPacket, data);
        } else if (prefix->type == GGPO_NET_PACKET_COSMETICS) {
            recognized = got >= (int)offsetof(GgpoNetCosmeticPacket, profile);
        } else if (prefix->type == GGPO_NET_PACKET_COSMETIC_ASSET_CHUNK) {
            recognized = got >= (int)offsetof(GgpoNetCosmeticAssetChunkPacket, data);
        } else if (prefix->type == GGPO_NET_PACKET_PALETTE) {
            recognized = got == (int)sizeof(GgpoNetPalettePacket);
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
        } else if (prefix->type == GGPO_NET_PACKET_PALETTE) {
            ggpo_net_handle_palette_packet(&packet.palette, &from);
        } else {
            ggpo_net_handle_packet(&packet.normal, &from);
        }
    }
    return GGPO_NET_RECEIVE_BUDGET;
}

#ifdef GGPO_NET_TEST
uint32_t ggpo_net_test_poll_socket(void) {
    return ggpo_net_poll_socket();
}
#endif

static void ggpo_net_send_correction_burst(void) {
    if (g_net.mode != GGPO_NET_MODE_HOST) return;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return;
    if (!ggpo_net_link_confirmed() || !g_net.correction_active ||
        g_net.correction_phase != GGPO_NET_CORRECTION_OFFER) return;
    /* RECEIVING is a real bilateral boundary, not a best-effort diagnostic:
     * do not flood a complete snapshot before the joiner has authenticated
     * the OFFER tuple. This also guarantees the host can distinguish an exit
     * before transfer from an exit during transfer. */
    if (g_net.correction_peer_phase != GGPO_NET_CORRECTION_RECEIVING) {
        return;
    }
    if (g_net.socket_backpressured_this_tick) {
        ggpo_net_saturating_increment(&g_net.socket_send_work_deferred);
        return;
    }
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
    g_net.socket_backpressured_this_tick = 0;
    ggpo_net_poll_socket();
    if (g_net.peer_disconnected) {
        ggpo_net_set_err(err, err_cap, "peer disconnected");
        return 0;
    }
    if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE &&
        g_net.correction_progress_tick != 0u &&
        !ggpo_net_correction_actionable() &&
        g_net.service_tick - g_net.correction_progress_tick >=
            GGPO_NET_CORRECTION_TIMEOUT_TICKS) {
        LOG_ERROR("ggpo.net: correction barrier timed out phase=%u peer_phase=%u id=%u snapshot=%u resume=%u ticks=%u sent=%u received=%u staged=%u/%u sim_pending=%u sim_drops=%u",
                  (unsigned int)g_net.correction_phase,
                  (unsigned int)g_net.correction_peer_phase,
                  (unsigned int)g_net.correction_id,
                  (unsigned int)g_net.correction_frame,
                  (unsigned int)g_net.correction_resume_frame,
                  (unsigned int)(g_net.service_tick -
                                 g_net.correction_progress_tick),
                  (unsigned int)g_net.correction_chunks_sent,
                  (unsigned int)g_net.correction_chunks_received,
                  (unsigned int)g_net.recv_state_chunks_complete,
                  (unsigned int)g_net.recv_state_chunk_count,
                  (unsigned int)ggpo_net_sim_pending_count(),
                  (unsigned int)g_net.sim_queue_drops);
        g_net.peer_disconnected = 1;
        ggpo_net_set_err(err, err_cap, "correction barrier timed out");
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
    if (!g_net.socket_backpressured_this_tick) {
        (void)ggpo_net_send_packet(heartbeat);
    }
    /* Fresh INPUT/ACK/HELLO traffic gets the first physical send opportunity.
     * Delayed datagrams retain their signed bytes on local backpressure; bulk
     * producers then stop for this service tick instead of advancing cursors. */
    ggpo_net_flush_sim_queue();
    if (!g_net.socket_backpressured_this_tick) {
        ggpo_net_send_palette_periodic();
        ggpo_net_send_cosmetic_profile_periodic();
        ggpo_net_send_cosmetic_asset_periodic();
        ggpo_net_send_state_sync_burst();
        ggpo_net_send_correction_burst();
    } else if ((!g_net.remote_state_synced && g_net.mode == GGPO_NET_MODE_HOST) ||
               (g_net.correction_active &&
                g_net.correction_phase == GGPO_NET_CORRECTION_OFFER)) {
        ggpo_net_saturating_increment(&g_net.socket_send_work_deferred);
    }
    return 1;
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_service_input(char* err, size_t err_cap) {
    return ggpo_net_service_transport(GGPO_NET_PACKET_INPUT, err, err_cap);
}
#endif

static uint32_t ggpo_net_oldest_missing_remote_input(uint32_t frame,
                                                      uint32_t max_prediction) {
    uint32_t tmp = 0;
    uint32_t oldest_missing = frame;
    uint32_t lookback = (g_net.frame_counter_wrapped || frame >= max_prediction)
        ? max_prediction
        : frame;
    uint32_t start = frame - lookback;

    for (uint32_t offset = 0u; offset <= lookback; offset++) {
        uint32_t candidate = start + offset;
        if (!ggpo_net_get_input(g_net.remote_inputs, candidate, &tmp)) {
            oldest_missing = candidate;
            break;
        }
    }
    return oldest_missing;
}

static int ggpo_net_prediction_stall_needed(uint32_t frame, uint32_t* out_oldest_missing) {
    uint32_t max_prediction = g_net.max_prediction ? g_net.max_prediction : GGPO_NET_DEFAULT_MAX_PREDICTION;
    uint32_t oldest_missing;

    oldest_missing = ggpo_net_oldest_missing_remote_input(frame, max_prediction);
    if (out_oldest_missing) *out_oldest_missing = oldest_missing;
    return (frame - oldest_missing) >= max_prediction;
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_prediction_stall_needed(uint32_t frame, uint32_t* out_oldest_missing) {
    return ggpo_net_prediction_stall_needed(frame, out_oldest_missing);
}
#endif

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

/* Return 1 when the modulo history slot may be used, 0 for a recoverable
 * checksum-delivery stall, and -1 after a bounded no-progress timeout. The
 * caller has already serviced and sent transport for this tick, so a stalled
 * peer continues retransmitting checksums and cumulative ACKs. */
static int ggpo_net_checksum_retirement_gate(uint32_t frame,
                                             char* err,
                                             size_t err_cap) {
    GgpoNetHistoryEntry* h =
        &g_net.history[frame % GGPO_NET_HISTORY_FRAMES];
    if (!h->valid || h->frame == frame ||
        !ggpo_net_frame_after(frame, h->frame) ||
        ggpo_net_history_checksum_retirable(h)) {
        ggpo_net_note_checksum_progress();
        return 1;
    }

    if (g_net.checksum_wait_start_tick == 0u ||
        g_net.checksum_wait_frame != h->frame) {
        g_net.checksum_wait_start_tick = ggpo_net_now_tick();
        g_net.checksum_wait_frame = h->frame;
        g_net.checksum_wait_announced = 0;
    }
    g_net.checksum_stalls++;
    if (!g_net.checksum_wait_announced) {
        LOG_WARN("ggpo.net: hard checksum recovery stall frame=%u blocked=%u local_rx_next=%u peer_rx_next=%u",
                 (unsigned int)frame,
                 (unsigned int)h->frame,
                 (unsigned int)g_net.remote_checksum_ack_next,
                 (unsigned int)g_net.peer_checksum_ack_next);
        g_net.checksum_wait_announced = 1;
    }
    if (g_net.service_tick - g_net.checksum_wait_start_tick >=
        GGPO_NET_CHECKSUM_STALL_TIMEOUT_TICKS) {
        LOG_ERROR("ggpo.net: checksum recovery timeout blocked=%u local_rx_next=%u peer_rx_next=%u ticks=%u",
                  (unsigned int)h->frame,
                  (unsigned int)g_net.remote_checksum_ack_next,
                  (unsigned int)g_net.peer_checksum_ack_next,
                  (unsigned int)(g_net.service_tick -
                                 g_net.checksum_wait_start_tick));
        g_net.peer_disconnected = 1;
        ggpo_net_set_err(err, err_cap,
                         "checksum delivery did not recover before history boundary");
        return -1;
    }
    return 0;
}

#ifdef GGPO_NET_TEST
int ggpo_net_test_checksum_retirement_gate(uint32_t frame,
                                           uint32_t service_tick) {
    char err[128];
    err[0] = '\0';
    g_net.service_tick = service_tick;
    return ggpo_net_checksum_retirement_gate(frame, err, sizeof(err));
}
#endif

/* One canonical frame-boundary capture is both the rollback state for `frame`
 * and the checksum/component source for `frame - 1`. A live/replayed tick
 * force-captures its post-state into the next boundary; the next tick then
 * reuses that exact blob instead of recapturing mutable live state. */
static int ggpo_net_frame_boundary(
    uint32_t frame,
    int force_capture,
    GgpoNetHistoryEntry** out_history,
    uint8_t** out_blob,
    LuaGameStateRollbackSummary* out_summary,
    int* out_has_summary,
    char* err,
    size_t err_cap) {
    GgpoNetHistoryEntry* h = NULL;
    uint8_t* blob = NULL;
    size_t state_len = 0;
    uint32_t checksum = 0;
    uint32_t confirmed_horizon = 0u;
    LuaGameStateRollbackSummary summary;
    int want_summary = (out_summary && g_net_config_rng_trace) ? 1 : 0;

    if (out_has_summary) *out_has_summary = 0;

    h = ggpo_net_history_slot(frame, &blob);
    if (!force_capture && h->valid && h->frame == frame &&
        h->state_len > 0u && h->state_len <= g_net.state_size) {
        if (out_history) *out_history = h;
        if (out_blob) *out_blob = blob;
        return 1;
    }
    if (!ggpo_net_ring_generation_may_replace(h->valid, h->frame, frame)) {
        ggpo_net_set_err(err, err_cap, "stale frame would overwrite newer rollback history");
        return 0;
    }
    if (h->valid && h->frame != frame &&
        ggpo_net_frame_after(frame, h->frame) &&
        (!ggpo_net_checksum_horizon(&confirmed_horizon) ||
         !ggpo_net_frame_at_or_before(h->frame, confirmed_horizon))) {
        LOG_ERROR("ggpo.net: refusing to retire unconfirmed rollback history frame=%u for generation=%u",
                  (unsigned int)h->frame,
                  (unsigned int)frame);
        ggpo_net_set_err(err, err_cap,
                         "unconfirmed rollback history reached the retention boundary");
        return 0;
    }
    if (h->valid && h->frame != frame &&
        ggpo_net_frame_after(frame, h->frame) &&
        !ggpo_net_history_checksum_retirable(h)) {
        LOG_ERROR("ggpo.net: refusing to retire checksum-unverified history frame=%u for generation=%u local_rx_next=%u peer_rx_next=%u",
                  (unsigned int)h->frame,
                  (unsigned int)frame,
                  (unsigned int)g_net.remote_checksum_ack_next,
                  (unsigned int)g_net.peer_checksum_ack_next);
        ggpo_net_set_err(err, err_cap,
                         "unverified checksum history reached the retention boundary");
        return 0;
    }
    if (!lua_manager_game_state_save(
            blob, g_net.state_size, &state_len, err, err_cap) ||
        !lua_manager_game_state_canonicalize_rollback(
            blob, state_len, err, err_cap) ||
        !lua_manager_game_state_analyze_canonical_rollback_blob(
            blob,
            state_len,
            &checksum,
            want_summary ? &summary : NULL,
            err,
            err_cap)) {
        /* The target blob may have been partially overwritten. Never leave its
         * old metadata pointing at bytes that are no longer that generation. */
        memset(h, 0, sizeof(*h));
        return 0;
    }
    memset(h, 0, sizeof(*h));
    h->valid = 1;
    h->frame = frame;
    h->state_len = state_len;
    h->pre_checksum = checksum;
    if (want_summary) {
        if (out_summary) *out_summary = summary;
        if (out_has_summary) *out_has_summary = 1;
    }
    if (out_history) *out_history = h;
    if (out_blob) *out_blob = blob;
    return 1;
}

static int ggpo_net_save_pre_state(
    uint32_t frame,
    GgpoNetHistoryEntry** out_history,
    uint8_t** out_blob,
    char* err,
    size_t err_cap) {
    return ggpo_net_frame_boundary(
        frame, 0, out_history, out_blob, NULL, NULL, err, err_cap);
}

static int ggpo_net_capture_post_state(
    uint32_t frame,
    GgpoNetHistoryEntry** out_history,
    uint8_t** out_blob,
    LuaGameStateRollbackSummary* out_summary,
    int* out_has_summary,
    char* err,
    size_t err_cap) {
    return ggpo_net_frame_boundary(
        frame, 1, out_history, out_blob,
        out_summary, out_has_summary, err, err_cap);
}

static int ggpo_net_seed_frame_boundary(
    uint32_t frame,
    const void* state,
    size_t state_len,
    uint32_t checksum,
    char* err,
    size_t err_cap) {
    GgpoNetHistoryEntry* h;
    uint8_t* blob = NULL;
    if (!state || state_len == 0u || state_len > g_net.state_size) {
        ggpo_net_set_err(err, err_cap, "invalid cached frame boundary");
        return 0;
    }
    h = ggpo_net_history_slot(frame, &blob);
    if (!h || !blob) {
        ggpo_net_set_err(err, err_cap, "frame boundary storage unavailable");
        return 0;
    }
    memmove(blob, state, state_len);
    memset(h, 0, sizeof(*h));
    h->valid = 1;
    h->frame = frame;
    h->state_len = state_len;
    h->pre_checksum = checksum;
    return 1;
}

#ifdef GGPO_NET_TEST
/* Rebase an already synchronized test session close to uint32 wrap without
 * changing its canonical gameplay state. Advancing the state epoch quarantines
 * any frame-zero INPUT packets still queued in either localhost socket; both
 * paired test processes invoke this before resuming simulation. Production
 * sessions never rebase and reach the same serial boundary naturally. */
int ggpo_net_test_rebase_active_frame(uint32_t start_frame,
                                      char* err,
                                      size_t err_cap) {
    GgpoNetHistoryEntry* source_history = NULL;
    uint8_t* source_blob = NULL;
    size_t state_len;
    uint32_t checksum;
    uint32_t prediction_prehistory;
    int had_clean_sim_state;

    if (!g_net.active || !g_net.start_state_loaded ||
        !g_net.state_blobs || !g_net.apply_backup_state ||
        g_net.state_size == 0u ||
        g_net.correction_phase != GGPO_NET_CORRECTION_NONE ||
        g_net.state_epoch == UINT32_MAX) {
        ggpo_net_set_err(err, err_cap,
                         "active synchronized state is unavailable for frame rebase");
        return 0;
    }
    source_history = ggpo_net_history_slot(g_net.frame, &source_blob);
    if (!source_history || !source_blob || !source_history->valid ||
        source_history->frame != g_net.frame ||
        source_history->state_len == 0u ||
        source_history->state_len > g_net.state_size) {
        ggpo_net_set_err(err, err_cap,
                         "current canonical frame boundary is unavailable for rebase");
        return 0;
    }
    state_len = source_history->state_len;
    checksum = source_history->pre_checksum;
    memcpy(g_net.apply_backup_state, source_blob, state_len);
    had_clean_sim_state = g_net.have_clean_sim_state;

    g_net.state_epoch++;
    ggpo_net_clear_runtime_history();
    g_net.frame = start_frame;
    g_net.frame_counter_wrapped = 0;
    ggpo_net_reset_checksum_channel(start_frame);
    memset(g_net.sim_queue, 0, sizeof(g_net.sim_queue));
    g_net.have_clean_sim_state = had_clean_sim_state;
    g_net.desync_detected = 0;
    g_net.last_checksum = checksum;

    /* A naturally long-running session already owns an exact neutral/real
     * input history behind this boundary and cumulative ACKs through the
     * preceding frame. Recreate only the neutral prefix required by the
     * prediction window; the tested 2,048-frame interval itself still arrives
     * exclusively through the authenticated lossy socket path. */
    prediction_prehistory = g_net.max_prediction
        ? g_net.max_prediction
        : GGPO_NET_DEFAULT_MAX_PREDICTION;
    for (uint32_t age = prediction_prehistory; age > 0u; age--) {
        if (!ggpo_net_store_input(g_net.remote_inputs,
                                  start_frame - age,
                                  0u)) {
            ggpo_net_set_err(err,
                             err_cap,
                             "failed to seed neutral prediction prehistory for frame rebase");
            return 0;
        }
    }
    g_net.remote_contiguous_input_frame = start_frame - 1u;
    g_net.has_remote_contiguous_input_frame = 1;
    g_net.peer_acked_local_input_frame = start_frame - 1u;
    g_net.has_peer_acked_local_input_frame = 1;
    g_net.has_peer_input_ack = 1;
    g_net.last_remote_cmd = 0u;
    g_net.last_remote_cmd_frame = start_frame - 1u;
    g_net.has_last_remote_cmd = 1;

    if (!ggpo_net_seed_frame_boundary(start_frame,
                                      g_net.apply_backup_state,
                                      state_len,
                                      checksum,
                                      err,
                                      err_cap) ||
        !ggpo_net_seed_local_input_delay_from(start_frame, err, err_cap)) {
        return 0;
    }
    return 1;
}
#endif

/* A native tick may fail only after mutating gameplay (for example, if its
 * post-tick checksum or clean-snapshot read fails). The caller is going to
 * terminate the online session, but leaving the live game one tick ahead of the
 * unchanged frame counter would also leak a partial online tick into the hub.
 * Restore the exact canonical pre-state before reporting the fatal error. */
static int ggpo_net_abort_live_tick_and_restore(GgpoNetHistoryEntry* history,
                                                const uint8_t* pre_state,
                                                char* err,
                                                size_t err_cap) {
    char cause[192];
    char restore_err[192];

    snprintf(cause,
             sizeof(cause),
             "%s",
             (err && err[0]) ? err : "native frame advance failed");
    restore_err[0] = '\0';
    if (!history || !history->valid || history->frame != g_net.frame ||
        !pre_state || history->state_len == 0u ||
        !ggpo_ext_load_game_state(pre_state,
                                  history->state_len,
                                  restore_err,
                                  sizeof(restore_err)) ||
        !ggpo_net_restore_clean_sim_state(restore_err,
                                          sizeof(restore_err)) ||
        !ggpo_net_restore_local_render_geometry(restore_err,
                                                sizeof(restore_err))) {
        LOG_ERROR("ggpo.net: fatal live-tick restore failure frame=%u cause=%s restore=%s",
                  (unsigned int)g_net.frame,
                  cause,
                  restore_err[0] ? restore_err : "pre-state unavailable");
        g_net.peer_disconnected = 1;
        ggpo_net_set_err(err,
                         err_cap,
                         "native tick failed and its rollback pre-state could not be restored");
        return 0;
    }

    LOG_ERROR("ggpo.net: native tick failed; restored frame=%u pre-state before abort (%s)",
              (unsigned int)g_net.frame,
              cause);
    ggpo_net_set_err(err, err_cap, cause);
    return 0;
}

static int ggpo_net_replay_frame(uint32_t frame, int arg0, int suppress_audio, uint32_t* out_checksum, char* err, size_t err_cap) {
    GgpoFrameInputs inputs;
    int predicted = 0;
    int ok = 0;

    memset(&inputs, 0, sizeof(inputs));
    ggpo_net_build_inputs(frame, &inputs, &predicted);
    if (suppress_audio) {
        int old_synth_enabled = hooks_set_native_synth_enabled(0);
        ok = ggpo_ext_advance_frame(&inputs, arg0, out_checksum, err, err_cap);
        hooks_set_native_synth_enabled(old_synth_enabled);
    } else {
        ok = ggpo_ext_advance_frame(&inputs, arg0, out_checksum, err, err_cap);
    }
    if (!ok) return 0;
    return 1;
}

static int ggpo_net_load_start_state_if_ready(char* err, size_t err_cap) {
    uint32_t checksum = 0;
    uint32_t attempt;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 1;
    if (!ggpo_net_state_layout_ready() || ggpo_net_state_layout_mismatch()) return 1;
    if (!ggpo_net_link_confirmed() || !g_net.state_synced || !g_net.remote_state_synced) return 1;
    if (g_net.start_state_loaded) return 1;
    if (!g_net.initial_state || g_net.initial_state_len == 0) {
        ggpo_net_set_err(err, err_cap, "missing synced start state");
        return 0;
    }

    /* Finish every fallible input-ring operation before committing the synced
     * live state. Both operations are idempotent, so an exact-restored retry
     * below cannot duplicate or overwrite an already committed input. */
    ggpo_net_apply_auto_input_delay();
    if (!ggpo_net_seed_local_input_delay(err, err_cap)) return 0;

    for (attempt = 0u; attempt < GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS; attempt++) {
        int restored_failure = 0;
        if (ggpo_net_apply_received_state_transaction(g_net.initial_state,
                                                      g_net.initial_state_len,
                                                      g_net.initial_checksum,
                                                      &checksum,
                                                      &restored_failure,
                                                      err,
                                                      err_cap)) {
            break;
        }
        /* Retry only when the transaction proved that the exact pre-attempt
         * serializer image was restored. Deterministic preflight failures and
         * fatal restore failures remain immediate setup errors. */
        if (!restored_failure || g_net.peer_disconnected ||
            attempt + 1u >= GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS) {
            return 0;
        }
        LOG_WARN("ggpo.net: retrying restored prematch start-state load attempt=%u/%u (%s)",
                 (unsigned int)(attempt + 1u),
                 (unsigned int)GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS,
                 (err && err[0]) ? err : "transient rollback load failure");
    }
    memcpy(g_net.state_blobs, g_net.initial_state, g_net.initial_state_len);
    /* INPUT packets are accepted only after the final authoritative state epoch
     * has been applied and both holds are released. A peer may prepare first and
     * send its neutral delay prefix (or even a few real inputs) while this side
     * is still on the last hub tick. Those exact inputs and the ACKs already sent
     * for them belong to this same epoch. Erasing them here would make our ACK
     * regress while the prepared peer correctly keeps its cumulative horizon,
     * leaving an input it will never retransmit. Reset only provisional rollback
     * and checksum history; preserve both input rings and monotonic ACK state. */
    ggpo_net_clear_rollback_history();
    if (!ggpo_net_seed_frame_boundary(
            0u,
            g_net.state_blobs,
            g_net.initial_state_len,
            checksum,
            err,
            err_cap)) {
        return 0;
    }
    ggpo_net_reset_checksum_channel(0u);
    g_net.frame_counter_wrapped = 0;
    g_net.have_clean_sim_state = 0;
    g_net.frame = 0;
    /* The exact synced start state pins frame zero's RNG, camera, and logical
     * simulation dimensions. Keep those in the clean snapshot, then put the
     * local logical dimensions back for rendering until the first real tick. */
    if (!ggpo_net_capture_clean_sim_state(err, err_cap)) {
        (void)ggpo_net_restore_local_render_geometry(NULL, 0u);
        return 0;
    }
    if (!ggpo_net_restore_local_render_geometry(err, err_cap)) return 0;
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
    uint32_t replay_frames;
    LuaGamePaletteState local_palette;

    if (!g_net.rollback_pending) return 1;
    start = g_net.rollback_to;
    end = g_net.frame;
    replay_frames = end - start;
    if (!ggpo_net_frame_before(start, end) ||
        replay_frames >= GGPO_NET_HISTORY_FRAMES) {
        g_net.rollback_pending = 0;
        g_net.dropped_inputs++;
        return 1;
    }
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
    if (!lua_manager_game_palette_capture(&local_palette)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to capture local room palette before rollback");
        rb_ok = 0;
    } else if (!ggpo_ext_load_game_state(blob, h->state_len, err, err_cap)) {
        rb_ok = 0;
    } else if (!lua_manager_game_palette_restore(&local_palette)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to restore local room palette after rollback load");
        rb_ok = 0;
    } else if (!ggpo_net_capture_clean_sim_state(err, err_cap)) {
        rb_ok = 0;
    } else {
        for (uint32_t replay_index = 0u; replay_index < replay_frames; replay_index++) {
            uint32_t f = start + replay_index;
            GgpoNetHistoryEntry* rh = NULL;
            int predicted = 0;
            GgpoFrameInputs inputs;
            uint32_t local_cmd = 0;
            uint32_t remote_cmd = 0;
            GgpoNetHistoryEntry* next_h = NULL;
            LuaGameStateRollbackSummary post_summary;
            int post_has_summary = 0;

            if (!ggpo_net_restore_clean_sim_state(err, err_cap) ||
                !ggpo_net_save_pre_state(f, &rh, NULL, err, err_cap)) {
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
                if (!ggpo_net_replay_frame(f, arg0, 1, NULL, err, err_cap)) {
                    if (trace) hooks_rng_trace_end();
                    rb_ok = 0;
                    break;
                }
                if (!ggpo_net_capture_clean_sim_state(err, err_cap)) {
                    if (trace) hooks_rng_trace_end();
                    rb_ok = 0;
                    break;
                }
                if (!ggpo_net_capture_post_state(
                        f + 1u,
                        &next_h,
                        NULL,
                        &post_summary,
                        &post_has_summary,
                        err,
                        err_cap)) {
                    if (trace) hooks_rng_trace_end();
                    rb_ok = 0;
                    break;
                }
                checksum = next_h->pre_checksum;
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
            rh->has_summary = post_has_summary;
            if (post_has_summary) rh->summary = post_summary;
        }
    }

    hooks_waterfall_audio_restore();

    if (!rb_ok) return 0;

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

static uint32_t ggpo_net_correction_transcript_begin(void) {
    uint32_t h = 2166136261u;
    h = ggpo_net_hash_mix_u32(h, 0x434F5252u); /* "CORR" */
    h = ggpo_net_hash_mix_u32(h, g_net.state_epoch);
    h = ggpo_net_hash_mix_u32(h, g_net.correction_id);
    h = ggpo_net_hash_mix_u32(h, g_net.correction_frame);
    h = ggpo_net_hash_mix_u32(h, g_net.correction_resume_frame);
    h = ggpo_net_hash_mix_u32(h, g_net.correction_checksum);
    h = ggpo_net_hash_mix_u32(h, g_net.correction_input_count);
    return h;
}

static int ggpo_net_apply_correction_and_replay(const void* candidate,
                                                 size_t candidate_len,
                                                 int arg0,
                                                 uint32_t* out_transcript,
                                                 char* err,
                                                 size_t err_cap) {
    uint32_t span = 0u;
    uint32_t loaded_checksum = 0u;
    uint32_t checksum = 0u;
    uint32_t transcript;
    uint32_t attempt;
    int loaded = 0;
    int old_synth_enabled;
    LuaGamePaletteState local_palette;

    if (!ggpo_net_correction_span(g_net.correction_frame,
                                  g_net.correction_resume_frame,
                                  &span) ||
        span != g_net.correction_input_count ||
        !candidate || candidate_len == 0u) {
        ggpo_net_set_err(err, err_cap,
                         "correction replay did not have an exact pinned input span");
        return 0;
    }
    if (!lua_manager_game_palette_capture(&local_palette)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to capture local room palette before correction");
        return 0;
    }

    for (attempt = 0u; attempt < GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS; attempt++) {
        int restored_failure = 0;
        g_net.correction_apply_attempts++;
        if (ggpo_net_apply_received_state_transaction(candidate,
                                                      candidate_len,
                                                      g_net.correction_checksum,
                                                      &loaded_checksum,
                                                      &restored_failure,
                                                      err,
                                                      err_cap)) {
            loaded = 1;
            break;
        }
        if (!restored_failure || g_net.peer_disconnected ||
            attempt + 1u >= GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS) {
            break;
        }
        LOG_WARN("ggpo.net: retrying restored correction snapshot load attempt=%u/%u id=%u (%s)",
                 (unsigned int)(attempt + 1u),
                 (unsigned int)GGPO_NET_PREMATCH_LOAD_MAX_ATTEMPTS,
                 (unsigned int)g_net.correction_id,
                 (err && err[0]) ? err : "transient rollback load failure");
    }
    /* Received-state application intentionally scrubs the checksum-excluded
     * palette controls. Restore the coherent peer-local presentation sidecar
     * after the complete transaction, including a transaction that restored
     * its gameplay backup on failure. */
    if (!lua_manager_game_palette_restore(&local_palette)) {
        ggpo_net_set_err(err, err_cap,
                         "failed to restore local room palette after correction");
        return 0;
    }
    if (!loaded || loaded_checksum != g_net.correction_checksum) {
        if (!err || !err[0]) {
            ggpo_net_set_err(err, err_cap, "correction snapshot transaction failed");
        }
        return 0;
    }
    if (!ggpo_net_capture_clean_sim_state(err, err_cap)) return 0;

    hooks_waterfall_audio_save();
    old_synth_enabled = hooks_set_native_synth_enabled(0);
    memset(g_net.history, 0, sizeof(g_net.history));
    g_net.rollback_to = 0u;
    g_net.rollback_pending = 0;
    g_net.desync_detected = 0;
    g_net.frame = g_net.correction_frame;
    if (!ggpo_net_seed_frame_boundary(
            g_net.correction_frame,
            candidate,
            candidate_len,
            loaded_checksum,
            err,
            err_cap)) {
        hooks_set_native_synth_enabled(old_synth_enabled);
        hooks_waterfall_audio_restore();
        return 0;
    }
    transcript = ggpo_net_correction_transcript_begin();

    for (uint32_t i = 0u; i < span; i++) {
        uint32_t frame = g_net.correction_frame + i;
        uint32_t p0 = g_net.correction_inputs[i][0];
        uint32_t p1 = g_net.correction_inputs[i][1];
        GgpoNetHistoryEntry* h = NULL;
        GgpoFrameInputs inputs;
        int trace = g_net_config_rng_trace;
        uint32_t seed_before = 0u;
        GgpoNetHistoryEntry* next_h = NULL;
        LuaGameStateRollbackSummary post_summary;
        int post_has_summary = 0;

        if (!ggpo_net_restore_clean_sim_state(err, err_cap) ||
            !ggpo_net_save_pre_state(frame, &h, NULL, err, err_cap)) {
            hooks_set_native_synth_enabled(old_synth_enabled);
            hooks_waterfall_audio_restore();
            return 0;
        }
        memset(&inputs, 0, sizeof(inputs));
        inputs.player_cmd[0] = p0;
        inputs.player_cmd[1] = p1;
        if (trace) {
            (void)lua_manager_game_rng_seed(&seed_before);
            hooks_rng_trace_begin(frame, 0u);
        }
        if (!ggpo_ext_advance_frame(&inputs, arg0, NULL, err, err_cap)) {
            if (trace) hooks_rng_trace_end();
            hooks_set_native_synth_enabled(old_synth_enabled);
            hooks_waterfall_audio_restore();
            return 0;
        }
        if (!ggpo_net_capture_clean_sim_state(err, err_cap)) {
            if (trace) hooks_rng_trace_end();
            hooks_set_native_synth_enabled(old_synth_enabled);
            hooks_waterfall_audio_restore();
            return 0;
        }
        if (!ggpo_net_capture_post_state(
                frame + 1u,
                &next_h,
                NULL,
                &post_summary,
                &post_has_summary,
                err,
                err_cap)) {
            if (trace) hooks_rng_trace_end();
            hooks_set_native_synth_enabled(old_synth_enabled);
            hooks_waterfall_audio_restore();
            return 0;
        }
        checksum = next_h->pre_checksum;
        if (trace) {
            HooksRngTrace tr;
            uint32_t seed_after = 0u;
            hooks_rng_trace_copy(&tr);
            hooks_rng_trace_end();
            (void)lua_manager_game_rng_seed(&seed_after);
            ggpo_net_record_rng_frame(frame,
                                      g_net.correction_inputs[i][g_net.local_player],
                                      g_net.correction_inputs[i][g_net.remote_player],
                                      seed_before,
                                      seed_after,
                                      &tr);
        }
        h->local_cmd = g_net.correction_inputs[i][g_net.local_player];
        h->remote_cmd = g_net.correction_inputs[i][g_net.remote_player];
        h->remote_predicted = 0;
        h->post_checksum = checksum;
        h->has_summary = post_has_summary;
        if (post_has_summary) h->summary = post_summary;
        transcript = ggpo_net_hash_mix_u32(transcript, frame);
        transcript = ggpo_net_hash_mix_u32(transcript, p0);
        transcript = ggpo_net_hash_mix_u32(transcript, p1);
        transcript = ggpo_net_hash_mix_u32(transcript, h->pre_checksum);
        transcript = ggpo_net_hash_mix_u32(transcript, h->post_checksum);
        g_net.frame = frame + 1u;
    }

    hooks_set_native_synth_enabled(old_synth_enabled);
    hooks_waterfall_audio_restore();
    g_net.frame = g_net.correction_resume_frame;
    g_net.last_checksum = checksum;
    g_net.last_remote_cmd =
        g_net.correction_inputs[span - 1u][g_net.remote_player];
    g_net.last_remote_cmd_frame = g_net.correction_resume_frame - 1u;
    g_net.has_last_remote_cmd = 1;
    /* Only after the mutually agreed replay has rebuilt D..H may either peer
     * retire the prior checksum generation. Inputs remain intact. */
    ggpo_net_reset_checksum_channel(g_net.correction_frame);
    g_net.correction_local_applied = 1;
    if (out_transcript) *out_transcript = transcript;
    return 1;
}

static int ggpo_net_progress_correction(int arg0, char* err, size_t err_cap) {
    uint32_t transcript = 0u;
    if (g_net.correction_phase == GGPO_NET_CORRECTION_NONE) return 1;
    if (g_net.correction_progress_tick != 0u &&
        !ggpo_net_correction_actionable() &&
        g_net.service_tick - g_net.correction_progress_tick >=
            GGPO_NET_CORRECTION_TIMEOUT_TICKS) {
        g_net.peer_disconnected = 1;
        ggpo_net_set_err(err, err_cap, "correction barrier timed out");
        return 0;
    }

    if (g_net.mode == GGPO_NET_MODE_JOIN &&
        g_net.correction_phase == GGPO_NET_CORRECTION_REQUEST) {
        (void)ggpo_net_request_host_correction("awaiting coordinated correction", 0);
        return 1;
    }

    if (g_net.mode == GGPO_NET_MODE_HOST &&
        g_net.correction_phase == GGPO_NET_CORRECTION_REQUEST) {
        if (!ggpo_net_apply_rollback_if_needed(arg0, err, err_cap)) return 0;
        if (!ggpo_net_prepare_host_correction("coordinated correction request")) {
            if (g_net.peer_disconnected) {
                if (!err || !err[0]) {
                    ggpo_net_set_err(err, err_cap,
                                     "host could not retain the correction boundary");
                }
                return 0;
            }
            return 1;
        }
        (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
        return 1;
    }

    if (g_net.mode == GGPO_NET_MODE_JOIN &&
        g_net.correction_phase == GGPO_NET_CORRECTION_RECEIVING &&
        g_net.correction_snapshot_ready) {
        if (!ggpo_net_pin_correction_inputs(g_net.correction_frame,
                                            g_net.correction_resume_frame)) {
            return 1;
        }
        g_net.correction_phase = GGPO_NET_CORRECTION_READY;
        ggpo_net_note_correction_progress();
        (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
        return 1;
    }

    if (g_net.mode == GGPO_NET_MODE_HOST &&
        g_net.correction_phase == GGPO_NET_CORRECTION_OFFER &&
        g_net.correction_peer_phase == GGPO_NET_CORRECTION_READY) {
        if (!ggpo_net_seed_local_input_delay_from(g_net.correction_resume_frame,
                                                  err,
                                                  err_cap) ||
            !ggpo_net_apply_correction_and_replay(g_net.correction_state,
                                                  g_net.correction_state_len,
                                                  arg0,
                                                  &transcript,
                                                  err,
                                                  err_cap)) {
            g_net.peer_disconnected = 1;
            return 0;
        }
        g_net.correction_transcript = transcript;
        g_net.correction_phase = GGPO_NET_CORRECTION_COMMIT;
        ggpo_net_note_correction_progress();
        (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
        LOG_INFO("ggpo.net: correction replay committed id=%u snapshot=%u resume=%u transcript=%u",
                 (unsigned int)g_net.correction_id,
                 (unsigned int)g_net.correction_frame,
                 (unsigned int)g_net.correction_resume_frame,
                 (unsigned int)g_net.correction_transcript);
        return 1;
    }

    if (g_net.mode == GGPO_NET_MODE_JOIN &&
        g_net.correction_phase == GGPO_NET_CORRECTION_READY &&
        g_net.correction_peer_phase == GGPO_NET_CORRECTION_COMMIT) {
        if (!ggpo_net_seed_local_input_delay_from(g_net.correction_resume_frame,
                                                  err,
                                                  err_cap) ||
            !ggpo_net_apply_correction_and_replay(g_net.recv_state,
                                                  g_net.recv_state_len,
                                                  arg0,
                                                  &transcript,
                                                  err,
                                                  err_cap)) {
            g_net.peer_disconnected = 1;
            return 0;
        }
        if (transcript != g_net.correction_expected_transcript) {
            LOG_ERROR("ggpo.net: correction transcript mismatch id=%u local=%u host=%u",
                      (unsigned int)g_net.correction_id,
                      (unsigned int)transcript,
                      (unsigned int)g_net.correction_expected_transcript);
            g_net.peer_disconnected = 1;
            ggpo_net_set_err(err, err_cap, "correction replay transcript mismatch");
            return 0;
        }
        g_net.correction_transcript = transcript;
        g_net.correction_phase = GGPO_NET_CORRECTION_APPLIED;
        ggpo_net_note_correction_progress();
        (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
        LOG_INFO("ggpo.net: correction replay applied id=%u snapshot=%u resume=%u transcript=%u",
                 (unsigned int)g_net.correction_id,
                 (unsigned int)g_net.correction_frame,
                 (unsigned int)g_net.correction_resume_frame,
                 (unsigned int)g_net.correction_transcript);
    }
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
        for (uint32_t i = 0u; i < frames; i++) {
            uint32_t f = g_net.frame + i;
            uint32_t tmp = 0;
            if (!ggpo_net_get_input(g_net.local_inputs, f, &tmp)) {
                if (!ggpo_net_store_local_input(f, 0u, NULL, 0u)) return 0;
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
    int requested = enabled ? 1 : 0;
    /* Changing policy may not dissolve one side of an in-flight mutual
     * barrier. The caller can either let it finish or terminate the session. */
    if (g_net.active && !requested &&
        g_net.correction_phase != GGPO_NET_CORRECTION_NONE) {
        return 0;
    }
    g_net_config_correction_enabled = requested;
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
            g_net.correction_progress_tick = 0;
            g_net.correction_phase = GGPO_NET_CORRECTION_NONE;
            g_net.correction_peer_phase = GGPO_NET_CORRECTION_NONE;
            g_net.correction_resume_frame = 0;
            g_net.correction_transcript = 0;
            g_net.correction_expected_transcript = 0;
            g_net.correction_input_count = 0;
            g_net.correction_snapshot_ready = 0;
            g_net.correction_local_applied = 0;
            g_net.correction_wait_cap_announced = 0;
            if ((g_net.recv_state_flags & GGPO_NET_STATE_FLAG_CORRECTION) != 0u) {
                ggpo_net_reset_recv_state();
            }
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

int ggpo_net_set_local_palette_preference(uint32_t skin_index,
                                          uint32_t clothing_index,
                                          uint32_t palette_count) {
    if (g_net.active ||
        !ggpo_net_palette_tuple_valid(palette_count,
                                      skin_index,
                                      clothing_index)) {
        return 0;
    }
    g_net_config_palette_count = palette_count;
    g_net_config_palette_skin = skin_index;
    g_net_config_palette_clothing = clothing_index;
    g_net_config_palette_valid = 1;
    return 1;
}

void ggpo_net_clear_local_palette_preference(void) {
    if (g_net.active) return;
    g_net_config_palette_count = 0u;
    g_net_config_palette_skin = 0u;
    g_net_config_palette_clothing = 0u;
    g_net_config_palette_valid = 0;
}

int ggpo_net_palette_ready(void) {
    if (!g_net.active) return 0;
    return ggpo_net_palette_ready_internal() ? 1 : 0;
}

int ggpo_net_local_palette_preference(uint32_t* out_skin_index,
                                      uint32_t* out_clothing_index,
                                      uint32_t* out_palette_count) {
    uint32_t skin = g_net.active
        ? g_net.local_palette_skin : g_net_config_palette_skin;
    uint32_t clothing = g_net.active
        ? g_net.local_palette_clothing : g_net_config_palette_clothing;
    uint32_t count = g_net.active
        ? g_net.local_palette_count : g_net_config_palette_count;
    int valid = g_net.active
        ? g_net.local_palette_valid : g_net_config_palette_valid;
    if (out_skin_index) *out_skin_index = valid ? skin : 0u;
    if (out_clothing_index) *out_clothing_index = valid ? clothing : 0u;
    if (out_palette_count) *out_palette_count = valid ? count : 0u;
    return valid ? 1 : 0;
}

int ggpo_net_remote_palette_preference(uint32_t* out_skin_index,
                                       uint32_t* out_clothing_index,
                                       uint32_t* out_palette_count) {
    if (out_skin_index) {
        *out_skin_index = g_net.remote_palette_valid
            ? g_net.remote_palette_skin : 0u;
    }
    if (out_clothing_index) {
        *out_clothing_index = g_net.remote_palette_valid
            ? g_net.remote_palette_clothing : 0u;
    }
    if (out_palette_count) {
        *out_palette_count = g_net.remote_palette_valid
            ? g_net.remote_palette_count : 0u;
    }
    return (g_net.active && g_net.remote_palette_valid) ? 1 : 0;
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

uint32_t ggpo_net_state_layout_fingerprint(void) {
    return (g_net.active && g_net.state_layout_finalized) ? g_net.state_layout_id : 0u;
}

int ggpo_net_state_layout_mismatch(void) {
    if (!g_net.active || !ggpo_net_link_confirmed() ||
        !g_net.state_layout_finalized) {
        return 0;
    }
    if (g_net.state_layout_conflict) return 1;
    if (g_net.remote_state_layout_id == 0u) return 0;
    return (g_net.state_layout_conflict ||
            g_net.state_layout_id != g_net.remote_state_layout_id ||
            g_net.state_size != g_net.remote_state_size) ? 1 : 0;
}

int ggpo_net_state_layout_ready(void) {
    if (!g_net.active || !ggpo_net_link_confirmed() ||
        !g_net.state_layout_finalized || g_net.state_layout_id == 0u ||
        g_net.remote_state_layout_id == 0u || g_net.state_layout_conflict) {
        return 0;
    }
    return (g_net.state_layout_id == g_net.remote_state_layout_id &&
            g_net.state_size == g_net.remote_state_size) ? 1 : 0;
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
        /* Constructor-time state belonged to the previously active map. It is
         * deliberately discarded so no rollback capacity is frozen until the
         * selected map/content has been installed behind this hold. */
        ggpo_net_discard_state_storage();
        g_net.remote_state_size = 0u;
        g_net.remote_state_layout_id = 0u;
        g_net.state_layout_conflict = 0;
        if (g_net.mode == GGPO_NET_MODE_HOST) {
            g_net.state_synced = 1;
            g_net.remote_state_synced = 0;
        } else {
            floor = g_net.remote_state_epoch_seen;
            if (g_net.state_epoch > floor) floor = g_net.state_epoch;
            /* A freshly started v17 host owns epoch 1 even if no HELLO has
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
    if (!g_net.state_layout_finalized || g_net.state_layout_id == 0u) {
        ggpo_net_set_err(err, err_cap, "rollback state layout was not finalized");
        return 0;
    }
    if (ggpo_net_state_layout_mismatch()) {
        ggpo_net_set_err(err, err_cap, "opponent rollback state layout does not match");
        return 0;
    }
    if (!ggpo_net_state_layout_ready()) {
        ggpo_net_set_err(err, err_cap, "opponent rollback state layout is not ready");
        return 0;
    }
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
static int ggpo_net_prime_prematch_frame0(char* err, size_t err_cap) {
    uint32_t cmd = 0u;
    if (!g_net.active || !g_net.prematch_used || g_net.frame != 0u) return 1;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 1;
    if (!ggpo_net_state_layout_ready() || ggpo_net_state_layout_mismatch()) return 1;
    if (!ggpo_net_link_confirmed() ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) return 1;
    if (ggpo_net_wait_for_cosmetic_profiles(NULL)) return 1;
    if (!ggpo_net_get_input(g_net.local_inputs, 0u, &cmd)) {
        if (!ggpo_net_store_local_input(0u, 0u, err, err_cap)) return 0;
    }
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
    return 1;
}

int ggpo_net_service(char* err, size_t err_cap) {
    if (!ggpo_net_service_transport(GGPO_NET_PACKET_HELLO, err, err_cap)) {
        return 0;
    }
    return ggpo_net_prime_prematch_frame0(err, err_cap);
}

int ggpo_net_prematch_ready(void) {
    uint32_t local_cmd = 0u;
    uint32_t remote_cmd = 0u;
    if (!g_net.active || !g_net.prematch_used || g_net.frame != 0u) return 0;
    if (g_net.prematch_hold ||
        (g_net.remote_prematch_hold_known && g_net.remote_prematch_hold)) return 0;
    if (!ggpo_net_state_layout_ready() || ggpo_net_state_layout_mismatch()) return 0;
    if (!ggpo_net_link_confirmed() ||
        !g_net.state_synced ||
        !g_net.remote_state_synced) return 0;
    if (!ggpo_net_cosmetic_profiles_ready() &&
        !g_net.cosmetic_wait_cap_announced) return 0;
    if (!ggpo_net_palette_ready_internal()) return 0;
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
    /* Complete even the idempotent frame-zero ring writes before mutating live
     * game state. The final state load preserves these current-epoch entries. */
    if (!ggpo_net_store_local_input(0u, local_cmd, err, err_cap) ||
        !ggpo_net_store_input(g_net.remote_inputs, 0u, remote_cmd)) {
        ggpo_net_set_err(err, err_cap, "frame-zero input ring could not be restored");
        return 0;
    }
    if (!ggpo_net_capture_local_render_geometry(err, err_cap)) {
        return 0;
    }
    if (!ggpo_net_load_start_state_if_ready(err, err_cap)) {
        return 0;
    }
    if (!g_net.start_state_loaded) {
        ggpo_net_set_err(err, err_cap,
                         "prematch start state was not loaded");
        return 0;
    }
    /* Do not regress newer command or simulation-progress markers learned from
     * a peer that prepared first. */
    ggpo_net_track_remote_cmd(0u, remote_cmd);
    (void)ggpo_net_note_remote_frame(0u);
    ggpo_net_advance_remote_contiguous_input();
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
    /* Repro writes use a copied blob, so gameplay never waits on disk. Session
     * teardown gives the tiny worker a bounded chance to finish its atomic
     * files before tests or the process inspect them. */
    ggpo_net_wait_desync_repro_worker(2000u);
    free(g_net.state_blobs);
    free(g_net.initial_state);
    free(g_net.correction_state);
    free(g_net.correction_base_state);
    free(g_net.apply_backup_state);
    free(g_net.apply_verify_state);
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

static int ggpo_net_start_common(GgpoNetMode mode,
                                 uint16_t local_port,
                                 int start_held,
                                 char* err,
                                 size_t err_cap) {
    SOCKET s = INVALID_SOCKET;
    uint16_t bound_port = local_port;
    size_t state_len = 0;
    uint32_t checksum = 0;
    uint32_t layout_id = 0;
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

    if (!start_held) {
        g_net.state_size = ggpo_ext_game_state_size();
        layout_id = ggpo_ext_game_state_layout_fingerprint();
        if (g_net.state_size == 0 || g_net.state_size > (size_t)UINT_MAX ||
            g_net.state_size > SIZE_MAX / (size_t)GGPO_NET_HISTORY_FRAMES ||
            layout_id == 0u) {
            ggpo_net_set_err(err, err_cap, "game state layout unavailable");
            return 0;
        }
    }
    if (!ggpo_net_make_socket(local_port, &s, &bound_port, err, err_cap)) {
        return 0;
    }

    if (!start_held) {
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
    g_net.apply_backup_state = (uint8_t*)malloc(g_net.state_size);
    g_net.apply_verify_state = (uint8_t*)malloc(g_net.state_size);
    if (!g_net.apply_backup_state || !g_net.apply_verify_state) {
        closesocket(s);
        free(g_net.state_blobs);
        free(g_net.initial_state);
        free(g_net.correction_state);
        free(g_net.correction_base_state);
        free(g_net.apply_backup_state);
        free(g_net.apply_verify_state);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        ggpo_net_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!ggpo_ext_save_game_state(g_net.state_blobs, g_net.state_size, &state_len, &checksum, err, err_cap) ||
        state_len == 0u || state_len > g_net.state_size ||
        ggpo_ext_game_state_size() != g_net.state_size ||
        ggpo_ext_game_state_layout_fingerprint() != layout_id) {
        closesocket(s);
        free(g_net.state_blobs);
        free(g_net.initial_state);
        free(g_net.correction_state);
        free(g_net.correction_base_state);
        free(g_net.apply_backup_state);
        free(g_net.apply_verify_state);
        memset(&g_net, 0, sizeof(g_net));
        g_net.sock = INVALID_SOCKET;
        if (!err || !err[0]) {
            ggpo_net_set_err(err, err_cap, "game state layout changed during initial capture");
        }
        return 0;
    }
    memcpy(g_net.initial_state, g_net.state_blobs, state_len);
    g_net.initial_state_len = state_len;
    (void)ggpo_net_set_correction_base(g_net.initial_state, g_net.initial_state_len, checksum);
    }
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
    g_net.local_palette_count = g_net_config_palette_count;
    g_net.local_palette_skin = g_net_config_palette_skin;
    g_net.local_palette_clothing = g_net_config_palette_clothing;
    g_net.local_palette_valid = g_net_config_palette_valid ? 1 : 0;
    g_net.sim_seed = ggpo_net_make_chaos_seed(g_net.session_id);
    g_net.sim_rng = g_net.sim_seed;
    g_net.initial_checksum = checksum;
    g_net.last_checksum = checksum;
    g_net.state_layout_id = layout_id;
    g_net.state_layout_finalized = start_held ? 0 : 1;
    g_net.state_epoch = (mode == GGPO_NET_MODE_HOST) ? GGPO_NET_INITIAL_STATE_EPOCH : 0u;
    g_net.state_synced = (mode == GGPO_NET_MODE_HOST) ? 1 : 0;
    g_net.remote_state_synced = (mode == GGPO_NET_MODE_HOST) ? 0 : 1;
    if (start_held) {
        g_net.prematch_hold = 1;
        g_net.prematch_used = 1;
        g_net.hold_epoch = 1u;
        if (mode == GGPO_NET_MODE_JOIN) {
            g_net.hold_remote_epoch_floor = GGPO_NET_INITIAL_STATE_EPOCH;
        }
    } else if (!ggpo_net_seed_frame_boundary(
                   0u,
                   g_net.state_blobs,
                   state_len,
                   checksum,
                   err,
                   err_cap)) {
        ggpo_net_stop_internal(0);
        return 0;
    }
    LOG_INFO("ggpo.net: local build fingerprint build=%08X exe=%08X dll=%08X correction=%s",
             (unsigned int)g_net.local_build_id,
             (unsigned int)g_net.local_exe_id,
             (unsigned int)g_net.local_dll_id,
             g_net.correction_enabled ? "on" : "off");
    return 1;
}

int ggpo_net_start_host(uint16_t local_port, char* err, size_t err_cap) {
    if (!ggpo_net_start_common(GGPO_NET_MODE_HOST, local_port, 0, err, err_cap)) {
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

int ggpo_net_start_host_held(uint16_t local_port, char* err, size_t err_cap) {
    if (!ggpo_net_start_common(GGPO_NET_MODE_HOST, local_port, 1, err, err_cap)) {
        return 0;
    }
    LOG_INFO("ggpo.net: hosting held udp port=%u; rollback layout deferred",
             (unsigned int)g_net.local_port);
    return 1;
}

int ggpo_net_start_join_deferred(uint16_t local_port, char* err, size_t err_cap) {
    if (!ggpo_net_start_common(GGPO_NET_MODE_JOIN, local_port, 0, err, err_cap)) {
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

int ggpo_net_start_join_deferred_held(uint16_t local_port, char* err, size_t err_cap) {
    if (!ggpo_net_start_common(GGPO_NET_MODE_JOIN, local_port, 1, err, err_cap)) {
        return 0;
    }
    LOG_INFO("ggpo.net: joining held/deferred local_port=%u; rollback layout deferred",
             (unsigned int)g_net.local_port);
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
        /* The endpoint belongs to a new socket/session attempt. Its predecessor's
         * authenticated layout proof must not keep release/ready true while we
         * wait for the replacement HELLO generation. */
        g_net.remote_state_size = 0u;
        g_net.remote_state_layout_id = 0u;
        g_net.state_layout_conflict = 0;
        ggpo_net_reset_recv_state();
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
    if (!ggpo_net_start_common(GGPO_NET_MODE_JOIN, local_port, 0, err, err_cap)) {
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
    if (ggpo_net_send_raw_bytes(line, len, &g_net.probe_server_addr) !=
        GGPO_NET_RAW_SEND_SENT) {
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
    uint32_t span = (g_net.frame_counter_wrapped || newest >= window) ? window : newest;
    uint32_t oldest = newest - span;
    if (!g_net_config_rng_trace) return;
    if (g_rng_dump_count >= 4u) return; /* cap per session: ~2 rounds of p0+p1, so a
                                         * correction storm can't cause a cascade of
                                         * heavy ring-dump freezes (was 16). */
    g_rng_dump_count++;
    LOG_INFO("ggpo.rngtrace DUMP p=%d desync_frame=%u window=%u..%u -> mods/desync_dump.log",
             g_net.local_player, (unsigned int)desync_frame,
             (unsigned int)oldest, (unsigned int)newest);
    log_dump_line("ggpo.rngtrace DUMP p=%d desync_frame=%u sid=%016llX x87=0x%04X mxcsr=0x%08X window=%u..%u",
                   g_net.local_player, (unsigned int)desync_frame,
                   (unsigned long long)g_net.session_id,
                  (unsigned int)fp_control_get_x87_control(),
                  (unsigned int)fp_control_get_mxcsr(),
                  (unsigned int)oldest, (unsigned int)newest);
    for (uint32_t offset = 0u; offset <= span; offset++) {
        uint32_t f = oldest + offset;
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
    GgpoNetHistoryEntry* h = NULL;
    GgpoNetHistoryEntry* next_h = NULL;
    uint8_t* pre_state = NULL;
    uint32_t checksum = 0;
    LuaGameStateRollbackSummary post_summary;
    int post_has_summary = 0;

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
    if (g_net.prematch_used && !ggpo_net_state_layout_ready()) {
        if (ggpo_net_state_layout_mismatch()) {
            ggpo_net_set_err(err, err_cap, "opponent rollback state layout does not match");
            return 0;
        }
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
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

    if (!ggpo_net_capture_local_render_geometry(err, err_cap)) {
        return 0;
    }
    if (!ggpo_net_load_start_state_if_ready(err, err_cap)) {
        return 0;
    }
    /* The loader intentionally reports a successful no-op while any setup
     * prerequisite is unmet. Never let an implicit ordering assumption turn
     * that transport-only state into committed input or a native tick. */
    if (!g_net.start_state_loaded) {
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE) {
        if (!ggpo_net_progress_correction(arg0, err, err_cap)) {
            (void)ggpo_net_restore_local_render_geometry(NULL, 0u);
            return 0;
        }
        if (!ggpo_net_restore_local_render_geometry(err, err_cap)) return 0;
        if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE) {
            if (out_checksum) *out_checksum = g_net.last_checksum;
            return 1;
        }
    }

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
        (void)ggpo_net_restore_local_render_geometry(NULL, 0u);
        return 0;
    }
    if (!ggpo_net_restore_local_render_geometry(err, err_cap)) return 0;
    ggpo_net_process_deferred_checksums(NULL);

    if (g_net.correction_phase != GGPO_NET_CORRECTION_NONE) {
        if (!ggpo_net_progress_correction(arg0, err, err_cap)) {
            (void)ggpo_net_restore_local_render_geometry(NULL, 0u);
            return 0;
        }
        if (!ggpo_net_restore_local_render_geometry(err, err_cap)) return 0;
        if (out_checksum) *out_checksum = g_net.last_checksum;
        return 1;
    }

    if (!g_net.correction_active &&
        g_net.has_remote_frame &&
        ggpo_net_frame_after(g_net.frame, g_net.remote_frame) &&
        (g_net.frame - g_net.remote_frame) > g_net.max_frame_advantage) {
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

    {
        uint32_t oldest_missing = 0;
        if (ggpo_net_prediction_stall_needed(g_net.frame, &oldest_missing)) {
            if (g_net.prediction_wait_start_tick == 0u) {
                g_net.prediction_wait_start_tick = ggpo_net_now_tick();
            }
            g_net.prediction_stalls++;
            if (!g_net.prediction_limit_wait_announced) {
                LOG_WARN("ggpo.net: hard recovery stall frame=%u predcap=%u oldest_missing=%u; selective resend remains active until recovery or peer timeout",
                         (unsigned int)g_net.frame,
                         (unsigned int)g_net.max_prediction,
                         (unsigned int)oldest_missing);
                g_net.prediction_limit_wait_announced = 1;
            }
            if (out_checksum) *out_checksum = g_net.last_checksum;
            return 1;
        } else {
            g_net.prediction_wait_start_tick = 0;
            g_net.prediction_wait_cap_announced = 0;
            g_net.prediction_limit_wait_announced = 0;
        }
    }

    {
        int checksum_gate =
            ggpo_net_checksum_retirement_gate(g_net.frame + 1u, err, err_cap);
        if (checksum_gate < 0) return 0;
        if (checksum_gate == 0) {
            if (out_checksum) *out_checksum = g_net.last_checksum;
            return 1;
        }
    }

    /* `raw_p*` is a tentative physical snapshot from the hook's current wall
     * tick. Commit it exactly once only after every no-advance gate above has
     * cleared. A long frame-advantage, prediction, checksum, frame-zero, or
     * correction wait therefore discards stale snapshots instead of assigning
     * the first one to a future input frame when simulation eventually resumes.
     * If a later state-save/tick failure retries this same logical frame,
     * queue_local_input is idempotent and preserves the original commitment. */
    {
        uint32_t sampled_cmd = (g_net.local_player == 0) ? raw_p0 : raw_p1;
        uint32_t input_frame = g_net.frame + g_net.input_delay;
        if (!ggpo_net_queue_local_input(input_frame,
                                        sampled_cmd,
                                        NULL,
                                        err,
                                        err_cap)) {
            return 0;
        }
    }
    (void)ggpo_net_send_packet(GGPO_NET_PACKET_INPUT);
    ggpo_net_send_cosmetic_profile_periodic();
    ggpo_net_send_cosmetic_asset_periodic();

    if (!ggpo_net_get_input(g_net.local_inputs, g_net.frame, &local_cmd)) {
        local_cmd = 0u;
    }

    remote_cmd = ggpo_net_predict_remote(g_net.frame, &predicted);
    if (predicted && !g_net.warned_prediction_limit) {
        uint32_t max_prediction = g_net.max_prediction ? g_net.max_prediction : GGPO_NET_DEFAULT_MAX_PREDICTION;
        uint32_t oldest_missing =
            ggpo_net_oldest_missing_remote_input(g_net.frame, max_prediction);
        if (g_net.frame - oldest_missing >= max_prediction) {
            LOG_WARN("ggpo.net: predicting remote input for %u+ frames (oldest_missing=%u current=%u)",
                     (unsigned int)max_prediction,
                     (unsigned int)oldest_missing,
                     (unsigned int)g_net.frame);
            g_net.warned_prediction_limit = 1;
        }
    }

    /* Undo every out-of-tick perturbation before capture. Besides render-time RNG
     * and camera work, adjust_layout may have rewritten both logical dimensions
     * from this peer's local drawable. The synced post-tick dimensions are the
     * simulation values; the saved local pair is restored after this tick. */
    if (!ggpo_net_restore_clean_sim_state(err, err_cap)) return 0;

    if (!ggpo_net_save_pre_state(g_net.frame, &h, &pre_state, err, err_cap)) {
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
        if (!ggpo_ext_advance_frame(&inputs, arg0, NULL, err, err_cap)) {
            if (trace) hooks_rng_trace_end();
            return ggpo_net_abort_live_tick_and_restore(h,
                                                        pre_state,
                                                        err,
                                                        err_cap);
        }
        if (!ggpo_net_capture_clean_sim_state(err, err_cap)) {
            if (trace) hooks_rng_trace_end();
            return ggpo_net_abort_live_tick_and_restore(h,
                                                        pre_state,
                                                        err,
                                                        err_cap);
        }
        if (!ggpo_net_capture_post_state(
                g_net.frame + 1u,
                &next_h,
                NULL,
                &post_summary,
                &post_has_summary,
                err,
                err_cap)) {
            if (trace) hooks_rng_trace_end();
            return ggpo_net_abort_live_tick_and_restore(h,
                                                        pre_state,
                                                        err,
                                                        err_cap);
        }
        checksum = next_h->pre_checksum;
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
    h->has_summary = post_has_summary;
    if (post_has_summary) h->summary = post_summary;

    if (!ggpo_net_restore_local_render_geometry(err, err_cap)) {
        return ggpo_net_abort_live_tick_and_restore(h,
                                                    pre_state,
                                                    err,
                                                    err_cap);
    }

    g_net.last_checksum = checksum;
    if (g_net.frame == UINT32_MAX) g_net.frame_counter_wrapped = 1;
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

int ggpo_net_remote_input_confirmed_frame(uint32_t* out_frame) {
    if (!g_net.has_remote_contiguous_input_frame) return 0;
    if (out_frame) *out_frame = g_net.remote_contiguous_input_frame;
    return 1;
}

int ggpo_net_peer_input_confirmed_frame(uint32_t* out_frame) {
    if (!g_net.has_peer_acked_local_input_frame) return 0;
    if (out_frame) *out_frame = g_net.peer_acked_local_input_frame;
    return 1;
}

int ggpo_net_checksum_confirmed_frame(uint32_t* out_frame) {
    return ggpo_net_checksum_horizon(out_frame);
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
    return (ggpo_net_frame_after(g_net.remote_frame, g_net.frame) &&
            (g_net.remote_frame - g_net.frame) > 1u) ? 1 : 0;
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

uint32_t ggpo_net_socket_would_block_count(void) {
    return g_net.socket_would_block_events;
}

uint32_t ggpo_net_socket_send_error_count(void) {
    return g_net.socket_send_errors;
}

uint32_t ggpo_net_socket_send_deferred_count(void) {
    return g_net.socket_send_work_deferred;
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
    uint32_t diag_checksum_horizon = 0u;
    int has_diag_checksum_horizon = 0;

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
    DIAG_LINE("palette: local=%s%u/%u remote=%s%u/%u entries=%u ack=%s conflict=%s",
              g_net.local_palette_valid ? "" : "none/",
              (unsigned int)(g_net.local_palette_valid
                  ? g_net.local_palette_skin : 0u),
              (unsigned int)(g_net.local_palette_valid
                  ? g_net.local_palette_clothing : 0u),
              g_net.remote_palette_valid ? "" : "none/",
              (unsigned int)(g_net.remote_palette_valid
                  ? g_net.remote_palette_skin : 0u),
              (unsigned int)(g_net.remote_palette_valid
                  ? g_net.remote_palette_clothing : 0u),
              (unsigned int)(g_net.local_palette_valid
                  ? g_net.local_palette_count : 0u),
              g_net.local_palette_acked ? "yes" : "no",
              g_net.palette_conflict ? "yes" : "no");
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
    has_diag_checksum_horizon =
        ggpo_net_checksum_horizon(&diag_checksum_horizon);
    DIAG_LINE("inputs:  local_frame=%u remote_frame=%u rx_contiguous=%s%u peer_acked=%s%u checksum_horizon=%s%u rollback=%s",
              (unsigned int)g_net.frame,
              (unsigned int)(g_net.has_remote_frame ? g_net.remote_frame : 0u),
              g_net.has_remote_contiguous_input_frame ? "" : "none/",
              (unsigned int)(g_net.has_remote_contiguous_input_frame
                  ? g_net.remote_contiguous_input_frame : 0u),
              g_net.has_peer_acked_local_input_frame ? "" : "none/",
              (unsigned int)(g_net.has_peer_acked_local_input_frame
                  ? g_net.peer_acked_local_input_frame : 0u),
              has_diag_checksum_horizon ? "" : "none/",
              (unsigned int)(has_diag_checksum_horizon
                  ? diag_checksum_horizon : 0u),
              g_net.rollback_pending ? "pending" : "clean");

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
    DIAG_LINE("tx pressure: would_block=%u send_error=%u deferred=%u last_error=%u sim_drop=%u queue_drop=%u pending=%u",
              (unsigned int)g_net.socket_would_block_events,
              (unsigned int)g_net.socket_send_errors,
              (unsigned int)g_net.socket_send_work_deferred,
              (unsigned int)g_net.socket_last_send_error,
              (unsigned int)g_net.sim_packets_dropped,
              (unsigned int)g_net.sim_queue_drops,
              (unsigned int)ggpo_net_sim_pending_count());

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
