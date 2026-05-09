#include "ggpo_loopback.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ggpo_ext.h"
#include "lua_manager.h"

#define GGPO_LOOPBACK_HISTORY_FRAMES 128
#define GGPO_LOOPBACK_VERIFY_INTERVAL 90
#define GGPO_LOOPBACK_VERIFY_DISTANCE 8

typedef struct GgpoLoopbackHistoryEntry {
    int valid;
    uint32_t frame;
    uint32_t pre_checksum;
    uint32_t post_checksum;
    size_t state_len;
    size_t post_state_len;
    GgpoFrameInputs inputs;
} GgpoLoopbackHistoryEntry;

typedef struct GgpoLoopbackSession {
    int active;
    uint32_t frame;
    uint32_t last_checksum;
    size_t state_size;
    uint8_t* state_blobs;
    uint8_t* post_state_blobs;
    uint8_t* last_post_state;
    uint8_t* scratch_state;
    uint8_t* verify_state;
    uint8_t* diff_state;
    size_t last_post_state_len;
    size_t scratch_state_len;
    size_t verify_state_len;
    int has_last_post_state;
    uint32_t verify_count;
    uint32_t verify_fail_count;
    GgpoLoopbackHistoryEntry history[GGPO_LOOPBACK_HISTORY_FRAMES];
} GgpoLoopbackSession;

static GgpoLoopbackSession g_loopback;

static void ggpo_loopback_set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    if (!msg) msg = "unknown error";
    snprintf(err, err_cap, "%s", msg);
}

int ggpo_loopback_active(void) {
    return g_loopback.active ? 1 : 0;
}

uint32_t ggpo_loopback_frame_count(void) {
    return g_loopback.frame;
}

uint32_t ggpo_loopback_last_checksum(void) {
    return g_loopback.last_checksum;
}

size_t ggpo_loopback_state_size(void) {
    return g_loopback.state_size;
}

int ggpo_loopback_history_capacity(void) {
    return GGPO_LOOPBACK_HISTORY_FRAMES;
}

uint32_t ggpo_loopback_verify_count(void) {
    return g_loopback.verify_count;
}

uint32_t ggpo_loopback_verify_failure_count(void) {
    return g_loopback.verify_fail_count;
}

int ggpo_loopback_verify_interval(void) {
    return GGPO_LOOPBACK_VERIFY_INTERVAL;
}

int ggpo_loopback_verify_distance(void) {
    return GGPO_LOOPBACK_VERIFY_DISTANCE;
}

static int ggpo_loopback_get_history_entry(uint32_t frame, GgpoLoopbackHistoryEntry** out_entry, uint8_t** out_blob, char* err, size_t err_cap) {
    int slot_index = (int)(frame % GGPO_LOOPBACK_HISTORY_FRAMES);
    GgpoLoopbackHistoryEntry* slot = &g_loopback.history[slot_index];

    if (!slot->valid || slot->frame != frame) {
        ggpo_loopback_set_err(err, err_cap, "requested rollback history frame is unavailable");
        return 0;
    }
    if (out_entry) *out_entry = slot;
    if (out_blob) *out_blob = g_loopback.state_blobs + ((size_t)slot_index * g_loopback.state_size);
    return 1;
}

static uint8_t* ggpo_loopback_post_blob_for_frame(uint32_t frame) {
    int slot_index = (int)(frame % GGPO_LOOPBACK_HISTORY_FRAMES);
    return g_loopback.post_state_blobs + ((size_t)slot_index * g_loopback.state_size);
}

static size_t ggpo_loopback_first_diff_offset(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len) {
    size_t min_len = (a_len < b_len) ? a_len : b_len;
    for (size_t i = 0; i < min_len; i++) {
        if (a[i] != b[i]) return i;
    }
    return (a_len == b_len) ? (size_t)-1 : min_len;
}

static int ggpo_loopback_restore_scratch(char* err, size_t err_cap) {
    if (!g_loopback.scratch_state || g_loopback.scratch_state_len == 0) {
        ggpo_loopback_set_err(err, err_cap, "scratch state unavailable");
        return 0;
    }
    return ggpo_ext_load_game_state(g_loopback.scratch_state, g_loopback.scratch_state_len, err, err_cap);
}

static int ggpo_loopback_verify_recent(int arg0, char* err, size_t err_cap) {
    uint32_t current_checksum = 0;
    uint32_t replay_checksum = 0;
    uint32_t restored_checksum = 0;
    uint32_t end_frame = 0;
    uint32_t start_frame = 0;

    if (g_loopback.frame < (uint32_t)GGPO_LOOPBACK_VERIFY_DISTANCE) {
        return 1;
    }
    if ((g_loopback.frame % (uint32_t)GGPO_LOOPBACK_VERIFY_INTERVAL) != 0u) {
        return 1;
    }

    if (!ggpo_ext_save_game_state(g_loopback.scratch_state, g_loopback.state_size, &g_loopback.scratch_state_len, &current_checksum, err, err_cap)) {
        return 0;
    }

    end_frame = g_loopback.frame - 1u;
    start_frame = g_loopback.frame - (uint32_t)GGPO_LOOPBACK_VERIFY_DISTANCE;

    {
        GgpoLoopbackHistoryEntry* start_entry = NULL;
        uint8_t* start_blob = NULL;
        if (!ggpo_loopback_get_history_entry(start_frame, &start_entry, &start_blob, err, err_cap)) {
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
        if (!ggpo_ext_load_game_state(start_blob, start_entry->state_len, err, err_cap)) {
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
    }

    for (uint32_t f = start_frame; f <= end_frame; f++) {
        GgpoLoopbackHistoryEntry* entry = NULL;
        uint8_t* entry_blob = NULL;
        uint32_t replay_pre_checksum = 0;
        if (!ggpo_loopback_get_history_entry(f, &entry, &entry_blob, err, err_cap)) {
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
        if (!ggpo_ext_save_game_state(g_loopback.verify_state, g_loopback.state_size, &g_loopback.verify_state_len, &replay_pre_checksum, err, err_cap)) {
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
        if (replay_pre_checksum != entry->pre_checksum) {
            size_t diff = (size_t)-1;
            const char* diff_name = lua_manager_game_state_offset_name(diff);
            char detail[256];
            memcpy(g_loopback.diff_state, entry_blob, entry->state_len);
            (void)lua_manager_game_state_canonicalize_rollback(g_loopback.diff_state, entry->state_len, NULL, 0);
            (void)lua_manager_game_state_canonicalize_rollback(g_loopback.verify_state, g_loopback.verify_state_len, NULL, 0);
            diff = ggpo_loopback_first_diff_offset(g_loopback.diff_state, entry->state_len, g_loopback.verify_state, g_loopback.verify_state_len);
            diff_name = lua_manager_game_state_offset_name(diff);
            snprintf(detail,
                     sizeof(detail),
                     "rollback pre checksum mismatch frame=%u expected=%u got=%u diff_offset=%u diff=%s expected_len=%u got_len=%u range=%u..%u",
                     f,
                     entry->pre_checksum,
                     replay_pre_checksum,
                     (unsigned int)diff,
                     diff_name ? diff_name : "unknown",
                     (unsigned int)entry->state_len,
                     (unsigned int)g_loopback.verify_state_len,
                     start_frame,
                     end_frame);
            ggpo_loopback_set_err(err, err_cap, detail);
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
        if (!ggpo_ext_advance_frame(&entry->inputs, arg0, &replay_checksum, err, err_cap)) {
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
        if (replay_checksum != entry->post_checksum) {
            uint8_t* post_blob = ggpo_loopback_post_blob_for_frame(f);
            size_t diff = (size_t)-1;
            const char* diff_name = "unknown";
            size_t replay_state_len = 0;
            uint32_t saved_replay_checksum = 0;
            char detail[256];
            if (post_blob && ggpo_ext_save_game_state(g_loopback.verify_state,
                                                       g_loopback.state_size,
                                                       &replay_state_len,
                                                       &saved_replay_checksum,
                                                       NULL,
                                                       0)) {
                memcpy(g_loopback.diff_state, post_blob, entry->post_state_len);
                (void)lua_manager_game_state_canonicalize_rollback(g_loopback.diff_state, entry->post_state_len, NULL, 0);
                (void)lua_manager_game_state_canonicalize_rollback(g_loopback.verify_state, replay_state_len, NULL, 0);
                diff = ggpo_loopback_first_diff_offset(g_loopback.diff_state, entry->post_state_len, g_loopback.verify_state, replay_state_len);
                diff_name = lua_manager_game_state_offset_name(diff);
            }
            snprintf(detail,
                     sizeof(detail),
                     "rollback replay checksum mismatch frame=%u expected=%u got=%u diff_offset=%u diff=%s p0=0x%08X p1=0x%08X range=%u..%u",
                     f,
                     entry->post_checksum,
                     replay_checksum,
                     (unsigned int)diff,
                     diff_name ? diff_name : "unknown",
                     entry->inputs.player_cmd[0],
                     entry->inputs.player_cmd[1],
                     start_frame,
                     end_frame);
            ggpo_loopback_set_err(err, err_cap, detail);
            (void)ggpo_loopback_restore_scratch(NULL, 0);
            return 0;
        }
    }

    if (replay_checksum != current_checksum) {
        char detail[160];
        snprintf(detail,
                 sizeof(detail),
                 "rollback final checksum mismatch expected=%u got=%u range=%u..%u",
                 current_checksum,
                 replay_checksum,
                 start_frame,
                 end_frame);
        ggpo_loopback_set_err(err, err_cap, detail);
        (void)ggpo_loopback_restore_scratch(NULL, 0);
        return 0;
    }

    if (!ggpo_loopback_restore_scratch(err, err_cap)) {
        return 0;
    }
    if (!lua_manager_game_state_rollback_checksum(&restored_checksum, err, err_cap)) {
        return 0;
    }
    if (restored_checksum != current_checksum) {
        char detail[160];
        snprintf(detail,
                 sizeof(detail),
                 "restored live state checksum mismatch expected=%u got=%u",
                 current_checksum,
                 restored_checksum);
        ggpo_loopback_set_err(err, err_cap, detail);
        return 0;
    }

    g_loopback.verify_count++;
    return 1;
}

void ggpo_loopback_stop(void) {
    free(g_loopback.state_blobs);
    free(g_loopback.post_state_blobs);
    free(g_loopback.last_post_state);
    free(g_loopback.scratch_state);
    free(g_loopback.verify_state);
    free(g_loopback.diff_state);
    memset(&g_loopback, 0, sizeof(g_loopback));
}

int ggpo_loopback_start(char* err, size_t err_cap) {
    size_t total_size = 0;

    if (g_loopback.active) return 1;

    memset(&g_loopback, 0, sizeof(g_loopback));
    g_loopback.state_size = ggpo_ext_game_state_size();
    if (g_loopback.state_size == 0) {
        ggpo_loopback_set_err(err, err_cap, "game state unavailable");
        return 0;
    }

    total_size = (size_t)GGPO_LOOPBACK_HISTORY_FRAMES * g_loopback.state_size;
    g_loopback.state_blobs = (uint8_t*)calloc(1, total_size);
    if (!g_loopback.state_blobs) {
        ggpo_loopback_set_err(err, err_cap, "out of memory");
        memset(&g_loopback, 0, sizeof(g_loopback));
        return 0;
    }
    g_loopback.post_state_blobs = (uint8_t*)calloc(1, total_size);
    if (!g_loopback.post_state_blobs) {
        ggpo_loopback_set_err(err, err_cap, "out of memory");
        ggpo_loopback_stop();
        return 0;
    }
    g_loopback.last_post_state = (uint8_t*)malloc(g_loopback.state_size);
    if (!g_loopback.last_post_state) {
        ggpo_loopback_set_err(err, err_cap, "out of memory");
        ggpo_loopback_stop();
        return 0;
    }
    g_loopback.scratch_state = (uint8_t*)malloc(g_loopback.state_size);
    if (!g_loopback.scratch_state) {
        ggpo_loopback_set_err(err, err_cap, "out of memory");
        ggpo_loopback_stop();
        return 0;
    }
    g_loopback.verify_state = (uint8_t*)malloc(g_loopback.state_size);
    if (!g_loopback.verify_state) {
        ggpo_loopback_set_err(err, err_cap, "out of memory");
        ggpo_loopback_stop();
        return 0;
    }
    g_loopback.diff_state = (uint8_t*)malloc(g_loopback.state_size);
    if (!g_loopback.diff_state) {
        ggpo_loopback_set_err(err, err_cap, "out of memory");
        ggpo_loopback_stop();
        return 0;
    }
    g_loopback.active = 1;
    return 1;
}

int ggpo_loopback_advance(uint32_t raw_p0, uint32_t raw_p1, int arg0, uint32_t* out_checksum, char* err, size_t err_cap) {
    GgpoLoopbackHistoryEntry* slot = NULL;
    uint8_t* slot_blob = NULL;
    uint8_t* post_blob = NULL;
    size_t state_len = 0;
    size_t post_state_len = 0;
    GgpoFrameInputs inputs;
    uint32_t checksum = 0;
    int slot_index = 0;

    if (!g_loopback.active) {
        ggpo_loopback_set_err(err, err_cap, "loopback session is not active");
        return 0;
    }
    if (!g_loopback.state_blobs || g_loopback.state_size == 0) {
        ggpo_loopback_set_err(err, err_cap, "loopback session has no state storage");
        return 0;
    }

    slot_index = (int)(g_loopback.frame % GGPO_LOOPBACK_HISTORY_FRAMES);
    slot = &g_loopback.history[slot_index];
    slot_blob = g_loopback.state_blobs + ((size_t)slot_index * g_loopback.state_size);
    post_blob = g_loopback.post_state_blobs + ((size_t)slot_index * g_loopback.state_size);

    if (g_loopback.has_last_post_state) {
        if (!ggpo_ext_load_game_state(g_loopback.last_post_state, g_loopback.last_post_state_len, err, err_cap)) {
            return 0;
        }
    }

    memset(slot, 0, sizeof(*slot));
    slot->frame = g_loopback.frame;
    inputs.player_cmd[0] = raw_p0;
    inputs.player_cmd[1] = raw_p1;
    slot->inputs = inputs;

    if (!ggpo_ext_save_game_state(slot_blob, g_loopback.state_size, &state_len, &slot->pre_checksum, err, err_cap)) {
        return 0;
    }
    slot->state_len = state_len;

    if (!ggpo_ext_advance_frame(&inputs, arg0, &checksum, err, err_cap)) {
        return 0;
    }
    if (!ggpo_ext_save_game_state(post_blob, g_loopback.state_size, &post_state_len, &checksum, err, err_cap)) {
        return 0;
    }
    if (post_state_len > g_loopback.state_size) {
        ggpo_loopback_set_err(err, err_cap, "post state exceeded rollback storage");
        return 0;
    }
    memcpy(g_loopback.last_post_state, post_blob, post_state_len);
    g_loopback.last_post_state_len = post_state_len;
    g_loopback.has_last_post_state = 1;

    slot->post_checksum = checksum;
    slot->post_state_len = post_state_len;
    slot->valid = 1;
    g_loopback.last_checksum = checksum;
    g_loopback.frame++;
    if (!ggpo_loopback_verify_recent(arg0, err, err_cap)) {
        g_loopback.verify_fail_count++;
        return 0;
    }
    if (out_checksum) *out_checksum = checksum;
    return 1;
}
