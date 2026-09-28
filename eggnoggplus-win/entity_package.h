#pragma once
#include "entity_world.h"
#define ENTITY_PACKAGE_KEY_MAX 97u
/* Empty sheet means invisible. Sprite transforms never affect collision regions.
 * Sheet is a builtin key or a direct PNG filename from the enclosing map. */
#define ENTITY_VISUAL_SHEET_MAX 128u
#define ENTITY_REGION_NAME_MAX 33u
#define ENTITY_ANIMATION_NAME_MAX 33u
#define ENTITY_ANIMATION_MAX 32u
enum { ENTITY_ANIMATION_LOOP=0, ENTITY_ANIMATION_ONCE=1, ENTITY_ANIMATION_PING_PONG=2 };
typedef struct EntityVisual {
    char sheet[ENTITY_VISUAL_SHEET_MAX];
    uint32_t sprite,frames,frame_ticks,animation_mode;
    int32_t offset_x,offset_y,scale_x,scale_y; /* 1/256 pixel / scale units */
    int32_t rotation; /* 1/256 degrees; presentation only. */
    uint32_t rgba,layer; /* RRGGBBAA; native sprite batch layer 0 or 1 */
} EntityVisual;
typedef struct EntityNamedAnimation {
    char name[ENTITY_ANIMATION_NAME_MAX];
    uint32_t sprite,frames,frame_ticks,animation_mode;
} EntityNamedAnimation;
typedef struct EntityNamedType {
    char key[ENTITY_PACKAGE_KEY_MAX];
    EntityType definition;
    EntityVisual visual;
    uint32_t animation_count;
    EntityNamedAnimation animations[ENTITY_ANIMATION_MAX];
    char region_names[ENTITY_TYPE_REGIONS_MAX][ENTITY_REGION_NAME_MAX];
} EntityNamedType;
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
/* Map layout borrowed during decode only. The source room list preserves the
 * classic mirrored schema. Explicit instances describe a room_graph after its
 * bounds have been normalized to world pixel coordinates. */
#define ENTITY_PACKAGE_SOURCE_ROOM_MAX 9u
#define ENTITY_PACKAGE_ROOM_INSTANCE_MAX 17u
typedef struct EntityPackageRoomInstance {
    const char* id;
    uint32_t source_room;
    int32_t x,y;
    uint32_t width,height;
    uint32_t mirror_x;
} EntityPackageRoomInstance;
typedef struct EntityPackageLayout {
    uint32_t count;
    const char* rooms[ENTITY_PACKAGE_SOURCE_ROOM_MAX];
    uint32_t instance_count;
    EntityPackageRoomInstance instances[ENTITY_PACKAGE_ROOM_INSTANCE_MAX];
    uint32_t start_room;
} EntityPackageLayout;
EntityPackage* entity_package_decode_layout(const char* json,size_t length,
    const EntityPackageLayout* layout,char* error,size_t error_size);

/* Combined host snapshots must agree on the simulation tick before mutation. */
int entity_package_load_for_tick(EntityPackage* package,const void* bytes,size_t size,uint64_t tick);

int entity_package_validate_for_tick(const EntityPackage* package,const void* bytes,size_t size,uint64_t tick);
int entity_package_snapshot_contains_handles(const EntityPackage* package,
    const void* bytes,size_t size,const EntityHandle* handles,size_t handle_count);

/* Detached immutable visual definition for a resolved numeric type. */
int entity_package_visual(const EntityPackage* package,uint32_t type_id,EntityVisual* out);
uint32_t entity_package_resolve_animation(const EntityPackage* package,uint32_t type_id,const char* name,size_t length);
const char* entity_package_animation_name(const EntityPackage* package,uint32_t type_id,uint32_t animation_id);
uint32_t entity_package_animation_count(const EntityPackage* package,uint32_t type_id);
int entity_package_animation_visual(const EntityPackage* package,uint32_t type_id,uint32_t animation_id,EntityVisual* out);
/* Animation resolved from simulation time and authored mode; no render-time mutation. */
uint32_t entity_visual_frame(const EntityVisual* visual,uint64_t tick);
int entity_package_animation_status(const EntityPackage* package,const EntityValue* value,
    const char** name,uint32_t* frame,uint32_t* frames,uint32_t* mode,int* finished);

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
/* Optional authored name for one immutable region; NULL when unnamed/unknown. */
const char* entity_package_region_name(const EntityPackage* package,uint32_t type_id,uint32_t region_id);
