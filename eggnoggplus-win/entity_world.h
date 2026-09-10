#pragma once
#include <stddef.h>
#include <stdint.h>

/* Framework-owned entities: no native thing pointers or Lua references.
 * Bound to map-local Lua; native collision response remains an explicit adapter.
 * Coordinates/velocity are signed 1/256-pixel units, per simulation tick. */
#define ENTITY_WORLD_LIMIT 4096u
#define ENTITY_WORLD_COORD_LIMIT INT32_C(1073741823)
typedef uint64_t EntityHandle;
#define ENTITY_FLAG_MIRROR_X UINT32_C(1)
#define ENTITY_FLAG_ANIMATION_PAUSED UINT32_C(2)
#define ENTITY_FLAG_MANUAL_MOTION UINT32_C(4)
#define ENTITY_ANIMATION_TICK_MAX UINT64_C(9007199254740991)
typedef struct EntityValue {
    uint32_t type_id;
    int32_t x, y, vx, vy;
    uint32_t flags;
    uint64_t animation_tick; /* Instance-local simulation clock, exact in Lua. */
} EntityValue;
typedef struct EntityWorld EntityWorld;
EntityWorld* entity_world_create(uint32_t capacity);
void entity_world_free(EntityWorld* world);
uint32_t entity_world_count(const EntityWorld* world);
uint64_t entity_world_tick(const EntityWorld* world);
EntityHandle entity_world_spawn(EntityWorld* world, const EntityValue* value);
int entity_world_remove(EntityWorld* world, EntityHandle handle);
int entity_world_read(const EntityWorld* world, EntityHandle handle, EntityValue* out);
int entity_world_write(EntityWorld* world, EntityHandle handle, const EntityValue* value);
/* Enumerate in stable slot order. cursor begins at zero; no allocation. */
EntityHandle entity_world_next(const EntityWorld* world, uint32_t* cursor);
/* Integrate unconstrained motion. Whole-world preflight rejects overflow without
 * advancing any entity or the clock. Collision resolution is a separate adapter. */
int entity_world_step(EntityWorld* world);
size_t entity_world_snapshot_size(const EntityWorld* world);
int entity_world_save(const EntityWorld* world, void* bytes, size_t size);
/* Exact-capacity, versioned LE decode; every byte is validated before commit.
 * Bound type definitions must match the snapshot byte-for-byte. The containing
 * runtime must additionally validate scripts/assets and full content identity. */
int entity_world_load(EntityWorld* world, const void* bytes, size_t size);

/* Independently authored regions. Masks are bilateral; roles describe purpose,
 * not automatic damage or collision response. Fixed-point local offsets. */
#define ENTITY_TYPE_REGIONS_MAX 16u
enum EntityRegionRole { ENTITY_REGION_BODY=1, ENTITY_REGION_SENSOR=2,
    ENTITY_REGION_HITBOX=3, ENTITY_REGION_HURTBOX=4, ENTITY_REGION_SOLID=5 };
typedef struct EntityRegion {
    uint32_t id, role, layer, mask;
    int32_t x, y, width, height;
} EntityRegion;
/* Effective local X after instance mirroring; uses 64 bits for wide regions. */
int64_t entity_region_local_x(const EntityValue* value,const EntityRegion* region);
typedef struct EntityType {
    uint32_t id, region_count;
    EntityRegion regions[ENTITY_TYPE_REGIONS_MAX];
} EntityType;
/* Copies definitions atomically. Allowed only before any spawn/tick; once bound,
 * types are immutable for this world's lifetime. Unknown types reject spawn/load. */
int entity_world_define_types(EntityWorld* world, const EntityType* types, uint32_t count);
/* Detached immutable definition. Unknown IDs leave output untouched. */
int entity_world_read_type(const EntityWorld* world,uint32_t id,EntityType* out);
typedef struct EntityContact {
    EntityHandle a, b;
    uint32_t region_a, region_b, role_a, role_b;
} EntityContact;
/* Stable entity-slot then region-declaration ordering. Edge-only contact does
 * not overlap. Returns required contact count; writes nothing if capacity is
 * insufficient. SIZE_MAX means invalid arguments, unconfigured definitions,
 * or the one-million comparison budget was exceeded; output remains untouched.
 * This bounded reference query is not yet the production spatial broad phase. */
size_t entity_world_contacts(const EntityWorld* world, EntityContact* out, size_t capacity);

/* Transactional fixed update. Visits the handles alive at entry in slot order.
 * Removed handles are skipped; new/reused slots first receive updates next tick.
 * Callbacks may spawn/remove/write. Returning zero, invalid nested advancement,
 * or integration failure aborts the update and restores every world byte.
 * External callback side effects are NOT rolled back; the managed scripting
 * adapter must include its own state in the enclosing transaction. */
typedef int (*EntityUpdateFn)(EntityWorld* world, EntityHandle entity, void* user);
int entity_world_update(EntityWorld* world, EntityUpdateFn callback, void* user);

/* Read-only preflight, including immutable definitions, slots and canonical bytes. */
int entity_world_validate_snapshot(const EntityWorld* world,const void* bytes,size_t size);

/* As above, consuming a shared execution budget for slot visits and region
 * comparisons. Preflights the write pass too; exhaustion leaves output intact.
 * The budget is transient execution accounting, not serialized world state. */
size_t entity_world_contacts_budgeted(const EntityWorld* world,EntityContact* out,size_t capacity,uint32_t* budget);

/* Swept axis-aligned native body against solid regions. Return contact bits:
 * 1 floor, 2 ceiling, 4 right wall, 8 left wall. Coordinates in pixels. */
unsigned entity_world_sweep_solids(const EntityWorld* world,double old_x,double old_y,double radius,double* x,double* y);
