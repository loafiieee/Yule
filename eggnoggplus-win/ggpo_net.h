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

typedef enum GgpoNetMode {
    GGPO_NET_MODE_NONE = 0,
    GGPO_NET_MODE_HOST = 1,
    GGPO_NET_MODE_JOIN = 2,
} GgpoNetMode;

int ggpo_net_active(void);
int ggpo_net_connected(void);
GgpoNetMode ggpo_net_mode(void);
const char* ggpo_net_mode_name(void);
int ggpo_net_local_player(void);
int ggpo_net_remote_player(void);
uint16_t ggpo_net_local_port(void);
uint16_t ggpo_net_remote_port(void);
uint32_t ggpo_net_input_delay(void);
int ggpo_net_set_input_delay(uint32_t frames);
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

int ggpo_net_start_host(uint16_t local_port, char* err, size_t err_cap);
int ggpo_net_start_join(const char* host, uint16_t remote_port, uint16_t local_port, char* err, size_t err_cap);
void ggpo_net_stop(void);

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
uint32_t ggpo_net_late_input_count(void);
uint32_t ggpo_net_dropped_input_count(void);
uint32_t ggpo_net_frame_advantage_stall_count(void);
uint32_t ggpo_net_prediction_stall_count(void);
uint32_t ggpo_net_desync_count(void);
uint32_t ggpo_net_desync_frame(void);
uint32_t ggpo_net_desync_local_checksum(void);
uint32_t ggpo_net_desync_remote_checksum(void);
uint32_t ggpo_net_peer_silence_ticks(void);

#ifdef __cplusplus
}
#endif
