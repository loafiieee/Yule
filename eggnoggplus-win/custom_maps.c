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
#include "entity_package.h"

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

typedef struct RoomConfig {
    int opponent_spawn_set;
    int opponent_spawn;
    int ambient_set;
    int ambient;
    AppearanceOverride appearance;
} RoomConfig;

typedef struct ParsedMapRoom {
    char id[CUSTOM_MAP_MAX_ID];
    char glyphs[ROOM_TEMPLATE_SIZE];
    unsigned char content_tile[ROOM_TEMPLATE_SIZE];
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
    char glyphs[ROOM_TEMPLATE_SIZE];
    unsigned char content_tile[ROOM_TEMPLATE_SIZE];
    RoomConfig config;
} CustomMapRoom;

typedef struct CustomMap {
    char folder_id[MAX_PATH];
    char id[CUSTOM_MAP_MAX_ID];
    char online_key[112];
    char online_sig[16];
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
    RoomConfig defaults_room;
    int source_room_count;
    CustomMapRoom rooms[CUSTOM_MAP_MAX_SOURCE_ROOMS];
    char content_owner[CONTENT_OWNER_MAX];
    ContentRegistryTx* content_transaction;
    MapContentTile content_tiles[CUSTOM_MAP_MAX_CONTENT_TILES];
    int content_tile_count;
    MapContentSheet content_sheets[CUSTOM_MAP_MAX_CONTENT_SHEETS];
    int content_sheet_count;
    char default_sheet_key[CONTENT_SHEET_KEY_MAX];
    int default_sheet_sprite_count;
    int native_layout;
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

/* Preserve the established text signature for v1/built-in-only packages, but
 * bind every external v2 sheet to the bytes the loader actually hashed.  This
 * lets authors omit redundant hand-maintained asset_sha256 fields without
 * allowing two different PNGs to advertise the same online map key. */
static uint32_t weak_map_package_hash32(const char* json_text,
                                        const char* map_text,
                                        const CustomMap* map) {
    const uint64_t modp = 4294967291ull;
    uint64_t h = weak_map_hash32(json_text, map_text);
    int i;
    if (!map) return (uint32_t)h;
    for (i = 0; i < map->content_sheet_count; i++) {
        const MapContentSheet* sheet = &map->content_sheets[i];
        const unsigned char* p;
        static const unsigned char separator = 0xffu;
        h = ((h * 16777619ull) + separator) % modp;
        for (p = (const unsigned char*)sheet->relative_path; *p; p++) {
            h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
        }
        h = ((h * 16777619ull) + separator) % modp;
        for (p = (const unsigned char*)sheet->asset_sha256; *p; p++) {
            h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
        }
    }
    if (map->script_sha256[0]) {
        const unsigned char* p;
        static const unsigned char separator = 0xffu;
        static const char script_name[] = "map.lua";
        h = ((h * 16777619ull) + separator) % modp;
        for (p = (const unsigned char*)script_name; *p; p++) {
            h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
        }
        h = ((h * 16777619ull) + separator) % modp;
        for (p = (const unsigned char*)map->script_sha256; *p; p++) {
            h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
        }
    }
    if (map->entity_sha256[0]) {
        const unsigned char* p;
        h = ((h * 16777619ull) + 0xeeu) % modp;
        for (p = (const unsigned char*)map->entity_sha256; *p; ++p)
            h = ((h * 16777619ull) + (uint64_t)(*p)) % modp;
    }
    return (uint32_t)h;
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
    out_definition->entity_layout.count=(uint32_t)map->source_room_count;
    for(int room=0;room<map->source_room_count;room++) out_definition->entity_layout.rooms[room]=map->rooms[room].id;
    return 1;
}

static int custom_map_validate_entity_visuals(MapDiagnostics* diag,const CustomMap* map) {
    EntityPackageLayout layout={0};layout.count=(uint32_t)map->source_room_count;
    for(int room=0;room<map->source_room_count;room++) layout.rooms[room]=map->rooms[room].id;
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
    if (width == 0 || height == 0 || width > 4096 || height > 4096) return 0;
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

static int parsed_tileset_add_sheet(MapDiagnostics* diag,
                                    ParsedTileset* tileset,
                                    const char* relative_path,
                                    const char* full_path,
                                    const char* digest,
                                    int cell_w,
                                    int cell_h,
                                    int padding,
                                    int sprite_count,
                                    char out_key[CONTENT_SHEET_KEY_MAX]) {
    char local_id[CONTENT_LOCAL_ID_MAX];
    char identity[CONTENT_SHA256_HEX_SIZE + 64];
    uint32_t path_hash;
    int i;
    snprintf(identity, sizeof(identity), "%s|%dx%d|p%d",
             digest, cell_w, cell_h, padding);
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
            tileset->sheets[i].padding == padding) {
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
    snprintf(out->relative_path, sizeof(out->relative_path), "%s",
             sprite_sheet);

    if (_strnicmp(sprite_sheet, "builtin:", 8) == 0) {
        if (geometry_authored) {
            diag_log(diag, 1,
                     "[data.json][%s] error: cell_w, cell_h, and padding only apply to external PNG sheets",
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
                     "[data.json][%s.sprite_sheet] error: invalid PNG header or dimensions (max 4096x4096)",
                     path);
            return 0;
        }
        if (out->image_w < cell_w || out->image_h < cell_h ||
            ((out->image_w + padding) % (cell_w + padding)) != 0 ||
            ((out->image_h + padding) % (cell_h + padding)) != 0) {
            diag_log(diag, 1,
                     "[data.json][%s.sprite_sheet] error: PNG dimensions %dx%d do not form a whole %dx%d grid with padding %d",
                     path, out->image_w, out->image_h, cell_w, cell_h,
                     padding);
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
            ((out->image_w + padding) / (cell_w + padding)) *
            ((out->image_h + padding) / (cell_h + padding));
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
        "native_layout", "tiles", "sheets"
    };
    static const char* const tile_keys[] = {
        "id", "symbol", "name", "native_glyph", "sprite_sheet",
        "asset_sha256", "sprite_index", "frame_count", "frame_ticks",
        "animation", "layer", "mirror_with_room", "random_phase",
        "native_visual",
        "offset_x", "offset_y", "scale_x", "scale_y", "angle_degrees", "tint",
        "cell_w", "cell_h", "padding", "collision", "force_mode",
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
    top_valid &= json_read_bool(diag, tileset_value, "native_layout", "tileset",
                                0, &native_layout);
    top_geometry_authored = json_object_get(tileset_value, "cell_w") != NULL ||
                            json_object_get(tileset_value, "cell_h") != NULL ||
                            json_object_get(tileset_value, "padding") != NULL;
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
                               default_padding, top_geometry_authored,
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
        static const char* const sheet_keys[]={"sprite_sheet","asset_sha256","cell_w","cell_h","padding"};
        JsonValue* sheets=json_object_get(tileset_value,"sheets");
        if(sheets) {
            if(sheets->type!=JSON_ARRAY || sheets->u.array_value.count>CUSTOM_MAP_MAX_CONTENT_SHEETS) {
                diag_log(diag,1,"[data.json][tileset.sheets] error: expected at most 16 sheet declarations");top_valid=0;
            } else for(int sheet_index=0;sheet_index<sheets->u.array_value.count;sheet_index++) {
                JsonValue* value=sheets->u.array_value.items[sheet_index];char path[96],filename[128]={0},sha[65]={0};int w=16,h=16,padding=0,valid=1;ResolvedMapSheet resolved;
                snprintf(path,sizeof(path),"tileset.sheets[%d]",sheet_index);
                if(value->type!=JSON_OBJECT) {diag_log(diag,1,"[data.json][%s] error: expected object",path);top_valid=0;continue;}
                reject_unknown_keys(diag,value,path,sheet_keys,5);
                valid &= json_read_bounded_string(diag,value,"sprite_sheet",path,1,filename,sizeof(filename));
                valid &= json_read_bounded_string(diag,value,"asset_sha256",path,0,sha,sizeof(sha));
                valid &= json_read_int_range(diag,value,"cell_w",path,16,1,512,&w);
                valid &= json_read_int_range(diag,value,"cell_h",path,16,1,512,&h);
                valid &= json_read_int_range(diag,value,"padding",path,0,0,64,&padding);
                if(!map_asset_direct_name_valid(filename)) {diag_log(diag,1,"[data.json][%s.sprite_sheet] error: expected a direct PNG filename",path);valid=0;}
                if(valid && !resolve_map_sheet(diag,out_tileset,folder_path,path,filename,sha,w,h,padding,1,&resolved)) valid=0;
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
        tile_sheet_authored = json_object_get(value, "sprite_sheet") != NULL;
        tile_geometry_authored = json_object_get(value, "cell_w") != NULL ||
                                 json_object_get(value, "cell_h") != NULL ||
                                 json_object_get(value, "padding") != NULL;
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
            (!effective_expected_sha[0] ||
             _stricmp(effective_expected_sha,
                      default_sheet.asset_sha256) == 0)) {
            resolved_sheet = default_sheet;
        } else if (valid &&
                   !resolve_map_sheet(
                       diag, out_tileset, folder_path, path,
                       effective_sprite_sheet, effective_expected_sha,
                       cell_w, cell_h, padding,
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

static int ambient_from_value(MapDiagnostics* diag, const JsonValue* value, const char* path, int* out_ambient) {
    int parsed_value;
    if (!value) return 0;

    if (value->type == JSON_STRING) {
        const char* text = value->u.string_value;
        if (strcmp(text, "none") == 0) parsed_value = 0;
        else if (strcmp(text, "bugs") == 0) parsed_value = 1;
        else if (strcmp(text, "clouds") == 0) parsed_value = 2;
        else if (strcmp(text, "art") == 0) parsed_value = 3;
        else if (strcmp(text, "flies") == 0) parsed_value = 4;
        else if (strcmp(text, "drips") == 0) parsed_value = 5;
        else if (strcmp(text, "dust") == 0) parsed_value = 6;
        else if (strcmp(text, "bats") == 0) parsed_value = 7;
        else if (strcmp(text, "bubbles") == 0) parsed_value = 8;
        else if (strcmp(text, "boil") == 0 || strcmp(text, "fumes") == 0) parsed_value = 9;
        else {
            diag_log(diag, 1, "[data.json][%s] error: unknown ambient \"%s\"", path, text);
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

static void parse_room_config(MapDiagnostics* diag, const JsonValue* value, const char* path, RoomConfig* config) {
    static const char* const known_keys[] = { "ambient", "appearance", "hook", "opponent_spawn" };
    JsonValue* ambient_value;
    JsonValue* appearance_value;
    JsonValue* hook_value;

    if (!value) return;
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected object", path);
        return;
    }

    warn_unknown_keys(diag, value, path, known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

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
        config->ambient_set = ambient_from_value(diag, ambient_value, path, &config->ambient);
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

static void parse_map_file(MapDiagnostics* diag,
                           const char* text,
                           const ParsedTileset* tileset,
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
            if (current_room && current_room->row_count != ROOM_TEMPLATE_H) {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: expected %d rows, got %d",
                         current_room->id, line - 1, ROOM_TEMPLATE_H, current_room->row_count);
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
            if (row_index >= ROOM_TEMPLATE_H) {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: room has more than %d rows",
                         current_room->id, line, ROOM_TEMPLATE_H);
                line++;
                continue;
            }
            if ((int)(close_quote - (trimmed + 1)) != ROOM_TEMPLATE_W) {
                diag_log(diag, 1, "[data.map][room=%s][line=%d] error: expected %d glyphs, got %d",
                         current_room->id, line, ROOM_TEMPLATE_W, (int)(close_quote - (trimmed + 1)));
                line++;
                continue;
            }
            {
                int i;
                for (i = 0; i < ROOM_TEMPLATE_W; i++) {
                    char ch = trimmed[1 + i];
                    int alias_index = parsed_tileset_find_symbol(tileset, ch);
                    int cell_index = row_index * ROOM_TEMPLATE_W + i;
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
            line++;
            continue;
        }

        diag_log(diag, 1, "[data.map][line=%d] error: expected [room_id] or quoted row", line);
        line++;
    }

    if (current_room && current_room->row_count != ROOM_TEMPLATE_H) {
        diag_log(diag, 1, "[data.map][room=%s][line=%d] error: expected %d rows, got %d",
                 current_room->id, line - 1, ROOM_TEMPLATE_H, current_room->row_count);
    }
}

static void validate_room_glyph_footprints(MapDiagnostics* diag, const ParsedMapFile* parsed_map) {
    int room_index;

    for (room_index = 0; room_index < parsed_map->room_count; room_index++) {
        const ParsedMapRoom* room = &parsed_map->rooms[room_index];
        int row;

        for (row = 0; row < ROOM_TEMPLATE_H; row++) {
            int col;
            int file_line = room->start_line + 1 + row;

            for (col = 0; col < ROOM_TEMPLATE_W; col++) {
                char ch = room->glyphs[row * ROOM_TEMPLATE_W + col];

                switch (ch) {
                    case 'G':
                        if (row < 3 || col == 0 || col == ROOM_TEMPLATE_W - 1) {
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
                        if (row < 3 || col == 0 || col == ROOM_TEMPLATE_W - 1) {
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
        int cell;
        for (cell = 0; cell < ROOM_TEMPLATE_SIZE; cell++) {
            switch (native_reset_spawn_kind(room->glyphs[cell])) {
                case 1: k_count++; break;
                case 2: sword_count++; break;
                case 3: mine_count++; break;
                default: break;
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

static void parse_defaults_room(MapDiagnostics* diag, const JsonValue* root_value, RoomConfig* defaults_room) {
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
    parse_room_config(diag, room_value, "defaults.room", defaults_room);
}

static void parse_room_overrides(MapDiagnostics* diag, const JsonValue* root_value, const ParsedMapFile* parsed_map,
                                 RoomConfig* overrides, unsigned char* override_present) {
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
        parse_room_config(diag, member->value, path, &overrides[room_index]);
        member = member->next;
    }
}

static int parse_layout(MapDiagnostics* diag, const JsonValue* root_value, const ParsedMapFile* parsed_map,
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

    warn_unknown_keys(diag, layout_value, "layout", known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

    kind_value = json_object_get(layout_value, "kind");
    if (!kind_value || kind_value->type != JSON_STRING ||
        strcmp(kind_value->u.string_value, "mirrored_source_rooms") != 0) {
        diag_log(diag, 1, "[data.json][layout.kind] error: expected \"mirrored_source_rooms\"");
    }

    format_value = json_object_get(layout_value, "room_format");
    if (!format_value || format_value->type != JSON_STRING ||
        strcmp(format_value->u.string_value, "vanilla_33x12") != 0) {
        diag_log(diag, 1, "[data.json][layout.room_format] error: expected \"vanilla_33x12\"");
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
            copy_truncated(diag, map->rooms[map->source_room_count].id,
                           sizeof(map->rooms[map->source_room_count].id),
                           parsed_map->rooms[room_index].id, "layout.order");
            memcpy(map->rooms[map->source_room_count].glyphs, parsed_map->rooms[room_index].glyphs, ROOM_TEMPLATE_SIZE);
            memcpy(map->rooms[map->source_room_count].content_tile,
                   parsed_map->rooms[room_index].content_tile, ROOM_TEMPLATE_SIZE);
            map->source_room_count++;
        }
    }

    for (i = 0; i < parsed_map->room_count; i++) {
        if (!referenced[i]) {
            diag_log(diag, 1, "[data.map][room=%s] error: room exists in data.map but is not referenced by layout.order",
                     parsed_map->rooms[i].id);
        }
    }

    return map->source_room_count > 0;
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
        "tileset"
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

    parse_rules(diag, json_object_get(root_value, "rules"), out_map);
    parse_defaults_room(diag, root_value, &out_map->defaults_room);
    parse_room_overrides(diag, root_value, parsed_map, overrides, override_present);
    parse_layout(diag, root_value, parsed_map, out_map);

    if (parsed_map->room_count > CUSTOM_MAP_MAX_SOURCE_ROOMS) {
        diag_log(diag, 1, "[data.map] error: map defines %d source rooms; current loader supports at most %d",
                 parsed_map->room_count, CUSTOM_MAP_MAX_SOURCE_ROOMS);
    }

    for (i = 0; i < out_map->source_room_count; i++) {
        int parsed_index = find_parsed_room(parsed_map, out_map->rooms[i].id);
        if (parsed_index >= 0 && override_present[parsed_index]) {
            out_map->rooms[i].config = overrides[parsed_index];
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
        uint32_t sig = weak_map_package_hash32(json_text, map_text, &custom_map);
        copy_lower_ascii(id_lower, sizeof(id_lower), custom_map.id[0] ? custom_map.id : custom_map.folder_id);
        snprintf(custom_map.online_sig, sizeof(custom_map.online_sig), "%08x", (unsigned int)sig);
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
                 custom_map.source_room_count * 2 - 1,
                 custom_map.mode ? "karate" : "swords",
                 custom_map.max_native_room_spawns,
                 NATIVE_ROOM_RESET_SPAWN_LIMIT,
                 custom_map.native_k_marker_count);
    } else {
        map_info(custom_map.id,
                 "registered: name=\"%s\" author=\"%s\" source_rooms=%d final_rooms=%d "
                 "mode=%s native_reset_spawners<=%d k_markers=0",
                 custom_map.name, custom_map.author, custom_map.source_room_count,
                 custom_map.source_room_count * 2 - 1,
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
        engine_roomdef_with_template(map->rooms[i].glyphs);
        if (*g_roomdef_count <= 0) continue;
        roomdef = &g_roomdefs[*g_roomdef_count - 1];
        apply_room_config(roomdef, &map->defaults_room, &map->rooms[i].config);
    }
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
        if (map->is_preview) continue;
        if (map->entity_source) continue;
        if (!first) appendf_counted(out, out_sz, &pos, ",");
        first = 0;
        appendf_counted(out, out_sz, &pos, "{\"key\":");
        append_json_string(out, out_sz, &pos, map->online_key[0] ? map->online_key : map->id);
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
        if (g_custom_registry.maps[i].entity_source) continue;
        p = g_custom_registry.maps[i].online_key;
        copy_lower_ascii(map_key, sizeof(map_key), p && p[0] ? p : g_custom_registry.maps[i].id);
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

static int custom_map_room_opponent_spawn(const CustomMap* map, int source_room) {
    if (!map || source_room < 0 || source_room >= map->source_room_count) return 0;
    if (map->rooms[source_room].config.opponent_spawn_set)
        return map->rooms[source_room].config.opponent_spawn;
    return map->defaults_room.opponent_spawn_set ? map->defaults_room.opponent_spawn : 0;
}

static int custom_map_final_opponent_spawn(const CustomMap* map, int final_room) {
    if (!map || final_room < 0 || final_room >= map->source_room_count * 2 - 1) return 0;
    return custom_map_room_opponent_spawn(map, abs(final_room - (map->source_room_count - 1)));
}

int custom_maps_opponent_spawn_policy(int selector, int final_room) {
    const CustomMap* map = g_engine_pinned_map;
    if (selector < VANILLA_MAP_COUNT || !g_custom_maps_inited ||
        !g_engine_pinned_registry_maps || !map || selector != g_engine_pinned_selector)
        return 0;
    return custom_map_final_opponent_spawn(map, final_room);
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
        source_room < 0 || x < 0 || y < 0 ||
        x >= ROOM_TEMPLATE_W || y >= ROOM_TEMPLATE_H) {
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
        source_room >= map->source_room_count) return -1;
    cell_index = y * ROOM_TEMPLATE_W + x;
    alias_plus_one = map->rooms[source_room].content_tile[cell_index];
    if (alias_plus_one == 0 || alias_plus_one > (unsigned int)map->content_tile_count) return 0;
    snprintf(out_key, out_key_size, "%s",
             map->content_tiles[alias_plus_one - 1u].key);
    if (out_native_glyph) {
        *out_native_glyph = map->content_tiles[alias_plus_one - 1u].native_glyph;
    }
    return 1;
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
        for (int room = 0; room < map.source_room_count && room < 9; ++room)
            out_summary->opponent_spawn[room] = custom_map_room_opponent_spawn(&map, room);
        for (int room = 0; room < map.source_room_count * 2 - 1 && room < 17; ++room)
            out_summary->final_opponent_spawn[room] = custom_map_final_opponent_spawn(&map, room);
        out_summary->content_tile_count = map.content_tile_count;
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
            int cell;
            for (cell = 0; cell < ROOM_TEMPLATE_SIZE; cell++) {
                if (map.rooms[room].content_tile[cell]) out_summary->content_cell_count++;
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
    parse_map_file(&diag, map_text, NULL, &parsed_map);
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
