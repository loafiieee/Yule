#include "ggpo_ext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hooks.h"
#include "lua_manager.h"

static const uint32_t k_ggpo_selftest_masks[] = {
    0u,
    0x08u,
    0x04u,
    0x01u,
    0x02u,
    0x09u,
    0x06u,
    0x0Au
};

static void ggpo_ext_set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    if (!msg) msg = "unknown error";
    snprintf(err, err_cap, "%s", msg);
}

static void ggpo_ext_clear_tick_inputs(void) {
    hooks_clear_tick_input(0);
    hooks_clear_tick_input(1);
    hooks_set_raw_input_blocked(0, 0);
    hooks_set_raw_input_blocked(1, 0);
}

static int ggpo_ext_advance_frame_internal(const GgpoFrameInputs* inputs, int arg0, int run_framework_tick, uint32_t* out_checksum, char* err, size_t err_cap) {
    int ran = 0;
    uint32_t checksum = 0;

    if (!inputs) {
        ggpo_ext_set_err(err, err_cap, "missing frame inputs");
        return 0;
    }

    hooks_set_raw_input_blocked(0, 1);
    hooks_set_raw_input_blocked(1, 1);
    hooks_set_tick_input(0, inputs->player_cmd[0], 1, 1);
    hooks_set_tick_input(1, inputs->player_cmd[1], 1, 1);

    ran = run_framework_tick ? hooks_advance_game_tick(arg0, 1) : hooks_simulate_game_ticks(1, arg0);
    if (ran != 1) {
        ggpo_ext_clear_tick_inputs();
        ggpo_ext_set_err(err, err_cap,
            (ran < 0) ? "game tick simulation unavailable" : "game tick simulation incomplete");
        return 0;
    }

    ggpo_ext_clear_tick_inputs();
    if (out_checksum) {
        if (!lua_manager_game_state_rollback_checksum(&checksum, err, err_cap)) {
            return 0;
        }
        *out_checksum = checksum;
    }
    return 1;
}

size_t ggpo_ext_game_state_size(void) {
    return lua_manager_game_state_size();
}

int ggpo_ext_save_game_state(void* dst, size_t dst_len, size_t* out_len, uint32_t* out_checksum, char* err, size_t err_cap) {
    uint32_t checksum = 0;

    if (!lua_manager_game_state_save(dst, dst_len, out_len, err, err_cap)) {
        return 0;
    }
    if (out_checksum) {
        if (!lua_manager_game_state_rollback_checksum(&checksum, err, err_cap)) {
            return 0;
        }
        *out_checksum = checksum;
    }
    return 1;
}

int ggpo_ext_load_game_state(const void* src, size_t src_len, char* err, size_t err_cap) {
    ggpo_ext_clear_tick_inputs();
    if (!lua_manager_game_state_load_rollback(src, src_len, err, err_cap)) {
        return 0;
    }
    hooks_sync_mad_ticks_to_game_clock();
    return 1;
}

int ggpo_ext_advance_frame(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap) {
    return ggpo_ext_advance_frame_internal(inputs, arg0, 0, out_checksum, err, err_cap);
}

int ggpo_ext_advance_frame_with_hooks(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap) {
    return ggpo_ext_advance_frame_internal(inputs, arg0, 1, out_checksum, err, err_cap);
}

int ggpo_ext_selftest(int frames, uint32_t* out_base_checksum, uint32_t* out_replay_checksum, char* err, size_t err_cap) {
    size_t state_size = ggpo_ext_game_state_size();
    void* state_blob = NULL;
    uint32_t* frame_checksums = NULL;
    size_t state_len = 0;
    uint32_t base_checksum = 0;
    uint32_t replay1_checksum = 0;
    uint32_t replay2_checksum = 0;
    uint32_t restored_checksum = 0;
    int mask_count = (int)(sizeof(k_ggpo_selftest_masks) / sizeof(k_ggpo_selftest_masks[0]));

    if (frames <= 0) {
        ggpo_ext_set_err(err, err_cap, "frames must be > 0");
        return 0;
    }
    if (frames > 3600) {
        ggpo_ext_set_err(err, err_cap, "frames must be <= 3600");
        return 0;
    }
    if (state_size == 0) {
        ggpo_ext_set_err(err, err_cap, "game state unavailable");
        return 0;
    }

    state_blob = malloc(state_size);
    if (!state_blob) {
        ggpo_ext_set_err(err, err_cap, "out of memory");
        return 0;
    }
    frame_checksums = (uint32_t*)calloc((size_t)frames, sizeof(uint32_t));
    if (!frame_checksums) {
        ggpo_ext_set_err(err, err_cap, "out of memory");
        free(state_blob);
        return 0;
    }

    if (!ggpo_ext_save_game_state(state_blob, state_size, &state_len, &base_checksum, err, err_cap)) {
        free(frame_checksums);
        free(state_blob);
        return 0;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < frames; i++) {
            GgpoFrameInputs inputs;
            uint32_t frame_checksum = 0;
            inputs.player_cmd[0] = k_ggpo_selftest_masks[i % mask_count];
            inputs.player_cmd[1] = k_ggpo_selftest_masks[((i * 3) + 2) % mask_count];
            if (!ggpo_ext_advance_frame(&inputs, 0, &frame_checksum, err, err_cap)) {
                (void)ggpo_ext_load_game_state(state_blob, state_len, NULL, 0);
                free(frame_checksums);
                free(state_blob);
                return 0;
            }
            if (pass == 0) {
                frame_checksums[i] = frame_checksum;
            } else if (frame_checksum != frame_checksums[i]) {
                char detail[160];
                snprintf(detail,
                         sizeof(detail),
                         "replay checksum mismatch frame=%d expected=%u got=%u",
                         i + 1,
                         frame_checksums[i],
                         frame_checksum);
                ggpo_ext_set_err(err, err_cap, detail);
                (void)ggpo_ext_load_game_state(state_blob, state_len, NULL, 0);
                free(frame_checksums);
                free(state_blob);
                return 0;
            }
        }

        if (!lua_manager_game_state_rollback_checksum(pass == 0 ? &replay1_checksum : &replay2_checksum, err, err_cap)) {
            (void)ggpo_ext_load_game_state(state_blob, state_len, NULL, 0);
            free(frame_checksums);
            free(state_blob);
            return 0;
        }
        if (!ggpo_ext_load_game_state(state_blob, state_len, err, err_cap)) {
            free(frame_checksums);
            free(state_blob);
            return 0;
        }
        if (!lua_manager_game_state_rollback_checksum(&restored_checksum, err, err_cap)) {
            free(frame_checksums);
            free(state_blob);
            return 0;
        }
        if (restored_checksum != base_checksum) {
            ggpo_ext_set_err(err, err_cap, "restore checksum mismatch");
            free(frame_checksums);
            free(state_blob);
            return 0;
        }
    }

    if (replay1_checksum != replay2_checksum) {
        char detail[160];
        snprintf(detail,
                 sizeof(detail),
                 "replay checksum mismatch expected=%u got=%u",
                 replay1_checksum,
                 replay2_checksum);
        ggpo_ext_set_err(err, err_cap, detail);
        free(frame_checksums);
        free(state_blob);
        return 0;
    }

    if (out_base_checksum) *out_base_checksum = base_checksum;
    if (out_replay_checksum) *out_replay_checksum = replay1_checksum;
    free(frame_checksums);
    free(state_blob);
    return 1;
}
