#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../map_script.h"
#include "../rollback_schema.h"

#define WIRE_TOTAL_SIZE_OFFSET 8u
#define WIRE_CHECKSUM_OFFSET 16u
#define WIRE_GAME_BUILD_OFFSET 24u
#define WIRE_GLOBALS_OFFSET_FIELD 64u
#define WIRE_ROLES_OFFSET_FIELD 72u
#define WIRE_ALLOCATOR_OFFSET_FIELD 80u
#define WIRE_ENTITIES_OFFSET_FIELD 88u
#define WIRE_ENTITIES_SIZE_FIELD 92u
#define WIRE_DANGER_OFFSET_FIELD 96u
#define WIRE_TILE_OFFSET_FIELD 104u
#define WIRE_TILE_SIZE_FIELD 108u
#define WIRE_MAP_OFFSET_FIELD 112u

#define GLOBAL_RNG_OFFSET 8u
#define GLOBAL_BOOL_OFFSET 72u
#define GLOBAL_CAMERA_X_OFFSET 80u

#define ENTITY_SECTION_HEADER_SIZE 16u
#define ENTITY_RECORD_SIZE 360u
#define ENTITY_ACTIVE_OFFSET 1u
#define ENTITY_KIND_OFFSET 2u
#define ENTITY_LIFECYCLE_OFFSET 4u
#define ENTITY_BEHAVIOR_OFFSET 8u

#define TILE_COUNT_OFFSET 16u
#define TILE_CELLS_OFFSET 32u
#define MAP_SECTION_HEADER_SIZE 16u
#define MAP_CHECKSUM_OFFSET 12u

static int failures = 0;

#define CHECK(condition, message)                                                \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__);      \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* The fixture deliberately exercises the current 32-bit x86 little-endian
 * packed MapScript bridge. It is not evidence of a portable host-struct wire
 * contract; the production codec parses the resulting subdocument as LE. */
_Static_assert(sizeof(MapScriptSnapshot) == ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES,
               "test fixture must match embedded MapScriptSnapshot v6");
_Static_assert(offsetof(MapScriptSnapshot, state) == 128u,
               "test fixture map snapshot header changed");

static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_zero_range(const uint8_t* bytes,
                                 size_t size,
                                 size_t zero_offset,
                                 size_t zero_size) {
    uint32_t crc = UINT32_MAX;
    size_t i;
    int bit;
    for (i = 0u; i < size; ++i) {
        uint8_t value = (i >= zero_offset && i - zero_offset < zero_size)
                      ? 0u : bytes[i];
        crc ^= value;
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & UINT32_C(1));
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static int bytes_are_value(const void* memory, size_t size, uint8_t value) {
    const uint8_t* bytes = (const uint8_t*)memory;
    size_t i;
    for (i = 0u; i < size; ++i) {
        if (bytes[i] != value) return 0;
    }
    return 1;
}

static void refresh_map_checksum(RollbackSchemaState* state) {
    MapScriptSnapshot* snapshot = (MapScriptSnapshot*)state->map_script_bytes;
    snapshot->checksum = crc32_zero_range(state->map_script_bytes,
                                          ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES,
                                          MAP_CHECKSUM_OFFSET,
                                          sizeof(snapshot->checksum));
}

static void install_inactive_map_snapshot(RollbackSchemaState* state) {
    MapScriptSnapshot* snapshot = (MapScriptSnapshot*)state->map_script_bytes;
    uint32_t i;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->magic = MAP_SCRIPT_SNAPSHOT_MAGIC;
    snapshot->version = MAP_SCRIPT_SNAPSHOT_VERSION;
    snapshot->header_size = (uint16_t)offsetof(MapScriptSnapshot, state);
    snapshot->total_size = (uint32_t)sizeof(*snapshot);
    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        state->entities[i].lifecycle = 0u;
    }
    refresh_map_checksum(state);
}

static uint8_t* wire_map_bytes(uint8_t* wire) {
    return wire + read_u32(wire + WIRE_MAP_OFFSET_FIELD) + MAP_SECTION_HEADER_SIZE;
}

static void refresh_embedded_map_checksum(uint8_t* wire) {
    uint8_t* map = wire_map_bytes(wire);
    write_u32(map + MAP_CHECKSUM_OFFSET,
              crc32_zero_range(map, ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES,
                               MAP_CHECKSUM_OFFSET, sizeof(uint32_t)));
}

static void refresh_wire_checksum(uint8_t* wire, size_t wire_size) {
    write_u32(wire + WIRE_CHECKSUM_OFFSET,
              crc32_zero_range(wire, wire_size, WIRE_CHECKSUM_OFFSET,
                               sizeof(uint32_t)));
}

static void activate_entity(RollbackSchemaState* state,
                            uint32_t slot,
                            uint8_t kind,
                            uint32_t behavior,
                            uint8_t seed) {
    size_t i;
    RollbackSchemaEntity* entity = &state->entities[slot];
    entity->active = 1u;
    entity->kind = kind;
    entity->behavior_id = behavior;
    for (i = 0u; i < sizeof(entity->payload); ++i) {
        entity->payload[i] = (uint8_t)(seed + (uint8_t)(i * 13u));
    }
}

static RollbackSchemaState* make_valid_state(void) {
    RollbackSchemaState* state =
        (RollbackSchemaState*)calloc(1u, sizeof(RollbackSchemaState));
    MapScriptSnapshot* snapshot;
    uint32_t i;
    if (!state) return NULL;

    state->identity.game_build_id = UINT64_C(0x0102030405060708);
    state->identity.framework_build_id = UINT64_C(0x1112131415161718);
    state->identity.content_id = UINT64_C(0x2122232425262728);
    state->identity.map_id = UINT64_C(0x3132333435363738);
    state->identity.protocol_version = UINT32_C(0x41424344);

    state->globals.rng_state = UINT32_C(0x11223344);
    state->globals.native_game_ticks = UINT32_C(0x55667788);
    state->globals.map_seed = UINT32_C(0x10293847);
    state->globals.game_level = UINT32_C(0xabcdef01);
    state->globals.active_room = 2;
    state->globals.old_active_room = 1;
    state->globals.start_countdown = 0;
    state->globals.end_countdown = 0;
    state->globals.map_selector = 3;
    state->globals.map_mode = 1;
    state->globals.score_target = 5;
    state->globals.armed_respawn_limit = 2;
    state->globals.score_p0 = 1;
    state->globals.score_p1 = 2;
    state->globals.player_mode0 = 0;
    state->globals.player_mode1 = 1;
    state->globals.round_end_any = 1u;
    state->globals.game_started = 1u;
    state->globals.freeze = 0u;
    state->globals.debug_enabled = 0u;
    state->globals.camera_x = 12.5f;
    state->globals.camera_y = -8.25f;
    state->globals.camera_shake = 0.5f;
    state->globals.camera_shake_decay = 0.75f;
    state->globals.roomdef_count = 4;
    state->globals.room_width = 32;
    state->globals.room_pixel_width = 512;
    state->globals.map_width = 3;
    state->globals.map_height = 2;
    state->globals.tile_width = 16;
    state->globals.tile_height = 16;
    state->globals.tilemap_pixel_width = 48;
    state->globals.tilemap_pixel_height = 32;

    state->allocator.capacity = ROLLBACK_SCHEMA_ENTITY_CAPACITY;
    state->allocator.active_count = 5u;
    state->allocator.cursor = 5u;
    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        state->entities[i].slot_id = (uint8_t)i;
        state->entities[i].kind = ROLLBACK_SCHEMA_ENTITY_FREE;
        state->entities[i].lifecycle = (i == 0u) ? 0u : 100u + i;
    }
    activate_entity(state, 1u, ROLLBACK_SCHEMA_ENTITY_PLAYER,
                    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_NOTHING, 0x10u);
    activate_entity(state, 2u, ROLLBACK_SCHEMA_ENTITY_PLAYER,
                    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_RUN, 0x20u);
    activate_entity(state, 3u, ROLLBACK_SCHEMA_ENTITY_SWORD,
                    ROLLBACK_SCHEMA_BEHAVIOR_NONE, 0x30u);
    activate_entity(state, 4u, ROLLBACK_SCHEMA_ENTITY_HAZARD,
                    ROLLBACK_SCHEMA_BEHAVIOR_HAZARD_K_UPDATE, 0x40u);
    activate_entity(state, 5u, ROLLBACK_SCHEMA_ENTITY_MINE,
                    ROLLBACK_SCHEMA_BEHAVIOR_MINE_UPDATE, 0x50u);

    state->roles.p0_slot = 1u;
    state->roles.p1_slot = 2u;
    state->roles.controller = ROLLBACK_SCHEMA_ROLE_P1;
    state->roles.leader = ROLLBACK_SCHEMA_ROLE_P0;
    state->roles.loser = ROLLBACK_SCHEMA_ROLE_P1;

    state->danger.count = 3u;
    memset(state->danger.slots, ROLLBACK_SCHEMA_SLOT_NONE,
           sizeof(state->danger.slots));
    state->danger.slots[0] = 3u;
    state->danger.slots[1] = 4u;
    state->danger.slots[2] = 1u;

    state->tilemap_width = 3u;
    state->tilemap_height = 2u;
    state->tile_cell_count = 6u;
    state->tile_cells[0] = UINT32_C(0x01020304);
    state->tile_cells[1] = UINT32_C(0x11121314);
    state->tile_cells[2] = UINT32_C(0x21222324);
    state->tile_cells[3] = UINT32_C(0x31323334);
    state->tile_cells[4] = UINT32_C(0x41424344);
    state->tile_cells[5] = UINT32_C(0x51525354);

    snapshot = (MapScriptSnapshot*)state->map_script_bytes;
    snapshot->magic = MAP_SCRIPT_SNAPSHOT_MAGIC;
    snapshot->version = MAP_SCRIPT_SNAPSHOT_VERSION;
    snapshot->header_size = (uint16_t)offsetof(MapScriptSnapshot, state);
    snapshot->total_size = (uint32_t)sizeof(*snapshot);
    snapshot->script_id = UINT64_C(0x8899aabbccddeeff);
    snapshot->tick = UINT64_C(500);
    snapshot->rng_state = UINT32_C(0x76543210);
    for (i = 0u; i < MAP_SCRIPT_MAX_LIFECYCLE_SLOTS; ++i) {
        snapshot->lifecycle_generation[i] = state->entities[i].lifecycle;
    }

    snapshot->state_count = 1u;
    snapshot->state[0].in_use = 1u;
    snapshot->state[0].type = MAP_SCRIPT_STATE_NUMBER;
    snapshot->state[0].key_len = 4u;
    snapshot->state[0].number_value = 1.25;
    memcpy(snapshot->state[0].key, "rate", 4u);

    snapshot->override_count = 1u;
    snapshot->overrides[0].in_use = 1u;
    snapshot->overrides[0].cell_index = 2u;
    snapshot->overrides[0].sprite_index = 129;
    snapshot->overrides[0].offset_x_q = 64;
    snapshot->overrides[0].offset_y_q = -512;
    snapshot->overrides[0].expires_after_tick = snapshot->tick + 8u;

    snapshot->contact_count = 1u;
    snapshot->contacts[0].in_use = 1u;
    snapshot->contacts[0].object_kind = MAP_SCRIPT_OBJECT_SWORD;
    snapshot->contacts[0].binding_index = 0u;
    snapshot->contacts[0].object_id = 2u + 3u;
    snapshot->contacts[0].lifecycle_id = state->entities[3].lifecycle;
    snapshot->contacts[0].cell_index = 1u;
    snapshot->contacts[0].tile_x = 1;
    snapshot->contacts[0].tile_y = 0;
    snapshot->contacts[0].object_x = 8.0f;
    snapshot->contacts[0].object_y = 12.0f;
    snapshot->contacts[0].object_vx = 0.5f;
    snapshot->contacts[0].object_vy = -1.5f;
    snapshot->contacts[0].room_mirrored = 0u;
    snapshot->contacts[0].contact_scope = MAP_SCRIPT_CONTACT_SCOPE_CELL;
    snapshot->contacts[0].last_seen_tick = snapshot->tick;

    snapshot->timer_count = 2u;
    snapshot->timers[0].remaining = 60u;
    snapshot->timers[0].interval = 120u;
    snapshot->velocity_limit_count = 1u;
    snapshot->velocity_limits[0].in_use = 1u;
    snapshot->velocity_limits[0].object_kind = MAP_SCRIPT_OBJECT_HAZARD;
    snapshot->velocity_limits[0].bound_flags = MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY;
    snapshot->velocity_limits[0].object_id = 2u + 4u;
    snapshot->velocity_limits[0].lifecycle_id = state->entities[4].lifecycle;
    snapshot->velocity_limits[0].max_vy = -2.0f;
    snapshot->velocity_limits[0].expires_after_tick = snapshot->tick + 8u;
    refresh_map_checksum(state);
    return state;
}

static void expect_state_rejected(const RollbackSchemaState* state,
                                  const char* message) {
    char err[256];
    err[0] = '\0';
    CHECK(!rollback_schema_validate_state(state, err, sizeof(err)), message);
    CHECK(err[0] != '\0', "state rejection did not provide a diagnostic");
}

static void expect_wire_rejected(const uint8_t* wire,
                                 size_t wire_size,
                                 const char* message) {
    RollbackSchemaState* output =
        (RollbackSchemaState*)malloc(sizeof(RollbackSchemaState));
    char err[256];
    if (!output) {
        CHECK(0, "wire rejection output allocation failed");
        return;
    }
    memset(output, 0xa5, sizeof(*output));
    err[0] = '\0';
    CHECK(!rollback_schema_decode(wire, wire_size, output, err, sizeof(err)), message);
    CHECK(err[0] != '\0', "wire rejection did not provide a diagnostic");
    CHECK(bytes_are_value(output, sizeof(*output), 0xa5u),
          "failed decode partially modified its output state");
    free(output);
}

static void expect_expected_decode_rejected(
    const uint8_t* wire,
    size_t wire_size,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    RollbackSchemaState* output,
    const char* message) {
    char err[256];
    memset(output, 0xa5, sizeof(*output));
    err[0] = '\0';
    CHECK(!rollback_schema_decode_expected(wire, wire_size, expected_identity,
                                           expected_map_script_id, output,
                                           err, sizeof(err)),
          message);
    CHECK(err[0] != '\0', "expected-decode rejection had no diagnostic");
    CHECK(bytes_are_value(output, sizeof(*output), 0xa5u),
          "expected-decode rejection partially modified output state");
}

static void test_round_trip(const RollbackSchemaState* state,
                            uint8_t** out_wire,
                            size_t* out_wire_size) {
    RollbackSchemaState* decoded =
        (RollbackSchemaState*)malloc(sizeof(RollbackSchemaState));
    uint8_t* wire = NULL;
    uint8_t* wire_again = NULL;
    size_t wire_size = 0u;
    size_t wire_again_size = 0u;
    uint32_t globals_offset;
    uint32_t tile_offset;
    char err[256];
    CHECK(decoded != NULL, "decoded-state allocation failed");
    if (!decoded) return;

    err[0] = '\0';
    CHECK(rollback_schema_encoded_size(state, &wire_size, err, sizeof(err)),
          "valid state did not report an encoded size");
    CHECK(wire_size == 35908u, "encoded size does not match the v2 section layout");
    wire = (uint8_t*)malloc(wire_size);
    wire_again = (uint8_t*)malloc(wire_size);
    CHECK(wire != NULL && wire_again != NULL, "wire allocation failed");
    if (!wire || !wire_again) goto done;

    err[0] = '\0';
    CHECK(rollback_schema_encode(state, wire, wire_size, out_wire_size,
                                 err, sizeof(err)),
          "valid state failed to encode");
    CHECK(*out_wire_size == wire_size, "encode returned the wrong wire size");
    err[0] = '\0';
    CHECK(!rollback_schema_encode(state, wire, wire_size - 1u, NULL,
                                  err, sizeof(err)),
          "undersized encode destination was accepted");
    CHECK(wire[0] == 'R' && wire[1] == 'B' && wire[2] == 'S' && wire[3] == '1',
          "wire magic is not explicit little-endian RBS1");
    CHECK(wire[WIRE_GAME_BUILD_OFFSET] == 0x08u &&
          wire[WIRE_GAME_BUILD_OFFSET + 7u] == 0x01u,
          "64-bit identity is not little-endian");
    globals_offset = read_u32(wire + WIRE_GLOBALS_OFFSET_FIELD);
    CHECK(wire[globals_offset + GLOBAL_RNG_OFFSET] == 0x44u &&
          wire[globals_offset + GLOBAL_RNG_OFFSET + 3u] == 0x11u,
          "32-bit deterministic global is not little-endian");
    tile_offset = read_u32(wire + WIRE_TILE_OFFSET_FIELD);
    CHECK(wire[tile_offset + TILE_CELLS_OFFSET] == 0x04u &&
          wire[tile_offset + TILE_CELLS_OFFSET + 3u] == 0x01u,
          "tile cells are not little-endian");
    CHECK(read_u32(wire + read_u32(wire + WIRE_ENTITIES_OFFSET_FIELD) +
                   ENTITY_SECTION_HEADER_SIZE + ENTITY_RECORD_SIZE +
                   ENTITY_BEHAVIOR_OFFSET) ==
              ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_NOTHING,
          "entity behavior was not encoded as a stable id");

    err[0] = '\0';
    CHECK(rollback_schema_validate_wire(wire, wire_size, err, sizeof(err)),
          "canonical wire did not validate");
    err[0] = '\0';
    CHECK(rollback_schema_decode(wire, wire_size, decoded, err, sizeof(err)),
          "canonical wire did not decode");
    CHECK(decoded->roles.controller == ROLLBACK_SCHEMA_ROLE_P1,
          "controller role did not survive decode");
    CHECK(decoded->entities[4].behavior_id ==
              ROLLBACK_SCHEMA_BEHAVIOR_HAZARD_K_UPDATE,
          "hazard behavior id did not survive decode");
    CHECK(decoded->danger.count == 3u && decoded->danger.slots[1] == 4u,
          "ordered danger references did not survive decode");
    CHECK(decoded->tile_cells[5] == UINT32_C(0x51525354),
          "tile cell did not survive decode");

    err[0] = '\0';
    CHECK(rollback_schema_encode(decoded, wire_again, wire_size,
                                 &wire_again_size, err, sizeof(err)),
          "decoded state failed to re-encode");
    CHECK(wire_again_size == wire_size &&
          memcmp(wire, wire_again, wire_size) == 0,
          "encode/decode/re-encode was not byte-identical");
    *out_wire = wire;
    wire = NULL;
done:
    free(wire);
    free(wire_again);
    free(decoded);
}

static void test_state_validation(const RollbackSchemaState* baseline) {
    RollbackSchemaState* bad =
        (RollbackSchemaState*)malloc(sizeof(RollbackSchemaState));
    uint32_t bits;
    MapScriptSnapshot* snapshot;
    CHECK(bad != NULL, "invalid-state fixture allocation failed");
    if (!bad) return;

#define RESET_BAD() memcpy(bad, baseline, sizeof(*bad))
    RESET_BAD();
    install_inactive_map_snapshot(bad);
    {
        char err[256];
        err[0] = '\0';
        CHECK(rollback_schema_validate_state(bad, err, sizeof(err)),
              "canonical inactive map snapshot was rejected");
    }

    RESET_BAD();
    bad->allocator.cursor = 6u; /* Native latest may point at a later-freed slot. */
    {
        char err[256];
        err[0] = '\0';
        CHECK(rollback_schema_validate_state(bad, err, sizeof(err)),
              "allocator cursor pointing at a free slot was rejected");
    }

    RESET_BAD();
    bad->danger.count = 4u;
    bad->danger.slots[3] = 6u;
    bad->entities[6].kind = ROLLBACK_SCHEMA_ENTITY_SWORD;
    bad->entities[6].behavior_id = ROLLBACK_SCHEMA_BEHAVIOR_NONE;
    memset(bad->entities[6].payload, 0x6d,
           sizeof(bad->entities[6].payload));
    {
        char err[256];
        err[0] = '\0';
        CHECK(rollback_schema_validate_state(bad, err, sizeof(err)),
              "inactive danger-referenced entity tombstone was rejected");
    }

    RESET_BAD();
    bad->globals.game_started = 2u;
    expect_state_rejected(bad, "non-canonical boolean state was accepted");

    RESET_BAD();
    bits = UINT32_C(0x80000000);
    memcpy(&bad->globals.camera_x, &bits, sizeof(bits));
    expect_state_rejected(bad, "negative-zero float state was accepted");

    RESET_BAD();
    bits = UINT32_C(0x7fc00000);
    memcpy(&bad->globals.camera_x, &bits, sizeof(bits));
    expect_state_rejected(bad, "NaN float state was accepted");

    RESET_BAD();
    bits = UINT32_C(0x7f800000);
    memcpy(&bad->globals.camera_x, &bits, sizeof(bits));
    expect_state_rejected(bad, "infinite float state was accepted");

    RESET_BAD();
    bad->allocator.capacity = 15u;
    expect_state_rejected(bad, "wrong allocator capacity was accepted");

    RESET_BAD();
    bad->allocator.active_count--;
    expect_state_rejected(bad, "wrong allocator active count was accepted");

    RESET_BAD();
    bad->allocator.cursor = 0u;
    expect_state_rejected(bad, "reserved allocator cursor was accepted");

    RESET_BAD();
    bad->entities[3].slot_id = 9u;
    expect_state_rejected(bad, "wrong entity slot marker was accepted");

    RESET_BAD();
    bad->entities[3].behavior_id = ROLLBACK_SCHEMA_BEHAVIOR_HAZARD_K_UPDATE;
    expect_state_rejected(bad, "wrong entity kind/behavior pair was accepted");

    RESET_BAD();
    bad->entities[6].payload[0] = 1u;
    expect_state_rejected(bad, "stale free-slot payload was accepted");

    RESET_BAD();
    bad->roles.p1_slot = bad->roles.p0_slot;
    expect_state_rejected(bad, "duplicate P0/P1 role was accepted");

    RESET_BAD();
    bad->roles.controller = 3u;
    expect_state_rejected(bad, "unknown controller role was accepted");

    RESET_BAD();
    bad->danger.slots[3] = 4u;
    expect_state_rejected(bad, "non-canonical unused danger ref was accepted");

    RESET_BAD();
    bad->danger.slots[0] = 6u;
    expect_state_rejected(bad, "danger ref to a free slot was accepted");

    RESET_BAD();
    bad->entities[3].lifecycle++;
    expect_state_rejected(bad, "entity/map lifecycle disagreement was accepted");

    RESET_BAD();
    snapshot = (MapScriptSnapshot*)bad->map_script_bytes;
    snapshot->timers[0].remaining = 0;
    refresh_map_checksum(bad);
    expect_state_rejected(bad, "stopped repeating timer accepted");
    RESET_BAD();
    snapshot = (MapScriptSnapshot*)bad->map_script_bytes;
    snapshot->timers[31].remaining = 1;
    refresh_map_checksum(bad);
    expect_state_rejected(bad, "undeclared active timer accepted");

    RESET_BAD();
    snapshot = (MapScriptSnapshot*)bad->map_script_bytes;
    snapshot->contacts[0].lifecycle_id++;
    refresh_map_checksum(bad);
    expect_state_rejected(bad, "stale map contact lifecycle was accepted");

    RESET_BAD();
    snapshot = (MapScriptSnapshot*)bad->map_script_bytes;
    bits = UINT32_C(0x80000000);
    memcpy(&snapshot->contacts[0].object_x, &bits, sizeof(bits));
    refresh_map_checksum(bad);
    expect_state_rejected(bad, "negative-zero map contact float was accepted");

    RESET_BAD();
    snapshot = (MapScriptSnapshot*)bad->map_script_bytes;
    snapshot->state[0].type = 99u;
    refresh_map_checksum(bad);
    expect_state_rejected(bad, "unknown map state enum was accepted");

    RESET_BAD();
    snapshot = (MapScriptSnapshot*)bad->map_script_bytes;
    snapshot->state[1] = snapshot->state[0];
    snapshot->state_count = 2u;
    refresh_map_checksum(bad);
    expect_state_rejected(bad, "duplicate map state keys were accepted");

    RESET_BAD();
    bad->tile_cell_count--;
    expect_state_rejected(bad, "tile dimension/count mismatch was accepted");
#undef RESET_BAD
    free(bad);
}

static void test_wire_validation(const uint8_t* baseline, size_t wire_size) {
    uint8_t* bad = (uint8_t*)malloc(wire_size);
    uint32_t globals_offset;
    uint32_t roles_offset;
    uint32_t allocator_offset;
    uint32_t entities_offset;
    uint32_t danger_offset;
    uint32_t tile_offset;
    uint32_t map_offset;
    uint8_t* record;
    uint8_t* map;
    size_t truncations[] = {0u, 1u, 63u, 127u, 128u};
    size_t section_truncations[14];
    size_t section_truncation_count = 0u;
    size_t i;
    CHECK(bad != NULL, "invalid-wire fixture allocation failed");
    if (!bad) return;
    globals_offset = read_u32(baseline + WIRE_GLOBALS_OFFSET_FIELD);
    roles_offset = read_u32(baseline + WIRE_ROLES_OFFSET_FIELD);
    allocator_offset = read_u32(baseline + WIRE_ALLOCATOR_OFFSET_FIELD);
    entities_offset = read_u32(baseline + WIRE_ENTITIES_OFFSET_FIELD);
    danger_offset = read_u32(baseline + WIRE_DANGER_OFFSET_FIELD);
    tile_offset = read_u32(baseline + WIRE_TILE_OFFSET_FIELD);
    map_offset = read_u32(baseline + WIRE_MAP_OFFSET_FIELD);

    for (i = 0u; i < sizeof(truncations) / sizeof(truncations[0]); ++i) {
        expect_wire_rejected(baseline, truncations[i], "truncated wire prefix was accepted");
    }
    expect_wire_rejected(baseline, wire_size - 1u, "one-byte-truncated wire was accepted");

#define RESET_WIRE() memcpy(bad, baseline, wire_size)
    section_truncations[section_truncation_count++] = globals_offset;
    section_truncations[section_truncation_count++] = roles_offset - 1u;
    section_truncations[section_truncation_count++] = roles_offset;
    section_truncations[section_truncation_count++] = allocator_offset - 1u;
    section_truncations[section_truncation_count++] = allocator_offset;
    section_truncations[section_truncation_count++] = entities_offset - 1u;
    section_truncations[section_truncation_count++] = entities_offset;
    section_truncations[section_truncation_count++] = danger_offset - 1u;
    section_truncations[section_truncation_count++] = danger_offset;
    section_truncations[section_truncation_count++] = tile_offset - 1u;
    section_truncations[section_truncation_count++] = tile_offset;
    section_truncations[section_truncation_count++] = map_offset - 1u;
    section_truncations[section_truncation_count++] = map_offset;
    section_truncations[section_truncation_count++] = wire_size - 1u;
    for (i = 0u; i < section_truncation_count; ++i) {
        RESET_WIRE();
        write_u32(bad + WIRE_TOTAL_SIZE_OFFSET,
                  (uint32_t)section_truncations[i]);
        refresh_wire_checksum(bad, section_truncations[i]);
        expect_wire_rejected(bad, section_truncations[i],
                             "self-sized section-boundary truncation was accepted");
    }

    RESET_WIRE();
    bad[4] = 3u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "unknown rollback schema version was accepted");

    RESET_WIRE();
    bad[20] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero header reserved byte was accepted");

    RESET_WIRE();
    bad[120] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero trailing header reserve was accepted");

    RESET_WIRE();
    write_u32(bad + WIRE_ROLES_OFFSET_FIELD, roles_offset + 1u);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "shifted roles directory entry was accepted");

    RESET_WIRE();
    write_u32(bad + WIRE_ENTITIES_SIZE_FIELD,
              read_u32(bad + WIRE_ENTITIES_SIZE_FIELD) - 1u);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "short entity directory size was accepted");

    RESET_WIRE();
    write_u32(bad + WIRE_MAP_OFFSET_FIELD, map_offset + 1u);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "shifted map directory entry was accepted");

    RESET_WIRE();
    bad[globals_offset + 76u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero section padding was accepted");

    RESET_WIRE();
    bad[roles_offset + 9u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero roles reserve was accepted");

    RESET_WIRE();
    bad[allocator_offset + 7u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero allocator reserve was accepted");

    RESET_WIRE();
    bad[entities_offset + 14u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero entity-section reserve was accepted");

    RESET_WIRE();
    bad[danger_offset + 5u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero danger reserve was accepted");

    RESET_WIRE();
    bad[tile_offset + 22u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero tile reserve was accepted");

    RESET_WIRE();
    bad[map_offset + 14u] = 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-zero map wrapper reserve was accepted");

    RESET_WIRE();
    bad[globals_offset] = 2u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "unknown section version was accepted");

    RESET_WIRE();
    memset(bad + WIRE_GAME_BUILD_OFFSET, 0, 8u);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "zero compatibility identity was accepted");

    RESET_WIRE();
    bad[globals_offset + GLOBAL_BOOL_OFFSET] = 2u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-canonical wire boolean was accepted");

    RESET_WIRE();
    write_u32(bad + globals_offset + GLOBAL_CAMERA_X_OFFSET, UINT32_C(0x7fc00000));
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "wire NaN was accepted");

    RESET_WIRE();
    write_u32(bad + globals_offset + GLOBAL_CAMERA_X_OFFSET, UINT32_C(0x80000000));
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "wire negative zero was accepted");

    RESET_WIRE();
    bad[roles_offset + 5u] = bad[roles_offset + 4u];
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "duplicate wire P0/P1 roles were accepted");

    RESET_WIRE();
    bad[roles_offset + 6u] = 7u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "unknown wire controller role was accepted");

    RESET_WIRE();
    bad[allocator_offset + 5u]--;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "wire allocator count mismatch was accepted");

    RESET_WIRE();
    record = bad + entities_offset + ENTITY_SECTION_HEADER_SIZE + 3u * ENTITY_RECORD_SIZE;
    record[0] = 8u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "wrong wire entity slot marker was accepted");

    RESET_WIRE();
    record = bad + entities_offset + ENTITY_SECTION_HEADER_SIZE + 3u * ENTITY_RECORD_SIZE;
    record[ENTITY_ACTIVE_OFFSET] = 2u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "non-canonical entity active marker was accepted");

    RESET_WIRE();
    record = bad + entities_offset + ENTITY_SECTION_HEADER_SIZE + 3u * ENTITY_RECORD_SIZE;
    record[ENTITY_KIND_OFFSET] = 99u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "unknown entity kind was accepted");

    RESET_WIRE();
    record = bad + entities_offset + ENTITY_SECTION_HEADER_SIZE + 3u * ENTITY_RECORD_SIZE;
    write_u32(record + ENTITY_BEHAVIOR_OFFSET, UINT32_C(0x0043c450));
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "native code address was accepted as behavior id");

    RESET_WIRE();
    record = bad + entities_offset + ENTITY_SECTION_HEADER_SIZE + 3u * ENTITY_RECORD_SIZE;
    write_u32(record + ENTITY_LIFECYCLE_OFFSET,
              read_u32(record + ENTITY_LIFECYCLE_OFFSET) + 1u);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "wire lifecycle mismatch was accepted");

    RESET_WIRE();
    write_u32(bad + tile_offset + TILE_COUNT_OFFSET, UINT32_MAX);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "overflowing tile count was accepted");

    RESET_WIRE();
    write_u32(bad + WIRE_TILE_SIZE_FIELD, UINT32_MAX);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "overflowing tile section size was accepted");

    RESET_WIRE();
    map = wire_map_bytes(bad);
    map[200] ^= 1u;
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "bad embedded map checksum was accepted");

    RESET_WIRE();
    map = wire_map_bytes(bad);
    map[4] = 99u;
    refresh_embedded_map_checksum(bad);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "unknown embedded map version was accepted");

    RESET_WIRE();
    map = wire_map_bytes(bad);
    write_u32(map + 64u + 3u * 4u, read_u32(map + 64u + 3u * 4u) + 1u);
    refresh_embedded_map_checksum(bad);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "map/entity lifecycle disagreement was accepted");

    RESET_WIRE();
    map = wire_map_bytes(bad);
    write_u32(map + 14464u + 8u,
              read_u32(map + 14464u + 8u) + 1u);
    refresh_embedded_map_checksum(bad);
    refresh_wire_checksum(bad, wire_size);
    expect_wire_rejected(bad, wire_size, "stale embedded contact lifecycle was accepted");

    RESET_WIRE();
    bad[0] ^= 1u;
    expect_wire_rejected(bad, wire_size, "tampered checksum/magic was accepted");
#undef RESET_WIRE
    free(bad);
}

static void test_expected_identity(const RollbackSchemaState* state,
                                   const uint8_t* wire,
                                   size_t wire_size) {
    RollbackSchemaState* output =
        (RollbackSchemaState*)malloc(sizeof(RollbackSchemaState));
    RollbackSchemaIdentity expected = state->identity;
    uint64_t script_id =
        ((const MapScriptSnapshot*)state->map_script_bytes)->script_id;
    char err[256];
    CHECK(output != NULL, "expected-identity output allocation failed");
    if (!output) return;

    err[0] = '\0';
    CHECK(rollback_schema_validate_compatibility(state, &expected, script_id,
                                                 err, sizeof(err)),
          "matching state compatibility identity was rejected");
    err[0] = '\0';
    CHECK(rollback_schema_validate_wire_expected(wire, wire_size, &expected,
                                                 script_id, err, sizeof(err)),
          "matching wire compatibility identity was rejected");
    err[0] = '\0';
    CHECK(rollback_schema_decode_expected(wire, wire_size, &expected, script_id,
                                          output, err, sizeof(err)),
          "matching expected identity failed to decode");

    expected = state->identity;
    expected.game_build_id ^= UINT64_C(1);
    expect_expected_decode_rejected(wire, wire_size, &expected, script_id,
                                    output,
                                    "mismatched game-build identity was accepted");

    expected = state->identity;
    expected.framework_build_id ^= UINT64_C(1);
    expect_expected_decode_rejected(
        wire, wire_size, &expected, script_id, output,
        "mismatched framework-build identity was accepted");

    expected = state->identity;
    expected.content_id ^= UINT64_C(1);
    expect_expected_decode_rejected(wire, wire_size, &expected, script_id,
                                    output,
                                    "mismatched content identity was accepted");

    expected = state->identity;
    expected.map_id ^= UINT64_C(1);
    expect_expected_decode_rejected(wire, wire_size, &expected, script_id,
                                    output,
                                    "mismatched map identity was accepted");

    expected = state->identity;
    expected.protocol_version ^= UINT32_C(1);
    expect_expected_decode_rejected(wire, wire_size, &expected, script_id,
                                    output,
                                    "mismatched protocol identity was accepted");

    expected = state->identity;
    expect_expected_decode_rejected(wire, wire_size, &expected, script_id + 1u,
                                    output,
                                    "mismatched map-script identity was accepted");
    err[0] = '\0';
    CHECK(!rollback_schema_validate_wire_expected(wire, wire_size, &expected,
                                                  script_id + 1u,
                                                  err, sizeof(err)),
          "mismatched map-script identity was accepted");

    expect_expected_decode_rejected(wire, wire_size, NULL, script_id, output,
                                    "null expected identity was accepted");
    free(output);
}

int main(void) {
    RollbackSchemaState* state = make_valid_state();
    uint8_t* wire = NULL;
    size_t wire_size = 0u;
    char err[256];
    if (!state) {
        fprintf(stderr, "FAIL: valid-state allocation failed\n");
        return 1;
    }
    err[0] = '\0';
    CHECK(rollback_schema_validate_state(state, err, sizeof(err)),
          "valid transitional rollback state was rejected");
    test_round_trip(state, &wire, &wire_size);
    test_state_validation(state);
    if (wire) {
        test_wire_validation(wire, wire_size);
        test_expected_identity(state, wire, wire_size);
    }
    else CHECK(0, "round-trip did not produce a wire image");
    free(wire);
    free(state);
    if (failures != 0) {
        fprintf(stderr, "rollback schema tests failed: %d\n", failures);
        return 1;
    }
    printf("PASS: canonical rollback envelope codec tests\n");
    return 0;
}
