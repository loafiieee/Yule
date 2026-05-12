#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GGPO_NET_DEFAULT_PORT 47777
#define GGPO_NET_MAX_INPUT_DELAY 8

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
