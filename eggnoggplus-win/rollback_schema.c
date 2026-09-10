#include "rollback_schema.h"

#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map_script.h"

#define RB_HEADER_SIZE 128u
#define RB_GLOBALS_SIZE 160u
#define RB_ROLES_SIZE 16u
#define RB_ALLOCATOR_SIZE 16u
#define RB_ENTITY_SECTION_HEADER_SIZE 16u
#define RB_ENTITY_RECORD_SIZE 360u
#define RB_ENTITY_SECTION_SIZE \
    (RB_ENTITY_SECTION_HEADER_SIZE + \
     ROLLBACK_SCHEMA_ENTITY_CAPACITY * RB_ENTITY_RECORD_SIZE)
#define RB_DANGER_SIZE 32u
#define RB_TILE_HEADER_SIZE 32u
#define RB_MAP_SCRIPT_HEADER_SIZE 16u
#define RB_MAP_SCRIPT_SECTION_SIZE \
    (RB_MAP_SCRIPT_HEADER_SIZE + ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES)

#define RB_SECTION_VERSION UINT16_C(1)

#define RB_H_MAGIC 0u
#define RB_H_VERSION 4u
#define RB_H_HEADER_SIZE 6u
#define RB_H_TOTAL_SIZE 8u
#define RB_H_FLAGS 12u
#define RB_H_CHECKSUM 16u
#define RB_H_RESERVED0 20u
#define RB_H_GAME_BUILD 24u
#define RB_H_FRAMEWORK_BUILD 32u
#define RB_H_CONTENT_ID 40u
#define RB_H_MAP_ID 48u
#define RB_H_PROTOCOL 56u
#define RB_H_LAYOUT_ID 60u
#define RB_H_GLOBALS_OFFSET 64u
#define RB_H_GLOBALS_SIZE 68u
#define RB_H_ROLES_OFFSET 72u
#define RB_H_ROLES_SIZE 76u
#define RB_H_ALLOCATOR_OFFSET 80u
#define RB_H_ALLOCATOR_SIZE 84u
#define RB_H_ENTITIES_OFFSET 88u
#define RB_H_ENTITIES_SIZE 92u
#define RB_H_DANGER_OFFSET 96u
#define RB_H_DANGER_SIZE 100u
#define RB_H_TILE_OFFSET 104u
#define RB_H_TILE_SIZE 108u
#define RB_H_MAP_SCRIPT_OFFSET 112u
#define RB_H_MAP_SCRIPT_SIZE 116u
#define RB_H_RESERVED1 120u

#define RB_G_RNG_STATE 8u
#define RB_G_NATIVE_TICKS 12u
#define RB_G_MAP_SEED 16u
#define RB_G_GAME_LEVEL 20u
#define RB_G_ACTIVE_ROOM 24u
#define RB_G_OLD_ACTIVE_ROOM 28u
#define RB_G_START_COUNTDOWN 32u
#define RB_G_END_COUNTDOWN 36u
#define RB_G_MAP_SELECTOR 40u
#define RB_G_MAP_MODE 44u
#define RB_G_SCORE_TARGET 48u
#define RB_G_RESPAWN_LIMIT 52u
#define RB_G_SCORE_P0 56u
#define RB_G_SCORE_P1 60u
#define RB_G_PLAYER_MODE0 64u
#define RB_G_PLAYER_MODE1 68u
#define RB_G_ROUND_END_ANY 72u
#define RB_G_GAME_STARTED 73u
#define RB_G_FREEZE 74u
#define RB_G_DEBUG_ENABLED 75u
#define RB_G_CAMERA_X 80u
#define RB_G_CAMERA_Y 84u
#define RB_G_CAMERA_SHAKE 88u
#define RB_G_CAMERA_SHAKE_DECAY 92u
#define RB_G_ROOMDEF_COUNT 96u
#define RB_G_ROOM_WIDTH 100u
#define RB_G_ROOM_PIXEL_WIDTH 104u
#define RB_G_MAP_WIDTH 108u
#define RB_G_MAP_HEIGHT 112u
#define RB_G_TILE_WIDTH 116u
#define RB_G_TILE_HEIGHT 120u
#define RB_G_TILEMAP_PIXEL_WIDTH 124u
#define RB_G_TILEMAP_PIXEL_HEIGHT 128u
#define RB_G_RESERVED1 132u

#define RB_E_SLOT_ID 0u
#define RB_E_ACTIVE 1u
#define RB_E_KIND 2u
#define RB_E_RESERVED0 3u
#define RB_E_LIFECYCLE 4u
#define RB_E_BEHAVIOR 8u
#define RB_E_PAYLOAD_SIZE 12u
#define RB_E_RESERVED1 14u
#define RB_E_PAYLOAD 16u
#define RB_E_RESERVED2 358u

#define RB_MS_MAGIC UINT32_C(0x4d534c53)
#define RB_MS_MAGIC_OFFSET 0u
#define RB_MS_VERSION_OFFSET 4u
#define RB_MS_HEADER_SIZE_OFFSET 6u
#define RB_MS_TOTAL_SIZE_OFFSET 8u
#define RB_MS_CHECKSUM_OFFSET 12u
#define RB_MS_SCRIPT_ID_OFFSET 16u
#define RB_MS_TICK_OFFSET 24u
#define RB_MS_RNG_OFFSET 32u
#define RB_MS_FLAGS_OFFSET 36u
#define RB_MS_STATE_COUNT_OFFSET 40u
#define RB_MS_OVERRIDE_COUNT_OFFSET 42u
#define RB_MS_CONTACT_COUNT_OFFSET 44u
#define RB_MS_LIMIT_COUNT_OFFSET 46u
#define RB_MS_RESERVED_OFFSET 48u
#define RB_MS_LIFECYCLE_OFFSET 64u
#define RB_MS_STATE_OFFSET 128u
#define RB_MS_STATE_SIZE 112u
#define RB_MS_OVERRIDE_OFFSET 7296u
#define RB_MS_OVERRIDE_SIZE 28u
#define RB_MS_CONTACT_OFFSET 14464u
#define RB_MS_CONTACT_SIZE 56u
#define RB_MS_LIMIT_OFFSET 28800u
#define RB_MS_LIMIT_SIZE 36u

#define RB_MS_MAX_STATE 64u
#define RB_MS_MAX_OVERRIDES 256u
#define RB_MS_MAX_CONTACTS 256u
#define RB_MS_MAX_LIMITS 18u
#define RB_MS_FLAG_FAULTED UINT32_C(1)
#define RB_MS_RENDER_OFFSET_LIMIT_Q INT32_C(1048576)

#define RB_MS_OBJECT_PLAYER 1u
#define RB_MS_OBJECT_SWORD 2u
#define RB_MS_OBJECT_DEAD_BODY 3u
#define RB_MS_OBJECT_HAZARD 4u

#define RB_MS_LIMIT_MIN_VX UINT8_C(1)
#define RB_MS_LIMIT_MAX_VX UINT8_C(2)
#define RB_MS_LIMIT_MIN_VY UINT8_C(4)
#define RB_MS_LIMIT_MAX_VY UINT8_C(8)
#define RB_MS_LIMIT_ALL UINT8_C(15)

_Static_assert(CHAR_BIT == 8, "rollback schema requires eight-bit bytes");
_Static_assert(sizeof(uint16_t) == 2, "rollback schema requires uint16_t");
_Static_assert(sizeof(uint32_t) == 4, "rollback schema requires uint32_t");
_Static_assert(sizeof(uint64_t) == 8, "rollback schema requires uint64_t");
_Static_assert(sizeof(float) == 4, "rollback schema requires IEEE-754 binary32");
_Static_assert(sizeof(double) == 8, "map snapshot requires IEEE-754 binary64");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "rollback schema requires IEEE-754 binary32 semantics");
_Static_assert(DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "map snapshot requires IEEE-754 binary64 semantics");
/* Transitional bridge guard: the current x86/Windows MapScript producer stores
 * its packed little-endian snapshot directly in map_script_bytes. The wire
 * parser below still reads every field as explicit LE; future non-x86/native
 * adapters must build that canonical subdocument field by field. */
_Static_assert(offsetof(MapScriptSnapshot, timer_count) == 29448u, "timer count offset changed");
_Static_assert(offsetof(MapScriptSnapshot, timers) == 29452u, "timer payload offset changed");
_Static_assert(sizeof(MapScriptSnapshot) == ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES,
               "MapScriptSnapshot v6 size changed; bump the rollback schema");
_Static_assert(offsetof(MapScriptSnapshot, state) == RB_MS_STATE_OFFSET,
               "MapScriptSnapshot v6 header changed");
_Static_assert(offsetof(MapScriptSnapshot, overrides) == RB_MS_OVERRIDE_OFFSET,
               "MapScriptSnapshot v6 override layout changed");
_Static_assert(offsetof(MapScriptSnapshot, contacts) == RB_MS_CONTACT_OFFSET,
               "MapScriptSnapshot v6 contact layout changed");
_Static_assert(offsetof(MapScriptSnapshot, velocity_limits) == RB_MS_LIMIT_OFFSET,
               "MapScriptSnapshot v6 velocity layout changed");

static void rb_set_error(char* err, size_t err_cap, const char* fmt, ...) {
    va_list ap;
    if (!err || err_cap == 0u) return;
    va_start(ap, fmt);
    (void)vsnprintf(err, err_cap, fmt ? fmt : "rollback schema error", ap);
    va_end(ap);
    err[err_cap - 1u] = '\0';
}

static int rb_bytes_zero(const uint8_t* bytes, size_t size) {
    size_t i;
    if (!bytes && size != 0u) return 0;
    for (i = 0u; i < size; ++i) {
        if (bytes[i] != 0u) return 0;
    }
    return 1;
}

static uint16_t rb_read_u16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rb_read_u32(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rb_read_u64(const uint8_t* p) {
    uint64_t lo = rb_read_u32(p);
    uint64_t hi = rb_read_u32(p + 4u);
    return lo | (hi << 32);
}

static int32_t rb_read_i32(const uint8_t* p) {
    uint32_t bits = rb_read_u32(p);
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float rb_read_f32(const uint8_t* p) {
    uint32_t bits = rb_read_u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void rb_write_u16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value & UINT16_C(0xff));
    p[1] = (uint8_t)(value >> 8);
}

static void rb_write_u32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value & UINT32_C(0xff));
    p[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    p[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    p[3] = (uint8_t)(value >> 24);
}

static void rb_write_u64(uint8_t* p, uint64_t value) {
    rb_write_u32(p, (uint32_t)value);
    rb_write_u32(p + 4u, (uint32_t)(value >> 32));
}

static void rb_write_i32(uint8_t* p, int32_t value) {
    rb_write_u32(p, (uint32_t)value);
}

static void rb_write_f32(uint8_t* p, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    rb_write_u32(p, bits);
}

static int rb_checked_add_size(size_t a, size_t b, size_t* out) {
    if (!out || a > SIZE_MAX - b) return 0;
    *out = a + b;
    return 1;
}

static int rb_checked_mul_size(size_t a, size_t b, size_t* out) {
    if (!out || (a != 0u && b > SIZE_MAX / a)) return 0;
    *out = a * b;
    return 1;
}

static uint32_t rb_crc32_zero_range(const uint8_t* bytes,
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

static int rb_f32_bits_canonical(uint32_t bits) {
    uint32_t exponent = (bits >> 23) & UINT32_C(0xff);
    if (exponent == UINT32_C(0xff)) return 0;
    return bits != UINT32_C(0x80000000);
}

static int rb_f32_canonical(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return rb_f32_bits_canonical(bits);
}

static int rb_f64_bits_canonical(uint64_t bits) {
    uint64_t exponent = (bits >> 52) & UINT64_C(0x7ff);
    if (exponent == UINT64_C(0x7ff)) return 0;
    return bits != UINT64_C(0x8000000000000000);
}

static int rb_role_valid(uint8_t role) {
    return role <= (uint8_t)ROLLBACK_SCHEMA_ROLE_P1;
}

static int rb_player_behavior(uint32_t behavior) {
    return behavior >= (uint32_t)ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_NOTHING &&
           behavior <= (uint32_t)ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_JUMP;
}

static int rb_kind_behavior_valid(uint8_t kind, uint32_t behavior) {
    switch (kind) {
        case ROLLBACK_SCHEMA_ENTITY_FREE:
            return behavior == (uint32_t)ROLLBACK_SCHEMA_BEHAVIOR_NONE;
        case ROLLBACK_SCHEMA_ENTITY_PLAYER:
            return rb_player_behavior(behavior);
        case ROLLBACK_SCHEMA_ENTITY_SWORD:
            return behavior == (uint32_t)ROLLBACK_SCHEMA_BEHAVIOR_NONE;
        case ROLLBACK_SCHEMA_ENTITY_HAZARD:
            return behavior == (uint32_t)ROLLBACK_SCHEMA_BEHAVIOR_HAZARD_K_UPDATE;
        case ROLLBACK_SCHEMA_ENTITY_MINE:
            return behavior == (uint32_t)ROLLBACK_SCHEMA_BEHAVIOR_MINE_UPDATE;
        default:
            return 0;
    }
}

static int rb_map_object_valid(const RollbackSchemaState* state,
                               uint32_t object_id,
                               uint32_t lifecycle,
                               uint8_t object_kind) {
    uint32_t slot;
    const RollbackSchemaEntity* entity;
    if (object_id < 2u) {
        return lifecycle == 0u &&
               (object_kind == RB_MS_OBJECT_PLAYER ||
                object_kind == RB_MS_OBJECT_DEAD_BODY);
    }
    slot = object_id - 2u;
    if (slot >= ROLLBACK_SCHEMA_ENTITY_CAPACITY) return 0;
    entity = &state->entities[slot];
    if (!entity->active || entity->lifecycle != lifecycle) return 0;
    if (object_kind == RB_MS_OBJECT_SWORD) {
        return entity->kind == ROLLBACK_SCHEMA_ENTITY_SWORD;
    }
    if (object_kind == RB_MS_OBJECT_HAZARD) {
        return entity->kind == ROLLBACK_SCHEMA_ENTITY_HAZARD;
    }
    return 0;
}

static int rb_validate_map_state_entries(const uint8_t* ms,
                                         uint16_t expected_count,
                                         char* err,
                                         size_t err_cap) {
    uint16_t count = 0u;
    uint32_t i;
    uint32_t j;
    for (i = 0u; i < RB_MS_MAX_STATE; ++i) {
        const uint8_t* entry = ms + RB_MS_STATE_OFFSET + i * RB_MS_STATE_SIZE;
        uint8_t in_use = entry[0];
        uint8_t type;
        uint8_t key_len;
        uint8_t string_len;
        uint8_t bool_value;
        if (in_use == 0u) {
            if (!rb_bytes_zero(entry, RB_MS_STATE_SIZE)) {
                rb_set_error(err, err_cap, "map script has a non-canonical empty state slot");
                return 0;
            }
            continue;
        }
        if (in_use != 1u) {
            rb_set_error(err, err_cap, "map script state in_use is not canonical");
            return 0;
        }
        ++count;
        type = entry[1];
        key_len = entry[2];
        string_len = entry[3];
        bool_value = entry[4];
        if (key_len == 0u || key_len >= 32u ||
            !rb_bytes_zero(entry + 5u, 3u) ||
            memchr(entry + 16u, 0, key_len) != NULL ||
            !rb_bytes_zero(entry + 16u + key_len, 32u - key_len)) {
            rb_set_error(err, err_cap, "map script has an invalid state key");
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            const uint8_t* earlier = ms + RB_MS_STATE_OFFSET + j * RB_MS_STATE_SIZE;
            if (earlier[0] == 1u && earlier[2] == key_len &&
                memcmp(earlier + 16u, entry + 16u, key_len) == 0) {
                rb_set_error(err, err_cap, "map script has duplicate state keys");
                return 0;
            }
        }
        switch (type) {
            case 1u:
                if (bool_value > 1u || string_len != 0u ||
                    !rb_bytes_zero(entry + 8u, 8u) ||
                    !rb_bytes_zero(entry + 48u, 64u)) {
                    rb_set_error(err, err_cap, "map script boolean state is not canonical");
                    return 0;
                }
                break;
            case 2u:
                if (!rb_f64_bits_canonical(rb_read_u64(entry + 8u)) ||
                    bool_value != 0u || string_len != 0u ||
                    !rb_bytes_zero(entry + 48u, 64u)) {
                    rb_set_error(err, err_cap, "map script number state is not canonical");
                    return 0;
                }
                break;
            case 3u:
                if (string_len >= 64u || bool_value != 0u ||
                    !rb_bytes_zero(entry + 8u, 8u) ||
                    !rb_bytes_zero(entry + 48u + string_len,
                                   64u - string_len)) {
                    rb_set_error(err, err_cap, "map script string state is not canonical");
                    return 0;
                }
                break;
            default:
                rb_set_error(err, err_cap, "map script has an unknown state type");
                return 0;
        }
    }
    if (count != expected_count) {
        rb_set_error(err, err_cap, "map script state count does not match its slots");
        return 0;
    }
    return 1;
}

static int rb_validate_map_overrides(const RollbackSchemaState* state,
                                     const uint8_t* ms,
                                     uint64_t tick,
                                     uint16_t expected_count,
                                     char* err,
                                     size_t err_cap) {
    uint16_t count = 0u;
    uint32_t i;
    uint32_t j;
    for (i = 0u; i < RB_MS_MAX_OVERRIDES; ++i) {
        const uint8_t* item = ms + RB_MS_OVERRIDE_OFFSET + i * RB_MS_OVERRIDE_SIZE;
        uint32_t cell;
        int32_t sprite;
        int32_t offset_x;
        int32_t offset_y;
        uint64_t expiry;
        if (item[0] == 0u) {
            if (!rb_bytes_zero(item, RB_MS_OVERRIDE_SIZE)) {
                rb_set_error(err, err_cap, "map script has a non-canonical empty override");
                return 0;
            }
            continue;
        }
        if (item[0] != 1u || !rb_bytes_zero(item + 1u, 3u)) {
            rb_set_error(err, err_cap, "map script override marker is invalid");
            return 0;
        }
        ++count;
        cell = rb_read_u32(item + 4u);
        sprite = rb_read_i32(item + 8u);
        offset_x = rb_read_i32(item + 12u);
        offset_y = rb_read_i32(item + 16u);
        expiry = rb_read_u64(item + 20u);
        if (cell >= state->tile_cell_count || sprite < 0 ||
            offset_x < -RB_MS_RENDER_OFFSET_LIMIT_Q ||
            offset_x > RB_MS_RENDER_OFFSET_LIMIT_Q ||
            offset_y < -RB_MS_RENDER_OFFSET_LIMIT_Q ||
            offset_y > RB_MS_RENDER_OFFSET_LIMIT_Q || expiry <= tick) {
            rb_set_error(err, err_cap, "map script override data is invalid");
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            const uint8_t* earlier = ms + RB_MS_OVERRIDE_OFFSET + j * RB_MS_OVERRIDE_SIZE;
            if (earlier[0] == 1u && rb_read_u32(earlier + 4u) == cell) {
                rb_set_error(err, err_cap, "map script has duplicate cell overrides");
                return 0;
            }
        }
    }
    if (count != expected_count) {
        rb_set_error(err, err_cap, "map script override count does not match its slots");
        return 0;
    }
    return 1;
}

static int rb_validate_map_contacts(const RollbackSchemaState* state,
                                    const uint8_t* ms,
                                    uint64_t tick,
                                    uint16_t expected_count,
                                    char* err,
                                    size_t err_cap) {
    uint16_t count = 0u;
    uint32_t i;
    uint32_t j;
    for (i = 0u; i < RB_MS_MAX_CONTACTS; ++i) {
        const uint8_t* item = ms + RB_MS_CONTACT_OFFSET + i * RB_MS_CONTACT_SIZE;
        uint8_t kind;
        uint16_t binding;
        uint32_t object_id;
        uint32_t lifecycle;
        uint32_t cell;
        uint8_t mirrored;
        uint8_t scope;
        uint64_t last_seen;
        if (item[0] == 0u) {
            if (!rb_bytes_zero(item, RB_MS_CONTACT_SIZE)) {
                rb_set_error(err, err_cap, "map script has a non-canonical empty contact");
                return 0;
            }
            continue;
        }
        if (item[0] != 1u) {
            rb_set_error(err, err_cap, "map script contact marker is invalid");
            return 0;
        }
        ++count;
        kind = item[1];
        binding = rb_read_u16(item + 2u);
        object_id = rb_read_u32(item + 4u);
        lifecycle = rb_read_u32(item + 8u);
        cell = rb_read_u32(item + 12u);
        mirrored = item[40];
        scope = item[41];
        last_seen = rb_read_u64(item + 48u);
        if (binding >= 256u || cell >= state->tile_cell_count ||
            mirrored > 1u || scope > 1u ||
            !rb_bytes_zero(item + 42u, 6u) || last_seen > tick ||
            !rb_f32_bits_canonical(rb_read_u32(item + 24u)) ||
            !rb_f32_bits_canonical(rb_read_u32(item + 28u)) ||
            !rb_f32_bits_canonical(rb_read_u32(item + 32u)) ||
            !rb_f32_bits_canonical(rb_read_u32(item + 36u)) ||
            !rb_map_object_valid(state, object_id, lifecycle, kind)) {
            rb_set_error(err, err_cap, "map script contact data is invalid");
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            const uint8_t* earlier = ms + RB_MS_CONTACT_OFFSET + j * RB_MS_CONTACT_SIZE;
            if (earlier[0] == 1u &&
                rb_read_u32(earlier + 4u) == object_id &&
                rb_read_u32(earlier + 8u) == lifecycle) {
                uint8_t earlier_kind = earlier[1];
                uint8_t earlier_scope = earlier[41];
                if (earlier_kind != kind ||
                    (scope == 0u && earlier_scope == 0u &&
                     rb_read_u32(earlier + 12u) == cell) ||
                    (scope == 1u && earlier_scope == 1u &&
                     rb_read_u16(earlier + 2u) == binding)) {
                    rb_set_error(err, err_cap, "map script has duplicate/inconsistent contacts");
                    return 0;
                }
            }
        }
    }
    if (count != expected_count) {
        rb_set_error(err, err_cap, "map script contact count does not match its slots");
        return 0;
    }
    return 1;
}

static int rb_validate_bound(const uint8_t* p, int present) {
    uint32_t bits = rb_read_u32(p);
    float value;
    if (!present) return bits == 0u;
    if (!rb_f32_bits_canonical(bits)) return 0;
    memcpy(&value, &bits, sizeof(value));
    return value >= -64.0f && value <= 64.0f;
}

static int rb_validate_map_limits(const RollbackSchemaState* state,
                                  const uint8_t* ms,
                                  uint64_t tick,
                                  uint16_t expected_count,
                                  char* err,
                                  size_t err_cap) {
    uint16_t count = 0u;
    uint32_t i;
    uint32_t j;
    for (i = 0u; i < RB_MS_MAX_LIMITS; ++i) {
        const uint8_t* item = ms + RB_MS_LIMIT_OFFSET + i * RB_MS_LIMIT_SIZE;
        uint8_t kind;
        uint8_t flags;
        uint32_t object_id;
        uint32_t lifecycle;
        uint64_t expiry;
        if (item[0] == 0u) {
            if (!rb_bytes_zero(item, RB_MS_LIMIT_SIZE)) {
                rb_set_error(err, err_cap, "map script has a non-canonical empty velocity limit");
                return 0;
            }
            continue;
        }
        if (item[0] != 1u || item[3] != 0u) {
            rb_set_error(err, err_cap, "map script velocity marker is invalid");
            return 0;
        }
        ++count;
        kind = item[1];
        flags = item[2];
        object_id = rb_read_u32(item + 4u);
        lifecycle = rb_read_u32(item + 8u);
        expiry = rb_read_u64(item + 28u);
        if (flags == 0u || (flags & (uint8_t)~RB_MS_LIMIT_ALL) != 0u ||
            expiry <= tick ||
            !rb_map_object_valid(state, object_id, lifecycle, kind) ||
            !rb_validate_bound(item + 12u, (flags & RB_MS_LIMIT_MIN_VX) != 0u) ||
            !rb_validate_bound(item + 16u, (flags & RB_MS_LIMIT_MAX_VX) != 0u) ||
            !rb_validate_bound(item + 20u, (flags & RB_MS_LIMIT_MIN_VY) != 0u) ||
            !rb_validate_bound(item + 24u, (flags & RB_MS_LIMIT_MAX_VY) != 0u)) {
            rb_set_error(err, err_cap, "map script velocity limit is invalid");
            return 0;
        }
        if ((flags & (RB_MS_LIMIT_MIN_VX | RB_MS_LIMIT_MAX_VX)) ==
                (RB_MS_LIMIT_MIN_VX | RB_MS_LIMIT_MAX_VX) &&
            rb_read_f32(item + 12u) > rb_read_f32(item + 16u)) {
            rb_set_error(err, err_cap, "map script x velocity bounds are reversed");
            return 0;
        }
        if ((flags & (RB_MS_LIMIT_MIN_VY | RB_MS_LIMIT_MAX_VY)) ==
                (RB_MS_LIMIT_MIN_VY | RB_MS_LIMIT_MAX_VY) &&
            rb_read_f32(item + 20u) > rb_read_f32(item + 24u)) {
            rb_set_error(err, err_cap, "map script y velocity bounds are reversed");
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            const uint8_t* earlier = ms + RB_MS_LIMIT_OFFSET + j * RB_MS_LIMIT_SIZE;
            if (earlier[0] == 1u &&
                rb_read_u32(earlier + 4u) == object_id &&
                rb_read_u32(earlier + 8u) == lifecycle) {
                rb_set_error(err, err_cap, "map script has duplicate velocity limits");
                return 0;
            }
        }
    }
    if (count != expected_count) {
        rb_set_error(err, err_cap, "map script velocity count does not match its slots");
        return 0;
    }
    return 1;
}

static int rb_validate_map_script(const RollbackSchemaState* state,
                                  char* err,
                                  size_t err_cap) {
    const uint8_t* ms = state->map_script_bytes;
    uint64_t script_id;
    uint64_t tick;
    uint32_t flags;
    uint16_t state_count;
    uint16_t override_count;
    uint16_t contact_count;
    uint16_t limit_count;
    uint32_t i;

    if (rb_read_u32(ms + RB_MS_MAGIC_OFFSET) != RB_MS_MAGIC ||
        rb_read_u16(ms + RB_MS_VERSION_OFFSET) != ROLLBACK_SCHEMA_MAP_SCRIPT_VERSION ||
        rb_read_u16(ms + RB_MS_HEADER_SIZE_OFFSET) != RB_MS_STATE_OFFSET ||
        rb_read_u32(ms + RB_MS_TOTAL_SIZE_OFFSET) != ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES) {
        rb_set_error(err, err_cap, "embedded map script header/version is invalid");
        return 0;
    }
    if (rb_read_u32(ms + RB_MS_CHECKSUM_OFFSET) !=
        rb_crc32_zero_range(ms, ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES,
                            RB_MS_CHECKSUM_OFFSET, sizeof(uint32_t))) {
        rb_set_error(err, err_cap, "embedded map script checksum is invalid");
        return 0;
    }
    if (!rb_bytes_zero(ms + RB_MS_RESERVED_OFFSET, 16u)) {
        rb_set_error(err, err_cap, "embedded map script reserved bytes are non-zero");
        return 0;
    }

    script_id = rb_read_u64(ms + RB_MS_SCRIPT_ID_OFFSET);
    tick = rb_read_u64(ms + RB_MS_TICK_OFFSET);
    flags = rb_read_u32(ms + RB_MS_FLAGS_OFFSET);
    state_count = rb_read_u16(ms + RB_MS_STATE_COUNT_OFFSET);
    override_count = rb_read_u16(ms + RB_MS_OVERRIDE_COUNT_OFFSET);
    contact_count = rb_read_u16(ms + RB_MS_CONTACT_COUNT_OFFSET);
    limit_count = rb_read_u16(ms + RB_MS_LIMIT_COUNT_OFFSET);
    if ((flags & ~RB_MS_FLAG_FAULTED) != 0u ||
        state_count > RB_MS_MAX_STATE || override_count > RB_MS_MAX_OVERRIDES ||
        contact_count > RB_MS_MAX_CONTACTS || limit_count > RB_MS_MAX_LIMITS) {
        rb_set_error(err, err_cap, "embedded map script counts/flags are invalid");
        return 0;
    }

    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        uint32_t lifecycle = rb_read_u32(ms + RB_MS_LIFECYCLE_OFFSET + i * 4u);
        if (lifecycle != state->entities[i].lifecycle) {
            rb_set_error(err, err_cap,
                         "entity %u lifecycle disagrees with map script snapshot",
                         (unsigned)i);
            return 0;
        }
    }

    if (script_id == 0u) {
        if (tick != 0u || rb_read_u32(ms + RB_MS_RNG_OFFSET) != 0u || flags != 0u ||
            state_count != 0u || override_count != 0u || contact_count != 0u ||
            limit_count != 0u ||
            !rb_bytes_zero(ms + RB_MS_LIFECYCLE_OFFSET,
                           ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES - RB_MS_LIFECYCLE_OFFSET)) {
            rb_set_error(err, err_cap, "inactive map script snapshot is not canonical");
            return 0;
        }
        return 1;
    }
    if (rb_read_u32(ms + RB_MS_RNG_OFFSET) == 0u) {
        rb_set_error(err, err_cap, "active map script RNG state is zero");
        return 0;
    }
    if ((flags & RB_MS_FLAG_FAULTED) != 0u &&
        (override_count != 0u || contact_count != 0u || limit_count != 0u)) {
        rb_set_error(err, err_cap, "faulted map script retains active effects");
        return 0;
    }
    {
        uint32_t timer_count = rb_read_u32(ms + 29448u);
        if (timer_count > 32u) { rb_set_error(err, err_cap, "invalid timer count"); return 0; }
        for (i = 0; i < 32u; ++i) {
            uint32_t remaining = rb_read_u32(ms + 29452u + i * 8u);
            uint32_t interval = rb_read_u32(ms + 29456u + i * 8u);
            if (remaining > 1000000000u || interval > 1000000000u ||
                (interval && !remaining) || (i >= timer_count && (remaining || interval))) {
                rb_set_error(err, err_cap, "invalid timer state"); return 0;
            }
        }
    }
    return rb_validate_map_state_entries(ms, state_count, err, err_cap) &&
           rb_validate_map_overrides(state, ms, tick, override_count, err, err_cap) &&
           rb_validate_map_contacts(state, ms, tick, contact_count, err, err_cap) &&
           rb_validate_map_limits(state, ms, tick, limit_count, err, err_cap);
}

static int rb_wire_size_for_cells(uint32_t cell_count, size_t* out_size) {
    size_t cells_size;
    size_t tile_size;
    size_t total;
    if (cell_count > ROLLBACK_SCHEMA_MAX_TILE_CELLS ||
        !rb_checked_mul_size((size_t)cell_count,
                             ROLLBACK_SCHEMA_TILE_CELL_BYTES, &cells_size) ||
        !rb_checked_add_size(RB_TILE_HEADER_SIZE, cells_size, &tile_size) ||
        !rb_checked_add_size(RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE +
                             RB_ALLOCATOR_SIZE + RB_ENTITY_SECTION_SIZE +
                             RB_DANGER_SIZE,
                             tile_size, &total) ||
        !rb_checked_add_size(total, RB_MAP_SCRIPT_SECTION_SIZE, &total) ||
        total > UINT32_MAX) {
        return 0;
    }
    *out_size = total;
    return 1;
}

int rollback_schema_validate_state(const RollbackSchemaState* state,
                                   char* err,
                                   size_t err_cap) {
    uint32_t active_count = 0u;
    uint32_t player_count = 0u;
    uint8_t danger_referenced[ROLLBACK_SCHEMA_ENTITY_CAPACITY] = {0};
    uint32_t i;
    size_t expected_cells;
    if (err && err_cap) err[0] = '\0';
    if (!state) {
        rb_set_error(err, err_cap, "rollback state is required");
        return 0;
    }
    if (state->identity.game_build_id == 0u ||
        state->identity.framework_build_id == 0u ||
        state->identity.content_id == 0u || state->identity.map_id == 0u ||
        state->identity.protocol_version == 0u) {
        rb_set_error(err, err_cap, "rollback identity fields must be non-zero");
        return 0;
    }
    if (state->globals.round_end_any > 1u || state->globals.game_started > 1u ||
        state->globals.freeze > 1u || state->globals.debug_enabled > 1u) {
        rb_set_error(err, err_cap, "rollback boolean globals are not canonical");
        return 0;
    }
    if (!rb_f32_canonical(state->globals.camera_x) ||
        !rb_f32_canonical(state->globals.camera_y) ||
        !rb_f32_canonical(state->globals.camera_shake) ||
        !rb_f32_canonical(state->globals.camera_shake_decay)) {
        rb_set_error(err, err_cap, "rollback float globals are not canonical");
        return 0;
    }
    if (state->globals.roomdef_count <= 0 || state->globals.roomdef_count > 4096 ||
        state->globals.room_width <= 0 || state->globals.room_width > 4096 ||
        state->globals.room_pixel_width <= 0 ||
        state->globals.map_width <= 0 || state->globals.map_width > 4096 ||
        state->globals.map_height <= 0 || state->globals.map_height > 4096 ||
        state->globals.tile_width <= 0 || state->globals.tile_width > 4096 ||
        state->globals.tile_height <= 0 || state->globals.tile_height > 4096 ||
        state->globals.tilemap_pixel_width <= 0 ||
        state->globals.tilemap_pixel_height <= 0) {
        rb_set_error(err, err_cap, "rollback layout globals are out of range");
        return 0;
    }
    if (state->allocator.capacity != ROLLBACK_SCHEMA_ENTITY_CAPACITY ||
        state->allocator.active_count > ROLLBACK_SCHEMA_ENTITY_CAPACITY ||
        state->allocator.cursor == 0u ||
        state->allocator.cursor >= ROLLBACK_SCHEMA_ENTITY_CAPACITY) {
        rb_set_error(err, err_cap, "rollback allocator capacity/count/cursor is invalid");
        return 0;
    }
    if (state->danger.count > ROLLBACK_SCHEMA_DANGER_CAPACITY) {
        rb_set_error(err, err_cap, "danger reference count is out of range");
        return 0;
    }
    for (i = 0u; i < ROLLBACK_SCHEMA_DANGER_CAPACITY; ++i) {
        uint8_t slot = state->danger.slots[i];
        if (i < state->danger.count) {
            if (slot == 0u || slot >= ROLLBACK_SCHEMA_ENTITY_CAPACITY) {
                rb_set_error(err, err_cap, "danger reference %u is invalid", (unsigned)i);
                return 0;
            }
            danger_referenced[slot] = 1u;
        } else if (slot != ROLLBACK_SCHEMA_SLOT_NONE) {
            rb_set_error(err, err_cap, "unused danger references are not canonical");
            return 0;
        }
    }
    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        const RollbackSchemaEntity* entity = &state->entities[i];
        if (entity->slot_id != i || entity->active > 1u) {
            rb_set_error(err, err_cap, "entity %u has an invalid slot marker", (unsigned)i);
            return 0;
        }
        if (i == 0u && (entity->active != 0u || entity->lifecycle != 0u)) {
            rb_set_error(err, err_cap,
                         "reserved entity slot zero is active or has lifecycle state");
            return 0;
        }
        if (!entity->active) {
            if (danger_referenced[i]) {
                if (entity->kind == ROLLBACK_SCHEMA_ENTITY_FREE ||
                    !rb_kind_behavior_valid(entity->kind, entity->behavior_id)) {
                    rb_set_error(err, err_cap,
                                 "danger tombstone %u has an invalid kind/behavior",
                                 (unsigned)i);
                    return 0;
                }
            } else {
                if (entity->kind != ROLLBACK_SCHEMA_ENTITY_FREE ||
                    entity->behavior_id != ROLLBACK_SCHEMA_BEHAVIOR_NONE ||
                    !rb_bytes_zero(entity->payload, sizeof(entity->payload))) {
                    rb_set_error(err, err_cap, "free entity %u is not canonical",
                                 (unsigned)i);
                    return 0;
                }
            }
            continue;
        }
        ++active_count;
        if (!rb_kind_behavior_valid(entity->kind, entity->behavior_id) ||
            entity->kind == ROLLBACK_SCHEMA_ENTITY_FREE) {
            rb_set_error(err, err_cap, "entity %u has an invalid kind/behavior pair",
                         (unsigned)i);
            return 0;
        }
        if (entity->kind == ROLLBACK_SCHEMA_ENTITY_PLAYER) ++player_count;
    }
    if (active_count != state->allocator.active_count || player_count != 2u) {
        rb_set_error(err, err_cap, "allocator active count or player cardinality is invalid");
        return 0;
    }
    if (state->roles.p0_slot == state->roles.p1_slot ||
        state->roles.p0_slot == 0u || state->roles.p1_slot == 0u ||
        state->roles.p0_slot >= ROLLBACK_SCHEMA_ENTITY_CAPACITY ||
        state->roles.p1_slot >= ROLLBACK_SCHEMA_ENTITY_CAPACITY ||
        !state->entities[state->roles.p0_slot].active ||
        !state->entities[state->roles.p1_slot].active ||
        state->entities[state->roles.p0_slot].kind != ROLLBACK_SCHEMA_ENTITY_PLAYER ||
        state->entities[state->roles.p1_slot].kind != ROLLBACK_SCHEMA_ENTITY_PLAYER) {
        rb_set_error(err, err_cap, "P0/P1 slot roles are invalid or duplicated");
        return 0;
    }
    if (!rb_role_valid(state->roles.controller) ||
        !rb_role_valid(state->roles.leader) ||
        !rb_role_valid(state->roles.loser)) {
        rb_set_error(err, err_cap, "controller/leader/loser role is unknown");
        return 0;
    }
    if (state->tilemap_width == 0u || state->tilemap_height == 0u ||
        state->tilemap_width > 4096u || state->tilemap_height > 4096u ||
        !rb_checked_mul_size((size_t)state->tilemap_width,
                             (size_t)state->tilemap_height, &expected_cells) ||
        expected_cells != state->tile_cell_count ||
        state->tile_cell_count > ROLLBACK_SCHEMA_MAX_TILE_CELLS) {
        rb_set_error(err, err_cap, "tilemap dimensions/count are invalid");
        return 0;
    }
    if ((uint64_t)(uint32_t)state->globals.room_width *
            (uint64_t)(uint32_t)state->globals.tile_width !=
            (uint64_t)(uint32_t)state->globals.room_pixel_width ||
        (uint64_t)state->tilemap_width *
            (uint64_t)(uint32_t)state->globals.tile_width !=
            (uint64_t)(uint32_t)state->globals.tilemap_pixel_width ||
        (uint64_t)state->tilemap_height *
            (uint64_t)(uint32_t)state->globals.tile_height !=
            (uint64_t)(uint32_t)state->globals.tilemap_pixel_height) {
        rb_set_error(err, err_cap, "tile/room pixel dimensions are inconsistent");
        return 0;
    }
    if (!rb_validate_map_script(state, err, err_cap)) return 0;
    return 1;
}

int rollback_schema_encoded_size(const RollbackSchemaState* state,
                                 size_t* out_size,
                                 char* err,
                                 size_t err_cap) {
    size_t size;
    if (err && err_cap) err[0] = '\0';
    if (!out_size) {
        rb_set_error(err, err_cap, "encoded-size output is required");
        return 0;
    }
    if (!rollback_schema_validate_state(state, err, err_cap)) return 0;
    if (!rb_wire_size_for_cells(state->tile_cell_count, &size)) {
        rb_set_error(err, err_cap, "rollback wire size overflowed");
        return 0;
    }
    *out_size = size;
    return 1;
}

int rollback_schema_validate_compatibility(
    const RollbackSchemaState* state,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    char* err,
    size_t err_cap) {
    uint64_t script_id;
    if (err && err_cap) err[0] = '\0';
    if (!expected_identity) {
        rb_set_error(err, err_cap, "expected rollback identity is required");
        return 0;
    }
    if (!rollback_schema_validate_state(state, err, err_cap)) return 0;
    if (state->identity.game_build_id != expected_identity->game_build_id ||
        state->identity.framework_build_id != expected_identity->framework_build_id ||
        state->identity.content_id != expected_identity->content_id ||
        state->identity.map_id != expected_identity->map_id ||
        state->identity.protocol_version != expected_identity->protocol_version) {
        rb_set_error(err, err_cap, "rollback compatibility identity mismatch");
        return 0;
    }
    script_id = rb_read_u64(state->map_script_bytes + RB_MS_SCRIPT_ID_OFFSET);
    if (script_id != expected_map_script_id) {
        rb_set_error(err, err_cap, "rollback map-script identity mismatch");
        return 0;
    }
    return 1;
}

static void rb_write_section_prefix(uint8_t* section, uint16_t size) {
    rb_write_u16(section, RB_SECTION_VERSION);
    rb_write_u16(section + 2u, size);
}

int rollback_schema_encode(const RollbackSchemaState* state,
                           void* dst,
                           size_t dst_cap,
                           size_t* out_size,
                           char* err,
                           size_t err_cap) {
    uint8_t* bytes = (uint8_t*)dst;
    uint8_t* globals;
    uint8_t* roles;
    uint8_t* allocator;
    uint8_t* entities;
    uint8_t* danger;
    uint8_t* tile;
    uint8_t* map_script;
    size_t total;
    size_t tile_size;
    size_t map_offset;
    uint32_t i;
    if (err && err_cap) err[0] = '\0';
    if (!rollback_schema_encoded_size(state, &total, err, err_cap)) return 0;
    if (!dst || dst_cap < total) {
        rb_set_error(err, err_cap, "rollback encode destination is too small");
        return 0;
    }
    tile_size = RB_TILE_HEADER_SIZE +
                (size_t)state->tile_cell_count * ROLLBACK_SCHEMA_TILE_CELL_BYTES;
    map_offset = RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE +
                 RB_ALLOCATOR_SIZE + RB_ENTITY_SECTION_SIZE + RB_DANGER_SIZE +
                 tile_size;
    memset(bytes, 0, total);

    rb_write_u32(bytes + RB_H_MAGIC, ROLLBACK_SCHEMA_MAGIC);
    rb_write_u16(bytes + RB_H_VERSION, ROLLBACK_SCHEMA_VERSION);
    rb_write_u16(bytes + RB_H_HEADER_SIZE, RB_HEADER_SIZE);
    rb_write_u32(bytes + RB_H_TOTAL_SIZE, (uint32_t)total);
    rb_write_u64(bytes + RB_H_GAME_BUILD, state->identity.game_build_id);
    rb_write_u64(bytes + RB_H_FRAMEWORK_BUILD, state->identity.framework_build_id);
    rb_write_u64(bytes + RB_H_CONTENT_ID, state->identity.content_id);
    rb_write_u64(bytes + RB_H_MAP_ID, state->identity.map_id);
    rb_write_u32(bytes + RB_H_PROTOCOL, state->identity.protocol_version);
    rb_write_u32(bytes + RB_H_LAYOUT_ID, ROLLBACK_SCHEMA_LAYOUT_ID);
    rb_write_u32(bytes + RB_H_GLOBALS_OFFSET, RB_HEADER_SIZE);
    rb_write_u32(bytes + RB_H_GLOBALS_SIZE, RB_GLOBALS_SIZE);
    rb_write_u32(bytes + RB_H_ROLES_OFFSET, RB_HEADER_SIZE + RB_GLOBALS_SIZE);
    rb_write_u32(bytes + RB_H_ROLES_SIZE, RB_ROLES_SIZE);
    rb_write_u32(bytes + RB_H_ALLOCATOR_OFFSET,
                 RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE);
    rb_write_u32(bytes + RB_H_ALLOCATOR_SIZE, RB_ALLOCATOR_SIZE);
    rb_write_u32(bytes + RB_H_ENTITIES_OFFSET,
                 RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE + RB_ALLOCATOR_SIZE);
    rb_write_u32(bytes + RB_H_ENTITIES_SIZE, RB_ENTITY_SECTION_SIZE);
    rb_write_u32(bytes + RB_H_DANGER_OFFSET,
                 RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE +
                 RB_ALLOCATOR_SIZE + RB_ENTITY_SECTION_SIZE);
    rb_write_u32(bytes + RB_H_DANGER_SIZE, RB_DANGER_SIZE);
    rb_write_u32(bytes + RB_H_TILE_OFFSET,
                 RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE +
                 RB_ALLOCATOR_SIZE + RB_ENTITY_SECTION_SIZE + RB_DANGER_SIZE);
    rb_write_u32(bytes + RB_H_TILE_SIZE, (uint32_t)tile_size);
    rb_write_u32(bytes + RB_H_MAP_SCRIPT_OFFSET, (uint32_t)map_offset);
    rb_write_u32(bytes + RB_H_MAP_SCRIPT_SIZE, RB_MAP_SCRIPT_SECTION_SIZE);

    globals = bytes + rb_read_u32(bytes + RB_H_GLOBALS_OFFSET);
    rb_write_section_prefix(globals, RB_GLOBALS_SIZE);
    rb_write_u32(globals + RB_G_RNG_STATE, state->globals.rng_state);
    rb_write_u32(globals + RB_G_NATIVE_TICKS, state->globals.native_game_ticks);
    rb_write_u32(globals + RB_G_MAP_SEED, state->globals.map_seed);
    rb_write_u32(globals + RB_G_GAME_LEVEL, state->globals.game_level);
    rb_write_i32(globals + RB_G_ACTIVE_ROOM, state->globals.active_room);
    rb_write_i32(globals + RB_G_OLD_ACTIVE_ROOM, state->globals.old_active_room);
    rb_write_i32(globals + RB_G_START_COUNTDOWN, state->globals.start_countdown);
    rb_write_i32(globals + RB_G_END_COUNTDOWN, state->globals.end_countdown);
    rb_write_i32(globals + RB_G_MAP_SELECTOR, state->globals.map_selector);
    rb_write_i32(globals + RB_G_MAP_MODE, state->globals.map_mode);
    rb_write_i32(globals + RB_G_SCORE_TARGET, state->globals.score_target);
    rb_write_i32(globals + RB_G_RESPAWN_LIMIT, state->globals.armed_respawn_limit);
    rb_write_i32(globals + RB_G_SCORE_P0, state->globals.score_p0);
    rb_write_i32(globals + RB_G_SCORE_P1, state->globals.score_p1);
    rb_write_i32(globals + RB_G_PLAYER_MODE0, state->globals.player_mode0);
    rb_write_i32(globals + RB_G_PLAYER_MODE1, state->globals.player_mode1);
    globals[RB_G_ROUND_END_ANY] = state->globals.round_end_any;
    globals[RB_G_GAME_STARTED] = state->globals.game_started;
    globals[RB_G_FREEZE] = state->globals.freeze;
    globals[RB_G_DEBUG_ENABLED] = state->globals.debug_enabled;
    rb_write_f32(globals + RB_G_CAMERA_X, state->globals.camera_x);
    rb_write_f32(globals + RB_G_CAMERA_Y, state->globals.camera_y);
    rb_write_f32(globals + RB_G_CAMERA_SHAKE, state->globals.camera_shake);
    rb_write_f32(globals + RB_G_CAMERA_SHAKE_DECAY, state->globals.camera_shake_decay);
    rb_write_i32(globals + RB_G_ROOMDEF_COUNT, state->globals.roomdef_count);
    rb_write_i32(globals + RB_G_ROOM_WIDTH, state->globals.room_width);
    rb_write_i32(globals + RB_G_ROOM_PIXEL_WIDTH, state->globals.room_pixel_width);
    rb_write_i32(globals + RB_G_MAP_WIDTH, state->globals.map_width);
    rb_write_i32(globals + RB_G_MAP_HEIGHT, state->globals.map_height);
    rb_write_i32(globals + RB_G_TILE_WIDTH, state->globals.tile_width);
    rb_write_i32(globals + RB_G_TILE_HEIGHT, state->globals.tile_height);
    rb_write_i32(globals + RB_G_TILEMAP_PIXEL_WIDTH, state->globals.tilemap_pixel_width);
    rb_write_i32(globals + RB_G_TILEMAP_PIXEL_HEIGHT, state->globals.tilemap_pixel_height);

    roles = bytes + rb_read_u32(bytes + RB_H_ROLES_OFFSET);
    rb_write_section_prefix(roles, RB_ROLES_SIZE);
    roles[4] = state->roles.p0_slot;
    roles[5] = state->roles.p1_slot;
    roles[6] = state->roles.controller;
    roles[7] = state->roles.leader;
    roles[8] = state->roles.loser;

    allocator = bytes + rb_read_u32(bytes + RB_H_ALLOCATOR_OFFSET);
    rb_write_section_prefix(allocator, RB_ALLOCATOR_SIZE);
    allocator[4] = state->allocator.capacity;
    allocator[5] = state->allocator.active_count;
    allocator[6] = state->allocator.cursor;

    entities = bytes + rb_read_u32(bytes + RB_H_ENTITIES_OFFSET);
    rb_write_u16(entities, RB_SECTION_VERSION);
    rb_write_u16(entities + 2u, RB_ENTITY_SECTION_HEADER_SIZE);
    rb_write_u32(entities + 4u, RB_ENTITY_SECTION_SIZE);
    rb_write_u16(entities + 8u, RB_ENTITY_RECORD_SIZE);
    rb_write_u16(entities + 10u, ROLLBACK_SCHEMA_ENTITY_CAPACITY);
    rb_write_u16(entities + 12u, ROLLBACK_SCHEMA_ENTITY_PAYLOAD_SIZE);
    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        const RollbackSchemaEntity* source = &state->entities[i];
        uint8_t* record = entities + RB_ENTITY_SECTION_HEADER_SIZE + i * RB_ENTITY_RECORD_SIZE;
        record[RB_E_SLOT_ID] = source->slot_id;
        record[RB_E_ACTIVE] = source->active;
        record[RB_E_KIND] = source->kind;
        rb_write_u32(record + RB_E_LIFECYCLE, source->lifecycle);
        rb_write_u32(record + RB_E_BEHAVIOR, source->behavior_id);
        rb_write_u16(record + RB_E_PAYLOAD_SIZE, ROLLBACK_SCHEMA_ENTITY_PAYLOAD_SIZE);
        memcpy(record + RB_E_PAYLOAD, source->payload, sizeof(source->payload));
    }

    danger = bytes + rb_read_u32(bytes + RB_H_DANGER_OFFSET);
    rb_write_section_prefix(danger, RB_DANGER_SIZE);
    danger[4] = state->danger.count;
    memcpy(danger + 8u, state->danger.slots, sizeof(state->danger.slots));

    tile = bytes + rb_read_u32(bytes + RB_H_TILE_OFFSET);
    rb_write_u16(tile, RB_SECTION_VERSION);
    rb_write_u16(tile + 2u, RB_TILE_HEADER_SIZE);
    rb_write_u32(tile + 4u, (uint32_t)tile_size);
    rb_write_u32(tile + 8u, state->tilemap_width);
    rb_write_u32(tile + 12u, state->tilemap_height);
    rb_write_u32(tile + 16u, state->tile_cell_count);
    rb_write_u16(tile + 20u, ROLLBACK_SCHEMA_TILE_CELL_BYTES);
    for (i = 0u; i < state->tile_cell_count; ++i) {
        rb_write_u32(tile + RB_TILE_HEADER_SIZE + i * 4u, state->tile_cells[i]);
    }

    map_script = bytes + map_offset;
    rb_write_u16(map_script, RB_SECTION_VERSION);
    rb_write_u16(map_script + 2u, RB_MAP_SCRIPT_HEADER_SIZE);
    rb_write_u32(map_script + 4u, RB_MAP_SCRIPT_SECTION_SIZE);
    rb_write_u32(map_script + 8u, ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES);
    rb_write_u16(map_script + 12u, ROLLBACK_SCHEMA_MAP_SCRIPT_VERSION);
    memcpy(map_script + RB_MAP_SCRIPT_HEADER_SIZE, state->map_script_bytes,
           ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES);

    rb_write_u32(bytes + RB_H_CHECKSUM,
                 rb_crc32_zero_range(bytes, total, RB_H_CHECKSUM, sizeof(uint32_t)));
    if (out_size) *out_size = total;
    return 1;
}

static int rb_validate_fixed_wire_sections(const uint8_t* bytes,
                                           size_t src_size,
                                           size_t* out_tile_size,
                                           size_t* out_map_offset,
                                           char* err,
                                           size_t err_cap) {
    uint32_t tile_offset = rb_read_u32(bytes + RB_H_TILE_OFFSET);
    uint32_t tile_size_u32 = rb_read_u32(bytes + RB_H_TILE_SIZE);
    uint32_t map_offset_u32 = rb_read_u32(bytes + RB_H_MAP_SCRIPT_OFFSET);
    size_t cell_bytes;
    size_t expected_tile_size;
    size_t expected_map_offset;
    const uint8_t* globals;
    const uint8_t* roles;
    const uint8_t* allocator;
    const uint8_t* entities;
    const uint8_t* danger;
    const uint8_t* tile;
    const uint8_t* map_script;
    uint32_t i;

    if (rb_read_u32(bytes + RB_H_GLOBALS_OFFSET) != RB_HEADER_SIZE ||
        rb_read_u32(bytes + RB_H_GLOBALS_SIZE) != RB_GLOBALS_SIZE ||
        rb_read_u32(bytes + RB_H_ROLES_OFFSET) != RB_HEADER_SIZE + RB_GLOBALS_SIZE ||
        rb_read_u32(bytes + RB_H_ROLES_SIZE) != RB_ROLES_SIZE ||
        rb_read_u32(bytes + RB_H_ALLOCATOR_OFFSET) !=
            RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE ||
        rb_read_u32(bytes + RB_H_ALLOCATOR_SIZE) != RB_ALLOCATOR_SIZE ||
        rb_read_u32(bytes + RB_H_ENTITIES_OFFSET) !=
            RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE + RB_ALLOCATOR_SIZE ||
        rb_read_u32(bytes + RB_H_ENTITIES_SIZE) != RB_ENTITY_SECTION_SIZE ||
        rb_read_u32(bytes + RB_H_DANGER_OFFSET) !=
            RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE + RB_ALLOCATOR_SIZE +
            RB_ENTITY_SECTION_SIZE ||
        rb_read_u32(bytes + RB_H_DANGER_SIZE) != RB_DANGER_SIZE ||
        tile_offset != RB_HEADER_SIZE + RB_GLOBALS_SIZE + RB_ROLES_SIZE +
                       RB_ALLOCATOR_SIZE + RB_ENTITY_SECTION_SIZE + RB_DANGER_SIZE) {
        rb_set_error(err, err_cap, "rollback section directory is not canonical");
        return 0;
    }
    if (tile_size_u32 < RB_TILE_HEADER_SIZE ||
        (size_t)tile_offset > src_size || (size_t)tile_size_u32 > src_size - tile_offset) {
        rb_set_error(err, err_cap, "rollback tile section is out of bounds");
        return 0;
    }
    tile = bytes + tile_offset;
    if (rb_read_u16(tile) != RB_SECTION_VERSION ||
        rb_read_u16(tile + 2u) != RB_TILE_HEADER_SIZE ||
        rb_read_u32(tile + 4u) != tile_size_u32 ||
        rb_read_u16(tile + 20u) != ROLLBACK_SCHEMA_TILE_CELL_BYTES ||
        !rb_bytes_zero(tile + 22u, 10u) ||
        !rb_checked_mul_size((size_t)rb_read_u32(tile + 16u),
                             ROLLBACK_SCHEMA_TILE_CELL_BYTES, &cell_bytes) ||
        !rb_checked_add_size(RB_TILE_HEADER_SIZE, cell_bytes, &expected_tile_size) ||
        expected_tile_size != tile_size_u32) {
        rb_set_error(err, err_cap, "rollback tile section header/size is invalid");
        return 0;
    }
    if (!rb_checked_add_size(tile_offset, expected_tile_size, &expected_map_offset) ||
        expected_map_offset != map_offset_u32 ||
        rb_read_u32(bytes + RB_H_MAP_SCRIPT_SIZE) != RB_MAP_SCRIPT_SECTION_SIZE ||
        expected_map_offset > src_size ||
        RB_MAP_SCRIPT_SECTION_SIZE > src_size - expected_map_offset ||
        expected_map_offset + RB_MAP_SCRIPT_SECTION_SIZE != src_size) {
        rb_set_error(err, err_cap, "rollback map-script section is out of bounds");
        return 0;
    }

    globals = bytes + rb_read_u32(bytes + RB_H_GLOBALS_OFFSET);
    roles = bytes + rb_read_u32(bytes + RB_H_ROLES_OFFSET);
    allocator = bytes + rb_read_u32(bytes + RB_H_ALLOCATOR_OFFSET);
    entities = bytes + rb_read_u32(bytes + RB_H_ENTITIES_OFFSET);
    danger = bytes + rb_read_u32(bytes + RB_H_DANGER_OFFSET);
    map_script = bytes + expected_map_offset;
    if (rb_read_u16(globals) != RB_SECTION_VERSION ||
        rb_read_u16(globals + 2u) != RB_GLOBALS_SIZE ||
        !rb_bytes_zero(globals + 4u, 4u) ||
        !rb_bytes_zero(globals + RB_G_RESERVED1, RB_GLOBALS_SIZE - RB_G_RESERVED1) ||
        rb_read_u16(roles) != RB_SECTION_VERSION ||
        rb_read_u16(roles + 2u) != RB_ROLES_SIZE ||
        !rb_bytes_zero(roles + 9u, 7u) ||
        rb_read_u16(allocator) != RB_SECTION_VERSION ||
        rb_read_u16(allocator + 2u) != RB_ALLOCATOR_SIZE ||
        allocator[7] != 0u || !rb_bytes_zero(allocator + 8u, 8u) ||
        rb_read_u16(entities) != RB_SECTION_VERSION ||
        rb_read_u16(entities + 2u) != RB_ENTITY_SECTION_HEADER_SIZE ||
        rb_read_u32(entities + 4u) != RB_ENTITY_SECTION_SIZE ||
        rb_read_u16(entities + 8u) != RB_ENTITY_RECORD_SIZE ||
        rb_read_u16(entities + 10u) != ROLLBACK_SCHEMA_ENTITY_CAPACITY ||
        rb_read_u16(entities + 12u) != ROLLBACK_SCHEMA_ENTITY_PAYLOAD_SIZE ||
        rb_read_u16(entities + 14u) != 0u ||
        rb_read_u16(danger) != RB_SECTION_VERSION ||
        rb_read_u16(danger + 2u) != RB_DANGER_SIZE ||
        !rb_bytes_zero(danger + 5u, 3u) || !rb_bytes_zero(danger + 24u, 8u) ||
        rb_read_u16(map_script) != RB_SECTION_VERSION ||
        rb_read_u16(map_script + 2u) != RB_MAP_SCRIPT_HEADER_SIZE ||
        rb_read_u32(map_script + 4u) != RB_MAP_SCRIPT_SECTION_SIZE ||
        rb_read_u32(map_script + 8u) != ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES ||
        rb_read_u16(map_script + 12u) != ROLLBACK_SCHEMA_MAP_SCRIPT_VERSION ||
        rb_read_u16(map_script + 14u) != 0u) {
        rb_set_error(err, err_cap, "rollback section header/reserved bytes are invalid");
        return 0;
    }
    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        const uint8_t* record = entities + RB_ENTITY_SECTION_HEADER_SIZE +
                                i * RB_ENTITY_RECORD_SIZE;
        if (record[RB_E_RESERVED0] != 0u ||
            rb_read_u16(record + RB_E_PAYLOAD_SIZE) !=
                ROLLBACK_SCHEMA_ENTITY_PAYLOAD_SIZE ||
            rb_read_u16(record + RB_E_RESERVED1) != 0u ||
            rb_read_u16(record + RB_E_RESERVED2) != 0u) {
            rb_set_error(err, err_cap, "entity %u wire reserved/size fields are invalid",
                         (unsigned)i);
            return 0;
        }
    }
    *out_tile_size = expected_tile_size;
    *out_map_offset = expected_map_offset;
    return 1;
}

static int rb_decode_into_scratch(const uint8_t* bytes,
                                  size_t src_size,
                                  RollbackSchemaState* state,
                                  char* err,
                                  size_t err_cap) {
    const uint8_t* globals;
    const uint8_t* roles;
    const uint8_t* allocator;
    const uint8_t* entities;
    const uint8_t* danger;
    const uint8_t* tile;
    const uint8_t* map_script;
    size_t tile_size;
    size_t map_offset;
    uint32_t i;
    if (src_size < RB_HEADER_SIZE) {
        rb_set_error(err, err_cap, "rollback wire image is truncated");
        return 0;
    }
    if (rb_read_u32(bytes + RB_H_MAGIC) != ROLLBACK_SCHEMA_MAGIC ||
        rb_read_u16(bytes + RB_H_VERSION) != ROLLBACK_SCHEMA_VERSION ||
        rb_read_u16(bytes + RB_H_HEADER_SIZE) != RB_HEADER_SIZE ||
        rb_read_u32(bytes + RB_H_TOTAL_SIZE) != src_size ||
        rb_read_u32(bytes + RB_H_FLAGS) != 0u ||
        rb_read_u32(bytes + RB_H_RESERVED0) != 0u ||
        rb_read_u32(bytes + RB_H_LAYOUT_ID) != ROLLBACK_SCHEMA_LAYOUT_ID ||
        !rb_bytes_zero(bytes + RB_H_RESERVED1, 8u)) {
        rb_set_error(err, err_cap, "rollback wire header/version/layout is invalid");
        return 0;
    }
    if (rb_read_u32(bytes + RB_H_CHECKSUM) !=
        rb_crc32_zero_range(bytes, src_size, RB_H_CHECKSUM, sizeof(uint32_t))) {
        rb_set_error(err, err_cap, "rollback wire checksum is invalid");
        return 0;
    }
    if (!rb_validate_fixed_wire_sections(bytes, src_size, &tile_size, &map_offset,
                                         err, err_cap)) {
        return 0;
    }
    (void)tile_size;
    state->identity.game_build_id = rb_read_u64(bytes + RB_H_GAME_BUILD);
    state->identity.framework_build_id = rb_read_u64(bytes + RB_H_FRAMEWORK_BUILD);
    state->identity.content_id = rb_read_u64(bytes + RB_H_CONTENT_ID);
    state->identity.map_id = rb_read_u64(bytes + RB_H_MAP_ID);
    state->identity.protocol_version = rb_read_u32(bytes + RB_H_PROTOCOL);

    globals = bytes + rb_read_u32(bytes + RB_H_GLOBALS_OFFSET);
    state->globals.rng_state = rb_read_u32(globals + RB_G_RNG_STATE);
    state->globals.native_game_ticks = rb_read_u32(globals + RB_G_NATIVE_TICKS);
    state->globals.map_seed = rb_read_u32(globals + RB_G_MAP_SEED);
    state->globals.game_level = rb_read_u32(globals + RB_G_GAME_LEVEL);
    state->globals.active_room = rb_read_i32(globals + RB_G_ACTIVE_ROOM);
    state->globals.old_active_room = rb_read_i32(globals + RB_G_OLD_ACTIVE_ROOM);
    state->globals.start_countdown = rb_read_i32(globals + RB_G_START_COUNTDOWN);
    state->globals.end_countdown = rb_read_i32(globals + RB_G_END_COUNTDOWN);
    state->globals.map_selector = rb_read_i32(globals + RB_G_MAP_SELECTOR);
    state->globals.map_mode = rb_read_i32(globals + RB_G_MAP_MODE);
    state->globals.score_target = rb_read_i32(globals + RB_G_SCORE_TARGET);
    state->globals.armed_respawn_limit = rb_read_i32(globals + RB_G_RESPAWN_LIMIT);
    state->globals.score_p0 = rb_read_i32(globals + RB_G_SCORE_P0);
    state->globals.score_p1 = rb_read_i32(globals + RB_G_SCORE_P1);
    state->globals.player_mode0 = rb_read_i32(globals + RB_G_PLAYER_MODE0);
    state->globals.player_mode1 = rb_read_i32(globals + RB_G_PLAYER_MODE1);
    state->globals.round_end_any = globals[RB_G_ROUND_END_ANY];
    state->globals.game_started = globals[RB_G_GAME_STARTED];
    state->globals.freeze = globals[RB_G_FREEZE];
    state->globals.debug_enabled = globals[RB_G_DEBUG_ENABLED];
    state->globals.camera_x = rb_read_f32(globals + RB_G_CAMERA_X);
    state->globals.camera_y = rb_read_f32(globals + RB_G_CAMERA_Y);
    state->globals.camera_shake = rb_read_f32(globals + RB_G_CAMERA_SHAKE);
    state->globals.camera_shake_decay = rb_read_f32(globals + RB_G_CAMERA_SHAKE_DECAY);
    state->globals.roomdef_count = rb_read_i32(globals + RB_G_ROOMDEF_COUNT);
    state->globals.room_width = rb_read_i32(globals + RB_G_ROOM_WIDTH);
    state->globals.room_pixel_width = rb_read_i32(globals + RB_G_ROOM_PIXEL_WIDTH);
    state->globals.map_width = rb_read_i32(globals + RB_G_MAP_WIDTH);
    state->globals.map_height = rb_read_i32(globals + RB_G_MAP_HEIGHT);
    state->globals.tile_width = rb_read_i32(globals + RB_G_TILE_WIDTH);
    state->globals.tile_height = rb_read_i32(globals + RB_G_TILE_HEIGHT);
    state->globals.tilemap_pixel_width = rb_read_i32(globals + RB_G_TILEMAP_PIXEL_WIDTH);
    state->globals.tilemap_pixel_height = rb_read_i32(globals + RB_G_TILEMAP_PIXEL_HEIGHT);

    roles = bytes + rb_read_u32(bytes + RB_H_ROLES_OFFSET);
    state->roles.p0_slot = roles[4];
    state->roles.p1_slot = roles[5];
    state->roles.controller = roles[6];
    state->roles.leader = roles[7];
    state->roles.loser = roles[8];

    allocator = bytes + rb_read_u32(bytes + RB_H_ALLOCATOR_OFFSET);
    state->allocator.capacity = allocator[4];
    state->allocator.active_count = allocator[5];
    state->allocator.cursor = allocator[6];

    entities = bytes + rb_read_u32(bytes + RB_H_ENTITIES_OFFSET);
    for (i = 0u; i < ROLLBACK_SCHEMA_ENTITY_CAPACITY; ++i) {
        RollbackSchemaEntity* target = &state->entities[i];
        const uint8_t* record = entities + RB_ENTITY_SECTION_HEADER_SIZE +
                                i * RB_ENTITY_RECORD_SIZE;
        target->slot_id = record[RB_E_SLOT_ID];
        target->active = record[RB_E_ACTIVE];
        target->kind = record[RB_E_KIND];
        target->lifecycle = rb_read_u32(record + RB_E_LIFECYCLE);
        target->behavior_id = rb_read_u32(record + RB_E_BEHAVIOR);
        memcpy(target->payload, record + RB_E_PAYLOAD, sizeof(target->payload));
    }

    danger = bytes + rb_read_u32(bytes + RB_H_DANGER_OFFSET);
    state->danger.count = danger[4];
    memcpy(state->danger.slots, danger + 8u, sizeof(state->danger.slots));

    tile = bytes + rb_read_u32(bytes + RB_H_TILE_OFFSET);
    state->tilemap_width = rb_read_u32(tile + 8u);
    state->tilemap_height = rb_read_u32(tile + 12u);
    state->tile_cell_count = rb_read_u32(tile + 16u);
    if (state->tile_cell_count > ROLLBACK_SCHEMA_MAX_TILE_CELLS) {
        rb_set_error(err, err_cap, "rollback tile count exceeds the fixed capacity");
        return 0;
    }
    for (i = 0u; i < state->tile_cell_count; ++i) {
        state->tile_cells[i] = rb_read_u32(tile + RB_TILE_HEADER_SIZE + i * 4u);
    }

    map_script = bytes + map_offset;
    memcpy(state->map_script_bytes, map_script + RB_MAP_SCRIPT_HEADER_SIZE,
           ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES);
    return rollback_schema_validate_state(state, err, err_cap);
}

int rollback_schema_decode(const void* src,
                           size_t src_size,
                           RollbackSchemaState* out_state,
                           char* err,
                           size_t err_cap) {
    const uint8_t* bytes = (const uint8_t*)src;
    RollbackSchemaState* scratch = NULL;
    uint8_t* canonical = NULL;
    size_t canonical_size = 0u;
    size_t maximum_size = 0u;
    int ok = 0;
    if (err && err_cap) err[0] = '\0';
    if (!src || !out_state) {
        rb_set_error(err, err_cap, "rollback source and output state are required");
        return 0;
    }
    if (!rb_wire_size_for_cells(ROLLBACK_SCHEMA_MAX_TILE_CELLS, &maximum_size) ||
        src_size < RB_HEADER_SIZE || src_size > maximum_size) {
        rb_set_error(err, err_cap, "rollback wire image length is out of range");
        return 0;
    }
    scratch = (RollbackSchemaState*)calloc(1u, sizeof(*scratch));
    if (!scratch) {
        rb_set_error(err, err_cap, "out of memory decoding rollback state");
        return 0;
    }
    if (!rb_decode_into_scratch(bytes, src_size, scratch, err, err_cap)) goto done;
    canonical = (uint8_t*)malloc(src_size);
    if (!canonical) {
        rb_set_error(err, err_cap, "out of memory canonicalizing rollback state");
        goto done;
    }
    if (!rollback_schema_encode(scratch, canonical, src_size, &canonical_size,
                                err, err_cap)) {
        goto done;
    }
    if (canonical_size != src_size || memcmp(canonical, bytes, src_size) != 0) {
        rb_set_error(err, err_cap, "rollback wire image is not canonical");
        goto done;
    }
    memcpy(out_state, scratch, sizeof(*scratch));
    ok = 1;
done:
    free(canonical);
    free(scratch);
    return ok;
}

int rollback_schema_validate_wire(const void* src,
                                  size_t src_size,
                                  char* err,
                                  size_t err_cap) {
    RollbackSchemaState* state;
    int ok;
    if (err && err_cap) err[0] = '\0';
    state = (RollbackSchemaState*)malloc(sizeof(*state));
    if (!state) {
        rb_set_error(err, err_cap, "out of memory validating rollback state");
        return 0;
    }
    ok = rollback_schema_decode(src, src_size, state, err, err_cap);
    free(state);
    return ok;
}

int rollback_schema_decode_expected(
    const void* src,
    size_t src_size,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    RollbackSchemaState* out_state,
    char* err,
    size_t err_cap) {
    RollbackSchemaState* candidate;
    int ok = 0;
    if (err && err_cap) err[0] = '\0';
    if (!out_state) {
        rb_set_error(err, err_cap, "expected rollback output state is required");
        return 0;
    }
    candidate = (RollbackSchemaState*)malloc(sizeof(*candidate));
    if (!candidate) {
        rb_set_error(err, err_cap, "out of memory checking rollback compatibility");
        return 0;
    }
    if (!rollback_schema_decode(src, src_size, candidate, err, err_cap) ||
        !rollback_schema_validate_compatibility(candidate, expected_identity,
                                                expected_map_script_id,
                                                err, err_cap)) {
        goto done;
    }
    memcpy(out_state, candidate, sizeof(*candidate));
    ok = 1;
done:
    free(candidate);
    return ok;
}

int rollback_schema_validate_wire_expected(
    const void* src,
    size_t src_size,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    char* err,
    size_t err_cap) {
    RollbackSchemaState* state;
    int ok;
    if (err && err_cap) err[0] = '\0';
    state = (RollbackSchemaState*)malloc(sizeof(*state));
    if (!state) {
        rb_set_error(err, err_cap, "out of memory validating expected rollback state");
        return 0;
    }
    ok = rollback_schema_decode_expected(src, src_size, expected_identity,
                                         expected_map_script_id, state,
                                         err, err_cap);
    free(state);
    return ok;
}
