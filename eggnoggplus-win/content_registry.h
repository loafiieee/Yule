#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTENT_OWNER_MAX        48
#define CONTENT_LOCAL_ID_MAX     48
#define CONTENT_KEY_MAX          (CONTENT_OWNER_MAX + CONTENT_LOCAL_ID_MAX + 1)
#define CONTENT_NAME_MAX         96
#define CONTENT_SHEET_KEY_MAX    128
#define CONTENT_SHA256_SIZE      32
#define CONTENT_SHA256_HEX_SIZE  65
#define CONTENT_TILES_PER_OWNER_MAX 4096
#define CONTENT_TILES_GLOBAL_MAX    65535

typedef enum ContentAnimationMode {
    CONTENT_ANIMATION_LOOP = 0,
    CONTENT_ANIMATION_PING_PONG = 1,
    CONTENT_ANIMATION_ONCE = 2
} ContentAnimationMode;

/* Collision presets deliberately compile to verified vanilla, single-cell
 * behavior glyphs. This keeps native collision/update code authoritative while
 * allowing a map tile's visual symbol to be independent from its behavior. */
typedef enum ContentCollisionMode {
    CONTENT_COLLISION_NATIVE = 0,
    CONTENT_COLLISION_SOLID = 1,
    CONTENT_COLLISION_PASS_THROUGH = 2,
    CONTENT_COLLISION_HAZARD = 3
} ContentCollisionMode;

typedef enum ContentForceMode {
    CONTENT_FORCE_ADD = 0,
    CONTENT_FORCE_SET = 1
} ContentForceMode;

enum {
    CONTENT_FORCE_AXIS_X = 1u << 0,
    CONTENT_FORCE_AXIS_Y = 1u << 1,
    CONTENT_FORCE_VALID_AXES = CONTENT_FORCE_AXIS_X | CONTENT_FORCE_AXIS_Y
};

enum {
    CONTENT_TILE_MIRROR_WITH_ROOM       = 1u << 0,
    CONTENT_TILE_RANDOM_PHASE           = 1u << 1,
    CONTENT_TILE_NATIVE_VISUAL_UNDERLAY = 1u << 2,
    CONTENT_TILE_VALID_FLAGS      = CONTENT_TILE_MIRROR_WITH_ROOM |
                                    CONTENT_TILE_RANDOM_PHASE |
                                    CONTENT_TILE_NATIVE_VISUAL_UNDERLAY
};

/* Declarative, deterministic tile definition. Runtime atlas IDs deliberately
 * do not live here: sprite_sheet + sprite_index are re-resolved after every
 * atlas rebuild. */
typedef struct ContentTileDef {
    char owner[CONTENT_OWNER_MAX];
    char local_id[CONTENT_LOCAL_ID_MAX];
    char key[CONTENT_KEY_MAX];
    char name[CONTENT_NAME_MAX];

    /* A validated, single-cell vanilla glyph supplies native collision/update
     * behavior. Rendering may be replaced for a bound runtime cell; otherwise
     * the native renderer remains authoritative. */
    char native_glyph;
    char sprite_sheet[CONTENT_SHEET_KEY_MAX];
    int32_t sprite_index;
    int32_t frame_count;
    int32_t frame_ticks;
    int32_t animation_mode;
    int32_t layer;
    uint32_t flags;

    /* Deterministic interaction data. native_glyph is the effective glyph
     * emitted to map generation after applying collision_mode. force_axes
     * distinguishes an omitted component from an explicitly authored zero. */
    int32_t collision_mode;
    int32_t force_mode;
    uint32_t force_axes;
    float force_x;
    float force_y;
    float max_speed_x;
    float max_speed_y;

    float offset_x;
    float offset_y;
    /* Callers must provide finite, non-zero scales. Parsers supply 1.0 when
     * their public format omits the fields. */
    float scale_x;
    float scale_y;
    float angle_degrees;
    float tint[4];

    uint8_t asset_sha256[CONTENT_SHA256_SIZE];
    uint8_t definition_sha256[CONTENT_SHA256_SIZE];
    char definition_sha256_hex[CONTENT_SHA256_HEX_SIZE];
} ContentTileDef;

typedef struct ContentTileInput {
    const char* id;
    const char* name;
    char native_glyph;
    const char* sprite_sheet;
    int sprite_index;
    int frame_count;
    int frame_ticks;
    ContentAnimationMode animation_mode;
    int layer;
    uint32_t flags;
    ContentCollisionMode collision_mode;
    ContentForceMode force_mode;
    uint32_t force_axes;
    float force_x;
    float force_y;
    float max_speed_x;
    float max_speed_y;
    float offset_x;
    float offset_y;
    float scale_x;
    float scale_y;
    float angle_degrees;
    float tint[4];
    /* Distinguishes an explicit transparent tint from a zero-initialized input,
     * whose historical/default tint is opaque white. */
    int tint_provided;

    /* External definitions require the loader-computed (and optionally
     * author-pinned) asset digest. Built-in sheets may use a key beginning
     * with "builtin:"; their stable identity is derived from that key. */
    const char* asset_sha256_hex;
} ContentTileInput;

typedef struct ContentRegistryTx ContentRegistryTx;
typedef struct ContentRegistryBatch ContentRegistryBatch;

void content_registry_init(void);
void content_registry_shutdown(void);

/* A transaction is the complete replacement definition set for one owner.
 * Failed validation/commit leaves the live registry untouched. */
ContentRegistryTx* content_registry_begin(const char* owner, char* err, size_t err_cap);
int content_registry_tx_register_tile(ContentRegistryTx* tx,
                                      const ContentTileInput* input,
                                      char* err,
                                      size_t err_cap);
int content_registry_commit(ContentRegistryTx* tx, char* err, size_t err_cap);
void content_registry_abort(ContentRegistryTx* tx);
int content_registry_remove_owner(const char* owner, char* err, size_t err_cap);

/* Atomically commits complete replacement sets for several owners. Ownership
 * of a transaction transfers to the batch only after add succeeds. A failed
 * batch commit changes nothing and leaves the batch available for abort. */
ContentRegistryBatch* content_registry_batch_begin(char* err, size_t err_cap);
int content_registry_batch_add(ContentRegistryBatch* batch,
                               ContentRegistryTx* tx,
                               char* err,
                               size_t err_cap);
int content_registry_batch_commit(ContentRegistryBatch* batch,
                                  char* err,
                                  size_t err_cap);
void content_registry_batch_abort(ContentRegistryBatch* batch);

uint64_t content_registry_generation(void);
size_t content_registry_tile_count(void);
int content_registry_tile_get(size_t index, ContentTileDef* out);
int content_registry_tile_find(const char* qualified_key, ContentTileDef* out);

/* SHA-256 over the sorted deterministic registry. */
int content_registry_fingerprint(uint8_t out[CONTENT_SHA256_SIZE],
                                 char out_hex[CONTENT_SHA256_HEX_SIZE]);

int content_registry_make_key(const char* owner,
                              const char* local_id,
                              char out[CONTENT_KEY_MAX],
                              char* err,
                              size_t err_cap);
int content_registry_tile_native_glyph_allowed(char glyph);

/* Streams a file through BCrypt SHA-256. This is shared by map and Lua asset
 * loaders so the digest in a definition is tied to bytes actually on disk. */
int content_registry_sha256_file(const char* path,
                                 uint8_t out[CONTENT_SHA256_SIZE],
                                 char out_hex[CONTENT_SHA256_HEX_SIZE],
                                 char* err,
                                 size_t err_cap);

/* SHA-256 for an already captured immutable byte buffer. Package scanners use
 * this when validation and identity must refer to the exact same read. */
int content_registry_sha256_bytes(const void* data,
                                  size_t len,
                                  uint8_t out[CONTENT_SHA256_SIZE],
                                  char out_hex[CONTENT_SHA256_HEX_SIZE],
                                  char* err,
                                  size_t err_cap);

#ifdef __cplusplus
}
#endif
