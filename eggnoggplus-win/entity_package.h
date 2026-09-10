#pragma once
#include "entity_world.h"
#define ENTITY_PACKAGE_KEY_MAX 97u
/* Empty sheet means invisible. Sprite transforms never affect collision regions.
 * Sheet is a builtin key or a direct PNG filename from the enclosing map. */
#define ENTITY_VISUAL_SHEET_MAX 128u
typedef struct EntityVisual {
    char sheet[ENTITY_VISUAL_SHEET_MAX];
    uint32_t sprite,frames,frame_ticks;
    int32_t offset_x,offset_y,scale_x,scale_y; /* 1/256 pixel / scale units */
    uint32_t rgba,layer; /* RRGGBBAA; native sprite batch layer 0 or 1 */
} EntityVisual;
typedef struct EntityNamedType { char key[ENTITY_PACKAGE_KEY_MAX]; EntityType definition; EntityVisual visual; } EntityNamedType;
typedef struct EntityPlacement {
    char name[ENTITY_PACKAGE_KEY_MAX];
    char type[ENTITY_PACKAGE_KEY_MAX];
    EntityValue value; /* type_id is assigned from the named catalog */
} EntityPlacement;
typedef struct EntityPackage EntityPackage;
/* All-or-nothing instance construction. Copies and canonicalizes definitions
 * and placements; caller memory may be freed on success. Diagnostic on failure. */
EntityPackage* entity_package_create(const EntityNamedType* types,uint32_t type_count,
    const EntityPlacement* placements,uint32_t placement_count,uint32_t capacity,char* error,size_t error_size);
void entity_package_free(EntityPackage* package);
EntityWorld* entity_package_world(EntityPackage* package);
uint32_t entity_package_resolve_type(void* package,const char* key,size_t length);
EntityHandle entity_package_placement(const EntityPackage* package,const char* name);
const char* entity_package_fingerprint(const EntityPackage* package);
size_t entity_package_snapshot_size(const EntityPackage* package);
int entity_package_save(const EntityPackage* package,void* bytes,size_t size);
int entity_package_load(EntityPackage* package,const void* bytes,size_t size);

/* Isolated, bounded JSON decoder. Schema 1; coordinates are pixel numbers.
 * Input is data only: no Lua libraries or executable content are loaded. */
EntityPackage* entity_package_decode(const char* json,size_t length,char* error,size_t error_size);
/* Native mirrored-source-room layout, center first. Borrowed during decode only. */
typedef struct EntityPackageLayout { uint32_t count; const char* rooms[9]; } EntityPackageLayout;
EntityPackage* entity_package_decode_layout(const char* json,size_t length,
    const EntityPackageLayout* layout,char* error,size_t error_size);

/* Combined host snapshots must agree on the simulation tick before mutation. */
int entity_package_load_for_tick(EntityPackage* package,const void* bytes,size_t size,uint64_t tick);

int entity_package_validate_for_tick(const EntityPackage* package,const void* bytes,size_t size,uint64_t tick);

/* Detached immutable visual definition for a resolved numeric type. */
int entity_package_visual(const EntityPackage* package,uint32_t type_id,EntityVisual* out);
/* Looping animation resolved from simulation time; no render-time mutation. */
uint32_t entity_visual_frame(const EntityVisual* visual,uint64_t tick);

typedef struct EntityRenderView {
    EntityHandle handle;
    EntityVisual visual;
    int32_t x,y; /* World position in 1/256 pixels, before visual offsets. */
    uint32_t sprite; /* Animation frame selected from the world tick. */
} EntityRenderView;
/* Stable slot order, invisible types omitted. Caller starts cursor at zero.
 * Copies only; never invokes Lua, advances state or stores graphics handles. */
int entity_package_render_next(const EntityPackage* package,uint32_t* cursor,EntityRenderView* out);

uint32_t entity_package_type_count(const EntityPackage* package);
const char* entity_package_type_key(const EntityPackage* package,uint32_t type_id);
