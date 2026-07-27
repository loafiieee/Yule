#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical rollback wire envelope, version 1.
 *
 * The public structs below are a logical, caller-owned model. Their native
 * sizeof/offsets are deliberately NOT part of the format. rollback_schema.c
 * writes and reads every integer explicitly in little-endian order and never
 * serializes a C pointer, uintptr_t, native padding byte, or function address.
 *
 * Entity payload is a temporary opaque bridge for the still-unclassified
 * native bytes at offsets 0x02..0x157. The known pointer at native +0x158 is
 * represented by behavior_id instead. The envelope itself never writes a C
 * pointer, but the opaque payload cannot yet prove that every bridged byte is
 * pointer-independent or semantically portable. This is not the final typed
 * per-kind entity field model and must not be treated as completing that work.
 */

#define ROLLBACK_SCHEMA_MAGIC UINT32_C(0x31534252) /* "RBS1" */
#define ROLLBACK_SCHEMA_VERSION UINT16_C(1)
#define ROLLBACK_SCHEMA_LAYOUT_ID UINT32_C(0x52530101)

#define ROLLBACK_SCHEMA_ENTITY_CAPACITY 16u
#define ROLLBACK_SCHEMA_ENTITY_PAYLOAD_SIZE 0x156u
#define ROLLBACK_SCHEMA_DANGER_CAPACITY 16u
#define ROLLBACK_SCHEMA_SLOT_NONE UINT8_C(0xff)

#define ROLLBACK_SCHEMA_TILE_CELL_BYTES 4u
#define ROLLBACK_SCHEMA_MAX_TILE_BYTES (4u * 1024u * 1024u)
#define ROLLBACK_SCHEMA_MAX_TILE_CELLS \
    (ROLLBACK_SCHEMA_MAX_TILE_BYTES / ROLLBACK_SCHEMA_TILE_CELL_BYTES)

/* MapScriptSnapshot v5 is embedded as a canonical little-endian subdocument.
 * The current 32-bit x86 Windows producer/test bridge obtains these bytes from
 * its packed, little-endian MapScriptSnapshot layout. That is a transitional
 * implementation detail, not a host-struct serialization contract: a portable
 * native adapter must convert every field to this byte layout explicitly. */
#define ROLLBACK_SCHEMA_MAP_SCRIPT_VERSION UINT16_C(5)
#define ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES 29448u

enum RollbackSchemaRole {
    ROLLBACK_SCHEMA_ROLE_NONE = 0,
    ROLLBACK_SCHEMA_ROLE_P0 = 1,
    ROLLBACK_SCHEMA_ROLE_P1 = 2
};

enum RollbackSchemaEntityKind {
    ROLLBACK_SCHEMA_ENTITY_FREE = 0,
    ROLLBACK_SCHEMA_ENTITY_PLAYER = 1,
    ROLLBACK_SCHEMA_ENTITY_SWORD = 2,
    ROLLBACK_SCHEMA_ENTITY_HAZARD = 3,
    ROLLBACK_SCHEMA_ENTITY_MINE = 4
};

/* Stable local resolver ids. No numeric value is a native code address. */
enum RollbackSchemaBehaviorId {
    ROLLBACK_SCHEMA_BEHAVIOR_NONE = 0,

    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_NOTHING = 0x100,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_PUNCH = 0x101,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_DEAD = 0x102,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_EGGNOGG = 0x103,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_KICK = 0x104,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_DUCK = 0x105,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_THROW = 0x106,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_STAB = 0x107,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_STUN = 0x108,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_STANCE = 0x109,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_RUN = 0x10a,
    ROLLBACK_SCHEMA_BEHAVIOR_PLAYER_JUMP = 0x10b,

    ROLLBACK_SCHEMA_BEHAVIOR_HAZARD_K_UPDATE = 0x200,
    ROLLBACK_SCHEMA_BEHAVIOR_MINE_UPDATE = 0x300
};

typedef struct RollbackSchemaIdentity {
    uint64_t game_build_id;
    uint64_t framework_build_id;
    uint64_t content_id;
    uint64_t map_id;
    uint32_t protocol_version;
} RollbackSchemaIdentity;

typedef struct RollbackSchemaGlobals {
    uint32_t rng_state;
    uint32_t native_game_ticks;
    uint32_t map_seed;
    uint32_t game_level;

    int32_t active_room;
    int32_t old_active_room;
    int32_t start_countdown;
    int32_t end_countdown;
    int32_t map_selector;
    int32_t map_mode;
    int32_t score_target;
    int32_t armed_respawn_limit;
    int32_t score_p0;
    int32_t score_p1;
    int32_t player_mode0;
    int32_t player_mode1;

    uint8_t round_end_any;
    uint8_t game_started;
    uint8_t freeze;
    uint8_t debug_enabled;

    float camera_x;
    float camera_y;
    float camera_shake;
    float camera_shake_decay;

    int32_t roomdef_count;
    int32_t room_width;
    int32_t room_pixel_width;
    int32_t map_width;
    int32_t map_height;
    int32_t tile_width;
    int32_t tile_height;
    int32_t tilemap_pixel_width;
    int32_t tilemap_pixel_height;
} RollbackSchemaGlobals;

typedef struct RollbackSchemaRoles {
    uint8_t p0_slot;
    uint8_t p1_slot;
    uint8_t controller;
    uint8_t leader;
    uint8_t loser;
} RollbackSchemaRoles;

typedef struct RollbackSchemaAllocator {
    uint8_t capacity;
    uint8_t active_count;
    uint8_t cursor;
} RollbackSchemaAllocator;

typedef struct RollbackSchemaEntity {
    uint8_t slot_id;
    uint8_t active;
    /* Inactive slots are normally canonical FREE records. An inactive slot
     * still referenced by the previous-frame danger list is a tombstone and
     * retains its validated kind/behavior/payload until that list is consumed. */
    uint8_t kind;
    uint32_t lifecycle;
    uint32_t behavior_id;
    uint8_t payload[ROLLBACK_SCHEMA_ENTITY_PAYLOAD_SIZE];
} RollbackSchemaEntity;

typedef struct RollbackSchemaDanger {
    uint8_t count;
    uint8_t slots[ROLLBACK_SCHEMA_DANGER_CAPACITY];
} RollbackSchemaDanger;

/* Allocate this model on the heap; the fixed tile capacity is intentionally
 * bounded by the existing four-megabyte native tilemap limit. */
typedef struct RollbackSchemaState {
    RollbackSchemaIdentity identity;
    RollbackSchemaGlobals globals;
    RollbackSchemaRoles roles;
    RollbackSchemaAllocator allocator;
    RollbackSchemaEntity entities[ROLLBACK_SCHEMA_ENTITY_CAPACITY];
    RollbackSchemaDanger danger;

    uint32_t tilemap_width;
    uint32_t tilemap_height;
    uint32_t tile_cell_count;
    uint32_t tile_cells[ROLLBACK_SCHEMA_MAX_TILE_CELLS];

    uint8_t map_script_bytes[ROLLBACK_SCHEMA_MAP_SCRIPT_BYTES];
} RollbackSchemaState;

/* Returns the exact canonical wire length for state. */
int rollback_schema_encoded_size(const RollbackSchemaState* state,
                                 size_t* out_size,
                                 char* err,
                                 size_t err_cap);

/* Validates the logical model without encoding or mutating it. */
int rollback_schema_validate_state(const RollbackSchemaState* state,
                                   char* err,
                                   size_t err_cap);

/* Production preflight must additionally pin the snapshot to the negotiated
 * session identity and exact active map-script/binding-manifest identity. */
int rollback_schema_validate_compatibility(
    const RollbackSchemaState* state,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    char* err,
    size_t err_cap);

/* Encodes one canonical little-endian image and writes its integrity checksum. */
int rollback_schema_encode(const RollbackSchemaState* state,
                           void* dst,
                           size_t dst_cap,
                           size_t* out_size,
                           char* err,
                           size_t err_cap);

/* Decodes only canonical wire images. Validation and canonical re-encoding
 * complete in scratch storage before out_state is modified. */
int rollback_schema_decode(const void* src,
                           size_t src_size,
                           RollbackSchemaState* out_state,
                           char* err,
                           size_t err_cap);

/* Decodes and checks the negotiated identities in scratch storage before
 * modifying out_state. This is the entry point intended for network loads. */
int rollback_schema_decode_expected(
    const void* src,
    size_t src_size,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    RollbackSchemaState* out_state,
    char* err,
    size_t err_cap);

/* Read-only convenience validation for an encoded image. */
int rollback_schema_validate_wire(const void* src,
                                  size_t src_size,
                                  char* err,
                                  size_t err_cap);

int rollback_schema_validate_wire_expected(
    const void* src,
    size_t src_size,
    const RollbackSchemaIdentity* expected_identity,
    uint64_t expected_map_script_id,
    char* err,
    size_t err_cap);

#ifdef __cplusplus
}
#endif
