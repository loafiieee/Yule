#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "ggpo_ext.h"
#include "ggpo_net.h"
#include "hooks.h"
#include "lua_manager.h"

#define TEST_STATE_BYTES 16384u
#define TEST_DEFAULT_STATE_BYTES 8192u
#define TEST_HOST_BOOTSTRAP_STATE_BYTES 1024u
#define TEST_JOIN_BOOTSTRAP_STATE_BYTES 2048u
#define TEST_PALETTE_COUNT 44u
#define TEST_HOST_PALETTE_SKIN 17u
#define TEST_HOST_PALETTE_CLOTHING 27u
#define TEST_JOIN_PALETTE_SKIN 31u
#define TEST_JOIN_PALETTE_CLOTHING 40u
#define TEST_FINAL_STATE_BYTES 12288u
#define TEST_ALT_FINAL_STATE_BYTES 14336u
#define TEST_FINAL_STATE_PROBE_OFFSET 10000u
#define TEST_GAME_WIDTH_OFFSET 32u
#define TEST_GAME_HEIGHT_OFFSET 36u
#define TEST_CAMERA_SHAKE_OFFSET 40u
#define TEST_CAMERA_SHAKE_DECAY_OFFSET 44u
#define TEST_AUTHORITATIVE_GAME_WIDTH 288.0f
#define TEST_AUTHORITATIVE_GAME_HEIGHT 160.0f
#define TEST_AUTHORITATIVE_CAMERA_SHAKE 5.0f
#define TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY 0.95f
#define TEST_LAYOUT_GENERATION 0x4C415931u
#define TEST_MISMATCH_LAYOUT_GENERATION 0x4C415932u
#define TEST_SKEW_INPUT_DELAY 4u
#define TEST_SKEW_SERVICE_TICKS 200
#define TEST_SKEW_TARGET_FRAME 40u
#define TEST_SAMPLING_SERVICE_TICKS 240
#define TEST_SAMPLING_STALE_TICKS 32
#define TEST_SAMPLING_TARGET_FRAME 40u
#define TEST_SAMPLING_STALE_CMD 0x40u
#define TEST_SAMPLING_FRESH_CMD 0x80u
#define TEST_CHAOS_FRAME_COUNT 2048u
#define TEST_WRAP_CHAOS_START (UINT32_MAX - 1023u)
#define TEST_MATCH_TOKEN \
    "00112233445566778899aabbccddeeff" \
    "fedcba98765432100123456789abcdef"

static uint8_t g_state[TEST_STATE_BYTES];
static size_t g_state_size = TEST_DEFAULT_STATE_BYTES;
static uint32_t g_layout_generation = TEST_LAYOUT_GENERATION;
static uint32_t g_seed;
static int g_reject_noncanonical_transport;
static int g_fail_load_remaining;
static int g_corrupt_load_once;
static int g_fail_raw_load_once;
static uint32_t g_rollback_load_calls;
static uint32_t g_raw_load_calls;
static uint32_t g_raw_save_calls;
static float g_game_width;
static float g_game_height;
static float g_camera_shake;
static float g_camera_shake_decay;
static float g_last_saved_game_width;
static float g_last_saved_game_height;
static float g_last_saved_camera_shake;
static float g_last_saved_camera_shake_decay;
static uint32_t g_width_set_calls;
static int g_sync_game_width;
static int g_require_pinned_width;
static int g_fail_width_read_on_call;
static LuaGamePaletteState g_palette;
static LuaGamePaletteState g_expected_palette;

static int test_float_near(float a, float b) {
    float delta = a - b;
    if (delta < 0.0f) delta = -delta;
    return delta <= 0.000001f;
}

static int test_frame_after(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

static void test_palette_set_sentinel(void) {
    int i;
    memset(&g_palette, 0, sizeof(g_palette));
    g_palette.pending = 1;
    g_palette.lerp_time = 17;
    for (i = 0; i < LUA_GAME_PALETTE_FLOATS; i++) {
        g_palette.colours[i] = 0.01f * (float)(i + 1);
    }
    g_expected_palette = g_palette;
}

static int test_palette_matches(void) {
    return memcmp(&g_palette, &g_expected_palette, sizeof(g_palette)) == 0;
}

static int test_trace_confirmed_history(FILE* trace,
                                        uint32_t start_frame,
                                        uint32_t frame_count,
                                        uint32_t* next_offset,
                                        uint8_t* scratch,
                                        size_t scratch_cap) {
    uint32_t confirmed;
    uint32_t confirmed_offset;
    uint32_t stop_offset;
    if (!trace || !next_offset || !scratch || scratch_cap == 0u ||
        scratch_cap > UINT32_MAX || frame_count == 0u) {
        return 0;
    }
    if (!ggpo_net_checksum_confirmed_frame(&confirmed) ||
        *next_offset >= frame_count ||
        (int32_t)(confirmed - start_frame) < 0) {
        return 1;
    }
    confirmed_offset = confirmed - start_frame;
    stop_offset = confirmed_offset;
    if (stop_offset >= frame_count) stop_offset = frame_count - 1u;
    while (*next_offset <= stop_offset) {
        uint32_t header[3];
        uint32_t frame = start_frame + *next_offset;
        size_t state_len = 0u;
        uint32_t post_checksum = 0u;
        if (!ggpo_net_test_copy_history_state(frame,
                                              scratch,
                                              scratch_cap,
                                              &state_len,
                                              &post_checksum) ||
            state_len == 0u || state_len > UINT32_MAX) {
            return 0;
        }
        header[0] = frame;
        header[1] = (uint32_t)state_len;
        header[2] = post_checksum;
        if (fwrite(header, sizeof(header), 1u, trace) != 1u ||
            fwrite(scratch, state_len, 1u, trace) != 1u) {
            return 0;
        }
        (*next_offset)++;
    }
    return 1;
}

static uint32_t test_checksum(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void test_state_store_game_geometry(void) {
    if (!g_sync_game_width ||
        TEST_CAMERA_SHAKE_DECAY_OFFSET + sizeof(g_camera_shake_decay) >
            g_state_size) return;
    memcpy(g_state + TEST_GAME_WIDTH_OFFSET,
           &g_game_width,
           sizeof(g_game_width));
    memcpy(g_state + TEST_GAME_HEIGHT_OFFSET,
           &g_game_height,
           sizeof(g_game_height));
    memcpy(g_state + TEST_CAMERA_SHAKE_OFFSET,
           &g_camera_shake,
           sizeof(g_camera_shake));
    memcpy(g_state + TEST_CAMERA_SHAKE_DECAY_OFFSET,
           &g_camera_shake_decay,
           sizeof(g_camera_shake_decay));
}

static void test_state_load_game_geometry(void) {
    if (!g_sync_game_width ||
        TEST_CAMERA_SHAKE_DECAY_OFFSET + sizeof(g_camera_shake_decay) >
            g_state_size) return;
    memcpy(&g_game_width,
           g_state + TEST_GAME_WIDTH_OFFSET,
           sizeof(g_game_width));
    memcpy(&g_game_height,
           g_state + TEST_GAME_HEIGHT_OFFSET,
           sizeof(g_game_height));
    memcpy(&g_camera_shake,
           g_state + TEST_CAMERA_SHAKE_OFFSET,
           sizeof(g_camera_shake));
    memcpy(&g_camera_shake_decay,
           g_state + TEST_CAMERA_SHAKE_DECAY_OFFSET,
           sizeof(g_camera_shake_decay));
}

size_t ggpo_ext_game_state_size(void) { return g_state_size; }

uint32_t ggpo_ext_game_state_layout_fingerprint(void) {
    return test_checksum(&g_layout_generation, sizeof(g_layout_generation));
}

int ggpo_ext_save_game_state(void* dst, size_t dst_len, size_t* out_len,
                             uint32_t* out_checksum, char* err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (!dst || dst_len < g_state_size) return 0;
    test_state_store_game_geometry();
    g_last_saved_game_width = g_game_width;
    g_last_saved_game_height = g_game_height;
    g_last_saved_camera_shake = g_camera_shake;
    g_last_saved_camera_shake_decay = g_camera_shake_decay;
    memcpy(dst, g_state, g_state_size);
    if (out_len) *out_len = g_state_size;
    if (out_checksum) *out_checksum = test_checksum(g_state, g_state_size);
    return 1;
}

int ggpo_ext_save_game_state_raw(void* dst, size_t dst_len, size_t* out_len,
                                 char* err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    g_raw_save_calls++;
    if (!dst || dst_len < g_state_size) return 0;
    test_state_store_game_geometry();
    memcpy(dst, g_state, g_state_size);
    if (out_len) *out_len = g_state_size;
    return 1;
}

int ggpo_ext_validate_rollback_blob(const void* src, size_t src_len,
                                    uint32_t* out_checksum,
                                    char* err, size_t err_cap) {
    if (!src || src_len != g_state_size || !out_checksum) {
        if (err && err_cap) snprintf(err, err_cap, "invalid rollback blob");
        return 0;
    }
    *out_checksum = test_checksum(src, src_len);
    return 1;
}

int ggpo_ext_validate_rollback_transport_blob(const void* src, size_t src_len,
                                              uint32_t* out_checksum,
                                              char* err, size_t err_cap) {
    if (g_reject_noncanonical_transport) {
        if (err && err_cap) snprintf(err, err_cap, "noncanonical rollback transport blob");
        return 0;
    }
    return ggpo_ext_validate_rollback_blob(src, src_len, out_checksum, err, err_cap);
}

int ggpo_ext_load_game_state_raw(const void* src, size_t src_len,
                                 char* err, size_t err_cap) {
    g_raw_load_calls++;
    if (!src || src_len != g_state_size) return 0;
    if (g_fail_raw_load_once) {
        g_fail_raw_load_once = 0;
        if (err && err_cap) snprintf(err, err_cap, "injected raw restore failure");
        return 0;
    }
    memcpy(g_state, src, g_state_size);
    test_state_load_game_geometry();
    return 1;
}

int ggpo_ext_load_game_state(const void* src, size_t src_len, char* err, size_t err_cap) {
    if (!src || src_len != g_state_size) return 0;
    g_rollback_load_calls++;
    /* Model production rollback canonicalization scrubbing the palette control
     * fields. Active-match rollback/correction must restore the peer-local
     * coherent sidecar around this load; prematch intentionally does not. */
    memset(&g_palette, 0, sizeof(g_palette));
    if (g_fail_load_remaining > 0) {
        g_fail_load_remaining--;
        memcpy(g_state, src, g_state_size);
        test_state_load_game_geometry();
        g_state[0] ^= 0x5Au;
        if (err && err_cap) snprintf(err, err_cap, "injected rollback load failure");
        return 0;
    }
    memcpy(g_state, src, g_state_size);
    test_state_load_game_geometry();
    if (g_corrupt_load_once) {
        g_corrupt_load_once = 0;
        g_state[0] ^= 0xA5u;
    }
    return 1;
}

int ggpo_ext_advance_frame(const GgpoFrameInputs* inputs, int arg0,
                           uint32_t* out_checksum, char* err, size_t err_cap) {
    uint32_t v;
    (void)arg0;
    if (!inputs) return 0;
    if (g_require_pinned_width &&
        (g_game_width != TEST_AUTHORITATIVE_GAME_WIDTH ||
         g_game_height != TEST_AUTHORITATIVE_GAME_HEIGHT ||
         !test_float_near(g_camera_shake,
                          TEST_AUTHORITATIVE_CAMERA_SHAKE) ||
         !test_float_near(g_camera_shake_decay,
                          TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY))) {
        if (err && err_cap) {
            snprintf(err, err_cap,
                     "clean simulation state was not restored before native tick");
        }
        return 0;
    }
    memcpy(&v, g_state + 16, sizeof(v));
    /* Order-sensitive deterministic recurrence: swapped, omitted, duplicated,
     * or mis-framed inputs cannot accidentally converge as they could under the
     * old additive fixture. The value lives in rollback state, so replay rewinds
     * and recomputes it exactly. */
    v = (v * 16777619u) ^ (inputs->player_cmd[0] * 0x9E3779B1u) ^
        (inputs->player_cmd[1] * 0x85EBCA77u) ^ 0xA5A5A5A5u;
    memcpy(g_state + 16, &v, sizeof(v));
    test_state_store_game_geometry();
    if (out_checksum) *out_checksum = test_checksum(g_state, g_state_size);
    return 1;
}

int lua_manager_game_state_rollback_checksum(uint32_t* out_crc, char* err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (out_crc) *out_crc = test_checksum(g_state, g_state_size);
    return 1;
}

int lua_manager_game_state_save(void* dst, size_t dst_len, size_t* out_len,
                                char* err, size_t err_cap) {
    return ggpo_ext_save_game_state(
        dst, dst_len, out_len, NULL, err, err_cap);
}

int lua_manager_game_state_canonicalize_rollback(
    void* blob, size_t blob_len, char* err, size_t err_cap) {
    (void)blob;
    (void)blob_len;
    (void)err;
    (void)err_cap;
    return 1;
}

int lua_manager_game_state_analyze_canonical_rollback_blob(
    const void* blob, size_t blob_len, uint32_t* out_crc,
    LuaGameStateRollbackSummary* out_summary,
    char* err, size_t err_cap) {
    if (!blob || blob_len != g_state_size || !out_crc) {
        if (err && err_cap) snprintf(err, err_cap, "invalid canonical rollback blob");
        return 0;
    }
    *out_crc = test_checksum(blob, blob_len);
    if (out_summary) memset(out_summary, 0, sizeof(*out_summary));
    return 1;
}

int lua_manager_game_state_rollback_summary(LuaGameStateRollbackSummary* out,
                                            char* err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (out) memset(out, 0, sizeof(*out));
    return 1;
}

int lua_manager_game_rng_seed(uint32_t* out_seed) {
    if (out_seed) *out_seed = g_seed;
    return 1;
}

int lua_manager_game_set_rng_seed(uint32_t seed) { g_seed = seed; return 1; }
int lua_manager_game_camera(float* x, float* y) { if (x) *x = 0.0f; if (y) *y = 0.0f; return 1; }
int lua_manager_game_set_camera(float x, float y) { (void)x; (void)y; return 1; }
int lua_manager_game_camera_shake(float* out_shake, float* out_decay) {
    if (!out_shake || !out_decay) return 0;
    *out_shake = g_camera_shake;
    *out_decay = g_camera_shake_decay;
    return 1;
}
int lua_manager_game_palette_capture(LuaGamePaletteState* out_state) {
    if (!out_state) return 0;
    *out_state = g_palette;
    return 1;
}
int lua_manager_game_palette_restore(const LuaGamePaletteState* state) {
    if (!state) return 0;
    g_palette = *state;
    return 1;
}
int lua_manager_game_width(float* out_width) {
    if (g_fail_width_read_on_call > 0) {
        g_fail_width_read_on_call--;
        if (g_fail_width_read_on_call == 0) return 0;
    }
    if (!out_width || g_game_width <= 0.0f) return 0;
    *out_width = g_game_width;
    return 1;
}
int lua_manager_game_set_width(float width) {
    if (width <= 0.0f) return 0;
    g_game_width = width;
    test_state_store_game_geometry();
    g_width_set_calls++;
    return 1;
}
int lua_manager_game_height(float* out_height) {
    if (!out_height || g_game_height <= 0.0f) return 0;
    *out_height = g_game_height;
    return 1;
}
int lua_manager_game_set_height(float height) {
    if (height <= 0.0f) return 0;
    g_game_height = height;
    test_state_store_game_geometry();
    g_width_set_calls++;
    return 1;
}
int lua_manager_game_set_geometry(float game_w, float game_h) {
    if (game_w <= 0.0f || game_h <= 0.0f) return 0;
    g_game_width = game_w;
    g_game_height = game_h;
    test_state_store_game_geometry();
    g_width_set_calls++;
    return 1;
}
int lua_manager_game_set_sim_state(uint32_t seed,
                                   float camera_x,
                                   float camera_y,
                                   float camera_shake,
                                   float camera_shake_decay,
                                   float game_w,
                                   float game_h) {
    (void)camera_x;
    (void)camera_y;
    if (game_w <= 0.0f || game_h <= 0.0f) return 0;
    g_seed = seed;
    g_camera_shake = camera_shake;
    g_camera_shake_decay = camera_shake_decay;
    g_game_width = game_w;
    g_game_height = game_h;
    test_state_store_game_geometry();
    g_width_set_calls++;
    return 1;
}
void lua_manager_game_dump_tilemap_blob(int player, unsigned int frame,
                                        const void* blob, size_t blob_len) {
    (void)player; (void)frame; (void)blob; (void)blob_len;
}

void hooks_rng_trace_begin(uint32_t frame, uint32_t phase) { (void)frame; (void)phase; }
void hooks_rng_trace_end(void) {}
void hooks_rng_trace_copy(HooksRngTrace* out) { if (out) memset(out, 0, sizeof(*out)); }
int hooks_set_native_synth_enabled(int enabled) { return enabled; }
void hooks_waterfall_audio_save(void) {}
void hooks_waterfall_audio_restore(void) {}

void log_write(const char* level, const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, "[%s] ", level ? level : "?");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void log_dump_line(const char* fmt, ...) { (void)fmt; }
void log_dump_flush(void) {}

static int fail(const char* role, const char* what, const char* err) {
    fprintf(stderr, "%s FAIL: %s%s%s\n", role, what,
            (err && err[0]) ? " - " : "", (err && err[0]) ? err : "");
    ggpo_net_stop();
    return 1;
}

static int test_local_render_geometry_matches(int is_host) {
    return g_game_width == (is_host ? 304.0f : 320.0f) &&
           g_game_height == (is_host ? 171.0f : 180.0f);
}

#ifdef GGPO_NET_TEST
static int frame_ring_fail(const char* what) {
    fprintf(stderr, "frame-ring FAIL: %s\n", what);
    return 1;
}

static void frame_ring_set_ack(uint32_t bits[GGPO_NET_TEST_ACK_WORDS],
                               uint32_t index) {
    bits[index / 32u] |= 1u << (index % 32u);
}

static int run_frame_ring_test(void) {
    const uint32_t remote_lead_limit =
        (GGPO_NET_MAX_PREDICTION_LIMIT > GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT)
            ? GGPO_NET_MAX_PREDICTION_LIMIT
            : GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT;
    const uint32_t future_limit = remote_lead_limit + GGPO_NET_MAX_INPUT_DELAY;
    const uint32_t retention_limit = 512u - future_limit - 1u;
    uint32_t frame = 0u;
    uint32_t cmd = 0u;
    uint32_t marker = 0u;
    uint32_t valid = 0u;
    uint32_t ack_frame = 0u;
    uint32_t horizon = 0u;
    uint32_t remote_checksum_next = 0u;
    uint32_t peer_checksum_next = 0u;
    uint32_t packet_count = 0u;
    uint32_t ack_bits[GGPO_NET_TEST_ACK_WORDS];
    uint32_t packet_frames[GGPO_NET_TEST_PACKET_INPUTS];
    uint32_t packet_cmds[GGPO_NET_TEST_PACKET_INPUTS];
    uint32_t checksum_frames[GGPO_NET_TEST_PACKET_CHECKSUMS];
    uint32_t checksum_values[GGPO_NET_TEST_PACKET_CHECKSUMS];

    /* The two accepted sides of the receive window occupy one complete ring
     * generation. The immediately adjacent aliases must be rejected. */
    ggpo_net_test_reset_frame_rings(700u);
    if (!ggpo_net_test_receive_remote_input(700u + future_limit, 0xA1u)) {
        return frame_ring_fail("future admission boundary was rejected");
    }
    if (ggpo_net_test_receive_remote_input(700u + future_limit + 1u, 0xA2u)) {
        return frame_ring_fail("far-future input was admitted");
    }
    if (!ggpo_net_test_receive_remote_input(700u - retention_limit, 0xB1u)) {
        return frame_ring_fail("retention boundary was rejected");
    }
    if (ggpo_net_test_receive_remote_input(700u - retention_limit - 1u, 0xB2u)) {
        return frame_ring_fail("expired input was admitted");
    }
    if (!ggpo_net_test_get_remote_input(700u + future_limit, &cmd) || cmd != 0xA1u) {
        return frame_ring_fail("rejected alias damaged the admitted future input");
    }

    /* Advancing exactly one 512-slot generation may replace the old slot; a
     * delayed copy of the old generation may not replace it back. */
    ggpo_net_test_reset_frame_rings(188u);
    if (!ggpo_net_test_receive_remote_input(188u, 0x11u)) {
        return frame_ring_fail("initial generation was rejected");
    }
    ggpo_net_test_set_local_frame(700u);
    if (!ggpo_net_test_receive_remote_input(700u, 0x22u)) {
        return frame_ring_fail("new input generation did not replace the expired slot");
    }
    if (ggpo_net_test_receive_remote_input(188u, 0x33u)) {
        return frame_ring_fail("delayed input generation overwrote a newer slot");
    }
    if (!ggpo_net_test_get_remote_input(700u, &cmd) || cmd != 0x22u ||
        ggpo_net_test_get_remote_input(188u, NULL)) {
        return frame_ring_fail("input generation marker was not preserved");
    }

    /* Rollback history uses the same generation rule even when exercised
     * independently of receive admission. */
    ggpo_net_test_reset_frame_rings(700u);
    if (!ggpo_net_test_store_history_marker(700u, 0xC1u) ||
        ggpo_net_test_store_history_marker(188u, 0xC2u) ||
        !ggpo_net_test_get_history_marker(700u, &marker) || marker != 0xC1u) {
        return frame_ring_fail("history generation guard failed");
    }

    /* Redundant packet entries arrive newest-to-oldest. An older entry must not
     * regress the newest-command marker, and prediction must use the nearest
     * known command at/before the requested frame rather than future input. */
    ggpo_net_test_reset_frame_rings(100u);
    if (!ggpo_net_test_receive_remote_input(108u, 0x08u) ||
        !ggpo_net_test_receive_remote_input(99u, 0x99u) ||
        !ggpo_net_test_latest_remote_input(&frame, &cmd) ||
        frame != 108u || cmd != 0x08u ||
        ggpo_net_test_predict_remote_input(100u) != 0x99u) {
        return frame_ring_fail("out-of-order input changed prediction provenance");
    }

    /* Remote progress and receive admission must preserve uint32 serial order
     * across UINT32_MAX -> 0. */
    ggpo_net_test_reset_frame_rings(UINT32_MAX - 3u);
    if (!ggpo_net_test_receive_remote_frame(UINT32_MAX - 1u)) {
        return frame_ring_fail("pre-wrap remote progress was rejected");
    }
    ggpo_net_test_set_local_frame(1u);
    if (!ggpo_net_test_receive_remote_frame(2u) ||
        !ggpo_net_test_receive_remote_frame(UINT32_MAX - 2u) ||
        ggpo_net_remote_frame_count() != 2u) {
        return frame_ring_fail("remote progress regressed at uint32 wrap");
    }
    if (ggpo_net_test_receive_remote_frame(1u + future_limit + 1u) ||
        ggpo_net_remote_frame_count() != 2u) {
        return frame_ring_fail("far-future remote progress was admitted");
    }

    /* Prediction-window scans must neither stop at zero nor loop forever once
     * the local frame counter has wrapped. Before the first wrap, frame 1 has
     * only one frame of history; after a wrap it has the full prediction span. */
    ggpo_net_test_reset_frame_rings(1u);
    if (ggpo_net_test_prediction_stall_needed(1u, &frame)) {
        return frame_ring_fail("pre-wrap prediction scan invented prior history");
    }
    ggpo_net_test_reset_frame_rings(UINT32_MAX - 3u);
    ggpo_net_test_set_local_frame(1u);
    if (!ggpo_net_test_prediction_stall_needed(1u, &frame) ||
        (uint32_t)(1u - frame) != 24u) {
        return frame_ring_fail("prediction scan did not cross uint32 wrap");
    }

    /* A newer exact input does not make an earlier hole recoverable forever.
     * The hard prediction barrier keys off the oldest gap, not only the current
     * frame's presence. */
    ggpo_net_test_reset_frame_rings(25u);
    if (!ggpo_net_test_receive_remote_input(0u, 0x10u)) {
        return frame_ring_fail("failed to seed hard-stall frame zero");
    }
    for (uint32_t i = 2u; i <= 25u; i++) {
        if (!ggpo_net_test_receive_remote_input(i, 0x10u + i)) {
            return frame_ring_fail("failed to seed hard-stall out-of-order inputs");
        }
    }
    if (!ggpo_net_test_prediction_stall_needed(25u, &frame) || frame != 1u) {
        return frame_ring_fail("exact current input bypassed an older prediction hole");
    }
    if (!ggpo_net_test_receive_remote_input(1u, 0x11u) ||
        ggpo_net_test_prediction_stall_needed(25u, &frame)) {
        return frame_ring_fail("closed prediction hole did not release hard stall");
    }

    /* Contiguous confirmation must not jump over reordered holes. Once frame 0
     * arrives it folds the already received 1/2 SACKs into one monotonic base. */
    ggpo_net_test_reset_frame_rings(3u);
    if (!ggpo_net_test_receive_remote_input(2u, 0x12u) ||
        !ggpo_net_test_receive_remote_input(1u, 0x11u) ||
        !ggpo_net_test_get_remote_ack(&valid, &ack_frame, ack_bits) ||
        valid != 0u || ack_frame != 0u ||
        (ack_bits[0] & 0x6u) != 0x6u || (ack_bits[0] & 0x1u) != 0u) {
        return frame_ring_fail("reordered input crossed a contiguous hole");
    }
    if (!ggpo_net_test_receive_remote_input(0u, 0x10u) ||
        !ggpo_net_test_get_remote_ack(&valid, &ack_frame, ack_bits) ||
        valid != 1u || ack_frame != 2u) {
        return frame_ring_fail("closing frame-zero hole did not advance contiguous ACK");
    }
    if (ggpo_net_test_receive_remote_input(1u, 0xDEADu) ||
        !ggpo_net_test_get_remote_input(1u, &cmd) || cmd != 0x11u) {
        return frame_ring_fail("conflicting same-frame input replaced its first value");
    }

    /* The SACK covers the complete 512-frame admitted generation, including the
     * last bit that the old newest-64 resend policy could never describe. */
    ggpo_net_test_reset_frame_rings(283u);
    if (!ggpo_net_test_receive_remote_input(511u, 0x511u) ||
        !ggpo_net_test_get_remote_ack(&valid, &ack_frame, ack_bits) ||
        valid != 0u || (ack_bits[15] & 0x80000000u) == 0u) {
        return frame_ring_fail("full-ring SACK boundary was not represented");
    }

    /* Keep the newest committed command live while explicitly recovering a sole
     * hole 215 frames behind it. Reordered ACKs may never regress the base. */
    ggpo_net_test_reset_frame_rings(220u);
    for (uint32_t i = 0u; i <= 220u; i++) {
        if (!ggpo_net_test_store_local_input(i, 0x1000u + i)) {
            return frame_ring_fail("failed to seed local selective-resend inputs");
        }
    }
    memset(ack_bits, 0, sizeof(ack_bits));
    for (uint32_t i = 1u; i < 220u; i++) {
        if (i != 5u) frame_ring_set_ack(ack_bits, i - 1u);
    }
    if (!ggpo_net_test_apply_peer_ack(1u, 0u, ack_bits) ||
        !ggpo_net_test_get_peer_ack(&valid, &ack_frame) ||
        valid != 1u || ack_frame != 4u) {
        return frame_ring_fail("peer SACK did not fold into a monotonic base");
    }
    packet_count = ggpo_net_test_build_input_packet(packet_frames,
                                                     packet_cmds,
                                                     GGPO_NET_TEST_PACKET_INPUTS);
    if (packet_count != 2u || packet_frames[0] != 220u ||
        packet_frames[1] != 5u || packet_cmds[1] != 0x1005u) {
        return frame_ring_fail("selective resend did not prioritize live edge and old hole");
    }
    memset(ack_bits, 0, sizeof(ack_bits));
    if (!ggpo_net_test_apply_peer_ack(1u, 0u, ack_bits) ||
        !ggpo_net_test_get_peer_ack(&valid, &ack_frame) || ack_frame != 4u) {
        return frame_ring_fail("reordered cumulative ACK regressed peer state");
    }
    if (!ggpo_net_test_apply_peer_ack(1u, 220u, ack_bits) ||
        ggpo_net_test_build_input_packet(packet_frames, packet_cmds,
                                         GGPO_NET_TEST_PACKET_INPUTS) != 0u) {
        return frame_ring_fail("fully acknowledged local inputs were retransmitted");
    }

    /* A cumulative ACK is a proof for every lower committed frame, not merely
     * its endpoint. Reject impossible jumps transactionally. */
    ggpo_net_test_reset_frame_rings(5u);
    memset(ack_bits, 0, sizeof(ack_bits));
    if (!ggpo_net_test_store_local_input(5u, 0x55u) ||
        ggpo_net_test_apply_peer_ack(1u, 5u, ack_bits) ||
        ggpo_net_test_get_peer_ack(&valid, &ack_frame)) {
        return frame_ring_fail("cumulative ACK skipped uncommitted local inputs");
    }

    /* A published checksum horizon is derived from the sender's cumulative
     * receipt of our inputs. It therefore requires a valid input ACK, cannot
     * exceed that ACK base, and must name an already simulated frame. */
    ggpo_net_test_reset_frame_rings(4u);
    for (uint32_t i = 0u; i <= 4u; i++) {
        if (!ggpo_net_test_store_local_input(i, 0x60u + i)) {
            return frame_ring_fail("failed to seed checksum-envelope inputs");
        }
    }
    if (!ggpo_net_test_validate_checksum_envelope(1u, 3u, 1u, 2u, 4u) ||
        ggpo_net_test_validate_checksum_envelope(0u, 0u, 1u, 0u, 4u) ||
        ggpo_net_test_validate_checksum_envelope(1u, 1u, 1u, 2u, 4u) ||
        ggpo_net_test_validate_checksum_envelope(1u, 4u, 1u, 4u, 4u)) {
        return frame_ring_fail("checksum confirmation envelope invariants failed");
    }

    /* An unacknowledged local generation is never silently overwritten. Once
     * frame 0 is cumulatively acknowledged, its modulo slot may be reused. */
    ggpo_net_test_reset_frame_rings(0u);
    if (!ggpo_net_test_store_local_input(0u, 0x20u) ||
        ggpo_net_test_store_local_input(512u, 0x21u)) {
        return frame_ring_fail("unacknowledged local input retirement was allowed");
    }
    memset(ack_bits, 0, sizeof(ack_bits));
    if (!ggpo_net_test_apply_peer_ack(1u, 0u, ack_bits) ||
        !ggpo_net_test_store_local_input(512u, 0x21u)) {
        return frame_ring_fail("acknowledged local input generation was not reusable");
    }

    /* Individually exact later frames cannot publish checksums across an earlier
     * input hole. Closing it with the predicted value unlocks the whole stable
     * history; a pending replay closes the horizon again. */
    ggpo_net_test_reset_frame_rings(6u);
    for (uint32_t i = 0u; i <= 6u; i++) {
        if (!ggpo_net_test_store_local_input(i, 0x2000u + i) ||
            !ggpo_net_test_seed_history(i, i == 1u ? 0x31u : 0x30u + i,
                                        i == 1u, 0x3000u + i)) {
            return frame_ring_fail("failed to seed checksum-horizon fixture");
        }
    }
    if (!ggpo_net_test_receive_remote_input(0u, 0x30u)) {
        return frame_ring_fail("failed to seed contiguous checksum input");
    }
    for (uint32_t i = 2u; i <= 5u; i++) {
        if (!ggpo_net_test_receive_remote_input(i, 0x30u + i)) {
            return frame_ring_fail("failed to seed out-of-order checksum input");
        }
    }
    memset(ack_bits, 0, sizeof(ack_bits));
    if (!ggpo_net_test_apply_peer_ack(1u, 5u, ack_bits) ||
        !ggpo_net_test_checksum_horizon(&horizon) || horizon != 0u) {
        return frame_ring_fail("checksum horizon crossed an input hole");
    }
    packet_count = ggpo_net_test_build_checksum_packet(checksum_frames,
                                                        checksum_values,
                                                        GGPO_NET_TEST_PACKET_CHECKSUMS);
    if (packet_count != 1u || checksum_frames[0] != 0u ||
        checksum_values[0] != 0x3000u) {
        return frame_ring_fail("checksum publication crossed contiguous horizon");
    }
    if (!ggpo_net_test_receive_remote_input(1u, 0x31u) ||
        !ggpo_net_test_checksum_horizon(&horizon) || horizon != 5u) {
        return frame_ring_fail("matching late input did not finalize contiguous history");
    }
    packet_count = ggpo_net_test_build_checksum_packet(checksum_frames,
                                                        checksum_values,
                                                        GGPO_NET_TEST_PACKET_CHECKSUMS);
    if (packet_count != 6u || checksum_frames[0] != 5u ||
        checksum_frames[1] != 0u || checksum_frames[5] != 4u) {
        return frame_ring_fail("checksum packet did not prioritize live edge then oldest recovery");
    }
    ggpo_net_test_set_rollback_pending(1);
    if (ggpo_net_test_checksum_horizon(&horizon) ||
        ggpo_net_test_build_checksum_packet(checksum_frames, checksum_values,
                                             GGPO_NET_TEST_PACKET_CHECKSUMS) != 0u) {
        return frame_ring_fail("dirty rollback history remained checksum-eligible");
    }

    /* Checksums are a cumulative go-back-N stream: keep the live edge in slot
     * zero, but recover a hole more than the old 32-frame redundancy window
     * behind it. Lost/reordered cumulative ACKs never regress the first-needed
     * frame. */
    ggpo_net_test_reset_frame_rings(81u);
    ggpo_net_test_set_checksum_epoch(1u, 0u, 0u);
    ggpo_net_test_set_remote_contiguous(1u, 80u);
    ggpo_net_test_set_peer_ack(1u, 80u);
    for (uint32_t i = 0u; i <= 80u; i++) {
        if (!ggpo_net_test_seed_history(i, 0x4000u + i, 0,
                                        0x5000u + i)) {
            return frame_ring_fail("failed to seed checksum resend history");
        }
    }
    packet_count = ggpo_net_test_build_checksum_packet(
        checksum_frames, checksum_values, GGPO_NET_TEST_PACKET_CHECKSUMS);
    if (packet_count != GGPO_NET_TEST_PACKET_CHECKSUMS ||
        checksum_frames[0] != 80u || checksum_frames[1] != 0u ||
        checksum_frames[31] != 30u ||
        !ggpo_net_test_apply_peer_checksum_ack(1u, 0u, 31u)) {
        return frame_ring_fail("initial cumulative checksum recovery batch was incorrect");
    }
    packet_count = ggpo_net_test_build_checksum_packet(
        checksum_frames, checksum_values, GGPO_NET_TEST_PACKET_CHECKSUMS);
    if (packet_count != GGPO_NET_TEST_PACKET_CHECKSUMS ||
        checksum_frames[0] != 80u || checksum_frames[1] != 31u ||
        checksum_frames[31] != 61u ||
        !ggpo_net_test_apply_peer_checksum_ack(1u, 0u, 10u)) {
        return frame_ring_fail("checksum recovery did not retain an old cumulative hole");
    }
    ggpo_net_test_get_checksum_ack_next(&remote_checksum_next,
                                         &peer_checksum_next);
    if (peer_checksum_next != 31u) {
        return frame_ring_fail("reordered checksum ACK regressed cumulative state");
    }

    /* A peer cannot jump its cumulative proof over checksums that were never
     * published. Envelope validation is read-only, and direct application must
     * reject the same impossible claim without moving the horizon. */
    ggpo_net_test_reset_frame_rings(81u);
    ggpo_net_test_set_checksum_epoch(2u, 0u, 0u);
    if (!ggpo_net_test_seed_history(80u, 0u, 0, 0x6080u) ||
        !ggpo_net_test_mark_checksum_published(80u) ||
        ggpo_net_test_validate_checksum_ack_envelope(2u, 0u, 81u) ||
        ggpo_net_test_apply_peer_checksum_ack(2u, 0u, 81u)) {
        return frame_ring_fail("impossible checksum ACK jump was accepted");
    }
    ggpo_net_test_get_checksum_ack_next(NULL, &peer_checksum_next);
    if (peer_checksum_next != 0u) {
        return frame_ring_fail("rejected checksum ACK mutated peer progress");
    }

    /* The existing state/correction tuple scopes checksum metadata. A delayed
     * prior-correction ACK is structurally valid but semantically inert. */
    ggpo_net_test_reset_frame_rings(101u);
    ggpo_net_test_set_checksum_epoch(3u, 7u, 100u);
    if (!ggpo_net_test_seed_history(100u, 0u, 0, 0x7100u) ||
        !ggpo_net_test_mark_checksum_published(100u) ||
        !ggpo_net_test_validate_checksum_ack_envelope(3u, 6u, 101u) ||
        !ggpo_net_test_apply_peer_checksum_ack(3u, 6u, 101u)) {
        return frame_ring_fail("stale checksum generation was not quarantined");
    }
    ggpo_net_test_get_checksum_ack_next(NULL, &peer_checksum_next);
    if (peer_checksum_next != 100u ||
        !ggpo_net_test_validate_checksum_ack_envelope(3u, 7u, 101u) ||
        !ggpo_net_test_apply_peer_checksum_ack(3u, 7u, 101u)) {
        return frame_ring_fail("current checksum generation did not advance");
    }

    /* `ack_next` has no sentinel, so a correction stream can cross uint32 wrap
     * without confusing "nothing received" with a real frame number. */
    ggpo_net_test_reset_frame_rings(2u);
    ggpo_net_test_set_checksum_epoch(9u, 4u, UINT32_MAX - 2u);
    for (uint32_t i = 0u; i < 5u; i++) {
        uint32_t wrapped_frame = (UINT32_MAX - 2u) + i;
        if (!ggpo_net_test_seed_history(wrapped_frame, 0u, 0,
                                        0x8000u + i) ||
            !ggpo_net_test_mark_checksum_published(wrapped_frame)) {
            return frame_ring_fail("failed to seed wrapped checksum generation");
        }
    }
    if (!ggpo_net_test_apply_peer_checksum_ack(9u, 4u, 2u) ||
        !ggpo_net_test_apply_peer_checksum_ack(9u, 4u, UINT32_MAX)) {
        return frame_ring_fail("checksum ACK failed across uint32 wrap");
    }
    ggpo_net_test_get_checksum_ack_next(NULL, &peer_checksum_next);
    if (peer_checksum_next != 2u) {
        return frame_ring_fail("wrapped reordered checksum ACK regressed");
    }

    /* ACK only after a successful comparison. Out-of-order matches remain
     * cached, rollback keeps receipt pending, and history retirement requires
     * proof in both directions. */
    ggpo_net_test_reset_frame_rings(6u);
    ggpo_net_test_set_checksum_epoch(1u, 0u, 0u);
    ggpo_net_test_set_remote_contiguous(1u, 5u);
    ggpo_net_test_set_peer_ack(1u, 5u);
    for (uint32_t i = 0u; i <= 5u; i++) {
        if (!ggpo_net_test_seed_history(i, 0u, 0, 0x9000u + i) ||
            !ggpo_net_test_mark_checksum_published(i)) {
            return frame_ring_fail("failed to seed checksum comparison history");
        }
    }
    if (ggpo_net_test_receive_remote_checksum(5u, 0x9005u) != 1) {
        return frame_ring_fail("out-of-order checksum could not be compared");
    }
    ggpo_net_test_get_checksum_ack_next(&remote_checksum_next,
                                         &peer_checksum_next);
    if (remote_checksum_next != 0u ||
        ggpo_net_test_checksum_history_retirable(0u)) {
        return frame_ring_fail("checksum ACK crossed an earlier receipt hole");
    }
    for (uint32_t i = 0u; i < 5u; i++) {
        if (ggpo_net_test_receive_remote_checksum(i, 0x9000u + i) != 1) {
            return frame_ring_fail("contiguous checksum comparison failed");
        }
    }
    ggpo_net_test_get_checksum_ack_next(&remote_checksum_next,
                                         &peer_checksum_next);
    if (remote_checksum_next != 6u ||
        ggpo_net_test_checksum_history_retirable(0u) ||
        !ggpo_net_test_apply_peer_checksum_ack(1u, 0u, 6u) ||
        !ggpo_net_test_checksum_history_retirable(0u)) {
        return frame_ring_fail("history retirement did not require both checksum directions");
    }

    ggpo_net_test_reset_frame_rings(1u);
    ggpo_net_test_set_checksum_epoch(1u, 0u, 0u);
    ggpo_net_test_set_remote_contiguous(1u, 0u);
    ggpo_net_test_set_peer_ack(1u, 0u);
    if (!ggpo_net_test_seed_history(0u, 0u, 0, 0xA000u)) {
        return frame_ring_fail("failed to seed deferred checksum history");
    }
    ggpo_net_test_set_rollback_pending(1);
    if (ggpo_net_test_receive_remote_checksum(0u, 0xA000u) != 0) {
        return frame_ring_fail("rollback-pending checksum was acknowledged early");
    }
    ggpo_net_test_get_checksum_ack_next(&remote_checksum_next, NULL);
    if (remote_checksum_next != 0u) {
        return frame_ring_fail("deferred checksum advanced receipt state");
    }
    ggpo_net_test_set_rollback_pending(0);
    ggpo_net_test_process_deferred_checksums();
    ggpo_net_test_get_checksum_ack_next(&remote_checksum_next, NULL);
    if (remote_checksum_next != 1u) {
        return frame_ring_fail("deferred checksum did not ACK after rollback cleared");
    }

    /* If checksum traffic alone makes no progress, the modulo-512 boundary is
     * a permanent stall followed by a deterministic disconnect—not overwrite. */
    ggpo_net_test_reset_frame_rings(512u);
    ggpo_net_test_set_checksum_epoch(1u, 0u, 0u);
    if (!ggpo_net_test_seed_history(0u, 0u, 0, 0xB000u) ||
        ggpo_net_test_checksum_retirement_gate(512u, 1u) != 0 ||
        ggpo_net_test_checksum_retirement_gate(512u, 601u) != -1) {
        return frame_ring_fail("checksum history boundary did not stall then time out");
    }

    /* Cumulative receive and peer-ACK bases use serial arithmetic across wrap. */
    ggpo_net_test_reset_frame_rings(UINT32_MAX - 1u);
    ggpo_net_test_set_remote_contiguous(1u, UINT32_MAX - 2u);
    ggpo_net_test_set_local_frame(0u);
    if (!ggpo_net_test_receive_remote_input(0u, 0x40u) ||
        !ggpo_net_test_get_remote_ack(&valid, &ack_frame, ack_bits) ||
        ack_frame != UINT32_MAX - 2u || (ack_bits[0] & 0x4u) == 0u ||
        !ggpo_net_test_receive_remote_input(UINT32_MAX - 1u, 0x41u) ||
        !ggpo_net_test_receive_remote_input(UINT32_MAX, 0x42u) ||
        !ggpo_net_test_get_remote_ack(&valid, &ack_frame, ack_bits) ||
        valid != 1u || ack_frame != 0u) {
        return frame_ring_fail("contiguous input ACK failed across uint32 wrap");
    }
    ggpo_net_test_reset_frame_rings(0u);
    if (!ggpo_net_test_store_local_input(UINT32_MAX - 1u, 1u) ||
        !ggpo_net_test_store_local_input(UINT32_MAX, 2u) ||
        !ggpo_net_test_store_local_input(0u, 3u)) {
        return frame_ring_fail("failed to seed wrapped local ACK fixture");
    }
    ggpo_net_test_set_peer_ack(1u, UINT32_MAX - 2u);
    memset(ack_bits, 0, sizeof(ack_bits));
    if (!ggpo_net_test_apply_peer_ack(1u, UINT32_MAX - 1u, ack_bits) ||
        !ggpo_net_test_apply_peer_ack(1u, 0u, ack_bits) ||
        !ggpo_net_test_apply_peer_ack(1u, UINT32_MAX, ack_bits) ||
        !ggpo_net_test_get_peer_ack(&valid, &ack_frame) || ack_frame != 0u) {
        return frame_ring_fail("reordered peer ACK regressed across uint32 wrap");
    }

    printf("frame-ring PASS future=%u retention=%u dropped=%u ack_words=%u\n",
           (unsigned int)future_limit,
           (unsigned int)retention_limit,
           (unsigned int)ggpo_net_dropped_input_count(),
           (unsigned int)GGPO_NET_TEST_ACK_WORDS);
    return 0;
}

static int state_transaction_fail(const char* what, const char* err) {
    fprintf(stderr, "state-transaction FAIL: %s%s%s\n",
            what,
            (err && err[0]) ? " - " : "",
            (err && err[0]) ? err : "");
    return 1;
}

static int run_state_transaction_test(void) {
    static uint8_t baseline[TEST_STATE_BYTES];
    static uint8_t candidate[TEST_STATE_BYTES];
    uint32_t expected;
    uint32_t applied = 0u;
    int fatal = 0;
    char err[512];

    g_state_size = TEST_DEFAULT_STATE_BYTES;
    memset(baseline, 0x22, g_state_size);
    memset(candidate, 0x11, g_state_size);
    candidate[16] = 0x7Bu;
    expected = test_checksum(candidate, g_state_size);

    /* Invalid shape, noncanonical transport bytes, and a forged advertised
     * checksum are all rejected before either a backup or a mutating rollback
     * load occurs. */
    memcpy(g_state, baseline, g_state_size);
    g_rollback_load_calls = g_raw_load_calls = g_raw_save_calls = 0u;
    err[0] = '\0';
    if (ggpo_net_test_apply_state_transaction(candidate,
                                              g_state_size - 1u,
                                              expected,
                                              &applied,
                                              &fatal,
                                              err,
                                              sizeof(err)) ||
        fatal || memcmp(g_state, baseline, g_state_size) != 0 ||
        g_rollback_load_calls != 0u || g_raw_load_calls != 0u ||
        g_raw_save_calls != 0u) {
        return state_transaction_fail("malformed blob touched live state", err);
    }
    g_reject_noncanonical_transport = 1;
    err[0] = '\0';
    if (ggpo_net_test_apply_state_transaction(candidate,
                                              g_state_size,
                                              expected,
                                              &applied,
                                              &fatal,
                                              err,
                                              sizeof(err)) ||
        fatal || memcmp(g_state, baseline, g_state_size) != 0 ||
        g_rollback_load_calls != 0u || g_raw_load_calls != 0u ||
        g_raw_save_calls != 0u || !strstr(err, "noncanonical")) {
        g_reject_noncanonical_transport = 0;
        return state_transaction_fail("noncanonical transport blob touched live state", err);
    }
    g_reject_noncanonical_transport = 0;
    err[0] = '\0';
    if (ggpo_net_test_apply_state_transaction(candidate,
                                              g_state_size,
                                              expected ^ 1u,
                                              &applied,
                                              &fatal,
                                              err,
                                              sizeof(err)) ||
        fatal || memcmp(g_state, baseline, g_state_size) != 0 ||
        g_rollback_load_calls != 0u || g_raw_load_calls != 0u ||
        g_raw_save_calls != 0u) {
        return state_transaction_fail("checksum preflight touched live state", err);
    }

    /* A loader may fail after partially writing memory. The transaction must
     * restore the exact raw serializer image and verify the restored bytes. */
    g_fail_load_remaining = 1;
    err[0] = '\0';
    if (ggpo_net_test_apply_state_transaction(candidate,
                                              g_state_size,
                                              expected,
                                              &applied,
                                              &fatal,
                                              err,
                                              sizeof(err)) ||
        fatal || memcmp(g_state, baseline, g_state_size) != 0 ||
        g_rollback_load_calls != 1u || g_raw_load_calls != 1u ||
        g_raw_save_calls != 2u || !strstr(err, "previous state restored")) {
        return state_transaction_fail("partial load failure was not restored exactly", err);
    }

    /* A nominally successful load is still uncommitted until its live canonical
     * checksum matches the validated network blob. */
    g_corrupt_load_once = 1;
    err[0] = '\0';
    if (ggpo_net_test_apply_state_transaction(candidate,
                                              g_state_size,
                                              expected,
                                              &applied,
                                              &fatal,
                                              err,
                                              sizeof(err)) ||
        fatal || memcmp(g_state, baseline, g_state_size) != 0 ||
        g_rollback_load_calls != 2u || g_raw_load_calls != 2u ||
        g_raw_save_calls != 4u) {
        return state_transaction_fail("post-load corruption was not rolled back", err);
    }

    /* If exact restoration itself fails, fail closed instead of simulating from
     * the now-unknown live state. */
    g_fail_load_remaining = 1;
    g_fail_raw_load_once = 1;
    err[0] = '\0';
    if (ggpo_net_test_apply_state_transaction(candidate,
                                              g_state_size,
                                              expected,
                                              &applied,
                                              &fatal,
                                              err,
                                              sizeof(err)) ||
        !fatal || !strstr(err, "fatal state restore failure")) {
        return state_transaction_fail("restore failure did not fail closed", err);
    }

    memcpy(g_state, baseline, g_state_size);
    g_fail_load_remaining = 0;
    g_corrupt_load_once = 0;
    g_fail_raw_load_once = 0;
    err[0] = '\0';
    fatal = 0;
    if (!ggpo_net_test_apply_state_transaction(candidate,
                                               g_state_size,
                                               expected,
                                               &applied,
                                               &fatal,
                                               err,
                                               sizeof(err)) ||
        fatal || applied != expected ||
        memcmp(g_state, candidate, g_state_size) != 0) {
        return state_transaction_fail("valid blob did not commit", err);
    }

    printf("state-transaction PASS checksum=%u rollback_loads=%u raw_restores=%u\n",
           (unsigned int)applied,
           (unsigned int)g_rollback_load_calls,
           (unsigned int)g_raw_load_calls);
    return 0;
}
#endif

int main(int argc, char** argv) {
    int is_host;
    const char* role;
    uint16_t local_port;
    uint16_t remote_port;
    int connected_tick = -1;
    int released = 0;
    int ready_tick = -1;
    char err[512];
    int tick;
    int expect_rejection = 0;
    int tamper_outgoing = 0;
    int replay_outgoing = 0;
    int restart_after_connect = 0;
    int watch_peer_restart = 0;
    int restart_after_state_sync = 0;
    int restarted = 0;
    int finalize_layout = 0;
    int layout_mismatch = 0;
    int layout_size_mismatch = 0;
    int layout_finalized = 0;
    int gameplay_chaos = 0;
    int gameplay_wrap_chaos = 0;
    int gameplay_correction = 0;
    int gameplay_long_correction = 0;
    int gameplay_disconnect = 0;
    int gameplay_disconnect_correction = 0;
    int gameplay_disconnect_receiving = 0;
    int gameplay_disconnect_commit = 0;
    int gameplay_disconnect_release = 0;
    int gameplay_disconnect_release_ack = 0;
    int input_sampling = 0;
    int tick_failure = 0;
    int prematch_skew = 0;
    int prematch_prepared = 0;
    int layout_finalize_tick = -1;
    int recovery_ready_not_before_tick = 0;
    uint16_t restart_port = 0;
    size_t bootstrap_state_size = 0u;
    size_t final_state_size = TEST_FINAL_STATE_BYTES;
    uint32_t skew_before_prepare = 0u;
    uint32_t skew_after_prepare = 0u;
    uint32_t local_palette_skin = 0u;
    uint32_t local_palette_clothing = 0u;
    const char* match_token = TEST_MATCH_TOKEN;

#ifdef GGPO_NET_TEST
    if (argc == 2 && strcmp(argv[1], "frame-ring") == 0) {
        return run_frame_ring_test();
    }
    if (argc == 2 && strcmp(argv[1], "state-transaction") == 0) {
        return run_state_transaction_test();
    }
#endif

    if (argc == 2 && strcmp(argv[1], "nokey") == 0) {
        memset(g_state, 0x33, sizeof(g_state));
        err[0] = '\0';
        if (ggpo_net_start_host(0, err, sizeof(err)) || ggpo_net_active()) {
            return fail("nokey", "session started without an authentication token", err);
        }
        if (!strstr(err, "token")) {
            return fail("nokey", "missing-token failure was not explicit", err);
        }
        printf("nokey PASS fail_closed=%s\n", err);
        return 0;
    }

    if (argc < 4 || argc > 7 || (strcmp(argv[1], "host") != 0 &&
                      strcmp(argv[1], "join") != 0 &&
                      strcmp(argv[1], "joinfail") != 0)) {
        fprintf(stderr, "usage: prematch_net_test host|join|joinfail local_port remote_port [token] [normal|chaos|wrapchaos|correction|longcorrection|disconnect|disconnectcorrection|disconnectreceiving|disconnectcommit|disconnectrelease|disconnectreleaseack|sampling|tickfailure|skew|layout|layoutmismatch|layoutsizemismatch|reject|restart|watchrestart|layoutrestart|layoutwatchrestart|restartlate|watchrestartlate] [tamper|replay|restart_port]\n");
        return 2;
    }
    is_host = strcmp(argv[1], "host") == 0;
    role = is_host ? "host" : "join";
    g_fail_load_remaining = strcmp(argv[1], "joinfail") == 0 ? 1 : 0;
    if (argc >= 5) match_token = argv[4];
    if (argc >= 6) {
        expect_rejection = strcmp(argv[5], "reject") == 0;
        gameplay_wrap_chaos = strcmp(argv[5], "wrapchaos") == 0;
        gameplay_chaos = strcmp(argv[5], "chaos") == 0 ||
                         gameplay_wrap_chaos;
        gameplay_long_correction = strcmp(argv[5], "longcorrection") == 0;
        gameplay_disconnect = strcmp(argv[5], "disconnect") == 0;
        gameplay_disconnect_correction =
            strcmp(argv[5], "disconnectcorrection") == 0;
        gameplay_disconnect_receiving =
            strcmp(argv[5], "disconnectreceiving") == 0;
        gameplay_disconnect_commit =
            strcmp(argv[5], "disconnectcommit") == 0;
        gameplay_disconnect_release =
            strcmp(argv[5], "disconnectrelease") == 0;
        gameplay_disconnect_release_ack =
            strcmp(argv[5], "disconnectreleaseack") == 0;
        gameplay_correction = strcmp(argv[5], "correction") == 0 ||
                              gameplay_long_correction ||
                              gameplay_disconnect_correction ||
                              gameplay_disconnect_receiving ||
                              gameplay_disconnect_commit ||
                              gameplay_disconnect_release ||
                              gameplay_disconnect_release_ack;
        input_sampling = strcmp(argv[5], "sampling") == 0;
        tick_failure = strcmp(argv[5], "tickfailure") == 0;
        prematch_skew = strcmp(argv[5], "skew") == 0;
        restart_after_connect = strcmp(argv[5], "restart") == 0 ||
                                strcmp(argv[5], "layoutrestart") == 0;
        watch_peer_restart = strcmp(argv[5], "watchrestart") == 0 ||
                             strcmp(argv[5], "layoutwatchrestart") == 0;
        restart_after_state_sync =
            strcmp(argv[5], "restartlate") == 0 ||
            strcmp(argv[5], "watchrestartlate") == 0;
        if (strcmp(argv[5], "restartlate") == 0) restart_after_connect = 1;
        if (strcmp(argv[5], "watchrestartlate") == 0) watch_peer_restart = 1;
        finalize_layout = strcmp(argv[5], "layout") == 0 ||
                          strcmp(argv[5], "layoutmismatch") == 0 ||
                          strcmp(argv[5], "layoutsizemismatch") == 0 ||
                          strcmp(argv[5], "layoutrestart") == 0 ||
                          strcmp(argv[5], "layoutwatchrestart") == 0;
        layout_mismatch = strcmp(argv[5], "layoutmismatch") == 0 ||
                          strcmp(argv[5], "layoutsizemismatch") == 0;
        layout_size_mismatch = strcmp(argv[5], "layoutsizemismatch") == 0;
    }
    if (argc >= 7) {
        tamper_outgoing = strcmp(argv[6], "tamper") == 0;
        replay_outgoing = strcmp(argv[6], "replay") == 0;
        if (restart_after_connect || watch_peer_restart) {
            unsigned long restart_value = strtoul(argv[6], NULL, 10);
            if (restart_value == 0ul || restart_value > 65535ul) {
                fprintf(stderr, "invalid restart test port\n");
                return 2;
            }
            restart_port = (uint16_t)restart_value;
        }
    }
    {
        unsigned long local_value = strtoul(argv[2], NULL, 10);
        unsigned long remote_value = strtoul(argv[3], NULL, 10);
        if (local_value == 0ul || local_value > 65535ul ||
            remote_value == 0ul || remote_value > 65535ul ||
            local_value == remote_value) {
            fprintf(stderr, "invalid test ports\n");
            return 2;
        }
        local_port = (uint16_t)local_value;
        remote_port = (uint16_t)remote_value;
    }
    g_state_size = finalize_layout
        ? (is_host ? TEST_HOST_BOOTSTRAP_STATE_BYTES
                   : TEST_JOIN_BOOTSTRAP_STATE_BYTES)
        : TEST_DEFAULT_STATE_BYTES;
    if (layout_size_mismatch && !is_host) {
        final_state_size = TEST_ALT_FINAL_STATE_BYTES;
    }
    bootstrap_state_size = g_state_size;
    if (finalize_layout && final_state_size <= bootstrap_state_size) {
        return fail(role, "fixture final layout did not grow from bootstrap", NULL);
    }
    g_layout_generation = TEST_LAYOUT_GENERATION;
    memset(g_state, is_host ? 0x11 : 0x22, sizeof(g_state));
    g_seed = 1234u;
    g_game_width = is_host ? TEST_AUTHORITATIVE_GAME_WIDTH : 320.0f;
    g_game_height = is_host ? TEST_AUTHORITATIVE_GAME_HEIGHT : 180.0f;
    g_camera_shake = is_host ? TEST_AUTHORITATIVE_CAMERA_SHAKE : 0.0f;
    g_camera_shake_decay = is_host
        ? TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY
        : 0.0f;
    g_sync_game_width = 1;
    test_state_store_game_geometry();

    if (prematch_skew &&
        (!ggpo_net_set_auto_input_delay(0) ||
         !ggpo_net_set_input_delay(TEST_SKEW_INPUT_DELAY))) {
        return fail(role, "configure prematch skew input delay", NULL);
    }
    if (input_sampling &&
        (!ggpo_net_set_auto_input_delay(0) ||
         !ggpo_net_set_input_delay(1u) ||
         !ggpo_net_set_max_prediction(2u) ||
         !ggpo_net_set_max_frame_advantage(
             GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT))) {
        return fail(role, "configure input sampling boundary", NULL);
    }

    local_palette_skin = is_host
        ? TEST_HOST_PALETTE_SKIN : TEST_JOIN_PALETTE_SKIN;
    local_palette_clothing = is_host
        ? TEST_HOST_PALETTE_CLOTHING : TEST_JOIN_PALETTE_CLOTHING;
    if (ggpo_net_set_local_palette_preference(0u, 0u, 0u) ||
        ggpo_net_set_local_palette_preference(TEST_PALETTE_COUNT,
                                              0u,
                                              TEST_PALETTE_COUNT) ||
        !ggpo_net_set_local_palette_preference(local_palette_skin,
                                               local_palette_clothing,
                                               TEST_PALETTE_COUNT)) {
        return fail(role, "configure bounded local palette", NULL);
    }
    {
        uint32_t skin = 0u;
        uint32_t clothing = 0u;
        uint32_t count = 0u;
        if (!ggpo_net_local_palette_preference(&skin, &clothing, &count) ||
            skin != local_palette_skin ||
            clothing != local_palette_clothing ||
            count != TEST_PALETTE_COUNT) {
            return fail(role, "read configured local palette", NULL);
        }
    }

    err[0] = '\0';
    if (!ggpo_net_set_match_token(match_token, err, sizeof(err))) {
        return fail(role, "match token", err);
    }
#ifdef GGPO_NET_TEST
    ggpo_net_test_set_tamper_outgoing(tamper_outgoing);
    ggpo_net_test_set_replay_outgoing(replay_outgoing);
#else
    (void)tamper_outgoing;
    (void)replay_outgoing;
#endif
    if (!(is_host ? ggpo_net_start_host_held(local_port, err, sizeof(err))
                  : ggpo_net_start_join_deferred_held(local_port, err, sizeof(err)))) {
        return fail(role, "start", err);
    }
    if (ggpo_net_set_local_palette_preference(0u, 1u, TEST_PALETTE_COUNT)) {
        return fail(role, "active session changed fixed local palette", NULL);
    }
    if (!ggpo_net_prematch_hold() || ggpo_net_state_size() != 0u ||
        ggpo_net_state_layout_fingerprint() != 0u ||
        ggpo_net_state_layout_ready() || ggpo_net_state_layout_mismatch()) {
        return fail(role, "held transport captured provisional state layout", NULL);
    }
    if (finalize_layout) {
        err[0] = '\0';
        if (ggpo_net_set_prematch_hold(0, err, sizeof(err)) ||
            !ggpo_net_prematch_hold()) {
            return fail(role, "unfinalized state layout released", err);
        }
    }
    if (!finalize_layout) {
        err[0] = '\0';
        if (!ggpo_net_finalize_state_layout(err, sizeof(err))) {
            return fail(role, "finalize unchanged state layout", err);
        }
    }
    if (!ggpo_net_set_peer("127.0.0.1", remote_port, err, sizeof(err))) {
        return fail(role, "peer", err);
    }

    if (expect_rejection) {
        for (tick = 0; tick < 700; tick++) {
            err[0] = '\0';
            if (!ggpo_net_service(err, sizeof(err))) {
                return fail(role, "rejection service", err);
            }
            if (ggpo_net_connected()) {
                return fail(role, "unauthorized/tampered peer connected", NULL);
            }
            Sleep(1);
        }
        printf("%s REJECT PASS rejected=%u tamper=%d\n",
               role,
               (unsigned int)ggpo_net_auth_rejected_packets(),
               tamper_outgoing);
        ggpo_net_stop_for_retry();
        return 0;
    }

    /* Even an accidental gameplay call while the barrier is held must service
     * transport only. This is the net layer's last line of defense against the
     * frozen-visible-frame regression the prematch pipeline prevents. */
    {
        uint32_t checksum = 0u;
        int advanced = -1;
        err[0] = '\0';
        if (!ggpo_net_advance(0xFFFFFFFFu, 0xFFFFFFFFu, 0,
                              &checksum, &advanced, err, sizeof(err)) ||
            advanced != 0 || ggpo_net_frame_count() != 0u ||
            ggpo_net_prematch_ready()) {
            return fail(role, "held session advanced gameplay", err);
        }
    }

    for (tick = 0; tick < 2400; tick++) {
        err[0] = '\0';
        if (!ggpo_net_service(err, sizeof(err))) return fail(role, "service", err);
        if (ggpo_net_frame_count() != 0u) {
            return fail(role, "service advanced gameplay", NULL);
        }
        if (connected_tick < 0 && ggpo_net_connected()) connected_tick = tick;
        if ((restart_after_connect || watch_peer_restart) && !restarted &&
            connected_tick >= 0 &&
            ((!restart_after_state_sync && tick - connected_tick >= 12) ||
             (restart_after_state_sync && released &&
              ggpo_net_state_synced() && ggpo_net_remote_state_synced()))) {
            /* Model the matchmaking retry race seen in production: one peer has
             * already pinned this confirmed session while the other rotates its
             * socket.  Rebind the same endpoint with a fresh authenticated
             * session; the surviving peer must recover before frame 0. */
            err[0] = '\0';
            if (restart_after_connect) {
                ggpo_net_stop_for_retry();
                local_port = restart_port;
                if (!ggpo_net_set_match_token(match_token, err, sizeof(err)) ||
                    !(is_host ? ggpo_net_start_host_held(local_port, err, sizeof(err))
                              : ggpo_net_start_join_deferred_held(local_port, err, sizeof(err))) ||
                    !ggpo_net_set_peer("127.0.0.1", remote_port, err, sizeof(err))) {
                    return fail(role, "prematch socket restart", err);
                }
                if (!ggpo_net_prematch_hold() || ggpo_net_state_size() != 0u ||
                    ggpo_net_state_layout_fingerprint() != 0u ||
                    ggpo_net_state_layout_ready() ||
                    ggpo_net_state_layout_mismatch()) {
                    return fail(role, "restart retained stale state layout proof", NULL);
                }
                if (!finalize_layout) {
                    err[0] = '\0';
                    if (!ggpo_net_finalize_state_layout(err, sizeof(err))) {
                        return fail(role, "finalize restarted state layout", err);
                    }
                    if (ggpo_net_state_layout_ready()) {
                        return fail(role, "restart accepted stale peer layout proof", NULL);
                    }
                } else {
                    layout_finalized = 0;
                    layout_finalize_tick = -1;
                }
            } else if (!ggpo_net_set_peer("127.0.0.1", restart_port,
                                          err, sizeof(err))) {
                return fail(role, "prematch peer endpoint update", err);
            }
            if (watch_peer_restart && ggpo_net_state_layout_ready()) {
                return fail(role, "peer restart retained stale layout proof", NULL);
            }
            restarted = 1;
            recovery_ready_not_before_tick = tick + 20;
            if (restart_after_connect) {
                connected_tick = -1;
                released = 0;
            }
            continue;
        }
        if (finalize_layout && connected_tick >= 0 && !layout_finalized) {
            const int finalize_delay = is_host ? 30 : 5;
            if (tick - connected_tick >= finalize_delay) {
                g_state_size = final_state_size;
                if (layout_mismatch && !layout_size_mismatch && !is_host) {
                    g_layout_generation = TEST_MISMATCH_LAYOUT_GENERATION;
                }
                memset(g_state, is_host ? 0xD5 : 0x6D, g_state_size);
                err[0] = '\0';
                if (!ggpo_net_finalize_state_layout(err, sizeof(err))) {
                    return fail(role, "finalize state layout", err);
                }
                if (ggpo_net_state_size() != final_state_size) {
                    return fail(role, "final state layout size was not installed", NULL);
                }
                if (ggpo_net_state_layout_fingerprint() !=
                    ggpo_ext_game_state_layout_fingerprint()) {
                    return fail(role, "final state layout fingerprint was not installed", NULL);
                }
                /* Repeating the exact finalized layout must be harmless. This
                 * makes retrying a setup callback safe without reallocating or
                 * changing the negotiated layout identity. */
                err[0] = '\0';
                if (!ggpo_net_finalize_state_layout(err, sizeof(err)) ||
                    ggpo_net_state_size() != final_state_size) {
                    return fail(role, "idempotent layout finalize", err);
                }
                {
                    const uint32_t frozen_layout =
                        ggpo_net_state_layout_fingerprint();
                    const uint32_t original_generation = g_layout_generation;
                    g_layout_generation ^= 0x01010101u;
                    err[0] = '\0';
                    if (ggpo_net_finalize_state_layout(err, sizeof(err)) ||
                        ggpo_net_state_size() != final_state_size ||
                        ggpo_net_state_layout_fingerprint() != frozen_layout) {
                        g_layout_generation = original_generation;
                        return fail(role, "changed layout refinalize was not rejected", err);
                    }
                    g_layout_generation = original_generation;
                }
                if (!is_host) {
                    if (ggpo_net_state_layout_ready()) {
                        return fail(role, "peer layout proof arrived before host finalized", NULL);
                    }
                    err[0] = '\0';
                    if (ggpo_net_set_prematch_hold(0, err, sizeof(err)) ||
                        !ggpo_net_prematch_hold()) {
                        return fail(role, "layout released before peer proof", err);
                    }
                }
                layout_finalized = 1;
                layout_finalize_tick = tick;
            }
        }
        if (layout_mismatch && layout_finalized &&
            ggpo_net_state_layout_mismatch()) {
            err[0] = '\0';
            if (ggpo_net_set_prematch_hold(0, err, sizeof(err))) {
                return fail(role, "mismatched final layout released", NULL);
            }
            if (!ggpo_net_prematch_hold() || ggpo_net_state_layout_ready() ||
                ggpo_net_prematch_ready()) {
                return fail(role, "layout mismatch did not remain fail-closed", err);
            }
            printf("%s LAYOUT REJECT PASS connected=%d finalized=%d bootstrap_size=%u state_size=%u layout_id=%08X mismatch=%d reason=%s\n",
                   role, connected_tick, layout_finalize_tick,
                   (unsigned int)bootstrap_state_size,
                   (unsigned int)ggpo_net_state_size(),
                   (unsigned int)ggpo_net_state_layout_fingerprint(),
                   ggpo_net_state_layout_mismatch(),
                   err[0] ? err : "rejected");
            ggpo_net_stop_for_retry();
            return 0;
        }
        if (connected_tick >= 0 && !released &&
            (!finalize_layout || layout_finalized) &&
            ggpo_net_state_layout_ready()) {
            int delay = is_host ? 40 : 4;
            if (tick - connected_tick >= delay) {
                memset(g_state, is_host ? 0xA5 : 0x5A, g_state_size);
                memcpy(g_state, "FINAL-HOST-STATE", 16);
                if (finalize_layout && is_host) {
                    memcpy(g_state + TEST_FINAL_STATE_PROBE_OFFSET,
                           "FINAL-LAYOUT-TAIL", 17);
                }
                err[0] = '\0';
                if (!ggpo_net_set_prematch_hold(0, err, sizeof(err))) {
                    return fail(role, "release", err);
                }
                released = 1;
            }
        }
        if (released &&
            tick >= recovery_ready_not_before_tick &&
            ggpo_net_prematch_ready()) {
            if (layout_mismatch) {
                return fail(role, "mismatched final layout became ready", NULL);
            }
            ready_tick = tick;
            break;
        }
        if (layout_mismatch && layout_finalize_tick >= 0 &&
            tick - layout_finalize_tick >= 400) {
            return fail(role, "final layout mismatch was not reported", NULL);
        }
        Sleep(1);
    }
    if (connected_tick < 0) return fail(role, "never connected", NULL);
    if (finalize_layout && !layout_finalized) {
        return fail(role, "final layout was never installed", NULL);
    }
    if (!ggpo_net_state_layout_ready()) {
        return fail(role, "final layout negotiation was not ready", NULL);
    }
    if (!released) return fail(role, "never released", NULL);
    if (ready_tick < 0) return fail(role, "never ready", NULL);
    {
        uint32_t remote_skin = 0u;
        uint32_t remote_clothing = 0u;
        uint32_t remote_count = 0u;
        uint32_t expected_skin = is_host
            ? TEST_JOIN_PALETTE_SKIN : TEST_HOST_PALETTE_SKIN;
        uint32_t expected_clothing = is_host
            ? TEST_JOIN_PALETTE_CLOTHING : TEST_HOST_PALETTE_CLOTHING;
        if (!ggpo_net_palette_ready() ||
            !ggpo_net_remote_palette_preference(&remote_skin,
                                                &remote_clothing,
                                                &remote_count) ||
            remote_skin != expected_skin ||
            remote_clothing != expected_clothing ||
            remote_count != TEST_PALETTE_COUNT) {
            fprintf(stderr,
                    "%s palette detail local=%u/%u remote=%u/%u/%u expected=%u/%u/%u\n",
                    role,
                    (unsigned int)local_palette_skin,
                    (unsigned int)local_palette_clothing,
                    (unsigned int)remote_skin,
                    (unsigned int)remote_clothing,
                    (unsigned int)remote_count,
                    (unsigned int)expected_skin,
                    (unsigned int)expected_clothing,
                    (unsigned int)TEST_PALETTE_COUNT);
            return fail(role, "authenticated palette exchange mismatch", NULL);
        }
    }

    if (prematch_skew) {
        uint32_t horizon = 0u;
        if (ggpo_net_input_delay() != TEST_SKEW_INPUT_DELAY) {
            return fail(role, "prematch skew input delay changed", NULL);
        }
        if (is_host) {
            err[0] = '\0';
            if (!ggpo_net_prepare_prematch_start(err, sizeof(err))) {
                return fail(role, "early skew prepare", err);
            }
            prematch_prepared = 1;
            /* Send the complete neutral delay prefix while the join process is
             * deliberately still servicing the hub-side prematch path. */
            for (tick = 0; tick < TEST_SKEW_SERVICE_TICKS; tick++) {
                err[0] = '\0';
                if (!ggpo_net_test_service_input(err, sizeof(err))) {
                    return fail(role, "early skew input service", err);
                }
                Sleep(1);
            }
            if (!ggpo_net_peer_input_confirmed_frame(&horizon) ||
                horizon < TEST_SKEW_INPUT_DELAY - 1u) {
                return fail(role, "late peer never acknowledged skew prefix", NULL);
            }
        } else {
            uint32_t rollback_loads_before_prepare;
            uint32_t raw_loads_before_prepare;
            uint32_t raw_saves_before_prepare;
            uint32_t horizon_after_failed_prepare = 0u;
            uint8_t state_before_prepare[TEST_STATE_BYTES];
            /* Receive and ACK the host's delay prefix before loading our own
             * final start state. The old bug erased these already-ACKed inputs. */
            for (tick = 0; tick < TEST_SKEW_SERVICE_TICKS / 2; tick++) {
                err[0] = '\0';
                if (!ggpo_net_service(err, sizeof(err))) {
                    return fail(role, "late skew prematch service", err);
                }
                Sleep(1);
            }
            if (!ggpo_net_remote_input_confirmed_frame(&skew_before_prepare) ||
                skew_before_prepare < TEST_SKEW_INPUT_DELAY - 1u) {
                return fail(role, "skew prefix was not received before prepare", NULL);
            }
            /* The deferred second load is transactional. Keep one more injected
             * failure than the bounded retry path permits: it must stop after
             * exactly three exact-restored attempts, leave the session retryable,
             * and preserve the already authenticated input/ACK prefix. */
            memcpy(state_before_prepare, g_state, g_state_size);
            rollback_loads_before_prepare = g_rollback_load_calls;
            raw_loads_before_prepare = g_raw_load_calls;
            raw_saves_before_prepare = g_raw_save_calls;
            g_fail_load_remaining = 4;
            err[0] = '\0';
            if (ggpo_net_prepare_prematch_start(err, sizeof(err))) {
                return fail(role, "persistent skew load failure exceeded retry cap", err);
            }
            if (g_fail_load_remaining != 1 ||
                g_rollback_load_calls != rollback_loads_before_prepare + 3u ||
                g_raw_load_calls != raw_loads_before_prepare + 3u ||
                g_raw_save_calls != raw_saves_before_prepare + 6u ||
                memcmp(g_state, state_before_prepare, g_state_size) != 0 ||
                !strstr(err, "previous state restored") ||
                !ggpo_net_connected() || ggpo_net_start_state_loaded() ||
                !ggpo_net_remote_input_confirmed_frame(&horizon_after_failed_prepare) ||
                horizon_after_failed_prepare < skew_before_prepare) {
                return fail(role, "prematch load retry cap was not exact and nonmutating", err);
            }

            /* A clean explicit retry must then commit once, without another raw
             * restore, while retaining the same authenticated input horizon. */
            g_fail_load_remaining = 0;
            err[0] = '\0';
            if (!ggpo_net_prepare_prematch_start(err, sizeof(err))) {
                return fail(role, "late skew prepare after bounded failure", err);
            }
            if (g_rollback_load_calls != rollback_loads_before_prepare + 4u ||
                g_raw_load_calls != raw_loads_before_prepare + 3u ||
                g_raw_save_calls != raw_saves_before_prepare + 7u ||
                !ggpo_net_start_state_loaded()) {
                return fail(role, "clean prematch retry did not commit exactly once", NULL);
            }
            prematch_prepared = 1;
            if (!ggpo_net_remote_input_confirmed_frame(&skew_after_prepare) ||
                skew_after_prepare < skew_before_prepare) {
                return fail(role, "prepare regressed acknowledged remote inputs", NULL);
            }
            for (tick = TEST_SKEW_SERVICE_TICKS / 2;
                 tick < TEST_SKEW_SERVICE_TICKS;
                 tick++) {
                err[0] = '\0';
                if (!ggpo_net_test_service_input(err, sizeof(err))) {
                    return fail(role, "late skew input service", err);
                }
                Sleep(1);
            }
        }
    }

    err[0] = '\0';
    if (!prematch_prepared &&
        !ggpo_net_prepare_prematch_start(err, sizeof(err))) {
        return fail(role, "prepare", err);
    }
    if (memcmp(g_state, "FINAL-HOST-STATE", 16) != 0 || g_state[128] != 0xA5u) {
        return fail(role, "authoritative host state mismatch", NULL);
    }
    if (!test_float_near(g_camera_shake,
                         TEST_AUTHORITATIVE_CAMERA_SHAKE) ||
        !test_float_near(g_camera_shake_decay,
                         TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY)) {
        fprintf(stderr,
                "%s shake detail live=%.9g/%.9g expected=%.9g/%.9g\n",
                role,
                (double)g_camera_shake,
                (double)g_camera_shake_decay,
                (double)TEST_AUTHORITATIVE_CAMERA_SHAKE,
                (double)TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY);
        return fail(role, "authoritative camera shake mismatch", NULL);
    }
    {
        float expected_local_width = is_host ? TEST_AUTHORITATIVE_GAME_WIDTH : 320.0f;
        float expected_local_height = is_host ? TEST_AUTHORITATIVE_GAME_HEIGHT : 180.0f;
        if (g_game_width != expected_local_width ||
            g_game_height != expected_local_height) {
            fprintf(stderr,
                    "%s geometry detail local=%.1f/%.1f expected=%.1f/%.1f\n",
                    role,
                    (double)g_game_width,
                    (double)g_game_height,
                    (double)expected_local_width,
                    (double)expected_local_height);
            return fail(role,
                        "authoritative simulation/local render geometry split failed",
                        NULL);
        }
    }
    if (finalize_layout &&
        memcmp(g_state + TEST_FINAL_STATE_PROBE_OFFSET,
               "FINAL-LAYOUT-TAIL", 17) != 0) {
        return fail(role, "authoritative final-layout tail mismatch", NULL);
    }
    test_palette_set_sentinel();

    if (gameplay_wrap_chaos) {
        err[0] = '\0';
        if (!ggpo_net_test_rebase_active_frame(TEST_WRAP_CHAOS_START,
                                               err,
                                               sizeof(err)) ||
            ggpo_net_frame_count() != TEST_WRAP_CHAOS_START) {
            return fail(role, "rebase synchronized session near uint32 wrap", err);
        }
    }

    if (tick_failure) {
        uint8_t expected[TEST_STATE_BYTES];
        uint32_t checksum = 0u;
        int advanced = 0;
        memcpy(expected, g_state, g_state_size);
        /* First read captures local render geometry; fail the second read in
         * the clean post-tick simulation snapshot. */
        g_fail_width_read_on_call = 2;
        err[0] = '\0';
        if (ggpo_net_advance(0u, 0u, 0, &checksum, &advanced,
                             err, sizeof(err))) {
            return fail(role,
                        "post-tick clean-state failure was not fatal",
                        err);
        }
        if (advanced || ggpo_net_frame_count() != 0u ||
            memcmp(expected, g_state, g_state_size) != 0 ||
            g_game_width != (is_host ? TEST_AUTHORITATIVE_GAME_WIDTH : 320.0f) ||
            g_game_height != (is_host ? TEST_AUTHORITATIVE_GAME_HEIGHT : 180.0f) ||
            !strstr(err, "capture clean native simulation state")) {
            return fail(role,
                        "failed live tick did not restore its exact pre-state",
                        err);
        }
        printf("%s TICK FAILURE PASS frame=%u restored=1\n",
               role,
               (unsigned int)ggpo_net_frame_count());
        ggpo_net_stop_for_retry();
        return 0;
    }

    {
        uint32_t checksum = 0;
        uint32_t width_sets_before = g_width_set_calls;
        int advanced = 0;
        /* Model adjust_layout rewriting both gameplay extents and an external
         * camera-rumble call perturbing shake after frame-zero preparation. The
         * clean host simulation state must be restored before capture/tick. */
        g_game_width = is_host ? 304.0f : 320.0f;
        g_game_height = is_host ? 171.0f : 180.0f;
        g_camera_shake = is_host ? 11.0f : 13.0f;
        g_camera_shake_decay = is_host ? 0.5f : 0.75f;
        test_state_store_game_geometry();
        g_require_pinned_width = 1;
        err[0] = '\0';
        if (!ggpo_net_advance(0u, 0u, 0, &checksum, &advanced, err, sizeof(err))) {
            return fail(role, "first advance", err);
        }
        if (!advanced ||
            ggpo_net_frame_count() !=
                (gameplay_wrap_chaos ? TEST_WRAP_CHAOS_START + 1u : 1u)) {
            return fail(role, "first visible tick stalled", NULL);
        }
        if (g_game_width != (is_host ? 304.0f : 320.0f) ||
            g_game_height != (is_host ? 171.0f : 180.0f) ||
            !test_float_near(g_camera_shake,
                             TEST_AUTHORITATIVE_CAMERA_SHAKE) ||
            !test_float_near(g_camera_shake_decay,
                             TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY) ||
            g_last_saved_game_width != TEST_AUTHORITATIVE_GAME_WIDTH ||
            g_last_saved_game_height != TEST_AUTHORITATIVE_GAME_HEIGHT ||
            !test_float_near(g_last_saved_camera_shake,
                             TEST_AUTHORITATIVE_CAMERA_SHAKE) ||
            !test_float_near(g_last_saved_camera_shake_decay,
                             TEST_AUTHORITATIVE_CAMERA_SHAKE_DECAY) ||
            g_width_set_calls <= width_sets_before) {
            return fail(role,
                        "local layout geometry reached capture or native simulation",
                        NULL);
        }
    }

    if (gameplay_disconnect) {
        const uint32_t disconnect_frame = TEST_CHAOS_FRAME_COUNT;
        uint32_t confirmed = 0u;

        /* Exercise a terminal, authenticated BYE only after ordinary gameplay
         * has four complete, mutually checksum-confirmed history generations.
         * The host intentionally owns shutdown while the join keeps servicing
         * the live simulation. */
        for (tick = 0; tick < 8000; tick++) {
            uint32_t f = ggpo_net_frame_count();
            uint32_t p0 = ((f % 5u) == 0u ? 0x01u : 0u) |
                          ((f % 17u) == 0u ? 0x10u : 0u);
            uint32_t p1 = ((f % 7u) == 0u ? 0x02u : 0u) |
                          ((f % 13u) == 0u ? 0x20u : 0u);
            uint32_t checksum = 0u;
            int advanced = 0;

            err[0] = '\0';
            if (!ggpo_net_advance(p0, p1, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                if (!is_host && strstr(err, "peer disconnected")) {
                    uint32_t detected_frame = ggpo_net_frame_count();
                    ggpo_net_stop_for_retry();
                    printf("%s DISCONNECT PASS phase=live frame=%u confirmed=%u detected=1 active=%d\n",
                           role,
                           (unsigned int)detected_frame,
                           (unsigned int)confirmed,
                           ggpo_net_active());
                    return 0;
                }
                return fail(role, "live disconnect gameplay advance", err);
            }
            if (is_host &&
                ggpo_net_frame_count() >= disconnect_frame &&
                ggpo_net_checksum_confirmed_frame(&confirmed) &&
                confirmed >= disconnect_frame - 1u) {
                uint32_t notified_frame = ggpo_net_frame_count();
                ggpo_net_stop();
                if (ggpo_net_active()) {
                    return fail(role, "live disconnect did not stop locally", NULL);
                }
                printf("%s DISCONNECT PASS phase=live frame=%u confirmed=%u notified=1 active=%d\n",
                       role,
                       (unsigned int)notified_frame,
                       (unsigned int)confirmed,
                       ggpo_net_active());
                return 0;
            }
            Sleep(1);
        }
        return fail(role, "live disconnect phase did not complete", NULL);
    }

    if (input_sampling) {
        uint32_t stall_frame = 0u;
        uint32_t input_frame = 0u;
        uint32_t committed_cmd = 0u;
        uint32_t target_checksum = 0u;
        uint32_t confirmed = 0u;
        uint32_t stalls_before = ggpo_net_prediction_stall_count();
        int completed = 0;

        if (is_host) {
            int stall_seen = 0;
            int use_fresh = 0;
            int recovered = 0;
            uint32_t stale_ticks = 0u;

            /* The join peer remains transport-only long enough for the host to
             * hit the hard prediction barrier. Repeated stale wall-tick samples
             * must remain tentative; only the newer sample present when the
             * barrier clears may be assigned to the next delayed input frame. */
            for (tick = 0; tick < 8000; tick++) {
                uint32_t frame_before = ggpo_net_frame_count();
                uint32_t cmd = use_fresh
                    ? TEST_SAMPLING_FRESH_CMD
                    : TEST_SAMPLING_STALE_CMD;
                uint32_t checksum = 0u;
                uint32_t observed_cmd = 0u;
                uint32_t stalls_now;
                int advanced = 0;

                err[0] = '\0';
                if (!ggpo_net_advance(cmd, 0u, 0, &checksum, &advanced,
                                      err, sizeof(err))) {
                    return fail(role, "sampling gameplay advance", err);
                }
                stalls_now = ggpo_net_prediction_stall_count();
                if (!stall_seen && !advanced && stalls_now > stalls_before) {
                    stall_seen = 1;
                    stall_frame = frame_before;
                    input_frame = stall_frame + ggpo_net_input_delay();
                }

                if (stall_seen && !advanced) {
                    if (ggpo_net_frame_count() != stall_frame) {
                        return fail(role, "sampling stall changed simulation frame", NULL);
                    }
                    if (ggpo_net_test_get_local_input(input_frame,
                                                      &observed_cmd)) {
                        return fail(role, "sampling stall committed a tentative input", NULL);
                    }
                    if (!use_fresh) {
                        stale_ticks++;
                        if (stale_ticks >= TEST_SAMPLING_STALE_TICKS) {
                            use_fresh = 1;
                        }
                    }
                } else if (stall_seen && advanced) {
                    if (!use_fresh ||
                        !ggpo_net_test_get_local_input(input_frame,
                                                       &committed_cmd) ||
                        committed_cmd != TEST_SAMPLING_FRESH_CMD) {
                        return fail(role, "sampling recovery committed the stale input", NULL);
                    }
                    recovered = 1;
                    break;
                }
                Sleep(1);
            }
            if (!stall_seen || !use_fresh || !recovered ||
                stale_ticks < TEST_SAMPLING_STALE_TICKS) {
                return fail(role, "sampling prediction barrier was not exercised", NULL);
            }
        } else {
            /* Keep the peer alive and ACKing without producing any new local
             * gameplay input. This is the deterministic source of the host's
             * prediction hole. */
            for (tick = 0; tick < TEST_SAMPLING_SERVICE_TICKS; tick++) {
                err[0] = '\0';
                if (!ggpo_net_test_service_input(err, sizeof(err))) {
                    return fail(role, "sampling transport-only service", err);
                }
                Sleep(1);
            }
        }

        /* Both peers now resume ordinary deterministic input and must converge
         * through a mutually checksum-confirmed frame after the forced stall. */
        for (tick = 0; tick < 8000; tick++) {
            uint32_t f = ggpo_net_frame_count();
            uint32_t p0 = ((f % 5u) == 0u ? 0x01u : 0u) |
                          ((f % 17u) == 0u ? 0x10u : 0u);
            uint32_t p1 = ((f % 7u) == 0u ? 0x02u : 0u) |
                          ((f % 19u) == 0u ? 0x20u : 0u);
            uint32_t checksum = 0u;
            int advanced = 0;
            err[0] = '\0';
            if (!ggpo_net_advance(p0, p1, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                return fail(role, "sampling convergence advance", err);
            }
            if (ggpo_net_frame_count() >= TEST_SAMPLING_TARGET_FRAME &&
                ggpo_net_checksum_confirmed_frame(&confirmed) &&
                confirmed >= TEST_SAMPLING_TARGET_FRAME - 1u &&
                ggpo_net_test_get_history_marker(
                    TEST_SAMPLING_TARGET_FRAME - 1u,
                    &target_checksum)) {
                completed = 1;
                break;
            }
            Sleep(1);
        }
        if (!completed) {
            return fail(role, "sampling peers did not reconverge", NULL);
        }
        for (tick = 0; tick < 180; tick++) {
            err[0] = '\0';
            if (!ggpo_net_test_service_input(err, sizeof(err))) {
                return fail(role, "sampling checksum ACK drain", err);
            }
            Sleep(1);
        }
        if (!test_local_render_geometry_matches(is_host)) {
            return fail(role, "sampling path leaked simulation geometry into rendering", NULL);
        }
        printf("%s SAMPLING PASS target=%u checksum=%u frame=%u horizon=%u stall_frame=%u input_frame=%u stale=%u committed=%u stalls=%u\n",
               role,
               (unsigned int)(TEST_SAMPLING_TARGET_FRAME - 1u),
               (unsigned int)target_checksum,
               (unsigned int)ggpo_net_frame_count(),
               (unsigned int)confirmed,
               (unsigned int)stall_frame,
               (unsigned int)input_frame,
               (unsigned int)TEST_SAMPLING_STALE_CMD,
               (unsigned int)committed_cmd,
               (unsigned int)(ggpo_net_prediction_stall_count() - stalls_before));
        ggpo_net_stop_for_retry();
        return 0;
    }

    if (prematch_skew) {
        uint32_t target_checksum = 0u;
        uint32_t confirmed = 0u;
        uint32_t remote_horizon = 0u;
        uint32_t peer_horizon = 0u;
        int completed = 0;
        for (tick = 0; tick < 5000; tick++) {
            uint32_t checksum = 0u;
            int advanced = 0;
            err[0] = '\0';
            if (!ggpo_net_advance(0u, 0u, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                return fail(role, "skew gameplay advance", err);
            }
            if (ggpo_net_frame_count() >= TEST_SKEW_TARGET_FRAME &&
                ggpo_net_checksum_confirmed_frame(&confirmed) &&
                confirmed >= TEST_SKEW_TARGET_FRAME - 1u &&
                ggpo_net_test_get_history_marker(TEST_SKEW_TARGET_FRAME - 1u,
                                                  &target_checksum)) {
                completed = 1;
                break;
            }
            Sleep(1);
        }
        if (!completed) {
            return fail(role, "skew gameplay did not cross prediction cap", NULL);
        }
        for (tick = 0; tick < 180; tick++) {
            err[0] = '\0';
            if (!ggpo_net_test_service_input(err, sizeof(err))) {
                return fail(role, "skew gameplay ACK drain", err);
            }
            Sleep(1);
        }
        if (!ggpo_net_remote_input_confirmed_frame(&remote_horizon) ||
            !ggpo_net_peer_input_confirmed_frame(&peer_horizon) ||
            remote_horizon < TEST_SKEW_TARGET_FRAME - 1u ||
            peer_horizon < TEST_SKEW_TARGET_FRAME - 1u) {
            return fail(role, "skew gameplay input horizons did not recover", NULL);
        }
        if (!test_local_render_geometry_matches(is_host)) {
            return fail(role, "skew path leaked simulation geometry into rendering", NULL);
        }
        printf("%s SKEW PASS target=%u checksum=%u frame=%u horizon=%u remote=%u peer=%u before=%u after=%u delay=%u\n",
               role,
               (unsigned int)(TEST_SKEW_TARGET_FRAME - 1u),
               (unsigned int)target_checksum,
               (unsigned int)ggpo_net_frame_count(),
               (unsigned int)confirmed,
               (unsigned int)remote_horizon,
               (unsigned int)peer_horizon,
               (unsigned int)skew_before_prepare,
               (unsigned int)skew_after_prepare,
               (unsigned int)ggpo_net_input_delay());
        ggpo_net_stop_for_retry();
        return 0;
    }

    if (gameplay_correction) {
        const uint32_t injection_ready_frame =
            gameplay_long_correction ? 600u : 52u;
        const uint32_t target_frame =
            gameplay_long_correction ? TEST_CHAOS_FRAME_COUNT : 180u;
        uint32_t injected_frame = 0u;
        uint32_t target_checksum = 0u;
        uint32_t confirmed = 0u;
        uint32_t checksum_rx_next = 0u;
        uint32_t checksum_peer_next = 0u;
        uint32_t correction_id = 0u;
        uint32_t snapshot_frame = 0u;
        uint32_t resume_frame = 0u;
        uint32_t transcript = 0u;
        int baseline_ready = 0;
        int correction_seen = 0;
        int checksum_suppressed = 0;
        int would_block_checked = 0;
        int completed = 0;

        if (gameplay_long_correction &&
            !ggpo_net_set_network_sim(20u, 0u, 8u)) {
            return fail(role, "enable long correction network chaos", NULL);
        }

        /* First prove the ordinary checksum channel is live. Then only the
         * join peer corrupts one canonical byte and withholds its checksum
         * payload, forcing the asymmetric detector -> REQUEST path. The long
         * case reaches this point after the 512-slot rings have already reused
         * a generation, with loss/reordering enabled from its first frame. */
        for (tick = 0; tick < 8000; tick++) {
            uint32_t f = ggpo_net_frame_count();
            uint32_t p0 = ((f % 5u) == 0u ? 0x01u : 0u) |
                          ((f % 17u) == 0u ? 0x10u : 0u);
            uint32_t p1 = ((f % 7u) == 0u ? 0x02u : 0u) |
                          ((f % 13u) == 0u ? 0x20u : 0u);
            uint32_t checksum = 0u;
            int advanced = 0;
            err[0] = '\0';
            if (!ggpo_net_advance(p0, p1, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                return fail(role, "correction baseline advance", err);
            }
            if (ggpo_net_frame_count() >= injection_ready_frame &&
                ggpo_net_checksum_confirmed_frame(&confirmed) &&
                confirmed >= injection_ready_frame - 13u) {
                baseline_ready = 1;
                break;
            }
            Sleep(1);
        }
        if (!baseline_ready) {
            return fail(role, "correction checksum baseline did not converge", NULL);
        }
        if (!gameplay_long_correction &&
            !ggpo_net_set_network_sim(20u, 0u, 8u)) {
            return fail(role, "enable correction network chaos", NULL);
        }
#ifdef GGPO_NET_TEST
        ggpo_net_test_set_replay_outgoing(1);
        if (!is_host) {
            ggpo_net_test_set_suppress_checksum_payload(1);
            checksum_suppressed = 1;
            injected_frame = ggpo_net_frame_count();
            g_state[256] ^= 0x5Au;
        }
#endif

        for (tick = 0; tick < 24000; tick++) {
            uint32_t f = ggpo_net_frame_count();
            uint32_t p0 = ((f % 5u) == 0u ? 0x01u : 0u) |
                          ((f % 17u) == 0u ? 0x10u : 0u);
            uint32_t p1 = ((f % 7u) == 0u ? 0x02u : 0u) |
                          ((f % 13u) == 0u ? 0x20u : 0u);
            uint32_t checksum = 0u;
            int advanced = 0;
            err[0] = '\0';
            if (!ggpo_net_advance(p0, p1, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                if (gameplay_disconnect_release_ack && is_host &&
                    strstr(err, "peer disconnected")) {
                    uint32_t detected_frame = ggpo_net_frame_count();
                    uint32_t detected_phase =
                        ggpo_net_test_correction_phase();
                    uint32_t detected_peer_phase =
                        ggpo_net_test_correction_peer_phase();
                    uint32_t detected_id = ggpo_net_correction_id();
                    if (!correction_seen ||
                        detected_phase !=
                            GGPO_NET_TEST_CORRECTION_RELEASE ||
                        detected_peer_phase !=
                            GGPO_NET_TEST_CORRECTION_RELEASE_ACK ||
                        detected_id == 0u) {
                        return fail(role,
                                    "release-ack disconnect arrived outside correction",
                                    err);
                    }
                    ggpo_net_stop_for_retry();
                    printf("%s DISCONNECT PASS phase=release_ack frame=%u correction_phase=%u peer_phase=%u id=%u detected=1 active=%d\n",
                           role,
                           (unsigned int)detected_frame,
                           (unsigned int)detected_phase,
                           (unsigned int)detected_peer_phase,
                           (unsigned int)detected_id,
                           ggpo_net_active());
                    return 0;
                }
                if ((gameplay_disconnect_correction ||
                     gameplay_disconnect_receiving ||
                     gameplay_disconnect_commit ||
                     gameplay_disconnect_release) && !is_host &&
                    strstr(err, "peer disconnected")) {
                    uint32_t detected_frame = ggpo_net_frame_count();
                    uint32_t detected_phase =
                        ggpo_net_test_correction_phase();
                    uint32_t detected_id = ggpo_net_correction_id();
                    if (!correction_seen &&
                        !ggpo_net_awaiting_correction()) {
                        return fail(role,
                                    "disconnect arrived before correction phase",
                                    err);
                    }
#ifdef GGPO_NET_TEST
                    if (checksum_suppressed) {
                        ggpo_net_test_set_suppress_checksum_payload(0);
                    }
#endif
                    ggpo_net_stop_for_retry();
                    printf("%s DISCONNECT PASS phase=%s frame=%u correction_phase=%u id=%u detected=1 active=%d\n",
                           role,
                           gameplay_disconnect_release
                               ? "release"
                               : (gameplay_disconnect_commit
                                      ? "commit"
                                      : (gameplay_disconnect_receiving
                                             ? "receiving"
                                             : "correction")),
                           (unsigned int)detected_frame,
                           (unsigned int)detected_phase,
                           (unsigned int)detected_id,
                           ggpo_net_active());
                    return 0;
                }
                return fail(role, "coordinated correction advance", err);
            }
            if (ggpo_net_test_correction_phase() != 0u ||
                ggpo_net_last_correction_applied_id() != 0u) {
                correction_seen = 1;
            }
#ifdef GGPO_NET_TEST
            if (gameplay_disconnect_release_ack && !is_host &&
                ggpo_net_test_correction_phase() ==
                    GGPO_NET_TEST_CORRECTION_RELEASE_ACK) {
                uint32_t disconnected_frame = ggpo_net_frame_count();
                uint32_t disconnected_phase =
                    ggpo_net_test_correction_phase();
                uint32_t disconnected_peer_phase =
                    ggpo_net_test_correction_peer_phase();
                uint32_t disconnected_id = ggpo_net_correction_id();
                uint32_t disconnected_snapshot =
                    ggpo_net_test_correction_snapshot_frame();
                if (!correction_seen || disconnected_id == 0u ||
                    ggpo_net_test_correction_transcript() == 0u) {
                    return fail(role,
                                "release-ack disconnect lacked barrier identity",
                                NULL);
                }
                ggpo_net_stop();
                if (ggpo_net_active()) {
                    return fail(role,
                                "release-ack disconnect did not stop locally",
                                NULL);
                }
                printf("%s DISCONNECT PASS phase=release_ack frame=%u correction_phase=%u peer_phase=%u id=%u snapshot=%u notified=1 active=%d\n",
                       role,
                       (unsigned int)disconnected_frame,
                       (unsigned int)disconnected_phase,
                       (unsigned int)disconnected_peer_phase,
                       (unsigned int)disconnected_id,
                       (unsigned int)disconnected_snapshot,
                       ggpo_net_active());
                return 0;
            }
            if (is_host &&
                ((gameplay_disconnect_correction &&
                  ggpo_net_test_correction_phase() ==
                      GGPO_NET_TEST_CORRECTION_OFFER) ||
                 (gameplay_disconnect_receiving &&
                  ggpo_net_test_correction_phase() ==
                      GGPO_NET_TEST_CORRECTION_OFFER &&
                  ggpo_net_test_correction_peer_phase() ==
                      GGPO_NET_TEST_CORRECTION_RECEIVING) ||
                 (gameplay_disconnect_commit &&
                  ggpo_net_test_correction_phase() ==
                      GGPO_NET_TEST_CORRECTION_COMMIT) ||
                 (gameplay_disconnect_release &&
                  ggpo_net_test_correction_phase() ==
                      GGPO_NET_TEST_CORRECTION_RELEASE))) {
                uint32_t disconnected_frame = ggpo_net_frame_count();
                uint32_t disconnected_phase =
                    ggpo_net_test_correction_phase();
                uint32_t disconnected_peer_phase =
                    ggpo_net_test_correction_peer_phase();
                uint32_t disconnected_id = ggpo_net_correction_id();
                uint32_t disconnected_snapshot =
                    ggpo_net_test_correction_snapshot_frame();
                if (!correction_seen || disconnected_id == 0u) {
                    return fail(role,
                                "correction disconnect lacked active barrier",
                                NULL);
                }
                if ((gameplay_disconnect_commit ||
                     gameplay_disconnect_release) &&
                    ggpo_net_test_correction_transcript() == 0u) {
                    return fail(role,
                                "post-replay disconnect lacked transcript",
                                NULL);
                }
                ggpo_net_stop();
                if (ggpo_net_active()) {
                    return fail(role,
                                "correction disconnect did not stop locally",
                                NULL);
                }
                printf("%s DISCONNECT PASS phase=%s frame=%u correction_phase=%u peer_phase=%u id=%u snapshot=%u notified=1 active=%d\n",
                       role,
                       gameplay_disconnect_release
                           ? "release"
                           : (gameplay_disconnect_commit
                                  ? "commit"
                                  : (gameplay_disconnect_receiving
                                         ? "receiving"
                                         : "correction")),
                       (unsigned int)disconnected_frame,
                       (unsigned int)disconnected_phase,
                       (unsigned int)disconnected_peer_phase,
                       (unsigned int)disconnected_id,
                       (unsigned int)disconnected_snapshot,
                       ggpo_net_active());
                return 0;
            }
            if (is_host && !would_block_checked &&
                ggpo_net_test_correction_phase() ==
                    GGPO_NET_TEST_CORRECTION_OFFER) {
                uint32_t full_before = 0u;
                uint32_t delta_before = 0u;
                uint32_t full_after = 0u;
                uint32_t delta_after = 0u;
                uint32_t blocked_before =
                    ggpo_net_socket_would_block_count();
                uint32_t sent_before = ggpo_net_packets_sent();
                uint32_t errors_before =
                    ggpo_net_socket_send_error_count();

                ggpo_net_test_get_correction_send_cursor(&full_before,
                                                          &delta_before);
                ggpo_net_test_force_state_chunk_would_block(1u);
                if (ggpo_net_test_send_correction_chunk_once()) {
                    return fail(role, "would-block state chunk reported success", NULL);
                }
                ggpo_net_test_get_correction_send_cursor(&full_after,
                                                          &delta_after);
                if (full_after != full_before || delta_after != delta_before ||
                    ggpo_net_socket_would_block_count() != blocked_before + 1u ||
                    ggpo_net_packets_sent() != sent_before ||
                    ggpo_net_socket_send_error_count() != errors_before) {
                    return fail(role,
                                "would-block advanced correction send state",
                                NULL);
                }
                would_block_checked = 1;
            }
            if (checksum_suppressed &&
                (ggpo_net_awaiting_correction() ||
                 ggpo_net_test_correction_phase() != 0u)) {
                ggpo_net_test_set_suppress_checksum_payload(0);
                checksum_suppressed = 0;
            }
#endif
            ggpo_net_test_get_checksum_ack_next(&checksum_rx_next,
                                                 &checksum_peer_next);
            if (ggpo_net_last_correction_applied_id() != 0u &&
                ggpo_net_test_correction_phase() == 0u &&
                ggpo_net_frame_count() >= target_frame &&
                checksum_rx_next > target_frame - 1u &&
                checksum_peer_next > target_frame - 1u &&
                ggpo_net_checksum_confirmed_frame(&confirmed) &&
                confirmed >= target_frame - 1u &&
                ggpo_net_test_get_history_marker(target_frame - 1u,
                                                  &target_checksum)) {
                completed = 1;
                break;
            }
            Sleep(1);
        }
#ifdef GGPO_NET_TEST
        if (checksum_suppressed) {
            ggpo_net_test_set_suppress_checksum_payload(0);
        }
#endif
        if (!completed) {
            fprintf(stderr,
                    "%s correction detail phase=%u id=%u applied=%u frame=%u confirmed=%u rx_next=%u peer_next=%u\n",
                    role,
                    (unsigned int)ggpo_net_test_correction_phase(),
                    (unsigned int)ggpo_net_correction_id(),
                    (unsigned int)ggpo_net_last_correction_applied_id(),
                    (unsigned int)ggpo_net_frame_count(),
                    (unsigned int)confirmed,
                    (unsigned int)checksum_rx_next,
                    (unsigned int)checksum_peer_next);
            return fail(role, "coordinated correction never completed", NULL);
        }

        /* Repeat the terminal proof long enough for the peer to observe our
         * final NONE/release and checksum ACK despite loss and reordering. */
        for (tick = 0; tick < 240; tick++) {
            err[0] = '\0';
            if (!ggpo_net_test_service_input(err, sizeof(err))) {
                return fail(role, "correction release ACK drain", err);
            }
            Sleep(1);
        }
        ggpo_net_test_get_checksum_ack_next(&checksum_rx_next,
                                             &checksum_peer_next);
        correction_id = ggpo_net_last_correction_applied_id();
        snapshot_frame = ggpo_net_test_correction_snapshot_frame();
        resume_frame = ggpo_net_test_correction_resume_frame();
        transcript = ggpo_net_test_correction_transcript();
        if (!correction_seen || correction_id == 0u ||
            correction_id != ggpo_net_correction_id() ||
            ggpo_net_test_correction_phase() != 0u ||
            snapshot_frame >= resume_frame ||
            resume_frame - snapshot_frame > 512u ||
            transcript == 0u ||
            checksum_rx_next <= target_frame - 1u ||
            checksum_peer_next <= target_frame - 1u ||
            ggpo_net_correction_request_count() == 0u ||
            ggpo_net_sim_dropped_packets() == 0u ||
            ggpo_net_sim_delayed_packets() == 0u ||
            ggpo_net_auth_rejected_packets() == 0u ||
            ggpo_net_socket_send_error_count() != 0u ||
            (is_host && (!would_block_checked ||
                         ggpo_net_socket_would_block_count() == 0u)) ||
            (is_host && ggpo_net_corrections_sent() == 0u) ||
            (!is_host && (ggpo_net_corrections_received() == 0u ||
                          snapshot_frame != injected_frame))) {
            fprintf(stderr,
                    "%s correction invariant detail id=%u current=%u phase=%u snapshot=%u resume=%u transcript=%u injected=%u rx=%u peer=%u requests=%u sent=%u received=%u rejected=%u drop=%u delay=%u\n",
                    role,
                    (unsigned int)correction_id,
                    (unsigned int)ggpo_net_correction_id(),
                    (unsigned int)ggpo_net_test_correction_phase(),
                    (unsigned int)snapshot_frame,
                    (unsigned int)resume_frame,
                    (unsigned int)transcript,
                    (unsigned int)injected_frame,
                    (unsigned int)checksum_rx_next,
                    (unsigned int)checksum_peer_next,
                    (unsigned int)ggpo_net_correction_request_count(),
                    (unsigned int)ggpo_net_corrections_sent(),
                    (unsigned int)ggpo_net_corrections_received(),
                    (unsigned int)ggpo_net_auth_rejected_packets(),
                    (unsigned int)ggpo_net_sim_dropped_packets(),
                    (unsigned int)ggpo_net_sim_delayed_packets());
            return fail(role, "correction barrier invariants were not satisfied", NULL);
        }
        if (!test_local_render_geometry_matches(is_host)) {
            return fail(role, "correction path leaked simulation geometry into rendering", NULL);
        }
        if (!test_palette_matches()) {
            return fail(role, "correction path lost the local palette sidecar", NULL);
        }
        printf("%s CORRECTION PASS long=%d target=%u checksum=%u frame=%u horizon=%u checksum_rx_next=%u checksum_peer_next=%u id=%u snapshot=%u resume=%u transcript=%u injected=%u sent=%u received=%u requests=%u rejected=%u sim_drop=%u sim_delay=%u pred=%u rb=%u would_block=%u send_error=%u deferred=%u\n",
               role,
               gameplay_long_correction,
               (unsigned int)(target_frame - 1u),
               (unsigned int)target_checksum,
               (unsigned int)ggpo_net_frame_count(),
               (unsigned int)confirmed,
               (unsigned int)checksum_rx_next,
               (unsigned int)checksum_peer_next,
               (unsigned int)correction_id,
               (unsigned int)snapshot_frame,
               (unsigned int)resume_frame,
               (unsigned int)transcript,
               (unsigned int)injected_frame,
               (unsigned int)ggpo_net_corrections_sent(),
               (unsigned int)ggpo_net_corrections_received(),
               (unsigned int)ggpo_net_correction_request_count(),
               (unsigned int)ggpo_net_auth_rejected_packets(),
               (unsigned int)ggpo_net_sim_dropped_packets(),
               (unsigned int)ggpo_net_sim_delayed_packets(),
               (unsigned int)ggpo_net_prediction_count(),
               (unsigned int)ggpo_net_rollback_count(),
               (unsigned int)ggpo_net_socket_would_block_count(),
               (unsigned int)ggpo_net_socket_send_error_count(),
               (unsigned int)ggpo_net_socket_send_deferred_count());
        ggpo_net_stop_for_retry();
        return 0;
    }

    if (gameplay_chaos) {
        /* Four complete 512-slot generations exercise actual history reuse,
         * including a second paired mode that crosses UINT32_MAX -> 0. */
        const uint32_t start_frame = gameplay_wrap_chaos
            ? TEST_WRAP_CHAOS_START
            : 0u;
        const uint32_t frame_count = TEST_CHAOS_FRAME_COUNT;
        const uint32_t target_frame = start_frame + frame_count - 1u;
        const char* trace_path = getenv("EGGNOGGPLUS_TEST_STATE_TRACE");
        FILE* trace = NULL;
        uint8_t* trace_scratch = NULL;
        uint32_t trace_next = 0u;
        uint32_t target_checksum = 0u;
        uint32_t confirmed = 0u;
        uint32_t checksum_rx_next = 0u;
        uint32_t checksum_peer_next = 0u;
        int completed = 0;
        int checksum_drained = 0;
        if (!trace_path || !trace_path[0] ||
            !(trace = fopen(trace_path, "wb")) ||
            !(trace_scratch = (uint8_t*)malloc(g_state_size))) {
            if (trace) fclose(trace);
            free(trace_scratch);
            return fail(role, "open exact chaos state trace", NULL);
        }
        if (!ggpo_net_set_network_sim(20u, 1u, 8u)) {
            return fail(role, "enable gameplay network chaos", NULL);
        }
        for (tick = 0; tick < 20000; tick++) {
            uint32_t f = ggpo_net_frame_count();
            uint32_t p0 = ((f % 7u) == 0u ? 0x01u : 0u) |
                          ((f % 19u) == 0u ? 0x04u : 0u);
            uint32_t p1 = ((f % 11u) == 0u ? 0x02u : 0u) |
                          ((f % 23u) == 0u ? 0x08u : 0u);
            uint32_t checksum = 0u;
            int advanced = 0;
            err[0] = '\0';
            if (!ggpo_net_advance(p0, p1, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                char diag[2048];
                ggpo_net_format_diag(diag, sizeof(diag));
                fprintf(stderr, "%s chaos failure diagnostics:\n%s",
                        role, diag);
                return fail(role, "chaos gameplay advance", err);
            }
            if (!test_trace_confirmed_history(trace,
                                              start_frame,
                                              frame_count,
                                              &trace_next,
                                              trace_scratch,
                                              g_state_size)) {
                return fail(role, "capture exact confirmed chaos history", NULL);
            }
            if (ggpo_net_checksum_confirmed_frame(&confirmed) &&
                (confirmed == target_frame ||
                 (int32_t)(confirmed - target_frame) > 0) &&
                ggpo_net_test_get_history_marker(target_frame,
                                                  &target_checksum)) {
                completed = 1;
                break;
            }
            Sleep(1);
        }
        if (!completed) {
            return fail(role, "chaos gameplay never confirmed target frame", NULL);
        }
        /* Keep advancing until any late inputs have actually replayed and both
         * directions prove comparison through the target. A transport-only
         * pump cannot clear rollback_pending, so it is insufficient as the
         * first phase of a terminal checksum barrier. */
        for (tick = 0; tick < 2000; tick++) {
            uint32_t f = ggpo_net_frame_count();
            uint32_t p0 = ((f % 7u) == 0u ? 0x01u : 0u) |
                          ((f % 19u) == 0u ? 0x04u : 0u);
            uint32_t p1 = ((f % 11u) == 0u ? 0x02u : 0u) |
                          ((f % 23u) == 0u ? 0x08u : 0u);
            uint32_t checksum = 0u;
            int advanced = 0;
            err[0] = '\0';
            if (!ggpo_net_advance(p0, p1, 0, &checksum, &advanced,
                                  err, sizeof(err))) {
                return fail(role, "chaos checksum replay drain", err);
            }
            if (!test_trace_confirmed_history(trace,
                                              start_frame,
                                              frame_count,
                                              &trace_next,
                                              trace_scratch,
                                              g_state_size)) {
                return fail(role, "capture drained exact chaos history", NULL);
            }
            ggpo_net_test_get_checksum_ack_next(&checksum_rx_next,
                                                 &checksum_peer_next);
            if (test_frame_after(checksum_rx_next, target_frame) &&
                test_frame_after(checksum_peer_next, target_frame)) {
                checksum_drained = 1;
                break;
            }
            Sleep(1);
        }
        if (!checksum_drained) {
            return fail(role, "chaos checksum barrier never drained", NULL);
        }
        /* Repeat the final cumulative proof after local completion so the peer
         * cannot exit with our last ACK still in a lossy/delayed queue. */
        for (tick = 0; tick < 180; tick++) {
            err[0] = '\0';
            if (!ggpo_net_test_service_input(err, sizeof(err))) {
                return fail(role, "chaos gameplay ACK drain", err);
            }
            Sleep(1);
        }
        ggpo_net_test_get_checksum_ack_next(&checksum_rx_next,
                                             &checksum_peer_next);
        if (!test_frame_after(checksum_rx_next, target_frame) ||
            !test_frame_after(checksum_peer_next, target_frame)) {
            fprintf(stderr,
                     "%s checksum drain detail target=%u rx_next=%u peer_next=%u frame=%u horizon=%u\n",
                     role,
                     (unsigned int)target_frame,
                    (unsigned int)checksum_rx_next,
                    (unsigned int)checksum_peer_next,
                    (unsigned int)ggpo_net_frame_count(),
                    (unsigned int)confirmed);
            return fail(role, "chaos final checksum was not acknowledged both ways", NULL);
        }
        if (!test_local_render_geometry_matches(is_host)) {
            return fail(role, "chaos path leaked simulation geometry into rendering", NULL);
        }
        if (!test_palette_matches()) {
            return fail(role, "rollback path lost the local palette sidecar", NULL);
        }
        if (trace_next != frame_count || fflush(trace) != 0 ||
            fclose(trace) != 0) {
            return fail(role, "finalize exact chaos state trace", NULL);
        }
        trace = NULL;
        free(trace_scratch);
        trace_scratch = NULL;
        printf("%s CHAOS PASS start=%u target=%u wrapped=%d checksum=%u frame=%u horizon=%u checksum_rx_next=%u checksum_peer_next=%u pred=%u rb=%u stalls=%u sim_drop=%u sim_delay=%u\n",
               role,
               (unsigned int)start_frame,
               (unsigned int)target_frame,
               gameplay_wrap_chaos,
               (unsigned int)target_checksum,
               (unsigned int)ggpo_net_frame_count(),
               (unsigned int)confirmed,
               (unsigned int)checksum_rx_next,
               (unsigned int)checksum_peer_next,
               (unsigned int)ggpo_net_prediction_count(),
               (unsigned int)ggpo_net_rollback_count(),
               (unsigned int)ggpo_net_prediction_stall_count(),
               (unsigned int)ggpo_net_sim_dropped_packets(),
               (unsigned int)ggpo_net_sim_delayed_packets());
        ggpo_net_stop_for_retry();
        return 0;
    }

    if (!test_local_render_geometry_matches(is_host)) {
        return fail(role, "normal path leaked simulation geometry into rendering", NULL);
    }
    if (!test_palette_matches()) {
        return fail(role, "normal path lost the local palette sidecar", NULL);
    }
    printf("%s PASS connected=%d ready=%d state_epoch=%u frame=%u rejected=%u restarted=%d layout_finalized=%d layout_ready=%d bootstrap_size=%u state_size=%u\n",
           role, connected_tick, ready_tick,
           (unsigned int)ggpo_net_state_epoch(),
           (unsigned int)ggpo_net_frame_count(),
           (unsigned int)ggpo_net_auth_rejected_packets(),
           restarted,
           layout_finalized,
           ggpo_net_state_layout_ready(),
           (unsigned int)bootstrap_state_size,
           (unsigned int)ggpo_net_state_size());
    ggpo_net_stop_for_retry();
    return 0;
}
