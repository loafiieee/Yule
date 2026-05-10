#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ggpo_local_active(void);
int ggpo_local_start(char* err, size_t err_cap);
void ggpo_local_stop(void);
int ggpo_local_advance(uint32_t raw_p0, uint32_t raw_p1, int arg0, uint32_t* out_checksum, char* err, size_t err_cap);
uint32_t ggpo_local_frame_count(void);
uint32_t ggpo_local_last_checksum(void);
size_t ggpo_local_state_size(void);

#ifdef __cplusplus
}
#endif
