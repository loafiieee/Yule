#include "ggpo_local.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ggpo_ext.h"

typedef struct GgpoLocalCallbacks {
    int (*begin_game)(const char* game_name);
    int (*save_game_state)(unsigned char** out_buffer, int* out_len, int* out_checksum, int frame, char* err, size_t err_cap);
    int (*load_game_state)(const unsigned char* buffer, int len, char* err, size_t err_cap);
    void (*free_buffer)(void* buffer);
    int (*advance_frame)(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap);
} GgpoLocalCallbacks;

typedef struct GgpoLocalSession {
    int active;
    uint32_t frame;
    uint32_t last_checksum;
    size_t state_size;
    GgpoLocalCallbacks callbacks;
} GgpoLocalSession;

static GgpoLocalSession g_local;

static void ggpo_local_set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg ? msg : "unknown error");
}

static int local_begin_game(const char* game_name) {
    (void)game_name;
    return 1;
}

static int local_save_game_state(unsigned char** out_buffer, int* out_len, int* out_checksum, int frame, char* err, size_t err_cap) {
    unsigned char* buffer = NULL;
    size_t state_len = 0;
    uint32_t checksum = 0;
    (void)frame;

    if (!out_buffer || !out_len || !out_checksum) {
        ggpo_local_set_err(err, err_cap, "missing save_game_state output");
        return 0;
    }
    if (g_local.state_size == 0 || g_local.state_size > (size_t)INT_MAX) {
        ggpo_local_set_err(err, err_cap, "invalid callback state size");
        return 0;
    }

    buffer = (unsigned char*)malloc(g_local.state_size);
    if (!buffer) {
        ggpo_local_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!ggpo_ext_save_game_state(buffer, g_local.state_size, &state_len, &checksum, err, err_cap)) {
        free(buffer);
        return 0;
    }
    if (state_len > (size_t)INT_MAX) {
        free(buffer);
        ggpo_local_set_err(err, err_cap, "state too large");
        return 0;
    }

    *out_buffer = buffer;
    *out_len = (int)state_len;
    *out_checksum = (int)checksum;
    return 1;
}

static int local_load_game_state(const unsigned char* buffer, int len, char* err, size_t err_cap) {
    if (!buffer || len <= 0) {
        ggpo_local_set_err(err, err_cap, "missing load_game_state buffer");
        return 0;
    }
    return ggpo_ext_load_game_state(buffer, (size_t)len, err, err_cap);
}

static void local_free_buffer(void* buffer) {
    free(buffer);
}

static int local_advance_frame(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap) {
    return ggpo_ext_advance_frame(inputs, arg0, out_checksum, err, err_cap);
}

static void ggpo_local_init_callbacks(GgpoLocalCallbacks* callbacks) {
    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->begin_game = local_begin_game;
    callbacks->save_game_state = local_save_game_state;
    callbacks->load_game_state = local_load_game_state;
    callbacks->free_buffer = local_free_buffer;
    callbacks->advance_frame = local_advance_frame;
}

int ggpo_local_active(void) {
    return g_local.active ? 1 : 0;
}

uint32_t ggpo_local_frame_count(void) {
    return g_local.frame;
}

uint32_t ggpo_local_last_checksum(void) {
    return g_local.last_checksum;
}

size_t ggpo_local_state_size(void) {
    return g_local.state_size;
}

void ggpo_local_stop(void) {
    memset(&g_local, 0, sizeof(g_local));
}

int ggpo_local_start(char* err, size_t err_cap) {
    unsigned char* initial_state = NULL;
    int initial_len = 0;
    int initial_checksum = 0;

    if (g_local.active) return 1;

    memset(&g_local, 0, sizeof(g_local));
    g_local.state_size = ggpo_ext_game_state_size();
    if (g_local.state_size == 0) {
        ggpo_local_set_err(err, err_cap, "game state unavailable");
        return 0;
    }

    ggpo_local_init_callbacks(&g_local.callbacks);
    if (!g_local.callbacks.begin_game("eggnoggplus")) {
        ggpo_local_set_err(err, err_cap, "begin_game callback failed");
        ggpo_local_stop();
        return 0;
    }
    if (!g_local.callbacks.save_game_state(&initial_state, &initial_len, &initial_checksum, 0, err, err_cap)) {
        ggpo_local_stop();
        return 0;
    }
    if (!g_local.callbacks.load_game_state(initial_state, initial_len, err, err_cap)) {
        g_local.callbacks.free_buffer(initial_state);
        ggpo_local_stop();
        return 0;
    }
    g_local.callbacks.free_buffer(initial_state);

    g_local.last_checksum = (uint32_t)initial_checksum;
    g_local.active = 1;
    return 1;
}

int ggpo_local_advance(uint32_t raw_p0, uint32_t raw_p1, int arg0, uint32_t* out_checksum, char* err, size_t err_cap) {
    GgpoFrameInputs inputs;
    uint32_t checksum = 0;

    if (!g_local.active) {
        ggpo_local_set_err(err, err_cap, "local session is not active");
        return 0;
    }

    inputs.player_cmd[0] = raw_p0;
    inputs.player_cmd[1] = raw_p1;
    if (!g_local.callbacks.advance_frame(&inputs, arg0, &checksum, err, err_cap)) {
        return 0;
    }

    g_local.last_checksum = checksum;
    g_local.frame++;
    if (out_checksum) *out_checksum = checksum;
    return 1;
}
