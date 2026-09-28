#include <windows.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_maps.h"
#include "content_registry.h"
#include "log.h"
#include "map_script.h"
#include "map_ambiance.h"
#include "entity_package.h"
#include "room_graph.h"

_Static_assert(CUSTOM_MAP_MAX_FINAL_ROOMS == ROOM_GRAPH_MAX_NODES,
               "public final-room view must match room graph capacity");
_Static_assert(CUSTOM_MAP_MAX_CONNECTIONS == ROOM_GRAPH_MAX_CONNECTIONS,
               "public connection capacity must match room graph capacity");

#define MAPS_PREFIX "[maps]"

#define VANILLA_MAP_COUNT 5
#define CUSTOM_MAP_MAX_SOURCE_ROOMS 9
#define CUSTOM_MAP_MAX_ID 64
#define CUSTOM_MAP_MAX_NAME 128
#define CUSTOM_MAP_MAX_AUTHOR 128
#define CUSTOM_MAP_MAX_DESCRIPTION 256
#define CUSTOM_MAP_MAX_PARSED_ROOMS 32
#define ROOM_TEMPLATE_W 33
#define ROOM_TEMPLATE_H 12
#define ROOM_TEMPLATE_SIZE (ROOM_TEMPLATE_W * ROOM_TEMPLATE_H)
#define ROOM_VARIABLE_MIN_W 8
#define ROOM_VARIABLE_MAX_W 128
#define ROOM_VARIABLE_MIN_H 6
#define ROOM_VARIABLE_MAX_H 64
#define ROOM_VARIABLE_MAX_SIZE (ROOM_VARIABLE_MAX_W * ROOM_VARIABLE_MAX_H)
#define ROOM_COLOR_SLOT_COUNT 8
#define CUSTOM_MAP_MAX_CONTENT_TILES 64
#define CUSTOM_MAP_MAX_CONTENT_SHEETS 16
#define CUSTOM_MAP_MAX_TEXT_FILE_BYTES (4u * 1024u * 1024u)
#define CUSTOM_MAP_MAX_SCRIPT_BYTES MAP_SCRIPT_SOURCE_MAX
#define CUSTOM_MAP_MAX_ASSET_BYTES (64ull * 1024ull * 1024ull)
#define CUSTOM_MAP_MAX_SHEET_SPRITES 8192
#define NATIVE_TILE_SPRITE_COUNT 128
/* thing_new scans 16 records but deliberately skips slot zero. Two of the
 * remaining 15 records are the native players, leaving at most 13 room-reset
 * allocations. K and the surrounding spawn action dereference allocation
 * without a complete failure path, so map validation reserves that capacity
 * conservatively across every native reset spawner. */
#define NATIVE_THING_POOL_RECORDS 16
#define NATIVE_THING_ALLOCATABLE_SLOTS (NATIVE_THING_POOL_RECORDS - 1)
#define NATIVE_PLAYER_RESERVED_SLOTS 2
#define NATIVE_ROOM_RESET_SPAWN_LIMIT \
    (NATIVE_THING_ALLOCATABLE_SLOTS - NATIVE_PLAYER_RESERVED_SLOTS)

#define ADDR_MAP_SELECTOR            0x55A2F4u
#define ADDR_MAP_MODE                0x55A2F8u
#define ADDR_ROUND_END_ANY           0x55A304u
#define ADDR_SCORE_TARGET            0x55A30Cu
#define ADDR_ARMED_RESPAWN_LIMIT     0x55A310u
#define ADDR_ROOMDEF_COUNT           0x54A360u
#define ADDR_MAP_AUTHOR              0x54A364u
#define ADDR_MAP_NAME                0x54A368u
#define ADDR_MAP_INIT                0x434730u
#define ADDR_MAP_SET_TILE_BASE       0x4349A0u
#define ADDR_MAP_CLEAR_TO            0x434A90u
#define ADDR_MAPGEN_PLOT_ROOM        0x4370E0u
#define ADDR_TILEMAP_DATA            0x54A1E4u
#define ADDR_TILEMAP_WIDTH           0x54A1E8u
#define ADDR_TILEMAP_HEIGHT          0x54A1ECu
#define ADDR_TILEMAP_LAYER           0x54A1E0u
#define ADDR_MAP_VIEW_W              0x54A208u
#define ADDR_MAP_VIEW_H              0x54A20Cu
#define ADDR_ROOM_W                  0x55A3A4u
#define ADDR_MAP_H                   0x55A3A0u
#define ADDR_MAP_W                   0x55A3A8u
#define ADDR_ROOM_PIXEL_W            0x55AB34u
#define ADDR_MAPDEF_START            0x4353C0u
#define ADDR_ROOMDEF                 0x435050u
#define ADDR_ROOMDEFS                0x55A3C0u

typedef void (__cdecl *fn_void_void_t)(void);

typedef enum JsonType {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct JsonMember {
    char* key;
    JsonValue* value;
    struct JsonMember* next;
} JsonMember;

struct JsonValue {
    JsonType type;
    int line;
    int col;
    union {
        int boolean_value;
        double number_value;
        char* string_value;
        struct {
            JsonValue** items;
            int count;
        } array_value;
        JsonMember* object_value;
    } u;
};

typedef struct MapDiagnostics {
    const char* map_id;
    int error_count;
    int warning_count;
} MapDiagnostics;

typedef struct JsonParser {
    const char* cur;
    int line;
    int col;
    MapDiagnostics* diag;
    const char* file_name;
    int failed;
} JsonParser;

typedef struct ColorFields {
    float rgb[ROOM_COLOR_SLOT_COUNT][3];
    unsigned char present[ROOM_COLOR_SLOT_COUNT];
} ColorFields;

typedef struct AppearanceOverride {
    int has_appearance;
    int has_primary_bank;
    int has_mirror_bank;
    ColorFields primary;
    ColorFields mirror;
} AppearanceOverride;

#define CUSTOM_MAP_MAX_SPAWN_MARKERS 128
enum {
    SPAWN_MARKER_DENY = 0,
    SPAWN_MARKER_ALLOW = 1,
    SPAWN_MARKER_ALLOW_P1 = 2,
    SPAWN_MARKER_ALLOW_P2 = 3
};

typedef struct CustomSpawnPoint {
    int set;
    int x;
    int y;
    int facing; /* -1 left, +1 right */
} CustomSpawnPoint;

typedef struct CustomSpawnMarker {
    unsigned char x;
    unsigned char y;
    unsigned char kind;
} CustomSpawnMarker;

typedef struct RoomConfig {
    int opponent_spawn_set;
    int opponent_spawn;
    int ambient_set;
    int ambient;
    int custom_ambiance_set;
    uint16_t custom_ambiance;
    int native_tileset_set;
    char native_tileset_key[CONTENT_SHEET_KEY_MAX];
    int native_tileset_sprite_count;
    AppearanceOverride appearance;
    CustomSpawnPoint player_spawn[2];
    CustomSpawnMarker spawn_markers[CUSTOM_MAP_MAX_SPAWN_MARKERS];
    int spawn_marker_count;
    int spawn_markers_set;
} RoomConfig;

typedef struct ParsedMapRoom {
    char id[CUSTOM_MAP_MAX_ID];
    char glyphs[ROOM_VARIABLE_MAX_SIZE];
    unsigned char content_tile[ROOM_VARIABLE_MAX_SIZE];
    int width;
    int height;
    int row_count;
    int start_line;
} ParsedMapRoom;

typedef struct ParsedMapFile {
    ParsedMapRoom rooms[CUSTOM_MAP_MAX_PARSED_ROOMS];
    int room_count;
} ParsedMapFile;

typedef struct MapContentTile {
    char symbol;
    char native_glyph;
    char key[CONTENT_KEY_MAX];
} MapContentTile;

typedef struct MapContentSheet {
    char key[CONTENT_SHEET_KEY_MAX];
    char relative_path[MAX_PATH];
    char full_path[MAX_PATH];
    char asset_sha256[CONTENT_SHA256_HEX_SIZE];
    int cell_w;
    int cell_h;
    int padding;
    int source_x;
    int source_y;
    int source_w;
    int source_h;
    int sprite_count;
    uint32_t atlas_flags;
} MapContentSheet;

typedef struct ResolvedMapSheet {
    char key[CONTENT_SHEET_KEY_MAX];
    char relative_path[MAX_PATH];
    char full_path[MAX_PATH];
    char asset_sha256[CONTENT_SHA256_HEX_SIZE];
    int cell_w;
    int cell_h;
    int padding;
    int source_x;
    int source_y;
    int source_w;
    int source_h;
    int sprite_count;
    int image_w;
    int image_h;
    int external;
} ResolvedMapSheet;

typedef struct ParsedTileset {
    char owner[CONTENT_OWNER_MAX];
    ContentRegistryTx* transaction;
    MapContentTile tiles[CUSTOM_MAP_MAX_CONTENT_TILES];
    int tile_count;
    MapContentSheet sheets[CUSTOM_MAP_MAX_CONTENT_SHEETS];
    int sheet_count;
    char default_sheet_key[CONTENT_SHEET_KEY_MAX];
    int default_sheet_sprite_count;
    int native_layout;
} ParsedTileset;

typedef struct OptionalMapScriptSource {
    unsigned char* bytes;
    size_t size;
    int present;
    char full_path[MAX_PATH];
    char sha256[CONTENT_SHA256_HEX_SIZE];
} OptionalMapScriptSource;

typedef struct CustomMapRoom {
    char id[CUSTOM_MAP_MAX_ID];
    char glyphs[ROOM_VARIABLE_MAX_SIZE];
    unsigned char content_tile[ROOM_VARIABLE_MAX_SIZE];
    char engine_template[ROOM_TEMPLATE_SIZE];
    int width;
    int height;
    RoomConfig config;
} CustomMapRoom;

typedef struct CustomMapFinalRoom {
    char id[ROOM_GRAPH_ID_CAP];
    int source_room;
    int x;
    int y;
    unsigned char mirror_x;
    unsigned char appearance;
    RoomConfig overrides;
} CustomMapFinalRoom;

typedef struct CustomMapConnection {
    unsigned char from_room;
    unsigned char from_side;
    unsigned char from_offset;
    unsigned char to_room;
    unsigned char to_side;
    unsigned char to_offset;
    unsigned char span;
    unsigned char one_way;
    unsigned char player_policy;
    unsigned char focus_policy;
} CustomMapConnection;

typedef struct CustomMap {
    char folder_id[MAX_PATH];
    char id[CUSTOM_MAP_MAX_ID];
    char online_key[112];
    char online_sig[33];
    char name[CUSTOM_MAP_MAX_NAME];
    char author[CUSTOM_MAP_MAX_AUTHOR];
    char description[CUSTOM_MAP_MAX_DESCRIPTION];
    int sort_order;
    int mode;
    int round_end_any;
    int score_target;
    int armed_respawn_limit;
    int has_eggnogg_color;
    float eggnogg_color[3];
    int format_version;
    int variable_rooms;
    int room_graph;
    RoomConfig defaults_room;
    int source_room_count;
    CustomMapRoom rooms[CUSTOM_MAP_MAX_SOURCE_ROOMS];
    int final_room_count;
    int layout_bounds_x;
    int layout_bounds_y;
    int layout_width;
    int layout_height;
    CustomMapFinalRoom final_rooms[CUSTOM_MAP_ENGINE_MAX_FINAL_ROOMS];
    int graph_start_room;
    int connection_count;
    CustomMapConnection connections[ROOM_GRAPH_MAX_CONNECTIONS];
    char content_owner[CONTENT_OWNER_MAX];
    ContentRegistryTx* content_transaction;
    MapContentTile content_tiles[CUSTOM_MAP_MAX_CONTENT_TILES];
    int content_tile_count;
    MapContentSheet content_sheets[CUSTOM_MAP_MAX_CONTENT_SHEETS];
    int content_sheet_count;
    char default_sheet_key[CONTENT_SHEET_KEY_MAX];
    int default_sheet_sprite_count;
    int native_layout;
    MapAmbianceCatalog ambiance_catalog;
    int max_native_room_spawns;
    int native_k_marker_count;
    char script_full_path[MAX_PATH];
    char script_sha256[CONTENT_SHA256_HEX_SIZE];
    unsigned char* script_source;
    unsigned char* entity_source;
    size_t entity_size;
    char entity_sha256[CONTENT_SHA256_HEX_SIZE];
    size_t script_size;
    uint64_t script_id;
    int is_preview;
    uint64_t preview_revision;
} CustomMap;

static int custom_map_final_room_count(const CustomMap* map) {
    return map ? map->final_room_count : 0;
}

static int custom_map_final_source_room(const CustomMap* map, int final_room) {
    if (!map || final_room < 0 || final_room >= map->final_room_count)
        return -1;
    return map->final_rooms[final_room].source_room;
}

static int custom_map_final_mirrored(const CustomMap* map, int final_room) {
    if (!map || final_room < 0 || final_room >= map->final_room_count)
        return 0;
    return map->final_rooms[final_room].mirror_x;
}

static int custom_map_final_appearance_mirrored(const CustomMap* map,
                                                int final_room) {
    if (!map || final_room < 0 || final_room >= map->final_room_count)
        return 0;
    return map->final_rooms[final_room].appearance ==
        ROOM_GRAPH_APPEARANCE_MIRROR;
}

static int custom_map_store_resolved_graph(CustomMap* map,
                                           const RoomGraph* graph,
                                           const RoomGraphValidation* validation) {
    int index;
    if (!map || !graph || !validation || graph->node_count < 1 ||
        graph->node_count > CUSTOM_MAP_ENGINE_MAX_FINAL_ROOMS || graph->connection_count < 0 ||
        graph->connection_count > ROOM_GRAPH_MAX_CONNECTIONS ||
        graph->start_node < 0 || graph->start_node >= graph->node_count)
        return 0;
    for (index = 0; index < graph->connection_count; ++index) {
        const RoomGraphConnection* source = &graph->connections[index];
        if (source->from_node < 0 || source->from_node > UCHAR_MAX ||
            source->to_node < 0 || source->to_node > UCHAR_MAX ||
            source->from_side < 0 || source->from_side > UCHAR_MAX ||
            source->to_side < 0 || source->to_side > UCHAR_MAX ||
            source->from_offset < 0 || source->from_offset > UCHAR_MAX ||
            source->to_offset < 0 || source->to_offset > UCHAR_MAX ||
            source->span < 1 || source->span > UCHAR_MAX ||
            source->one_way < 0 || source->one_way > 1 ||
            source->player_policy < ROOM_GRAPH_PLAYERS_BOTH ||
            source->player_policy > ROOM_GRAPH_PLAYERS_GO ||
            source->focus_policy < ROOM_GRAPH_FOCUS_GO ||
            source->focus_policy > ROOM_GRAPH_FOCUS_CROSSING) return 0;
    }
    map->final_room_count = graph->node_count;
    map->layout_bounds_x = validation->bounds_x;
    map->layout_bounds_y = validation->bounds_y;
    map->layout_width = validation->bounds_width;
    map->layout_height = validation->bounds_height;
    map->graph_start_room = graph->start_node;
    map->connection_count = graph->connection_count;
    for (index = 0; index < graph->node_count; ++index) {
        snprintf(map->final_rooms[index].id,
                 sizeof(map->final_rooms[index].id), "%s",
                 graph->nodes[index].id);
        map->final_rooms[index].source_room = graph->nodes[index].source_room;
        map->final_rooms[index].x = graph->nodes[index].x;
        map->final_rooms[index].y = graph->nodes[index].y;
        map->final_rooms[index].mirror_x =
            (unsigned char)graph->nodes[index].mirror_x;
        map->final_rooms[index].appearance =
            (unsigned char)graph->nodes[index].appearance;
    }
    for (index = 0; index < graph->connection_count; ++index) {
        const RoomGraphConnection* source = &graph->connections[index];
        CustomMapConnection* output = &map->connections[index];
        output->from_room = (unsigned char)source->from_node;
        output->from_side = (unsigned char)source->from_side;
        output->from_offset = (unsigned char)source->from_offset;
        output->to_room = (unsigned char)source->to_node;
        output->to_side = (unsigned char)source->to_side;
        output->to_offset = (unsigned char)source->to_offset;
        output->span = (unsigned char)source->span;
        output->one_way = (unsigned char)source->one_way;
        output->player_policy = (unsigned char)source->player_policy;
        output->focus_policy = (unsigned char)source->focus_policy;
    }
    return 1;
}

static void custom_map_entity_layout(const CustomMap* map,
                                     EntityPackageLayout* layout) {
    int room;
    if (!layout) return;
    memset(layout, 0, sizeof(*layout));
    if (!map) return;
    layout->count = (uint32_t)map->source_room_count;
    for (room = 0; room < map->source_room_count; ++room)
        layout->rooms[room] = map->rooms[room].id;
    if (!map->room_graph) return;
    layout->start_room = (uint32_t)map->graph_start_room;
    layout->instance_count = (uint32_t)map->final_room_count;
    for (room = 0; room < map->final_room_count; ++room) {
        const CustomMapFinalRoom* final_room = &map->final_rooms[room];
        const CustomMapRoom* source = &map->rooms[final_room->source_room];
        EntityPackageRoomInstance* instance = &layout->instances[room];
        instance->id = final_room->id;
        instance->source_room = (uint32_t)final_room->source_room;
        instance->x = (final_room->x - map->layout_bounds_x) * 16;
        instance->y = (final_room->y - map->layout_bounds_y) * 16;
        instance->width = (uint32_t)source->width * 16u;
        instance->height = (uint32_t)source->height * 16u;
        instance->mirror_x = final_room->mirror_x;
    }
}

typedef struct CustomMapRegistry {
    CustomMap* maps;
    int count;
    int cap;
    int rejected_count;
} CustomMapRegistry;

typedef struct RetiredRegistryBuffer {
    CustomMap* maps;
    int count;
    struct RetiredRegistryBuffer* next;
} RetiredRegistryBuffer;

typedef struct EngineRoomdef {
    const char* template_ptr;
    int ambient;
    int unknown_08;
    int unknown_0c;
    float primary[ROOM_COLOR_SLOT_COUNT][3];
    float mirror[ROOM_COLOR_SLOT_COUNT][3];
    void (*callback)(void);
} EngineRoomdef;

static int g_custom_maps_inited = 0;
static CustomMapRegistry g_custom_registry = { 0 };
static CustomMap g_preview_map;
static int g_preview_active = 0;
static char g_preview_folder[MAX_PATH];
static uint64_t g_preview_revision = 0;
static RetiredRegistryBuffer* g_retired_registry_buffers = NULL;
/* Native roomdefs retain pointers into the registry that most recently
 * supplied a custom map (name/author and room glyph rows). A hot reload may
 * replace that registry while the map is still live, so exactly that buffer
 * remains pinned until another custom or vanilla definition is installed. */
static CustomMap* g_engine_pinned_registry_maps = NULL;
static const CustomMap* g_engine_pinned_map = NULL;
static int g_engine_pinned_selector = -1;
static uint64_t g_engine_pinned_generation = 0;
static uint64_t g_custom_maps_signature = 0;
static uint64_t g_custom_maps_failed_signature = 0;
static uint64_t g_custom_maps_generation = 1;

static volatile int* g_map_selector = (volatile int*)(uintptr_t)ADDR_MAP_SELECTOR;
static volatile int* g_map_mode = (volatile int*)(uintptr_t)ADDR_MAP_MODE;
static volatile int* g_round_end_any = (volatile int*)(uintptr_t)ADDR_ROUND_END_ANY;
static volatile int* g_score_target = (volatile int*)(uintptr_t)ADDR_SCORE_TARGET;
static volatile int* g_armed_respawn_limit = (volatile int*)(uintptr_t)ADDR_ARMED_RESPAWN_LIMIT;
static volatile int* g_roomdef_count = (volatile int*)(uintptr_t)ADDR_ROOMDEF_COUNT;
static const char** g_map_author = (const char**)(uintptr_t)ADDR_MAP_AUTHOR;
static const char** g_map_name = (const char**)(uintptr_t)ADDR_MAP_NAME;
static EngineRoomdef* g_roomdefs = (EngineRoomdef*)(uintptr_t)ADDR_ROOMDEFS;

static fn_void_void_t p_mapdef_start = (fn_void_void_t)(uintptr_t)ADDR_MAPDEF_START;
typedef int (__cdecl *fn_map_init_t)(int, int);
typedef void (__cdecl *fn_map_set_tile_base_t)(int, int, int);
typedef void (__cdecl *fn_map_clear_to_t)(unsigned char);
static fn_map_init_t p_map_init = (fn_map_init_t)(uintptr_t)ADDR_MAP_INIT;
static fn_map_set_tile_base_t p_map_set_tile_base = (fn_map_set_tile_base_t)(uintptr_t)ADDR_MAP_SET_TILE_BASE;
static fn_map_clear_to_t p_map_clear_to = (fn_map_clear_to_t)(uintptr_t)ADDR_MAP_CLEAR_TO;

static const char* color_slot_names[ROOM_COLOR_SLOT_COUNT] = {
    "fg1",
    "fg2",
    "bg1",
    "bg2",
    "water",
    "water_hi",
    "special",
    "special2",
};

static int path_join(char* dst, size_t dst_size, const char* a, const char* b);

static void diag_log(MapDiagnostics* diag, int is_error, const char* fmt, ...) {
    char message[1024];
    char full[1200];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    snprintf(full, sizeof(full), "%s[%s] %s", MAPS_PREFIX, diag->map_id, message);
    if (is_error) {
        diag->error_count++;
        LOG_ERROR("%s", full);
    } else {
        diag->warning_count++;
        LOG_WARN("%s", full);
    }
}

static void map_info(const char* map_id, const char* fmt, ...) {
    char message[1024];
    char full[1200];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    snprintf(full, sizeof(full), "%s[%s] %s", MAPS_PREFIX, map_id, message);
    LOG_INFO("%s", full);
}

static void copy_truncated(MapDiagnostics* diag, char* dst, size_t dst_size, const char* src, const char* path) {
    size_t src_len;

    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    src_len = strlen(src);
    if (src_len >= dst_size) {
        diag_log(diag, 0, "[data.json][%s] warning: value too long; truncating to %u bytes",
                 path, (unsigned)(dst_size - 1));
        src_len = dst_size - 1;
    }
    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static int positive_mod(int value, int mod) {
    int result = value % mod;
    if (result < 0) result += mod;
    return result;
}

static uint64_t hash_bytes64(uint64_t h, const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint32_t weak_map_hash32(const char* a, const char* b) {
    const uint64_t modp = 4294967291ull;
    uint64_t h = 2166136261ull;
    const unsigned char* p;
    for (p = (const unsigned char*)(a ? a : ""); *p; p++) {
        h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
    }
    {
        static const char sep[] = "\n--MAP--\n";
        for (p = (const unsigned char*)sep; *p; p++) {
            h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
        }
    }
    for (p = (const unsigned char*)(b ? b : ""); *p; p++) {
        h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
    }
    return (uint32_t)h;
}

/* A public map key is a compact admission identity rather than a display hash.
 * Bind exact map text and every separately loaded runtime file. The full YMC3
 * identity remains the second, untruncated state-transfer preflight. */
static int map_package_signature(const char* json_text,
                                 const char* map_text,
                                 const CustomMap* map,
                                 char out[33]) {
    static const char domain[] = "eggnoggplus/online-map-package/v2";
    size_t json_len;
    size_t map_len;
    size_t bytes;
    size_t position = 0;
    unsigned char* canonical;
    char digest[CONTENT_SHA256_HEX_SIZE];
    int i;
    if (!json_text || !map_text || !map || !out) return 0;
    json_len = strlen(json_text);
    map_len = strlen(map_text);
    if (json_len > SIZE_MAX - sizeof(domain) - 2u ||
        map_len > SIZE_MAX - sizeof(domain) - 2u - json_len) return 0;
    bytes = sizeof(domain) + json_len + map_len + 2u;
    for (i = 0; i < map->content_sheet_count; ++i) {
        size_t path_len = strlen(map->content_sheets[i].relative_path);
        if (bytes > SIZE_MAX - path_len - CONTENT_SHA256_HEX_SIZE - 1u) return 0;
        bytes += path_len + CONTENT_SHA256_HEX_SIZE + 1u;
    }
    if (map->script_sha256[0]) {
        if (bytes > SIZE_MAX - CONTENT_SHA256_HEX_SIZE) return 0;
        bytes += CONTENT_SHA256_HEX_SIZE;
    }
    if (map->entity_sha256[0]) {
        if (bytes > SIZE_MAX - CONTENT_SHA256_HEX_SIZE) return 0;
        bytes += CONTENT_SHA256_HEX_SIZE;
    }
    canonical = (unsigned char*)malloc(bytes);
    if (!canonical) return 0;
#define APPEND_SIGNATURE_BYTES(source, length) do { \
        size_t append_length = (length); \
        memcpy(canonical + position, (source), append_length); \
        position += append_length; \
    } while (0)
    APPEND_SIGNATURE_BYTES(domain, sizeof(domain));
    APPEND_SIGNATURE_BYTES(json_text, json_len + 1u);
    APPEND_SIGNATURE_BYTES(map_text, map_len + 1u);
    for (i = 0; i < map->content_sheet_count; ++i) {
        const MapContentSheet* sheet = &map->content_sheets[i];
        APPEND_SIGNATURE_BYTES(sheet->relative_path, strlen(sheet->relative_path) + 1u);
        APPEND_SIGNATURE_BYTES(sheet->asset_sha256, CONTENT_SHA256_HEX_SIZE);
    }
    if (map->script_sha256[0])
        APPEND_SIGNATURE_BYTES(map->script_sha256, CONTENT_SHA256_HEX_SIZE);
    if (map->entity_sha256[0])
        APPEND_SIGNATURE_BYTES(map->entity_sha256, CONTENT_SHA256_HEX_SIZE);
#undef APPEND_SIGNATURE_BYTES
    if (position > bytes ||
        !content_registry_sha256_bytes(canonical, position, NULL, digest, NULL, 0)) {
        free(canonical);
        return 0;
    }
    free(canonical);
    memcpy(out, digest, 32u);
    out[32] = '\0';
    return 1;
}

static void copy_lower_ascii(char* dst, size_t dst_sz, const char* src) {
    size_t i;
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    for (i = 0; i + 1 < dst_sz && src[i]; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        dst[i] = (char)ch;
    }
    dst[i] = '\0';
}

static uint64_t hash_path_ci64(uint64_t h, const char* s) {
    while (s && *s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        h ^= (uint64_t)ch;
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t hash_signature_entry(const char* rel_path, uint32_t attrs, uint64_t write_time, uint64_t size) {
    uint64_t h = 1469598103934665603ull;
    h = hash_path_ci64(h, rel_path);
    h = hash_bytes64(h, &attrs, sizeof(attrs));
    h = hash_bytes64(h, &write_time, sizeof(write_time));
    h = hash_bytes64(h, &size, sizeof(size));
    return h;
}

static void signature_add(uint64_t* xor_accum, uint64_t* add_accum, uint64_t* count, uint64_t value) {
    *xor_accum ^= value;
    *add_accum += value;
    (*count)++;
}

static uint64_t filetime_to_u64(const FILETIME* ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft->dwLowDateTime;
    value.HighPart = ft->dwHighDateTime;
    return (uint64_t)value.QuadPart;
}

static void signature_add_map_folder_files(uint64_t* xor_accum,
                                           uint64_t* add_accum,
                                           uint64_t* count,
                                           const char* folder_name) {
    char folder_path[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA file_data;
    HANDLE find_handle;
    if (!path_join(folder_path, sizeof(folder_path), "maps", folder_name) ||
        !path_join(pattern, sizeof(pattern), folder_path, "*")) {
        signature_add(xor_accum, add_accum, count,
                      hash_signature_entry("maps_folder_path_too_long", 0, 0, 0));
        return;
    }
    find_handle = FindFirstFileA(pattern, &file_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        signature_add(xor_accum, add_accum, count,
                      hash_signature_entry("maps_folder_unreadable", 0, 0, 0));
        return;
    }
    do {
        char relative_path[MAX_PATH];
        uint64_t entry_hash;
        if (file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        int written = snprintf(relative_path, sizeof(relative_path), "%s\\%s",
                               folder_name, file_data.cFileName);
        if (written < 0 || (size_t)written >= sizeof(relative_path)) {
            signature_add(xor_accum, add_accum, count,
                          hash_signature_entry("map_file_path_too_long", 0, 0, 0));
            continue;
        }
        entry_hash = hash_signature_entry(
            relative_path,
            file_data.dwFileAttributes,
            filetime_to_u64(&file_data.ftLastWriteTime),
            ((uint64_t)file_data.nFileSizeHigh << 32) |
                (uint64_t)file_data.nFileSizeLow);
        {
            const char* extension = strrchr(file_data.cFileName, '.');
            int is_script = _stricmp(file_data.cFileName, "map.lua") == 0;
            int is_entities = _stricmp(file_data.cFileName, "entities.json") == 0;
            int is_png = extension && _stricmp(extension, ".png") == 0;
            char full_path[MAX_PATH];
            char digest[CONTENT_SHA256_HEX_SIZE];
            char err[128];
            uint64_t max_size = is_script
                ? (uint64_t)CUSTOM_MAP_MAX_SCRIPT_BYTES
                : is_entities ? UINT64_C(1048576) : CUSTOM_MAP_MAX_ASSET_BYTES;
            static const char script_unreadable_marker[] =
                "map.lua:unreadable-or-unsafe";
            static const char png_unreadable_marker[] =
                "png:unreadable-or-unsafe";
            if (is_script || is_entities || is_png) {
                const char* unreadable_marker = is_script
                    ? script_unreadable_marker
                    : png_unreadable_marker;
                if (!(file_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                    ((((uint64_t)file_data.nFileSizeHigh << 32) |
                      (uint64_t)file_data.nFileSizeLow) <= max_size) &&
                    path_join(full_path, sizeof(full_path), folder_path,
                              file_data.cFileName) &&
                    content_registry_sha256_file(full_path, NULL, digest,
                                                 err, sizeof(err))) {
                    entry_hash = hash_bytes64(entry_hash, digest, strlen(digest));
                } else {
                    entry_hash = hash_bytes64(entry_hash, unreadable_marker,
                                               strlen(unreadable_marker));
                }
            }
        }
        signature_add(xor_accum, add_accum, count, entry_hash);
    } while (FindNextFileA(find_handle, &file_data));
    FindClose(find_handle);
}

/* Shared traversal keeps discovery and reload detection in agreement. A package
 * owns its descendants; ZIP wrapper directories are traversed, never registered.
 * Depth counts directories below maps/. Reparse directories are never followed. */
#define CUSTOM_MAP_SCAN_DEPTH 8u
#define CUSTOM_MAP_SCAN_DIRECTORIES 4096u
typedef void (*MapFolderVisitor)(void*,const WIN32_FIND_DATAA*,const char*,int);
typedef struct MapFolderWalk {
    MapFolderVisitor visit;void* user;unsigned visited;int limited;
} MapFolderWalk;
static void walk_map_folders(MapFolderWalk* walk,const char* parent,unsigned depth) {
    char folder[MAX_PATH],pattern[MAX_PATH];WIN32_FIND_DATAA fd;HANDLE find;
    if (!path_join(folder,sizeof(folder),"maps",parent) || !path_join(pattern,sizeof(pattern),folder,"*")) {walk->limited=1;return;}
    find=FindFirstFileA(pattern,&fd);if(find==INVALID_HANDLE_VALUE) return;
    do {
        char relative[MAX_PATH],path[MAX_PATH],marker[MAX_PATH];DWORD json_attrs,map_attrs;int candidate,package;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ||
            !strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..") || fd.cFileName[0]=='_') continue;
        if(depth>=CUSTOM_MAP_SCAN_DEPTH || walk->visited>=CUSTOM_MAP_SCAN_DIRECTORIES) {walk->limited=1;break;}
        if(parent[0]) {
            if(!path_join(relative,sizeof(relative),parent,fd.cFileName)) {walk->limited=1;continue;}
        } else snprintf(relative,sizeof(relative),"%s",fd.cFileName);
        if(!path_join(path,sizeof(path),"maps",relative)) {walk->limited=1;continue;}
        walk->visited++;
        json_attrs=path_join(marker,sizeof(marker),path,"data.json") ? GetFileAttributesA(marker) : INVALID_FILE_ATTRIBUTES;
        map_attrs=path_join(marker,sizeof(marker),path,"data.map") ? GetFileAttributesA(marker) : INVALID_FILE_ATTRIBUTES;
        candidate=json_attrs!=INVALID_FILE_ATTRIBUTES || map_attrs!=INVALID_FILE_ATTRIBUTES;
        package=json_attrs!=INVALID_FILE_ATTRIBUTES && map_attrs!=INVALID_FILE_ATTRIBUTES;
        walk->visit(walk->user,&fd,relative,candidate);
        if(!package) walk_map_folders(walk,relative,depth+1);
    } while(FindNextFileA(find,&fd));
    FindClose(find);
}
static void visit_map_folders(MapFolderWalk* walk) {
    DWORD attrs=GetFileAttributesA("maps");
    if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) return;
    walk_map_folders(walk,"",0);
}
typedef struct MapSignatureAccum {uint64_t xor_accum,add_accum,count;} MapSignatureAccum;
static void signature_visit_folder(void* user,const WIN32_FIND_DATAA* fd,const char* relative,int candidate) {
    MapSignatureAccum* a=user;(void)candidate;
    signature_add(&a->xor_accum,&a->add_accum,&a->count,
        hash_signature_entry(relative,fd->dwFileAttributes,filetime_to_u64(&fd->ftLastWriteTime),0));
    signature_add_map_folder_files(&a->xor_accum,&a->add_accum,&a->count,relative);
}
static uint64_t custom_maps_compute_signature(void) {
    MapSignatureAccum accum={0};MapFolderWalk walk={signature_visit_folder,&accum,0,0};
    uint64_t final_hash=1469598103934665603ull;const uint64_t version=4ull;
    visit_map_folders(&walk);
    final_hash=hash_bytes64(final_hash,&version,sizeof(version));
    final_hash=hash_bytes64(final_hash,&accum.xor_accum,sizeof(accum.xor_accum));
    final_hash=hash_bytes64(final_hash,&accum.add_accum,sizeof(accum.add_accum));
    final_hash=hash_bytes64(final_hash,&accum.count,sizeof(accum.count));
    final_hash=hash_bytes64(final_hash,&walk.limited,sizeof(walk.limited));
    return final_hash;
}

static int glyph_allowed(char ch) {
    switch (ch) {
        case ' ':
        case '!':
        case '#':
        case '(':
        case ')':
        case '*':
        case '+':
        case '-':
        case '.':
        case '1':
        case '2':
        case ':':
        case '=':
        case '?':
        case '@':
        case 'A':
        case 'C':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'K':
        case 'L':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'S':
        case 'T':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
        case '^':
        case '_':
        case '`':
        case 'c':
        case 'e':
        case 'f':
        case 'i':
        case 'l':
        case 'm':
        case 'q':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case '|':
        case '~':
            return 1;
        default:
            return 0;
    }
}

static int read_text_file(const char* path, char** out_text) {
    FILE* f;
    long size;
    char* text;
    size_t read_count;

    DWORD attributes;
    if (!out_text) return 0;
    *out_text = NULL;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        return 0;
    }
    f = fopen(path, "rb");
    if (!f) return 0;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0 || (unsigned long)size > CUSTOM_MAP_MAX_TEXT_FILE_BYTES) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(f);
        return 0;
    }

    read_count = fread(text, 1, (size_t)size, f);
    if (read_count != (size_t)size || ferror(f)) {
        fclose(f);
        free(text);
        return 0;
    }
    fclose(f);

    text[read_count] = '\0';
    *out_text = text;
    return 1;
}

static void optional_map_script_source_dispose(OptionalMapScriptSource* source) {
    if (!source) return;
    free(source->bytes);
    memset(source, 0, sizeof(*source));
}

/* map.lua is deliberately a single, direct package file. Opening the reparse
 * point itself and denying write/delete sharing keeps the validated bytes,
 * digest, and stored path tied to one regular file for the whole read. */
static int read_optional_package_source(MapDiagnostics* diag,
                                           const char* folder_path,
                                           int format_version,
                                           const char* filename, size_t maximum_bytes,
                                           OptionalMapScriptSource* out) {
    char candidate[MAX_PATH];
    char folder_full[MAX_PATH];
    char expected_full[MAX_PATH];
    char script_full[MAX_PATH];
    DWORD attrs;
    DWORD error_code;
    DWORD folder_length;
    DWORD script_length;
    HANDLE folder = INVALID_HANDLE_VALUE;
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION folder_info;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD expected_size;
    DWORD bytes_read = 0;
    DWORD extra_read = 0;
    unsigned char extra_byte = 0;
    size_t length;
    int ok = 0;

    if (!diag || !folder_path || !out) return 0;
    memset(out, 0, sizeof(*out));
    if (!path_join(candidate, sizeof(candidate), folder_path, filename)) {
        diag_log(diag, 1, "[%s] error: script path is too long", filename);
        return 0;
    }

    folder_length = GetFullPathNameA(folder_path, (DWORD)sizeof(folder_full),
                                     folder_full, NULL);
    script_length = GetFullPathNameA(candidate, (DWORD)sizeof(script_full),
                                     script_full, NULL);
    if (folder_length == 0 || folder_length >= (DWORD)sizeof(folder_full) ||
        script_length == 0 || script_length >= (DWORD)sizeof(script_full)) {
        diag_log(diag, 1, "[%s] error: cannot form a bounded absolute package path", filename);
        return 0;
    }
    length = strlen(folder_full);
    while (length > 0 &&
           (folder_full[length - 1] == '\\' || folder_full[length - 1] == '/') &&
           !(length == 3 && folder_full[1] == ':')) {
        folder_full[--length] = '\0';
    }
    if (!path_join(expected_full, sizeof(expected_full), folder_full, filename) ||
        _stricmp(expected_full, script_full) != 0) {
        diag_log(diag, 1, "[%s] error: script must resolve directly inside its map folder", filename);
        return 0;
    }

    attrs = GetFileAttributesA(script_full);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        error_code = GetLastError();
        if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND) {
            return 1;
        }
        diag_log(diag, 1, "[%s] error: cannot inspect optional script (Windows error %lu)", filename,
                 (unsigned long)error_code);
        return 0;
    }
    out->present = 1;
    if (format_version != 2) {
        diag_log(diag, 1, "[%s] error: scripts require eggnogg-map/v2", filename);
        return 0;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        diag_log(diag, 1, "[%s] error: script must be a direct, regular, non-reparse file", filename);
        return 0;
    }

    folder = CreateFileA(folder_full, FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                         NULL, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS |
                             FILE_FLAG_OPEN_REPARSE_POINT,
                         NULL);
    if (folder == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(folder, &folder_info) ||
        !(folder_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (folder_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (folder != INVALID_HANDLE_VALUE) CloseHandle(folder);
        diag_log(diag, 1, "[%s] error: map folder must be a direct, non-reparse directory", filename);
        return 0;
    }

    file = CreateFileA(script_full, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                           FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (file == INVALID_HANDLE_VALUE) {
        diag_log(diag, 1, "[%s] error: cannot open script safely (Windows error %lu)", filename,
                 (unsigned long)GetLastError());
        CloseHandle(folder);
        return 0;
    }
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                  FILE_ATTRIBUTE_REPARSE_POINT))) {
        diag_log(diag, 1, "[%s] error: opened script is not a direct regular file", filename);
        goto done;
    }
    if (info.nFileSizeHigh != 0 ||
        info.nFileSizeLow > (DWORD)maximum_bytes) {
        diag_log(diag, 1, "[%s] error: file exceeds its package byte limit", filename);
        goto done;
    }
    expected_size = info.nFileSizeLow;
    out->bytes = (unsigned char*)malloc((size_t)expected_size + 1u);
    if (!out->bytes) {
        diag_log(diag, 1, "[%s] error: out of memory while reading script", filename);
        goto done;
    }
    if (expected_size != 0 &&
        (!ReadFile(file, out->bytes, expected_size, &bytes_read, NULL) ||
         bytes_read != expected_size)) {
        diag_log(diag, 1, "[%s] error: could not read the complete script", filename);
        goto done;
    }
    if (!ReadFile(file, &extra_byte, 1, &extra_read, NULL) || extra_read != 0) {
        diag_log(diag, 1, "[%s] error: script changed while it was being read", filename);
        goto done;
    }
    if (expected_size != 0 && memchr(out->bytes, '\0', expected_size)) {
        diag_log(diag, 1, "[%s] error: embedded NUL bytes are not supported", filename);
        goto done;
    }
    out->bytes[expected_size] = '\0';
    out->size = (size_t)expected_size;
    if (!content_registry_sha256_bytes(out->bytes, out->size, NULL,
                                       out->sha256, NULL, 0)) {
        diag_log(diag, 1, "[%s] error: cannot hash validated bytes", filename);
        goto done;
    }
    snprintf(out->full_path, sizeof(out->full_path), "%s", script_full);
    ok = 1;

done:
    CloseHandle(file);
    CloseHandle(folder);
    if (!ok) optional_map_script_source_dispose(out);
    return ok;
}

static int read_optional_map_script_source(MapDiagnostics* diag, const char* folder_path,
    int format_version, OptionalMapScriptSource* out) {
    return read_optional_package_source(diag, folder_path, format_version,
        "map.lua", CUSTOM_MAP_MAX_SCRIPT_BYTES, out);
}

static uint64_t map_script_id_from_package(const char* digest,
                                           const CustomMap* map) {
    static const char domain[] = "eggnoggplus/map-script-definition/v1";
    unsigned char canonical[(sizeof(domain) - 1u) +
                            (CONTENT_SHA256_HEX_SIZE - 1u) + 2u +
                            CUSTOM_MAP_MAX_CONTENT_TILES *
                                (3u + CONTENT_KEY_MAX - 1u) + CONTENT_SHA256_HEX_SIZE];
    uint8_t identity_digest[CONTENT_SHA256_SIZE];
    size_t position = 0;
    uint64_t value = 0;
    int i;
    if (!digest || strlen(digest) != CONTENT_SHA256_HEX_SIZE - 1u || !map ||
        map->content_tile_count < 0 ||
        map->content_tile_count > CUSTOM_MAP_MAX_CONTENT_TILES) {
        return 0;
    }
    memcpy(canonical + position, domain, sizeof(domain) - 1u);
    position += sizeof(domain) - 1u;
    memcpy(canonical + position, digest, CONTENT_SHA256_HEX_SIZE - 1u);
    position += CONTENT_SHA256_HEX_SIZE - 1u;
    if (map->entity_source) {
        canonical[position++] = 0xee;
        memcpy(canonical + position, map->entity_sha256, CONTENT_SHA256_HEX_SIZE - 1u);
        position += CONTENT_SHA256_HEX_SIZE - 1u;
    }
    canonical[position++] = (unsigned char)(map->content_tile_count & 0xff);
    canonical[position++] = (unsigned char)((map->content_tile_count >> 8) & 0xff);
    for (i = 0; i < map->content_tile_count; i++) {
        size_t key_length = strlen(map->content_tiles[i].key);
        if (key_length == 0 || key_length >= CONTENT_KEY_MAX ||
            position + 3u + key_length > sizeof(canonical)) {
            return 0;
        }
        canonical[position++] = (unsigned char)map->content_tiles[i].symbol;
        canonical[position++] = (unsigned char)(key_length & 0xffu);
        canonical[position++] = (unsigned char)((key_length >> 8) & 0xffu);
        memcpy(canonical + position, map->content_tiles[i].key, key_length);
        position += key_length;
    }
    if (!content_registry_sha256_bytes(canonical, position, identity_digest,
                                       NULL, NULL, 0)) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        value = (value << 8) | (uint64_t)identity_digest[i];
    }
    /* The VM reserves zero for "no script". Canonicalizing the vanishingly
     * unlikely truncated SHA-256 result keeps this identity total. */
    return value ? value : UINT64_C(1);
}

static int custom_map_script_definition(const CustomMap* map,
                                        MapScriptDefinition* out_definition,
                                        MapScriptTileBinding* out_bindings,
                                        size_t bindings_cap,
                                        char* out_chunk_name,
                                        size_t chunk_name_cap) {
    int i;
    int written;
    if (!map || !out_definition || !out_bindings || !out_chunk_name ||
        map->script_id == 0 || !map->script_source ||
        map->content_tile_count < 0 ||
        (size_t)map->content_tile_count > bindings_cap) {
        return 0;
    }
    written = snprintf(out_chunk_name, chunk_name_cap, "@%s",
                       map->script_full_path);
    if (written < 0 || (size_t)written >= chunk_name_cap) return 0;
    for (i = 0; i < map->content_tile_count; i++) {
        out_bindings[i].symbol = map->content_tiles[i].symbol;
        out_bindings[i].qualified_key = map->content_tiles[i].key;
    }
    memset(out_definition, 0, sizeof(*out_definition));
    out_definition->script_id = map->script_id;
    out_definition->chunk_name = out_chunk_name;
    out_definition->source = (const char*)map->script_source;
    out_definition->source_len = map->script_size;
    out_definition->bindings = out_bindings;
    out_definition->binding_count = (size_t)map->content_tile_count;
    custom_map_entity_layout(map, &out_definition->entity_layout);
    return 1;
}

static int custom_map_validate_entity_visuals(MapDiagnostics* diag,const CustomMap* map) {
    EntityPackageLayout layout={0};custom_map_entity_layout(map,&layout);
    char err[384];EntityPackage* package=entity_package_decode_layout((const char*)map->entity_source,map->entity_size,&layout,err,sizeof(err));
    int valid=1;
    if(!package) {diag_log(diag,1,"[entities.json] error: %s",err);return 0;}
    for(uint32_t id=1;id<=entity_package_type_count(package);id++) {
        EntityVisual visual;int found=0;
        if(!entity_package_visual(package,id,&visual)) continue;
        if(!strncmp(visual.sheet,"builtin:",8)) {
            found=!strcmp(visual.sheet,"builtin:tiles") || !strcmp(visual.sheet,"builtin:sprites") ||
                !strcmp(visual.sheet,"builtin:misc") || !strcmp(visual.sheet,"builtin:glyphs");
            if(!found) diag_log(diag,1,"[entities.json][%s.visual.sheet] error: unknown built-in sheet '%s'",entity_package_type_key(package,id),visual.sheet);
        } else {
            int match=-1, matches=0;
            for(int i=0;i<map->content_sheet_count;i++) if(!strcmp(visual.sheet,map->content_sheets[i].relative_path)) {match=i;matches++;}
            if(matches>1) diag_log(diag,1,"[entities.json][%s.visual.sheet] error: '%s' has multiple grid declarations; entity sheets must be unambiguous",entity_package_type_key(package,id),visual.sheet);
            else if(matches==0) diag_log(diag,1,"[entities.json][%s.visual.sheet] error: '%s' must reference a declared map tileset sheet",entity_package_type_key(package,id),visual.sheet);
            else {
                found=(uint64_t)visual.sprite+visual.frames<=(uint64_t)map->content_sheets[match].sprite_count;
                if(!found) diag_log(diag,1,"[entities.json][%s.visual] error: animation range exceeds '%s' (%d sprites)",entity_package_type_key(package,id),visual.sheet,map->content_sheets[match].sprite_count);
                for(uint32_t animation_id=1;animation_id<=entity_package_animation_count(package,id);++animation_id) {
                    EntityVisual clip;const char* animation_name=entity_package_animation_name(package,id,animation_id);
                    if(!entity_package_animation_visual(package,id,animation_id,&clip) ||
                       (uint64_t)clip.sprite+clip.frames>(uint64_t)map->content_sheets[match].sprite_count) {
                        diag_log(diag,1,"[entities.json][%s.animations.%s] error: animation range exceeds '%s' (%d sprites)",
                            entity_package_type_key(package,id),animation_name?animation_name:"?",visual.sheet,map->content_sheets[match].sprite_count);
                        valid=0;
                    }
                }
            }
        }
        if(!found) valid=0;
    }
    entity_package_free(package);return valid;
}

static int custom_map_attach_optional_script(MapDiagnostics* diag,
                                             const char* folder_path,
                                             CustomMap* map) {
    OptionalMapScriptSource source, entities;
    MapScriptDefinition definition;
    MapScriptTileBinding bindings[CUSTOM_MAP_MAX_CONTENT_TILES];
    char chunk_name[MAX_PATH + 2];
    char err[512];
    int valid;
    if (!diag || !folder_path || !map) return 0;
    memset(&source, 0, sizeof(source));
    memset(&entities, 0, sizeof(entities));
    if (!read_optional_package_source(diag, folder_path, map->format_version,
            "entities.json", 1024u * 1024u, &entities)) return 0;
    if (entities.present) {
        map->entity_source = entities.bytes;
        map->entity_size = entities.size;
        snprintf(map->entity_sha256, sizeof(map->entity_sha256), "%s", entities.sha256);
        entities.bytes = NULL;
    }
    optional_map_script_source_dispose(&entities);
    if(map->entity_source && !custom_map_validate_entity_visuals(diag,map)) return 0;
    if (!read_optional_map_script_source(diag, folder_path,
                                         map->format_version, &source)) {
        return 0;
    }
    if (!source.present && map->entity_source) {
        source.bytes = calloc(1, 1);
        if (!source.bytes || !content_registry_sha256_bytes(source.bytes, 0, NULL, source.sha256, NULL, 0)) {
            optional_map_script_source_dispose(&source);
            diag_log(diag, 1, "[entities.json] cannot prepare default empty map script"); return 0;
        }
        source.present = 1;
        snprintf(source.full_path, sizeof(source.full_path), "entities.json/default-script");
    }
    if (!source.present) return 1;

    map->script_id = map_script_id_from_package(source.sha256, map);
    if (map->script_id == 0) {
        diag_log(diag, 1, "[map.lua] error: could not derive a stable script identity");
        optional_map_script_source_dispose(&source);
        return 0;
    }
    snprintf(map->script_full_path, sizeof(map->script_full_path), "%s",
             source.full_path);
    snprintf(map->script_sha256, sizeof(map->script_sha256), "%s",
             source.sha256);
    map->script_source = source.bytes;
    map->script_size = source.size;
    source.bytes = NULL;
    optional_map_script_source_dispose(&source);

    if (!custom_map_script_definition(map, &definition, bindings,
                                      sizeof(bindings) / sizeof(bindings[0]),
                                      chunk_name, sizeof(chunk_name))) {
        diag_log(diag, 1, "[map.lua] error: could not build the validation definition");
        free(map->script_source);
        map->script_source = NULL;
        map->script_size = 0;
        map->script_id = 0;
        map->script_full_path[0] = '\0';
        map->script_sha256[0] = '\0';
        return 0;
    }
    err[0] = '\0';
    valid = map->entity_source
        ? map_script_validate_content(&definition, (const char*)map->entity_source,
                                      map->entity_size, err, sizeof(err))
        : map_script_validate(&definition, err, sizeof(err));
    if (!valid) {
        diag_log(diag, 1, "[map.lua] error: %s",
                 err[0] ? err : "script validation failed");
        free(map->script_source);
        map->script_source = NULL;
        map->script_size = 0;
        map->script_id = 0;
        map->script_full_path[0] = '\0';
        map->script_sha256[0] = '\0';
        return 0;
    }
    return 1;
}

static void skip_json_ws(JsonParser* parser) {
    while (*parser->cur) {
        char ch = *parser->cur;
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            parser->cur++;
            parser->col++;
            continue;
        }
        if (ch == '\n') {
            parser->cur++;
            parser->line++;
            parser->col = 1;
            continue;
        }
        break;
    }
}

static void json_parse_error(JsonParser* parser, const char* fmt, ...) {
    char message[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    diag_log(parser->diag, 1, "[%s][line=%d][col=%d] error: %s",
             parser->file_name, parser->line, parser->col, message);
    parser->failed = 1;
}

static JsonValue* json_alloc_value(JsonParser* parser, JsonType type, int line, int col) {
    JsonValue* value = (JsonValue*)calloc(1, sizeof(*value));
    if (!value) {
        json_parse_error(parser, "out of memory");
        return NULL;
    }
    value->type = type;
    value->line = line;
    value->col = col;
    return value;
}

static char* json_parse_string_raw(JsonParser* parser) {
    char* out = NULL;
    size_t cap = 32;
    size_t len = 0;

    if (*parser->cur != '"') {
        json_parse_error(parser, "expected string");
        return NULL;
    }
    parser->cur++;
    parser->col++;

    out = (char*)malloc(cap);
    if (!out) {
        json_parse_error(parser, "out of memory");
        return NULL;
    }

    while (*parser->cur) {
        unsigned char ch = (unsigned char)*parser->cur;
        if (ch == '"') {
            parser->cur++;
            parser->col++;
            out[len] = '\0';
            return out;
        }
        if (ch == '\\') {
            char escaped;
            parser->cur++;
            parser->col++;
            if (!*parser->cur) break;
            escaped = *parser->cur;
            switch (escaped) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    unsigned codepoint = 0;
                    int i;
                    parser->cur++;
                    parser->col++;
                    for (i = 0; i < 4; i++) {
                        char hex = *parser->cur;
                        if (!isxdigit((unsigned char)hex)) {
                            free(out);
                            json_parse_error(parser, "invalid unicode escape");
                            return NULL;
                        }
                        codepoint <<= 4;
                        if (hex >= '0' && hex <= '9') codepoint |= (unsigned)(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') codepoint |= (unsigned)(hex - 'a' + 10);
                        else codepoint |= (unsigned)(hex - 'A' + 10);
                        parser->cur++;
                        parser->col++;
                    }
                    if (codepoint == 0) {
                        free(out);
                        json_parse_error(parser, "JSON strings cannot contain NUL");
                        return NULL;
                    }
                    ch = (codepoint <= 0x7f) ? (unsigned char)codepoint : '?';
                    goto append_char;
                }
                default:
                    free(out);
                    json_parse_error(parser, "unsupported escape '\\%c'", escaped);
                    return NULL;
            }
        } else if (ch < 0x20) {
            free(out);
            json_parse_error(parser, "unescaped control character in string");
            return NULL;
        }
        parser->cur++;
        parser->col++;
append_char:
        if (len + 2 > cap) {
            char* bigger;
            cap *= 2;
            bigger = (char*)realloc(out, cap);
            if (!bigger) {
                free(out);
                json_parse_error(parser, "out of memory");
                return NULL;
            }
            out = bigger;
        }
        out[len++] = (char)ch;
    }

    free(out);
    json_parse_error(parser, "unterminated string");
    return NULL;
}

static JsonValue* json_parse_value(JsonParser* parser);
static void json_free_value(JsonValue* value);

static JsonValue* json_parse_string_value(JsonParser* parser) {
    JsonValue* value;
    char* text;
    int line = parser->line;
    int col = parser->col;

    text = json_parse_string_raw(parser);
    if (!text) return NULL;

    value = json_alloc_value(parser, JSON_STRING, line, col);
    if (!value) {
        free(text);
        return NULL;
    }
    value->u.string_value = text;
    return value;
}

static JsonValue* json_parse_number_value(JsonParser* parser) {
    JsonValue* value;
    char* end_ptr = NULL;
    double number_value;
    int line = parser->line;
    int col = parser->col;

    errno = 0;
    number_value = strtod(parser->cur, &end_ptr);
    if (end_ptr == parser->cur) {
        json_parse_error(parser, "expected number");
        return NULL;
    }
    if (errno == ERANGE) {
        json_parse_error(parser, "number out of range");
        return NULL;
    }

    while (parser->cur < end_ptr) {
        parser->cur++;
        parser->col++;
    }

    value = json_alloc_value(parser, JSON_NUMBER, line, col);
    if (!value) return NULL;
    value->u.number_value = number_value;
    return value;
}

static JsonValue* json_parse_array_value(JsonParser* parser) {
    JsonValue* value;
    JsonValue** items = NULL;
    int count = 0;
    int cap = 0;
    int line = parser->line;
    int col = parser->col;

    parser->cur++;
    parser->col++;

    value = json_alloc_value(parser, JSON_ARRAY, line, col);
    if (!value) return NULL;

    skip_json_ws(parser);
    if (*parser->cur == ']') {
        parser->cur++;
        parser->col++;
        return value;
    }

    while (*parser->cur) {
        JsonValue* item;
        if (count >= cap) {
            JsonValue** bigger;
            cap = cap ? cap * 2 : 4;
            bigger = (JsonValue**)realloc(items, (size_t)cap * sizeof(*items));
            if (!bigger) {
                free(items);
                free(value);
                json_parse_error(parser, "out of memory");
                return NULL;
            }
            items = bigger;
        }

        item = json_parse_value(parser);
        if (!item) {
            int i;
            for (i = 0; i < count; i++) json_free_value(items[i]);
            free(items);
            free(value);
            return NULL;
        }
        items[count++] = item;

        skip_json_ws(parser);
        if (*parser->cur == ',') {
            parser->cur++;
            parser->col++;
            skip_json_ws(parser);
            continue;
        }
        if (*parser->cur == ']') {
            parser->cur++;
            parser->col++;
            value->u.array_value.items = items;
            value->u.array_value.count = count;
            return value;
        }
        json_parse_error(parser, "expected ',' or ']'");
        break;
    }

    {
        int i;
        for (i = 0; i < count; i++) json_free_value(items[i]);
    }
    free(items);
    free(value);
    return NULL;
}

static JsonValue* json_parse_object_value(JsonParser* parser) {
    JsonValue* value;
    JsonMember* head = NULL;
    JsonMember* tail = NULL;
    int line = parser->line;
    int col = parser->col;

    parser->cur++;
    parser->col++;

    value = json_alloc_value(parser, JSON_OBJECT, line, col);
    if (!value) return NULL;

    skip_json_ws(parser);
    if (*parser->cur == '}') {
        parser->cur++;
        parser->col++;
        return value;
    }

    while (*parser->cur) {
        JsonMember* member;
        char* key;
        JsonValue* member_value;

        if (*parser->cur != '"') {
            json_parse_error(parser, "expected object key");
            break;
        }

        key = json_parse_string_raw(parser);
        if (!key) break;

        {
            JsonMember* existing = head;
            while (existing) {
                if (strcmp(existing->key, key) == 0) {
                    json_parse_error(parser, "duplicate object key '%s'", key);
                    free(key);
                    key = NULL;
                    break;
                }
                existing = existing->next;
            }
            if (!key) break;
        }

        skip_json_ws(parser);
        if (*parser->cur != ':') {
            free(key);
            json_parse_error(parser, "expected ':' after property");
            break;
        }
        parser->cur++;
        parser->col++;

        skip_json_ws(parser);
        member_value = json_parse_value(parser);
        if (!member_value) {
            free(key);
            break;
        }

        member = (JsonMember*)calloc(1, sizeof(*member));
        if (!member) {
            free(key);
            json_free_value(member_value);
            json_parse_error(parser, "out of memory");
            break;
        }
        member->key = key;
        member->value = member_value;

        if (!head) head = member;
        else tail->next = member;
        tail = member;

        skip_json_ws(parser);
        if (*parser->cur == ',') {
            parser->cur++;
            parser->col++;
            skip_json_ws(parser);
            continue;
        }
        if (*parser->cur == '}') {
            parser->cur++;
            parser->col++;
            value->u.object_value = head;
            return value;
        }
        json_parse_error(parser, "expected ',' or '}'");
        break;
    }

    while (head) {
        JsonMember* next = head->next;
        free(head->key);
        json_free_value(head->value);
        free(head);
        head = next;
    }
    free(value);
    return NULL;
}

static JsonValue* json_parse_value(JsonParser* parser) {
    skip_json_ws(parser);
    if (!*parser->cur) {
        json_parse_error(parser, "unexpected end of file");
        return NULL;
    }

    switch (*parser->cur) {
        case '{':
            return json_parse_object_value(parser);
        case '[':
            return json_parse_array_value(parser);
        case '"':
            return json_parse_string_value(parser);
        case 't': {
            JsonValue* value;
            if (strncmp(parser->cur, "true", 4) != 0) {
                json_parse_error(parser, "invalid token");
                return NULL;
            }
            value = json_alloc_value(parser, JSON_BOOL, parser->line, parser->col);
            if (!value) return NULL;
            value->u.boolean_value = 1;
            parser->cur += 4;
            parser->col += 4;
            return value;
        }
        case 'f': {
            JsonValue* value;
            if (strncmp(parser->cur, "false", 5) != 0) {
                json_parse_error(parser, "invalid token");
                return NULL;
            }
            value = json_alloc_value(parser, JSON_BOOL, parser->line, parser->col);
            if (!value) return NULL;
            value->u.boolean_value = 0;
            parser->cur += 5;
            parser->col += 5;
            return value;
        }
        case 'n': {
            JsonValue* value;
            if (strncmp(parser->cur, "null", 4) != 0) {
                json_parse_error(parser, "invalid token");
                return NULL;
            }
            value = json_alloc_value(parser, JSON_NULL, parser->line, parser->col);
            if (!value) return NULL;
            parser->cur += 4;
            parser->col += 4;
            return value;
        }
        default:
            if (*parser->cur == '-' || isdigit((unsigned char)*parser->cur)) {
                return json_parse_number_value(parser);
            }
            json_parse_error(parser, "unexpected character '%c'", *parser->cur);
            return NULL;
    }
}

static void json_free_value(JsonValue* value) {
    if (!value) return;
    switch (value->type) {
        case JSON_STRING:
            free(value->u.string_value);
            break;
        case JSON_ARRAY: {
            int i;
            for (i = 0; i < value->u.array_value.count; i++) {
                json_free_value(value->u.array_value.items[i]);
            }
            free(value->u.array_value.items);
        } break;
        case JSON_OBJECT: {
            JsonMember* member = value->u.object_value;
            while (member) {
                JsonMember* next = member->next;
                free(member->key);
                json_free_value(member->value);
                free(member);
                member = next;
            }
        } break;
        default:
            break;
    }
    free(value);
}

static JsonValue* json_parse_document(MapDiagnostics* diag, const char* file_name, const char* text) {
    JsonParser parser;
    JsonValue* root;

    memset(&parser, 0, sizeof(parser));
    parser.cur = text;
    parser.line = 1;
    parser.col = 1;
    parser.diag = diag;
    parser.file_name = file_name;

    root = json_parse_value(&parser);
    if (!root) return NULL;

    skip_json_ws(&parser);
    if (*parser.cur) {
        json_parse_error(&parser, "unexpected trailing content");
        json_free_value(root);
        return NULL;
    }

    return root;
}

static JsonValue* json_object_get(const JsonValue* object_value, const char* key) {
    JsonMember* member;

    if (!object_value || object_value->type != JSON_OBJECT) return NULL;
    member = object_value->u.object_value;
    while (member) {
        if (strcmp(member->key, key) == 0) return member->value;
        member = member->next;
    }
    return NULL;
}

static int json_number_to_int(const JsonValue* value, int* out_value) {
    double as_double;
    int as_int;

    if (!value || value->type != JSON_NUMBER || !out_value) return 0;
    as_double = value->u.number_value;
    if (!isfinite(as_double) || as_double < (double)INT_MIN ||
        as_double > (double)INT_MAX) {
        return 0;
    }
    as_int = (int)as_double;
    if ((double)as_int != as_double) return 0;
    *out_value = as_int;
    return 1;
}

static void warn_unknown_keys(MapDiagnostics* diag, const JsonValue* object_value, const char* path,
                              const char* const* known_keys, int known_count) {
    JsonMember* member;
    int i;

    if (!object_value || object_value->type != JSON_OBJECT) return;
    member = object_value->u.object_value;
    while (member) {
        int known = 0;
        for (i = 0; i < known_count; i++) {
            if (strcmp(member->key, known_keys[i]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) {
            if (path && path[0]) {
                diag_log(diag, 0, "[data.json][%s] warning: unknown key \"%s\"; ignoring", path, member->key);
            } else {
                diag_log(diag, 0, "[data.json] warning: unknown key \"%s\"; ignoring", member->key);
            }
        }
        member = member->next;
    }
}

static void reject_unknown_keys(MapDiagnostics* diag, const JsonValue* object_value,
                                const char* path, const char* const* known_keys,
                                int known_count) {
    JsonMember* member;
    int i;
    if (!object_value || object_value->type != JSON_OBJECT) return;
    for (member = object_value->u.object_value; member; member = member->next) {
        int known = 0;
        for (i = 0; i < known_count; i++) {
            if (strcmp(member->key, known_keys[i]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) {
            diag_log(diag, 1, "[data.json][%s] error: unknown key \"%s\"",
                     path && path[0] ? path : "root", member->key);
        }
    }
}

static int json_read_bounded_string(MapDiagnostics* diag,
                                    const JsonValue* object_value,
                                    const char* key,
                                    const char* path,
                                    int required,
                                    char* out,
                                    size_t out_cap) {
    JsonValue* value = json_object_get(object_value, key);
    size_t length;
    if (!out || out_cap == 0) return 0;
    out[0] = '\0';
    if (!value) {
        if (required) {
            diag_log(diag, 1, "[data.json][%s.%s] error: missing required string", path, key);
            return 0;
        }
        return 1;
    }
    if (value->type != JSON_STRING || (required && !value->u.string_value[0])) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected %sstring",
                 path, key, required ? "non-empty " : "");
        return 0;
    }
    length = strlen(value->u.string_value);
    if (length >= out_cap) {
        diag_log(diag, 1,
                 "[data.json][%s.%s] error: string is too long (max %u characters)",
                 path, key, (unsigned)(out_cap - 1));
        return 0;
    }
    memcpy(out, value->u.string_value, length + 1);
    return 1;
}

static int json_read_int_range(MapDiagnostics* diag,
                               const JsonValue* object_value,
                               const char* key,
                               const char* path,
                               int fallback,
                               int minimum,
                               int maximum,
                               int* out) {
    JsonValue* value = json_object_get(object_value, key);
    int parsed;
    if (!value) {
        *out = fallback;
        return 1;
    }
    if (!json_number_to_int(value, &parsed) || parsed < minimum || parsed > maximum) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected integer in %d..%d",
                 path, key, minimum, maximum);
        return 0;
    }
    *out = parsed;
    return 1;
}

static int json_read_float_range(MapDiagnostics* diag,
                                 const JsonValue* object_value,
                                 const char* key,
                                 const char* path,
                                 float fallback,
                                 float minimum,
                                 float maximum,
                                 float* out) {
    JsonValue* value = json_object_get(object_value, key);
    double parsed;
    if (!value) {
        *out = fallback;
        return 1;
    }
    if (value->type != JSON_NUMBER ||
        !isfinite(parsed = value->u.number_value) ||
        parsed < (double)minimum || parsed > (double)maximum) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected finite number in %.3g..%.3g",
                 path, key, (double)minimum, (double)maximum);
        return 0;
    }
    *out = (float)parsed;
    return 1;
}

static int json_read_bool(MapDiagnostics* diag,
                          const JsonValue* object_value,
                          const char* key,
                          const char* path,
                          int fallback,
                          int* out) {
    JsonValue* value = json_object_get(object_value, key);
    if (!value) {
        *out = fallback;
        return 1;
    }
    if (value->type != JSON_BOOL) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected boolean", path, key);
        return 0;
    }
    *out = value->u.boolean_value ? 1 : 0;
    return 1;
}

static int json_read_tint4(MapDiagnostics* diag,
                           const JsonValue* object_value,
                           const char* path,
                           float out[4]) {
    JsonValue* value = json_object_get(object_value, "tint");
    int i;
    for (i = 0; i < 4; i++) out[i] = 1.0f;
    if (!value) return 1;
    if (value->type != JSON_ARRAY || value->u.array_value.count != 4) {
        diag_log(diag, 1, "[data.json][%s.tint] error: expected a 4-number array", path);
        return 0;
    }
    for (i = 0; i < 4; i++) {
        JsonValue* channel = value->u.array_value.items[i];
        if (!channel || channel->type != JSON_NUMBER ||
            !isfinite(channel->u.number_value) ||
            channel->u.number_value < 0.0 || channel->u.number_value > 1.0) {
            diag_log(diag, 1,
                     "[data.json][%s.tint] error: channel %d must be a finite number in 0..1",
                     path, i + 1);
            return 0;
        }
        out[i] = (float)channel->u.number_value;
    }
    return 1;
}

static int json_read_tint4_named(MapDiagnostics* diag,
                                 const JsonValue* object_value,
                                 const char* key,
                                 const char* path,
                                 const float fallback[4],
                                 float out[4]) {
    JsonValue* value = json_object_get(object_value, key);
    int i;
    for (i = 0; i < 4; i++) out[i] = fallback[i];
    if (!value) return 1;
    if (value->type != JSON_ARRAY || value->u.array_value.count != 4) {
        diag_log(diag, 1,
                 "[data.json][%s.%s] error: expected a 4-number array",
                 path, key);
        return 0;
    }
    for (i = 0; i < 4; i++) {
        JsonValue* channel = value->u.array_value.items[i];
        if (!channel || channel->type != JSON_NUMBER ||
            !isfinite(channel->u.number_value) ||
            channel->u.number_value < 0.0 || channel->u.number_value > 1.0) {
            diag_log(diag, 1,
                     "[data.json][%s.%s] error: channel %d must be a finite number in 0..1",
                     path, key, i + 1);
            return 0;
        }
        out[i] = (float)channel->u.number_value;
    }
    return 1;
}

static int parse_map_format_and_owner(MapDiagnostics* diag,
                                      const JsonValue* root_value,
                                      const char* folder_id,
                                      int* out_version,
                                      char out_owner[CONTENT_OWNER_MAX]) {
    JsonValue* format_value;
    JsonValue* id_value;
    const char* stable_id = folder_id;
    char owner_candidate[CONTENT_OWNER_MAX + 16];
    char key[CONTENT_KEY_MAX];
    char err[256];
    char* colon;
    size_t owner_length;
    int written;
    if (out_version) *out_version = 0;
    if (out_owner) out_owner[0] = '\0';
    if (!root_value || root_value->type != JSON_OBJECT) return 0;
    format_value = json_object_get(root_value, "format");
    if (!format_value || format_value->type != JSON_STRING) {
        diag_log(diag, 1, "[data.json][format] error: missing format string");
        return 0;
    }
    if (strcmp(format_value->u.string_value, "eggnogg-map/v1") == 0) {
        if (out_version) *out_version = 1;
        return 1;
    }
    if (strcmp(format_value->u.string_value, "eggnogg-map/v2") != 0) {
        diag_log(diag, 1,
                 "[data.json][format] error: expected \"eggnogg-map/v1\" or \"eggnogg-map/v2\"");
        return 0;
    }
    if (out_version) *out_version = 2;
    id_value = json_object_get(root_value, "id");
    if (!id_value || id_value->type != JSON_STRING || !id_value->u.string_value[0]) {
        diag_log(diag, 1, "[data.json][id] error: v2 requires a non-empty stable id");
        return 0;
    }
    stable_id = id_value->u.string_value;
    err[0] = '\0';
    written = snprintf(owner_candidate, sizeof(owner_candidate), "map.%s", stable_id);
    if (written < 0 || (size_t)written >= sizeof(owner_candidate)) {
        diag_log(diag, 1,
                 "[data.json][id] error: id is too long for a map content namespace");
        return 0;
    }
    if (!content_registry_make_key(owner_candidate, "owner", key, err, sizeof(err))) {
        diag_log(diag, 1, "[data.json][id] error: cannot form content namespace: %s",
                 err[0] ? err : "invalid id");
        return 0;
    }
    colon = strchr(key, ':');
    if (!colon) return 0;
    owner_length = (size_t)(colon - key);
    if (owner_length == 0 || owner_length >= CONTENT_OWNER_MAX) return 0;
    *colon = '\0';
    memcpy(out_owner, key, owner_length);
    out_owner[owner_length] = '\0';
    return 1;
}

static int map_asset_direct_name_valid(const char* relative_path) {
    const char* extension;
    size_t length;
    if (!relative_path || !relative_path[0]) return 0;
    length = strlen(relative_path);
    if (length >= MAX_PATH || strcmp(relative_path, ".") == 0 ||
        strcmp(relative_path, "..") == 0 || strchr(relative_path, '/') ||
        strchr(relative_path, '\\') || strchr(relative_path, ':')) {
        return 0;
    }
    extension = strrchr(relative_path, '.');
    return extension && _stricmp(extension, ".png") == 0;
}

static int map_asset_png_header_valid(const char* full_path, int* out_width, int* out_height) {
    static const unsigned char signature[8] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
    };
    unsigned char header[24];
    FILE* file = fopen(full_path, "rb");
    uint32_t width;
    uint32_t height;
    if (!file) return 0;
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    if (memcmp(header, signature, sizeof(signature)) != 0 ||
        header[8] != 0 || header[9] != 0 || header[10] != 0 || header[11] != 13 ||
        memcmp(header + 12, "IHDR", 4) != 0) {
        return 0;
    }
    width = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16) |
            ((uint32_t)header[18] << 8) | (uint32_t)header[19];
    height = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16) |
             ((uint32_t)header[22] << 8) | (uint32_t)header[23];
    if (width == 0 || height == 0 || width > 8192 || height > 8192) return 0;
    if (out_width) *out_width = (int)width;
    if (out_height) *out_height = (int)height;
    return 1;
}

static int parsed_tileset_find_symbol(const ParsedTileset* tileset, char symbol) {
    int i;
    if (!tileset) return -1;
    for (i = 0; i < tileset->tile_count; i++) {
        if (tileset->tiles[i].symbol == symbol) return i;
    }
    return -1;
}

static int parsed_tileset_find_sheet_path(const ParsedTileset* tileset,
                                          const char* relative_path) {
    int i;
    if (!tileset || !relative_path || !relative_path[0]) return -1;
    for (i = 0; i < tileset->sheet_count; i++) {
        if (strcmp(tileset->sheets[i].relative_path, relative_path) == 0) return i;
    }
    return -1;
}

static int parsed_tileset_add_sheet(MapDiagnostics* diag,
                                    ParsedTileset* tileset,
                                    const char* relative_path,
                                    const char* full_path,
                                    const char* digest,
                                    int cell_w,
                                    int cell_h,
                                    int padding,
                                    int source_x,
                                    int source_y,
                                    int source_w,
                                    int source_h,
                                    int sprite_count,
                                    char out_key[CONTENT_SHEET_KEY_MAX]) {
    char local_id[CONTENT_LOCAL_ID_MAX];
    char identity[CONTENT_SHA256_HEX_SIZE + 128];
    uint32_t path_hash;
    int i;
    snprintf(identity, sizeof(identity), "%s|%dx%d|p%d|r%d,%d,%d,%d",
             digest, cell_w, cell_h, padding,
             source_x, source_y, source_w, source_h);
    path_hash = weak_map_hash32(relative_path, identity);
    snprintf(local_id, sizeof(local_id), "sheet-%08x-%dx%d-p%d",
             (unsigned int)path_hash, cell_w, cell_h, padding);
    if (!content_registry_make_key(tileset->owner, local_id, out_key, NULL, 0)) {
        diag_log(diag, 1, "[data.json][tileset] error: failed to build sheet key");
        return 0;
    }
    for (i = 0; i < tileset->sheet_count; i++) {
        if (strcmp(tileset->sheets[i].key, out_key) != 0) continue;
        if (_stricmp(tileset->sheets[i].relative_path, relative_path) == 0 &&
            _stricmp(tileset->sheets[i].asset_sha256, digest) == 0 &&
            tileset->sheets[i].cell_w == cell_w &&
            tileset->sheets[i].cell_h == cell_h &&
            tileset->sheets[i].padding == padding &&
            tileset->sheets[i].source_x == source_x &&
            tileset->sheets[i].source_y == source_y &&
            tileset->sheets[i].source_w == source_w &&
            tileset->sheets[i].source_h == source_h) {
            return 1;
        }
        diag_log(diag, 1,
                 "[data.json][tileset] error: generated sprite-sheet key collision");
        return 0;
    }
    if (tileset->sheet_count >= CUSTOM_MAP_MAX_CONTENT_SHEETS) {
        diag_log(diag, 1, "[data.json][tileset] error: too many unique sprite sheets (max %d)",
                 CUSTOM_MAP_MAX_CONTENT_SHEETS);
        return 0;
    }
    snprintf(tileset->sheets[tileset->sheet_count].key,
             sizeof(tileset->sheets[tileset->sheet_count].key), "%s", out_key);
    snprintf(tileset->sheets[tileset->sheet_count].relative_path,
             sizeof(tileset->sheets[tileset->sheet_count].relative_path), "%s", relative_path);
    snprintf(tileset->sheets[tileset->sheet_count].full_path,
             sizeof(tileset->sheets[tileset->sheet_count].full_path), "%s", full_path);
    snprintf(tileset->sheets[tileset->sheet_count].asset_sha256,
             sizeof(tileset->sheets[tileset->sheet_count].asset_sha256), "%s", digest);
    tileset->sheets[tileset->sheet_count].cell_w = cell_w;
    tileset->sheets[tileset->sheet_count].cell_h = cell_h;
    tileset->sheets[tileset->sheet_count].padding = padding;
    tileset->sheets[tileset->sheet_count].source_x = source_x;
    tileset->sheets[tileset->sheet_count].source_y = source_y;
    tileset->sheets[tileset->sheet_count].source_w = source_w;
    tileset->sheets[tileset->sheet_count].source_h = source_h;
    tileset->sheets[tileset->sheet_count].sprite_count = sprite_count;
    tileset->sheets[tileset->sheet_count].atlas_flags = 1u;
    tileset->sheet_count++;
    return 1;
}

static int resolve_map_sheet(MapDiagnostics* diag,
                             ParsedTileset* tileset,
                             const char* folder_path,
                             const char* path,
                             const char* sprite_sheet,
                             const char* expected_sha,
                             int cell_w,
                             int cell_h,
                             int padding,
                             int source_x,
                             int source_y,
                             int source_w,
                             int source_h,
                             int geometry_authored,
                             ResolvedMapSheet* out) {
    char err[256];
    if (!diag || !tileset || !folder_path || !path || !sprite_sheet ||
        !sprite_sheet[0] || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->cell_w = cell_w;
    out->cell_h = cell_h;
    out->padding = padding;
    out->source_x = source_x;
    out->source_y = source_y;
    snprintf(out->relative_path, sizeof(out->relative_path), "%s",
             sprite_sheet);

    if (_strnicmp(sprite_sheet, "builtin:", 8) == 0) {
        if (geometry_authored) {
            diag_log(diag, 1,
                     "[data.json][%s] error: cell and source rectangle fields only apply to external PNG sheets",
                     path);
            return 0;
        }
        if (expected_sha && expected_sha[0]) {
            diag_log(diag, 1,
                     "[data.json][%s.asset_sha256] error: omit hash for built-in sheets",
                     path);
            return 0;
        }
        if (strlen(sprite_sheet) >= sizeof(out->key)) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: built-in sheet key is too long",
                     path);
            return 0;
        }
        snprintf(out->key, sizeof(out->key), "%s", sprite_sheet);
        return 1;
    }

    {
        WIN32_FILE_ATTRIBUTE_DATA file_info;
        uint64_t file_size = 0;
        char actual_sha[CONTENT_SHA256_HEX_SIZE];
        if (!map_asset_direct_name_valid(sprite_sheet) ||
            !path_join(out->full_path, sizeof(out->full_path), folder_path,
                       sprite_sheet)) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: expected a direct .png filename inside the map folder",
                     path);
            return 0;
        }
        if (!GetFileAttributesExA(out->full_path, GetFileExInfoStandard,
                                  &file_info) ||
            (file_info.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: file is missing, a directory, or a reparse point",
                     path);
            return 0;
        }
        file_size = ((uint64_t)file_info.nFileSizeHigh << 32) |
                    (uint64_t)file_info.nFileSizeLow;
        if (file_size == 0 || file_size > CUSTOM_MAP_MAX_ASSET_BYTES) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: PNG must be 1 byte..64 MiB",
                     path);
            return 0;
        }
        if (!map_asset_png_header_valid(out->full_path, &out->image_w,
                                        &out->image_h)) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: invalid PNG header or dimensions (max 8192x8192)",
                     path);
            return 0;
        }
        if (source_x < 0 || source_y < 0 || source_x >= out->image_w ||
            source_y >= out->image_h) {
            diag_log(diag, 1,
                     "[data.json][%s] error: source origin %d,%d is outside PNG dimensions %dx%d",
                     path, source_x, source_y, out->image_w, out->image_h);
            return 0;
        }
        out->source_w = source_w > 0 ? source_w : out->image_w - source_x;
        out->source_h = source_h > 0 ? source_h : out->image_h - source_y;
        if (out->source_w < cell_w || out->source_h < cell_h ||
            out->source_w > out->image_w - source_x ||
            out->source_h > out->image_h - source_y ||
            ((out->source_w + padding) % (cell_w + padding)) != 0 ||
            ((out->source_h + padding) % (cell_h + padding)) != 0) {
            diag_log(diag, 1,
                     "[data.json][%s] error: source rectangle %d,%d %dx%d does not fit or form a whole %dx%d grid with padding %d",
                     path, source_x, source_y, out->source_w, out->source_h,
                     cell_w, cell_h, padding);
            return 0;
        }
        err[0] = '\0';
        if (!content_registry_sha256_file(out->full_path, NULL, actual_sha,
                                         err, sizeof(err))) {
            diag_log(diag, 1, "[data.json][%s.sprite_sheet] error: %s",
                     path, err[0] ? err : "SHA-256 failure");
            return 0;
        }
        if (expected_sha && expected_sha[0] &&
            _stricmp(expected_sha, actual_sha) != 0) {
            diag_log(diag, 1,
                     "[data.json][%s.asset_sha256] error: declared SHA-256 does not match file bytes",
                     path);
            return 0;
        }
        out->sprite_count =
            ((out->source_w + padding) / (cell_w + padding)) *
            ((out->source_h + padding) / (cell_h + padding));
        if (out->sprite_count > CUSTOM_MAP_MAX_SHEET_SPRITES) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: sheet grid contains %d sprites (max %d)",
                     path, out->sprite_count,
                     CUSTOM_MAP_MAX_SHEET_SPRITES);
            return 0;
        }
        snprintf(out->asset_sha256, sizeof(out->asset_sha256), "%s",
                 actual_sha);
        if (!parsed_tileset_add_sheet(diag, tileset, sprite_sheet,
                                      out->full_path, out->asset_sha256,
                                      cell_w, cell_h, padding,
                                      out->source_x, out->source_y,
                                      out->source_w, out->source_h,
                                      out->sprite_count, out->key)) {
            return 0;
        }
        out->external = 1;
        return 1;
    }
}

static void parsed_tileset_abort(ParsedTileset* tileset) {
    if (!tileset) return;
    if (tileset->transaction) content_registry_abort(tileset->transaction);
    memset(tileset, 0, sizeof(*tileset));
}

static int parse_v2_tileset(MapDiagnostics* diag,
                            const JsonValue* root_value,
                            const char* folder_path,
                            const char* owner,
                            ParsedTileset* out_tileset) {
    static const char* const tileset_keys[] = {
        "sprite_sheet", "asset_sha256", "cell_w", "cell_h", "padding",
        "source_x", "source_y", "source_w", "source_h",
        "native_layout", "tiles", "sheets"
    };
    static const char* const tile_keys[] = {
        "id", "symbol", "name", "native_glyph", "sprite_sheet",
        "asset_sha256", "sprite_index", "frame_count", "frame_ticks",
        "animation", "layer", "mirror_with_room", "random_phase",
        "native_visual",
        "offset_x", "offset_y", "scale_x", "scale_y", "angle_degrees", "tint",
        "cell_w", "cell_h", "padding", "source_x", "source_y", "source_w", "source_h",
        "collision", "force_mode",
        "force_x", "force_y", "max_speed_x", "max_speed_y"
    };
    JsonValue* tileset_value = json_object_get(root_value, "tileset");
    JsonValue* tiles_value;
    ResolvedMapSheet default_sheet;
    char default_sprite_sheet[MAX_PATH];
    char default_expected_sha[CONTENT_SHA256_HEX_SIZE];
    int default_cell_w = 16;
    int default_cell_h = 16;
    int default_padding = 0;
    int default_source_x = 0;
    int default_source_y = 0;
    int default_source_w = 0;
    int default_source_h = 0;
    int native_layout = 0;
    int have_default_sheet = 0;
    int top_geometry_authored = 0;
    int top_valid = 1;
    int start_errors = diag->error_count;
    int i;
    memset(out_tileset, 0, sizeof(*out_tileset));
    snprintf(out_tileset->owner, sizeof(out_tileset->owner), "%s", owner);
    out_tileset->transaction = content_registry_begin(owner, NULL, 0);
    if (!out_tileset->transaction) {
        diag_log(diag, 1, "[data.json][tileset] error: failed to create content transaction");
        return 0;
    }
    if (!tileset_value) return 1;
    if (tileset_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][tileset] error: expected object");
        parsed_tileset_abort(out_tileset);
        return 0;
    }
    reject_unknown_keys(diag, tileset_value, "tileset", tileset_keys,
                        (int)(sizeof(tileset_keys) / sizeof(tileset_keys[0])));
    memset(&default_sheet, 0, sizeof(default_sheet));
    top_valid &= json_read_bounded_string(diag, tileset_value, "sprite_sheet",
                                          "tileset", 0,
                                          default_sprite_sheet,
                                          sizeof(default_sprite_sheet));
    top_valid &= json_read_bounded_string(diag, tileset_value, "asset_sha256",
                                          "tileset", 0,
                                          default_expected_sha,
                                          sizeof(default_expected_sha));
    top_valid &= json_read_int_range(diag, tileset_value, "cell_w", "tileset",
                                     16, 1, 512, &default_cell_w);
    top_valid &= json_read_int_range(diag, tileset_value, "cell_h", "tileset",
                                     16, 1, 512, &default_cell_h);
    top_valid &= json_read_int_range(diag, tileset_value, "padding", "tileset",
                                     0, 0, 64, &default_padding);
    top_valid &= json_read_int_range(diag, tileset_value, "source_x", "tileset",
                                     0, 0, 8191, &default_source_x);
    top_valid &= json_read_int_range(diag, tileset_value, "source_y", "tileset",
                                     0, 0, 8191, &default_source_y);
    top_valid &= json_read_int_range(diag, tileset_value, "source_w", "tileset",
                                     0, 0, 8192, &default_source_w);
    top_valid &= json_read_int_range(diag, tileset_value, "source_h", "tileset",
                                     0, 0, 8192, &default_source_h);
    top_valid &= json_read_bool(diag, tileset_value, "native_layout", "tileset",
                                0, &native_layout);
    top_geometry_authored = json_object_get(tileset_value, "cell_w") != NULL ||
                            json_object_get(tileset_value, "cell_h") != NULL ||
                            json_object_get(tileset_value, "padding") != NULL ||
                            json_object_get(tileset_value, "source_x") != NULL ||
                            json_object_get(tileset_value, "source_y") != NULL ||
                            json_object_get(tileset_value, "source_w") != NULL ||
                            json_object_get(tileset_value, "source_h") != NULL;
    if (json_object_get(tileset_value, "sprite_sheet") &&
        !default_sprite_sheet[0]) {
        diag_log(diag, 1,
                 "[data.json][tileset.sprite_sheet] error: expected non-empty string");
        top_valid = 0;
    }
    if (default_expected_sha[0] && !default_sprite_sheet[0]) {
        diag_log(diag, 1,
                 "[data.json][tileset.asset_sha256] error: default hash requires tileset.sprite_sheet");
        top_valid = 0;
    }
    if (native_layout && !default_sprite_sheet[0]) {
        diag_log(diag, 1,
                 "[data.json][tileset.native_layout] error: native layout requires an external default sprite_sheet");
        top_valid = 0;
    }
    if (top_valid && default_sprite_sheet[0]) {
        if (!resolve_map_sheet(diag, out_tileset, folder_path, "tileset",
                               default_sprite_sheet, default_expected_sha,
                               default_cell_w, default_cell_h,
                               default_padding, default_source_x, default_source_y,
                               default_source_w, default_source_h,
                               top_geometry_authored,
                               &default_sheet)) {
            top_valid = 0;
        } else {
            have_default_sheet = 1;
            snprintf(out_tileset->default_sheet_key,
                     sizeof(out_tileset->default_sheet_key), "%s",
                     default_sheet.key);
            out_tileset->default_sheet_sprite_count =
                default_sheet.sprite_count;
        }
    }
    {
        static const char* const sheet_keys[]={"sprite_sheet","asset_sha256","cell_w","cell_h","padding","source_x","source_y","source_w","source_h"};
        JsonValue* sheets=json_object_get(tileset_value,"sheets");
        if(sheets) {
            if(sheets->type!=JSON_ARRAY || sheets->u.array_value.count>CUSTOM_MAP_MAX_CONTENT_SHEETS) {
                diag_log(diag,1,"[data.json][tileset.sheets] error: expected at most 16 sheet declarations");top_valid=0;
            } else for(int sheet_index=0;sheet_index<sheets->u.array_value.count;sheet_index++) {
                JsonValue* value=sheets->u.array_value.items[sheet_index];char path[96],filename[128]={0},sha[65]={0};int w=16,h=16,padding=0,source_x=0,source_y=0,source_w=0,source_h=0,valid=1;ResolvedMapSheet resolved;
                snprintf(path,sizeof(path),"tileset.sheets[%d]",sheet_index);
                if(value->type!=JSON_OBJECT) {diag_log(diag,1,"[data.json][%s] error: expected object",path);top_valid=0;continue;}
                reject_unknown_keys(diag,value,path,sheet_keys,9);
                valid &= json_read_bounded_string(diag,value,"sprite_sheet",path,1,filename,sizeof(filename));
                valid &= json_read_bounded_string(diag,value,"asset_sha256",path,0,sha,sizeof(sha));
                valid &= json_read_int_range(diag,value,"cell_w",path,16,1,512,&w);
                valid &= json_read_int_range(diag,value,"cell_h",path,16,1,512,&h);
                valid &= json_read_int_range(diag,value,"padding",path,0,0,64,&padding);
                valid &= json_read_int_range(diag,value,"source_x",path,0,0,8191,&source_x);
                valid &= json_read_int_range(diag,value,"source_y",path,0,0,8191,&source_y);
                valid &= json_read_int_range(diag,value,"source_w",path,0,0,8192,&source_w);
                valid &= json_read_int_range(diag,value,"source_h",path,0,0,8192,&source_h);
                if(!map_asset_direct_name_valid(filename)) {diag_log(diag,1,"[data.json][%s.sprite_sheet] error: expected a direct PNG filename",path);valid=0;}
                if(valid && !resolve_map_sheet(diag,out_tileset,folder_path,path,filename,sha,w,h,padding,source_x,source_y,source_w,source_h,1,&resolved)) valid=0;
                if(!valid) top_valid=0;
            }
        }
    }
    if (top_valid && native_layout) {
        if (!have_default_sheet || !default_sheet.external) {
            diag_log(diag, 1,
                     "[data.json][tileset.native_layout] error: native layout requires an external default map sheet");
            top_valid = 0;
        } else if (default_sheet.sprite_count < NATIVE_TILE_SPRITE_COUNT) {
            diag_log(diag, 1,
                     "[data.json][tileset.native_layout] error: map sheet needs at least 128 sprites because native tile actions address the first 128 cells; extra cells are allowed");
            top_valid = 0;
        }
    }
    out_tileset->native_layout = native_layout && top_valid;
    if (!top_valid || diag->error_count != start_errors) {
        parsed_tileset_abort(out_tileset);
        return 0;
    }
    tiles_value = json_object_get(tileset_value, "tiles");
    if (!tiles_value) return diag->error_count == start_errors;
    if (tiles_value->type != JSON_ARRAY) {
        diag_log(diag, 1, "[data.json][tileset.tiles] error: expected array");
        parsed_tileset_abort(out_tileset);
        return 0;
    }
    if (tiles_value->u.array_value.count > CUSTOM_MAP_MAX_CONTENT_TILES) {
        diag_log(diag, 1, "[data.json][tileset.tiles] error: too many tiles (max %d)",
                 CUSTOM_MAP_MAX_CONTENT_TILES);
    }
    for (i = 0; i < tiles_value->u.array_value.count &&
                i < CUSTOM_MAP_MAX_CONTENT_TILES; i++) {
        JsonValue* value = tiles_value->u.array_value.items[i];
        ContentTileInput input;
        MapContentTile alias;
        char path[96];
        char id[CONTENT_LOCAL_ID_MAX];
        char name[CONTENT_NAME_MAX];
        char symbol[2];
        char native_glyph[2];
        char tile_sprite_sheet[MAX_PATH];
        char effective_sprite_sheet[MAX_PATH];
        char tile_expected_sha[CONTENT_SHA256_HEX_SIZE];
        char effective_expected_sha[CONTENT_SHA256_HEX_SIZE];
        ResolvedMapSheet resolved_sheet;
        char animation[24];
        char collision[24];
        char native_visual[24];
        char force_mode[24];
        char err[256];
        int mirror = 0;
        int random_phase = 0;
        int cell_w = default_cell_w;
        int cell_h = default_cell_h;
        int padding = default_padding;
        int source_x = default_source_x;
        int source_y = default_source_y;
        int source_w = default_source_w;
        int source_h = default_source_h;
        int sheet_sprite_count = 0;
        int tile_sheet_authored = 0;
        int tile_geometry_authored = 0;
        int valid = 1;
        snprintf(path, sizeof(path), "tileset.tiles[%d]", i + 1);
        memset(&input, 0, sizeof(input));
        memset(&alias, 0, sizeof(alias));
        if (!value || value->type != JSON_OBJECT) {
            diag_log(diag, 1, "[data.json][%s] error: expected object", path);
            continue;
        }
        reject_unknown_keys(diag, value, path, tile_keys,
                            (int)(sizeof(tile_keys) / sizeof(tile_keys[0])));
        valid &= json_read_bounded_string(diag, value, "id", path, 1,
                                          id, sizeof(id));
        valid &= json_read_bounded_string(diag, value, "symbol", path, 1,
                                          symbol, sizeof(symbol));
        valid &= json_read_bounded_string(diag, value, "name", path, 0,
                                          name, sizeof(name));
        valid &= json_read_bounded_string(diag, value, "native_glyph", path, 0,
                                          native_glyph, sizeof(native_glyph));
        valid &= json_read_bounded_string(diag, value, "sprite_sheet", path, 0,
                                          tile_sprite_sheet,
                                          sizeof(tile_sprite_sheet));
        valid &= json_read_bounded_string(diag, value, "asset_sha256", path, 0,
                                          tile_expected_sha,
                                          sizeof(tile_expected_sha));
        valid &= json_read_bounded_string(diag, value, "animation", path, 0,
                                          animation, sizeof(animation));
        valid &= json_read_bounded_string(diag, value, "collision", path, 0,
                                           collision, sizeof(collision));
        valid &= json_read_bounded_string(diag, value, "native_visual", path, 0,
                                          native_visual,
                                          sizeof(native_visual));
        valid &= json_read_bounded_string(diag, value, "force_mode", path, 0,
                                          force_mode, sizeof(force_mode));
        if (!valid) continue;
        valid &= json_read_int_range(diag, value, "cell_w", path,
                                     default_cell_w, 1, 512,
                                     &cell_w);
        valid &= json_read_int_range(diag, value, "cell_h", path,
                                     default_cell_h, 1, 512,
                                     &cell_h);
        valid &= json_read_int_range(diag, value, "padding", path,
                                     default_padding, 0, 64,
                                     &padding);
        valid &= json_read_int_range(diag, value, "source_x", path,
                                     default_source_x, 0, 8191, &source_x);
        valid &= json_read_int_range(diag, value, "source_y", path,
                                     default_source_y, 0, 8191, &source_y);
        valid &= json_read_int_range(diag, value, "source_w", path,
                                     default_source_w, 0, 8192, &source_w);
        valid &= json_read_int_range(diag, value, "source_h", path,
                                     default_source_h, 0, 8192, &source_h);
        tile_sheet_authored = json_object_get(value, "sprite_sheet") != NULL;
        tile_geometry_authored = json_object_get(value, "cell_w") != NULL ||
                                 json_object_get(value, "cell_h") != NULL ||
                                 json_object_get(value, "padding") != NULL ||
                                 json_object_get(value, "source_x") != NULL ||
                                 json_object_get(value, "source_y") != NULL ||
                                 json_object_get(value, "source_w") != NULL ||
                                 json_object_get(value, "source_h") != NULL;
        if (tile_sheet_authored && !tile_sprite_sheet[0]) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: expected non-empty string",
                     path);
            valid = 0;
        }
        if (tile_sprite_sheet[0]) {
            snprintf(effective_sprite_sheet,
                     sizeof(effective_sprite_sheet), "%s",
                     tile_sprite_sheet);
        } else if (default_sprite_sheet[0]) {
            snprintf(effective_sprite_sheet,
                     sizeof(effective_sprite_sheet), "%s",
                     default_sprite_sheet);
        } else {
            effective_sprite_sheet[0] = '\0';
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: missing tile sheet and tileset has no default sprite_sheet",
                     path);
            valid = 0;
        }
        if (tile_expected_sha[0]) {
            snprintf(effective_expected_sha,
                     sizeof(effective_expected_sha), "%s",
                     tile_expected_sha);
        } else if (!tile_sheet_authored ||
                   (default_sprite_sheet[0] &&
                    _stricmp(effective_sprite_sheet,
                             default_sprite_sheet) == 0)) {
            snprintf(effective_expected_sha,
                     sizeof(effective_expected_sha), "%s",
                     default_expected_sha);
        } else {
            effective_expected_sha[0] = '\0';
        }
        if ((unsigned char)symbol[0] < 0x20 || (unsigned char)symbol[0] > 0x7e) {
            diag_log(diag, 1,
                     "[data.json][%s.symbol] error: symbol must be one printable ASCII glyph",
                     path);
            valid = 0;
        }
        if (parsed_tileset_find_symbol(out_tileset, symbol[0]) >= 0) {
            diag_log(diag, 1, "[data.json][%s.symbol] error: duplicate symbol \"%c\"",
                     path, symbol[0]);
            valid = 0;
        }
        input.id = id;
        input.name = name[0] ? name : NULL;
        if (!collision[0] || strcmp(collision, "native") == 0) {
            input.collision_mode = CONTENT_COLLISION_NATIVE;
        } else if (strcmp(collision, "solid") == 0) {
            input.collision_mode = CONTENT_COLLISION_SOLID;
        } else if (strcmp(collision, "pass_through") == 0 ||
                   strcmp(collision, "passthrough") == 0) {
            input.collision_mode = CONTENT_COLLISION_PASS_THROUGH;
        } else if (strcmp(collision, "hazard") == 0) {
            input.collision_mode = CONTENT_COLLISION_HAZARD;
        } else {
            diag_log(diag, 1,
                     "[data.json][%s.collision] error: expected native, solid, pass_through, or hazard",
                     path);
            valid = 0;
        }
        if (!force_mode[0] || strcmp(force_mode, "add") == 0) {
            input.force_mode = CONTENT_FORCE_ADD;
        } else if (strcmp(force_mode, "set") == 0) {
            input.force_mode = CONTENT_FORCE_SET;
        } else {
            diag_log(diag, 1,
                     "[data.json][%s.force_mode] error: expected add or set", path);
            valid = 0;
        }
        if (json_object_get(value, "force_x")) input.force_axes |= CONTENT_FORCE_AXIS_X;
        if (json_object_get(value, "force_y")) input.force_axes |= CONTENT_FORCE_AXIS_Y;
        valid &= json_read_float_range(diag, value, "force_x", path, 0.0f,
                                       -64.0f, 64.0f, &input.force_x);
        valid &= json_read_float_range(diag, value, "force_y", path, 0.0f,
                                       -64.0f, 64.0f, &input.force_y);
        valid &= json_read_float_range(diag, value, "max_speed_x", path, 0.0f,
                                       0.0f, 64.0f, &input.max_speed_x);
        valid &= json_read_float_range(diag, value, "max_speed_y", path, 0.0f,
                                       0.0f, 64.0f, &input.max_speed_y);
        if (json_object_get(value, "force_mode") && input.force_axes == 0) {
            diag_log(diag, 1,
                     "[data.json][%s.force_mode] error: force_mode requires force_x and/or force_y",
                     path);
            valid = 0;
        }
        if (input.collision_mode == CONTENT_COLLISION_NATIVE) {
            if (!native_glyph[0] && glyph_allowed(symbol[0]) &&
                content_registry_tile_native_glyph_allowed(symbol[0])) {
                native_glyph[0] = symbol[0];
                native_glyph[1] = '\0';
            }
            if (!native_glyph[0]) {
                diag_log(diag, 1,
                         "[data.json][%s.native_glyph] error: native collision requires a safe native_glyph (builtin overrides default to their symbol)",
                         path);
                valid = 0;
            }
        }
        input.native_glyph = native_glyph[0];
        memset(&resolved_sheet, 0, sizeof(resolved_sheet));
        if (valid && have_default_sheet &&
            _stricmp(effective_sprite_sheet, default_sprite_sheet) == 0 &&
            cell_w == default_sheet.cell_w &&
            cell_h == default_sheet.cell_h &&
            padding == default_sheet.padding &&
            source_x == default_sheet.source_x &&
            source_y == default_sheet.source_y &&
            (source_w == 0 || source_w == default_sheet.source_w) &&
            (source_h == 0 || source_h == default_sheet.source_h) &&
            (!effective_expected_sha[0] ||
             _stricmp(effective_expected_sha,
                      default_sheet.asset_sha256) == 0)) {
            resolved_sheet = default_sheet;
        } else if (valid &&
                   !resolve_map_sheet(
                       diag, out_tileset, folder_path, path,
                       effective_sprite_sheet, effective_expected_sha,
                       cell_w, cell_h, padding,
                       source_x, source_y, source_w, source_h,
                       tile_geometry_authored ||
                           (!tile_sheet_authored && top_geometry_authored),
                       &resolved_sheet)) {
            valid = 0;
        }
        sheet_sprite_count = resolved_sheet.sprite_count;
        input.sprite_sheet = resolved_sheet.key;
        input.asset_sha256_hex = resolved_sheet.external
            ? resolved_sheet.asset_sha256 : NULL;
        valid &= json_read_int_range(diag, value, "sprite_index", path, 0, 0, 1000000,
                                     &input.sprite_index);
        valid &= json_read_int_range(diag, value, "frame_count", path, 1, 1, 256,
                                     &input.frame_count);
        valid &= json_read_int_range(diag, value, "frame_ticks", path, 1, 1, 3600,
                                     &input.frame_ticks);
        valid &= json_read_int_range(diag, value, "layer", path, 0, 0, 1,
                                     &input.layer);
        valid &= json_read_bool(diag, value, "mirror_with_room", path, 0, &mirror);
        valid &= json_read_bool(diag, value, "random_phase", path, 0, &random_phase);
        valid &= json_read_float_range(diag, value, "offset_x", path, 0.0f,
                                       -4096.0f, 4096.0f, &input.offset_x);
        valid &= json_read_float_range(diag, value, "offset_y", path, 0.0f,
                                       -4096.0f, 4096.0f, &input.offset_y);
        valid &= json_read_float_range(diag, value, "scale_x", path, 1.0f,
                                       -64.0f, 64.0f, &input.scale_x);
        valid &= json_read_float_range(diag, value, "scale_y", path, 1.0f,
                                       -64.0f, 64.0f, &input.scale_y);
        valid &= json_read_float_range(diag, value, "angle_degrees", path, 0.0f,
                                       -360000.0f, 360000.0f, &input.angle_degrees);
        valid &= json_read_tint4(diag, value, path, input.tint);
        input.tint_provided = json_object_get(value, "tint") != NULL;
        if (sheet_sprite_count > 0 &&
            (input.sprite_index >= sheet_sprite_count ||
             input.frame_count > sheet_sprite_count - input.sprite_index)) {
            diag_log(diag, 1,
                     "[data.json][%s] error: sprite_index/frame_count exceeds the external sheet's %d sprites",
                     path, sheet_sprite_count);
            valid = 0;
        }
        if (input.scale_x == 0.0f || input.scale_y == 0.0f) {
            diag_log(diag, 1, "[data.json][%s] error: scale_x and scale_y must be non-zero", path);
            valid = 0;
        }
        if (mirror) input.flags |= CONTENT_TILE_MIRROR_WITH_ROOM;
        if (random_phase) input.flags |= CONTENT_TILE_RANDOM_PHASE;
        if (!native_visual[0] || strcmp(native_visual, "replace") == 0) {
            /* Custom rendering replaces the native draw by default. */
        } else if (strcmp(native_visual, "underlay") == 0) {
            input.flags |= CONTENT_TILE_NATIVE_VISUAL_UNDERLAY;
        } else {
            diag_log(diag, 1,
                     "[data.json][%s.native_visual] error: expected replace or underlay",
                     path);
            valid = 0;
        }
        if (!animation[0] || strcmp(animation, "loop") == 0) {
            input.animation_mode = CONTENT_ANIMATION_LOOP;
        } else if (strcmp(animation, "ping_pong") == 0 || strcmp(animation, "pingpong") == 0) {
            input.animation_mode = CONTENT_ANIMATION_PING_PONG;
        } else if (strcmp(animation, "once") == 0) {
            input.animation_mode = CONTENT_ANIMATION_ONCE;
        } else {
            diag_log(diag, 1,
                     "[data.json][%s.animation] error: expected loop, ping_pong, or once", path);
            valid = 0;
        }
        if (!valid) continue;
        if (!content_registry_tx_register_tile(out_tileset->transaction, &input,
                                               err, sizeof(err))) {
            diag_log(diag, 1, "[data.json][%s] error: %s", path, err);
            continue;
        }
        alias.symbol = symbol[0];
        if (input.collision_mode == CONTENT_COLLISION_SOLID) alias.native_glyph = '@';
        else if (input.collision_mode == CONTENT_COLLISION_PASS_THROUGH) alias.native_glyph = 'x';
        else if (input.collision_mode == CONTENT_COLLISION_HAZARD) alias.native_glyph = 'X';
        else alias.native_glyph = native_glyph[0];
        if (!content_registry_make_key(owner, id, alias.key, err, sizeof(err))) {
            diag_log(diag, 1, "[data.json][%s.id] error: %s", path, err);
            continue;
        }
        out_tileset->tiles[out_tileset->tile_count++] = alias;
    }
    if (diag->error_count != start_errors) {
        parsed_tileset_abort(out_tileset);
        return 0;
    }
    return 1;
}

static int parse_required_string(MapDiagnostics* diag, const JsonValue* object_value, const char* key,
                                 const char* path, char* dst, size_t dst_size) {
    JsonValue* value = json_object_get(object_value, key);
    if (!value) {
        diag_log(diag, 1, "[data.json][%s.%s] error: missing required string", path, key);
        return 0;
    }
    if (value->type != JSON_STRING || !value->u.string_value[0]) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected non-empty string", path, key);
        return 0;
    }
    copy_truncated(diag, dst, dst_size, value->u.string_value, path);
    return 1;
}

static int parse_optional_string(MapDiagnostics* diag, const JsonValue* object_value, const char* key,
                                 const char* path, char* dst, size_t dst_size) {
    JsonValue* value = json_object_get(object_value, key);
    if (!value) return 0;
    if (value->type != JSON_STRING) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected string", path, key);
        return 0;
    }
    copy_truncated(diag, dst, dst_size, value->u.string_value, path);
    return 1;
}

static int parse_integer_field(MapDiagnostics* diag, const JsonValue* object_value, const char* key,
                               const char* path, int* out_value) {
    JsonValue* value = json_object_get(object_value, key);
    int parsed_value;
    if (!value) return 0;
    if (!json_number_to_int(value, &parsed_value)) {
        diag_log(diag, 1, "[data.json][%s.%s] error: expected integer", path, key);
        return 0;
    }
    *out_value = parsed_value;
    return 1;
}

static int builtin_ambient_from_text(const char* text) {
    if (!text) return -1;
    if (strcmp(text, "none") == 0) return 0;
    if (strcmp(text, "bugs") == 0) return 1;
    if (strcmp(text, "clouds") == 0) return 2;
    if (strcmp(text, "art") == 0) return 3;
    if (strcmp(text, "flies") == 0) return 4;
    if (strcmp(text, "drips") == 0) return 5;
    if (strcmp(text, "dust") == 0) return 6;
    if (strcmp(text, "bats") == 0) return 7;
    if (strcmp(text, "bubbles") == 0) return 8;
    if (strcmp(text, "boil") == 0 || strcmp(text, "fumes") == 0) return 9;
    return -1;
}

static int ambient_from_value(MapDiagnostics* diag, const JsonValue* value,
                              const char* path,
                              const MapAmbianceCatalog* catalog,
                              int* out_ambient,
                              int* out_custom_set,
                              uint16_t* out_custom) {
    int parsed_value;
    if (!value) return 0;
    if (out_custom_set) *out_custom_set = 0;

    if (value->type == JSON_STRING) {
        const char* text = value->u.string_value;
        parsed_value = builtin_ambient_from_text(text);
        if (parsed_value < 0 && catalog) {
            for (uint16_t i = 0; i < catalog->ambiance_count; i++) {
                if (strcmp(text, catalog->ambiances[i].id) == 0) {
                    parsed_value = catalog->ambiances[i].native_ambient;
                    if (out_custom_set) *out_custom_set = 1;
                    if (out_custom) *out_custom = i;
                    break;
                }
            }
        }
        if (parsed_value < 0) {
            diag_log(diag, 1,
                     "[data.json][%s] error: unknown ambient \"%s\"",
                     path, text);
            return 0;
        }
        *out_ambient = parsed_value;
        return 1;
    }

    if (!json_number_to_int(value, &parsed_value)) {
        diag_log(diag, 1, "[data.json][%s] error: ambient must be a string alias or integer 0..9", path);
        return 0;
    }
    if (parsed_value < 0 || parsed_value > 9) {
        diag_log(diag, 1, "[data.json][%s] error: ambient integer must be in 0..9", path);
        return 0;
    }
    *out_ambient = parsed_value;
    return 1;
}

static int ambiance_sheet_range_valid(MapDiagnostics* diag,
                                       const ParsedTileset* tileset,
                                       const MapParticleDefinition* particle,
                                       const char* path) {
    uint64_t end = (uint64_t)(uint32_t)particle->sprite_index +
                   particle->frame_count;
    if (strncmp(particle->sprite_sheet, "builtin:", 8) == 0) {
        unsigned sprite_count = 0u;
        if (strcmp(particle->sprite_sheet, "builtin:tiles") != 0 &&
            strcmp(particle->sprite_sheet, "builtin:sprites") != 0 &&
            strcmp(particle->sprite_sheet, "builtin:misc") != 0 &&
            strcmp(particle->sprite_sheet, "builtin:glyphs") != 0) {
            diag_log(diag, 1,
                     "[data.json][%s.visual.sprite_sheet] error: unknown built-in sheet \"%s\"",
                     path, particle->sprite_sheet);
            return 0;
        }
        if (strcmp(particle->sprite_sheet, "builtin:misc") == 0) {
            sprite_count = 64u;
        } else if (strcmp(particle->sprite_sheet, "builtin:glyphs") == 0) {
            sprite_count = 256u;
        } else {
            sprite_count = 128u;
        }
        if (end > sprite_count) {
            diag_log(diag, 1,
                     "[data.json][%s.visual.sprite_index] error: particle animation exceeds built-in sheet \"%s\" (%u sprites)",
                     path, particle->sprite_sheet, sprite_count);
            return 0;
        }
        return 1;
    }
    if (!tileset) {
        diag_log(diag, 1,
                 "[data.json][%s.visual.sprite_sheet] error: custom particles require a v2 declared sheet",
                 path);
        return 0;
    }
    {
        int match = -1;
        int matches = 0;
        for (int i = 0; i < tileset->sheet_count; i++) {
            if (strcmp(particle->sprite_sheet,
                       tileset->sheets[i].relative_path) == 0) {
                match = i;
                matches++;
            }
        }
        if (matches != 1) {
            diag_log(diag, 1,
                     "[data.json][%s.visual.sprite_sheet] error: \"%s\" must identify one declared sheet",
                     path, particle->sprite_sheet);
            return 0;
        }
        if (end > (uint64_t)tileset->sheets[match].sprite_count) {
            diag_log(diag, 1,
                     "[data.json][%s.visual] error: animation exceeds \"%s\" (%d sprites)",
                     path, particle->sprite_sheet,
                     tileset->sheets[match].sprite_count);
            return 0;
        }
    }
    return 1;
}

static int ambiance_read_range(MapDiagnostics* diag, const JsonValue* value,
                               const char* path, float fallback_min,
                               float fallback_max, MapAmbianceRange* out) {
    static const char* const keys[] = {"min", "max"};
    float minimum = fallback_min;
    float maximum = fallback_max;
    if (!value) {
        out->min_q = (int32_t)lroundf(minimum * MAP_AMBIANCE_FIXED_SCALE);
        out->max_q = (int32_t)lroundf(maximum * MAP_AMBIANCE_FIXED_SCALE);
        return 1;
    }
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected {min,max}", path);
        return 0;
    }
    reject_unknown_keys(diag, value, path, keys, 2);
    if (!json_read_float_range(diag, value, "min", path, fallback_min,
                               -256.0f, 256.0f, &minimum) ||
        !json_read_float_range(diag, value, "max", path, fallback_max,
                               -256.0f, 256.0f, &maximum)) return 0;
    if (minimum > maximum) {
        diag_log(diag, 1,
                 "[data.json][%s] error: min must not exceed max", path);
        return 0;
    }
    out->min_q = (int32_t)lroundf(minimum * MAP_AMBIANCE_FIXED_SCALE);
    out->max_q = (int32_t)lroundf(maximum * MAP_AMBIANCE_FIXED_SCALE);
    return 1;
}

static int ambiance_particle_index(const MapAmbianceCatalog* catalog,
                                    const char* id) {
    for (uint16_t i = 0; catalog && i < catalog->particle_count; i++) {
        if (strcmp(id, catalog->particles[i].id) == 0) return i;
    }
    return -1;
}

static void parse_particle_catalog(MapDiagnostics* diag,
                                   const JsonValue* root,
                                   int format_version,
                                   const ParsedTileset* tileset,
                                   const char* map_id,
                                   MapAmbianceCatalog* catalog) {
    static const char* const particle_keys[] = {
        "id", "name", "visual", "lifetime_ticks", "fade_in_ticks",
        "fade_out_ticks"
    };
    static const char* const visual_keys[] = {
        "sprite_sheet", "sprite_index", "frame_count", "frame_ticks",
        "tint", "scale_x", "scale_y", "end_tint", "end_scale_x",
        "end_scale_y", "start_rotation", "end_rotation", "interpolation"
    };
    static const char* const ambiance_keys[] = {
        "id", "name", "native_ambient", "emitters"
    };
    static const char* const emitter_keys[] = {
        "particle", "count", "area", "velocity_x", "velocity_y",
        "acceleration_x", "acceleration_y", "rotation_speed",
        "particle_layer", "blend", "mirror_with_room", "shape",
        "motion_interpolation"
    };
    static const char* const area_keys[] = {"x", "y", "width", "height"};
    JsonValue* particles = json_object_get(root, "particles");
    JsonValue* ambiances = json_object_get(root, "ambiances");
    uint64_t identity = UINT64_C(1469598103934665603);
    memset(catalog, 0, sizeof(*catalog));
    for (const unsigned char* p = (const unsigned char*)(map_id ? map_id : "");
         *p; p++) {
        identity ^= *p;
        identity *= UINT64_C(1099511628211);
    }
    catalog->identity = identity ? identity : UINT64_C(1);
    if (!particles && !ambiances) return;
    if (format_version != 2) {
        diag_log(diag, 1,
                 "[data.json] error: particles and ambiances require eggnogg-map/v2");
        return;
    }
    if (!particles || particles->type != JSON_ARRAY ||
        particles->u.array_value.count > (int)MAP_AMBIANCE_MAX_PARTICLES) {
        diag_log(diag, 1,
                 "[data.json][particles] error: expected an array with at most %u entries",
                 MAP_AMBIANCE_MAX_PARTICLES);
        return;
    }
    for (int i = 0; i < particles->u.array_value.count; i++) {
        JsonValue* item = particles->u.array_value.items[i];
        MapParticleDefinition* particle = &catalog->particles[i];
        JsonValue* visual;
        char path[96];
        int sprite = 0, frames = 1, frame_ticks = 1;
        int lifetime = 120, fade_in = 0, fade_out = 0;
        float scale_x = 1.0f, scale_y = 1.0f, tint[4], end_tint[4];
        float end_scale_x = 1.0f, end_scale_y = 1.0f;
        float start_rotation = 0.0f, end_rotation = 0.0f;
        snprintf(path, sizeof(path), "particles.%d", i);
        if (!item || item->type != JSON_OBJECT) {
            diag_log(diag, 1, "[data.json][%s] error: expected object", path);
            continue;
        }
        reject_unknown_keys(diag, item, path, particle_keys, 6);
        json_read_bounded_string(diag, item, "id", path, 1,
                                 particle->id, sizeof(particle->id));
        json_read_bounded_string(diag, item, "name", path, 1,
                                 particle->name, sizeof(particle->name));
        json_read_int_range(diag, item, "lifetime_ticks", path, 120, 1,
                            360000, &lifetime);
        json_read_int_range(diag, item, "fade_in_ticks", path, 0, 0,
                            360000, &fade_in);
        json_read_int_range(diag, item, "fade_out_ticks", path, 0, 0,
                            360000, &fade_out);
        particle->lifetime_ticks = (uint32_t)lifetime;
        particle->fade_in_ticks = (uint32_t)fade_in;
        particle->fade_out_ticks = (uint32_t)fade_out;
        visual = json_object_get(item, "visual");
        if (!visual || visual->type != JSON_OBJECT) {
            diag_log(diag, 1,
                     "[data.json][%s.visual] error: expected object", path);
            continue;
        }
        {
            char visual_path[112];
            snprintf(visual_path, sizeof(visual_path), "%s.visual", path);
            reject_unknown_keys(diag, visual, visual_path, visual_keys, 13);
        }
        json_read_bounded_string(diag, visual, "sprite_sheet", path, 1,
                                 particle->sprite_sheet,
                                 sizeof(particle->sprite_sheet));
        json_read_int_range(diag, visual, "sprite_index", path, 0, 0,
                            1000000, &sprite);
        json_read_int_range(diag, visual, "frame_count", path, 1, 1, 256,
                            &frames);
        json_read_int_range(diag, visual, "frame_ticks", path, 1, 1, 3600,
                            &frame_ticks);
        json_read_float_range(diag, visual, "scale_x", path, 1.0f,
                              -64.0f, 64.0f, &scale_x);
        json_read_float_range(diag, visual, "scale_y", path, 1.0f,
                              -64.0f, 64.0f, &scale_y);
        json_read_tint4(diag, visual, path, tint);
        end_scale_x = scale_x;
        end_scale_y = scale_y;
        json_read_float_range(diag, visual, "end_scale_x", path, scale_x,
                              -64.0f, 64.0f, &end_scale_x);
        json_read_float_range(diag, visual, "end_scale_y", path, scale_y,
                              -64.0f, 64.0f, &end_scale_y);
        json_read_float_range(diag, visual, "start_rotation", path, 0.0f,
                              -3600.0f, 3600.0f, &start_rotation);
        json_read_float_range(diag, visual, "end_rotation", path,
                              start_rotation, -3600.0f, 3600.0f,
                              &end_rotation);
        json_read_tint4_named(diag, visual, "end_tint", path, tint, end_tint);
        particle->sprite_index = sprite;
        particle->frame_count = (uint16_t)frames;
        particle->frame_ticks = (uint16_t)frame_ticks;
        particle->scale_x_q = (int32_t)lroundf(scale_x * 256.0f);
        particle->scale_y_q = (int32_t)lroundf(scale_y * 256.0f);
        particle->end_scale_x_q =
            (int32_t)lroundf(end_scale_x * MAP_AMBIANCE_FIXED_SCALE);
        particle->end_scale_y_q =
            (int32_t)lroundf(end_scale_y * MAP_AMBIANCE_FIXED_SCALE);
        particle->start_rotation_q =
            (int32_t)lroundf(start_rotation * MAP_AMBIANCE_FIXED_SCALE);
        particle->end_rotation_q =
            (int32_t)lroundf(end_rotation * MAP_AMBIANCE_FIXED_SCALE);
        particle->rgba = ((uint32_t)lroundf(tint[0] * 255.0f) << 24) |
                         ((uint32_t)lroundf(tint[1] * 255.0f) << 16) |
                         ((uint32_t)lroundf(tint[2] * 255.0f) << 8) |
                         (uint32_t)lroundf(tint[3] * 255.0f);
        particle->end_rgba =
            ((uint32_t)lroundf(end_tint[0] * 255.0f) << 24) |
            ((uint32_t)lroundf(end_tint[1] * 255.0f) << 16) |
            ((uint32_t)lroundf(end_tint[2] * 255.0f) << 8) |
            (uint32_t)lroundf(end_tint[3] * 255.0f);
        if (json_object_get(visual, "end_scale_x") ||
            json_object_get(visual, "end_scale_y")) {
            particle->transition_flags |= MAP_PARTICLE_TRANSITION_SCALE;
        }
        if (json_object_get(visual, "end_tint")) {
            particle->transition_flags |= MAP_PARTICLE_TRANSITION_COLOR;
        }
        if (json_object_get(visual, "start_rotation") ||
            json_object_get(visual, "end_rotation")) {
            particle->transition_flags |= MAP_PARTICLE_TRANSITION_ROTATION;
        }
        {
            JsonValue* interpolation = json_object_get(visual, "interpolation");
            if (!interpolation) {
                particle->interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
            } else if (interpolation->type != JSON_STRING) {
                diag_log(diag, 1,
                         "[data.json][%s.interpolation] error: expected linear, ease_in, ease_out, or ease_in_out",
                         path);
            } else if (strcmp(interpolation->u.string_value, "linear") == 0) {
                particle->interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
            } else if (strcmp(interpolation->u.string_value, "ease_in") == 0) {
                particle->interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN;
            } else if (strcmp(interpolation->u.string_value, "ease_out") == 0) {
                particle->interpolation = MAP_PARTICLE_INTERPOLATION_EASE_OUT;
            } else if (strcmp(interpolation->u.string_value, "ease_in_out") == 0) {
                particle->interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT;
            } else {
                diag_log(diag, 1,
                         "[data.json][%s.interpolation] error: expected linear, ease_in, ease_out, or ease_in_out",
                         path);
            }
        }
        ambiance_sheet_range_valid(diag, tileset, particle, path);
        catalog->particle_count++;
    }
    if (!ambiances || ambiances->type != JSON_ARRAY ||
        ambiances->u.array_value.count > (int)MAP_AMBIANCE_MAX_DEFINITIONS) {
        diag_log(diag, 1,
                 "[data.json][ambiances] error: expected an array with at most %u entries",
                 MAP_AMBIANCE_MAX_DEFINITIONS);
        return;
    }
    for (int i = 0; i < ambiances->u.array_value.count; i++) {
        JsonValue* item = ambiances->u.array_value.items[i];
        MapAmbianceDefinition* ambiance = &catalog->ambiances[i];
        JsonValue* emitters;
        JsonValue* native;
        char path[96];
        int native_ambient = 0;
        snprintf(path, sizeof(path), "ambiances.%d", i);
        if (!item || item->type != JSON_OBJECT) {
            diag_log(diag, 1, "[data.json][%s] error: expected object", path);
            continue;
        }
        reject_unknown_keys(diag, item, path, ambiance_keys, 4);
        json_read_bounded_string(diag, item, "id", path, 1,
                                 ambiance->id, sizeof(ambiance->id));
        json_read_bounded_string(diag, item, "name", path, 1,
                                 ambiance->name, sizeof(ambiance->name));
        if (builtin_ambient_from_text(ambiance->id) >= 0) {
            diag_log(diag, 1,
                     "[data.json][%s.id] error: custom id collides with a native ambient",
                     path);
        }
        native = json_object_get(item, "native_ambient");
        if (native && !ambient_from_value(diag, native, path, NULL,
                                          &native_ambient, NULL, NULL)) {
            native_ambient = 0;
        }
        ambiance->native_ambient = (uint8_t)native_ambient;
        emitters = json_object_get(item, "emitters");
        if (!emitters || emitters->type != JSON_ARRAY ||
            emitters->u.array_value.count > (int)MAP_AMBIANCE_MAX_EMITTERS) {
            diag_log(diag, 1,
                     "[data.json][%s.emitters] error: expected an array with at most %u entries",
                     path, MAP_AMBIANCE_MAX_EMITTERS);
            continue;
        }
        for (int e = 0; e < emitters->u.array_value.count; e++) {
            JsonValue* emitter_value = emitters->u.array_value.items[e];
            MapAmbianceEmitter* emitter = &ambiance->emitters[e];
            JsonValue* area;
            JsonValue* ref;
            JsonValue* blend;
            JsonValue* shape;
            JsonValue* motion_interpolation;
            char emitter_path[128];
            int count = 1, layer = 2;
            int mirror = 1;
            float x = 0, y = 0, width = 528, height = 192;
            float accel_x = 0, accel_y = 0;
            snprintf(emitter_path, sizeof(emitter_path), "%s.emitters.%d",
                     path, e);
            if (!emitter_value || emitter_value->type != JSON_OBJECT) {
                diag_log(diag, 1,
                         "[data.json][%s] error: expected object",
                         emitter_path);
                continue;
            }
            reject_unknown_keys(diag, emitter_value, emitter_path,
                                emitter_keys, 13);
            ref = json_object_get(emitter_value, "particle");
            {
                int particle_index = ref && ref->type == JSON_STRING
                    ? ambiance_particle_index(catalog, ref->u.string_value)
                    : -1;
                if (particle_index < 0) {
                    emitter->particle_index = UINT16_MAX;
                    diag_log(diag, 1,
                             "[data.json][%s.particle] error: unknown particle id",
                             emitter_path);
                } else {
                    emitter->particle_index = (uint16_t)particle_index;
                }
            }
            json_read_int_range(diag, emitter_value, "count", emitter_path,
                                1, 1, MAP_AMBIANCE_MAX_LANES, &count);
            json_read_int_range(diag, emitter_value, "particle_layer",
                                emitter_path, 2, 0, 4, &layer);
            json_read_bool(diag, emitter_value, "mirror_with_room",
                           emitter_path, 1, &mirror);
            area = json_object_get(emitter_value, "area");
            if (!area || area->type != JSON_OBJECT) {
                diag_log(diag, 1,
                         "[data.json][%s.area] error: expected object",
                         emitter_path);
            } else {
                reject_unknown_keys(diag, area, "emitter.area", area_keys, 4);
                json_read_float_range(diag, area, "x", emitter_path, 0,
                                      -4096, 4096, &x);
                json_read_float_range(diag, area, "y", emitter_path, 0,
                                      -4096, 4096, &y);
                json_read_float_range(diag, area, "width", emitter_path, 528,
                                      0, 8192, &width);
                json_read_float_range(diag, area, "height", emitter_path, 192,
                                      0, 8192, &height);
            }
            ambiance_read_range(diag,
                json_object_get(emitter_value, "velocity_x"),
                "emitter.velocity_x", 0, 0, &emitter->velocity_x);
            ambiance_read_range(diag,
                json_object_get(emitter_value, "velocity_y"),
                "emitter.velocity_y", 0, 0, &emitter->velocity_y);
            ambiance_read_range(diag,
                json_object_get(emitter_value, "rotation_speed"),
                "emitter.rotation_speed", 0, 0, &emitter->rotation_speed);
            json_read_float_range(diag, emitter_value, "acceleration_x",
                                  emitter_path, 0, -64, 64, &accel_x);
            json_read_float_range(diag, emitter_value, "acceleration_y",
                                  emitter_path, 0, -64, 64, &accel_y);
            blend = json_object_get(emitter_value, "blend");
            if (!blend) emitter->blend = 0;
            else if (blend->type == JSON_STRING &&
                     strcmp(blend->u.string_value, "alpha") == 0) {
                emitter->blend = 0;
            } else if (blend->type == JSON_STRING &&
                       strcmp(blend->u.string_value, "additive") == 0) {
                emitter->blend = 1;
            } else {
                diag_log(diag, 1,
                         "[data.json][%s.blend] error: expected alpha or additive",
                         emitter_path);
            }
            shape = json_object_get(emitter_value, "shape");
            if (!shape) emitter->shape = MAP_AMBIANCE_SHAPE_RECTANGLE;
            else if (shape->type == JSON_STRING &&
                     strcmp(shape->u.string_value, "rectangle") == 0)
                emitter->shape = MAP_AMBIANCE_SHAPE_RECTANGLE;
            else if (shape->type == JSON_STRING &&
                     strcmp(shape->u.string_value, "ellipse") == 0)
                emitter->shape = MAP_AMBIANCE_SHAPE_ELLIPSE;
            else if (shape->type == JSON_STRING &&
                     strcmp(shape->u.string_value, "line") == 0)
                emitter->shape = MAP_AMBIANCE_SHAPE_LINE;
            else
                diag_log(diag, 1,
                         "[data.json][%s.shape] error: expected rectangle, ellipse, or line",
                         emitter_path);
            motion_interpolation = json_object_get(emitter_value,
                                                   "motion_interpolation");
            if (!motion_interpolation) {
                emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
            } else if (motion_interpolation->type == JSON_STRING &&
                       strcmp(motion_interpolation->u.string_value, "linear") == 0) {
                emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
            } else if (motion_interpolation->type == JSON_STRING &&
                       strcmp(motion_interpolation->u.string_value, "ease_in") == 0) {
                emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN;
            } else if (motion_interpolation->type == JSON_STRING &&
                       strcmp(motion_interpolation->u.string_value, "ease_out") == 0) {
                emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_EASE_OUT;
            } else if (motion_interpolation->type == JSON_STRING &&
                       strcmp(motion_interpolation->u.string_value, "ease_in_out") == 0) {
                emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT;
            } else {
                diag_log(diag, 1,
                         "[data.json][%s.motion_interpolation] error: expected linear, ease_in, ease_out, or ease_in_out",
                         emitter_path);
            }
            emitter->count = (uint16_t)count;
            emitter->particle_layer = (uint8_t)layer;
            emitter->mirror_with_room = (uint8_t)mirror;
            emitter->area_x_q = (int32_t)lroundf(x * 256.0f);
            emitter->area_y_q = (int32_t)lroundf(y * 256.0f);
            emitter->area_width_q = (int32_t)lroundf(width * 256.0f);
            emitter->area_height_q = (int32_t)lroundf(height * 256.0f);
            emitter->acceleration_x_q =
                (int32_t)lroundf(accel_x * 256.0f);
            emitter->acceleration_y_q =
                (int32_t)lroundf(accel_y * 256.0f);
            ambiance->emitter_count++;
        }
        catalog->ambiance_count++;
    }
    {
        char err[160];
        if (!map_ambiance_catalog_validate(catalog, err, sizeof(err))) {
            diag_log(diag, 1, "[data.json][ambiances] error: %s", err);
        }
    }
}

static int color_slot_from_key(const char* key) {
    int i;
    for (i = 0; i < ROOM_COLOR_SLOT_COUNT; i++) {
        if (strcmp(key, color_slot_names[i]) == 0) return i;
    }
    return -1;
}

static void parse_color_triplet(MapDiagnostics* diag, const JsonValue* value, const char* path,
                                ColorFields* fields, int slot) {
    int i;
    if (!value || value->type != JSON_ARRAY || value->u.array_value.count != 3) {
        diag_log(diag, 1, "[data.json][%s] error: colour must be a 3-number array", path);
        return;
    }

    for (i = 0; i < 3; i++) {
        JsonValue* item = value->u.array_value.items[i];
        float channel;
        if (!item || item->type != JSON_NUMBER) {
            diag_log(diag, 1, "[data.json][%s] error: colour channel %d must be numeric", path, i);
            return;
        }
        if (!isfinite(item->u.number_value) || item->u.number_value < 0.0 ||
            item->u.number_value > 1.0) {
            diag_log(diag, 1, "[data.json][%s] error: colour channel %d must be in 0.0..1.0", path, i);
            return;
        }
        channel = (float)item->u.number_value;
        fields->rgb[slot][i] = channel;
    }
    fields->present[slot] = 1;
}

static int appearance_has_duplicate_explicit_colour(const AppearanceOverride* appearance) {
    int slot;
    for (slot = 0; slot < ROOM_COLOR_SLOT_COUNT; slot++) {
        if (appearance->primary.present[slot] && appearance->mirror.present[slot] &&
            appearance->primary.rgb[slot][0] == appearance->mirror.rgb[slot][0] &&
            appearance->primary.rgb[slot][1] == appearance->mirror.rgb[slot][1] &&
            appearance->primary.rgb[slot][2] == appearance->mirror.rgb[slot][2]) {
            return 1;
        }
    }
    return 0;
}

static void parse_color_bank(MapDiagnostics* diag, const JsonValue* value, const char* path, ColorFields* fields) {
    static const char* const known_keys[] = {
        "fg1", "fg2", "bg1", "bg2", "water", "water_hi", "special", "special2"
    };
    JsonMember* member;

    if (!value || value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected object", path);
        return;
    }

    warn_unknown_keys(diag, value, path, known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

    member = value->u.object_value;
    while (member) {
        int slot = color_slot_from_key(member->key);
        if (slot >= 0) {
            char child_path[256];
            snprintf(child_path, sizeof(child_path), "%s.%s", path, member->key);
            parse_color_triplet(diag, member->value, child_path, fields, slot);
        }
        member = member->next;
    }
}

static void parse_appearance(MapDiagnostics* diag, const JsonValue* value, const char* path,
                             AppearanceOverride* appearance) {
    static const char* const known_keys[] = { "primary", "mirror" };
    JsonValue* primary;
    JsonValue* mirror;
    char child_path[320];

    if (!value) return;
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected object", path);
        return;
    }

    appearance->has_appearance = 1;
    warn_unknown_keys(diag, value, path, known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

    primary = json_object_get(value, "primary");
    if (primary) {
        appearance->has_primary_bank = 1;
        snprintf(child_path, sizeof(child_path), "%s.primary", path);
        parse_color_bank(diag, primary, child_path, &appearance->primary);
    }

    mirror = json_object_get(value, "mirror");
    if (mirror) {
        appearance->has_mirror_bank = 1;
        snprintf(child_path, sizeof(child_path), "%s.mirror", path);
        parse_color_bank(diag, mirror, child_path, &appearance->mirror);
    }

    if (appearance->has_primary_bank && appearance->has_mirror_bank &&
        appearance_has_duplicate_explicit_colour(appearance)) {
        diag_log(diag, 0, "[data.json][%s] warning: primary and mirror define duplicate explicit colours", path);
    }
}

static void parse_room_spawn(MapDiagnostics* diag, const JsonValue* value,
                             const char* path, RoomConfig* config) {
    static const char* const spawn_keys[] = { "players", "markers" };
    JsonValue* players;
    JsonValue* markers;
    int i;
    if (!value) return;
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s.spawn] error: expected object", path);
        return;
    }
    warn_unknown_keys(diag, value, "spawn", spawn_keys, 2);
    players = json_object_get(value, "players");
    if (players) {
        if (players->type != JSON_OBJECT) {
            diag_log(diag, 1, "[data.json][%s.spawn.players] error: expected object", path);
        } else {
            JsonMember* member = players->u.object_value;
            while (member) {
                int slot = strcmp(member->key, "1") == 0 ? 0 : strcmp(member->key, "2") == 0 ? 1 : -1;
                JsonValue* point = member->value;
                int x = -1, y = -1, facing = slot == 0 ? 1 : -1;
                if (slot < 0 || !point || point->type != JSON_OBJECT ||
                    !json_number_to_int(json_object_get(point, "x"), &x) ||
                    !json_number_to_int(json_object_get(point, "y"), &y) ||
                    x < 0 || x >= ROOM_VARIABLE_MAX_W || y < 1 || y >= ROOM_VARIABLE_MAX_H) {
                    diag_log(diag, 1, "[data.json][%s.spawn.players.%s] error: expected in-bounds integer x/y", path, member->key);
                } else {
                    JsonValue* direction = json_object_get(point, "facing");
                    if (direction && (direction->type != JSON_STRING ||
                        (strcmp(direction->u.string_value, "left") != 0 && strcmp(direction->u.string_value, "right") != 0))) {
                        diag_log(diag, 1, "[data.json][%s.spawn.players.%s.facing] error: expected left or right", path, member->key);
                    } else {
                        if (direction) facing = strcmp(direction->u.string_value, "left") == 0 ? -1 : 1;
                        config->player_spawn[slot].set = 1;
                        config->player_spawn[slot].x = x;
                        config->player_spawn[slot].y = y;
                        config->player_spawn[slot].facing = facing;
                    }
                }
                member = member->next;
            }
        }
    }
    markers = json_object_get(value, "markers");
    if (!markers) return;
    if (markers->type != JSON_ARRAY) {
        diag_log(diag, 1, "[data.json][%s.spawn.markers] error: expected array", path);
        return;
    }
    config->spawn_markers_set = 1;
    if (markers->u.array_value.count > CUSTOM_MAP_MAX_SPAWN_MARKERS) {
        diag_log(diag, 1, "[data.json][%s.spawn.markers] error: at most %d markers are supported", path, CUSTOM_MAP_MAX_SPAWN_MARKERS);
    }
    for (i = 0; i < markers->u.array_value.count && i < CUSTOM_MAP_MAX_SPAWN_MARKERS; ++i) {
        JsonValue* marker = markers->u.array_value.items[i];
        JsonValue* kind_value;
        int x = -1, y = -1, kind = -1, previous;
        if (!marker || marker->type != JSON_OBJECT ||
            !json_number_to_int(json_object_get(marker, "x"), &x) ||
            !json_number_to_int(json_object_get(marker, "y"), &y) ||
            !(kind_value = json_object_get(marker, "kind")) || kind_value->type != JSON_STRING) {
            diag_log(diag, 1, "[data.json][%s.spawn.markers.%d] error: expected x, y, and kind", path, i);
            continue;
        }
        if (strcmp(kind_value->u.string_value, "deny") == 0) kind = SPAWN_MARKER_DENY;
        else if (strcmp(kind_value->u.string_value, "allow") == 0) kind = SPAWN_MARKER_ALLOW;
        else if (strcmp(kind_value->u.string_value, "allow_p1") == 0) kind = SPAWN_MARKER_ALLOW_P1;
        else if (strcmp(kind_value->u.string_value, "allow_p2") == 0) kind = SPAWN_MARKER_ALLOW_P2;
        if (x < 0 || x >= ROOM_VARIABLE_MAX_W || y < 1 || y >= ROOM_VARIABLE_MAX_H || kind < 0) {
            diag_log(diag, 1, "[data.json][%s.spawn.markers.%d] error: invalid coordinate or marker kind", path, i);
            continue;
        }
        for (previous = 0; previous < config->spawn_marker_count; ++previous) {
            if (config->spawn_markers[previous].x == x && config->spawn_markers[previous].y == y) break;
        }
        if (previous != config->spawn_marker_count) {
            diag_log(diag, 1, "[data.json][%s.spawn.markers.%d] error: duplicate marker cell", path, i);
            continue;
        }
        config->spawn_markers[config->spawn_marker_count].x = (unsigned char)x;
        config->spawn_markers[config->spawn_marker_count].y = (unsigned char)y;
        config->spawn_markers[config->spawn_marker_count].kind = (unsigned char)kind;
        config->spawn_marker_count++;
    }
}

static void parse_room_config(MapDiagnostics* diag, const JsonValue* value,
                              const char* path, RoomConfig* config,
                              const MapAmbianceCatalog* catalog,
                              const ParsedTileset* tileset) {
    static const char* const known_keys[] = { "ambient", "appearance", "hook", "opponent_spawn", "native_tileset", "spawn" };
    JsonValue* ambient_value;
    JsonValue* appearance_value;
    JsonValue* hook_value;

    if (!value) return;
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected object", path);
        return;
    }

    warn_unknown_keys(diag, value, path, known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));
    parse_room_spawn(diag, json_object_get(value, "spawn"), path, config);

    {
        JsonValue* spawn = json_object_get(value, "opponent_spawn");
        if (spawn) {
            int policy = -1;
            if (spawn->type == JSON_STRING) {
                if (strcmp(spawn->u.string_value, "default") == 0) policy = 0;
                else if (strcmp(spawn->u.string_value, "always") == 0) policy = 1;
                else if (strcmp(spawn->u.string_value, "never") == 0) policy = 2;
            }
            if (policy < 0) diag_log(diag, 1,
                "[data.json][%s.opponent_spawn] error: expected default, always, or never", path);
            else { config->opponent_spawn_set = 1; config->opponent_spawn = policy; }
        }
    }

    ambient_value = json_object_get(value, "ambient");
    if (ambient_value) {
        config->ambient_set = ambient_from_value(
            diag, ambient_value, path, catalog, &config->ambient,
            &config->custom_ambiance_set, &config->custom_ambiance);
    }

    {
        JsonValue* native_tileset = json_object_get(value, "native_tileset");
        if (native_tileset && native_tileset->type != JSON_NULL &&
            !(native_tileset->type == JSON_STRING && !native_tileset->u.string_value[0])) {
            int sheet_index = -1;
            if (native_tileset->type != JSON_STRING) {
                diag_log(diag, 1,
                         "[data.json][%s.native_tileset] error: expected a declared PNG sheet name or null",
                         path);
            } else if (!tileset ||
                       (sheet_index = parsed_tileset_find_sheet_path(
                            tileset, native_tileset->u.string_value)) < 0) {
                diag_log(diag, 1,
                         "[data.json][%s.native_tileset] error: \"%s\" must identify one declared external tileset sheet",
                         path, native_tileset->u.string_value);
            } else if (tileset->sheets[sheet_index].sprite_count < 128) {
                diag_log(diag, 1,
                         "[data.json][%s.native_tileset] error: sheet needs at least 128 sprites for native tile actions",
                         path);
            } else {
                config->native_tileset_set = 1;
                snprintf(config->native_tileset_key,
                         sizeof(config->native_tileset_key), "%s",
                         tileset->sheets[sheet_index].key);
                config->native_tileset_sprite_count =
                    tileset->sheets[sheet_index].sprite_count;
            }
        }
    }

    appearance_value = json_object_get(value, "appearance");
    if (appearance_value) {
        char child_path[256];
        snprintf(child_path, sizeof(child_path), "%s.appearance", path);
        parse_appearance(diag, appearance_value, child_path, &config->appearance);
    }

    hook_value = json_object_get(value, "hook");
    if (hook_value && hook_value->type != JSON_NULL) {
        diag_log(diag, 1, "[data.json][%s.hook] error: non-null hook is not supported in v1", path);
    }
}

static void parse_room_instance_overrides(MapDiagnostics* diag,
                                          const JsonValue* value,
                                          const char* path,
                                          RoomConfig* config,
                                          const MapAmbianceCatalog* catalog,
                                          const ParsedTileset* tileset) {
    JsonMember* member;
    if (!value) return;
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected object", path);
        return;
    }
    for (member = value->u.object_value; member; member = member->next) {
        if (strcmp(member->key, "ambient") != 0 &&
            strcmp(member->key, "opponent_spawn") != 0 &&
            strcmp(member->key, "native_tileset") != 0 &&
            strcmp(member->key, "spawn") != 0) {
            diag_log(diag, 1,
                     "[data.json][%s.%s] error: placed-room overrides support ambient, opponent_spawn, native_tileset, and spawn",
                     path, member->key);
        }
    }
    parse_room_config(diag, value, path, config, catalog, tileset);
}

static int find_parsed_room(const ParsedMapFile* parsed_map, const char* room_id) {
    int i;
    for (i = 0; i < parsed_map->room_count; i++) {
        if (strcmp(parsed_map->rooms[i].id, room_id) == 0) return i;
    }
    return -1;
}

static int line_is_comment_or_blank(const char* text) {
    while (*text && isspace((unsigned char)*text)) text++;
    return *text == '\0' || *text == ';' || (*text == '/' && text[1] == '/');
}

static int map_uses_variable_rooms(const JsonValue* root) {
    JsonValue* layout = json_object_get(root, "layout");
    JsonValue* format = layout && layout->type == JSON_OBJECT
        ? json_object_get(layout, "room_format") : NULL;
    return format && format->type == JSON_STRING &&
           strcmp(format->u.string_value, "variable_cells") == 0;
}

static void parse_map_file(MapDiagnostics* diag,
                           const char* text,
                           const ParsedTileset* tileset,
                           int variable_rooms,
                           ParsedMapFile* parsed_map) {
    const char* cur = text;
    int line = 1;
    ParsedMapRoom* current_room = NULL;

    memset(parsed_map, 0, sizeof(*parsed_map));

    while (*cur) {
        char line_buf[512];
        int len = 0;
        const char* trimmed;
        int first_col = 1;

        while (*cur && *cur != '\n' && len < (int)sizeof(line_buf) - 1) {
            if (*cur != '\r') line_buf[len++] = *cur;
            cur++;
        }
        if (*cur == '\n') cur++;
        line_buf[len] = '\0';

        trimmed = line_buf;
        while (*trimmed && isspace((unsigned char)*trimmed)) {
            trimmed++;
            first_col++;
        }

        if (line_is_comment_or_blank(trimmed)) {
            line++;
            continue;
        }

        if (*trimmed == '[') {
            const char* end = strchr(trimmed, ']');
            int id_len;
            int existing_index;
            if (current_room && ((!variable_rooms && current_room->row_count != ROOM_TEMPLATE_H) ||
                    (variable_rooms && (current_room->row_count < ROOM_VARIABLE_MIN_H || current_room->row_count > ROOM_VARIABLE_MAX_H)))) {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: invalid room height %d",
                         current_room->id, line - 1, current_room->row_count);
            }
            if (!end || end[1] != '\0') {
                diag_log(diag, 1, "[data.map][line=%d] error: room header must be [room_id]", line);
                current_room = NULL;
                line++;
                continue;
            }
            id_len = (int)(end - (trimmed + 1));
            if (id_len <= 0) {
                diag_log(diag, 1, "[data.map][line=%d] error: room id cannot be empty", line);
                current_room = NULL;
                line++;
                continue;
            }
            if (parsed_map->room_count >= CUSTOM_MAP_MAX_PARSED_ROOMS) {
                diag_log(diag, 1, "[data.map][line=%d] error: too many room sections; parser cap is %d",
                         line, CUSTOM_MAP_MAX_PARSED_ROOMS);
                current_room = NULL;
                line++;
                continue;
            }

            current_room = &parsed_map->rooms[parsed_map->room_count];
            memset(current_room, 0, sizeof(*current_room));
            current_room->start_line = line;
            if (id_len >= (int)sizeof(current_room->id)) {
                diag_log(diag, 1, "[data.map][line=%d] error: room id is too long", line);
                id_len = (int)sizeof(current_room->id) - 1;
            }
            memcpy(current_room->id, trimmed + 1, (size_t)id_len);
            current_room->id[id_len] = '\0';

            existing_index = find_parsed_room(parsed_map, current_room->id);
            if (existing_index >= 0) {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: duplicate room id", current_room->id, line);
                current_room = NULL;
            } else {
                parsed_map->room_count++;
            }
            line++;
            continue;
        }

        if (*trimmed == '"') {
            const char* close_quote;
            int row_index;
            if (!current_room) {
                diag_log(diag, 1, "[data.map][line=%d] error: room row found before any [room_id] header", line);
                line++;
                continue;
            }
            close_quote = strrchr(trimmed + 1, '"');
            if (!close_quote || close_quote[1] != '\0') {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: room row must be a quoted %d-character string",
                         current_room->id, line, ROOM_TEMPLATE_W);
                line++;
                continue;
            }
            row_index = current_room->row_count;
            if (row_index >= (variable_rooms ? ROOM_VARIABLE_MAX_H : ROOM_TEMPLATE_H)) {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: room has more than %d rows",
                         current_room->id, line, variable_rooms ? ROOM_VARIABLE_MAX_H : ROOM_TEMPLATE_H);
                line++;
                continue;
            }
            {
                int row_width = (int)(close_quote - (trimmed + 1));
                int expected_width = current_room->width ? current_room->width :
                    (variable_rooms ? row_width : ROOM_TEMPLATE_W);
                if ((variable_rooms && (row_width < ROOM_VARIABLE_MIN_W || row_width > ROOM_VARIABLE_MAX_W || row_width != expected_width)) ||
                    (!variable_rooms && row_width != ROOM_TEMPLATE_W)) {
                    diag_log(diag, 1, "[data.map][room=%s][line=%d] error: invalid or inconsistent row width %d",
                             current_room->id, line, row_width);
                    line++;
                    continue;
                }
                current_room->width = expected_width;
            }
            {
                int i;
                for (i = 0; i < current_room->width; i++) {
                    char ch = trimmed[1 + i];
                    int alias_index = parsed_tileset_find_symbol(tileset, ch);
                    int cell_index = row_index * ROOM_VARIABLE_MAX_W + i;
                    if (alias_index >= 0) {
                        current_room->glyphs[cell_index] =
                            tileset->tiles[alias_index].native_glyph;
                        current_room->content_tile[cell_index] =
                            (unsigned char)(alias_index + 1);
                    } else if (glyph_allowed(ch)) {
                        current_room->glyphs[cell_index] = ch;
                    } else {
                        current_room->glyphs[cell_index] = ' ';
                        diag_log(diag, 1,
                                 "[data.map][room=%s][line=%d][col=%d] error: invalid glyph \"%c\"",
                                 current_room->id, line, first_col + 1 + i, ch);
                    }
                }
            }
            current_room->row_count++;
            current_room->height = current_room->row_count;
            line++;
            continue;
        }

        diag_log(diag, 1, "[data.map][line=%d] error: expected [room_id] or quoted row", line);
        line++;
    }

    if (current_room && ((!variable_rooms && current_room->row_count != ROOM_TEMPLATE_H) ||
            (variable_rooms && (current_room->row_count < ROOM_VARIABLE_MIN_H || current_room->row_count > ROOM_VARIABLE_MAX_H)))) {
        diag_log(diag, 1, "[data.map][room=%s][line=%d] error: invalid room height %d",
                 current_room->id, line - 1, current_room->row_count);
    }
}

static void validate_room_glyph_footprints(MapDiagnostics* diag, const ParsedMapFile* parsed_map) {
    int room_index;

    for (room_index = 0; room_index < parsed_map->room_count; room_index++) {
        const ParsedMapRoom* room = &parsed_map->rooms[room_index];
        int row;

        for (row = 0; row < room->height; row++) {
            int col;
            int file_line = room->start_line + 1 + row;

            for (col = 0; col < room->width; col++) {
                char ch = room->glyphs[row * ROOM_VARIABLE_MAX_W + col];

                switch (ch) {
                    case 'G':
                        if (row < 3 || col == 0 || col == room->width - 1) {
                            diag_log(
                                diag, 1,
                                "[data.map][room=%s][line=%d][row=%d][col=%d] error: glyph \"G\" expands to a 3x3 decal and needs 3 tiles of headroom plus 1 tile of side clearance",
                                room->id, file_line, row + 1, col + 1
                            );
                        }
                        break;
                    case 'L':
                    case 'N':
                    case 'Y':
                        if (row < 3 || col == 0 || col == room->width - 1) {
                            diag_log(
                                diag, 1,
                                "[data.map][room=%s][line=%d][row=%d][col=%d] error: glyph \"%c\" expands upward into a 2x3 art block and cannot be placed on the top 3 rows or either side edge",
                                room->id, file_line, row + 1, col + 1, ch
                            );
                        }
                        break;
                    case 'T':
                    case 't':
                    case 's': {
                        int height = ch == 'T' ? 4 : (ch == 't' ? 3 : 2);
                        if (row < height - 1) {
                            diag_log(
                                diag, 1,
                                "[data.map][room=%s][line=%d][row=%d][col=%d] error: glyph \"%c\" expands upward into a %d-tile tentacle and needs %d tiles of headroom",
                                room->id, file_line, row + 1, col + 1,
                                ch, height, height - 1
                            );
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }
}

static int native_reset_spawn_kind(char glyph) {
    if (glyph == 'K') return 1;
    if (glyph == '*') return 2;
    if (glyph == 'm') return 3;
    return 0;
}

static void validate_native_room_spawn_budget(MapDiagnostics* diag,
                                              const ParsedMapFile* parsed_map,
                                              int* out_max_room_spawns,
                                              int* out_k_marker_count) {
    int room_index;
    int max_room_spawns = 0;
    int total_k_markers = 0;

    for (room_index = 0; room_index < parsed_map->room_count; room_index++) {
        const ParsedMapRoom* room = &parsed_map->rooms[room_index];
        int spawn_count = 0;
        int k_count = 0;
        int sword_count = 0;
        int mine_count = 0;
        int row;
        for (row = 0; row < room->height; row++) {
            int col;
            for (col = 0; col < room->width; col++) {
            switch (native_reset_spawn_kind(room->glyphs[row * ROOM_VARIABLE_MAX_W + col])) {
                case 1: k_count++; break;
                case 2: sword_count++; break;
                case 3: mine_count++; break;
                default: break;
            }
            }
        }
        spawn_count = k_count + sword_count + mine_count;
        if (spawn_count > max_room_spawns) max_room_spawns = spawn_count;
        total_k_markers += k_count;
        /* Preserve established mine-only/sword-only rooms: mine allocation
         * checks NULL and sword_new recycles swords. A room containing K is
         * different because spawn_thing_action dereferences its failed type-3
         * allocation. Conservatively count every reset spawner in that room
         * so ordering cannot consume a slot K depends on. */
        if (k_count > 0 && spawn_count > NATIVE_ROOM_RESET_SPAWN_LIMIT) {
            diag_log(
                diag, 1,
                "[data.map][room=%s] error: native room-reset spawn budget is %d/%d "
                "(K hazards=%d, swords=%d, mines=%d); thing_new has %d allocatable "
                "slots after skipping slot zero and two are reserved for players",
                room->id, spawn_count, NATIVE_ROOM_RESET_SPAWN_LIMIT,
                k_count, sword_count, mine_count,
                NATIVE_THING_ALLOCATABLE_SLOTS
            );
        }
    }
    if (out_max_room_spawns) *out_max_room_spawns = max_room_spawns;
    if (out_k_marker_count) *out_k_marker_count = total_k_markers;
}

static int path_join(char* dst, size_t dst_size, const char* a, const char* b) {
    int written = snprintf(dst, dst_size, "%s\\%s", a, b);
    return written > 0 && (size_t)written < dst_size;
}

static void apply_color_fields(float dst[ROOM_COLOR_SLOT_COUNT][3], const ColorFields* fields) {
    int slot;
    for (slot = 0; slot < ROOM_COLOR_SLOT_COUNT; slot++) {
        if (!fields->present[slot]) continue;
        dst[slot][0] = fields->rgb[slot][0];
        dst[slot][1] = fields->rgb[slot][1];
        dst[slot][2] = fields->rgb[slot][2];
    }
}

/* C11's qualifier rules for pointers to multidimensional arrays reject passing
 * roomdef->primary to a const-qualified array parameter under -pedantic (that
 * conversion only became well-defined in C23).  This private byte-copy helper
 * does not write through src; leaving the parameter unqualified keeps strict
 * C11 builds honest without changing behavior. */
static void copy_color_bank(float dst[ROOM_COLOR_SLOT_COUNT][3], float src[ROOM_COLOR_SLOT_COUNT][3]) {
    memcpy(dst, src, sizeof(float) * ROOM_COLOR_SLOT_COUNT * 3);
}

static void engine_roomdef_with_template(const char* room_template) {
    uintptr_t fn = (uintptr_t)ADDR_ROOMDEF;
    __asm__ __volatile__(
        "movl %0, %%eax\n\t"
        "call *%1\n\t"
        :
        : "r"(room_template), "r"(fn)
        : "eax", "ecx", "edx", "cc", "memory"
    );
}

static void apply_room_config(EngineRoomdef* roomdef, const RoomConfig* defaults_config, const RoomConfig* room_config) {
    int defaults_has_appearance = defaults_config->appearance.has_appearance;
    int defaults_has_mirror = defaults_config->appearance.has_mirror_bank;

    if (defaults_config->ambient_set) {
        roomdef->ambient = defaults_config->ambient;
    }
    if (room_config->ambient_set) {
        roomdef->ambient = room_config->ambient;
    }

    if (defaults_has_appearance) {
        apply_color_fields(roomdef->primary, &defaults_config->appearance.primary);
        copy_color_bank(roomdef->mirror, roomdef->primary);
        if (defaults_has_mirror) {
            apply_color_fields(roomdef->mirror, &defaults_config->appearance.mirror);
        }
    }

    if (room_config->appearance.has_appearance) {
        apply_color_fields(roomdef->primary, &room_config->appearance.primary);
        if (room_config->appearance.has_mirror_bank) {
            if (!defaults_has_appearance || !defaults_has_mirror) {
                copy_color_bank(roomdef->mirror, roomdef->primary);
            }
            apply_color_fields(roomdef->mirror, &room_config->appearance.mirror);
        } else {
            copy_color_bank(roomdef->mirror, roomdef->primary);
        }
    }

    roomdef->callback = NULL;
}

static int registry_reserve(CustomMapRegistry* registry, int needed) {
    int new_cap;
    CustomMap* bigger;

    if (!registry) return 0;
    if (needed <= registry->cap) return 1;

    new_cap = registry->cap ? registry->cap * 2 : 4;
    if (new_cap < needed) new_cap = needed;

    bigger = (CustomMap*)realloc(registry->maps, (size_t)new_cap * sizeof(*registry->maps));
    if (!bigger) {
        LOG_ERROR("%s failed to grow custom map registry", MAPS_PREFIX);
        return 0;
    }

    registry->maps = bigger;
    registry->cap = new_cap;
    return 1;
}

static void custom_map_release_payload(CustomMap* map) {
    if(map->content_transaction)content_registry_abort(map->content_transaction);
    free(map->entity_source);free(map->script_source);
    map->content_transaction=NULL;map->entity_source=NULL;map->script_source=NULL;
}
static void free_registry_map_array(CustomMap* maps, int count) {
    int i;
    if (!maps) return;
    for (i = 0; i < count; i++) {
        if (maps[i].content_transaction) {
            content_registry_abort(maps[i].content_transaction);
            maps[i].content_transaction = NULL;
        }
        free(maps[i].entity_source);
        maps[i].entity_source = NULL;
        free(maps[i].script_source);
        maps[i].script_source = NULL;
    }
    free(maps);
}

static void retire_registry_maps(CustomMap* maps, int count) {
    RetiredRegistryBuffer* retired;

    if (!maps) return;

    retired = (RetiredRegistryBuffer*)malloc(sizeof(*retired));
    if (!retired) {
        LOG_WARN("%s failed to track retired map registry; leaking old buffer for safety", MAPS_PREFIX);
        return;
    }

    retired->maps = maps;
    retired->count = count;
    retired->next = g_retired_registry_buffers;
    g_retired_registry_buffers = retired;
}

static void free_retired_registry_maps(void) {
    RetiredRegistryBuffer* retired = g_retired_registry_buffers;
    while (retired) {
        RetiredRegistryBuffer* next = retired->next;
        free_registry_map_array(retired->maps, retired->count);
        free(retired);
        retired = next;
    }
    g_retired_registry_buffers = NULL;
}

/* Registry versions that were parsed and superseded without ever being handed
 * to native roomdefs own no live engine pointers. Free those immediately; at
 * most the one engine-pinned generation may remain retired between matches. */
static void free_unpinned_retired_registry_maps(void) {
    RetiredRegistryBuffer** link = &g_retired_registry_buffers;
    while (*link) {
        RetiredRegistryBuffer* retired = *link;
        if (retired->maps == g_engine_pinned_registry_maps) {
            link = &retired->next;
            continue;
        }
        *link = retired->next;
        free_registry_map_array(retired->maps, retired->count);
        free(retired);
    }
}

static int custom_map_compare(const void* lhs_ptr, const void* rhs_ptr) {
    const CustomMap* lhs = (const CustomMap*)lhs_ptr;
    const CustomMap* rhs = (const CustomMap*)rhs_ptr;
    int cmp;

    if (lhs->sort_order != rhs->sort_order) {
        return (lhs->sort_order < rhs->sort_order) ? -1 : 1;
    }
    cmp = _stricmp(lhs->name, rhs->name);
    if (cmp != 0) return cmp;
    return _stricmp(lhs->id, rhs->id);
}

static int parse_rules(MapDiagnostics* diag, const JsonValue* rules_value, CustomMap* map) {
    static const char* const known_keys[] = {
        "mode", "round_end_rooms", "score_target", "armed_respawn_limit", "eggnogg_color"
    };
    JsonValue* mode_value;
    JsonValue* round_value;
    JsonValue* score_value;
    JsonValue* limit_value;
    JsonValue* eggnogg_value;

    if (!rules_value) return 1;
    if (rules_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][rules] error: expected object");
        return 0;
    }

    warn_unknown_keys(diag, rules_value, "rules", known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

    eggnogg_value = json_object_get(rules_value, "eggnogg_color");
    if (eggnogg_value) {
        ColorFields fields;
        memset(&fields, 0, sizeof(fields));
        parse_color_triplet(diag, eggnogg_value, "rules.eggnogg_color", &fields, 0);
        if (fields.present[0]) {
            map->has_eggnogg_color = 1;
            memcpy(map->eggnogg_color, fields.rgb[0], sizeof(map->eggnogg_color));
        }
    }

    mode_value = json_object_get(rules_value, "mode");
    if (!mode_value) {
        diag_log(diag, 1, "[data.json][rules.mode] error: rules.mode is required when rules exists");
    } else if (mode_value->type != JSON_STRING) {
        diag_log(diag, 1, "[data.json][rules.mode] error: expected string");
    } else if (strcmp(mode_value->u.string_value, "swords") == 0) {
        map->mode = 0;
    } else if (strcmp(mode_value->u.string_value, "karate") == 0) {
        map->mode = 1;
    } else {
        diag_log(diag, 1, "[data.json][rules.mode] error: expected \"swords\" or \"karate\"");
    }

    round_value = json_object_get(rules_value, "round_end_rooms");
    if (round_value) {
        if (round_value->type != JSON_STRING) {
            diag_log(diag, 1, "[data.json][rules.round_end_rooms] error: expected string");
        } else if (strcmp(round_value->u.string_value, "inner_only") == 0) {
            map->round_end_any = 0;
        } else if (strcmp(round_value->u.string_value, "any") == 0) {
            map->round_end_any = 1;
        } else {
            diag_log(diag, 1, "[data.json][rules.round_end_rooms] error: expected \"inner_only\" or \"any\"");
        }
    }

    score_value = json_object_get(rules_value, "score_target");
    if (score_value) {
        int score_target;
        if (score_value->type == JSON_NULL) {
            map->score_target = 0;
        } else if (!json_number_to_int(score_value, &score_target) || score_target <= 0) {
            diag_log(diag, 1, "[data.json][rules.score_target] error: expected null or positive integer");
        } else {
            map->score_target = score_target;
        }
    }

    limit_value = json_object_get(rules_value, "armed_respawn_limit");
    if (limit_value) {
        int limit;
        if (!json_number_to_int(limit_value, &limit) || limit < 0) {
            diag_log(diag, 1, "[data.json][rules.armed_respawn_limit] error: expected non-negative integer");
        } else {
            map->armed_respawn_limit = limit;
        }
    }

    return 1;
}

static void parse_defaults_room(MapDiagnostics* diag,
                                const JsonValue* root_value,
                                RoomConfig* defaults_room,
                                const MapAmbianceCatalog* catalog,
                                const ParsedTileset* tileset) {
    static const char* const defaults_keys[] = { "room" };
    JsonValue* defaults_value = json_object_get(root_value, "defaults");
    JsonValue* room_value;

    if (!defaults_value) return;
    if (defaults_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][defaults] error: expected object");
        return;
    }

    warn_unknown_keys(diag, defaults_value, "defaults", defaults_keys, (int)(sizeof(defaults_keys) / sizeof(defaults_keys[0])));
    room_value = json_object_get(defaults_value, "room");
    if (!room_value) return;
    parse_room_config(diag, room_value, "defaults.room", defaults_room,
                      catalog, tileset);
}

static void parse_room_overrides(MapDiagnostics* diag, const JsonValue* root_value, const ParsedMapFile* parsed_map,
                                 RoomConfig* overrides, unsigned char* override_present,
                                 const MapAmbianceCatalog* catalog,
                                 const ParsedTileset* tileset) {
    JsonValue* rooms_value = json_object_get(root_value, "rooms");
    JsonMember* member;

    if (!rooms_value) return;
    if (rooms_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][rooms] error: expected object");
        return;
    }

    member = rooms_value->u.object_value;
    while (member) {
        int room_index = find_parsed_room(parsed_map, member->key);
        char path[256];
        if (room_index < 0) {
            diag_log(diag, 1, "[data.json][rooms.%s] error: unknown room id", member->key);
            member = member->next;
            continue;
        }
        snprintf(path, sizeof(path), "rooms.%s", member->key);
        override_present[room_index] = 1;
        parse_room_config(diag, member->value, path, &overrides[room_index],
                          catalog, tileset);
        member = member->next;
    }
}

static void copy_layout_source_room(MapDiagnostics* diag,
                                    const ParsedMapFile* parsed_map,
                                    int parsed_index,
                                    CustomMap* map,
                                    const char* path) {
    CustomMapRoom* output;
    int y;
    if (!parsed_map || !map || parsed_index < 0 ||
        parsed_index >= parsed_map->room_count ||
        map->source_room_count >= CUSTOM_MAP_MAX_SOURCE_ROOMS) return;
    output = &map->rooms[map->source_room_count];
    copy_truncated(diag, output->id, sizeof(output->id),
                   parsed_map->rooms[parsed_index].id, path);
    memcpy(output->glyphs, parsed_map->rooms[parsed_index].glyphs,
           ROOM_VARIABLE_MAX_SIZE);
    memcpy(output->content_tile, parsed_map->rooms[parsed_index].content_tile,
           ROOM_VARIABLE_MAX_SIZE);
    output->width = parsed_map->rooms[parsed_index].width;
    output->height = parsed_map->rooms[parsed_index].height;
    memset(output->engine_template, ' ', ROOM_TEMPLATE_SIZE);
    for (y = 0; y < ROOM_TEMPLATE_H && y < output->height; ++y) {
        int x;
        for (x = 0; x < ROOM_TEMPLATE_W && x < output->width; ++x)
            output->engine_template[y * ROOM_TEMPLATE_W + x] =
                output->glyphs[y * ROOM_VARIABLE_MAX_W + x];
    }
    map->source_room_count++;
}

static int room_graph_side_from_json(const JsonValue* value) {
    if (!value || value->type != JSON_STRING) return -1;
    if (strcmp(value->u.string_value, "left") == 0) return ROOM_GRAPH_SIDE_LEFT;
    if (strcmp(value->u.string_value, "right") == 0) return ROOM_GRAPH_SIDE_RIGHT;
    if (strcmp(value->u.string_value, "top") == 0) return ROOM_GRAPH_SIDE_TOP;
    if (strcmp(value->u.string_value, "bottom") == 0) return ROOM_GRAPH_SIDE_BOTTOM;
    return -1;
}

static int room_graph_node_index_by_id(const RoomGraph* graph, const char* id) {
    int index;
    if (!graph || !id) return -1;
    for (index = 0; index < graph->node_count; ++index)
        if (strcmp(graph->nodes[index].id, id) == 0) return index;
    return -1;
}

static int parse_room_graph_layout(MapDiagnostics* diag,
                                   const JsonValue* layout_value,
                                   const ParsedMapFile* parsed_map,
                                   const MapAmbianceCatalog* catalog,
                                   const ParsedTileset* tileset,
                                   CustomMap* map) {
    static const char* const layout_keys[] = {
        "kind", "room_format", "start", "nodes", "connections"
    };
    static const char* const node_keys[] = {
        "id", "room", "x", "y", "mirror_x", "appearance", "overrides"
    };
    static const char* const connection_keys[] = {
        "id", "from", "from_side", "from_offset", "to", "to_side", "to_offset",
        "span", "one_way", "players", "focus"
    };
    JsonValue* format_value = json_object_get(layout_value, "room_format");
    JsonValue* start_value = json_object_get(layout_value, "start");
    JsonValue* nodes_value = json_object_get(layout_value, "nodes");
    JsonValue* connections_value = json_object_get(layout_value, "connections");
    RoomGraphSource sources[CUSTOM_MAP_MAX_SOURCE_ROOMS];
    RoomGraphValidation validation;
    RoomGraph* graph = NULL;
    RoomConfig* node_overrides = NULL;
    int errors_before = diag->error_count;
    int index;

    warn_unknown_keys(diag, layout_value, "layout", layout_keys,
                      (int)(sizeof(layout_keys) / sizeof(layout_keys[0])));
    if (map->format_version != 2)
        diag_log(diag, 1,
                 "[data.json][layout.kind] error: room_graph requires eggnogg-map/v2");
    if (!format_value || format_value->type != JSON_STRING ||
        strcmp(format_value->u.string_value, "variable_cells") != 0) {
        diag_log(diag, 1,
                 "[data.json][layout.room_format] error: room_graph requires \"variable_cells\"");
    }
    map->variable_rooms = 1;
    map->room_graph = 1;
    if (parsed_map->room_count < 1 ||
        parsed_map->room_count > CUSTOM_MAP_MAX_SOURCE_ROOMS) {
        diag_log(diag, 1,
                 "[data.map] error: room_graph requires 1..%d source rooms",
                 CUSTOM_MAP_MAX_SOURCE_ROOMS);
    } else {
        for (index = 0; index < parsed_map->room_count; ++index)
            copy_layout_source_room(diag, parsed_map, index, map,
                                    "layout.nodes.room");
    }

    graph = (RoomGraph*)calloc(1u, sizeof(*graph));
    node_overrides = (RoomConfig*)calloc(ROOM_GRAPH_MAX_NODES,
                                        sizeof(*node_overrides));
    if (!graph || !node_overrides) {
        diag_log(diag, 1,
                 "[data.json][layout] error: out of memory while parsing room_graph");
        free(node_overrides);
        free(graph);
        return 0;
    }
    graph->start_node = -1;
    if (!nodes_value || nodes_value->type != JSON_ARRAY ||
        nodes_value->u.array_value.count < 1 ||
        nodes_value->u.array_value.count > ROOM_GRAPH_MAX_NODES) {
        diag_log(diag, 1,
                 "[data.json][layout.nodes] error: expected an array with 1..%d nodes",
                 ROOM_GRAPH_MAX_NODES);
    } else {
        graph->node_count = nodes_value->u.array_value.count;
        for (index = 0; index < graph->node_count; ++index) {
            JsonValue* value = nodes_value->u.array_value.items[index];
            RoomGraphNode* node = &graph->nodes[index];
            JsonValue* id_value;
            JsonValue* room_value;
            JsonValue* mirror_value;
            JsonValue* appearance_value;
            JsonValue* overrides_value;
            char path[96];
            int parsed_room;
            snprintf(path, sizeof(path), "layout.nodes.%d", index);
            if (!value || value->type != JSON_OBJECT) {
                diag_log(diag, 1, "[data.json][%s] error: expected object", path);
                continue;
            }
            warn_unknown_keys(diag, value, path, node_keys,
                              (int)(sizeof(node_keys) / sizeof(node_keys[0])));
            id_value = json_object_get(value, "id");
            room_value = json_object_get(value, "room");
            if (!id_value || id_value->type != JSON_STRING || !id_value->u.string_value[0] ||
                strlen(id_value->u.string_value) >= sizeof(node->id)) {
                diag_log(diag, 1,
                         "[data.json][%s.id] error: expected a non-empty identifier of at most %d bytes",
                         path, ROOM_GRAPH_ID_CAP - 1);
            } else {
                snprintf(node->id, sizeof(node->id), "%s", id_value->u.string_value);
            }
            if (!room_value || room_value->type != JSON_STRING ||
                (parsed_room = find_parsed_room(parsed_map,
                                                room_value->u.string_value)) < 0) {
                diag_log(diag, 1,
                         "[data.json][%s.room] error: expected a known data.map room id",
                         path);
                node->source_room = -1;
            } else {
                node->source_room = parsed_room;
            }
            if (!json_number_to_int(json_object_get(value, "x"), &node->x) ||
                !json_number_to_int(json_object_get(value, "y"), &node->y))
                diag_log(diag, 1,
                         "[data.json][%s] error: x and y must be whole numbers", path);
            mirror_value = json_object_get(value, "mirror_x");
            if (mirror_value) {
                if (mirror_value->type != JSON_BOOL)
                    diag_log(diag, 1,
                             "[data.json][%s.mirror_x] error: expected boolean", path);
                else node->mirror_x = mirror_value->u.boolean_value;
            }
            appearance_value = json_object_get(value, "appearance");
            if (!appearance_value) {
                node->appearance = ROOM_GRAPH_APPEARANCE_PRIMARY;
            } else if (appearance_value->type != JSON_STRING) {
                diag_log(diag, 1,
                         "[data.json][%s.appearance] error: expected string", path);
                node->appearance = -1;
            } else if (strcmp(appearance_value->u.string_value, "primary") == 0) {
                node->appearance = ROOM_GRAPH_APPEARANCE_PRIMARY;
            } else if (strcmp(appearance_value->u.string_value, "mirror") == 0) {
                node->appearance = ROOM_GRAPH_APPEARANCE_MIRROR;
            } else {
                diag_log(diag, 1,
                         "[data.json][%s.appearance] error: expected \"primary\" or \"mirror\"",
                         path);
                node->appearance = -1;
            }
            overrides_value = json_object_get(value, "overrides");
            if (overrides_value) {
                char override_path[112];
                snprintf(override_path, sizeof(override_path),
                         "%s.overrides", path);
                parse_room_instance_overrides(
                    diag, overrides_value, override_path,
                    &node_overrides[index], catalog, tileset);
            }
        }
    }
    if (!start_value || start_value->type != JSON_STRING ||
        (graph->start_node = room_graph_node_index_by_id(
             graph, start_value->u.string_value)) < 0)
        diag_log(diag, 1,
                 "[data.json][layout.start] error: expected a known node id");

    if (!connections_value || connections_value->type != JSON_ARRAY ||
        connections_value->u.array_value.count > ROOM_GRAPH_MAX_CONNECTIONS) {
        diag_log(diag, 1,
                 "[data.json][layout.connections] error: expected an array with at most %d entries",
                 ROOM_GRAPH_MAX_CONNECTIONS);
    } else {
        graph->connection_count = connections_value->u.array_value.count;
        for (index = 0; index < graph->connection_count; ++index) {
            JsonValue* value = connections_value->u.array_value.items[index];
            RoomGraphConnection* connection = &graph->connections[index];
            JsonValue* from_value;
            JsonValue* to_value;
            JsonValue* one_way_value;
            JsonValue* players_value;
            JsonValue* focus_value;
            char path[96];
            snprintf(path, sizeof(path), "layout.connections.%d", index);
            if (!value || value->type != JSON_OBJECT) {
                diag_log(diag, 1, "[data.json][%s] error: expected object", path);
                connection->from_node = connection->to_node = -1;
                continue;
            }
            warn_unknown_keys(diag, value, path, connection_keys,
                              (int)(sizeof(connection_keys) /
                                    sizeof(connection_keys[0])));
            from_value = json_object_get(value, "from");
            to_value = json_object_get(value, "to");
            connection->from_node = from_value && from_value->type == JSON_STRING
                ? room_graph_node_index_by_id(graph, from_value->u.string_value) : -1;
            connection->to_node = to_value && to_value->type == JSON_STRING
                ? room_graph_node_index_by_id(graph, to_value->u.string_value) : -1;
            connection->from_side = room_graph_side_from_json(
                json_object_get(value, "from_side"));
            connection->to_side = room_graph_side_from_json(
                json_object_get(value, "to_side"));
            if (connection->from_node < 0 || connection->to_node < 0)
                diag_log(diag, 1,
                         "[data.json][%s] error: from and to must name known nodes",
                         path);
            if (connection->from_side < 0 || connection->to_side < 0)
                diag_log(diag, 1,
                         "[data.json][%s] error: sides must be left, right, top, or bottom",
                         path);
            if (!json_number_to_int(json_object_get(value, "from_offset"),
                                    &connection->from_offset) ||
                !json_number_to_int(json_object_get(value, "to_offset"),
                                    &connection->to_offset) ||
                !json_number_to_int(json_object_get(value, "span"),
                                    &connection->span))
                diag_log(diag, 1,
                         "[data.json][%s] error: offsets and span must be whole numbers",
                         path);
            one_way_value = json_object_get(value, "one_way");
            if (one_way_value) {
                if (one_way_value->type != JSON_BOOL)
                    diag_log(diag, 1,
                             "[data.json][%s.one_way] error: expected boolean", path);
                else connection->one_way = one_way_value->u.boolean_value;
            }
            players_value = json_object_get(value, "players");
            if (!players_value) connection->player_policy = ROOM_GRAPH_PLAYERS_BOTH;
            else if (players_value->type != JSON_STRING) {
                diag_log(diag, 1,
                         "[data.json][%s.players] error: expected both, player1, player2, or go",
                         path);
                connection->player_policy = -1;
            } else if (strcmp(players_value->u.string_value, "both") == 0)
                connection->player_policy = ROOM_GRAPH_PLAYERS_BOTH;
            else if (strcmp(players_value->u.string_value, "player1") == 0)
                connection->player_policy = ROOM_GRAPH_PLAYERS_PLAYER1;
            else if (strcmp(players_value->u.string_value, "player2") == 0)
                connection->player_policy = ROOM_GRAPH_PLAYERS_PLAYER2;
            else if (strcmp(players_value->u.string_value, "go") == 0)
                connection->player_policy = ROOM_GRAPH_PLAYERS_GO;
            else {
                diag_log(diag, 1,
                         "[data.json][%s.players] error: expected both, player1, player2, or go",
                         path);
                connection->player_policy = -1;
            }
            focus_value = json_object_get(value, "focus");
            if (!focus_value) connection->focus_policy = ROOM_GRAPH_FOCUS_GO;
            else if (focus_value->type != JSON_STRING) {
                diag_log(diag, 1,
                         "[data.json][%s.focus] error: expected go or crossing", path);
                connection->focus_policy = -1;
            } else if (strcmp(focus_value->u.string_value, "go") == 0)
                connection->focus_policy = ROOM_GRAPH_FOCUS_GO;
            else if (strcmp(focus_value->u.string_value, "crossing") == 0)
                connection->focus_policy = ROOM_GRAPH_FOCUS_CROSSING;
            else {
                diag_log(diag, 1,
                         "[data.json][%s.focus] error: expected go or crossing", path);
                connection->focus_policy = -1;
            }
        }
    }

    memset(sources, 0, sizeof(sources));
    for (index = 0; index < map->source_room_count; ++index) {
        sources[index].width = map->rooms[index].width;
        sources[index].height = map->rooms[index].height;
    }
    if (diag->error_count == errors_before &&
        !room_graph_validate(graph, sources, map->source_room_count, &validation)) {
        diag_log(diag, 1,
                 "[data.json][layout] error: invalid room_graph (%s, node=%d, connection=%d)",
                 room_graph_error_code(validation.first_error),
                 validation.node_index, validation.connection_index);
    }
    if (diag->error_count == errors_before) {
        if (graph->node_count > CUSTOM_MAP_ENGINE_MAX_FINAL_ROOMS)
            diag_log(diag, 1,
                     "[data.json][layout.nodes] error: gameplay supports at most %d placed rooms because the native room-state table has that many entries",
                     CUSTOM_MAP_ENGINE_MAX_FINAL_ROOMS);
        else if (!map->round_end_any)
            diag_log(diag, 1,
                     "[data.json][rules.round_end_rooms] error: room_graph requires \"any\" because a branched layout has no automatic outermost winning room");
        else if (!custom_map_store_resolved_graph(map, graph, &validation))
            diag_log(diag, 1,
                     "[data.json][layout] error: room_graph exceeds compact runtime limits");
        else for (index = 0; index < graph->node_count; ++index)
            map->final_rooms[index].overrides = node_overrides[index];
    }
    free(node_overrides);
    free(graph);
    return diag->error_count == errors_before;
}

static int parse_layout(MapDiagnostics* diag, const JsonValue* root_value,
                        const ParsedMapFile* parsed_map,
                        const MapAmbianceCatalog* catalog,
                        const ParsedTileset* tileset,
                        CustomMap* map) {
    static const char* const known_keys[] = { "kind", "room_format", "order" };
    JsonValue* layout_value = json_object_get(root_value, "layout");
    JsonValue* kind_value;
    JsonValue* format_value;
    JsonValue* order_value;
    unsigned char referenced[CUSTOM_MAP_MAX_PARSED_ROOMS];
    int i;

    memset(referenced, 0, sizeof(referenced));

    if (!layout_value) {
        diag_log(diag, 1, "[data.json][layout] error: missing required object");
        return 0;
    }
    if (layout_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][layout] error: expected object");
        return 0;
    }

    kind_value = json_object_get(layout_value, "kind");
    if (kind_value && kind_value->type == JSON_STRING &&
        strcmp(kind_value->u.string_value, "room_graph") == 0)
        return parse_room_graph_layout(diag, layout_value, parsed_map,
                                       catalog, tileset, map);
    warn_unknown_keys(diag, layout_value, "layout", known_keys,
                      (int)(sizeof(known_keys) / sizeof(known_keys[0])));
    if (!kind_value || kind_value->type != JSON_STRING ||
        strcmp(kind_value->u.string_value, "mirrored_source_rooms") != 0) {
        diag_log(diag, 1, "[data.json][layout.kind] error: expected \"mirrored_source_rooms\"");
    }

    format_value = json_object_get(layout_value, "room_format");
    if (!format_value || format_value->type != JSON_STRING ||
        (strcmp(format_value->u.string_value, "vanilla_33x12") != 0 &&
         strcmp(format_value->u.string_value, "variable_cells") != 0)) {
        diag_log(diag, 1, "[data.json][layout.room_format] error: expected \"vanilla_33x12\" or \"variable_cells\"");
    } else {
        map->variable_rooms = strcmp(format_value->u.string_value, "variable_cells") == 0;
    }

    order_value = json_object_get(layout_value, "order");
    if (!order_value || order_value->type != JSON_ARRAY || order_value->u.array_value.count <= 0) {
        diag_log(diag, 1, "[data.json][layout.order] error: expected non-empty array");
        return 0;
    }
    if (order_value->u.array_value.count > CUSTOM_MAP_MAX_SOURCE_ROOMS) {
        diag_log(diag, 1, "[data.json][layout.order] error: layout.order contains more than %d source rooms",
                 CUSTOM_MAP_MAX_SOURCE_ROOMS);
    }

    for (i = 0; i < order_value->u.array_value.count; i++) {
        JsonValue* item = order_value->u.array_value.items[i];
        int room_index;
        if (!item || item->type != JSON_STRING || !item->u.string_value[0]) {
            diag_log(diag, 1, "[data.json][layout.order] error: each room id must be a non-empty string");
            continue;
        }
        room_index = find_parsed_room(parsed_map, item->u.string_value);
        if (room_index < 0) {
            diag_log(diag, 1, "[data.json][layout.order] error: unknown room id \"%s\"", item->u.string_value);
            continue;
        }
        if (referenced[room_index]) {
            diag_log(diag, 1, "[data.json][layout.order] error: duplicate room id \"%s\"", item->u.string_value);
            continue;
        }
        referenced[room_index] = 1;
        if (map->source_room_count < CUSTOM_MAP_MAX_SOURCE_ROOMS) {
            copy_layout_source_room(diag, parsed_map, room_index, map,
                                    "layout.order");
        }
    }

    for (i = 0; i < parsed_map->room_count; i++) {
        if (!referenced[i]) {
            diag_log(diag, 1, "[data.map][room=%s] error: room exists in data.map but is not referenced by layout.order",
                     parsed_map->rooms[i].id);
        }
    }

    if (map->source_room_count > 0) {
        RoomGraphSource sources[CUSTOM_MAP_MAX_SOURCE_ROOMS];
        RoomGraphValidation validation;
        RoomGraph* graph = (RoomGraph*)calloc(1u, sizeof(*graph));
        memset(sources, 0, sizeof(sources));
        for (i = 0; i < map->source_room_count; ++i) {
            sources[i].width = map->rooms[i].width;
            sources[i].height = map->rooms[i].height;
        }
        if (!graph) {
            diag_log(diag, 1,
                     "[data.json][layout] error: out of memory while resolving final rooms");
        } else if (!room_graph_from_mirrored(sources, map->source_room_count,
                                             graph, &validation)) {
            diag_log(diag, 1,
                     "[data.json][layout] error: mirrored layout could not form a valid room graph (%s, node=%d, connection=%d)",
                     room_graph_error_code(validation.first_error),
                     validation.node_index, validation.connection_index);
        } else {
            if (!custom_map_store_resolved_graph(map, graph, &validation))
                diag_log(diag, 1,
                         "[data.json][layout] error: resolved layout exceeds compact runtime limits");
        }
        free(graph);
    }

    return map->source_room_count > 0;
}

static char validation_room_native_glyph(const CustomMapRoom* room,
                                         const ParsedTileset* tileset,
                                         int x, int y) {
    int index;
    unsigned int alias;
    if (!room || x < 0 || y < 0 || x >= room->width || y >= room->height)
        return '\0';
    index = y * ROOM_VARIABLE_MAX_W + x;
    alias = room->content_tile[index];
    if (tileset && alias > 0 && alias <= (unsigned int)tileset->tile_count)
        return tileset->tiles[alias - 1u].native_glyph;
    return room->glyphs[index];
}

static int validation_room_safe_spawn_floor(const CustomMapRoom* room,
                                            const ParsedTileset* tileset,
                                            int x, int y) {
    char here;
    char above;
    if (!room || x <= 0 || x >= room->width - 1 || y <= 0 ||
        y >= room->height) return 0;
    here = validation_room_native_glyph(room, tileset, x, y);
    above = validation_room_native_glyph(room, tileset, x, y - 1);
    return here == '@' && strchr("!@_Xv12WwmK", above) == NULL;
}

static void validate_room_config_spawns(MapDiagnostics* diag,
                                        const CustomMapRoom* room,
                                        const RoomConfig* config,
                                        const ParsedTileset* tileset,
                                        const char* path) {
    int player, marker;
    if (!room || !config) return;
    for (player = 0; player < 2; ++player) {
        const CustomSpawnPoint* point = &config->player_spawn[player];
        if (!point->set) continue;
        if (point->x >= room->width || point->y >= room->height)
            diag_log(diag, 1, "[data.json][%s.spawn.players.%d] error: point is outside the room",
                     path, player + 1);
        else if (!validation_room_safe_spawn_floor(room, tileset,
                                                   point->x, point->y))
            diag_log(diag, 1, "[data.json][%s.spawn.players.%d] error: start must be on a safe @ floor tile with open space above",
                     path, player + 1);
    }
    for (marker = 0; marker < config->spawn_marker_count; ++marker) {
        const CustomSpawnMarker* point = &config->spawn_markers[marker];
        if (point->x >= room->width || point->y >= room->height)
            diag_log(diag, 1, "[data.json][%s.spawn.markers.%d] error: marker is outside the room",
                     path, marker);
        else if (!validation_room_safe_spawn_floor(room, tileset,
                                                   point->x, point->y))
            diag_log(diag, 1, "[data.json][%s.spawn.markers.%d] error: marker must be on a safe @ floor tile with open space above",
                     path, marker);
    }
}

static int build_custom_map(MapDiagnostics* diag,
                            const ParsedMapFile* parsed_map,
                            const JsonValue* root_value,
                            const char* folder_id,
                            int format_version,
                            ParsedTileset* tileset,
                            CustomMap* out_map) {
    static const char* const known_top_level[] = {
        "format", "id", "name", "author", "description", "sort_order", "rules", "layout", "defaults", "rooms",
        "tileset", "particles", "ambiances"
    };
    RoomConfig overrides[CUSTOM_MAP_MAX_PARSED_ROOMS];
    unsigned char override_present[CUSTOM_MAP_MAX_PARSED_ROOMS];
    int i;

    memset(out_map, 0, sizeof(*out_map));
    copy_truncated(diag, out_map->folder_id, sizeof(out_map->folder_id), folder_id, "folder");
    copy_truncated(diag, out_map->id, sizeof(out_map->id), folder_id, "id");
    out_map->mode = 0;
    out_map->round_end_any = 0;
    out_map->score_target = 0;
    out_map->armed_respawn_limit = 4;
    out_map->format_version = format_version;

    memset(overrides, 0, sizeof(overrides));
    memset(override_present, 0, sizeof(override_present));

    if (!root_value || root_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json] error: root must be an object");
        return 0;
    }

    warn_unknown_keys(diag, root_value, "", known_top_level, (int)(sizeof(known_top_level) / sizeof(known_top_level[0])));

    if (format_version != 1 && format_version != 2) {
        diag_log(diag, 1, "[data.json][format] error: unsupported parsed format version");
    }
    if (format_version == 1 && json_object_get(root_value, "tileset")) {
        diag_log(diag, 1, "[data.json][tileset] error: tileset requires eggnogg-map/v2");
    }

    parse_optional_string(diag, root_value, "id", "data.json", out_map->id, sizeof(out_map->id));
    parse_required_string(diag, root_value, "name", "data.json", out_map->name, sizeof(out_map->name));
    parse_required_string(diag, root_value, "author", "data.json", out_map->author, sizeof(out_map->author));
    parse_optional_string(diag, root_value, "description", "data.json", out_map->description, sizeof(out_map->description));
    parse_integer_field(diag, root_value, "sort_order", "data.json", &out_map->sort_order);

    parse_particle_catalog(diag, root_value, format_version, tileset,
                           out_map->id, &out_map->ambiance_catalog);
    parse_rules(diag, json_object_get(root_value, "rules"), out_map);
    parse_defaults_room(diag, root_value, &out_map->defaults_room,
                        &out_map->ambiance_catalog, tileset);
    parse_room_overrides(diag, root_value, parsed_map, overrides,
                         override_present, &out_map->ambiance_catalog,
                         tileset);
    parse_layout(diag, root_value, parsed_map, &out_map->ambiance_catalog,
                 tileset, out_map);

    if (parsed_map->room_count > CUSTOM_MAP_MAX_SOURCE_ROOMS) {
        diag_log(diag, 1, "[data.map] error: map defines %d source rooms; current loader supports at most %d",
                 parsed_map->room_count, CUSTOM_MAP_MAX_SOURCE_ROOMS);
    }

    for (i = 0; i < out_map->source_room_count; i++) {
        int parsed_index = find_parsed_room(parsed_map, out_map->rooms[i].id);
        if (parsed_index >= 0 && override_present[parsed_index]) {
            char path[160];
            out_map->rooms[i].config = overrides[parsed_index];
            snprintf(path, sizeof(path), "rooms.%s", out_map->rooms[i].id);
            validate_room_config_spawns(diag, &out_map->rooms[i],
                                        &out_map->rooms[i].config, tileset, path);
        }
    }

    if (out_map->room_graph) {
        for (i = 0; i < out_map->final_room_count; ++i) {
            const CustomMapFinalRoom* placed = &out_map->final_rooms[i];
            const RoomConfig* config = &placed->overrides;
            char path[160];
            if (placed->source_room < 0 ||
                placed->source_room >= out_map->source_room_count) continue;
            snprintf(path, sizeof(path), "layout.nodes.%d.overrides", i);
            validate_room_config_spawns(diag,
                                        &out_map->rooms[placed->source_room],
                                        config, tileset, path);
        }
    }

    if (out_map->source_room_count == 1) {
        diag_log(diag, 0, "warning: map has only one source room");
    }

    if (diag->error_count != 0) return 0;
    if (format_version == 2 && tileset) {
        snprintf(out_map->content_owner, sizeof(out_map->content_owner), "%s", tileset->owner);
        out_map->content_tile_count = tileset->tile_count;
        memcpy(out_map->content_tiles, tileset->tiles,
               (size_t)tileset->tile_count * sizeof(out_map->content_tiles[0]));
        out_map->content_sheet_count = tileset->sheet_count;
        memcpy(out_map->content_sheets, tileset->sheets,
               (size_t)tileset->sheet_count * sizeof(out_map->content_sheets[0]));
        snprintf(out_map->default_sheet_key,
                 sizeof(out_map->default_sheet_key), "%s",
                 tileset->default_sheet_key);
        out_map->default_sheet_sprite_count =
            tileset->default_sheet_sprite_count;
        out_map->native_layout = tileset->native_layout;
        out_map->content_transaction = tileset->transaction;
        tileset->transaction = NULL;
    }
    return 1;
}

static int register_custom_map(CustomMapRegistry* registry, const CustomMap* map) {
    int i;
    if (!registry) return 0;
    for (i = 0; i < registry->count; i++) {
        if (_stricmp(registry->maps[i].id, map->id) == 0 ||
            (map->content_owner[0] && registry->maps[i].content_owner[0] &&
             _stricmp(registry->maps[i].content_owner, map->content_owner) == 0)) {
            return 0;
        }
    }
    if (!registry_reserve(registry, registry->count + 1)) return 0;
    registry->maps[registry->count++] = *map;
    return 1;
}

static void scan_map_folder(CustomMapRegistry* registry, const WIN32_FIND_DATAA* fd, const char* relative_folder) {
    char folder_path[MAX_PATH];
    char json_path[MAX_PATH];
    char map_path[MAX_PATH];
    char* json_text = NULL;
    char* map_text = NULL;
    JsonValue* json_root = NULL;
    ParsedMapFile parsed_map;
    ParsedTileset parsed_tileset;
    CustomMap custom_map;
    MapDiagnostics diag;
    char content_owner[CONTENT_OWNER_MAX];
    int format_version = 0;
    int max_native_room_spawns = 0;
    int native_k_marker_count = 0;

    memset(&diag, 0, sizeof(diag));
    memset(&parsed_tileset, 0, sizeof(parsed_tileset));
    memset(&custom_map, 0, sizeof(custom_map));
    memset(content_owner, 0, sizeof(content_owner));
    diag.map_id = fd->cFileName;

    if (!path_join(folder_path, sizeof(folder_path), "maps", relative_folder) ||
        !path_join(json_path, sizeof(json_path), folder_path, "data.json") ||
        !path_join(map_path, sizeof(map_path), folder_path, "data.map")) {
        LOG_ERROR("%s[%s] path too long; skipping", MAPS_PREFIX, fd->cFileName);
        return;
    }

    map_info(fd->cFileName, "scanning folder: %s", folder_path);
    map_info(fd->cFileName, "loading json: %s", json_path);
    if (!read_text_file(json_path, &json_text)) {
        diag_log(&diag, 1, "missing or unreadable file: %s", json_path);
        goto cleanup;
    }

    map_info(fd->cFileName, "loading map: %s", map_path);
    if (!read_text_file(map_path, &map_text)) {
        diag_log(&diag, 1, "missing or unreadable file: %s", map_path);
        goto cleanup;
    }

    json_root = json_parse_document(&diag, "data.json", json_text);
    if (!json_root) goto cleanup;

    if (!parse_map_format_and_owner(&diag, json_root, fd->cFileName,
                                    &format_version, content_owner)) {
        goto cleanup;
    }
    if (format_version == 2 &&
        !parse_v2_tileset(&diag, json_root, folder_path, content_owner,
                          &parsed_tileset)) {
        goto cleanup;
    }

    parse_map_file(&diag, map_text,
                   format_version == 2 ? &parsed_tileset : NULL,
                   map_uses_variable_rooms(json_root),
                   &parsed_map);
    validate_room_glyph_footprints(&diag, &parsed_map);
    validate_native_room_spawn_budget(&diag, &parsed_map,
                                      &max_native_room_spawns,
                                      &native_k_marker_count);
    if (diag.error_count != 0) goto cleanup;

    if (!build_custom_map(&diag, &parsed_map, json_root, fd->cFileName,
                          format_version, &parsed_tileset, &custom_map)) {
        goto cleanup;
    }
    custom_map.max_native_room_spawns = max_native_room_spawns;
    custom_map.native_k_marker_count = native_k_marker_count;
    if (!custom_map_attach_optional_script(&diag, folder_path, &custom_map)) {
        goto cleanup;
    }

    {
        char id_lower[CUSTOM_MAP_MAX_ID];
        copy_lower_ascii(id_lower, sizeof(id_lower), custom_map.id[0] ? custom_map.id : custom_map.folder_id);
        if (!map_package_signature(json_text, map_text, &custom_map,
                                   custom_map.online_sig)) {
            diag_log(&diag, 1, "could not derive the online package identity");
            goto cleanup;
        }
        snprintf(custom_map.online_key, sizeof(custom_map.online_key), "custom:%s:%s", id_lower, custom_map.online_sig);
    }

    /* Keep the physical relative path separate from the leaf-name ID default. */
    snprintf(custom_map.folder_id, sizeof(custom_map.folder_id), "%s", relative_folder);
    if (!register_custom_map(registry, &custom_map)) {
        diag_log(&diag, 1, "failed to register custom map (duplicate id/content namespace or out of memory)");
        goto cleanup;
    }
    custom_map.content_transaction = NULL;
    custom_map.script_source = NULL;
    custom_map.entity_source = NULL;

    if (custom_map.native_k_marker_count > 0) {
        map_info(custom_map.id,
                 "registered: name=\"%s\" author=\"%s\" source_rooms=%d final_rooms=%d "
                 "mode=%s K-room reset budget<=%d/%d k_markers=%d",
                 custom_map.name, custom_map.author, custom_map.source_room_count,
                 custom_map_final_room_count(&custom_map),
                 custom_map.mode ? "karate" : "swords",
                 custom_map.max_native_room_spawns,
                 NATIVE_ROOM_RESET_SPAWN_LIMIT,
                 custom_map.native_k_marker_count);
    } else {
        map_info(custom_map.id,
                 "registered: name=\"%s\" author=\"%s\" source_rooms=%d final_rooms=%d "
                 "mode=%s native_reset_spawners<=%d k_markers=0",
                 custom_map.name, custom_map.author, custom_map.source_room_count,
                 custom_map_final_room_count(&custom_map),
                 custom_map.mode ? "karate" : "swords",
                 custom_map.max_native_room_spawns);
    }

cleanup:
    if (diag.error_count != 0) {
        if (registry) registry->rejected_count++;
        LOG_ERROR("%s[%s] map rejected: %d errors, %d warnings",
                  MAPS_PREFIX, fd->cFileName, diag.error_count, diag.warning_count);
    }
    parsed_tileset_abort(&parsed_tileset);
    if (custom_map.content_transaction) {
        content_registry_abort(custom_map.content_transaction);
        custom_map.content_transaction = NULL;
    }
    free(custom_map.entity_source);
    custom_map.entity_source = NULL;
    free(custom_map.script_source);
    custom_map.script_source = NULL;
    json_free_value(json_root);
    free(json_text);
    free(map_text);
}

static void scan_visit_folder(void* user,const WIN32_FIND_DATAA* fd,const char* relative,int candidate) {
    if(candidate) scan_map_folder((CustomMapRegistry*)user,fd,relative);
}
static void scan_maps_directory(CustomMapRegistry* registry) {
    MapFolderWalk walk={scan_visit_folder,registry,0,0};
    visit_map_folders(&walk);
    if(walk.limited) LOG_WARN("%s map scan reached its depth, directory-count or path limit",MAPS_PREFIX);
}

static void apply_custom_map(const CustomMap* map) {
    int i;

    if (!map) return;

    p_mapdef_start();
    *g_map_name = map->name;
    *g_map_author = map->author;
    *g_map_mode = map->mode;
    *g_round_end_any = map->round_end_any;
    *g_score_target = map->score_target;
    *g_armed_respawn_limit = map->armed_respawn_limit;

    for (i = 0; i < map->source_room_count; i++) {
        EngineRoomdef* roomdef;
        engine_roomdef_with_template(map->rooms[i].engine_template);
        if (*g_roomdef_count <= 0) continue;
        roomdef = &g_roomdefs[*g_roomdef_count - 1];
        apply_room_config(roomdef, &map->defaults_room, &map->rooms[i].config);
    }
}

static void engine_plot_room_zero(void) {
    uintptr_t fn = (uintptr_t)ADDR_MAPGEN_PLOT_ROOM;
    __asm__ __volatile__(
        "xorl %%eax, %%eax\n\t"
        "xorl %%edx, %%edx\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "pushl $0\n\t"
        "call *%0\n\t"
        "addl $4, %%esp\n\t"
        :
        : "r"(fn)
        : "eax", "ecx", "edx", "cc", "memory"
    );
}

int custom_maps_rebuild_variable_map(int selector) {
    const CustomMap* map = g_engine_pinned_map;
    EngineRoomdef saved_roomdef;
    unsigned char chunk[ROOM_TEMPLATE_SIZE];
    uint32_t* staging = NULL;
    int final_rooms;
    int total_width = 0;
    int max_height = 0;
    int final_room;
    int saved_layer;
    int saved_view_w;
    int saved_view_h;
    if (!map || selector != g_engine_pinned_selector || !map->variable_rooms) return 0;
    if (!p_map_init || !p_map_set_tile_base || !p_map_clear_to || !g_roomdefs ||
        !g_roomdef_count || *g_roomdef_count < 1) return -1;
    final_rooms = custom_map_final_room_count(map);
    total_width = map->layout_width;
    max_height = map->layout_height;
    for (final_room = 0; final_room < final_rooms; final_room++) {
        int source = custom_map_final_source_room(map, final_room);
        if (source < 0 || map->rooms[source].width <= 0 ||
            map->rooms[source].height <= 0) return -1;
    }
    if (total_width <= 0 || max_height <= 0 ||
        (size_t)total_width > SIZE_MAX / (size_t)max_height / sizeof(uint32_t)) return -1;
    staging = (uint32_t*)calloc((size_t)total_width * (size_t)max_height, sizeof(uint32_t));
    if (!staging) return -1;
    saved_roomdef = g_roomdefs[0];
    saved_layer = *(volatile int*)(uintptr_t)ADDR_TILEMAP_LAYER;
    saved_view_w = *(volatile int*)(uintptr_t)ADDR_MAP_VIEW_W;
    saved_view_h = *(volatile int*)(uintptr_t)ADDR_MAP_VIEW_H;

    for (final_room = 0; final_room < final_rooms; final_room++) {
        int source = custom_map_final_source_room(map, final_room);
        const CustomMapRoom* room = &map->rooms[source];
        int mirror = custom_map_final_mirrored(map, final_room);
        int destination_x = map->final_rooms[final_room].x -
            map->layout_bounds_x;
        int destination_y = map->final_rooms[final_room].y -
            map->layout_bounds_y;
        int block_y;
        for (block_y = 0; block_y < room->height; block_y += 9) {
            int block_x;
            for (block_x = 0; block_x < room->width; block_x += 31) {
                int window_x = block_x - 1;
                int window_y = block_y - 3;
                int y;
                memset(chunk, ' ', sizeof(chunk));
                for (y = 0; y < ROOM_TEMPLATE_H; y++) {
                    int source_y = window_y + y;
                    int x;
                    if (source_y < 0 || source_y >= room->height) continue;
                    for (x = 0; x < ROOM_TEMPLATE_W; x++) {
                        int logical_x = window_x + x;
                        int authored_x;
                        if (logical_x < 0 || logical_x >= room->width) continue;
                        authored_x = mirror ? room->width - 1 - logical_x : logical_x;
                        chunk[y * ROOM_TEMPLATE_W + x] =
                            room->glyphs[source_y * ROOM_VARIABLE_MAX_W + authored_x];
                    }
                }
                g_roomdefs[0] = saved_roomdef;
                g_roomdefs[0].template_ptr = (const char*)chunk;
                if (p_map_init(ROOM_TEMPLATE_W, ROOM_TEMPLATE_H) != 1) goto fail;
                p_map_set_tile_base(saved_layer, 16, 16);
                p_map_clear_to(0x14u);
                engine_plot_room_zero();
                {
                    const uint32_t* generated = *(const uint32_t* volatile*)(uintptr_t)ADDR_TILEMAP_DATA;
                    int copy_y;
                    if (!generated) goto fail;
                    for (copy_y = 3; copy_y < ROOM_TEMPLATE_H; copy_y++) {
                        int logical_y = window_y + copy_y;
                        int copy_x;
                        if (logical_y < block_y || logical_y >= room->height) continue;
                        for (copy_x = 1; copy_x < ROOM_TEMPLATE_W - 1; copy_x++) {
                            int logical_x = window_x + copy_x;
                            if (logical_x < block_x || logical_x >= room->width) continue;
                            staging[(size_t)(destination_y + logical_y) * (size_t)total_width +
                                    (size_t)destination_x + (size_t)logical_x] =
                                generated[copy_y * ROOM_TEMPLATE_W + copy_x];
                        }
                    }
                }
            }
        }
    }
    g_roomdefs[0] = saved_roomdef;
    if (p_map_init(total_width, max_height) != 1) goto fail_restored;
    p_map_set_tile_base(saved_layer, 16, 16);
    p_map_clear_to(0x14u);
    memcpy(*(void* volatile*)(uintptr_t)ADDR_TILEMAP_DATA, staging,
           (size_t)total_width * (size_t)max_height * sizeof(uint32_t));
    *(volatile int*)(uintptr_t)ADDR_MAP_VIEW_W = saved_view_w;
    *(volatile int*)(uintptr_t)ADDR_MAP_VIEW_H = saved_view_h;
    *(volatile int*)(uintptr_t)ADDR_ROOM_W = ROOM_TEMPLATE_W;
    *(volatile int*)(uintptr_t)ADDR_MAP_W = total_width;
    *(volatile int*)(uintptr_t)ADDR_MAP_H = max_height;
    *(volatile int*)(uintptr_t)ADDR_ROOM_PIXEL_W = ROOM_TEMPLATE_W * 16;
    free(staging);
    map_info(map->id, "rebuilt variable room tilemap: %dx%d cells across %d final rooms",
             total_width, max_height, final_rooms);
    return 1;

fail:
    g_roomdefs[0] = saved_roomdef;
fail_restored:
    free(staging);
    return -1;
}

static void registry_clear(CustomMapRegistry* registry) {
    if (!registry) return;
    free_registry_map_array(registry->maps, registry->count);
    registry->maps = NULL;
    registry->count = 0;
    registry->cap = 0;
    registry->rejected_count = 0;
}

static int registry_has_content_owner(const CustomMapRegistry* registry,
                                      const char* owner) {
    int i;
    if (!registry || !owner || !owner[0]) return 0;
    for (i = 0; i < registry->count; i++) {
        if (_stricmp(registry->maps[i].content_owner, owner) == 0) return 1;
    }
    return 0;
}

static int registry_contains_folder(const CustomMapRegistry* registry,
                                    const char* folder_id) {
    int i;
    if (!registry || !folder_id) return 0;
    for (i = 0; i < registry->count; i++) {
        if (_stricmp(registry->maps[i].folder_id, folder_id) == 0) return 1;
    }
    return 0;
}

static int registry_missing_previous_package(const CustomMapRegistry* incoming,
                                             const CustomMapRegistry* outgoing) {
    int i;
    if (!incoming || !outgoing) return 0;
    for (i = 0; i < outgoing->count; i++) {
        if (!registry_contains_folder(incoming, outgoing->maps[i].folder_id)) return 1;
    }
    return 0;
}

static int registry_commit_content_reload(CustomMapRegistry* incoming,
                                          const CustomMapRegistry* outgoing) {
    ContentRegistryBatch* batch;
    char err[256];
    int i;
    batch = content_registry_batch_begin(err, sizeof(err));
    if (!batch) {
        LOG_ERROR("%s content reload failed: %s", MAPS_PREFIX, err);
        return 0;
    }
    for (i = 0; i < incoming->count; i++) {
        CustomMap* map = &incoming->maps[i];
        if (!map->content_transaction) continue;
        if (!content_registry_batch_add(batch, map->content_transaction,
                                        err, sizeof(err))) {
            LOG_ERROR("%s[%s] content reload failed: %s", MAPS_PREFIX,
                      map->id, err);
            content_registry_batch_abort(batch);
            content_registry_abort(map->content_transaction);
            map->content_transaction = NULL;
            return 0;
        }
        map->content_transaction = NULL;
    }
    for (i = 0; i < outgoing->count; i++) {
        const CustomMap* old_map = &outgoing->maps[i];
        ContentRegistryTx* removal;
        if (!old_map->content_owner[0] ||
            registry_has_content_owner(incoming, old_map->content_owner)) {
            continue;
        }
        removal = content_registry_begin(old_map->content_owner, err, sizeof(err));
        if (!removal || !content_registry_batch_add(batch, removal, err, sizeof(err))) {
            if (removal) content_registry_abort(removal);
            LOG_ERROR("%s content owner removal failed for %s: %s", MAPS_PREFIX,
                      old_map->content_owner, err);
            content_registry_batch_abort(batch);
            return 0;
        }
    }
    if (!content_registry_batch_commit(batch, err, sizeof(err))) {
        LOG_ERROR("%s atomic content reload failed: %s", MAPS_PREFIX, err);
        content_registry_batch_abort(batch);
        return 0;
    }
    return 1;
}

static void registry_swap_in(CustomMapRegistry* incoming) {
    CustomMap* old_maps;
    int old_count;

    if (!incoming) return;

    old_maps = g_custom_registry.maps;
    old_count = g_custom_registry.count;
    g_custom_registry = *incoming;
    g_custom_maps_generation++;
    incoming->maps = NULL;
    incoming->count = 0;
    incoming->cap = 0;
    incoming->rejected_count = 0;

    if (old_maps == g_engine_pinned_registry_maps) {
        retire_registry_maps(old_maps, old_count);
    } else {
        free_registry_map_array(old_maps, old_count);
    }
    free_unpinned_retired_registry_maps();
}

static void rebuild_custom_map_registry(CustomMapRegistry* registry) {
    if (!registry) return;

    registry->maps = NULL;
    registry->count = 0;
    registry->cap = 0;
    registry->rejected_count = 0;

    scan_maps_directory(registry);
    if (registry->count > 1) {
        qsort(registry->maps, (size_t)registry->count, sizeof(*registry->maps), custom_map_compare);
    }
    if (g_preview_active) {
        if(g_preview_folder[0]) {
            CustomMapRegistry preview={0};WIN32_FIND_DATAA fd;memset(&fd,0,sizeof(fd));
            snprintf(fd.cFileName,sizeof(fd.cFileName),"%s",strrchr(g_preview_folder,'/')+1);
            scan_map_folder(&preview,&fd,g_preview_folder);
            if(preview.count!=1){registry->rejected_count++;registry_clear(&preview);return;}
            CustomMap candidate=preview.maps[0];free(preview.maps);
            /* Preview keeps authored namespaces. Replace a matching installed
             * map in this temporary registry, never rewrite its script keys. */
            for(int i=registry->count-1;i>=0;i--)if(!_stricmp(registry->maps[i].id,candidate.id)||
                (candidate.content_owner[0]&&!_stricmp(registry->maps[i].content_owner,candidate.content_owner))) {
                custom_map_release_payload(&registry->maps[i]);
                memmove(&registry->maps[i],&registry->maps[i+1],(size_t)(registry->count-i-1)*sizeof(CustomMap));registry->count--;
            }
            if(!registry_reserve(registry,registry->count+1)){custom_map_release_payload(&candidate);registry->rejected_count++;return;}
            candidate.is_preview=1;candidate.preview_revision=g_preview_revision;candidate.online_key[0]=0;candidate.online_sig[0]=0;
            registry->maps[registry->count++]=candidate;
        }else {
            if (!registry_reserve(registry, registry->count + 1)) {
                registry->rejected_count++;LOG_ERROR("%s failed to append in-memory preview map", MAPS_PREFIX);return;
            }
            registry->maps[registry->count++] = g_preview_map;
        }
    }

}

static void custom_maps_reload_registry_if_needed(int force_reload) {
    uint64_t signature;

    if (!g_custom_maps_inited) return;

    signature = custom_maps_compute_signature();
    if (!force_reload && signature == g_custom_maps_signature) {
        return;
    }
    if (!force_reload && signature == g_custom_maps_failed_signature) {
        return;
    }

    {
        CustomMapRegistry reloaded = { 0 };
        int old_count = g_custom_registry.count;

        if (force_reload) {
            LOG_INFO("%s scanning maps/ for custom maps", MAPS_PREFIX);
        } else {
            LOG_INFO("%s detected maps/ change; rebuilding registry", MAPS_PREFIX);
        }

        rebuild_custom_map_registry(&reloaded);
        if(g_preview_active&&g_preview_folder[0]&&(!reloaded.count||!reloaded.maps[reloaded.count-1].is_preview||reloaded.maps[reloaded.count-1].preview_revision!=g_preview_revision)) {
            registry_clear(&reloaded);return;
        }

        if (!force_reload && reloaded.rejected_count > 0 &&
            registry_missing_previous_package(&reloaded, &g_custom_registry)) {
            LOG_ERROR("%s reload rejected because a previously valid package is now invalid (%d rejected); keeping last known-good registry",
                      MAPS_PREFIX, reloaded.rejected_count);
            g_custom_maps_failed_signature = signature;
            registry_clear(&reloaded);
            return;
        }
        if (!registry_commit_content_reload(&reloaded, &g_custom_registry)) {
            LOG_ERROR("%s registry reload rejected; keeping previous maps and content", MAPS_PREFIX);
            g_custom_maps_failed_signature = signature;
            registry_clear(&reloaded);
            return;
        }
        registry_swap_in(&reloaded);
        g_custom_maps_signature = signature;
        g_custom_maps_failed_signature = 0;

        LOG_INFO("%s registry %s: %d custom maps (was %d)",
                 MAPS_PREFIX,
                 force_reload ? "ready" : "reloaded",
                 g_custom_registry.count,
                 old_count);
    }
}

void custom_maps_init(void) {
    if (g_custom_maps_inited) return;
    content_registry_init();
    g_custom_maps_inited = 1;
    g_custom_registry.maps = NULL;
    g_custom_registry.count = 0;
    g_custom_registry.cap = 0;
    g_engine_pinned_registry_maps = NULL;
    g_engine_pinned_map = NULL;
    g_engine_pinned_selector = -1;
    g_engine_pinned_generation = 0;
    g_custom_maps_signature = 0;
    g_custom_maps_failed_signature = 0;

    custom_maps_reload_registry_if_needed(1);
}

void custom_maps_shutdown(void) {
    CustomMapRegistry empty = { 0 };
    map_script_deactivate();
    (void)registry_commit_content_reload(&empty, &g_custom_registry);
    registry_clear(&g_custom_registry);
    g_engine_pinned_registry_maps = NULL;
    g_engine_pinned_map = NULL;
    g_engine_pinned_selector = -1;
    g_engine_pinned_generation = 0;
    free_retired_registry_maps();
    g_custom_maps_signature = 0;
    g_custom_maps_failed_signature = 0;
    memset(&g_preview_map, 0, sizeof(g_preview_map));
    g_preview_active = 0;g_preview_folder[0]=0;
    g_preview_revision = 0;
    g_custom_maps_inited = 0;
    g_custom_maps_generation++;
}

void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void)) {
    int selector;
    int total_maps;
    int custom_index;

    if (!orig_mapgen_init) return;
    custom_maps_reload_registry_if_needed(0);

    if (g_custom_registry.count <= 0) {
        orig_mapgen_init();
        g_engine_pinned_registry_maps = NULL;
        g_engine_pinned_map = NULL;
        g_engine_pinned_selector = -1;
        g_engine_pinned_generation = g_custom_maps_generation;
        free_unpinned_retired_registry_maps();
        return;
    }

    total_maps = VANILLA_MAP_COUNT + g_custom_registry.count;
    selector = *g_map_selector;
    selector = positive_mod(selector, total_maps);
    *g_map_selector = selector;

    if (selector < VANILLA_MAP_COUNT) {
        orig_mapgen_init();
        g_engine_pinned_registry_maps = NULL;
        g_engine_pinned_map = NULL;
        g_engine_pinned_selector = selector;
        g_engine_pinned_generation = g_custom_maps_generation;
        free_unpinned_retired_registry_maps();
        return;
    }

    custom_index = selector - VANILLA_MAP_COUNT;
    if (custom_index < 0 || custom_index >= g_custom_registry.count) {
        LOG_ERROR("%s custom selector index out of range: selector=%d custom_index=%d total_custom=%d",
                  MAPS_PREFIX, selector, custom_index, g_custom_registry.count);
        orig_mapgen_init();
        g_engine_pinned_registry_maps = NULL;
        g_engine_pinned_map = NULL;
        g_engine_pinned_selector = -1;
        g_engine_pinned_generation = 0;
        free_unpinned_retired_registry_maps();
        return;
    }

    map_info(g_custom_registry.maps[custom_index].id, "applying custom map for selector index %d", selector);
    apply_custom_map(&g_custom_registry.maps[custom_index]);
    g_engine_pinned_registry_maps = g_custom_registry.maps;
    g_engine_pinned_map = &g_custom_registry.maps[custom_index];
    g_engine_pinned_selector = selector;
    g_engine_pinned_generation = g_custom_maps_generation;
    free_unpinned_retired_registry_maps();
    if (g_engine_pinned_map->has_eggnogg_color) {
        map_info(g_engine_pinned_map->id, "Eggnogg color override RGB=%.6f,%.6f,%.6f",
                 g_engine_pinned_map->eggnogg_color[0],
                 g_engine_pinned_map->eggnogg_color[1],
                 g_engine_pinned_map->eggnogg_color[2]);
    }
}

static int appendf_counted(char* out, size_t out_sz, size_t* pos, const char* fmt, ...) {
    va_list args;
    int n;
    char scratch[1024];
    if (!pos || !fmt) return 0;
    va_start(args, fmt);
    if (out && *pos < out_sz) {
        n = vsnprintf(out + *pos, out_sz - *pos, fmt, args);
    } else {
        n = vsnprintf(scratch, sizeof(scratch), fmt, args);
    }
    va_end(args);
    if (n < 0) return 0;
    *pos += (size_t)n;
    return 1;
}

static void append_json_string(char* out, size_t out_sz, size_t* pos, const char* s) {
    if (!appendf_counted(out, out_sz, pos, "\"")) return;
    for (; s && *s; s++) {
        unsigned char ch = (unsigned char)*s;
        switch (ch) {
            case '\\': appendf_counted(out, out_sz, pos, "\\\\"); break;
            case '"':  appendf_counted(out, out_sz, pos, "\\\""); break;
            case '\b': appendf_counted(out, out_sz, pos, "\\b"); break;
            case '\f': appendf_counted(out, out_sz, pos, "\\f"); break;
            case '\n': appendf_counted(out, out_sz, pos, "\\n"); break;
            case '\r': appendf_counted(out, out_sz, pos, "\\r"); break;
            case '\t': appendf_counted(out, out_sz, pos, "\\t"); break;
            default:
                if (ch < 0x20) appendf_counted(out, out_sz, pos, "\\u%04x", (unsigned int)ch);
                else appendf_counted(out, out_sz, pos, "%c", (int)ch);
                break;
        }
    }
    appendf_counted(out, out_sz, pos, "\"");
}

int custom_maps_build_manifest_json(char* out, size_t out_sz) {
    size_t pos = 0;
    int first = 1;
    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);

    appendf_counted(out, out_sz, &pos, "[");
    for (int i = 0; i < VANILLA_MAP_COUNT; i++) {
        if (!first) appendf_counted(out, out_sz, &pos, ",");
        first = 0;
        appendf_counted(out, out_sz, &pos, "{\"key\":\"vanilla:%d\",\"selector\":%d,\"label\":\"Vanilla %d\",\"kind\":\"vanilla\"}", i, i, i + 1);
    }
    for (int i = 0; i < g_custom_registry.count; i++) {
        const CustomMap* map = &g_custom_registry.maps[i];
        if (map->is_preview || !map->online_key[0]) continue;
        if (!first) appendf_counted(out, out_sz, &pos, ",");
        first = 0;
        appendf_counted(out, out_sz, &pos, "{\"key\":");
        append_json_string(out, out_sz, &pos, map->online_key);
        appendf_counted(out, out_sz, &pos, ",\"selector\":%d,\"label\":", VANILLA_MAP_COUNT + i);
        append_json_string(out, out_sz, &pos, map->name[0] ? map->name : map->id);
        appendf_counted(out, out_sz, &pos, ",\"kind\":\"custom\"}");
    }
    appendf_counted(out, out_sz, &pos, "]");
    if (out && out_sz > 0) {
        if (pos >= out_sz) out[out_sz - 1] = '\0';
        else out[pos] = '\0';
    }
    return (int)pos;
}

int custom_maps_total_selectors(void) {
    return VANILLA_MAP_COUNT + g_custom_registry.count;
}

uint64_t custom_maps_generation(void) {
    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);
    return g_custom_maps_generation;
}

int custom_maps_selector_for_key(const char* key, int* out_selector) {
    const char* p;
    char norm_key[160];
    if (!key || !key[0] || !out_selector) return 0;
    copy_lower_ascii(norm_key, sizeof(norm_key), key);
    if (strncmp(norm_key, "vanilla:", 8) == 0) {
        char* end = NULL;
        long idx = strtol(norm_key + 8, &end, 10);
        if (end && *end == '\0' && idx >= 0 && idx < VANILLA_MAP_COUNT) {
            *out_selector = (int)idx;
            return 1;
        }
    }

    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);
    for (int i = 0; i < g_custom_registry.count; i++) {
        char map_key[160];
        if (g_custom_registry.maps[i].is_preview) continue;
        p = g_custom_registry.maps[i].online_key;
        if (!p[0]) continue;
        copy_lower_ascii(map_key, sizeof(map_key), p);
        if (strcmp(norm_key, map_key) == 0) {
            *out_selector = VANILLA_MAP_COUNT + i;
            return 1;
        }
    }
    return 0;
}

static void custom_maps_set_script_error(char* err, size_t err_cap,
                                         const char* message) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", message ? message : "map script error");
    err[err_cap - 1] = '\0';
}

void custom_maps_deactivate_script(void) {
    map_script_deactivate();
}

int custom_maps_activate_script_for_selector(int selector,
                                             const MapScriptHost* host,
                                             char* err,
                                             size_t err_cap) {
    MapScriptDefinition definition;
    MapScriptTileBinding bindings[CUSTOM_MAP_MAX_CONTENT_TILES];
    char chunk_name[MAX_PATH + 2];
    const CustomMap* map;

    if (err && err_cap) err[0] = '\0';
    if (selector >= 0 && selector < VANILLA_MAP_COUNT) {
        map_script_deactivate();
        return 1;
    }
    /* Do not poll/rebuild here. The native roomdefs and the validated source
     * must come from the exact generation pinned during mapgen_init. */
    if (!g_custom_maps_inited || !g_engine_pinned_registry_maps ||
        !g_engine_pinned_map || selector != g_engine_pinned_selector) {
        map_script_deactivate();
        custom_maps_set_script_error(err, err_cap,
                                     "selected map is not pinned for script activation");
        return 0;
    }
    map = g_engine_pinned_map;
    if (map->script_id == 0 || !map->script_source) {
        map_script_deactivate();
        return 1;
    }
    if (!custom_map_script_definition(map, &definition, bindings,
                                      sizeof(bindings) / sizeof(bindings[0]),
                                      chunk_name, sizeof(chunk_name))) {
        map_script_deactivate();
        custom_maps_set_script_error(err, err_cap,
                                     "pinned map script definition is invalid");
        return 0;
    }
    map_script_deactivate();
    if (!(map->entity_source
            ? map_script_activate_content(&definition, host, (const char*)map->entity_source,
                                          map->entity_size, err, err_cap)
            : map_script_activate(&definition, host, err, err_cap))) {
        map_script_deactivate();
        if (err && err_cap && !err[0]) {
            custom_maps_set_script_error(err, err_cap,
                                         "map script activation failed");
        }
        return 0;
    }
    return 1;
}

/* A symmetrical graph has one centre room and paired source rooms in its
 * placed-room order. Check the entire structure, not generated node names:
 * those are editable and are not gameplay metadata. */
static int custom_map_is_symmetrical_outer(const CustomMap* map, int final_room) {
    int middle, leftmost = -1, rightmost = -1, left_count = 0, right_count = 0;
    if (!map || final_room < 0 || final_room >= map->final_room_count ||
        map->source_room_count < 2) return 0;
    if (!map->room_graph)
        return custom_map_final_source_room(map, final_room) ==
               map->source_room_count - 1;
    /* Room designs can be unused, and placed nodes can be reordered without
     * changing the symmetrical arrangement. Identify the paired endpoints by
     * geometry and source identity instead of assuming array positions. */
    if ((map->final_room_count & 1) == 0 || map->final_room_count < 3)
        return 0;
    /* The start room is an author choice; the unpaired room design identifies
     * the symmetry centre even when the countdown begins elsewhere. */
    middle = -1;
    for (int i = 0; i < map->final_room_count; ++i) {
        int copies = 0;
        for (int j = 0; j < map->final_room_count; ++j)
            if (map->final_rooms[j].source_room ==
                map->final_rooms[i].source_room) ++copies;
        if (copies == 1) {
            if (middle >= 0) return 0;
            middle = i;
        }
    }
    if (middle < 0) return 0;
    for (int i = 0; i < map->final_room_count; ++i) {
        const CustomMapFinalRoom* node = &map->final_rooms[i];
        const CustomMapFinalRoom* center = &map->final_rooms[middle];
        int partner = -1;
        if (i == middle) continue;
        if (node->source_room == center->source_room || node->x == center->x)
            return 0;
        if (node->x < center->x) {
            ++left_count;
            if (leftmost < 0 || node->x < map->final_rooms[leftmost].x)
                leftmost = i;
        } else {
            ++right_count;
            if (rightmost < 0 || node->x > map->final_rooms[rightmost].x)
                rightmost = i;
        }
        for (int j = 0; j < map->final_room_count; ++j) {
            const CustomMapFinalRoom* other = &map->final_rooms[j];
            if (j == i || j == middle ||
                (other->x < center->x) == (node->x < center->x) ||
                other->source_room != node->source_room ||
                other->mirror_x == node->mirror_x ||
                other->y != node->y) continue;
            if (partner >= 0) return 0;
            partner = j;
        }
        if (partner < 0) return 0;
    }
    return left_count == right_count &&
           (final_room == leftmost || final_room == rightmost) &&
           map->final_rooms[leftmost].source_room ==
               map->final_rooms[rightmost].source_room;
}

static int custom_map_room_opponent_spawn(const CustomMap* map, int source_room) {
    if (!map || source_room < 0 || source_room >= map->source_room_count) return 0;
    if (map->rooms[source_room].config.opponent_spawn_set)
        return map->rooms[source_room].config.opponent_spawn;
    return map->defaults_room.opponent_spawn_set ? map->defaults_room.opponent_spawn : 0;
}

static int custom_map_final_opponent_spawn(const CustomMap* map, int final_room) {
    int policy;
    int source_room = custom_map_final_source_room(map, final_room);
    if (source_room < 0) return 0;
    if (map->room_graph &&
        map->final_rooms[final_room].overrides.opponent_spawn_set)
        policy = map->final_rooms[final_room].overrides.opponent_spawn;
    else
        policy = custom_map_room_opponent_spawn(map, source_room);
    /* "default" in a symmetrical goal room means the traditional native
     * no-opponent rule. An explicit Always can still opt into combat there. */
    return policy == 0 && custom_map_is_symmetrical_outer(map, final_room)
        ? 2 : policy;
}

int custom_maps_opponent_spawn_policy(int selector, int final_room) {
    const CustomMap* map = g_engine_pinned_map;
    if (selector < VANILLA_MAP_COUNT || !g_custom_maps_inited ||
        !g_engine_pinned_registry_maps || !map || selector != g_engine_pinned_selector)
        return 0;
    return custom_map_final_opponent_spawn(map, final_room);
}

int custom_maps_pinned_ambiance(int selector, int final_room,
                                const MapAmbianceCatalog** out_catalog,
                                uint16_t* out_ambiance_index,
                                int* out_source_room,
                                int* out_mirror_room) {
    const CustomMap* map = g_engine_pinned_map;
    const RoomConfig* room;
    const RoomConfig* selected;
    int source_room;
    if (out_catalog) *out_catalog = NULL;
    if (out_ambiance_index) *out_ambiance_index = 0;
    if (out_source_room) *out_source_room = -1;
    if (out_mirror_room) *out_mirror_room = 0;
    if (!out_catalog || !out_ambiance_index || !out_source_room ||
        !out_mirror_room || selector < VANILLA_MAP_COUNT ||
        !g_custom_maps_inited || !g_engine_pinned_registry_maps || !map ||
        selector != g_engine_pinned_selector) return -1;
    source_room = custom_map_final_source_room(map, final_room);
    if (source_room < 0) {
        return -1;
    }
    room = &map->rooms[source_room].config;
    if (map->room_graph &&
        map->final_rooms[final_room].overrides.ambient_set)
        selected = &map->final_rooms[final_room].overrides;
    else
        selected = room->ambient_set ? room : &map->defaults_room;
    if (!selected->custom_ambiance_set) return 0;
    if (selected->custom_ambiance >= map->ambiance_catalog.ambiance_count) {
        return -1;
    }
    *out_catalog = &map->ambiance_catalog;
    *out_ambiance_index = selected->custom_ambiance;
    *out_source_room = source_room;
    *out_mirror_room = custom_map_final_mirrored(map, final_room);
    return 1;
}

int custom_maps_pinned_native_tileset(int selector, int final_room,
                                      char* out_sheet_key,
                                      size_t out_sheet_key_size,
                                      int* out_sprite_count) {
    const CustomMap* map = g_engine_pinned_map;
    const RoomConfig* room;
    const RoomConfig* selected = NULL;
    int source_room;
    if (out_sheet_key && out_sheet_key_size) out_sheet_key[0] = '\0';
    if (out_sprite_count) *out_sprite_count = 0;
    if (!out_sheet_key || out_sheet_key_size == 0 || !out_sprite_count ||
        selector < VANILLA_MAP_COUNT || !g_custom_maps_inited ||
        !g_engine_pinned_registry_maps || !map ||
        selector != g_engine_pinned_selector) return -1;
    source_room = custom_map_final_source_room(map, final_room);
    if (source_room < 0) {
        return -1;
    }
    room = &map->rooms[source_room].config;
    if (map->room_graph &&
        map->final_rooms[final_room].overrides.native_tileset_set)
        selected = &map->final_rooms[final_room].overrides;
    else if (room->native_tileset_set) selected = room;
    else if (map->defaults_room.native_tileset_set) selected = &map->defaults_room;
    if (selected) {
        if (!selected->native_tileset_key[0] ||
            selected->native_tileset_sprite_count < 128 ||
            strlen(selected->native_tileset_key) >= out_sheet_key_size) return -1;
        snprintf(out_sheet_key, out_sheet_key_size, "%s",
                 selected->native_tileset_key);
        *out_sprite_count = selected->native_tileset_sprite_count;
        return 1;
    }
    if (!map->native_layout) return 0;
    if (!map->default_sheet_key[0] || map->default_sheet_sprite_count < 128 ||
        strlen(map->default_sheet_key) >= out_sheet_key_size) return -1;
    snprintf(out_sheet_key, out_sheet_key_size, "%s", map->default_sheet_key);
    *out_sprite_count = map->default_sheet_sprite_count;
    return 1;
}

int custom_maps_pinned_room_ambient_override(int selector, int final_room,
                                             int* out_ambient) {
    const CustomMap* map = g_engine_pinned_map;
    const RoomConfig* overrides;
    if (out_ambient) *out_ambient = 0;
    if (!out_ambient || selector < VANILLA_MAP_COUNT ||
        !g_custom_maps_inited || !g_engine_pinned_registry_maps || !map ||
        selector != g_engine_pinned_selector) return -1;
    if (!map->room_graph) return 0;
    if (final_room < 0 || final_room >= map->final_room_count) return -1;
    overrides = &map->final_rooms[final_room].overrides;
    if (!overrides->ambient_set) return 0;
    if (overrides->ambient < 0 || overrides->ambient > 9) return -1;
    *out_ambient = overrides->ambient;
    return 1;
}

static void custom_map_content_view_copy(const CustomMap* map,
                                         int selector,
                                         uint64_t generation,
                                         CustomMapContentView* out_view) {
    if (!map || !out_view) return;
    out_view->generation = generation;
    out_view->selector = selector;
    out_view->format_version = map->format_version;
    out_view->source_room_count = map->source_room_count;
    out_view->variable_rooms = map->variable_rooms;
    out_view->room_graph = map->room_graph;
    out_view->final_room_count = custom_map_final_room_count(map);
    out_view->graph_start_room = map->graph_start_room;
    out_view->connection_count = map->connection_count;
    out_view->layout_width = map->layout_width;
    out_view->layout_height = map->layout_height;
    {
        int room;
        for (room = 0; room < map->source_room_count && room < CUSTOM_MAP_MAX_SOURCE_ROOMS; room++) {
            out_view->room_width[room] = map->rooms[room].width;
            out_view->room_height[room] = map->rooms[room].height;
        }
        for (room = 0; room < out_view->final_room_count &&
             room < ROOM_GRAPH_MAX_NODES; ++room) {
            out_view->final_source_room[room] =
                custom_map_final_source_room(map, room);
            out_view->final_x[room] = map->final_rooms[room].x -
                map->layout_bounds_x;
            out_view->final_y[room] = map->final_rooms[room].y -
                map->layout_bounds_y;
            out_view->final_mirror_x[room] =
                custom_map_final_mirrored(map, room);
        }
    }
    out_view->content_tile_count = map->content_tile_count;
    snprintf(out_view->default_sheet_key,
             sizeof(out_view->default_sheet_key), "%s",
             map->default_sheet_key);
    out_view->default_sheet_sprite_count =
        map->default_sheet_sprite_count;
    out_view->native_layout = map->native_layout;
}

int custom_maps_content_view_open(int selector, CustomMapContentView* out_view) {
    int custom_index;
    const CustomMap* map;
    if (!out_view) return -1;
    memset(out_view, 0, sizeof(*out_view));
    out_view->selector = selector;
    if (selector >= 0 && selector < VANILLA_MAP_COUNT) return 0;
    if (selector < 0) return -1;
    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);
    custom_index = selector - VANILLA_MAP_COUNT;
    if (custom_index < 0 || custom_index >= g_custom_registry.count) return -1;
    map = &g_custom_registry.maps[custom_index];
    custom_map_content_view_copy(map, selector, g_custom_maps_generation,
                                 out_view);
    return 1;
}

int custom_maps_pinned_content_view(int selector,
                                    CustomMapContentView* out_view) {
    if (!out_view) return -1;
    memset(out_view, 0, sizeof(*out_view));
    out_view->selector = selector;
    if (selector >= 0 && selector < VANILLA_MAP_COUNT) {
        return 0;
    }
    if (!g_custom_maps_inited || !g_engine_pinned_map ||
        selector != g_engine_pinned_selector ||
        g_engine_pinned_generation == 0) {
        return -1;
    }
    custom_map_content_view_copy(g_engine_pinned_map, selector,
                                 g_engine_pinned_generation, out_view);
    return 1;
}

int custom_maps_pinned_eggnogg_color(int selector, float out_rgb[3]) {
    if (!out_rgb || !g_custom_maps_inited || !g_engine_pinned_map ||
        selector != g_engine_pinned_selector || g_engine_pinned_generation == 0 ||
        !g_engine_pinned_map->has_eggnogg_color) return 0;
    memcpy(out_rgb, g_engine_pinned_map->eggnogg_color, sizeof(float) * 3u);
    return 1;
}

int custom_maps_pinned_script_id(int selector, uint64_t* out_script_id) {
    if (!out_script_id) return -1;
    *out_script_id = 0;
    if (selector >= 0 && selector < VANILLA_MAP_COUNT) return 0;
    if (!g_custom_maps_inited || !g_engine_pinned_map ||
        selector != g_engine_pinned_selector ||
        g_engine_pinned_generation == 0) {
        return -1;
    }
    *out_script_id = g_engine_pinned_map->script_id;
    return 1;
}

int custom_maps_pinned_online_key(int selector, char* out_key, size_t out_key_size) {
    int written;
    if (!out_key || out_key_size == 0) return -1;
    out_key[0] = '\0';
    if (selector >= 0 && selector < VANILLA_MAP_COUNT) {
        written = snprintf(out_key, out_key_size, "vanilla:%d", selector);
        return written >= 0 && (size_t)written < out_key_size ? 1 : -1;
    }
    if (!g_custom_maps_inited || !g_engine_pinned_map ||
        selector != g_engine_pinned_selector ||
        g_engine_pinned_generation == 0 ||
        !g_engine_pinned_map->online_key[0]) {
        return -1;
    }
    written = snprintf(out_key, out_key_size, "%s",
                       g_engine_pinned_map->online_key);
    return written >= 0 && (size_t)written < out_key_size ? 1 : -1;
}

int custom_maps_content_view_cell(const CustomMapContentView* view,
                                  int source_room,
                                  int x,
                                  int y,
                                  char* out_key,
                                  size_t out_key_size,
                                  char* out_native_glyph) {
    const CustomMap* map;
    unsigned int alias_plus_one;
    int custom_index;
    int cell_index;
    if (out_key && out_key_size) out_key[0] = '\0';
    if (out_native_glyph) *out_native_glyph = '\0';
    if (!view || !out_key || out_key_size == 0 ||
        view->selector < VANILLA_MAP_COUNT ||
        source_room < 0 || x < 0 || y < 0) {
        return -1;
    }
    if (!g_custom_maps_inited || view->generation == 0 ||
        view->generation != g_custom_maps_generation) return -1;
    custom_index = view->selector - VANILLA_MAP_COUNT;
    if (custom_index < 0 || custom_index >= g_custom_registry.count) return -1;
    map = &g_custom_registry.maps[custom_index];
    if (view->format_version != map->format_version ||
        view->source_room_count != map->source_room_count ||
        view->content_tile_count != map->content_tile_count ||
        source_room >= map->source_room_count ||
        x >= map->rooms[source_room].width ||
        y >= map->rooms[source_room].height) return -1;
    cell_index = y * ROOM_VARIABLE_MAX_W + x;
    alias_plus_one = map->rooms[source_room].content_tile[cell_index];
    if (alias_plus_one == 0 || alias_plus_one > (unsigned int)map->content_tile_count) return 0;
    snprintf(out_key, out_key_size, "%s",
             map->content_tiles[alias_plus_one - 1u].key);
    if (out_native_glyph) {
        *out_native_glyph = map->content_tiles[alias_plus_one - 1u].native_glyph;
    }
    return 1;
}

int custom_maps_pinned_tile_at_world(int selector,
                                     double world_x,
                                     double world_y,
                                     char* out_reference,
                                     size_t out_reference_size) {
    const CustomMap* map = g_engine_pinned_map;
    int final_room_count;
    int final_room;
    int source_room;
    int room_start = 0;
    int room_top = 0;
    int room_width = 0;
    int local_col;
    int source_col;
    int row;
    int cell_index;
    unsigned int alias_plus_one;
    if (out_reference && out_reference_size) out_reference[0] = '\0';
    if (!out_reference || out_reference_size < 2u || !isfinite(world_x) ||
        !isfinite(world_y) || !g_custom_maps_inited || !map ||
        selector != g_engine_pinned_selector || !g_engine_pinned_generation) {
        return -1;
    }
    if (world_x < 0.0 || world_y < 0.0 || world_x >= 4194304.0) return 0;
    final_room_count = custom_map_final_room_count(map);
    final_room = -1;
    {
        int candidate;
        int pixel_x = (int)floor(world_x);
        int pixel_y = (int)floor(world_y);
        for (candidate = 0; candidate < final_room_count; candidate++) {
            int candidate_source = custom_map_final_source_room(map, candidate);
            int candidate_start = (map->final_rooms[candidate].x -
                map->layout_bounds_x) * 16;
            int candidate_top = (map->final_rooms[candidate].y -
                map->layout_bounds_y) * 16;
            int candidate_width = map->rooms[candidate_source].width * 16;
            int candidate_height = map->rooms[candidate_source].height * 16;
            if (pixel_x >= candidate_start && pixel_x < candidate_start + candidate_width &&
                pixel_y >= candidate_top && pixel_y < candidate_top + candidate_height) {
                final_room = candidate;
                room_start = candidate_start;
                room_top = candidate_top;
                room_width = candidate_width / 16;
                break;
            }
        }
    }
    if (final_room < 0) return 0;
    local_col = (int)floor((world_x - (double)room_start) / 16.0);
    row = (int)floor((world_y - (double)room_top) / 16.0);
    source_room = custom_map_final_source_room(map, final_room);
    if (local_col < 0 || local_col >= room_width || row < 0 ||
        row >= map->rooms[source_room].height) return 0;
    source_col = custom_map_final_mirrored(map, final_room)
        ? room_width - 1 - local_col : local_col;
    cell_index = row * ROOM_VARIABLE_MAX_W + source_col;
    alias_plus_one = map->rooms[source_room].content_tile[cell_index];
    if (alias_plus_one) {
        if (alias_plus_one > (unsigned int)map->content_tile_count) return -1;
        out_reference[0] = map->content_tiles[alias_plus_one - 1u].symbol;
        out_reference[1] = '\0';
    } else {
        out_reference[0] = map->rooms[source_room].glyphs[cell_index];
        out_reference[1] = '\0';
    }
    return 1;
}

int custom_maps_variable_room_bounds(int selector, double world_x,
                                     int* out_final_room,
                                     int* out_start_px,
                                     int* out_width_px,
                                     int* out_height_px) {
    const CustomMap* map = g_engine_pinned_map;
    int final_rooms;
    int room;
    if (!map || selector != g_engine_pinned_selector || !map->variable_rooms) return 0;
    if (!isfinite(world_x)) return -1;
    final_rooms = custom_map_final_room_count(map);
    for (room = 0; room < final_rooms; room++) {
        int source = custom_map_final_source_room(map, room);
        int start = (map->final_rooms[room].x -
            map->layout_bounds_x) * 16;
        int width = map->rooms[source].width * 16;
        if (world_x >= (double)start && world_x < (double)(start + width)) {
            if (out_final_room) *out_final_room = room;
            if (out_start_px) *out_start_px = start;
            if (out_width_px) *out_width_px = width;
            if (out_height_px) *out_height_px = map->rooms[source].height * 16;
            return 1;
        }
    }
    return -1;
}

int custom_maps_variable_room_at(int selector, double world_x, double world_y,
                                 int* out_final_room,
                                 int* out_start_x_px,
                                 int* out_start_y_px,
                                 int* out_width_px,
                                 int* out_height_px) {
    const CustomMap* map = g_engine_pinned_map;
    int room;
    if (!map || selector != g_engine_pinned_selector || !map->variable_rooms)
        return 0;
    if (!isfinite(world_x) || !isfinite(world_y)) return -1;
    for (room = 0; room < custom_map_final_room_count(map); ++room) {
        int source = custom_map_final_source_room(map, room);
        int start_x = (map->final_rooms[room].x - map->layout_bounds_x) * 16;
        int start_y = (map->final_rooms[room].y - map->layout_bounds_y) * 16;
        int width = map->rooms[source].width * 16;
        int height = map->rooms[source].height * 16;
        if (world_x < (double)start_x || world_x >= (double)(start_x + width) ||
            world_y < (double)start_y || world_y >= (double)(start_y + height))
            continue;
        if (out_final_room) *out_final_room = room;
        if (out_start_x_px) *out_start_x_px = start_x;
        if (out_start_y_px) *out_start_y_px = start_y;
        if (out_width_px) *out_width_px = width;
        if (out_height_px) *out_height_px = height;
        return 1;
    }
    return -1;
}

int custom_maps_variable_room_bounds_for_index(int selector, int final_room,
                                               int* out_start_px,
                                               int* out_width_px,
                                               int* out_height_px) {
    const CustomMap* map = g_engine_pinned_map;
    int final_rooms;
    if (!map || selector != g_engine_pinned_selector || !map->variable_rooms) return 0;
    final_rooms = custom_map_final_room_count(map);
    if (final_room < 0 || final_room >= final_rooms) return -1;
    {
        int source = custom_map_final_source_room(map, final_room);
        int start = (map->final_rooms[final_room].x -
            map->layout_bounds_x) * 16;
        if (out_start_px) *out_start_px = start;
        if (out_width_px) *out_width_px = map->rooms[source].width * 16;
        if (out_height_px) *out_height_px = map->rooms[source].height * 16;
    }
    return 1;
}

int custom_maps_variable_room_bounds_2d_for_index(int selector, int final_room,
                                                  int* out_start_x_px,
                                                  int* out_start_y_px,
                                                  int* out_width_px,
                                                  int* out_height_px) {
    const CustomMap* map = g_engine_pinned_map;
    int source;
    if (!map || selector != g_engine_pinned_selector || !map->variable_rooms)
        return 0;
    source = custom_map_final_source_room(map, final_room);
    if (source < 0) return -1;
    if (out_start_x_px) *out_start_x_px =
        (map->final_rooms[final_room].x - map->layout_bounds_x) * 16;
    if (out_start_y_px) *out_start_y_px =
        (map->final_rooms[final_room].y - map->layout_bounds_y) * 16;
    if (out_width_px) *out_width_px = map->rooms[source].width * 16;
    if (out_height_px) *out_height_px = map->rooms[source].height * 16;
    return 1;
}

int custom_maps_variable_room_count(int selector) {
    const CustomMap* map = g_engine_pinned_map;
    if (!map || selector != g_engine_pinned_selector || !map->variable_rooms)
        return 0;
    return custom_map_final_room_count(map);
}

int custom_maps_start_room(int selector) {
    const CustomMap* map = g_engine_pinned_map;
    if (!map || selector != g_engine_pinned_selector) return -1;
    if (map->graph_start_room < 0 ||
        map->graph_start_room >= map->final_room_count) return -1;
    return map->graph_start_room;
}

int custom_maps_uses_room_graph(int selector) {
    const CustomMap* map = g_engine_pinned_map;
    if (!map || selector != g_engine_pinned_selector) return 0;
    return map->room_graph != 0;
}

int custom_maps_pinned_room_definition(int selector, int final_room,
                                        int* out_source_room,
                                        int* out_appearance_mirror) {
    const CustomMap* map = g_engine_pinned_map;
    if (out_source_room) *out_source_room = -1;
    if (out_appearance_mirror) *out_appearance_mirror = 0;
    if (!out_source_room || !out_appearance_mirror ||
        selector < VANILLA_MAP_COUNT || !g_custom_maps_inited ||
        !g_engine_pinned_registry_maps || !map ||
        selector != g_engine_pinned_selector) return -1;
    if (!map->room_graph) return 0;
    if (final_room < 0 || final_room >= map->final_room_count) return -1;
    *out_source_room = custom_map_final_source_room(map, final_room);
    *out_appearance_mirror =
        custom_map_final_appearance_mirrored(map, final_room);
    return *out_source_room >= 0 ? 1 : -1;
}

int custom_maps_room_exit(int selector, int current_room, int side,
                          int edge_offset, int* out_room, int* out_side,
                          int* out_offset, int* out_connection) {
    const CustomMap* map = g_engine_pinned_map;
    int found = 0;
    int index;
    if (out_room) *out_room = -1;
    if (out_side) *out_side = -1;
    if (out_offset) *out_offset = -1;
    if (out_connection) *out_connection = -1;
    if (!map || selector != g_engine_pinned_selector) return 0;
    if (current_room < 0 || current_room >= map->final_room_count ||
        side < ROOM_GRAPH_SIDE_LEFT || side > ROOM_GRAPH_SIDE_BOTTOM ||
        edge_offset < 0 || map->connection_count < 0 ||
        map->connection_count > ROOM_GRAPH_MAX_CONNECTIONS) return -1;
    for (index = 0; index < map->connection_count; ++index) {
        const CustomMapConnection* connection = &map->connections[index];
        int endpoint_offset;
        int destination_room;
        int destination_side;
        int destination_offset;
        if ((int)connection->from_room == current_room &&
            (int)connection->from_side == side) {
            endpoint_offset = connection->from_offset;
            destination_room = connection->to_room;
            destination_side = connection->to_side;
            destination_offset = connection->to_offset;
        } else if (!connection->one_way &&
                   (int)connection->to_room == current_room &&
                   (int)connection->to_side == side) {
            endpoint_offset = connection->to_offset;
            destination_room = connection->from_room;
            destination_side = connection->from_side;
            destination_offset = connection->from_offset;
        } else {
            continue;
        }
        if (edge_offset < endpoint_offset ||
            edge_offset >= endpoint_offset + (int)connection->span) continue;
        if (found) return -1;
        found = 1;
        if (out_room) *out_room = destination_room;
        if (out_side) *out_side = destination_side;
        if (out_offset)
            *out_offset = destination_offset + (edge_offset - endpoint_offset);
        if (out_connection) *out_connection = index;
    }
    return found;
}

int custom_maps_resolve_room_transition(int selector, int current_room,
                                        double old_x, double old_y,
                                        double new_x, double new_y,
                                        int* out_room,
                                        int* out_connection) {
    const CustomMap* map = g_engine_pinned_map;
    int source;
    int start_x;
    int start_y;
    int width;
    int height;
    int side = -1;
    int offset = -1;
    int destination = -1;
    int connection = -1;
    int crossed = 0;
    int candidate_side[4];
    int candidate_offset[4];
    int candidate_count = 0;
    int index;
    if (out_room) *out_room = -1;
    if (out_connection) *out_connection = -1;
    if (!map || selector != g_engine_pinned_selector || !map->room_graph)
        return 0;
    if (current_room < 0 || current_room >= map->final_room_count ||
        !isfinite(old_x) || !isfinite(old_y) || !isfinite(new_x) ||
        !isfinite(new_y)) return -1;
    source = custom_map_final_source_room(map, current_room);
    if (source < 0) return -1;
    start_x = (map->final_rooms[current_room].x - map->layout_bounds_x) * 16;
    start_y = (map->final_rooms[current_room].y - map->layout_bounds_y) * 16;
    width = map->rooms[source].width * 16;
    height = map->rooms[source].height * 16;
    if (old_x < (double)start_x || old_x >= (double)(start_x + width) ||
        old_y < (double)start_y || old_y >= (double)(start_y + height))
        return -1;
    if (new_x < (double)start_x) {
        candidate_side[candidate_count] = ROOM_GRAPH_SIDE_LEFT;
        candidate_offset[candidate_count++] =
            (int)floor((new_y - (double)start_y) / 16.0);
    } else if (new_x >= (double)(start_x + width)) {
        candidate_side[candidate_count] = ROOM_GRAPH_SIDE_RIGHT;
        candidate_offset[candidate_count++] =
            (int)floor((new_y - (double)start_y) / 16.0);
    }
    if (new_y < (double)start_y) {
        candidate_side[candidate_count] = ROOM_GRAPH_SIDE_TOP;
        candidate_offset[candidate_count++] =
            (int)floor((new_x - (double)start_x) / 16.0);
    } else if (new_y >= (double)(start_y + height)) {
        candidate_side[candidate_count] = ROOM_GRAPH_SIDE_BOTTOM;
        candidate_offset[candidate_count++] =
            (int)floor((new_x - (double)start_x) / 16.0);
    }
    for (index = 0; index < candidate_count; ++index) {
        int candidate_room = -1;
        int candidate_connection = -1;
        int result;
        if (candidate_offset[index] < 0) continue;
        result = custom_maps_room_exit(selector, current_room,
                                       candidate_side[index],
                                       candidate_offset[index],
                                       &candidate_room, NULL, NULL,
                                       &candidate_connection);
        if (result < 0) return -1;
        if (result == 0) continue;
        if (crossed) return -1;
        crossed = 1;
        side = candidate_side[index];
        offset = candidate_offset[index];
        destination = candidate_room;
        connection = candidate_connection;
    }
    (void)side;
    (void)offset;
    if (!crossed) return 0;
    if (destination < 0 || destination >= map->final_room_count) return -1;
    if (out_room) *out_room = destination;
    if (out_connection) *out_connection = connection;
    return 1;
}

int custom_maps_room_connection_policy(int selector, int connection,
                                       int* out_players, int* out_focus) {
    const CustomMap* map = g_engine_pinned_map;
    if (out_players) *out_players = ROOM_GRAPH_PLAYERS_BOTH;
    if (out_focus) *out_focus = ROOM_GRAPH_FOCUS_GO;
    if (!map || selector != g_engine_pinned_selector || !map->room_graph)
        return 0;
    if (connection < 0 || connection >= map->connection_count) return -1;
    if (out_players) *out_players = map->connections[connection].player_policy;
    if (out_focus) *out_focus = map->connections[connection].focus_policy;
    return 1;
}

static int custom_spawn_marker_applies(unsigned char kind, int player_index) {
    return kind == SPAWN_MARKER_ALLOW ||
        (kind == SPAWN_MARKER_ALLOW_P1 && player_index == 0) ||
        (kind == SPAWN_MARKER_ALLOW_P2 && player_index == 1);
}

static char custom_room_native_glyph(const CustomMap* map,
                                     const CustomMapRoom* room,
                                     int x, int y) {
    int index = y * ROOM_VARIABLE_MAX_W + x;
    unsigned int alias = room->content_tile[index];
    if (alias > 0 && alias <= (unsigned int)map->content_tile_count)
        return map->content_tiles[alias - 1u].native_glyph;
    return room->glyphs[index];
}

int custom_maps_player_start_position(int selector, int player_index,
                                      float near_x, float* out_x, float* out_y,
                                      int* out_facing) {
    const CustomMap* map = g_engine_pinned_map;
    const CustomMapRoom* room;
    const CustomSpawnPoint* point;
    int final_room = -1, start_px = 0, start_y_px = 0, width_px = 0;
    int source_room, mirror;
    if (!map || selector != g_engine_pinned_selector || player_index < 0 ||
        player_index > 1 || !out_x || !out_y || !isfinite(near_x)) return 0;
    final_room = custom_maps_start_room(selector);
    if (final_room >= 0 && custom_maps_variable_room_bounds_2d_for_index(
            selector, final_room, &start_px, &start_y_px, &width_px, NULL) == 1) {
        /* Authored player starts belong to the graph's stable start instance. */
    } else if (custom_maps_variable_room_bounds(selector, near_x, &final_room,
                                                 &start_px, &width_px, NULL) != 1) {
        width_px = ROOM_TEMPLATE_W * 16;
        final_room = (int)floor((double)near_x / (double)width_px);
        if (final_room < 0 || final_room >= custom_map_final_room_count(map)) return 0;
        start_px = final_room * width_px;
    }
    if (custom_maps_variable_room_bounds_2d_for_index(
            selector, final_room, &start_px, &start_y_px, &width_px, NULL) != 1)
        start_y_px = 0;
    source_room = custom_map_final_source_room(map, final_room);
    mirror = custom_map_final_mirrored(map, final_room);
    room = &map->rooms[source_room];
    point = &room->config.player_spawn[player_index];
    if (map->room_graph &&
        map->final_rooms[final_room].overrides.player_spawn[player_index].set)
        point = &map->final_rooms[final_room].overrides.player_spawn[player_index];
    if (!point->set) return 0;
    *out_x = (float)(start_px + ((mirror ? room->width - 1 - point->x : point->x) + 0.5) * 16.0);
    *out_y = (float)(start_y_px + (point->y - 1) * 16.0);
    if (out_facing) *out_facing = mirror ? -point->facing : point->facing;
    return 1;
}

int custom_maps_adjust_spawn_position(int selector, int final_room,
                                      int player_index,
                                      float* inout_x, float* inout_y) {
    const CustomMap* map = g_engine_pinned_map;
    const CustomMapRoom* room;
    const RoomConfig* config;
    int start_px = 0, start_y_px = 0, width_px = 0, height_px = 0;
    int source_room, mirror, marker, whitelist = 0, best = -1;
    double best_distance = 0.0;
    int native_floor_x, native_floor_y, native_denied = 0;
    if (!map || selector != g_engine_pinned_selector || !inout_x || !inout_y ||
        player_index < 0 || player_index > 1 ||
        !isfinite(*inout_x) || !isfinite(*inout_y)) return 0;
    if (final_room >= 0 && custom_maps_variable_room_bounds_2d_for_index(
            selector, final_room, &start_px, &start_y_px, &width_px,
            &height_px) == 1) {
        /* The caller captured the active room before native fixed-width spawn
         * search could move the player into a wrong 33-cell segment. */
    } else if (custom_maps_variable_room_bounds(selector, *inout_x, &final_room,
                                                 &start_px, &width_px, NULL) != 1) {
        /* Fixed rooms use the same authored marker format. */
        width_px = ROOM_TEMPLATE_W * 16;
        height_px = ROOM_TEMPLATE_H * 16;
        final_room = (int)floor((double)*inout_x / (double)width_px);
        if (final_room < 0 || final_room >= custom_map_final_room_count(map)) return 0;
        start_px = final_room * width_px;
    }
    source_room = custom_map_final_source_room(map, final_room);
    mirror = custom_map_final_mirrored(map, final_room);
    room = &map->rooms[source_room];
    config = &room->config;
    if (map->room_graph &&
        map->final_rooms[final_room].overrides.spawn_markers_set)
        config = &map->final_rooms[final_room].overrides;
    if (config->spawn_marker_count <= 0 && !map->variable_rooms) return 0;

    native_floor_x = (int)floor(((double)*inout_x - start_px) / 16.0);
    if (mirror) native_floor_x = room->width - 1 - native_floor_x;
    native_floor_y = (int)floor(((double)*inout_y - start_y_px) / 16.0) + 1;
    for (marker = 0; marker < config->spawn_marker_count; ++marker) {
        const CustomSpawnMarker* entry = &config->spawn_markers[marker];
        if (custom_spawn_marker_applies(entry->kind, player_index)) whitelist = 1;
        if (entry->kind == SPAWN_MARKER_DENY && entry->x == native_floor_x &&
            entry->y == native_floor_y) native_denied = 1;
    }
    for (marker = 0; marker < config->spawn_marker_count; ++marker) {
        const CustomSpawnMarker* entry = &config->spawn_markers[marker];
        int eligible = whitelist ? custom_spawn_marker_applies(entry->kind, player_index) : 0;
        double world_x, world_y, dx, dy, distance;
        if (!eligible) continue;
        world_x = start_px + ((mirror ? room->width - 1 - entry->x : entry->x) + 0.5) * 16.0;
        world_y = start_y_px + (entry->y - 1) * 16.0;
        dx = world_x - *inout_x;
        dy = world_y - *inout_y;
        distance = dx * dx + dy * dy;
        if (best < 0 || distance < best_distance) {
            best = marker;
            best_distance = distance;
        }
    }
    if (best >= 0) {
        const CustomSpawnMarker* entry = &config->spawn_markers[best];
        *inout_x = (float)(start_px + ((mirror ? room->width - 1 - entry->x : entry->x) + 0.5) * 16.0);
        *inout_y = (float)(start_y_px + (entry->y - 1) * 16.0);
        return 1;
    }
    /* A deny-only room retains native selection. If its chosen floor was
     * denied, pick the nearest safe native @ floor deterministically. */
    if (native_denied || map->variable_rooms) {
        int x, y;
        for (y = 1; y < room->height; ++y) for (x = 1; x < room->width - 1; ++x) {
            int denied = 0;
            char here = custom_room_native_glyph(map, room, x, y);
            char above = custom_room_native_glyph(map, room, x, y - 1);
            double world_x, world_y, dx, dy, distance;
            if (here != '@' || strchr("!@_Xv12WwmK", above)) continue;
            for (marker = 0; marker < config->spawn_marker_count; ++marker)
                if (config->spawn_markers[marker].kind == SPAWN_MARKER_DENY &&
                    config->spawn_markers[marker].x == x && config->spawn_markers[marker].y == y) denied = 1;
            if (denied) continue;
            world_x = start_px + ((mirror ? room->width - 1 - x : x) + 0.5) * 16.0;
            world_y = start_y_px + (y - 1) * 16.0;
            dx = world_x - *inout_x; dy = world_y - *inout_y; distance = dx * dx + dy * dy;
            if (best < 0 || distance < best_distance) { best = y * room->width + x; best_distance = distance; }
        }
        if (best >= 0) {
            y = best / room->width; x = best % room->width;
            *inout_x = (float)(start_px + ((mirror ? room->width - 1 - x : x) + 0.5) * 16.0);
            *inout_y = (float)(start_y_px + (y - 1) * 16.0);
            return 1;
        }
    }
    return 0;
}

int custom_maps_content_cell_for_selector(int selector,
                                          int source_room,
                                          int x,
                                          int y,
                                          char* out_key,
                                          size_t out_key_size,
                                          char* out_native_glyph) {
    CustomMapContentView view;
    if (custom_maps_content_view_open(selector, &view) != 1) {
        if (out_key && out_key_size) out_key[0] = '\0';
        if (out_native_glyph) *out_native_glyph = '\0';
        return 0;
    }
    return custom_maps_content_view_cell(&view, source_room, x, y,
                                         out_key, out_key_size,
                                         out_native_glyph) == 1;
}

static int custom_map_content_sheet_copy_valid(const MapContentSheet* sheet,
                                               CustomMapContentSheetInfo* out_info) {
    char actual_sha[CONTENT_SHA256_HEX_SIZE];
    char hash_error[128];
    DWORD attributes;
    if (!sheet || !out_info) return 0;
    memset(out_info, 0, sizeof(*out_info));
    attributes = GetFileAttributesA(sheet->full_path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !map_asset_png_header_valid(sheet->full_path, NULL, NULL) ||
        !content_registry_sha256_file(sheet->full_path, NULL, actual_sha,
                                      hash_error, sizeof(hash_error)) ||
        _stricmp(actual_sha, sheet->asset_sha256) != 0) {
        return 0;
    }
    snprintf(out_info->key, sizeof(out_info->key), "%s", sheet->key);
    snprintf(out_info->full_path, sizeof(out_info->full_path), "%s", sheet->full_path);
    snprintf(out_info->asset_sha256, sizeof(out_info->asset_sha256), "%s",
             sheet->asset_sha256);
    out_info->cell_w = sheet->cell_w;
    out_info->cell_h = sheet->cell_h;
    out_info->padding = sheet->padding;
    out_info->source_x = sheet->source_x;
    out_info->source_y = sheet->source_y;
    out_info->source_w = sheet->source_w;
    out_info->source_h = sheet->source_h;
    out_info->sprite_count = sheet->sprite_count;
    out_info->atlas_flags = sheet->atlas_flags;
    return 1;
}

int custom_maps_content_sheet_count(void) {
    int map_index;
    int count = 0;
    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);
    for (map_index = 0; map_index < g_custom_registry.count; map_index++) {
        if (g_custom_registry.maps[map_index].content_sheet_count > INT_MAX - count) {
            return INT_MAX;
        }
        count += g_custom_registry.maps[map_index].content_sheet_count;
    }
    return count;
}

int custom_maps_content_sheet_get(int index, CustomMapContentSheetInfo* out_info) {
    int map_index;
    if (out_info) memset(out_info, 0, sizeof(*out_info));
    if (index < 0 || !out_info) return 0;
    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);
    for (map_index = 0; map_index < g_custom_registry.count; map_index++) {
        const CustomMap* map = &g_custom_registry.maps[map_index];
        if (index < map->content_sheet_count) {
            return custom_map_content_sheet_copy_valid(&map->content_sheets[index], out_info);
        }
        index -= map->content_sheet_count;
    }
    return 0;
}

int custom_maps_content_sheet_for_key(const char* sheet_key,
                                      char* out_full_path,
                                      size_t out_full_path_size,
                                      char* out_sha256,
                                      size_t out_sha256_size) {
    int map_index;
    if (out_full_path && out_full_path_size) out_full_path[0] = '\0';
    if (out_sha256 && out_sha256_size) out_sha256[0] = '\0';
    if (!sheet_key || !sheet_key[0] || !out_full_path || out_full_path_size == 0) return 0;
    if (!g_custom_maps_inited) custom_maps_init();
    custom_maps_reload_registry_if_needed(0);
    for (map_index = 0; map_index < g_custom_registry.count; map_index++) {
        const CustomMap* map = &g_custom_registry.maps[map_index];
        int sheet_index;
        for (sheet_index = 0; sheet_index < map->content_sheet_count; sheet_index++) {
            const MapContentSheet* sheet = &map->content_sheets[sheet_index];
            CustomMapContentSheetInfo info;
            if (_stricmp(sheet->key, sheet_key) != 0) continue;
            if (!custom_map_content_sheet_copy_valid(sheet, &info)) return 0;
            snprintf(out_full_path, out_full_path_size, "%s", info.full_path);
            if (out_sha256 && out_sha256_size) {
                snprintf(out_sha256, out_sha256_size, "%s", info.asset_sha256);
            }
            return 1;
        }
    }
    return 0;
}

int custom_maps_validate_package_text(const char* folder_id,
                                      const char* folder_path,
                                      const char* json_text,
                                      const char* map_text,
                                      CustomMapValidationSummary* out_summary) {
    MapDiagnostics diag;
    JsonValue* root = NULL;
    ParsedMapFile parsed_map;
    ParsedTileset tileset;
    CustomMap map;
    char owner[CONTENT_OWNER_MAX];
    int format_version = 0;
    int ok = 0;
    int room;
    int max_native_room_spawns = 0;
    int native_k_marker_count = 0;
    if (out_summary) memset(out_summary, 0, sizeof(*out_summary));
    if (!folder_id || !folder_id[0] || !folder_path || !json_text || !map_text ||
        strlen(json_text) > CUSTOM_MAP_MAX_TEXT_FILE_BYTES ||
        strlen(map_text) > CUSTOM_MAP_MAX_TEXT_FILE_BYTES) {
        return 0;
    }
    memset(&diag, 0, sizeof(diag));
    memset(&tileset, 0, sizeof(tileset));
    memset(&map, 0, sizeof(map));
    diag.map_id = folder_id;
    root = json_parse_document(&diag, "data.json", json_text);
    if (!root) goto done;
    if (!parse_map_format_and_owner(&diag, root, folder_id,
                                    &format_version, owner)) goto done;
    if (format_version == 2 &&
        !parse_v2_tileset(&diag, root, folder_path, owner, &tileset)) goto done;
    parse_map_file(&diag, map_text, format_version == 2 ? &tileset : NULL,
                   map_uses_variable_rooms(root),
                   &parsed_map);
    validate_room_glyph_footprints(&diag, &parsed_map);
    validate_native_room_spawn_budget(&diag, &parsed_map,
                                      &max_native_room_spawns,
                                      &native_k_marker_count);
    if (diag.error_count != 0) goto done;
    if (!build_custom_map(&diag, &parsed_map, root, folder_id,
                          format_version, &tileset, &map)) goto done;
    map.max_native_room_spawns = max_native_room_spawns;
    map.native_k_marker_count = native_k_marker_count;
    if (!custom_map_attach_optional_script(&diag, folder_path, &map)) goto done;
    ok = 1;

done:
    if (out_summary) {
        out_summary->format_version = format_version;
        out_summary->has_eggnogg_color = map.has_eggnogg_color;
        memcpy(out_summary->eggnogg_color, map.eggnogg_color, sizeof(map.eggnogg_color));
        out_summary->source_room_count = map.source_room_count;
        out_summary->final_room_count = map.final_room_count;
        out_summary->room_graph = map.room_graph;
        out_summary->graph_start_room = map.graph_start_room;
        out_summary->connection_count = map.connection_count;
        out_summary->layout_bounds_x = map.layout_bounds_x;
        out_summary->layout_bounds_y = map.layout_bounds_y;
        out_summary->layout_width = map.layout_width;
        out_summary->layout_height = map.layout_height;
        for (int room = 0; room < map.source_room_count && room < 9; ++room)
            out_summary->opponent_spawn[room] = custom_map_room_opponent_spawn(&map, room);
        for (int room = 0; room < custom_map_final_room_count(&map) &&
             room < CUSTOM_MAP_MAX_FINAL_ROOMS; ++room)
            out_summary->final_opponent_spawn[room] = custom_map_final_opponent_spawn(&map, room);
        out_summary->content_tile_count = map.content_tile_count;
        out_summary->particle_definition_count =
            map.ambiance_catalog.particle_count;
        out_summary->ambiance_definition_count =
            map.ambiance_catalog.ambiance_count;
        for (int ambiance_index = 0;
             ambiance_index < map.ambiance_catalog.ambiance_count;
             ambiance_index++) {
            const MapAmbianceDefinition* ambiance =
                &map.ambiance_catalog.ambiances[ambiance_index];
            for (int emitter_index = 0;
                 emitter_index < ambiance->emitter_count; emitter_index++) {
                out_summary->ambiance_lane_count +=
                    ambiance->emitters[emitter_index].count;
            }
        }
        snprintf(out_summary->default_sheet_key,
                 sizeof(out_summary->default_sheet_key), "%s",
                 map.default_sheet_key);
        out_summary->default_sheet_sprite_count =
            map.default_sheet_sprite_count;
        out_summary->native_layout = map.native_layout;
        out_summary->max_native_room_spawns = max_native_room_spawns;
        out_summary->native_room_spawn_limit = NATIVE_ROOM_RESET_SPAWN_LIMIT;
        out_summary->native_k_marker_count = native_k_marker_count;
        out_summary->has_script = map.script_id != 0 && map.script_source != NULL;
        out_summary->script_size = map.script_size;
        out_summary->has_entities = map.entity_source != NULL;
        out_summary->entity_size = map.entity_size;
        snprintf(out_summary->entity_sha256, sizeof(out_summary->entity_sha256), "%s", map.entity_sha256);
        out_summary->script_id = map.script_id;
        snprintf(out_summary->script_full_path,
                 sizeof(out_summary->script_full_path), "%s",
                 map.script_full_path);
        snprintf(out_summary->script_sha256,
                 sizeof(out_summary->script_sha256), "%s",
                 map.script_sha256);
        for (room = 0; room < map.source_room_count; room++) {
            int y;
            for (y = 0; y < map.rooms[room].height; y++) {
                int x;
                for (x = 0; x < map.rooms[room].width; x++) {
                    if (map.rooms[room].content_tile[y * ROOM_VARIABLE_MAX_W + x]) out_summary->content_cell_count++;
                }
            }
        }
        out_summary->error_count = diag.error_count;
        out_summary->warning_count = diag.warning_count;
    }
    parsed_tileset_abort(&tileset);
    if (map.content_transaction) content_registry_abort(map.content_transaction);
    free(map.entity_source);
    map.entity_source = NULL;
    free(map.script_source);
    map.script_source = NULL;
    json_free_value(root);
    return ok && diag.error_count == 0;
}

static void custom_maps_preview_set_error(char* err, size_t err_cap,
                                          const char* message) {
    if (!err || err_cap == 0u) return;
    snprintf(err, err_cap, "%s", message ? message : "preview map is invalid");
    err[err_cap - 1u] = '\0';
}

int custom_maps_install_preview_folder(const char* token,int* selector,char* error,size_t capacity) {
    char previous[MAX_PATH];uint64_t old_revision;int old_active;
    if(selector)*selector=-1;
    if(!token||strlen(token)!=32||!selector){custom_maps_preview_set_error(error,capacity,"Invalid preview token.");return 0;}
    for(size_t i=0;i<32;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f'))){custom_maps_preview_set_error(error,capacity,"Invalid preview token.");return 0;}
    if(!g_custom_maps_inited)custom_maps_init();
    snprintf(previous,sizeof(previous),"%s",g_preview_folder);old_active=g_preview_active;old_revision=g_preview_revision;
    snprintf(g_preview_folder,sizeof(g_preview_folder),"_greggnogg_previews/%s",token);g_preview_active=1;if(!++g_preview_revision)++g_preview_revision;
    custom_maps_reload_registry_if_needed(1);
    if(g_custom_registry.count&&g_custom_registry.maps[g_custom_registry.count-1].is_preview&&g_custom_registry.maps[g_custom_registry.count-1].preview_revision==g_preview_revision) {
        *selector=VANILLA_MAP_COUNT+g_custom_registry.count-1;if(error&&capacity)error[0]=0;return 1;
    }
    snprintf(g_preview_folder,sizeof(g_preview_folder),"%s",previous);g_preview_active=old_active;g_preview_revision=old_revision;
    custom_maps_preview_set_error(error,capacity,"Preview package failed loader validation; see modframework.log.");return 0;
}
int custom_maps_preview_session_in_use(const char* token) {
    char folder[MAX_PATH];
    if(!token||strlen(token)!=32)return 0;
    for(unsigned i=0;i<32;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return 0;
    snprintf(folder,sizeof(folder),"_greggnogg_previews/%s",token);
    if(g_preview_active&&!strcmp(g_preview_folder,folder))return 1;
    if(g_engine_pinned_map&&!strcmp(g_engine_pinned_map->folder_id,folder))return 1;
    for(int i=0;i<g_custom_registry.count;i++)
        if(!strcmp(g_custom_registry.maps[i].folder_id,folder))return 1;
    return 0;
}

void custom_maps_clear_preview(void) {
    if(!g_preview_active)return;
    g_preview_active=0;g_preview_folder[0]=0;custom_maps_reload_registry_if_needed(1);
}

int custom_maps_install_preview_text(const char* json_text,
                                     const char* map_text,
                                     int* out_selector,
                                     char* err,
                                     size_t err_cap) {
    static const char preview_id[] = "_greggnogg_preview";
    enum { PREVIEW_TEXT_MAX_BYTES = 12288 };
    MapDiagnostics diag;
    JsonValue* root = NULL;
    ParsedMapFile parsed_map;
    CustomMap candidate;
    CustomMap previous;
    char owner[CONTENT_OWNER_MAX];
    int format_version = 0;
    int max_native_room_spawns = 0;
    int native_k_marker_count = 0;
    int previous_active;
    char previous_folder[MAX_PATH];
    uint64_t revision;
    int installed = 0;

    if (out_selector) *out_selector = -1;
    if (err && err_cap > 0u) err[0] = '\0';
    if (!json_text || !map_text || !out_selector ||
        strlen(json_text) == 0u || strlen(map_text) == 0u ||
        strlen(json_text) > PREVIEW_TEXT_MAX_BYTES ||
        strlen(map_text) > PREVIEW_TEXT_MAX_BYTES) {
        custom_maps_preview_set_error(err, err_cap,
                                      "preview files are empty or exceed the preview limit");
        return 0;
    }

    memset(&diag, 0, sizeof(diag));
    memset(&parsed_map, 0, sizeof(parsed_map));
    memset(&candidate, 0, sizeof(candidate));
    memset(owner, 0, sizeof(owner));
    diag.map_id = preview_id;

    root = json_parse_document(&diag, "data.json", json_text);
    if (!root) goto done;
    if (!parse_map_format_and_owner(&diag, root, preview_id,
                                    &format_version, owner)) goto done;
    if (format_version != 1) {
        diag_log(&diag, 1,
                 "[data.json][format] error: preview links currently accept eggnogg-map/v1 only");
        goto done;
    }
    parse_map_file(&diag, map_text, NULL, map_uses_variable_rooms(root), &parsed_map);
    validate_room_glyph_footprints(&diag, &parsed_map);
    validate_native_room_spawn_budget(&diag, &parsed_map,
                                      &max_native_room_spawns,
                                      &native_k_marker_count);
    if (diag.error_count != 0) goto done;
    if (!build_custom_map(&diag, &parsed_map, root, preview_id,
                          format_version, NULL, &candidate)) goto done;
    if (diag.error_count != 0) goto done;

    candidate.max_native_room_spawns = max_native_room_spawns;
    candidate.native_k_marker_count = native_k_marker_count;
    snprintf(candidate.folder_id, sizeof(candidate.folder_id), "%s", preview_id);
    snprintf(candidate.id, sizeof(candidate.id), "%s", preview_id);
    candidate.online_key[0] = '\0';
    candidate.online_sig[0] = '\0';
    candidate.is_preview = 1;
    revision = ++g_preview_revision;
    if (revision == 0u) revision = ++g_preview_revision;
    candidate.preview_revision = revision;

    if (!g_custom_maps_inited) custom_maps_init();
    previous = g_preview_map;
    previous_active = g_preview_active;
    snprintf(previous_folder,sizeof(previous_folder),"%s",g_preview_folder);g_preview_folder[0]=0;
    g_preview_map = candidate;
    g_preview_active = 1;
    custom_maps_reload_registry_if_needed(1);
    if (g_custom_registry.count > 0 &&
        g_custom_registry.maps[g_custom_registry.count - 1].is_preview &&
        g_custom_registry.maps[g_custom_registry.count - 1].preview_revision == revision) {
        *out_selector = VANILLA_MAP_COUNT + g_custom_registry.count - 1;
        installed = 1;
        LOG_INFO("%s installed in-memory preview map \"%s\" at selector %d",
                 MAPS_PREFIX,
                 candidate.name[0] ? candidate.name : preview_id,
                 *out_selector);
    } else {
        g_preview_map = previous;
        g_preview_active = previous_active;
        snprintf(g_preview_folder,sizeof(g_preview_folder),"%s",previous_folder);
        custom_maps_reload_registry_if_needed(1);
        custom_maps_preview_set_error(err, err_cap,
                                      "preview map could not be installed atomically");
    }

done:
    if (!installed && (!err || err_cap == 0u || !err[0])) {
        custom_maps_preview_set_error(err, err_cap,
                                      "preview map failed loader validation; see modframework.log");
    }
    json_free_value(root);
    return installed;
}

#ifdef CUSTOM_MAPS_TESTING
int custom_maps_test_pin_folder(const char* folder_id) {
    if (!folder_id) {
        g_engine_pinned_registry_maps = NULL; g_engine_pinned_map = NULL;
        g_engine_pinned_selector = -1; g_engine_pinned_generation = 0;
        free_unpinned_retired_registry_maps(); return -1;
    }
    for (int i=0; i<g_custom_registry.count; ++i) {
        if (strcmp(g_custom_registry.maps[i].folder_id, folder_id)) continue;
        g_engine_pinned_registry_maps = g_custom_registry.maps;
        g_engine_pinned_map = &g_custom_registry.maps[i];
        g_engine_pinned_selector = VANILLA_MAP_COUNT + i;
        g_engine_pinned_generation = g_custom_maps_generation;
        free_unpinned_retired_registry_maps(); return g_engine_pinned_selector;
    }
    return -1;
}
#endif

int custom_maps_pinned_visual_sheet(const char* filename,int sprite,char* key,size_t capacity) {
    if(!g_engine_pinned_map || !filename || !key || !capacity || sprite<0) return 0;
    for(int i=0;i<g_engine_pinned_map->content_sheet_count;i++) {
        const MapContentSheet* sheet=&g_engine_pinned_map->content_sheets[i];
        if(!strcmp(sheet->relative_path,filename) && sprite<sheet->sprite_count && strlen(sheet->key)<capacity) {
            strcpy(key,sheet->key);return 1;
        }
    }
    return 0;
}
