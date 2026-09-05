#include <windows.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ggpo_ext.h"
#include "../hooks.h"
#include "../lua_manager.h"
#include "../map_script.h"

/*
 * This test deliberately exercises the production lua_manager.c serializer.
 * The game stores its native state at fixed 32-bit addresses. A test-only
 * binding seam points those production serializer variables at this fixture;
 * the save/validate/canonicalize/load implementation itself is unchanged.
 */
#define GAME_REGION_BASE        ((uintptr_t)0x00440000u)
#define GAME_REGION_END         ((uintptr_t)0x0055c000u)
#define GAME_REGION_SIZE        ((size_t)(GAME_REGION_END - GAME_REGION_BASE))

#define ADDR_MRAND_SEED           ((uintptr_t)0x00496da0u)
#define ADDR_MAD_TICKS            ((uintptr_t)0x0045f160u)
#define ADDR_PARTICLE_STATE       ((uintptr_t)0x00526420u)
#define ADDR_TRANSIENT_GAME_STATE ((uintptr_t)0x00541e00u)
#define ADDR_LEADER               ((uintptr_t)0x00541e0cu)
#define ADDR_WATERFALL_FX         ((uintptr_t)0x00541e44u)
#define ADDR_SOUND_SWORD_LAST_TICK ((uintptr_t)0x00541ef8u)
#define ADDR_MINE_ANIM_LAST_TICK   ((uintptr_t)0x00541efcu)
#define ADDR_CROWD_CHEER_LAST_TICK ((uintptr_t)0x00541f00u)
#define ADDR_COLOUR_LERP_BLOCK     ((uintptr_t)0x00541f6cu)
#define ADDR_LERP_TIME             ((uintptr_t)0x00542040u)
#define ADDR_GAME_DO_LERP_COLOURS  ((uintptr_t)0x00542050u)
#define ADDR_PLAYER_ARRAY         ((uintptr_t)0x00542058u)
#define ADDR_CONTROLLER           ((uintptr_t)0x00542060u)
#define ADDR_LOSER                ((uintptr_t)0x00542064u)
#define ADDR_THINGS               ((uintptr_t)0x00542080u)
#define ADDR_THING_INFO           ((uintptr_t)0x00543640u)
#define ADDR_ROOM_INFO            ((uintptr_t)0x00543700u)
#define ADDR_GAME_TICKS           ((uintptr_t)0x00547ba0u)
#define ADDR_CAMERA_X              ((uintptr_t)0x0055a360u)
#define ADDR_CAMERA_Y              ((uintptr_t)0x0055a364u)
#define ADDR_CAMERA_SHAKE          ((uintptr_t)0x0055a37cu)
#define ADDR_CAMERA_SHAKE_DECAY    ((uintptr_t)0x0055a380u)
#define ADDR_GAME_W                ((uintptr_t)0x0055a394u)
#define ADDR_GAME_H                ((uintptr_t)0x0055a324u)
#define ADDR_RESUMED               ((uintptr_t)0x00448334u)
#define ADDR_TILEMAP_DATA_PTR      ((uintptr_t)0x0054a1e4u)
#define ADDR_TILEMAP_W             ((uintptr_t)0x0054a1e8u)
#define ADDR_TILEMAP_H             ((uintptr_t)0x0054a1ecu)
#define ADDR_SCORE_P0              ((uintptr_t)0x0055a314u)

#define PLAYER_SIZE              ((size_t)0x15cu)
#define PLAYER_PAGE_SIZE         ((size_t)0x1000u)
#define TILEMAP_W                3
#define TILEMAP_H                2
#define TILEMAP_BYTES            ((size_t)TILEMAP_W * (size_t)TILEMAP_H * sizeof(uint32_t))

#define LEADER_SENTINEL          ((uintptr_t)0x13572468u)
#define LOSER_SENTINEL           ((uintptr_t)0x24681357u)
#define WATERFALL_SENTINEL       ((uintptr_t)0x35791357u)
#define INJECTED_POINTER         ((uintptr_t)0x5a5aa5a5u)

static int g_failures = 0;
static uint8_t* g_native_state_fixture = NULL;

#define NATIVE_AT(address) \
    (g_native_state_fixture + ((uintptr_t)(address) - GAME_REGION_BASE))

static void copy_native_regions(uint8_t* dst) {
    memcpy(dst, g_native_state_fixture, GAME_REGION_SIZE);
}

static int native_regions_equal(const uint8_t* snapshot) {
    return memcmp(snapshot, g_native_state_fixture, GAME_REGION_SIZE) == 0;
}

#define CHECK(condition, message)                                                \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__);      \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void fill_pattern(void* dst, size_t len, uint32_t seed) {
    uint8_t* bytes = (uint8_t*)dst;
    uint32_t value = seed;
    size_t i;
    for (i = 0; i < len; ++i) {
        value = value * UINT32_C(1664525) + UINT32_C(1013904223);
        bytes[i] = (uint8_t)(value >> 24);
    }
}

static size_t field_offset(const char* name, size_t blob_len) {
    size_t offset;
    for (offset = 0; offset < blob_len; ++offset) {
        const char* found = lua_manager_game_state_offset_name(offset);
        if (found && strcmp(found, name) == 0) return offset;
    }
    return SIZE_MAX;
}

static int setup_native_state(uint8_t** out_player0,
                              uint8_t** out_player1,
                              uint8_t** out_tilemap) {
    uint8_t* player0;
    uint8_t* player1;
    uint8_t* tilemap;

    char bind_err[256];

    g_native_state_fixture = (uint8_t*)VirtualAlloc(
        NULL, GAME_REGION_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_native_state_fixture) {
        fprintf(stderr, "FAIL: could not allocate synthetic native state\n");
        return 0;
    }
    bind_err[0] = '\0';
    if (!lua_manager_test_bind_native_state(g_native_state_fixture,
                                             GAME_REGION_SIZE,
                                             bind_err, sizeof(bind_err))) {
        fprintf(stderr, "FAIL: could not bind synthetic native state: %s\n",
                bind_err[0] ? bind_err : "no diagnostic");
        VirtualFree(g_native_state_fixture, 0, MEM_RELEASE);
        g_native_state_fixture = NULL;
        return 0;
    }
    hooks_test_bind_mad_ticks(
        (volatile uint32_t*)NATIVE_AT(ADDR_MAD_TICKS));

    player0 = (uint8_t*)VirtualAlloc(NULL, PLAYER_PAGE_SIZE,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    player1 = (uint8_t*)VirtualAlloc(NULL, PLAYER_PAGE_SIZE,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    tilemap = (uint8_t*)VirtualAlloc(NULL, PLAYER_PAGE_SIZE,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!player0 || !player1 || !tilemap) {
        fprintf(stderr, "FAIL: could not allocate synthetic player/tilemap pages\n");
        if (player0) VirtualFree(player0, 0, MEM_RELEASE);
        if (player1) VirtualFree(player1, 0, MEM_RELEASE);
        if (tilemap) VirtualFree(tilemap, 0, MEM_RELEASE);
        VirtualFree(g_native_state_fixture, 0, MEM_RELEASE);
        g_native_state_fixture = NULL;
        return 0;
    }

    fill_pattern(g_native_state_fixture, GAME_REGION_SIZE,
                 UINT32_C(0x10203040));
    fill_pattern(player0, PLAYER_PAGE_SIZE, UINT32_C(0x11111111));
    fill_pattern(player1, PLAYER_PAGE_SIZE, UINT32_C(0x22222222));
    fill_pattern(tilemap, PLAYER_PAGE_SIZE, UINT32_C(0x33333333));

    *(uintptr_t*)NATIVE_AT(ADDR_PLAYER_ARRAY) = (uintptr_t)player0;
    *(uintptr_t*)NATIVE_AT(ADDR_PLAYER_ARRAY + sizeof(uintptr_t)) =
        (uintptr_t)player1;
    *(uintptr_t*)NATIVE_AT(ADDR_CONTROLLER) = (uintptr_t)player1;
    *(uintptr_t*)NATIVE_AT(ADDR_LEADER) = LEADER_SENTINEL;
    *(uintptr_t*)NATIVE_AT(ADDR_WATERFALL_FX) = WATERFALL_SENTINEL;
    *(uintptr_t*)NATIVE_AT(ADDR_LOSER) = LOSER_SENTINEL;
    *(uintptr_t*)NATIVE_AT(ADDR_TILEMAP_DATA_PTR) = (uintptr_t)tilemap;
    *(int*)NATIVE_AT(ADDR_TILEMAP_W) = TILEMAP_W;
    *(int*)NATIVE_AT(ADDR_TILEMAP_H) = TILEMAP_H;
    *(uint32_t*)NATIVE_AT(ADDR_MRAND_SEED) = UINT32_C(0x1234abcd);
    *(uint32_t*)NATIVE_AT(ADDR_GAME_TICKS) = UINT32_C(0x10293847);
    *(uint32_t*)NATIVE_AT(ADDR_MAD_TICKS) = UINT32_C(0x10293847);
    *(uint32_t*)NATIVE_AT(ADDR_SOUND_SWORD_LAST_TICK) = UINT32_C(0xa1a2a3a4);
    /* Model a second mine callback in the current native tick. Restoring this
     * value must preserve the equality guard that suppresses that callback. */
    *(uint32_t*)NATIVE_AT(ADDR_MINE_ANIM_LAST_TICK) = UINT32_C(0x10293847);
    *(uint32_t*)NATIVE_AT(ADDR_CROWD_CHEER_LAST_TICK) = UINT32_C(0xb1b2b3b4);
    *(int*)NATIVE_AT(ADDR_GAME_DO_LERP_COLOURS) = 1;
    *(int*)NATIVE_AT(ADDR_LERP_TIME) = 17;
    for (int i = 0; i < LUA_GAME_PALETTE_FLOATS; i++) {
        ((float*)NATIVE_AT(ADDR_COLOUR_LERP_BLOCK))[i] =
            0.01f * (float)(i + 1);
    }
    *(float*)NATIVE_AT(ADDR_CAMERA_X) = 12.5f;
    *(float*)NATIVE_AT(ADDR_CAMERA_Y) = -3.25f;
    *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE) = 5.0f;
    *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE_DECAY) = 0.95f;
    *(float*)NATIVE_AT(ADDR_GAME_W) = 288.0f;
    *(float*)NATIVE_AT(ADDR_GAME_H) = 160.0f;
    *(int*)NATIVE_AT(ADDR_RESUMED) = 1;

    *out_player0 = player0;
    *out_player1 = player1;
    *out_tilemap = tilemap;
    return 1;
}

static void mutate_serialized_live_state(uint8_t* player0,
                                         uint8_t* player1,
                                         uint8_t* tilemap) {
    player0[17] ^= UINT8_C(0xff);
    player1[71] ^= UINT8_C(0xa5);
    *(uint32_t*)NATIVE_AT(ADDR_SCORE_P0) ^= UINT32_C(0x6c6c6c6c);
    *(uint32_t*)NATIVE_AT(ADDR_THINGS + 0x200u) ^= UINT32_C(0x01020304);
    *(uint32_t*)NATIVE_AT(ADDR_THING_INFO + 0x20u) ^= UINT32_C(0x10203040);
    *(uint32_t*)NATIVE_AT(ADDR_ROOM_INFO + 0x120u) ^= UINT32_C(0x55667788);
    *(uint32_t*)NATIVE_AT(ADDR_PARTICLE_STATE + 0x400u) ^=
        UINT32_C(0x88776655);
    *(uint32_t*)tilemap ^= UINT32_C(0xf0f0f0f0);
    *(uintptr_t*)NATIVE_AT(ADDR_CONTROLLER) = 0;
    *(uintptr_t*)NATIVE_AT(ADDR_LEADER) = 0;
    *(uintptr_t*)NATIVE_AT(ADDR_WATERFALL_FX) = 0;
    *(uintptr_t*)NATIVE_AT(ADDR_LOSER) = 0;
}

static int expect_call(int result, const char* operation, const char* err) {
    if (!result) {
        fprintf(stderr, "FAIL: %s: %s\n", operation,
                (err && err[0]) ? err : "no diagnostic");
        g_failures++;
        return 0;
    }
    return 1;
}

int main(void) {
    uint8_t* player0 = NULL;
    uint8_t* player1 = NULL;
    uint8_t* tilemap = NULL;
    uint8_t* raw = NULL;
    uint8_t* raw_roundtrip = NULL;
    uint8_t* canonical = NULL;
    uint8_t* injected = NULL;
    uint8_t* before_region = NULL;
    uint8_t before_player0[PLAYER_SIZE];
    uint8_t before_player1[PLAYER_SIZE];
    uint8_t before_tilemap[TILEMAP_BYTES];
    LuaGamePaletteState palette_baseline;
    LuaGamePaletteState palette_after;
    LuaGamePaletteState invalid_palette;
    size_t capacity;
    size_t raw_len = 0;
    size_t raw_roundtrip_len = 0;
    size_t canonical_len = 0;
    size_t leader_raw_offset;
    size_t loser_raw_offset;
    size_t waterfall_raw_offset;
    size_t transient_offset;
    size_t mine_anim_last_tick_offset;
    size_t rng_seed_offset;
    size_t camera_x_offset;
    size_t camera_shake_offset;
    size_t camera_shake_decay_offset;
    size_t game_w_offset;
    size_t game_h_offset;
    size_t resumed_offset;
    uint32_t raw_checksum = 0;
    uint32_t canonical_checksum = 0;
    uint32_t injected_checksum = 0;
    LuaGameStateRollbackSummary canonical_summary;
    DWORD old_protect = 0;
    DWORD ignored_protect = 0;
    char err[256];

    if (!setup_native_state(&player0, &player1, &tilemap)) return 1;

    CHECK(lua_manager_game_palette_capture(&palette_baseline),
          "could not capture coherent native palette baseline");
    invalid_palette = palette_baseline;
    invalid_palette.pending = 2;
    CHECK(!lua_manager_game_palette_restore(&invalid_palette),
          "palette restore accepted an invalid pending flag");
    CHECK(lua_manager_game_palette_capture(&palette_after) &&
              memcmp(&palette_after, &palette_baseline,
                     sizeof(palette_after)) == 0,
          "invalid pending flag partially mutated native palette");
    invalid_palette = palette_baseline;
    invalid_palette.colours[7] = NAN;
    CHECK(!lua_manager_game_palette_restore(&invalid_palette),
          "palette restore accepted a nonfinite colour");
    CHECK(lua_manager_game_palette_capture(&palette_after) &&
              memcmp(&palette_after, &palette_baseline,
                     sizeof(palette_after)) == 0,
          "nonfinite palette input partially mutated native palette");

    {
        uint32_t original_seed = *(uint32_t*)NATIVE_AT(ADDR_MRAND_SEED);
        float original_x = *(float*)NATIVE_AT(ADDR_CAMERA_X);
        float original_y = *(float*)NATIVE_AT(ADDR_CAMERA_Y);
        float original_shake = *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE);
        float original_decay = *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE_DECAY);
        float width = 0.0f;
        float height = 0.0f;
        uint8_t* geometry_page = (uint8_t*)((uintptr_t)NATIVE_AT(ADDR_GAME_H) &
                                            ~(uintptr_t)0xfffu);
        DWORD geometry_old_protect = 0;
        DWORD geometry_ignored_protect = 0;

        CHECK(!lua_manager_game_width(NULL),
              "simulation-width getter accepted a null output");
        CHECK(!lua_manager_game_height(NULL),
              "simulation-height getter accepted a null output");
        CHECK(!lua_manager_game_camera_shake(NULL, &height) &&
                  !lua_manager_game_camera_shake(&width, NULL),
              "camera-shake getter accepted a null output");
        CHECK(!lua_manager_game_set_width(0.0f) &&
                  !lua_manager_game_set_width(-1.0f) &&
                  !lua_manager_game_set_width(NAN) &&
                  !lua_manager_game_set_width(INFINITY) &&
                  !lua_manager_game_set_width(4097.0f),
              "simulation-width setter accepted an invalid extent");
        CHECK(!lua_manager_game_set_height(0.0f) &&
                  !lua_manager_game_set_height(-1.0f) &&
                  !lua_manager_game_set_height(NAN) &&
                  !lua_manager_game_set_height(INFINITY) &&
                  !lua_manager_game_set_height(4097.0f),
              "simulation-height setter accepted an invalid extent");
        CHECK(!lua_manager_game_set_sim_state(UINT32_C(0xaabbccdd),
                                               44.0f, 55.0f,
                                               original_shake, original_decay,
                                               288.0f, NAN),
              "combined simulation restore accepted invalid geometry");
        CHECK(!lua_manager_game_set_sim_state(UINT32_C(0xaabbccdd),
                                               44.0f, 55.0f,
                                               NAN, original_decay,
                                               288.0f, 160.0f),
              "combined simulation restore accepted invalid camera shake");
        CHECK(*(uint32_t*)NATIVE_AT(ADDR_MRAND_SEED) == original_seed &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_X) == original_x &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_Y) == original_y &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE) == original_shake &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE_DECAY) == original_decay,
              "invalid combined simulation restore partially mutated state");
        CHECK(lua_manager_game_set_sim_state(original_seed,
                                              original_x, original_y,
                                              original_shake, original_decay,
                                              300.0f, 170.0f),
              "valid combined simulation restore failed");
        CHECK(lua_manager_game_width(&width) && width == 300.0f &&
                  lua_manager_game_height(&height) && height == 170.0f,
              "valid combined simulation geometry did not roundtrip");
        CHECK(lua_manager_game_set_sim_state(original_seed,
                                              original_x, original_y,
                                              original_shake, original_decay,
                                              288.0f, 160.0f),
              "could not restore baseline simulation geometry");

        CHECK(VirtualProtect(geometry_page, PLAYER_PAGE_SIZE, PAGE_READONLY,
                             &geometry_old_protect) != 0,
              "could not make simulation-geometry page read-only");
        CHECK(!lua_manager_game_set_sim_state(UINT32_C(0x11223344),
                                               99.0f, 88.0f,
                                               7.0f, 0.9f,
                                               320.0f, 180.0f),
              "combined simulation restore accepted read-only geometry");
        CHECK(*(uint32_t*)NATIVE_AT(ADDR_MRAND_SEED) == original_seed &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_X) == original_x &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_Y) == original_y &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE) == original_shake &&
                  *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE_DECAY) == original_decay &&
                  *(float*)NATIVE_AT(ADDR_GAME_W) == 288.0f &&
                  *(float*)NATIVE_AT(ADDR_GAME_H) == 160.0f,
              "read-only combined simulation restore partially mutated state");
        if (geometry_old_protect != 0) {
            CHECK(VirtualProtect(geometry_page, PLAYER_PAGE_SIZE,
                                 geometry_old_protect,
                                 &geometry_ignored_protect) != 0,
                  "could not restore simulation-geometry page protection");
        }
    }

    capacity = ggpo_ext_game_state_size();
    CHECK(capacity > 0, "production serializer reported zero capacity");
    raw = (uint8_t*)malloc(capacity);
    raw_roundtrip = (uint8_t*)malloc(capacity);
    canonical = (uint8_t*)malloc(capacity);
    injected = (uint8_t*)malloc(capacity);
    before_region = (uint8_t*)malloc(GAME_REGION_SIZE);
    if (!raw || !raw_roundtrip || !canonical || !injected || !before_region) {
        fprintf(stderr, "FAIL: serializer test allocation failed\n");
        return 1;
    }

    err[0] = '\0';
    if (!expect_call(ggpo_ext_save_game_state_raw(raw, capacity, &raw_len,
                                                   err, sizeof(err)),
                     "raw save", err)) {
        return 1;
    }
    CHECK(raw_len > 0 && raw_len <= capacity, "raw save length is invalid");

    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_blob(raw, raw_len, &raw_checksum,
                                                err, sizeof(err)),
                "generic raw rollback validation", err);
    err[0] = '\0';
    CHECK(!ggpo_ext_validate_rollback_transport_blob(raw, raw_len,
                                                      &injected_checksum,
                                                      err, sizeof(err)),
          "pointer-bearing raw save passed transport validation");

    mutate_serialized_live_state(player0, player1, tilemap);
    err[0] = '\0';
    expect_call(ggpo_ext_load_game_state_raw(raw, raw_len, err, sizeof(err)),
                "raw load", err);
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_LEADER) == LEADER_SENTINEL,
          "raw load lost non-player leader sentinel");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_LOSER) == LOSER_SENTINEL,
          "raw load lost non-player loser sentinel");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_WATERFALL_FX) == WATERFALL_SENTINEL,
          "raw load lost waterfall pointer sentinel");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_CONTROLLER) == (uintptr_t)player1,
          "raw load did not remap controller to the current player");

    err[0] = '\0';
    expect_call(ggpo_ext_save_game_state_raw(raw_roundtrip, capacity,
                                             &raw_roundtrip_len,
                                             err, sizeof(err)),
                "raw re-save", err);
    CHECK(raw_roundtrip_len == raw_len,
          "raw load/re-save changed serialized length");
    CHECK(raw_roundtrip_len == raw_len &&
              memcmp(raw, raw_roundtrip, raw_len) == 0,
          "raw save/load/re-save was not byte-identical");

    err[0] = '\0';
    expect_call(ggpo_ext_save_game_state(canonical, capacity, &canonical_len,
                                         &canonical_checksum,
                                         err, sizeof(err)),
                "canonical rollback save", err);
    CHECK(canonical_len == raw_len, "canonical save changed blob length");
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    canonical, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "canonical transport validation", err);
    CHECK(injected_checksum == canonical_checksum,
          "canonical save and transport validator checksums differ");
    CHECK(raw_checksum == canonical_checksum,
          "generic raw validation and canonical save checksums differ");
    memset(&canonical_summary, 0, sizeof(canonical_summary));
    err[0] = '\0';
    expect_call(lua_manager_game_state_analyze_canonical_rollback_blob(
                    canonical, canonical_len, &injected_checksum,
                    &canonical_summary, err, sizeof(err)),
                "single-capture checksum/summary analysis", err);
    CHECK(injected_checksum == canonical_checksum &&
              canonical_summary.full_crc == canonical_checksum,
          "single captured boundary produced different checksum/summary CRCs");

    leader_raw_offset = field_offset("leader_raw", canonical_len);
    loser_raw_offset = field_offset("loser_raw", canonical_len);
    waterfall_raw_offset = field_offset("waterfall_fx_raw", canonical_len);
    transient_offset = field_offset("transient_game_state", canonical_len);
    rng_seed_offset = field_offset("rng_seed", canonical_len);
    camera_x_offset = field_offset("camera_x", canonical_len);
    camera_shake_offset = field_offset("camera_shake", canonical_len);
    camera_shake_decay_offset = field_offset("camera_shake_decay", canonical_len);
    game_w_offset = field_offset("game_w", canonical_len);
    game_h_offset = field_offset("game_h", canonical_len);
    resumed_offset = field_offset("resumed", canonical_len);
    CHECK(leader_raw_offset != SIZE_MAX, "leader_raw offset was not discoverable");
    CHECK(loser_raw_offset != SIZE_MAX, "loser_raw offset was not discoverable");
    CHECK(waterfall_raw_offset != SIZE_MAX,
          "waterfall_fx_raw offset was not discoverable");
    CHECK(transient_offset != SIZE_MAX,
          "transient_game_state offset was not discoverable");
    CHECK(rng_seed_offset != SIZE_MAX, "rng_seed offset was not discoverable");
    CHECK(camera_x_offset != SIZE_MAX, "camera_x offset was not discoverable");
    CHECK(camera_shake_offset != SIZE_MAX,
          "camera_shake offset was not discoverable");
    CHECK(camera_shake_decay_offset != SIZE_MAX,
          "camera_shake_decay offset was not discoverable");
    CHECK(game_w_offset != SIZE_MAX, "game_w offset was not discoverable");
    CHECK(game_h_offset != SIZE_MAX, "game_h offset was not discoverable");
    CHECK(resumed_offset != SIZE_MAX, "resumed offset was not discoverable");
    if (leader_raw_offset == SIZE_MAX || loser_raw_offset == SIZE_MAX ||
        waterfall_raw_offset == SIZE_MAX ||
        transient_offset == SIZE_MAX || rng_seed_offset == SIZE_MAX ||
        camera_x_offset == SIZE_MAX || camera_shake_offset == SIZE_MAX ||
        camera_shake_decay_offset == SIZE_MAX || game_w_offset == SIZE_MAX ||
        game_h_offset == SIZE_MAX ||
        resumed_offset == SIZE_MAX) {
        return 1;
    }
    mine_anim_last_tick_offset = transient_offset +
        (size_t)(ADDR_MINE_ANIM_LAST_TICK - ADDR_TRANSIENT_GAME_STATE);
    CHECK(*(uintptr_t*)(canonical + leader_raw_offset) == 0,
          "canonical save retained leader raw pointer");
    CHECK(*(uintptr_t*)(canonical + loser_raw_offset) == 0,
          "canonical save retained loser raw pointer");
    CHECK(*(uintptr_t*)(canonical + waterfall_raw_offset) == 0,
          "canonical save retained waterfall raw pointer");
    CHECK(*(uint32_t*)(canonical + transient_offset +
                      (ADDR_SOUND_SWORD_LAST_TICK -
                       ADDR_TRANSIENT_GAME_STATE)) == 0,
          "canonical save retained sword sound debounce stamp");
    CHECK(*(uint32_t*)(canonical + transient_offset +
                      (ADDR_CROWD_CHEER_LAST_TICK -
                       ADDR_TRANSIENT_GAME_STATE)) == 0,
          "canonical save retained crowd cheer debounce stamp");
    CHECK(*(uint32_t*)(canonical + mine_anim_last_tick_offset) ==
              UINT32_C(0x10293847),
          "canonical save erased authoritative mine animation tick guard");

    {
        uint8_t* geometry_page = (uint8_t*)((uintptr_t)NATIVE_AT(ADDR_GAME_H) &
                                            ~(uintptr_t)0xfffu);
        DWORD geometry_old_protect = 0;
        DWORD geometry_ignored_protect = 0;
        copy_native_regions(before_region);
        CHECK(VirtualProtect(geometry_page, PLAYER_PAGE_SIZE, PAGE_READONLY,
                             &geometry_old_protect) != 0,
              "could not protect authoritative simulation geometry");
        err[0] = '\0';
        CHECK(!ggpo_ext_validate_rollback_transport_blob(
                  canonical, canonical_len, &injected_checksum,
                  err, sizeof(err)),
              "rollback preflight accepted read-only simulation geometry");
        CHECK(strstr(err, "authoritative simulation state unavailable") != NULL,
              "read-only geometry did not report authoritative-state failure");
        err[0] = '\0';
        CHECK(!ggpo_ext_load_game_state(canonical, canonical_len,
                                        err, sizeof(err)),
              "rollback load accepted read-only simulation geometry");
        CHECK(native_regions_equal(before_region),
              "failed simulation-geometry preflight mutated native state");
        if (geometry_old_protect != 0) {
            CHECK(VirtualProtect(geometry_page, PLAYER_PAGE_SIZE,
                                 geometry_old_protect,
                                 &geometry_ignored_protect) != 0,
                  "could not restore authoritative geometry protection");
        }
    }

    /* Tilemap globals live on a separate page from the tile allocation. A
     * readable but non-writable dimensions page formerly passed preflight and
     * allowed all other regions to be committed. An unreadable page silently
     * turned capture into an empty tilemap snapshot. */
    {
        uint8_t* layout_page = (uint8_t*)((uintptr_t)NATIVE_AT(ADDR_TILEMAP_W) &
                                         ~(uintptr_t)0xfffu);
        DWORD layout_old_protect = 0;
        DWORD layout_ignored_protect = 0;
        *(uint32_t*)NATIVE_AT(ADDR_SCORE_P0) ^= UINT32_C(0x11223344);
        copy_native_regions(before_region);
        memcpy(before_player0, player0, sizeof(before_player0));
        memcpy(before_player1, player1, sizeof(before_player1));
        memcpy(before_tilemap, tilemap, sizeof(before_tilemap));
        CHECK(VirtualProtect(layout_page, PLAYER_PAGE_SIZE, PAGE_READONLY,
                             &layout_old_protect) != 0,
              "could not protect native tilemap layout globals");
        err[0] = '\0';
        CHECK(!ggpo_ext_validate_rollback_transport_blob(
                  canonical, canonical_len, &injected_checksum,
                  err, sizeof(err)),
              "rollback preflight accepted read-only tilemap globals");
        CHECK(strstr(err, "native state unavailable") != NULL,
              "layout preflight did not identify inaccessible native state");
        CHECK(!ggpo_ext_load_game_state(canonical, canonical_len,
                                        err, sizeof(err)),
              "rollback load accepted read-only tilemap globals");
        CHECK(native_regions_equal(before_region) &&
                  memcmp(before_player0, player0, sizeof(before_player0)) == 0 &&
                  memcmp(before_player1, player1, sizeof(before_player1)) == 0 &&
                  memcmp(before_tilemap, tilemap, sizeof(before_tilemap)) == 0,
              "failed scalar preflight partially committed a snapshot");
        CHECK(VirtualProtect(layout_page, PLAYER_PAGE_SIZE, PAGE_NOACCESS,
                             &layout_ignored_protect) != 0,
              "could not make native tilemap globals unreadable");
        memset(injected, 0x5a, capacity);
        memcpy(raw_roundtrip, injected, capacity);
        CHECK(!ggpo_ext_save_game_state_raw(injected, capacity, NULL,
                                            err, sizeof(err)),
              "capture silently replaced inaccessible tilemap with an empty map");
        CHECK(memcmp(injected, raw_roundtrip, capacity) == 0,
              "failed scalar capture preflight modified destination");
        if (layout_old_protect != 0) {
            CHECK(VirtualProtect(layout_page, PLAYER_PAGE_SIZE,
                                 layout_old_protect,
                                 &layout_ignored_protect) != 0,
                  "could not restore native layout page protection");
        }
        CHECK(native_regions_equal(before_region),
              "failed scalar capture preflight modified native state");
        *(uint32_t*)NATIVE_AT(ADDR_SCORE_P0) ^= UINT32_C(0x11223344);
    }

    {
        const char* role_names[] = { "leader_mode", "loser_mode" };
        size_t role;
        for (role = 0; role < sizeof(role_names) / sizeof(role_names[0]); ++role) {
            size_t offset = field_offset(role_names[role], canonical_len);
            CHECK(offset != SIZE_MAX, "player role offset unavailable");
            if (offset == SIZE_MAX) continue;
            memcpy(injected, canonical, canonical_len);
            *(uint32_t*)(injected + offset) = UINT32_MAX;
            copy_native_regions(before_region);
            CHECK(!ggpo_ext_validate_rollback_transport_blob(
                      injected, canonical_len, &injected_checksum,
                      err, sizeof(err)),
                  "unknown player role passed transport preflight");
            CHECK(!ggpo_ext_load_game_state(injected, canonical_len,
                                            err, sizeof(err)),
                  "unknown player role passed load preflight");
            CHECK(native_regions_equal(before_region),
                  "invalid player role load modified native state");
        }
    }

    memcpy(injected, canonical, canonical_len);
    *(uintptr_t*)(injected + leader_raw_offset) = INJECTED_POINTER;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_blob(injected, canonical_len,
                                                &injected_checksum,
                                                err, sizeof(err)),
                "generic masked-pointer validation", err);
    CHECK(injected_checksum == canonical_checksum,
          "checksum-masked header pointer changed generic checksum");
    err[0] = '\0';
    CHECK(!ggpo_ext_validate_rollback_transport_blob(
              injected, canonical_len, &injected_checksum, err, sizeof(err)),
          "checksum-masked header pointer injection passed transport validation");

    memcpy(injected, canonical, canonical_len);
    *(uintptr_t*)(injected + transient_offset +
                  (ADDR_PLAYER_ARRAY - ADDR_TRANSIENT_GAME_STATE)) =
        INJECTED_POINTER;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_blob(injected, canonical_len,
                                                &injected_checksum,
                                                err, sizeof(err)),
                "generic transient-pointer validation", err);
    CHECK(injected_checksum == canonical_checksum,
          "checksum-masked transient pointer changed generic checksum");
    err[0] = '\0';
    CHECK(!ggpo_ext_validate_rollback_transport_blob(
              injected, canonical_len, &injected_checksum, err, sizeof(err)),
          "checksum-masked transient pointer injection passed transport validation");

    memcpy(injected, canonical, canonical_len);
    *(uint32_t*)(injected + rng_seed_offset) ^= UINT32_C(0x01020304);
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "gameplay RNG checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "gameplay RNG change was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(uint32_t*)(injected + mine_anim_last_tick_offset) ^=
        UINT32_C(0x01010101);
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "mine animation guard checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "mine animation tick guard was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + camera_x_offset) = 13.5f;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "simulation camera checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "simulation camera change was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + camera_shake_offset) = 7.0f;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "camera-shake checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "camera shake was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + camera_shake_decay_offset) = 0.9f;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "camera-shake decay checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "camera shake decay was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + camera_shake_offset) = NAN;
    err[0] = '\0';
    CHECK(!ggpo_ext_validate_rollback_transport_blob(
              injected, canonical_len, &injected_checksum,
              err, sizeof(err)),
          "NaN camera shake passed rollback preflight");
    CHECK(strstr(err, "simulation geometry") != NULL,
          "invalid camera shake did not report camera-state failure");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + camera_shake_decay_offset) = INFINITY;
    err[0] = '\0';
    CHECK(!ggpo_ext_load_game_state(injected, canonical_len,
                                    err, sizeof(err)),
          "infinite camera-shake decay passed rollback load preflight");
    CHECK(strstr(err, "simulation geometry") != NULL,
          "invalid camera-shake decay did not report camera-state failure");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + game_w_offset) = 320.0f;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "simulation viewport-width checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "simulation viewport width was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + game_h_offset) = 180.0f;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "simulation viewport-height checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "simulation viewport height was erased from rollback checksum");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + game_w_offset) = 0.0f;
    err[0] = '\0';
    CHECK(!ggpo_ext_validate_rollback_transport_blob(
              injected, canonical_len, &injected_checksum,
              err, sizeof(err)),
          "zero simulation width passed rollback preflight");
    CHECK(strstr(err, "simulation geometry") != NULL,
          "invalid simulation width did not report geometry failure");

    memcpy(injected, canonical, canonical_len);
    *(float*)(injected + game_h_offset) = INFINITY;
    err[0] = '\0';
    CHECK(!ggpo_ext_load_game_state(injected, canonical_len,
                                    err, sizeof(err)),
          "infinite simulation height passed rollback load preflight");
    CHECK(strstr(err, "simulation geometry") != NULL,
          "invalid simulation height did not report geometry failure");

    memcpy(injected, canonical, canonical_len);
    *(int32_t*)(injected + resumed_offset) = 0;
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    injected, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "resume-input state checksum validation", err);
    CHECK(injected_checksum != canonical_checksum,
          "resume-input state was erased from rollback checksum");

    *(uintptr_t*)NATIVE_AT(ADDR_CONTROLLER) = (uintptr_t)player1;
    *(uintptr_t*)NATIVE_AT(ADDR_LEADER) = LEADER_SENTINEL;
    *(uintptr_t*)NATIVE_AT(ADDR_WATERFALL_FX) = WATERFALL_SENTINEL;
    *(uintptr_t*)NATIVE_AT(ADDR_LOSER) = LOSER_SENTINEL;
    *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE) = 99.0f;
    *(float*)NATIVE_AT(ADDR_CAMERA_SHAKE_DECAY) = 0.25f;
    *(float*)NATIVE_AT(ADDR_GAME_W) = 320.0f;
    *(float*)NATIVE_AT(ADDR_GAME_H) = 180.0f;
    *(int*)NATIVE_AT(ADDR_RESUMED) = 0;
    *(uint32_t*)NATIVE_AT(ADDR_MINE_ANIM_LAST_TICK) = 0;
    *(int*)NATIVE_AT(ADDR_GAME_DO_LERP_COLOURS) = 0;
    *(int*)NATIVE_AT(ADDR_LERP_TIME) = 0;
    memset(NATIVE_AT(ADDR_COLOUR_LERP_BLOCK), 0,
           LUA_GAME_PALETTE_FLOATS * sizeof(float));
    err[0] = '\0';
    expect_call(ggpo_ext_load_game_state(canonical, canonical_len,
                                         err, sizeof(err)),
                "canonical rollback load", err);
    /* Non-player leader/loser pointers are removed from canonical state.
     * Their role tags must describe the reconstructed null pointers too. */
    CHECK(lua_manager_game_state_rollback_checksum(&injected_checksum,
                                                    err, sizeof(err)),
          "could not checksum reconstructed canonical snapshot");
    CHECK(injected_checksum == canonical_checksum,
          "canonical load changed checksum after raw role pointers were removed");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_PLAYER_ARRAY) == (uintptr_t)player0 &&
              *(uintptr_t*)NATIVE_AT(ADDR_PLAYER_ARRAY + sizeof(uintptr_t)) ==
                  (uintptr_t)player1,
          "canonical load did not preserve current player addresses");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_CONTROLLER) == 0,
          "canonical load retained a serialized controller pointer");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_LEADER) == 0,
          "canonical load retained non-player leader pointer");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_LOSER) == 0,
          "canonical load retained non-player loser pointer");
    CHECK(*(uintptr_t*)NATIVE_AT(ADDR_WATERFALL_FX) == 0,
          "canonical load retained waterfall pointer");
    CHECK(*(float*)NATIVE_AT(ADDR_CAMERA_SHAKE) == 5.0f,
          "canonical load did not restore camera shake");
    CHECK(*(float*)NATIVE_AT(ADDR_CAMERA_SHAKE_DECAY) == 0.95f,
          "canonical load did not restore camera-shake decay");
    CHECK(*(float*)NATIVE_AT(ADDR_GAME_W) == 288.0f,
          "canonical load did not restore simulation viewport width");
    CHECK(*(float*)NATIVE_AT(ADDR_GAME_H) == 160.0f,
          "canonical load did not restore simulation viewport height");
    CHECK(*(int*)NATIVE_AT(ADDR_RESUMED) == 1,
          "canonical load did not restore resume-input state");
    CHECK(*(uint32_t*)NATIVE_AT(ADDR_MINE_ANIM_LAST_TICK) ==
              *(uint32_t*)NATIVE_AT(ADDR_MAD_TICKS),
          "canonical load did not preserve same-tick second-mine suppression");
    CHECK(lua_manager_game_palette_restore(&palette_baseline) &&
              lua_manager_game_palette_capture(&palette_after) &&
              memcmp(&palette_after, &palette_baseline,
                     sizeof(palette_after)) == 0,
          "rollback palette sidecar did not restore pending transition coherently");

    /* A read-only live player must reject the entire apply during preflight.
     * Seed a value different from the blob, then compare every mapped byte to
     * prove neither validation nor the failed load partially committed state. */
    *(uint32_t*)NATIVE_AT(ADDR_SCORE_P0) = UINT32_C(0x7b6a5948);
    player1[91] ^= UINT8_C(0x3c);
    copy_native_regions(before_region);
    memcpy(before_player0, player0, sizeof(before_player0));
    memcpy(before_player1, player1, sizeof(before_player1));
    memcpy(before_tilemap, tilemap, sizeof(before_tilemap));
    CHECK(VirtualProtect(player0, PLAYER_PAGE_SIZE, PAGE_READONLY,
                         &old_protect) != 0,
          "could not make player zero read-only for preflight test");

    err[0] = '\0';
    CHECK(!ggpo_ext_validate_rollback_transport_blob(
              canonical, canonical_len, &injected_checksum, err, sizeof(err)),
          "transport validation accepted a non-writable live player");
    CHECK(strstr(err, "player state unavailable") != NULL,
          "non-writable validation did not report player state failure");
    err[0] = '\0';
    CHECK(!ggpo_ext_load_game_state(canonical, canonical_len,
                                    err, sizeof(err)),
          "rollback load accepted a non-writable live player");
    CHECK(strstr(err, "player state unavailable") != NULL,
          "non-writable load did not report player state failure");

    CHECK(native_regions_equal(before_region),
          "failed player-writability preflight mutated native state");
    CHECK(memcmp(before_player0, player0, sizeof(before_player0)) == 0,
          "failed player-writability preflight mutated player zero");
    CHECK(memcmp(before_player1, player1, sizeof(before_player1)) == 0,
          "failed player-writability preflight mutated player one");
    CHECK(memcmp(before_tilemap, tilemap, sizeof(before_tilemap)) == 0,
          "failed player-writability preflight mutated tilemap state");

    if (old_protect != 0) {
        CHECK(VirtualProtect(player0, PLAYER_PAGE_SIZE, old_protect,
                             &ignored_protect) != 0,
              "could not restore player page protection");
    }
    err[0] = '\0';
    expect_call(ggpo_ext_validate_rollback_transport_blob(
                    canonical, canonical_len, &injected_checksum,
                    err, sizeof(err)),
                "post-protection canonical validation", err);
    err[0] = '\0';
    expect_call(ggpo_ext_load_game_state(canonical, canonical_len,
                                         err, sizeof(err)),
                "post-protection canonical load", err);

    free(before_region);
    free(injected);
    free(canonical);
    free(raw_roundtrip);
    free(raw);
    VirtualFree(tilemap, 0, MEM_RELEASE);
    VirtualFree(player1, 0, MEM_RELEASE);
    VirtualFree(player0, 0, MEM_RELEASE);
    hooks_test_bind_mad_ticks(NULL);
    VirtualFree(g_native_state_fixture, 0, MEM_RELEASE);
    g_native_state_fixture = NULL;

    if (g_failures != 0) {
        fprintf(stderr, "state serializer tests failed: %d\n", g_failures);
        return 1;
    }
    printf("state serializer tests: ALL OK\n");
    return 0;
}
