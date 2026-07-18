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

#define TEST_STATE_BYTES 8192u
#define TEST_MATCH_TOKEN \
    "00112233445566778899aabbccddeeff" \
    "fedcba98765432100123456789abcdef"

static uint8_t g_state[TEST_STATE_BYTES];
static uint32_t g_seed;
static int g_fail_load_once;

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

size_t ggpo_ext_game_state_size(void) { return sizeof(g_state); }

int ggpo_ext_save_game_state(void* dst, size_t dst_len, size_t* out_len,
                             uint32_t* out_checksum, char* err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (!dst || dst_len < sizeof(g_state)) return 0;
    memcpy(dst, g_state, sizeof(g_state));
    if (out_len) *out_len = sizeof(g_state);
    if (out_checksum) *out_checksum = test_checksum(g_state, sizeof(g_state));
    return 1;
}

int ggpo_ext_load_game_state(const void* src, size_t src_len, char* err, size_t err_cap) {
    if (!src || src_len != sizeof(g_state)) return 0;
    if (g_fail_load_once) {
        g_fail_load_once = 0;
        if (err && err_cap) snprintf(err, err_cap, "injected one-shot load failure");
        return 0;
    }
    memcpy(g_state, src, sizeof(g_state));
    return 1;
}

int ggpo_ext_advance_frame(const GgpoFrameInputs* inputs, int arg0,
                           uint32_t* out_checksum, char* err, size_t err_cap) {
    uint32_t v;
    (void)arg0;
    (void)err;
    (void)err_cap;
    if (!inputs) return 0;
    memcpy(&v, g_state + 16, sizeof(v));
    v += 1u + inputs->player_cmd[0] + inputs->player_cmd[1];
    memcpy(g_state + 16, &v, sizeof(v));
    if (out_checksum) *out_checksum = test_checksum(g_state, sizeof(g_state));
    return 1;
}

int lua_manager_game_state_rollback_checksum(uint32_t* out_crc, char* err, size_t err_cap) {
    (void)err;
    (void)err_cap;
    if (out_crc) *out_crc = test_checksum(g_state, sizeof(g_state));
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
    const char* match_token = TEST_MATCH_TOKEN;

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
        fprintf(stderr, "usage: prematch_net_test host|join|joinfail local_port remote_port [token] [normal|reject] [tamper|replay]\n");
        return 2;
    }
    is_host = strcmp(argv[1], "host") == 0;
    role = is_host ? "host" : "join";
    g_fail_load_once = strcmp(argv[1], "joinfail") == 0;
    if (argc >= 5) match_token = argv[4];
    if (argc >= 6) expect_rejection = strcmp(argv[5], "reject") == 0;
    if (argc >= 7) {
        tamper_outgoing = strcmp(argv[6], "tamper") == 0;
        replay_outgoing = strcmp(argv[6], "replay") == 0;
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
    memset(g_state, is_host ? 0x11 : 0x22, sizeof(g_state));
    g_seed = 1234u;

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
    if (!(is_host ? ggpo_net_start_host(local_port, err, sizeof(err))
                  : ggpo_net_start_join_deferred(local_port, err, sizeof(err)))) {
        return fail(role, "start", err);
    }
    if (!ggpo_net_set_prematch_hold(1, err, sizeof(err))) {
        return fail(role, "hold", err);
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
        if (connected_tick >= 0 && !released) {
            int delay = is_host ? 40 : 4;
            if (tick - connected_tick >= delay) {
                memset(g_state, is_host ? 0xA5 : 0x5A, sizeof(g_state));
                memcpy(g_state, "FINAL-HOST-STATE", 16);
                err[0] = '\0';
                if (!ggpo_net_set_prematch_hold(0, err, sizeof(err))) {
                    return fail(role, "release", err);
                }
                released = 1;
            }
        }
        if (released && ggpo_net_prematch_ready()) {
            ready_tick = tick;
            break;
        }
        Sleep(1);
    }
    if (connected_tick < 0) return fail(role, "never connected", NULL);
    if (!released) return fail(role, "never released", NULL);
    if (ready_tick < 0) return fail(role, "never ready", NULL);

    err[0] = '\0';
    if (!ggpo_net_prepare_prematch_start(err, sizeof(err))) {
        return fail(role, "prepare", err);
    }
    if (memcmp(g_state, "FINAL-HOST-STATE", 16) != 0 || g_state[128] != 0xA5u) {
        return fail(role, "authoritative host state mismatch", NULL);
    }

    {
        uint32_t checksum = 0;
        int advanced = 0;
        err[0] = '\0';
        if (!ggpo_net_advance(0u, 0u, 0, &checksum, &advanced, err, sizeof(err))) {
            return fail(role, "first advance", err);
        }
        if (!advanced || ggpo_net_frame_count() != 1u) {
            return fail(role, "first visible tick stalled", NULL);
        }
    }

    printf("%s PASS connected=%d ready=%d state_epoch=%u frame=%u rejected=%u\n",
           role, connected_tick, ready_tick,
           (unsigned int)ggpo_net_state_epoch(),
           (unsigned int)ggpo_net_frame_count(),
           (unsigned int)ggpo_net_auth_rejected_packets());
    ggpo_net_stop_for_retry();
    return 0;
}
