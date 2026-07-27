#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GgpoFrameInputs {
    uint32_t player_cmd[2];
} GgpoFrameInputs;

size_t ggpo_ext_game_state_size(void);
uint32_t ggpo_ext_game_state_layout_fingerprint(void);
int ggpo_ext_save_game_state_raw(void* dst, size_t dst_len, size_t* out_len, char* err, size_t err_cap);
int ggpo_ext_save_game_state(void* dst, size_t dst_len, size_t* out_len, uint32_t* out_checksum, char* err, size_t err_cap);
int ggpo_ext_validate_rollback_blob(const void* src, size_t src_len, uint32_t* out_checksum, char* err, size_t err_cap);
int ggpo_ext_validate_rollback_transport_blob(const void* src, size_t src_len, uint32_t* out_checksum, char* err, size_t err_cap);
int ggpo_ext_load_game_state_raw(const void* src, size_t src_len, char* err, size_t err_cap);
int ggpo_ext_load_game_state(const void* src, size_t src_len, char* err, size_t err_cap);
int ggpo_ext_advance_frame(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap);
int ggpo_ext_advance_frame_with_hooks(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap);
int ggpo_ext_selftest(int frames, uint32_t* out_base_checksum, uint32_t* out_replay_checksum, char* err, size_t err_cap);

#ifdef __cplusplus
}
#endif
