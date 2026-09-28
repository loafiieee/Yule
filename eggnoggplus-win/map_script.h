#pragma once

#include <stddef.h>
#include <stdint.h>
#include "entity_package.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Map-local Lua is deliberately a small, fixed rollback surface.  These
 * limits are part of the snapshot ABI; changing them requires a version bump. */
#define MAP_SCRIPT_SOURCE_MAX             (256u * 1024u)
#define MAP_SCRIPT_TILE_KEY_MAX           97
#define MAP_SCRIPT_MAX_BINDINGS           256
#define MAP_SCRIPT_MAX_STATE_ENTRIES      64
#define MAP_SCRIPT_STATE_KEY_MAX          32
#define MAP_SCRIPT_STATE_STRING_MAX       64
#define MAP_SCRIPT_MAX_SPRITE_OVERRIDES   256
#define MAP_SCRIPT_MAX_CONTACTS           256
#define MAP_SCRIPT_MAX_MINE_TRIGGERS       32
#define MAP_SCRIPT_MAX_NATIVE_ATTACK_PROBES 64
#define MAP_SCRIPT_MAX_LIFECYCLE_SLOTS    16
#define MAP_SCRIPT_MAX_VELOCITY_LIMITS    18

#define MAP_SCRIPT_DEFAULT_MEMORY_BYTES   (2u * 1024u * 1024u)
#define MAP_SCRIPT_DEFAULT_INSTRUCTIONS   100000u

/* Bump when the source-visible map API or host dispatch contract changes.
 * Online layout negotiation mixes this value into its compatibility key. */
#define MAP_SCRIPT_API_VERSION            UINT32_C(36)

#define MAP_SCRIPT_SENSOR_QUANTIZATION    256
#define MAP_SCRIPT_PLAYER_CONTACT_RADIUS  6.0f
#define MAP_SCRIPT_SWORD_CONTACT_RADIUS   4.0f
#define MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS 6.0f
#define MAP_SCRIPT_HAZARD_CONTACT_RADIUS  0.0f
#define MAP_SCRIPT_RENDER_OFFSET_QUANTIZATION 256
#define MAP_SCRIPT_RENDER_OFFSET_LIMIT    4096

#define MAP_SCRIPT_SNAPSHOT_MAGIC         UINT32_C(0x4d534c53) /* "MSLS" */
#define MAP_SCRIPT_SNAPSHOT_VERSION       9u
#define MAP_SCRIPT_EXIT_LOCK_BYTES        32u

/* Managed object variables deliberately live outside MapScriptSnapshot.  That
 * fixed structure is embedded in every ordinary online rollback state, while
 * managed entities are currently admitted only offline.  Keeping this store in
 * the combined content extension gives object authors substantially more state
 * without inflating supported online packets. */
#define MAP_SCRIPT_OBJECT_STATE_MAGIC     UINT32_C(0x53564d59) /* "YMVS" */
#define MAP_SCRIPT_OBJECT_STATE_VERSION   1u
#define MAP_SCRIPT_MAX_OBJECT_STATE_ENTRIES 512u
#define MAP_SCRIPT_OBJECT_STATE_KEY_MAX   32u

typedef struct MapScriptTileBinding {
    char symbol;
    const char* qualified_key;
} MapScriptTileBinding;

/* Source bytes and binding strings only need to remain alive for the call;
 * activation copies every binding and Lua owns the compiled chunk. */
typedef struct MapScriptDefinition {
    uint64_t script_id;             /* non-zero identity of bytes + ordered bindings */
    const char* chunk_name;          /* diagnostic name, e.g. "@.../map.lua" */
    const char* source;
    size_t source_len;
    const MapScriptTileBinding* bindings;
    size_t binding_count;
    size_t memory_limit_bytes;       /* zero selects the safe default */
    uint32_t instruction_budget;     /* per load/callback; zero = default */
    EntityPackageLayout entity_layout; /* borrowed room names, copied through package expansion */
} MapScriptDefinition;

typedef struct MapScriptObjectView {
    uint32_t object_id;              /* stable host slot/id */
    uint32_t lifecycle_id;           /* distinguishes reuse of that host slot */
    uint8_t object_kind;             /* enum MapScriptObjectKind */
    uint8_t reserved[3];             /* must be zero */
    float x;
    float y;
    float vx;
    float vy;
} MapScriptObjectView;

/* Dispatch writes successful object changes back to object.  A key may be
 * omitted when symbol is supplied; when both are supplied they must identify
 * the same activation binding. */
typedef struct MapScriptContactView {
    MapScriptObjectView* object;
    uint32_t cell_index;
    int32_t tile_x;
    int32_t tile_y;
    char symbol;
    int room_mirrored;
    const char* qualified_key;
} MapScriptContactView;

enum MapScriptObjectKind {
    MAP_SCRIPT_OBJECT_UNKNOWN = 0,
    MAP_SCRIPT_OBJECT_PLAYER = 1,
    MAP_SCRIPT_OBJECT_SWORD = 2,
    MAP_SCRIPT_OBJECT_DEAD_BODY = 3,
    MAP_SCRIPT_OBJECT_HAZARD = 4
};

/* Verified native combat classifications. These are deliberately separate
 * from managed entity type ids: Lua receives them as stable source_type names
 * such as "native:punch" and "native:sword". */
enum MapScriptNativeDamageKind {
    MAP_SCRIPT_NATIVE_DAMAGE_PUNCH = 1,
    MAP_SCRIPT_NATIVE_DAMAGE_KICK = 2,
    MAP_SCRIPT_NATIVE_DAMAGE_SWORD = 3,
    MAP_SCRIPT_NATIVE_DAMAGE_THROWN_SWORD = 4,
    MAP_SCRIPT_NATIVE_DAMAGE_SPIKE_BALL = 5,
    MAP_SCRIPT_NATIVE_DAMAGE_MINE = 6
};

typedef struct MapScriptNativeAttackProbe {
    float x;
    float y;
    float radius;
    uint8_t player_slot; /* zero based; 0xff when the native source has no owner */
    uint8_t kind;        /* enum MapScriptNativeDamageKind */
    uint8_t reserved[2];
} MapScriptNativeAttackProbe;

enum MapScriptContactScope {
    MAP_SCRIPT_CONTACT_SCOPE_CELL = 0,
    MAP_SCRIPT_CONTACT_SCOPE_BINDING = 1
};

/* A host supplies one already-bound map cell from its stable neighborhood
 * enumeration.  The VM validates the native object profile, evaluates the
 * immutable map.sensor geometry, and dispatches through the ordinary contact
 * tracker only when the boxes touch. */
typedef struct MapScriptCellCandidateView {
    MapScriptObjectView* object;
    int object_kind;                 /* must match object->object_kind */
    float contact_radius;            /* player/dead 6, sword 4, hazard point 0 */
    float sensor_x;                  /* immutable pre-callback physics sample */
    float sensor_y;
    uint32_t cell_index;
    int32_t tile_x;
    int32_t tile_y;
    int32_t tile_width;
    int32_t tile_height;
    char symbol;
    int room_mirrored;
    const char* qualified_key;
} MapScriptCellCandidateView;

enum MapScriptCandidateResult {
    MAP_SCRIPT_CANDIDATE_ERROR = 0,
    MAP_SCRIPT_CANDIDATE_NO_SENSOR = 1,
    MAP_SCRIPT_CANDIDATE_OUTSIDE = 2,
    MAP_SCRIPT_CANDIDATE_DISPATCHED = 3
};

typedef void (*MapScriptLogFn)(void* userdata, const char* message);
typedef void (*MapScriptApplyObjectFn)(void* userdata,
                                       const MapScriptObjectView* object);

/* Query one current native player by zero-based slot. Return 1 for a live
 * player, 0 if absent/dead, -1 for an invalid host read. Never mutate native state. */
typedef int (*MapScriptReadPlayerFn)(void* userdata,uint32_t slot,MapScriptObjectView* out);

/* Commit masked player velocities together. Validate every target first; on
 * failure return zero with NO native mutations. Bits 0/1 identify players 1/2. */
typedef int (*MapScriptApplyPlayerVelocitiesFn)(void* userdata,uint32_t mask,const MapScriptObjectView players[2]);

/* Combined final commit: preflight ALL targets before position/velocity, room,
 * or death effects. Room values are zero-based placed-room numbers. Return zero
 * with no effects on failure; slot-order deaths follow movement and room writes. */
typedef int (*MapScriptCommitPlayersFn)(void* userdata,uint32_t velocity_mask,
    uint32_t defeat_mask,uint32_t room_mask,const int8_t rooms[2],
    const MapScriptObjectView players[2]);

enum MapScriptPlayerCommandBits {
    MAP_SCRIPT_COMMAND_ATTACK = 1u << 0,
    MAP_SCRIPT_COMMAND_JUMP = 1u << 1,
    MAP_SCRIPT_COMMAND_RIGHT = 1u << 2,
    MAP_SCRIPT_COMMAND_LEFT = 1u << 3,
    MAP_SCRIPT_COMMAND_UP = 1u << 4,
    MAP_SCRIPT_COMMAND_DOWN = 1u << 5,
    MAP_SCRIPT_COMMAND_MENU = 1u << 6
};
typedef struct MapScriptPlayerObservation {
    uint32_t command_bits;
    uint32_t previous_command_bits;
    uint8_t grounded;
    uint8_t previously_grounded;
    uint8_t native_state;
    uint8_t collision_flags;
    uint8_t previous_collision_flags;
    int8_t room;
    int8_t facing;                   /* -1 left, 0 neutral/unknown, 1 right */
    uint8_t has_sword;
    uint32_t skin_palette;
    uint32_t clothing_palette;
} MapScriptPlayerObservation;
/* Optional deterministic detail for map.players(). Return one on success.
 * Production reads only rollback-owned native player state. */
typedef int (*MapScriptReadPlayerObservationFn)(void* userdata,uint32_t slot,MapScriptPlayerObservation* out);
typedef struct MapScriptMineTrigger {
    double x;
    double y;
} MapScriptMineTrigger;
/* Called only after every scripted callback and fallible native-player commit
 * succeeds. Production ignores positions which do not currently resolve to a
 * native mine tile. */
typedef void (*MapScriptTriggerMinesFn)(void* userdata,const MapScriptMineTrigger* triggers,uint32_t count);

typedef struct MapScriptHost {
    uint32_t rng_seed;               /* zero is canonicalized to a fixed seed */
    MapScriptLogFn log_fn;
    MapScriptApplyObjectFn apply_object_fn; /* used for synthesized leave events */
    void* userdata;
    MapScriptReadPlayerFn read_player_fn;
    MapScriptApplyPlayerVelocitiesFn apply_player_velocities_fn;
    MapScriptCommitPlayersFn commit_players_fn;
    int (*solid_box_fn)(void* userdata,double x,double y,double width,double height);
    int (*tile_at_fn)(void* userdata,double x,double y,char* out_reference,size_t out_size);
    MapScriptReadPlayerObservationFn read_player_observation_fn;
    MapScriptTriggerMinesFn trigger_mines_fn;
} MapScriptHost;

enum {
    MAP_SCRIPT_STATE_NIL = 0,
    MAP_SCRIPT_STATE_BOOL = 1,
    MAP_SCRIPT_STATE_NUMBER = 2,
    MAP_SCRIPT_STATE_STRING = 3
};

/* Public POD layout lets GGPO embed it directly without serializing Lua heap
 * internals.  Reserved bytes must remain zero and are checked on load. */
#pragma pack(push, 1)
typedef struct MapScriptSnapshotStateEntry {
    uint8_t in_use;
    uint8_t type;
    uint8_t key_len;
    uint8_t string_len;
    uint8_t bool_value;
    uint8_t reserved[3];
    double number_value;
    char key[MAP_SCRIPT_STATE_KEY_MAX];
    char string_value[MAP_SCRIPT_STATE_STRING_MAX];
} MapScriptSnapshotStateEntry;

typedef struct MapScriptSnapshotSpriteOverride {
    uint8_t in_use;
    uint8_t reserved[3];
    uint32_t cell_index;
    int32_t sprite_index;
    int32_t offset_x_q;              /* additive pixels, fixed at 1/256 */
    int32_t offset_y_q;
    uint64_t expires_after_tick;
} MapScriptSnapshotSpriteOverride;

typedef struct MapScriptSnapshotContact {
    uint8_t in_use;
    uint8_t object_kind;
    uint16_t binding_index;
    uint32_t object_id;
    uint32_t lifecycle_id;
    uint32_t cell_index;
    int32_t tile_x;
    int32_t tile_y;
    float object_x;
    float object_y;
    float object_vx;
    float object_vy;
    uint8_t room_mirrored;
    uint8_t contact_scope;
    uint8_t reserved[6];
    uint64_t last_seen_tick;
} MapScriptSnapshotContact;

enum MapScriptVelocityLimitFlags {
    MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX = 1u << 0,
    MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX = 1u << 1,
    MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY = 1u << 2,
    MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY = 1u << 3
};

/* One temporary policy follows one exact native object generation.  Bounds
 * whose flag is clear are canonically stored as positive zero. */
typedef struct MapScriptSnapshotVelocityLimit {
    uint8_t in_use;
    uint8_t object_kind;
    uint8_t bound_flags;
    uint8_t reserved;
    uint32_t object_id;
    uint32_t lifecycle_id;
    float min_vx;
    float max_vx;
    float min_vy;
    float max_vy;
    uint64_t expires_after_tick;
} MapScriptSnapshotVelocityLimit;

#define MAP_SCRIPT_MAX_TIMERS 32
#define MAP_SCRIPT_TIMER_MAX_TICKS 1000000000u
typedef struct MapScriptTimerState {
    uint32_t remaining;
    uint32_t interval;
} MapScriptTimerState;

enum MapScriptPlayerPresentationFlags {
    MAP_SCRIPT_PLAYER_PRESENTATION_BODY_VISIBLE = 1u << 0,
    MAP_SCRIPT_PLAYER_PRESENTATION_SKIN_TINT = 1u << 1,
    MAP_SCRIPT_PLAYER_PRESENTATION_CLOTHING_TINT = 1u << 2
};

/* Draw-only player overrides. Tints are packed RRGGBBAA multipliers. A clear
 * flag canonically stores zero in its corresponding value. */
typedef struct MapScriptSnapshotPlayerPresentation {
    uint8_t flags;
    uint8_t body_visible;
    uint8_t reserved[2];
    uint32_t skin_tint;
    uint32_t clothing_tint;
} MapScriptSnapshotPlayerPresentation;

/* Optional map-owned health for native players. This is gameplay state and is
 * therefore part of the deterministic rollback payload. */
typedef struct MapScriptSnapshotPlayerHealth {
    uint8_t enabled;
    uint8_t awaiting_respawn;
    uint8_t invulnerable;
    uint8_t reserved[5];
    double health;
    double max_health;
} MapScriptSnapshotPlayerHealth;

/* Draw-only point camera. Values use 1/256 pixels/zoom; zeroed when inactive.
 * It travels with rollback even though native gameplay camera state is untouched. */
typedef struct MapScriptSnapshotCamera {
    uint8_t active;
    uint8_t reserved[3];
    int32_t x_q;
    int32_t y_q;
    uint16_t zoom_q;
    uint16_t reserved2;
} MapScriptSnapshotCamera;

typedef struct MapScriptSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t checksum;
    uint64_t script_id;
    uint64_t tick;
    uint32_t rng_state;
    uint32_t flags;                  /* bit 0: runtime faulted */
    uint16_t state_count;
    uint16_t override_count;
    uint16_t contact_count;
    uint16_t velocity_limit_count;
    uint8_t player_observation_initialized_mask;
    uint8_t player_observation_present_mask;
    int8_t player_previous_room[2];
    uint8_t player_previous_native_state[2];
    uint8_t reserved[10];
    uint8_t exit_locks[MAP_SCRIPT_EXIT_LOCK_BYTES];
    uint32_t lifecycle_generation[MAP_SCRIPT_MAX_LIFECYCLE_SLOTS];
    MapScriptSnapshotStateEntry state[MAP_SCRIPT_MAX_STATE_ENTRIES];
    MapScriptSnapshotSpriteOverride overrides[MAP_SCRIPT_MAX_SPRITE_OVERRIDES];
    MapScriptSnapshotContact contacts[MAP_SCRIPT_MAX_CONTACTS];
    MapScriptSnapshotVelocityLimit velocity_limits[MAP_SCRIPT_MAX_VELOCITY_LIMITS];
    uint32_t timer_count;
    MapScriptTimerState timers[MAP_SCRIPT_MAX_TIMERS];
    MapScriptSnapshotPlayerPresentation player_presentation[2];
    MapScriptSnapshotPlayerHealth player_health[2];
    MapScriptSnapshotCamera camera;
} MapScriptSnapshot;

typedef struct MapScriptObjectStateEntry {
    uint8_t in_use;
    uint8_t type;
    uint8_t key_len;
    uint8_t string_len;
    uint8_t bool_value;
    uint8_t reserved[3];
    uint64_t handle;
    double number_value;
    char key[MAP_SCRIPT_OBJECT_STATE_KEY_MAX];
    char string_value[MAP_SCRIPT_STATE_STRING_MAX];
} MapScriptObjectStateEntry;

typedef struct MapScriptObjectStateSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t checksum;
    uint16_t count;
    uint8_t reserved[14];
    MapScriptObjectStateEntry entries[MAP_SCRIPT_MAX_OBJECT_STATE_ENTRIES];
} MapScriptObjectStateSnapshot;
#pragma pack(pop)

/* Validation runs the source in a disposable sandbox and verifies callback and
 * binding declarations without replacing the live map runtime. */
int map_script_validate(const MapScriptDefinition* definition,
                        char* err,
                        size_t err_cap);
int map_script_activate(const MapScriptDefinition* definition,
                        const MapScriptHost* host,
                        char* err,
                        size_t err_cap);
/* Experimental host adapter. The caller must use combined content snapshots;
 * legacy fixed map snapshots reject an entity-bearing runtime. Not online admission. */
int map_script_validate_content(const MapScriptDefinition* definition,
    const char* package_json, size_t length, char* err, size_t err_cap);
int map_script_activate_content(const MapScriptDefinition* definition,
    const MapScriptHost* host, const char* package_json, size_t length, char* err, size_t err_cap);
#define MAP_SCRIPT_CONTENT_HEADER_BYTES 48u
int map_script_has_entities(void);
/* Current admission policy; checked before network startup and every network tick. */
int map_script_network_admissible(char* err, size_t err_cap);
size_t map_script_content_snapshot_size(void);
int map_script_content_snapshot_save(void* bytes, size_t size, char* err, size_t err_cap);
int map_script_content_snapshot_validate(const void* bytes, size_t size, char* err, size_t err_cap);
int map_script_content_snapshot_load(const void* bytes, size_t size, char* err, size_t err_cap);
void map_script_deactivate(void);
int map_script_is_active(void);
int map_script_is_faulted(void);
/* Deterministic authored draw camera; returns zero for native behavior. */
int map_script_render_camera(float* x, float* y, float* zoom);
uint64_t map_script_active_id(void);
uint64_t map_script_tick_count(void);
const char* map_script_last_error(void);
/* Read-only native bridge for room-graph traversal. connection_index is the
 * zero-based canonical connection array index; Lua authors see it as 1-based.
 * Lock values live in a dedicated 256-bit ordinary rollback snapshot field. */
int map_script_exit_locked(uint32_t connection_index);

/* Host-managed reusable object pools assign one rollback-tracked generation
 * per fixed slot. Advance exactly once after a successful allocation, then put
 * the current value in MapScriptObjectView.lifecycle_id. A new generation is a
 * distinct contact identity even when object_id and cell_index are unchanged. */
int map_script_object_lifecycle_advance(uint32_t slot);
uint32_t map_script_object_lifecycle_current(uint32_t slot);

/* Send every current contact once, then call dispatch_tick once.  The runtime
 * derives enter/stay/leave from object/lifecycle plus either cell_index or the
 * binding, according to that sensor's contact_scope. dispatch_tick fires
 * missing leaves, named timers, on_tick, advances the deterministic clock, and expires
 * temporary sprite and object-velocity policies. */
int map_script_dispatch_contact(MapScriptContactView* contact,
                                char* err,
                                size_t err_cap);
/* Pure binding metadata query for host dispatch routing.  Invalid, unknown,
 * inactive, and unsensored keys all return zero and never fault the VM. */
int map_script_binding_has_sensor(const char* qualified_key);
/* Returns MapScriptCandidateResult.  NO_SENSOR tells the host to preserve its
 * legacy point/foot sampling for that binding; OUTSIDE is a valid filtered or
 * non-intersecting sensor candidate. */
int map_script_dispatch_cell_candidate(MapScriptCellCandidateView* candidate,
                                       char* err,
                                       size_t err_cap);
/* Refreshes the cached current object used by synthesized leave callbacks and
 * writes any active rollback-owned velocity clamp back to the caller. Call
 * once per live object after native physics and before sending contacts. */
int map_script_update_object(MapScriptObjectView* object,
                             char* err,
                             size_t err_cap);
int map_script_dispatch_tick(char* err, size_t err_cap);
/* Queue one native combat probe while the native simulation is running. Lua
 * callbacks are deferred until dispatch_tick, after the native stack unwinds.
 * Invalid/inactive submissions are ignored and return zero. */
int map_script_submit_native_attack_probe(const MapScriptNativeAttackProbe* probe);

typedef struct MapScriptVisualOverride {
    int sprite_index;
    float offset_x;                  /* additive render pixels */
    float offset_y;
    uint64_t expires_after_tick;
} MapScriptVisualOverride;

typedef struct MapScriptPlayerPresentation {
    int body_visible;
    uint32_t skin_tint;              /* packed RRGGBBAA multiplier */
    uint32_t clothing_tint;
    uint8_t flags;                   /* enum MapScriptPlayerPresentationFlags */
} MapScriptPlayerPresentation;

typedef struct MapScriptPlayerSprite {
    char sheet[128];
    uint32_t sprite;
    int32_t offset_x,offset_y,scale_x,scale_y;
    int32_t rotation;
    uint32_t rgba,layer;
    int visible;
    float x,y;
} MapScriptPlayerSprite;

/* Pure draw-time query. Returns zero for an inactive/faulted runtime, an
 * invalid slot, or a player with no presentation override. */
int map_script_player_presentation(uint32_t slot,
                                   MapScriptPlayerPresentation* out);
/* Draw-only replacement resolved from a validated managed-object visual. */
int map_script_player_sprite(uint32_t slot,MapScriptPlayerSprite* out);

int map_script_visual_override(uint32_t cell_index,
                               MapScriptVisualOverride* out);
/* Compatibility query for callers interested only in the temporary sprite. */
int map_script_sprite_override(uint32_t cell_index,
                               int* out_sprite_index,
                               uint64_t* out_expires_after_tick);

size_t map_script_snapshot_size(void);
int map_script_snapshot_save(MapScriptSnapshot* out,
                             char* err,
                             size_t err_cap);
int map_script_snapshot_validate(const MapScriptSnapshot* snapshot,
                                 uint64_t expected_script_id,
                                 char* err,
                                 size_t err_cap);
int map_script_snapshot_load(const MapScriptSnapshot* snapshot,
                             char* err,
                             size_t err_cap);

#ifdef __cplusplus
}
#endif

/* Read-only entity rendering view; opaque declaration keeps the map API header
 * independent of the package author's native metadata definitions. */
struct EntityRenderView;
int map_script_entity_render_next(uint32_t* cursor,struct EntityRenderView* out);

unsigned map_script_sweep_solids(double old_x,double old_y,double radius,double* x,double* y);
