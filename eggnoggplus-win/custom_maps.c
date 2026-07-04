#include <windows.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_maps.h"
#include "log.h"

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
    int ambient_set;
    int ambient;
    AppearanceOverride appearance;
} RoomConfig;

typedef struct ParsedMapRoom {
    char id[CUSTOM_MAP_MAX_ID];
    char glyphs[ROOM_TEMPLATE_SIZE];
    int row_count;
    int start_line;
} ParsedMapRoom;

typedef struct ParsedMapFile {
    ParsedMapRoom rooms[CUSTOM_MAP_MAX_PARSED_ROOMS];
    int room_count;
} ParsedMapFile;

typedef struct CustomMapRoom {
    char id[CUSTOM_MAP_MAX_ID];
    char glyphs[ROOM_TEMPLATE_SIZE];
    RoomConfig config;
} CustomMapRoom;

typedef struct CustomMap {
    char folder_id[CUSTOM_MAP_MAX_ID];
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
    RoomConfig defaults_room;
    int source_room_count;
    CustomMapRoom rooms[CUSTOM_MAP_MAX_SOURCE_ROOMS];
} CustomMap;

typedef struct CustomMapRegistry {
    CustomMap* maps;
    int count;
    int cap;
} CustomMapRegistry;

typedef struct RetiredRegistryBuffer {
    CustomMap* maps;
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
static RetiredRegistryBuffer* g_retired_registry_buffers = NULL;
static uint64_t g_custom_maps_signature = 0;

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

static void signature_add_file_or_missing(uint64_t* xor_accum, uint64_t* add_accum, uint64_t* count,
                                          const char* folder_name, const char* file_name) {
    char folder_path[MAX_PATH];
    char full_path[MAX_PATH];
    char rel_path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE find_handle;

    if (!path_join(folder_path, sizeof(folder_path), "maps", folder_name) ||
        !path_join(full_path, sizeof(full_path), folder_path, file_name)) {
        uint64_t marker = hash_signature_entry("maps_path_too_long", 0, 0, 0);
        signature_add(xor_accum, add_accum, count, marker);
        return;
    }

    if (snprintf(rel_path, sizeof(rel_path), "%s\\%s", folder_name, file_name) < 0) {
        uint64_t marker = hash_signature_entry("maps_rel_path_error", 0, 0, 0);
        signature_add(xor_accum, add_accum, count, marker);
        return;
    }

    find_handle = FindFirstFileA(full_path, &fd);
    if (find_handle == INVALID_HANDLE_VALUE) {
        char missing_rel[MAX_PATH];
        if (snprintf(missing_rel, sizeof(missing_rel), "%s_missing", rel_path) > 0) {
            uint64_t marker = hash_signature_entry(missing_rel, 0, 0, 0);
            signature_add(xor_accum, add_accum, count, marker);
        }
        return;
    }

    FindClose(find_handle);
    signature_add(
        xor_accum, add_accum, count,
        hash_signature_entry(
            rel_path,
            fd.dwFileAttributes,
            filetime_to_u64(&fd.ftLastWriteTime),
            ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow
        )
    );
}

static uint64_t custom_maps_compute_signature(void) {
    uint64_t xor_accum = 0;
    uint64_t add_accum = 0;
    uint64_t count = 0;
    WIN32_FIND_DATAA fd;
    HANDLE find_handle = FindFirstFileA("maps\\*", &fd);

    if (find_handle == INVALID_HANDLE_VALUE) {
        uint64_t marker = hash_signature_entry("maps_missing", 0, 0, 0);
        signature_add(&xor_accum, &add_accum, &count, marker);
    } else {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (fd.cFileName[0] == '_') continue;

            signature_add(
                &xor_accum, &add_accum, &count,
                hash_signature_entry(
                    fd.cFileName,
                    fd.dwFileAttributes,
                    filetime_to_u64(&fd.ftLastWriteTime),
                    0
                )
            );
            signature_add_file_or_missing(&xor_accum, &add_accum, &count, fd.cFileName, "data.json");
            signature_add_file_or_missing(&xor_accum, &add_accum, &count, fd.cFileName, "data.map");
        } while (FindNextFileA(find_handle, &fd));

        FindClose(find_handle);
    }

    {
        uint64_t final_hash = 1469598103934665603ull;
        const uint64_t version = 1ull;
        final_hash = hash_bytes64(final_hash, &version, sizeof(version));
        final_hash = hash_bytes64(final_hash, &xor_accum, sizeof(xor_accum));
        final_hash = hash_bytes64(final_hash, &add_accum, sizeof(add_accum));
        final_hash = hash_bytes64(final_hash, &count, sizeof(count));
        return final_hash;
    }
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

    *out_text = NULL;
    f = fopen(path, "rb");
    if (!f) return 0;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0) {
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
    fclose(f);

    text[read_count] = '\0';
    *out_text = text;
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
                    ch = (codepoint <= 0x7f) ? (unsigned char)codepoint : '?';
                    goto append_char;
                }
                default:
                    free(out);
                    json_parse_error(parser, "unsupported escape '\\%c'", escaped);
                    return NULL;
            }
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

    if (!value || value->type != JSON_NUMBER) return 0;
    as_double = value->u.number_value;
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
        else if (strcmp(text, "boil") == 0) parsed_value = 9;
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
        channel = (float)item->u.number_value;
        if (channel < 0.0f || channel > 1.0f) {
            diag_log(diag, 1, "[data.json][%s] error: colour channel %d must be in 0.0..1.0", path, i);
            return;
        }
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
    char child_path[256];

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
    static const char* const known_keys[] = { "ambient", "appearance", "hook" };
    JsonValue* ambient_value;
    JsonValue* appearance_value;
    JsonValue* hook_value;

    if (!value) return;
    if (value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][%s] error: expected object", path);
        return;
    }

    warn_unknown_keys(diag, value, path, known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

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

static void parse_map_file(MapDiagnostics* diag, const char* text, ParsedMapFile* parsed_map) {
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
                    current_room->glyphs[row_index * ROOM_TEMPLATE_W + i] = ch;
                    if (!glyph_allowed(ch)) {
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
                    default:
                        break;
                }
            }
        }
    }
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

static void copy_color_bank(float dst[ROOM_COLOR_SLOT_COUNT][3], const float src[ROOM_COLOR_SLOT_COUNT][3]) {
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

static void retire_registry_maps(CustomMap* maps) {
    RetiredRegistryBuffer* retired;

    if (!maps) return;

    retired = (RetiredRegistryBuffer*)malloc(sizeof(*retired));
    if (!retired) {
        LOG_WARN("%s failed to track retired map registry; leaking old buffer for safety", MAPS_PREFIX);
        return;
    }

    retired->maps = maps;
    retired->next = g_retired_registry_buffers;
    g_retired_registry_buffers = retired;
}

static void free_retired_registry_maps(void) {
    RetiredRegistryBuffer* retired = g_retired_registry_buffers;
    while (retired) {
        RetiredRegistryBuffer* next = retired->next;
        free(retired->maps);
        free(retired);
        retired = next;
    }
    g_retired_registry_buffers = NULL;
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
        "mode", "round_end_rooms", "score_target", "armed_respawn_limit"
    };
    JsonValue* mode_value;
    JsonValue* round_value;
    JsonValue* score_value;
    JsonValue* limit_value;

    if (!rules_value) return 1;
    if (rules_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json][rules] error: expected object");
        return 0;
    }

    warn_unknown_keys(diag, rules_value, "rules", known_keys, (int)(sizeof(known_keys) / sizeof(known_keys[0])));

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

static int build_custom_map(MapDiagnostics* diag, const ParsedMapFile* parsed_map, const JsonValue* root_value,
                            const char* folder_id, CustomMap* out_map) {
    static const char* const known_top_level[] = {
        "format", "id", "name", "author", "description", "sort_order", "rules", "layout", "defaults", "rooms"
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

    memset(overrides, 0, sizeof(overrides));
    memset(override_present, 0, sizeof(override_present));

    if (!root_value || root_value->type != JSON_OBJECT) {
        diag_log(diag, 1, "[data.json] error: root must be an object");
        return 0;
    }

    warn_unknown_keys(diag, root_value, "", known_top_level, (int)(sizeof(known_top_level) / sizeof(known_top_level[0])));

    {
        JsonValue* format_value = json_object_get(root_value, "format");
        if (!format_value || format_value->type != JSON_STRING ||
            strcmp(format_value->u.string_value, "eggnogg-map/v1") != 0) {
            diag_log(diag, 1, "[data.json][format] error: expected \"eggnogg-map/v1\"");
        }
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

    return diag->error_count == 0;
}

static int register_custom_map(CustomMapRegistry* registry, const CustomMap* map) {
    if (!registry) return 0;
    if (!registry_reserve(registry, registry->count + 1)) return 0;
    registry->maps[registry->count++] = *map;
    return 1;
}

static void scan_map_folder(CustomMapRegistry* registry, const WIN32_FIND_DATAA* fd) {
    char folder_path[MAX_PATH];
    char json_path[MAX_PATH];
    char map_path[MAX_PATH];
    char* json_text = NULL;
    char* map_text = NULL;
    JsonValue* json_root = NULL;
    ParsedMapFile parsed_map;
    CustomMap custom_map;
    MapDiagnostics diag;

    memset(&diag, 0, sizeof(diag));
    diag.map_id = fd->cFileName;

    if (!path_join(folder_path, sizeof(folder_path), "maps", fd->cFileName) ||
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

    parse_map_file(&diag, map_text, &parsed_map);
    validate_room_glyph_footprints(&diag, &parsed_map);
    if (diag.error_count != 0) goto cleanup;

    if (!build_custom_map(&diag, &parsed_map, json_root, fd->cFileName, &custom_map)) {
        goto cleanup;
    }

    {
        char id_lower[CUSTOM_MAP_MAX_ID];
        uint32_t sig = weak_map_hash32(json_text, map_text);
        copy_lower_ascii(id_lower, sizeof(id_lower), custom_map.id[0] ? custom_map.id : custom_map.folder_id);
        snprintf(custom_map.online_sig, sizeof(custom_map.online_sig), "%08x", (unsigned int)sig);
        snprintf(custom_map.online_key, sizeof(custom_map.online_key), "custom:%s:%s", id_lower, custom_map.online_sig);
    }

    if (!register_custom_map(registry, &custom_map)) {
        diag_log(&diag, 1, "failed to register custom map");
        goto cleanup;
    }

    map_info(custom_map.id,
             "registered: name=\"%s\" author=\"%s\" source_rooms=%d final_rooms=%d mode=%s",
             custom_map.name, custom_map.author, custom_map.source_room_count,
             custom_map.source_room_count * 2 - 1, custom_map.mode ? "karate" : "swords");

cleanup:
    if (diag.error_count != 0) {
        LOG_ERROR("%s[%s] map rejected: %d errors, %d warnings",
                  MAPS_PREFIX, fd->cFileName, diag.error_count, diag.warning_count);
    }
    json_free_value(json_root);
    free(json_text);
    free(map_text);
}

static void scan_maps_directory(CustomMapRegistry* registry) {
    WIN32_FIND_DATAA fd;
    HANDLE find_handle = FindFirstFileA("maps\\*", &fd);

    if (find_handle == INVALID_HANDLE_VALUE) {
        LOG_INFO("%s no maps directory found; custom map registry is empty", MAPS_PREFIX);
        return;
    }

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (fd.cFileName[0] == '_') continue;
        scan_map_folder(registry, &fd);
    } while (FindNextFileA(find_handle, &fd));

    FindClose(find_handle);
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
    free(registry->maps);
    registry->maps = NULL;
    registry->count = 0;
    registry->cap = 0;
}

static void registry_swap_in(CustomMapRegistry* incoming) {
    CustomMap* old_maps;

    if (!incoming) return;

    old_maps = g_custom_registry.maps;
    g_custom_registry = *incoming;
    incoming->maps = NULL;
    incoming->count = 0;
    incoming->cap = 0;

    retire_registry_maps(old_maps);
}

static void rebuild_custom_map_registry(CustomMapRegistry* registry) {
    if (!registry) return;

    registry->maps = NULL;
    registry->count = 0;
    registry->cap = 0;

    scan_maps_directory(registry);
    if (registry->count > 1) {
        qsort(registry->maps, (size_t)registry->count, sizeof(*registry->maps), custom_map_compare);
    }
}

static void custom_maps_reload_registry_if_needed(int force_reload) {
    uint64_t signature;

    if (!g_custom_maps_inited) return;

    signature = custom_maps_compute_signature();
    if (!force_reload && signature == g_custom_maps_signature) {
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
        registry_swap_in(&reloaded);
        g_custom_maps_signature = signature;

        LOG_INFO("%s registry %s: %d custom maps (was %d)",
                 MAPS_PREFIX,
                 force_reload ? "ready" : "reloaded",
                 g_custom_registry.count,
                 old_count);
    }
}

void custom_maps_init(void) {
    if (g_custom_maps_inited) return;
    g_custom_maps_inited = 1;
    g_custom_registry.maps = NULL;
    g_custom_registry.count = 0;
    g_custom_registry.cap = 0;
    g_custom_maps_signature = 0;

    custom_maps_reload_registry_if_needed(1);
}

void custom_maps_shutdown(void) {
    registry_clear(&g_custom_registry);
    free_retired_registry_maps();
    g_custom_maps_signature = 0;
    g_custom_maps_inited = 0;
}

void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void)) {
    int selector;
    int total_maps;
    int custom_index;

    if (!orig_mapgen_init) return;
    custom_maps_reload_registry_if_needed(0);

    if (g_custom_registry.count <= 0) {
        orig_mapgen_init();
        return;
    }

    total_maps = VANILLA_MAP_COUNT + g_custom_registry.count;
    selector = *g_map_selector;
    selector = positive_mod(selector, total_maps);
    *g_map_selector = selector;

    if (selector < VANILLA_MAP_COUNT) {
        orig_mapgen_init();
        return;
    }

    custom_index = selector - VANILLA_MAP_COUNT;
    if (custom_index < 0 || custom_index >= g_custom_registry.count) {
        LOG_ERROR("%s custom selector index out of range: selector=%d custom_index=%d total_custom=%d",
                  MAPS_PREFIX, selector, custom_index, g_custom_registry.count);
        orig_mapgen_init();
        return;
    }

    map_info(g_custom_registry.maps[custom_index].id, "applying custom map for selector index %d", selector);
    apply_custom_map(&g_custom_registry.maps[custom_index]);
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
        p = g_custom_registry.maps[i].online_key;
        copy_lower_ascii(map_key, sizeof(map_key), p && p[0] ? p : g_custom_registry.maps[i].id);
        if (strcmp(norm_key, map_key) == 0) {
            *out_selector = VANILLA_MAP_COUNT + i;
            return 1;
        }
    }
    return 0;
}
