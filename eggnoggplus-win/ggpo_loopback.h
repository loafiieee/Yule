#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ggpo_loopback_active(void);
int ggpo_loopback_start(char* err, size_t err_cap);
void ggpo_loopback_stop(void);
int ggpo_loopback_advance(uint32_t raw_p0, uint32_t raw_p1, int arg0, uint32_t* out_checksum, char* err, size_t err_cap);
uint32_t ggpo_loopback_frame_count(void);
uint32_t ggpo_loopback_last_checksum(void);
size_t ggpo_loopback_state_size(void);
int ggpo_loopback_history_capacity(void);
uint32_t ggpo_loopback_verify_count(void);
uint32_t ggpo_loopback_verify_failure_count(void);
int ggpo_loopback_verify_interval(void);
int ggpo_loopback_verify_distance(void);

#ifdef __cplusplus
}
#endif
