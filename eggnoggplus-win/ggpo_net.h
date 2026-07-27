#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GGPO_NET_DEFAULT_PORT 47777
#define GGPO_NET_MAX_INPUT_DELAY 8
#define GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT 220
#define GGPO_NET_MAX_PREDICTION_LIMIT 220
#define GGPO_NET_SIM_MAX_DELAY_TICKS 120
#define GGPO_NET_COSMETIC_PROFILE_BYTES 2048
#define GGPO_NET_COSMETIC_ASSET_ID_BYTES 65
#define GGPO_NET_COSMETIC_ASSET_MAX_BYTES 1048576
#define GGPO_NET_MATCH_TOKEN_HEX_BYTES 64
#define GGPO_NET_PROTOCOL_VERSION 17u
#define GGPO_NET_PALETTE_MAX_ENTRIES 256u

typedef enum GgpoNetMode {
    GGPO_NET_MODE_NONE = 0,
    GGPO_NET_MODE_HOST = 1,
    GGPO_NET_MODE_JOIN = 2,
} GgpoNetMode;

int ggpo_net_active(void);
/* True after the current socket/session has bidirectional reachability proof.
 * State sync/gameplay use an additional internal peer-acknowledgement gate. */
int ggpo_net_connected(void);
/* True only after both peers have acknowledged the current authenticated
 * socket/session. Prematch setup and state transfer must use this stricter gate;
 * ggpo_net_connected() is the earlier reachability proof used to avoid dueling
 * retry rotations. */
int ggpo_net_link_ready(void);
GgpoNetMode ggpo_net_mode(void);
const char* ggpo_net_mode_name(void);
int ggpo_net_local_player(void);
int ggpo_net_remote_player(void);
uint16_t ggpo_net_local_port(void);
uint16_t ggpo_net_remote_port(void);
uint32_t ggpo_net_input_delay(void);
int ggpo_net_set_input_delay(uint32_t frames);
int ggpo_net_auto_input_delay(void);
int ggpo_net_set_auto_input_delay(int enabled);
int ggpo_net_rng_trace(void);
int ggpo_net_set_rng_trace(int enabled);
uint32_t ggpo_net_rtt_ticks(void);
uint32_t ggpo_net_max_frame_advantage(void);
int ggpo_net_set_max_frame_advantage(uint32_t frames);
uint32_t ggpo_net_max_prediction(void);
int ggpo_net_set_max_prediction(uint32_t frames);
int ggpo_net_set_network_sim(uint32_t loss_percent, uint32_t min_delay_ticks, uint32_t max_delay_ticks);
uint32_t ggpo_net_sim_loss_percent(void);
uint32_t ggpo_net_sim_delay_min_ticks(void);
uint32_t ggpo_net_sim_delay_max_ticks(void);
uint32_t ggpo_net_sim_dropped_packets(void);
uint32_t ggpo_net_sim_delayed_packets(void);
uint32_t ggpo_net_sim_queue_drop_count(void);
uint32_t ggpo_net_sim_pending_packets(void);
int ggpo_net_correction_enabled(void);
int ggpo_net_set_correction_enabled(int enabled);
int ggpo_net_correction_active(void);
int ggpo_net_awaiting_correction(void);
uint32_t ggpo_net_corrections_sent(void);
uint32_t ggpo_net_corrections_received(void);
uint32_t ggpo_net_correction_request_count(void);
uint32_t ggpo_net_correction_id(void);
uint32_t ggpo_net_last_correction_applied_id(void);
uint32_t ggpo_net_stale_correction_request_count(void);
uint32_t ggpo_net_duplicate_state_chunk_count(void);
uint32_t ggpo_net_local_build_id(void);
uint32_t ggpo_net_local_exe_id(void);
uint32_t ggpo_net_local_dll_id(void);
uint32_t ggpo_net_remote_build_id(void);
uint32_t ggpo_net_remote_exe_id(void);
uint32_t ggpo_net_remote_dll_id(void);
int ggpo_net_build_mismatch(void);

/* Fixed-size presentation-only player palette exchange. Configure the local
 * assigned fighter before start_*; the tuple is authenticated, range-checked,
 * repeated and exactly acknowledged during prematch, but never serialized into
 * rollback state or included in gameplay checksums. */
int ggpo_net_set_local_palette_preference(uint32_t skin_index,
                                          uint32_t clothing_index,
                                          uint32_t palette_count);
void ggpo_net_clear_local_palette_preference(void);
int ggpo_net_palette_ready(void);
int ggpo_net_local_palette_preference(uint32_t* out_skin_index,
                                      uint32_t* out_clothing_index,
                                      uint32_t* out_palette_count);
int ggpo_net_remote_palette_preference(uint32_t* out_skin_index,
                                       uint32_t* out_clothing_index,
                                       uint32_t* out_palette_count);

/* Legacy cosmetic transport surface. The current online build compiles peer
 * cosmetics off: setters reject/clear payloads and getters remain empty. Keep
 * these declarations for source compatibility; do not treat them as an active
 * online customization API. */
int ggpo_net_set_local_cosmetic_profile(const char* profile, size_t profile_len);
const char* ggpo_net_remote_cosmetic_profile(size_t* out_len, uint32_t* out_revision);
void ggpo_net_mark_remote_cosmetic_profile_applied(uint32_t revision);
uint32_t ggpo_net_local_cosmetic_profile_revision(void);
uint32_t ggpo_net_remote_cosmetic_profile_revision(void);
uint32_t ggpo_net_remote_cosmetic_profile_applied_revision(void);
int ggpo_net_set_local_cosmetic_asset(const char* asset_id, const void* data, size_t data_len);
const void* ggpo_net_remote_cosmetic_asset(const char** out_id, size_t* out_len, uint32_t* out_revision);
void ggpo_net_mark_remote_cosmetic_asset_applied(uint32_t revision);
uint32_t ggpo_net_local_cosmetic_asset_revision(void);
uint32_t ggpo_net_remote_cosmetic_asset_revision(void);
uint32_t ggpo_net_remote_cosmetic_asset_applied_revision(void);
int ggpo_net_start_state_loaded(void);
int ggpo_net_state_synced(void);
int ggpo_net_remote_state_synced(void);
int ggpo_net_has_peer(void);
/* Prematch transport mode. Engage immediately after start_* and before the first
 * service call. While held, handshake/retry/RTT traffic continues but state is
 * never applied and gameplay never advances. Releasing captures a fresh start
 * state on host or re-arms fresh-epoch sync on join. */
int ggpo_net_set_prematch_hold(int enabled, char* err, size_t err_cap);
int ggpo_net_prematch_hold(void);
int ggpo_net_peer_prematch_hold(void);
uint32_t ggpo_net_state_epoch(void);
/* Freeze rollback capacity/schema from the map that is currently installed.
 * Server-managed callers must invoke this while both peers remain held, after
 * map reset/content installation and before releasing the hold. */
int ggpo_net_finalize_state_layout(char* err, size_t err_cap);
int ggpo_net_state_layout_ready(void);
int ggpo_net_state_layout_mismatch(void);
uint32_t ggpo_net_state_layout_fingerprint(void);
/* Service exactly one transport tick without sampling input or simulating a
 * frame. Intended for the online hub/countdown and setup transition. */
int ggpo_net_service(char* err, size_t err_cap);
/* True once the released prematch session has completed authoritative state
 * and neutral frame-0 input exchange. (Cosmetics are compile-disabled in this
 * build.) This is a query only;
 * callers must keep invoking ggpo_net_service while it is false. */
int ggpo_net_prematch_ready(void);
/* Atomically restore the synchronized frame-0 state immediately before the
 * visible switch to gameplay. Returns 1 on success, 0 with err populated on a
 * fatal setup error. It never advances the simulation. */
int ggpo_net_prepare_prematch_start(char* err, size_t err_cap);

/* Arm the next start_* call with the server-issued 256-bit per-match secret.
 * `token_hex` must contain exactly 64 hexadecimal characters. The token is
 * immediately decoded and domain-separated into a packet-authentication key;
 * the text is never retained or logged. A configured key is consumed by one
 * successful start, so each retry must arm it again. All peer packets use a
 * truncated HMAC-SHA-256 tag before any endpoint/session adoption. This gives
 * authentication and integrity, not encryption or traffic confidentiality. */
int ggpo_net_set_match_token(const char* token_hex, char* err, size_t err_cap);
void ggpo_net_clear_match_token(void);
uint32_t ggpo_net_auth_rejected_packets(void);

int ggpo_net_start_host(uint16_t local_port, char* err, size_t err_cap);
/* Socket/auth-only prematch starts. They intentionally allocate no rollback
 * storage until ggpo_net_finalize_state_layout() runs after map installation. */
int ggpo_net_start_host_held(uint16_t local_port, char* err, size_t err_cap);
int ggpo_net_start_join(const char* host, uint16_t remote_port, uint16_t local_port, char* err, size_t err_cap);
int ggpo_net_start_join_deferred(uint16_t local_port, char* err, size_t err_cap);
int ggpo_net_start_join_deferred_held(uint16_t local_port, char* err, size_t err_cap);
int ggpo_net_set_peer(const char* host, uint16_t remote_port, char* err, size_t err_cap);
/* Register an additional hole-punch candidate endpoint (e.g. the peer's public
 * NAT address alongside its LAN address). Handshake HELLOs go to all candidates;
 * whichever replies is adopted as the peer. Enables cross-network play. */
int ggpo_net_add_peer_candidate(const char* host, uint16_t remote_port, char* err, size_t err_cap);
int ggpo_net_send_server_probe(const char* host,
                               uint16_t port,
                               int match_id,
                               const char* username,
                               const char* token,
                               char* err,
                               size_t err_cap);
void ggpo_net_stop(void);
/* Tear down an unestablished attempt without sending BYE. A retry may race with
 * the peer's final HELLO, so a terminal disconnect packet would poison its next
 * attempt. Use only while the connection retry state machine is pre-gameplay. */
void ggpo_net_stop_for_retry(void);

int ggpo_net_advance(uint32_t raw_p0,
                     uint32_t raw_p1,
                     int arg0,
                     uint32_t* out_checksum,
                     int* out_advanced,
                     char* err,
                     size_t err_cap);

uint32_t ggpo_net_frame_count(void);
uint32_t ggpo_net_remote_frame_count(void);
uint32_t ggpo_net_last_checksum(void);
size_t ggpo_net_state_size(void);
int ggpo_net_catchup_pending(void);
uint32_t ggpo_net_prediction_count(void);
uint32_t ggpo_net_rollback_count(void);
uint32_t ggpo_net_packets_sent(void);
uint32_t ggpo_net_packets_received(void);
/* Physical UDP send pressure. Would-block/ENOBUFS datagrams are retried by
 * their owning transfer; hard errors are counted separately. */
uint32_t ggpo_net_socket_would_block_count(void);
uint32_t ggpo_net_socket_send_error_count(void);
uint32_t ggpo_net_socket_send_deferred_count(void);
/* Multi-line, human-readable P2P connection troubleshooter (see net.diag). */
void ggpo_net_format_diag(char* out, size_t cap);
uint32_t ggpo_net_late_input_count(void);
uint32_t ggpo_net_dropped_input_count(void);
uint32_t ggpo_net_frame_advantage_stall_count(void);
uint32_t ggpo_net_prediction_stall_count(void);
uint32_t ggpo_net_desync_count(void);
uint32_t ggpo_net_desync_frame(void);
uint32_t ggpo_net_desync_local_checksum(void);
uint32_t ggpo_net_desync_remote_checksum(void);
uint32_t ggpo_net_peer_silence_ticks(void);
/* Input-delivery horizons are distinct from remote_frame (peer simulation
 * progress). Each accessor returns zero until at least frame 0 is contiguous. */
int ggpo_net_remote_input_confirmed_frame(uint32_t* out_frame);
int ggpo_net_peer_input_confirmed_frame(uint32_t* out_frame);
int ggpo_net_checksum_confirmed_frame(uint32_t* out_frame);

#ifdef GGPO_NET_TEST
#define GGPO_NET_TEST_ACK_WORDS 16u
#define GGPO_NET_TEST_PACKET_INPUTS 64u
#define GGPO_NET_TEST_PACKET_CHECKSUMS 32u
#define GGPO_NET_TEST_CORRECTION_OFFER 2u
#define GGPO_NET_TEST_CORRECTION_RECEIVING 3u
#define GGPO_NET_TEST_CORRECTION_COMMIT 5u
#define GGPO_NET_TEST_CORRECTION_RELEASE 7u
#define GGPO_NET_TEST_CORRECTION_RELEASE_ACK 8u
/* Adversarial loopback hook: corrupt each signed peer datagram after its MAC is
 * computed. It is deliberately absent from production builds. */
void ggpo_net_test_set_tamper_outgoing(int enabled);
void ggpo_net_test_set_replay_outgoing(int enabled);
void ggpo_net_test_set_suppress_checksum_payload(int enabled);
void ggpo_net_test_force_state_chunk_would_block(uint32_t count);
int ggpo_net_test_send_correction_chunk_once(void);
void ggpo_net_test_get_correction_send_cursor(uint32_t* out_full_offset,
                                               uint32_t* out_delta_next);
uint32_t ggpo_net_test_correction_phase(void);
uint32_t ggpo_net_test_correction_peer_phase(void);
uint32_t ggpo_net_test_correction_snapshot_frame(void);
uint32_t ggpo_net_test_correction_resume_frame(void);
uint32_t ggpo_net_test_correction_transcript(void);
/* Deterministic frame-ring probes. These bypass sockets/authentication and are
 * available only to the native transport regression test. */
void ggpo_net_test_reset_frame_rings(uint32_t local_frame);
void ggpo_net_test_set_local_frame(uint32_t local_frame);
int ggpo_net_test_rebase_active_frame(uint32_t start_frame,
                                      char* err,
                                      size_t err_cap);
int ggpo_net_test_receive_remote_frame(uint32_t frame);
int ggpo_net_test_receive_remote_input(uint32_t frame, uint32_t cmd);
int ggpo_net_test_get_remote_input(uint32_t frame, uint32_t* out_cmd);
uint32_t ggpo_net_test_predict_remote_input(uint32_t frame);
int ggpo_net_test_latest_remote_input(uint32_t* out_frame, uint32_t* out_cmd);
int ggpo_net_test_prediction_stall_needed(uint32_t frame, uint32_t* out_oldest_missing);
int ggpo_net_test_store_history_marker(uint32_t frame, uint32_t marker);
int ggpo_net_test_get_history_marker(uint32_t frame, uint32_t* out_marker);
int ggpo_net_test_copy_history_state(uint32_t frame, void* dst, size_t dst_cap,
                                     size_t* out_len,
                                     uint32_t* out_post_checksum);
int ggpo_net_test_store_local_input(uint32_t frame, uint32_t cmd);
int ggpo_net_test_get_local_input(uint32_t frame, uint32_t* out_cmd);
int ggpo_net_test_get_remote_ack(uint32_t* out_valid,
                                 uint32_t* out_frame,
                                 uint32_t out_bits[GGPO_NET_TEST_ACK_WORDS]);
int ggpo_net_test_apply_peer_ack(uint32_t valid,
                                 uint32_t frame,
                                 const uint32_t bits[GGPO_NET_TEST_ACK_WORDS]);
int ggpo_net_test_get_peer_ack(uint32_t* out_valid, uint32_t* out_frame);
uint32_t ggpo_net_test_build_input_packet(uint32_t* out_frames,
                                          uint32_t* out_cmds,
                                          uint32_t capacity);
uint32_t ggpo_net_test_build_checksum_packet(uint32_t* out_frames,
                                             uint32_t* out_checksums,
                                             uint32_t capacity);
int ggpo_net_test_checksum_horizon(uint32_t* out_frame);
void ggpo_net_test_set_remote_contiguous(uint32_t valid, uint32_t frame);
void ggpo_net_test_set_peer_ack(uint32_t valid, uint32_t frame);
void ggpo_net_test_set_rollback_pending(int pending);
int ggpo_net_test_seed_history(uint32_t frame,
                               uint32_t remote_cmd,
                               int remote_predicted,
                               uint32_t post_checksum);
int ggpo_net_test_validate_checksum_envelope(uint32_t input_ack_valid,
                                             uint32_t input_ack_base,
                                             uint32_t confirmed_valid,
                                             uint32_t confirmed_frame,
                                             uint32_t packet_frame);
void ggpo_net_test_set_checksum_epoch(uint32_t state_epoch,
                                      uint32_t correction_id,
                                      uint32_t start_frame);
void ggpo_net_test_get_checksum_ack_next(uint32_t* out_remote_next,
                                         uint32_t* out_peer_next);
void ggpo_net_test_set_checksum_ack_next(uint32_t remote_next,
                                         uint32_t peer_next);
int ggpo_net_test_mark_checksum_published(uint32_t frame);
int ggpo_net_test_apply_peer_checksum_ack(uint32_t state_epoch,
                                          uint32_t correction_id,
                                          uint32_t ack_next);
int ggpo_net_test_validate_checksum_ack_envelope(uint32_t state_epoch,
                                                 uint32_t correction_id,
                                                 uint32_t ack_next);
int ggpo_net_test_receive_remote_checksum(uint32_t frame, uint32_t checksum);
void ggpo_net_test_process_deferred_checksums(void);
int ggpo_net_test_checksum_history_retirable(uint32_t frame);
int ggpo_net_test_checksum_retirement_gate(uint32_t frame,
                                           uint32_t service_tick);
int ggpo_net_test_service_input(char* err, size_t err_cap);
/* Exercise the same preflight/apply/restore transaction used by completed host
 * and correction state transfers without opening a socket. */
int ggpo_net_test_apply_state_transaction(const void* candidate,
                                          size_t candidate_len,
                                          uint32_t expected_checksum,
                                          uint32_t* out_checksum,
                                          int* out_fatal_restore,
                                          char* err,
                                          size_t err_cap);
#endif

#ifdef __cplusplus
}
#endif
