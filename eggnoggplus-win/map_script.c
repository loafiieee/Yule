#include "map_script.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <luajit-2.1/lua.h>
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>
#include <luajit-2.1/luajit.h>

#define MAP_SCRIPT_MIN_MEMORY_BYTES (256u * 1024u)
#define MAP_SCRIPT_MAX_MEMORY_BYTES (16u * 1024u * 1024u)
#define MAP_SCRIPT_MAX_INSTRUCTIONS 10000000u
#define MAP_SCRIPT_CALLBACK_NONE LUA_NOREF
#define MAP_SCRIPT_FLAG_FAULTED UINT32_C(1)
#define MAP_SCRIPT_RNG_FALLBACK UINT32_C(0x6d2b79f5)
#define MAP_SCRIPT_SPRITE_MAX INT32_MAX
#define MAP_SCRIPT_DURATION_MAX UINT32_C(1000000)
#define MAP_SCRIPT_SENSOR_TILE_MIN_Q (-2 * MAP_SCRIPT_SENSOR_QUANTIZATION)
#define MAP_SCRIPT_SENSOR_TILE_MAX_Q (3 * MAP_SCRIPT_SENSOR_QUANTIZATION)
#define MAP_SCRIPT_SENSOR_OBJECT_MIN_Q (-2 * MAP_SCRIPT_SENSOR_QUANTIZATION)
#define MAP_SCRIPT_SENSOR_OBJECT_MAX_Q (2 * MAP_SCRIPT_SENSOR_QUANTIZATION)
#define MAP_SCRIPT_SENSOR_TILE_DIM_MAX 4096
#define MAP_SCRIPT_SENSOR_PLAYER_BIT UINT8_C(1)
#define MAP_SCRIPT_SENSOR_SWORD_BIT UINT8_C(2)
#define MAP_SCRIPT_SENSOR_DEAD_BODY_BIT UINT8_C(4)
#define MAP_SCRIPT_SENSOR_HAZARD_BIT UINT8_C(8)
#define MAP_SCRIPT_VELOCITY_LIMIT_ABS_MAX 64.0f
#define MAP_SCRIPT_VELOCITY_LIMIT_ALL_FLAGS \
    (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX | MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX | \
     MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY | MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY)

enum MapScriptEvent {
    MAP_SCRIPT_EVENT_CONTACT = 0,
    MAP_SCRIPT_EVENT_ENTER = 1,
    MAP_SCRIPT_EVENT_LEAVE = 2,
    MAP_SCRIPT_EVENT_COUNT = 3
};

enum MapScriptSensorProfile {
    MAP_SCRIPT_SENSOR_CENTER = 0,
    MAP_SCRIPT_SENSOR_BODY = 1,
    MAP_SCRIPT_SENSOR_FEET = 2,
    MAP_SCRIPT_SENSOR_CUSTOM = 3
};

typedef struct MapScriptSensorOwned {
    uint8_t configured;
    uint8_t object_mask;
    uint8_t object_profile;
    uint8_t mirror_with_room;
    uint8_t contact_scope;
    uint8_t reserved[3];
    int32_t tile_left_q;
    int32_t tile_top_q;
    int32_t tile_right_q;
    int32_t tile_bottom_q;
    int32_t object_left_q;
    int32_t object_top_q;
    int32_t object_right_q;
    int32_t object_bottom_q;
} MapScriptSensorOwned;

typedef struct MapScriptBindingOwned {
    char symbol;
    char qualified_key[MAP_SCRIPT_TILE_KEY_MAX];
    int callback_ref[MAP_SCRIPT_EVENT_COUNT];
    MapScriptSensorOwned sensor;
} MapScriptBindingOwned;

typedef struct MapScriptRuntime {
    lua_State* L;
    size_t lua_bytes;
    size_t memory_limit;
    uint32_t instruction_budget;
    uint32_t instructions_left;
    uint32_t hook_step;

    uint64_t script_id;
    uint64_t tick;
    uint32_t rng_state;
    int active;
    int loading;
    int faulted;
    int locked_env_ref;
    int on_tick_ref;
    int object_userdata_ref;
    int tile_userdata_ref;
    int safe_table_ref[5];
    int safe_function_ref[8];

    size_t binding_count;
    MapScriptBindingOwned bindings[MAP_SCRIPT_MAX_BINDINGS];

    uint16_t state_count;
    uint16_t override_count;
    uint16_t contact_count;
    uint16_t velocity_limit_count;
    uint32_t lifecycle_generation[MAP_SCRIPT_MAX_LIFECYCLE_SLOTS];
    MapScriptSnapshotStateEntry state[MAP_SCRIPT_MAX_STATE_ENTRIES];
    MapScriptSnapshotSpriteOverride overrides[MAP_SCRIPT_MAX_SPRITE_OVERRIDES];
    MapScriptSnapshotContact contacts[MAP_SCRIPT_MAX_CONTACTS];
    MapScriptSnapshotVelocityLimit velocity_limits[MAP_SCRIPT_MAX_VELOCITY_LIMITS];

    MapScriptHost host;
    char last_error[384];
} MapScriptRuntime;

typedef struct MapScriptObjectUserdata {
    MapScriptRuntime* runtime;
    MapScriptObjectView* object;
    uint8_t object_kind;
    float contact_radius;
} MapScriptObjectUserdata;

typedef struct MapScriptTileUserdata {
    MapScriptRuntime* runtime;
    uint16_t binding_index;
    uint32_t cell_index;
    int32_t x;
    int32_t y;
    uint8_t mirrored;
} MapScriptTileUserdata;

static const char g_state_metatable_key[] = "eggnoggplus.map_script.state.v1";
static const char g_object_metatable_key[] = "eggnoggplus.map_script.object.v1";
static const char g_tile_metatable_key[] = "eggnoggplus.map_script.tile.v1";
static const char* const g_safe_table_names[5] = {
    "map", "math", "string", "table", "bit"
};
static const char* const g_safe_function_names[8] = {
    "assert", "error", "ipairs", "rawequal", "select", "tostring", "type", "unpack"
};
static MapScriptRuntime* g_runtime = NULL;
static char g_last_error[384];

static void set_error(char* err, size_t err_cap, const char* fmt, ...) {
    va_list args;
    if (!err || err_cap == 0) return;
    va_start(args, fmt);
    vsnprintf(err, err_cap, fmt ? fmt : "map script error", args);
    va_end(args);
    err[err_cap - 1] = '\0';
}

static void remember_error(MapScriptRuntime* runtime, const char* text) {
    const char* message = (text && text[0]) ? text : "map script error";
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
    if (runtime) {
        snprintf(runtime->last_error, sizeof(runtime->last_error), "%s", message);
        runtime->last_error[sizeof(runtime->last_error) - 1] = '\0';
    }
}

static float canonical_float(float value) {
    return value == 0.0f ? 0.0f : value;
}

static double canonical_double(double value) {
    return value == 0.0 ? 0.0 : value;
}

static int finite_float_value(lua_State* L, int index, float* out) {
    lua_Number number = luaL_checknumber(L, index);
    if (!isfinite((double)number) || number > FLT_MAX || number < -FLT_MAX) {
        return luaL_error(L, "physics values must be finite 32-bit numbers");
    }
    *out = canonical_float((float)number);
    return 1;
}

static int object_physics_valid(const MapScriptObjectView* object) {
    return object && isfinite(object->x) && isfinite(object->y) &&
           isfinite(object->vx) && isfinite(object->vy);
}

static int object_kind_matches_id(uint32_t object_id, int object_kind) {
    int player_slot = object_id < 2u;
    int player_kind = object_kind == MAP_SCRIPT_OBJECT_PLAYER ||
                      object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY;
    return object_kind >= MAP_SCRIPT_OBJECT_PLAYER &&
           object_kind <= MAP_SCRIPT_OBJECT_HAZARD &&
           player_slot == player_kind;
}

static int object_kinds_share_lifecycle(int left, int right) {
    if (left == right) return 1;
    return (left == MAP_SCRIPT_OBJECT_PLAYER ||
            left == MAP_SCRIPT_OBJECT_DEAD_BODY) &&
           (right == MAP_SCRIPT_OBJECT_PLAYER ||
            right == MAP_SCRIPT_OBJECT_DEAD_BODY);
}

static int object_profile_valid(const MapScriptObjectView* object) {
    if (!object || object->reserved[0] != 0 || object->reserved[1] != 0 ||
        object->reserved[2] != 0) {
        return 0;
    }
    return object_kind_matches_id(object->object_id, object->object_kind);
}

static void canonicalize_object(MapScriptObjectView* object) {
    object->x = canonical_float(object->x);
    object->y = canonical_float(object->y);
    object->vx = canonical_float(object->vx);
    object->vy = canonical_float(object->vy);
}

static int lua_long_bracket_open(const char* source,
                                 size_t source_len,
                                 size_t offset,
                                 size_t* out_equals,
                                 size_t* out_content) {
    size_t cursor;
    if (!source || offset >= source_len || source[offset] != '[') return 0;
    cursor = offset + 1u;
    while (cursor < source_len && source[cursor] == '=') cursor++;
    if (cursor >= source_len || source[cursor] != '[') return 0;
    if (out_equals) *out_equals = cursor - offset - 1u;
    if (out_content) *out_content = cursor + 1u;
    return 1;
}

static size_t lua_long_bracket_end(const char* source,
                                   size_t source_len,
                                   size_t content,
                                   size_t equals) {
    size_t cursor = content;
    while (cursor < source_len) {
        size_t closing;
        size_t count;
        if (source[cursor] != ']') {
            cursor++;
            continue;
        }
        closing = cursor + 1u;
        count = 0;
        while (closing < source_len && source[closing] == '=' && count < equals) {
            closing++;
            count++;
        }
        if (count == equals && closing < source_len && source[closing] == ']') {
            return closing + 1u;
        }
        cursor++;
    }
    return source_len;
}

/* Lua's numeric '^' routes through libm/pow and can vary at the last bit
 * across CPUs/CRT builds. A tiny lexer rejects only actual operators while
 * allowing carets in short strings, long strings, and comments. */
static int source_uses_power_operator(const char* source, size_t source_len) {
    size_t cursor = 0;
    while (source && cursor < source_len) {
        char current = source[cursor];
        if (current == '\'' || current == '"') {
            char quote = current;
            cursor++;
            while (cursor < source_len) {
                if (source[cursor] == '\\') {
                    cursor += (cursor + 1u < source_len) ? 2u : 1u;
                } else if (source[cursor] == quote) {
                    cursor++;
                    break;
                } else {
                    cursor++;
                }
            }
            continue;
        }
        if (current == '-' && cursor + 1u < source_len &&
            source[cursor + 1u] == '-') {
            size_t equals;
            size_t content;
            cursor += 2u;
            if (lua_long_bracket_open(source, source_len, cursor,
                                      &equals, &content)) {
                cursor = lua_long_bracket_end(source, source_len, content, equals);
            } else {
                while (cursor < source_len && source[cursor] != '\r' &&
                       source[cursor] != '\n') {
                    cursor++;
                }
            }
            continue;
        }
        if (current == '[') {
            size_t equals;
            size_t content;
            if (lua_long_bracket_open(source, source_len, cursor,
                                      &equals, &content)) {
                cursor = lua_long_bracket_end(source, source_len, content, equals);
                continue;
            }
        }
        if (current == '^') return 1;
        cursor++;
    }
    return 0;
}

static int ascii_id_character(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.';
}

static int normalize_key_part(const char* begin,
                              size_t length,
                              char* out,
                              size_t out_cap) {
    size_t i;
    if (!begin || length == 0 || length >= out_cap) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)begin[i];
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        if (!ascii_id_character(ch)) return 0;
        out[i] = (char)ch;
    }
    if (out[0] == '.' || out[0] == '-') return 0;
    out[length] = '\0';
    return 1;
}

static int normalize_qualified_key(const char* input,
                                   char out[MAP_SCRIPT_TILE_KEY_MAX]) {
    const char* colon;
    size_t owner_length;
    size_t local_length;
    char owner[48];
    char local[48];
    int written;
    if (!input || !input[0]) return 0;
    colon = strchr(input, ':');
    if (!colon || strchr(colon + 1, ':')) return 0;
    owner_length = (size_t)(colon - input);
    local_length = strlen(colon + 1);
    if (!normalize_key_part(input, owner_length, owner, sizeof(owner)) ||
        !normalize_key_part(colon + 1, local_length, local, sizeof(local))) {
        return 0;
    }
    written = snprintf(out, MAP_SCRIPT_TILE_KEY_MAX, "%s:%s", owner, local);
    return written > 0 && written < MAP_SCRIPT_TILE_KEY_MAX;
}

static int copy_bindings(MapScriptRuntime* runtime,
                         const MapScriptDefinition* definition,
                         char* err,
                         size_t err_cap) {
    size_t i;
    size_t j;
    if (definition->binding_count > MAP_SCRIPT_MAX_BINDINGS) {
        set_error(err, err_cap, "map.lua has too many tile bindings (max %u)",
                  (unsigned)MAP_SCRIPT_MAX_BINDINGS);
        return 0;
    }
    if (definition->binding_count && !definition->bindings) {
        set_error(err, err_cap, "map.lua tile binding array is missing");
        return 0;
    }
    for (i = 0; i < definition->binding_count; ++i) {
        const MapScriptTileBinding* source = &definition->bindings[i];
        MapScriptBindingOwned* target = &runtime->bindings[i];
        unsigned char symbol = (unsigned char)source->symbol;
        if (symbol < 0x20 || symbol > 0x7e) {
            set_error(err, err_cap,
                      "map.lua tile binding %u has a blank or non-ASCII symbol",
                      (unsigned)(i + 1));
            return 0;
        }
        if (!normalize_qualified_key(source->qualified_key, target->qualified_key)) {
            set_error(err, err_cap,
                      "map.lua tile binding '%c' needs a valid owner:id key", symbol);
            return 0;
        }
        target->symbol = (char)symbol;
        for (j = 0; j < MAP_SCRIPT_EVENT_COUNT; ++j) {
            target->callback_ref[j] = MAP_SCRIPT_CALLBACK_NONE;
        }
        for (j = 0; j < i; ++j) {
            if (runtime->bindings[j].symbol == target->symbol) {
                set_error(err, err_cap, "duplicate map.lua tile symbol '%c'", symbol);
                return 0;
            }
            if (strcmp(runtime->bindings[j].qualified_key,
                       target->qualified_key) == 0) {
                set_error(err, err_cap, "duplicate map.lua tile key '%s'",
                          target->qualified_key);
                return 0;
            }
        }
    }
    runtime->binding_count = definition->binding_count;
    return 1;
}

static void* capped_allocator(void* userdata,
                              void* pointer,
                              size_t old_size,
                              size_t new_size) {
    MapScriptRuntime* runtime = (MapScriptRuntime*)userdata;
    void* result;
    if (new_size == 0) {
        free(pointer);
        if (pointer) {
            runtime->lua_bytes = old_size <= runtime->lua_bytes
                               ? runtime->lua_bytes - old_size : 0;
        }
        return NULL;
    }
    if (!pointer) old_size = 0; /* Lua uses old_size as a type tag for new objects. */
    if (new_size > old_size &&
        new_size - old_size > runtime->memory_limit - runtime->lua_bytes) {
        return NULL;
    }
    result = realloc(pointer, new_size);
    if (!result) return NULL;
    if (new_size >= old_size) runtime->lua_bytes += new_size - old_size;
    else runtime->lua_bytes -= old_size - new_size;
    return result;
}

static MapScriptRuntime* runtime_from_lua(lua_State* L) {
    void* userdata = NULL;
    (void)lua_getallocf(L, &userdata);
    return (MapScriptRuntime*)userdata;
}

static void instruction_hook(lua_State* L, lua_Debug* debug) {
    MapScriptRuntime* runtime = runtime_from_lua(L);
    (void)debug;
    if (!runtime || runtime->instructions_left <= runtime->hook_step) {
        if (runtime) runtime->instructions_left = 0;
        luaL_error(L, "map.lua instruction budget exceeded");
        return;
    }
    runtime->instructions_left -= runtime->hook_step;
}

static int protected_call(MapScriptRuntime* runtime,
                          int argument_count,
                          int result_count,
                          const char* label,
                          char* err,
                          size_t err_cap) {
    lua_State* L = runtime->L;
    int status;
    const char* detail;
    runtime->instructions_left = runtime->instruction_budget;
    runtime->hook_step = runtime->instruction_budget < 1000u
                       ? runtime->instruction_budget : 1000u;
    if (runtime->hook_step == 0) runtime->hook_step = 1;
    lua_sethook(L, instruction_hook, LUA_MASKCOUNT, (int)runtime->hook_step);
    status = lua_pcall(L, argument_count, result_count, 0);
    lua_sethook(L, NULL, 0, 0);
    if (status == 0) {
        lua_gc(L, LUA_GCCOLLECT, 0);
        return 1;
    }
    detail = lua_tostring(L, -1);
    set_error(err, err_cap, "%s: %s", label,
              detail ? detail : (status == LUA_ERRMEM ? "memory limit exceeded" : "Lua error"));
    lua_pop(L, 1);
    lua_gc(L, LUA_GCCOLLECT, 0);
    return 0;
}

static int function_has_upvalues(lua_State* L, int index) {
    int absolute = index < 0 ? lua_gettop(L) + index + 1 : index;
    const char* name;
    if (!lua_isfunction(L, absolute)) return 0;
    name = lua_getupvalue(L, absolute, 1);
    if (!name) return 0;
    lua_pop(L, 1);
    return 1;
}

static int readonly_assignment(lua_State* L) {
    (void)L;
    return luaL_error(L, "map.lua globals and API tables are read-only during callbacks");
}

static void push_readonly_proxy(lua_State* L, int table_index) {
    int absolute = table_index < 0 ? lua_gettop(L) + table_index + 1 : table_index;
    lua_newtable(L);
    lua_newtable(L);
    lua_pushvalue(L, absolute);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, readonly_assignment);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "locked map.lua table");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
}

static int table_is_allowed_global(const char* name) {
    return strcmp(name, "map") == 0 || strcmp(name, "math") == 0 ||
           strcmp(name, "string") == 0 || strcmp(name, "table") == 0 ||
           strcmp(name, "bit") == 0;
}

static int safe_table_slot(const char* name) {
    int i;
    for (i = 0; i < 5; ++i) {
        if (strcmp(name, g_safe_table_names[i]) == 0) return i;
    }
    return -1;
}

static int safe_function_slot(const char* name) {
    int i;
    for (i = 0; i < 8; ++i) {
        if (strcmp(name, g_safe_function_names[i]) == 0) return i;
    }
    return -1;
}

static int set_function_environment(lua_State* L,
                                    int function_index,
                                    int environment_index) {
    int function_absolute = function_index < 0
                          ? lua_gettop(L) + function_index + 1 : function_index;
    int environment_absolute = environment_index < 0
                             ? lua_gettop(L) + environment_index + 1 : environment_index;
    lua_pushvalue(L, environment_absolute);
    return lua_setfenv(L, function_absolute);
}

static int lock_script_environment_body(MapScriptRuntime* runtime,
                                        char* err,
                                        size_t err_cap) {
    lua_State* L = runtime->L;
    int globals_index;
    int backing_index;
    int environment_index;
    size_t i;
    int event;

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    globals_index = lua_gettop(L);
    lua_newtable(L);
    backing_index = lua_gettop(L);

    for (i = 0; i < 5; ++i) {
        int matches;
        lua_getglobal(L, g_safe_table_names[i]);
        if (runtime->safe_table_ref[i] == LUA_NOREF) {
            lua_pop(L, 1);
            set_error(err, err_cap, "map.lua sandbox is missing reserved table '%s'",
                      g_safe_table_names[i]);
            lua_settop(L, globals_index - 1);
            return 0;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->safe_table_ref[i]);
        matches = lua_istable(L, -2) && lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (!matches) {
            set_error(err, err_cap, "map.lua replaced reserved table '%s'",
                      g_safe_table_names[i]);
            lua_settop(L, globals_index - 1);
            return 0;
        }
    }
    for (i = 0; i < 8; ++i) {
        int matches;
        lua_getglobal(L, g_safe_function_names[i]);
        if (runtime->safe_function_ref[i] == LUA_NOREF) {
            lua_pop(L, 1);
            set_error(err, err_cap, "map.lua sandbox is missing reserved helper '%s'",
                      g_safe_function_names[i]);
            lua_settop(L, globals_index - 1);
            return 0;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->safe_function_ref[i]);
        matches = lua_isfunction(L, -2) && lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (!matches) {
            set_error(err, err_cap, "map.lua replaced reserved helper '%s'",
                      g_safe_function_names[i]);
            lua_settop(L, globals_index - 1);
            return 0;
        }
    }

    lua_pushnil(L);
    while (lua_next(L, globals_index) != 0) {
        const char* name;
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 1);
            continue;
        }
        name = lua_tostring(L, -2);
        if (strcmp(name, "_G") == 0) {
            lua_pop(L, 1);
            continue;
        }
        if (lua_istable(L, -1)) {
            int safe_slot = safe_table_slot(name);
            int matches_pinned = 0;
            if (safe_slot >= 0 && runtime->safe_table_ref[safe_slot] != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->safe_table_ref[safe_slot]);
                matches_pinned = lua_rawequal(L, -1, -2);
                lua_pop(L, 1);
            }
            if (!table_is_allowed_global(name) || !matches_pinned) {
                set_error(err, err_cap,
                          "map.lua global table '%s' is mutable or replaced; use map.state",
                          name);
                lua_settop(L, globals_index - 1);
                return 0;
            }
            lua_pushvalue(L, -2);
            push_readonly_proxy(L, -2);
            lua_settable(L, backing_index);
        } else {
            if (lua_isfunction(L, -1) && function_has_upvalues(L, -1)) {
                int safe_slot = safe_function_slot(name);
                int matches_pinned = 0;
                if (safe_slot >= 0 && runtime->safe_function_ref[safe_slot] != LUA_NOREF) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->safe_function_ref[safe_slot]);
                    matches_pinned = lua_rawequal(L, -1, -2);
                    lua_pop(L, 1);
                }
                if (!matches_pinned) {
                    set_error(err, err_cap,
                              "map.lua helper '%s' captures mutable upvalues; use map.state",
                              name);
                    lua_settop(L, globals_index - 1);
                    return 0;
                }
            }
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_settable(L, backing_index);
        }
        lua_pop(L, 1);
    }

    lua_newtable(L);
    environment_index = lua_gettop(L);
    lua_newtable(L);
    lua_pushvalue(L, backing_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, readonly_assignment);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "locked map.lua environment");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, environment_index);

    lua_pushnil(L);
    while (lua_next(L, backing_index) != 0) {
        if (lua_isfunction(L, -1) && !lua_iscfunction(L, -1)) {
            if (!set_function_environment(L, -1, environment_index)) {
                set_error(err, err_cap, "failed to lock a map.lua helper environment");
                lua_settop(L, globals_index - 1);
                return 0;
            }
        }
        lua_pop(L, 1);
    }

    for (i = 0; i < runtime->binding_count; ++i) {
        for (event = 0; event < MAP_SCRIPT_EVENT_COUNT; ++event) {
            int reference = runtime->bindings[i].callback_ref[event];
            if (reference == MAP_SCRIPT_CALLBACK_NONE) continue;
            lua_rawgeti(L, LUA_REGISTRYINDEX, reference);
            if (!set_function_environment(L, -1, environment_index)) {
                lua_pop(L, 1);
                set_error(err, err_cap, "failed to lock a map.lua tile callback");
                lua_settop(L, globals_index - 1);
                return 0;
            }
            lua_pop(L, 1);
        }
    }
    if (runtime->on_tick_ref != MAP_SCRIPT_CALLBACK_NONE) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->on_tick_ref);
        if (!set_function_environment(L, -1, environment_index)) {
            lua_pop(L, 1);
            set_error(err, err_cap, "failed to lock map.lua on_tick callback");
            lua_settop(L, globals_index - 1);
            return 0;
        }
        lua_pop(L, 1);
    }

    lua_pushvalue(L, environment_index);
    runtime->locked_env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_settop(L, globals_index - 1);
    return 1;
}

typedef struct MapScriptLockCall {
    MapScriptRuntime* runtime;
    int ok;
    char error[384];
} MapScriptLockCall;

static int lock_script_environment_entry(lua_State* L) {
    MapScriptLockCall* call = (MapScriptLockCall*)lua_touserdata(L, 1);
    call->ok = lock_script_environment_body(call->runtime,
                                            call->error,
                                            sizeof(call->error));
    if (!call->ok) return luaL_error(L, "%s", call->error);
    return 0;
}

static int lock_script_environment(MapScriptRuntime* runtime,
                                   char* err,
                                   size_t err_cap) {
    MapScriptLockCall call;
    int status;
    memset(&call, 0, sizeof(call));
    call.runtime = runtime;
    status = lua_cpcall(runtime->L, lock_script_environment_entry, &call);
    if (status != 0) {
        const char* detail = lua_tostring(runtime->L, -1);
        set_error(err, err_cap, "could not lock map.lua sandbox: %s",
                  detail ? detail : (status == LUA_ERRMEM
                                     ? "memory limit exceeded" : "Lua setup error"));
        lua_pop(runtime->L, 1);
        return 0;
    }
    return call.ok;
}

static MapScriptRuntime* checked_runtime(lua_State* L) {
    MapScriptRuntime* runtime = runtime_from_lua(L);
    if (!runtime) luaL_error(L, "map.lua runtime is unavailable");
    return runtime;
}

static int find_binding_by_ref(MapScriptRuntime* runtime,
                               const char* reference,
                               size_t length) {
    size_t i;
    char normalized[MAP_SCRIPT_TILE_KEY_MAX];
    if (length == 1) {
        for (i = 0; i < runtime->binding_count; ++i) {
            if (runtime->bindings[i].symbol == reference[0]) return (int)i;
        }
        return -1;
    }
    if (length >= MAP_SCRIPT_TILE_KEY_MAX ||
        !normalize_qualified_key(reference, normalized)) return -1;
    for (i = 0; i < runtime->binding_count; ++i) {
        if (strcmp(runtime->bindings[i].qualified_key, normalized) == 0) return (int)i;
    }
    return -1;
}

static int register_tile_callback(lua_State* L, enum MapScriptEvent event) {
    MapScriptRuntime* runtime = checked_runtime(L);
    size_t reference_length;
    const char* reference;
    int binding;
    if (!runtime->loading) return luaL_error(L, "callbacks may only be registered while map.lua loads");
    if (lua_gettop(L) != 2) return luaL_error(L, "expected tile reference and callback function");
    reference = luaL_checklstring(L, 1, &reference_length);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (lua_iscfunction(L, 2) || function_has_upvalues(L, 2)) {
        return luaL_error(L, "callbacks cannot capture upvalues; persist values in map.state");
    }
    binding = find_binding_by_ref(runtime, reference, reference_length);
    if (binding < 0) return luaL_error(L, "callback references an unknown map tile");
    if (runtime->bindings[binding].callback_ref[event] != MAP_SCRIPT_CALLBACK_NONE) {
        return luaL_error(L, "duplicate callback registration for this tile and event");
    }
    lua_pushvalue(L, 2);
    runtime->bindings[binding].callback_ref[event] = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static int api_on_contact(lua_State* L) {
    return register_tile_callback(L, MAP_SCRIPT_EVENT_CONTACT);
}

static int api_on_enter(lua_State* L) {
    return register_tile_callback(L, MAP_SCRIPT_EVENT_ENTER);
}

static int api_on_leave(lua_State* L) {
    return register_tile_callback(L, MAP_SCRIPT_EVENT_LEAVE);
}

static int api_on_tick(lua_State* L) {
    MapScriptRuntime* runtime = checked_runtime(L);
    if (!runtime->loading) return luaL_error(L, "callbacks may only be registered while map.lua loads");
    if (lua_gettop(L) != 1) return luaL_error(L, "on_tick expects one callback function");
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (lua_iscfunction(L, 1) || function_has_upvalues(L, 1)) {
        return luaL_error(L, "callbacks cannot capture upvalues; persist values in map.state");
    }
    if (runtime->on_tick_ref != MAP_SCRIPT_CALLBACK_NONE) {
        return luaL_error(L, "duplicate on_tick callback registration");
    }
    lua_pushvalue(L, 1);
    runtime->on_tick_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static int lua_absolute_index(lua_State* L, int index) {
    if (index > 0 || index <= LUA_REGISTRYINDEX) return index;
    return lua_gettop(L) + index + 1;
}

static int table_has_metatable(lua_State* L, int index) {
    int absolute = lua_absolute_index(L, index);
    int has_metatable = lua_getmetatable(L, absolute);
    if (has_metatable) lua_pop(L, 1);
    return has_metatable;
}

static int sensor_string_equals(const char* value,
                                size_t value_length,
                                const char* expected) {
    size_t expected_length = expected ? strlen(expected) : 0u;
    return value && expected && value_length == expected_length &&
           memcmp(value, expected, expected_length) == 0;
}

static int sensor_box_field(const char* key, size_t key_length) {
    if (sensor_string_equals(key, key_length, "left")) return 0;
    if (sensor_string_equals(key, key_length, "top")) return 1;
    if (sensor_string_equals(key, key_length, "right")) return 2;
    if (sensor_string_equals(key, key_length, "bottom")) return 3;
    return -1;
}

static int32_t quantize_sensor_number(lua_State* L,
                                      int value_index,
                                      int32_t minimum_q,
                                      int32_t maximum_q,
                                      const char* label) {
    double value;
    double scaled;
    double rounded;
    if (lua_type(L, value_index) != LUA_TNUMBER) {
        luaL_error(L, "%s must be a number", label);
        return 0;
    }
    value = (double)lua_tonumber(L, value_index);
    if (!isfinite(value) ||
        value < (double)minimum_q / MAP_SCRIPT_SENSOR_QUANTIZATION ||
        value > (double)maximum_q / MAP_SCRIPT_SENSOR_QUANTIZATION) {
        luaL_error(L, "%s is non-finite or outside its supported range", label);
        return 0;
    }
    scaled = value * MAP_SCRIPT_SENSOR_QUANTIZATION;
    rounded = scaled < 0.0 ? ceil(scaled - 0.5) : floor(scaled + 0.5);
    if (rounded < minimum_q || rounded > maximum_q) {
        luaL_error(L, "%s is outside its supported quantized range", label);
        return 0;
    }
    return (int32_t)rounded;
}

static void parse_sensor_box(lua_State* L,
                             int table_index,
                             int32_t minimum_q,
                             int32_t maximum_q,
                             const char* label,
                             int32_t* out_left,
                             int32_t* out_top,
                             int32_t* out_right,
                             int32_t* out_bottom) {
    int absolute = lua_absolute_index(L, table_index);
    int32_t values[4] = { 0, 0, 0, 0 };
    unsigned seen = 0;
    if (lua_type(L, absolute) != LUA_TTABLE) {
        luaL_error(L, "%s must be a box table", label);
        return;
    }
    if (table_has_metatable(L, absolute)) {
        luaL_error(L, "%s cannot have a metatable", label);
        return;
    }
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        const char* key;
        size_t key_length;
        int field;
        char field_label[96];
        if (lua_type(L, -2) != LUA_TSTRING) {
            luaL_error(L, "%s accepts only left, top, right, and bottom fields", label);
            return;
        }
        key = lua_tolstring(L, -2, &key_length);
        field = sensor_box_field(key, key_length);
        if (field < 0) {
            luaL_error(L, "%s has unknown field '%s'", label, key);
            return;
        }
        snprintf(field_label, sizeof(field_label), "%s.%s", label, key);
        values[field] = quantize_sensor_number(L, -1, minimum_q, maximum_q,
                                               field_label);
        seen |= 1u << field;
        lua_pop(L, 1);
    }
    if (seen != 0x0fu) {
        luaL_error(L, "%s requires left, top, right, and bottom", label);
        return;
    }
    if (values[0] >= values[2] || values[1] >= values[3]) {
        luaL_error(L, "%s must have positive width and height after 1/256 quantization",
                   label);
        return;
    }
    *out_left = values[0];
    *out_top = values[1];
    *out_right = values[2];
    *out_bottom = values[3];
}

static uint8_t parse_sensor_objects(lua_State* L, int table_index) {
    int absolute = lua_absolute_index(L, table_index);
    unsigned index_mask = 0;
    uint8_t object_mask = 0;
    unsigned count = 0;
    if (lua_type(L, absolute) != LUA_TTABLE) {
        luaL_error(L, "map.sensor objects must be a dense list");
        return 0;
    }
    if (table_has_metatable(L, absolute)) {
        luaL_error(L, "map.sensor objects cannot have a metatable");
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        double numeric_key;
        int list_index;
        const char* kind;
        size_t kind_length;
        uint8_t kind_bit;
        if (lua_type(L, -2) != LUA_TNUMBER) {
            luaL_error(L, "map.sensor objects must be a dense numeric list");
            return 0;
        }
        numeric_key = (double)lua_tonumber(L, -2);
        if (!isfinite(numeric_key) || floor(numeric_key) != numeric_key ||
            numeric_key < 1.0 || numeric_key > 4.0) {
            luaL_error(L, "map.sensor objects indices must be dense from 1");
            return 0;
        }
        list_index = (int)numeric_key;
        if ((index_mask & (1u << (list_index - 1))) != 0) {
            luaL_error(L, "map.sensor objects contains a duplicate index");
            return 0;
        }
        if (lua_type(L, -1) != LUA_TSTRING) {
            luaL_error(L,
                       "map.sensor objects entries must be supported object kind names");
            return 0;
        }
        kind = lua_tolstring(L, -1, &kind_length);
        if (sensor_string_equals(kind, kind_length, "player")) {
            /* Compatibility selector: before API 4, dying player bodies were
             * indistinguishable from live players and were included here. */
            kind_bit = MAP_SCRIPT_SENSOR_PLAYER_BIT |
                       MAP_SCRIPT_SENSOR_DEAD_BODY_BIT;
        } else if (sensor_string_equals(kind, kind_length, "alive_player")) {
            kind_bit = MAP_SCRIPT_SENSOR_PLAYER_BIT;
        } else if (sensor_string_equals(kind, kind_length, "dead_body")) {
            kind_bit = MAP_SCRIPT_SENSOR_DEAD_BODY_BIT;
        } else if (sensor_string_equals(kind, kind_length, "sword")) {
            kind_bit = MAP_SCRIPT_SENSOR_SWORD_BIT;
        } else if (sensor_string_equals(kind, kind_length, "hazard")) {
            kind_bit = MAP_SCRIPT_SENSOR_HAZARD_BIT;
        }
        else {
            luaL_error(L, "map.sensor objects has unknown kind '%s'", kind);
            return 0;
        }
        if ((object_mask & kind_bit) != 0) {
            luaL_error(L, "map.sensor objects contains duplicate kind '%s'", kind);
            return 0;
        }
        index_mask |= 1u << (list_index - 1);
        object_mask |= kind_bit;
        count++;
        lua_pop(L, 1);
    }
    if (count == 0) {
        luaL_error(L, "map.sensor objects cannot be empty");
        return 0;
    }
    if (index_mask != ((1u << count) - 1u)) {
        luaL_error(L, "map.sensor objects must not be sparse");
        return 0;
    }
    return object_mask;
}

static int sensor_option_field(const char* key, size_t key_length) {
    return sensor_string_equals(key, key_length, "tile_box") ||
           sensor_string_equals(key, key_length, "object_box") ||
           sensor_string_equals(key, key_length, "objects") ||
           sensor_string_equals(key, key_length, "mirror_with_room") ||
           sensor_string_equals(key, key_length, "contact_scope");
}

static void validate_sensor_options(lua_State* L, int table_index) {
    int absolute = lua_absolute_index(L, table_index);
    if (lua_type(L, absolute) != LUA_TTABLE) {
        luaL_error(L, "map.sensor options must be a table");
        return;
    }
    if (table_has_metatable(L, absolute)) {
        luaL_error(L, "map.sensor options cannot have a metatable");
        return;
    }
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        const char* key;
        size_t key_length;
        if (lua_type(L, -2) != LUA_TSTRING) {
            luaL_error(L, "map.sensor options accepts only named fields");
            return;
        }
        key = lua_tolstring(L, -2, &key_length);
        if (!sensor_option_field(key, key_length)) {
            luaL_error(L, "map.sensor options has unknown field '%s'", key);
            return;
        }
        lua_pop(L, 1);
    }
}

static void sensor_raw_field(lua_State* L, int table_index, const char* key) {
    int absolute = lua_absolute_index(L, table_index);
    lua_pushstring(L, key);
    lua_rawget(L, absolute);
}

static int api_sensor(lua_State* L) {
    MapScriptRuntime* runtime = checked_runtime(L);
    MapScriptSensorOwned sensor;
    size_t reference_length;
    const char* reference;
    int binding;
    if (!runtime->loading) {
        return luaL_error(L, "sensors may only be registered while map.lua loads");
    }
    if (lua_gettop(L) != 2) return luaL_error(L, "map.sensor expects a tile reference and options table");
    if (lua_type(L, 1) != LUA_TSTRING) return luaL_error(L, "map.sensor tile reference must be a string");
    reference = lua_tolstring(L, 1, &reference_length);
    binding = find_binding_by_ref(runtime, reference, reference_length);
    if (binding < 0) return luaL_error(L, "map.sensor references an unknown map tile");
    if (runtime->bindings[binding].sensor.configured) {
        return luaL_error(L, "duplicate map.sensor registration for this tile");
    }
    validate_sensor_options(L, 2);
    memset(&sensor, 0, sizeof(sensor));
    sensor.configured = 1;
    sensor.object_mask = MAP_SCRIPT_SENSOR_PLAYER_BIT |
                         MAP_SCRIPT_SENSOR_DEAD_BODY_BIT |
                         MAP_SCRIPT_SENSOR_SWORD_BIT;
    sensor.object_profile = MAP_SCRIPT_SENSOR_CENTER;
    sensor.tile_right_q = MAP_SCRIPT_SENSOR_QUANTIZATION;
    sensor.tile_bottom_q = MAP_SCRIPT_SENSOR_QUANTIZATION;

    sensor_raw_field(L, 2, "tile_box");
    if (!lua_isnil(L, -1)) {
        parse_sensor_box(L, -1,
                         MAP_SCRIPT_SENSOR_TILE_MIN_Q,
                         MAP_SCRIPT_SENSOR_TILE_MAX_Q,
                         "map.sensor tile_box",
                         &sensor.tile_left_q, &sensor.tile_top_q,
                         &sensor.tile_right_q, &sensor.tile_bottom_q);
    }
    lua_pop(L, 1);

    sensor_raw_field(L, 2, "object_box");
    if (!lua_isnil(L, -1)) {
        if (lua_type(L, -1) == LUA_TSTRING) {
            size_t profile_length;
            const char* profile = lua_tolstring(L, -1, &profile_length);
            if (sensor_string_equals(profile, profile_length, "center")) {
                sensor.object_profile = MAP_SCRIPT_SENSOR_CENTER;
            } else if (sensor_string_equals(profile, profile_length, "body")) {
                sensor.object_profile = MAP_SCRIPT_SENSOR_BODY;
            } else if (sensor_string_equals(profile, profile_length, "feet")) {
                sensor.object_profile = MAP_SCRIPT_SENSOR_FEET;
            }
            else return luaL_error(L, "map.sensor object_box has unknown profile '%s'", profile);
        } else if (lua_type(L, -1) == LUA_TTABLE) {
            sensor.object_profile = MAP_SCRIPT_SENSOR_CUSTOM;
            parse_sensor_box(L, -1,
                             MAP_SCRIPT_SENSOR_OBJECT_MIN_Q,
                             MAP_SCRIPT_SENSOR_OBJECT_MAX_Q,
                             "map.sensor object_box",
                             &sensor.object_left_q, &sensor.object_top_q,
                             &sensor.object_right_q, &sensor.object_bottom_q);
        } else {
            return luaL_error(L, "map.sensor object_box must be center, body, feet, or a box table");
        }
    }
    lua_pop(L, 1);

    sensor_raw_field(L, 2, "objects");
    if (!lua_isnil(L, -1)) sensor.object_mask = parse_sensor_objects(L, -1);
    lua_pop(L, 1);

    sensor_raw_field(L, 2, "mirror_with_room");
    if (!lua_isnil(L, -1)) {
        if (lua_type(L, -1) != LUA_TBOOLEAN) {
            return luaL_error(L, "map.sensor mirror_with_room must be boolean");
        }
        sensor.mirror_with_room = (uint8_t)(lua_toboolean(L, -1) != 0);
    }
    lua_pop(L, 1);

    sensor_raw_field(L, 2, "contact_scope");
    if (!lua_isnil(L, -1)) {
        size_t scope_length;
        const char* scope;
        if (lua_type(L, -1) != LUA_TSTRING) {
            return luaL_error(L,
                "map.sensor contact_scope must be 'cell' or 'binding'");
        }
        scope = lua_tolstring(L, -1, &scope_length);
        if (sensor_string_equals(scope, scope_length, "cell")) {
            sensor.contact_scope = MAP_SCRIPT_CONTACT_SCOPE_CELL;
        } else if (sensor_string_equals(scope, scope_length, "binding")) {
            sensor.contact_scope = MAP_SCRIPT_CONTACT_SCOPE_BINDING;
        } else {
            return luaL_error(L,
                "map.sensor contact_scope must be 'cell' or 'binding'");
        }
    }
    lua_pop(L, 1);

    runtime->bindings[binding].sensor = sensor;
    return 0;
}

static int state_find(MapScriptRuntime* runtime, const char* key, size_t key_length) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_STATE_ENTRIES; ++i) {
        MapScriptSnapshotStateEntry* entry = &runtime->state[i];
        if (entry->in_use && entry->key_len == key_length &&
            memcmp(entry->key, key, key_length) == 0) return i;
    }
    return -1;
}

static int checked_state_key(lua_State* L,
                             int index,
                             const char** out_key,
                             size_t* out_length) {
    const char* key;
    size_t length;
    key = luaL_checklstring(L, index, &length);
    if (length == 0 || length >= MAP_SCRIPT_STATE_KEY_MAX ||
        memchr(key, '\0', length) != NULL) {
        return luaL_error(L, "map.state keys must be 1-31 byte strings without NULs");
    }
    *out_key = key;
    *out_length = length;
    return 1;
}

static int state_index(lua_State* L) {
    MapScriptRuntime* runtime = checked_runtime(L);
    const char* key;
    size_t key_length;
    int index;
    (void)lua_touserdata(L, 1);
    if (!checked_state_key(L, 2, &key, &key_length)) return 0;
    index = state_find(runtime, key, key_length);
    if (index < 0) {
        lua_pushnil(L);
        return 1;
    }
    switch (runtime->state[index].type) {
        case MAP_SCRIPT_STATE_BOOL:
            lua_pushboolean(L, runtime->state[index].bool_value != 0);
            break;
        case MAP_SCRIPT_STATE_NUMBER:
            lua_pushnumber(L, runtime->state[index].number_value);
            break;
        case MAP_SCRIPT_STATE_STRING:
            lua_pushlstring(L, runtime->state[index].string_value,
                            runtime->state[index].string_len);
            break;
        default:
            lua_pushnil(L);
            break;
    }
    return 1;
}

static int state_newindex(lua_State* L) {
    MapScriptRuntime* runtime = checked_runtime(L);
    const char* key;
    size_t key_length;
    int index;
    int first_free = -1;
    int i;
    if (!checked_state_key(L, 2, &key, &key_length)) return 0;
    index = state_find(runtime, key, key_length);
    for (i = 0; i < MAP_SCRIPT_MAX_STATE_ENTRIES && first_free < 0; ++i) {
        if (!runtime->state[i].in_use) first_free = i;
    }
    if (lua_isnil(L, 3)) {
        if (index >= 0) {
            memset(&runtime->state[index], 0, sizeof(runtime->state[index]));
            runtime->state_count--;
        }
        return 0;
    }
    if (index < 0) {
        if (first_free < 0) return luaL_error(L, "map.state entry limit reached");
        index = first_free;
        runtime->state[index].in_use = 1;
        runtime->state[index].key_len = (uint8_t)key_length;
        memcpy(runtime->state[index].key, key, key_length);
        runtime->state_count++;
    } else {
        uint8_t saved_key_length = runtime->state[index].key_len;
        char saved_key[MAP_SCRIPT_STATE_KEY_MAX];
        memcpy(saved_key, runtime->state[index].key, sizeof(saved_key));
        memset(&runtime->state[index], 0, sizeof(runtime->state[index]));
        runtime->state[index].in_use = 1;
        runtime->state[index].key_len = saved_key_length;
        memcpy(runtime->state[index].key, saved_key, sizeof(saved_key));
    }
    switch (lua_type(L, 3)) {
        case LUA_TBOOLEAN:
            runtime->state[index].type = MAP_SCRIPT_STATE_BOOL;
            runtime->state[index].bool_value = (uint8_t)(lua_toboolean(L, 3) != 0);
            break;
        case LUA_TNUMBER: {
            double number = (double)lua_tonumber(L, 3);
            if (!isfinite(number)) return luaL_error(L, "map.state numbers must be finite");
            runtime->state[index].type = MAP_SCRIPT_STATE_NUMBER;
            runtime->state[index].number_value = canonical_double(number);
            break;
        }
        case LUA_TSTRING: {
            size_t length;
            const char* value = lua_tolstring(L, 3, &length);
            if (length >= MAP_SCRIPT_STATE_STRING_MAX) {
                return luaL_error(L, "map.state strings may contain at most 63 bytes");
            }
            runtime->state[index].type = MAP_SCRIPT_STATE_STRING;
            runtime->state[index].string_len = (uint8_t)length;
            memcpy(runtime->state[index].string_value, value, length);
            break;
        }
        default:
            return luaL_error(L, "map.state accepts only nil, boolean, number, or short string");
    }
    return 0;
}

static MapScriptObjectUserdata* check_object_userdata(lua_State* L, int index) {
    MapScriptObjectUserdata* userdata =
        (MapScriptObjectUserdata*)luaL_checkudata(L, index, g_object_metatable_key);
    if (!userdata || !userdata->runtime || !userdata->object) {
        luaL_error(L, "expired map object");
        return NULL;
    }
    return userdata;
}

static MapScriptTileUserdata* check_tile_userdata(lua_State* L, int index) {
    MapScriptTileUserdata* userdata =
        (MapScriptTileUserdata*)luaL_checkudata(L, index, g_tile_metatable_key);
    if (!userdata || !userdata->runtime ||
        userdata->binding_index >= userdata->runtime->binding_count) {
        luaL_error(L, "expired map tile");
        return NULL;
    }
    return userdata;
}

static int object_set_velocity(lua_State* L) {
    MapScriptObjectUserdata* userdata = check_object_userdata(L, 1);
    float vx;
    float vy;
    if (lua_gettop(L) != 3) return luaL_error(L, "set_velocity expects vx and vy");
    if (!finite_float_value(L, 2, &vx) || !finite_float_value(L, 3, &vy)) return 0;
    userdata->object->vx = vx;
    userdata->object->vy = vy;
    return 0;
}

static int object_add_velocity(lua_State* L) {
    MapScriptObjectUserdata* userdata = check_object_userdata(L, 1);
    float add_x;
    float add_y;
    double result_x;
    double result_y;
    if (lua_gettop(L) != 3) return luaL_error(L, "add_velocity expects vx and vy deltas");
    if (!finite_float_value(L, 2, &add_x) || !finite_float_value(L, 3, &add_y)) return 0;
    result_x = (double)userdata->object->vx + add_x;
    result_y = (double)userdata->object->vy + add_y;
    if (!isfinite(result_x) || !isfinite(result_y) ||
        result_x > FLT_MAX || result_x < -FLT_MAX ||
        result_y > FLT_MAX || result_y < -FLT_MAX) {
        return luaL_error(L, "velocity addition overflowed a finite 32-bit number");
    }
    userdata->object->vx = canonical_float((float)result_x);
    userdata->object->vy = canonical_float((float)result_y);
    return 0;
}

static int find_velocity_limit(MapScriptRuntime* runtime,
                               uint32_t object_id,
                               uint32_t lifecycle_id) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        const MapScriptSnapshotVelocityLimit* limit = &runtime->velocity_limits[i];
        if (limit->in_use && limit->object_id == object_id &&
            limit->lifecycle_id == lifecycle_id) {
            return i;
        }
    }
    return -1;
}

static void clear_velocity_limit_at(MapScriptRuntime* runtime, int index) {
    if (!runtime || index < 0 || index >= MAP_SCRIPT_MAX_VELOCITY_LIMITS ||
        !runtime->velocity_limits[index].in_use) {
        return;
    }
    memset(&runtime->velocity_limits[index], 0,
           sizeof(runtime->velocity_limits[index]));
    runtime->velocity_limit_count--;
}

static void clamp_object_velocity(
        MapScriptObjectView* object,
        const MapScriptSnapshotVelocityLimit* limit) {
    if ((limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX) &&
        object->vx < limit->min_vx) object->vx = limit->min_vx;
    if ((limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX) &&
        object->vx > limit->max_vx) object->vx = limit->max_vx;
    if ((limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY) &&
        object->vy < limit->min_vy) object->vy = limit->min_vy;
    if ((limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY) &&
        object->vy > limit->max_vy) object->vy = limit->max_vy;
    object->vx = canonical_float(object->vx);
    object->vy = canonical_float(object->vy);
}

/* A player becoming a corpse (or being restored to a live player by native
 * rollback state) is the same fixed host slot and generation.  No other kind
 * is permitted to inherit a policy merely because its numeric ids collide. */
static int apply_velocity_limit(MapScriptRuntime* runtime,
                                MapScriptObjectView* object,
                                char* err,
                                size_t err_cap) {
    int index = find_velocity_limit(runtime, object->object_id,
                                    object->lifecycle_id);
    MapScriptSnapshotVelocityLimit* limit;
    if (index < 0) return 1;
    limit = &runtime->velocity_limits[index];
    if (limit->expires_after_tick <= runtime->tick) {
        clear_velocity_limit_at(runtime, index);
        return 1;
    }
    if (!object_kinds_share_lifecycle(limit->object_kind,
                                      object->object_kind)) {
        set_error(err, err_cap,
                  "object refresh changes a velocity-limited native object kind");
        return 0;
    }
    limit->object_kind = object->object_kind;
    clamp_object_velocity(object, limit);
    return 1;
}

static float parse_velocity_limit_bound(lua_State* L,
                                        int value_index,
                                        const char* field) {
    double number;
    if (lua_type(L, value_index) != LUA_TNUMBER) {
        luaL_error(L, "velocity limit %s must be a number", field);
        return 0.0f;
    }
    number = (double)lua_tonumber(L, value_index);
    if (!isfinite(number) || number < -MAP_SCRIPT_VELOCITY_LIMIT_ABS_MAX ||
        number > MAP_SCRIPT_VELOCITY_LIMIT_ABS_MAX) {
        luaL_error(L, "velocity limit %s must be finite and within -64..64", field);
        return 0.0f;
    }
    return canonical_float((float)number);
}

static int object_set_velocity_limits(lua_State* L) {
    MapScriptObjectUserdata* userdata = check_object_userdata(L, 1);
    MapScriptRuntime* runtime = userdata->runtime;
    MapScriptSnapshotVelocityLimit replacement;
    double duration_number;
    uint32_t duration;
    uint64_t expiry;
    int absolute;
    int index;
    int i;
    if (lua_gettop(L) != 3) {
        return luaL_error(
            L, "set_velocity_limits expects an options table and duration_ticks");
    }
    if ((userdata->object->object_id < 2u &&
         userdata->object->lifecycle_id != 0u) ||
        userdata->object->object_id >=
            2u + MAP_SCRIPT_MAX_LIFECYCLE_SLOTS ||
        (userdata->object->object_id >= 2u &&
         userdata->object->lifecycle_id !=
            runtime->lifecycle_generation[userdata->object->object_id - 2u])) {
        return luaL_error(
            L, "set_velocity_limits requires a rollback-tracked object lifecycle");
    }
    absolute = lua_absolute_index(L, 2);
    if (lua_type(L, absolute) != LUA_TTABLE) {
        return luaL_error(L, "set_velocity_limits options must be a table");
    }
    if (table_has_metatable(L, absolute)) {
        return luaL_error(L, "set_velocity_limits options cannot have a metatable");
    }
    if (lua_type(L, 3) != LUA_TNUMBER) {
        return luaL_error(L, "velocity limit duration must be an integer number");
    }
    duration_number = (double)lua_tonumber(L, 3);
    if (!isfinite(duration_number) || floor(duration_number) != duration_number ||
        duration_number < 1.0 ||
        duration_number > (double)MAP_SCRIPT_DURATION_MAX) {
        return luaL_error(
            L, "velocity limit duration must be between 1 and 1000000 ticks");
    }
    duration = (uint32_t)duration_number;
    memset(&replacement, 0, sizeof(replacement));
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        const char* key;
        size_t key_length;
        if (lua_type(L, -2) != LUA_TSTRING) {
            return luaL_error(
                L, "set_velocity_limits options accepts only named fields");
        }
        key = lua_tolstring(L, -2, &key_length);
        if (sensor_string_equals(key, key_length, "min_vx")) {
            replacement.bound_flags |= MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX;
            replacement.min_vx = parse_velocity_limit_bound(L, -1, "min_vx");
        } else if (sensor_string_equals(key, key_length, "max_vx")) {
            replacement.bound_flags |= MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX;
            replacement.max_vx = parse_velocity_limit_bound(L, -1, "max_vx");
        } else if (sensor_string_equals(key, key_length, "min_vy")) {
            replacement.bound_flags |= MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY;
            replacement.min_vy = parse_velocity_limit_bound(L, -1, "min_vy");
        } else if (sensor_string_equals(key, key_length, "max_vy")) {
            replacement.bound_flags |= MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY;
            replacement.max_vy = parse_velocity_limit_bound(L, -1, "max_vy");
        } else {
            return luaL_error(
                L, "set_velocity_limits options has an unknown field '%s'", key);
        }
        lua_pop(L, 1);
    }
    if (replacement.bound_flags == 0) {
        return luaL_error(L, "set_velocity_limits requires at least one bound");
    }
    if ((replacement.bound_flags &
         (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX | MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX)) ==
            (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX | MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX) &&
        replacement.min_vx > replacement.max_vx) {
        return luaL_error(L, "velocity limit min_vx cannot exceed max_vx");
    }
    if ((replacement.bound_flags &
         (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY | MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY)) ==
            (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY | MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY) &&
        replacement.min_vy > replacement.max_vy) {
        return luaL_error(L, "velocity limit min_vy cannot exceed max_vy");
    }

    index = find_velocity_limit(runtime, userdata->object->object_id,
                                userdata->object->lifecycle_id);
    if (index >= 0 &&
        !object_kinds_share_lifecycle(runtime->velocity_limits[index].object_kind,
                                      userdata->object_kind)) {
        return luaL_error(
            L, "set_velocity_limits changes the native object kind for a lifecycle");
    }
    if (index < 0) {
        for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
            if (!runtime->velocity_limits[i].in_use) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            return luaL_error(L, "temporary object velocity limit reached");
        }
        runtime->velocity_limit_count++;
    }
    expiry = runtime->tick + (uint64_t)duration + UINT64_C(1);
    if (expiry < runtime->tick) expiry = UINT64_MAX;
    replacement.in_use = 1;
    replacement.object_kind = userdata->object_kind;
    replacement.object_id = userdata->object->object_id;
    replacement.lifecycle_id = userdata->object->lifecycle_id;
    replacement.expires_after_tick = expiry;
    runtime->velocity_limits[index] = replacement;
    clamp_object_velocity(userdata->object, &runtime->velocity_limits[index]);
    return 0;
}

static int object_clear_velocity_limits(lua_State* L) {
    MapScriptObjectUserdata* userdata = check_object_userdata(L, 1);
    int index;
    if (lua_gettop(L) != 1) {
        return luaL_error(L, "clear_velocity_limits expects no arguments");
    }
    index = find_velocity_limit(userdata->runtime, userdata->object->object_id,
                                userdata->object->lifecycle_id);
    if (index >= 0 && object_kinds_share_lifecycle(
            userdata->runtime->velocity_limits[index].object_kind,
            userdata->object_kind)) {
        clear_velocity_limit_at(userdata->runtime, index);
    }
    return 0;
}

static int object_index(lua_State* L) {
    MapScriptObjectUserdata* userdata = check_object_userdata(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "id") == 0) lua_pushnumber(L, userdata->object->object_id);
    else if (strcmp(key, "kind") == 0) {
        if (userdata->object_kind == MAP_SCRIPT_OBJECT_PLAYER) lua_pushliteral(L, "player");
        else if (userdata->object_kind == MAP_SCRIPT_OBJECT_SWORD) lua_pushliteral(L, "sword");
        else if (userdata->object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY) lua_pushliteral(L, "dead_body");
        else if (userdata->object_kind == MAP_SCRIPT_OBJECT_HAZARD) lua_pushliteral(L, "hazard");
        else lua_pushliteral(L, "unknown");
    }
    else if (strcmp(key, "contact_radius") == 0) {
        lua_pushnumber(L, userdata->contact_radius);
    }
    else if (strcmp(key, "x") == 0) lua_pushnumber(L, userdata->object->x);
    else if (strcmp(key, "y") == 0) lua_pushnumber(L, userdata->object->y);
    else if (strcmp(key, "vx") == 0) lua_pushnumber(L, userdata->object->vx);
    else if (strcmp(key, "vy") == 0) lua_pushnumber(L, userdata->object->vy);
    else if (strcmp(key, "set_velocity") == 0) lua_pushcfunction(L, object_set_velocity);
    else if (strcmp(key, "add_velocity") == 0) lua_pushcfunction(L, object_add_velocity);
    else if (strcmp(key, "set_velocity_limits") == 0) {
        lua_pushcfunction(L, object_set_velocity_limits);
    }
    else if (strcmp(key, "clear_velocity_limits") == 0) {
        lua_pushcfunction(L, object_clear_velocity_limits);
    }
    else lua_pushnil(L);
    return 1;
}

static int object_newindex(lua_State* L) {
    MapScriptObjectUserdata* userdata = check_object_userdata(L, 1);
    const char* key = luaL_checkstring(L, 2);
    float value;
    if (strcmp(key, "x") != 0 && strcmp(key, "y") != 0 &&
        strcmp(key, "vx") != 0 && strcmp(key, "vy") != 0) {
        return luaL_error(L, "unknown or read-only map object property");
    }
    if (!finite_float_value(L, 3, &value)) return 0;
    if (strcmp(key, "x") == 0) userdata->object->x = value;
    else if (strcmp(key, "y") == 0) userdata->object->y = value;
    else if (strcmp(key, "vx") == 0) userdata->object->vx = value;
    else if (strcmp(key, "vy") == 0) userdata->object->vy = value;
    return 0;
}

static int find_override(MapScriptRuntime* runtime, uint32_t cell_index) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_SPRITE_OVERRIDES; ++i) {
        if (runtime->overrides[i].in_use &&
            runtime->overrides[i].cell_index == cell_index) return i;
    }
    return -1;
}

static int32_t quantize_render_offset(lua_State* L,
                                      int value_index,
                                      const char* label) {
    double value;
    double scaled;
    double rounded;
    const int32_t limit_q =
        MAP_SCRIPT_RENDER_OFFSET_LIMIT * MAP_SCRIPT_RENDER_OFFSET_QUANTIZATION;
    if (lua_type(L, value_index) != LUA_TNUMBER) {
        luaL_error(L, "%s must be a number", label);
        return 0;
    }
    value = (double)lua_tonumber(L, value_index);
    if (!isfinite(value) || value < -(double)MAP_SCRIPT_RENDER_OFFSET_LIMIT ||
        value > (double)MAP_SCRIPT_RENDER_OFFSET_LIMIT) {
        luaL_error(L, "%s is non-finite or outside -4096..4096 pixels", label);
        return 0;
    }
    scaled = value * MAP_SCRIPT_RENDER_OFFSET_QUANTIZATION;
    rounded = scaled < 0.0 ? ceil(scaled - 0.5) : floor(scaled + 0.5);
    if (rounded < -(double)limit_q || rounded > (double)limit_q) {
        luaL_error(L, "%s is outside its supported quantized range", label);
        return 0;
    }
    return (int32_t)rounded;
}

static void parse_sprite_options(lua_State* L,
                                 int table_index,
                                 int32_t* out_offset_x_q,
                                 int32_t* out_offset_y_q) {
    int absolute = lua_absolute_index(L, table_index);
    if (lua_type(L, absolute) != LUA_TTABLE) {
        luaL_error(L, "set_sprite options must be a table");
        return;
    }
    if (table_has_metatable(L, absolute)) {
        luaL_error(L, "set_sprite options cannot have a metatable");
        return;
    }
    lua_pushnil(L);
    while (lua_next(L, absolute) != 0) {
        const char* key;
        size_t key_length;
        if (lua_type(L, -2) != LUA_TSTRING) {
            luaL_error(L, "set_sprite options accepts only named fields");
            return;
        }
        key = lua_tolstring(L, -2, &key_length);
        if (sensor_string_equals(key, key_length, "offset_x")) {
            *out_offset_x_q = quantize_render_offset(
                L, -1, "set_sprite options.offset_x");
        } else if (sensor_string_equals(key, key_length, "offset_y")) {
            *out_offset_y_q = quantize_render_offset(
                L, -1, "set_sprite options.offset_y");
        } else {
            luaL_error(L, "set_sprite options has an unknown field '%s'", key);
            return;
        }
        lua_pop(L, 1);
    }
}

static int tile_set_sprite(lua_State* L) {
    MapScriptTileUserdata* userdata = check_tile_userdata(L, 1);
    MapScriptRuntime* runtime = userdata->runtime;
    double sprite_number;
    double duration_number;
    int32_t sprite;
    int32_t offset_x_q = 0;
    int32_t offset_y_q = 0;
    uint32_t duration;
    int index;
    int i;
    uint64_t expiry;
    if (lua_gettop(L) != 3 && lua_gettop(L) != 4) {
        return luaL_error(
            L, "set_sprite expects sprite index, duration_ticks, and optional options");
    }
    sprite_number = (double)luaL_checknumber(L, 2);
    duration_number = (double)luaL_checknumber(L, 3);
    if (!isfinite(sprite_number) || floor(sprite_number) != sprite_number ||
        sprite_number < 0.0 || sprite_number > (double)MAP_SCRIPT_SPRITE_MAX) {
        return luaL_error(L, "sprite index is out of range");
    }
    if (!isfinite(duration_number) || floor(duration_number) != duration_number ||
        duration_number < 1.0 || duration_number > (double)MAP_SCRIPT_DURATION_MAX) {
        return luaL_error(L, "sprite duration must be between 1 and 1000000 ticks");
    }
    sprite = (int32_t)sprite_number;
    duration = (uint32_t)duration_number;
    if (lua_gettop(L) == 4) {
        parse_sprite_options(L, 4, &offset_x_q, &offset_y_q);
    }
    index = find_override(runtime, userdata->cell_index);
    if (index < 0) {
        for (i = 0; i < MAP_SCRIPT_MAX_SPRITE_OVERRIDES; ++i) {
            if (!runtime->overrides[i].in_use) {
                index = i;
                break;
            }
        }
        if (index < 0) return luaL_error(L, "temporary tile sprite limit reached");
        memset(&runtime->overrides[index], 0, sizeof(runtime->overrides[index]));
        runtime->overrides[index].in_use = 1;
        runtime->overrides[index].cell_index = userdata->cell_index;
        runtime->override_count++;
    }
    expiry = runtime->tick + (uint64_t)duration + UINT64_C(1);
    if (expiry < runtime->tick) expiry = UINT64_MAX;
    runtime->overrides[index].sprite_index = sprite;
    runtime->overrides[index].offset_x_q = offset_x_q;
    runtime->overrides[index].offset_y_q = offset_y_q;
    runtime->overrides[index].expires_after_tick = expiry;
    return 0;
}

static int tile_reset_sprite(lua_State* L) {
    MapScriptTileUserdata* userdata = check_tile_userdata(L, 1);
    MapScriptRuntime* runtime = userdata->runtime;
    int index;
    if (lua_gettop(L) != 1) return luaL_error(L, "reset_sprite expects no arguments");
    index = find_override(runtime, userdata->cell_index);
    if (index >= 0) {
        memset(&runtime->overrides[index], 0, sizeof(runtime->overrides[index]));
        runtime->override_count--;
    }
    return 0;
}

static int tile_index(lua_State* L) {
    MapScriptTileUserdata* userdata = check_tile_userdata(L, 1);
    MapScriptBindingOwned* binding = &userdata->runtime->bindings[userdata->binding_index];
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "x") == 0) lua_pushinteger(L, userdata->x);
    else if (strcmp(key, "y") == 0) lua_pushinteger(L, userdata->y);
    else if (strcmp(key, "key") == 0) lua_pushstring(L, binding->qualified_key);
    else if (strcmp(key, "symbol") == 0) {
        char symbol[2] = { binding->symbol, '\0' };
        lua_pushlstring(L, symbol, 1);
    } else if (strcmp(key, "mirrored") == 0) lua_pushboolean(L, userdata->mirrored != 0);
    else if (strcmp(key, "set_sprite") == 0) lua_pushcfunction(L, tile_set_sprite);
    else if (strcmp(key, "reset_sprite") == 0) lua_pushcfunction(L, tile_reset_sprite);
    else lua_pushnil(L);
    return 1;
}

static int tile_newindex(lua_State* L) {
    (void)L;
    return luaL_error(L, "map tile properties are read-only");
}

static int userdata_has_metatable(lua_State* L, int index, const char* key) {
    int equal;
    if (!lua_getmetatable(L, index)) return 0;
    luaL_getmetatable(L, key);
    equal = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return equal;
}

static int deterministic_tostring(lua_State* L) {
    char buffer[192];
    int type = lua_type(L, 1);
    if (lua_gettop(L) != 1) return luaL_error(L, "tostring expects one value");
    switch (type) {
        case LUA_TNIL:
            lua_pushliteral(L, "nil");
            return 1;
        case LUA_TBOOLEAN:
            lua_pushstring(L, lua_toboolean(L, 1) ? "true" : "false");
            return 1;
        case LUA_TSTRING:
            lua_pushvalue(L, 1);
            return 1;
        case LUA_TNUMBER: {
            double number = canonical_double((double)lua_tonumber(L, 1));
            double integer_part;
            if (!isfinite(number)) return luaL_error(L, "cannot stringify a non-finite number");
            if (modf(number, &integer_part) == 0.0 &&
                integer_part >= -9007199254740991.0 &&
                integer_part <= 9007199254740991.0) {
                uint64_t magnitude;
                char digits[32];
                size_t count = 0;
                size_t position = 0;
                int negative = integer_part < 0.0;
                magnitude = negative ? (uint64_t)(-integer_part) : (uint64_t)integer_part;
                do {
                    digits[count++] = (char)('0' + magnitude % 10u);
                    magnitude /= 10u;
                } while (magnitude != 0);
                if (negative) buffer[position++] = '-';
                while (count != 0) buffer[position++] = digits[--count];
                buffer[position] = '\0';
            } else {
                uint64_t bits;
                uint32_t high;
                uint32_t low;
                memcpy(&bits, &number, sizeof(bits));
                high = (uint32_t)(bits >> 32);
                low = (uint32_t)bits;
                snprintf(buffer, sizeof(buffer), "number:0x%08x%08x",
                         (unsigned)high, (unsigned)low);
            }
            lua_pushstring(L, buffer);
            return 1;
        }
        case LUA_TUSERDATA:
            if (userdata_has_metatable(L, 1, g_object_metatable_key)) {
                MapScriptObjectUserdata* object = (MapScriptObjectUserdata*)lua_touserdata(L, 1);
                snprintf(buffer, sizeof(buffer), "object:%u:%u",
                         object->object->object_id,
                         object->object->lifecycle_id);
                lua_pushstring(L, buffer);
                return 1;
            }
            if (userdata_has_metatable(L, 1, g_tile_metatable_key)) {
                MapScriptTileUserdata* tile = (MapScriptTileUserdata*)lua_touserdata(L, 1);
                snprintf(buffer, sizeof(buffer), "tile:%u", tile->cell_index);
                lua_pushstring(L, buffer);
                return 1;
            }
            return luaL_error(L, "cannot stringify opaque userdata");
        default:
            return luaL_error(L, "cannot stringify %s values in deterministic map.lua",
                              lua_typename(L, type));
    }
}

static uint32_t rng_next(MapScriptRuntime* runtime) {
    uint32_t value = runtime->rng_state;
    if (value == 0) value = MAP_SCRIPT_RNG_FALLBACK;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    runtime->rng_state = value ? value : MAP_SCRIPT_RNG_FALLBACK;
    return value;
}

static uint32_t rng_bounded(MapScriptRuntime* runtime, uint32_t span) {
    uint32_t threshold;
    uint32_t value;
    if (span == 0) return rng_next(runtime);
    threshold = (uint32_t)(-span) % span;
    do {
        value = rng_next(runtime);
    } while (value < threshold);
    return value % span;
}

static int exact_integer(lua_State* L, int index, int64_t* out) {
    double number = (double)luaL_checknumber(L, index);
    if (!isfinite(number) || floor(number) != number ||
        number < -2147483647.0 || number > 2147483647.0) {
        return luaL_error(L, "random bounds must be 32-bit integers");
    }
    *out = (int64_t)number;
    return 1;
}

static int api_random(lua_State* L) {
    MapScriptRuntime* runtime = checked_runtime(L);
    int count = lua_gettop(L);
    int64_t lower;
    int64_t upper;
    uint64_t span;
    uint32_t value;
    if (runtime->loading) return luaL_error(L, "map.random is only available inside callbacks");
    if (count == 0) {
        lua_pushnumber(L, (lua_Number)((double)rng_next(runtime) / 4294967296.0));
        return 1;
    }
    if (count == 1) {
        lower = 1;
        if (!exact_integer(L, 1, &upper)) return 0;
    } else if (count == 2) {
        if (!exact_integer(L, 1, &lower) || !exact_integer(L, 2, &upper)) return 0;
    } else {
        return luaL_error(L, "map.random expects zero, one, or two bounds");
    }
    if (upper < lower) return luaL_error(L, "map.random interval is empty");
    span = (uint64_t)(upper - lower) + UINT64_C(1);
    if (span > UINT32_MAX) return luaL_error(L, "map.random interval is too wide");
    value = rng_bounded(runtime, (uint32_t)span);
    lua_pushnumber(L, (lua_Number)(lower + value));
    return 1;
}

static int api_tick(lua_State* L) {
    MapScriptRuntime* runtime = checked_runtime(L);
    if (lua_gettop(L) != 0) return luaL_error(L, "map.tick expects no arguments");
    /* Exact while practical; maps cannot run for 2^53 game ticks. */
    lua_pushnumber(L, (lua_Number)runtime->tick);
    return 1;
}

static void create_metatable(lua_State* L,
                             const char* key,
                             lua_CFunction index,
                             lua_CFunction newindex,
                             const char* label) {
    luaL_newmetatable(L, key);
    lua_pushcfunction(L, index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushstring(L, label);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
}

static void remove_global(lua_State* L, const char* name) {
    lua_pushnil(L);
    lua_setglobal(L, name);
}

static void replace_global_with_readonly_fields(lua_State* L,
                                                const char* name,
                                                const char* const* fields,
                                                size_t field_count) {
    int source_index;
    int backing_index;
    size_t i;
    lua_getglobal(L, name);
    source_index = lua_gettop(L);
    lua_newtable(L);
    backing_index = lua_gettop(L);
    if (lua_istable(L, source_index)) {
        for (i = 0; i < field_count; ++i) {
            lua_getfield(L, source_index, fields[i]);
            if (!lua_isnil(L, -1)) lua_setfield(L, backing_index, fields[i]);
            else lua_pop(L, 1);
        }
    }
    push_readonly_proxy(L, backing_index);
    lua_setglobal(L, name);
    lua_pop(L, 2); /* private backing and original table */
}

static int install_sandbox_entry(lua_State* L) {
    static const char* const map_fields[] = {
        "on_contact", "on_enter", "on_leave", "on_tick", "sensor", "random",
        "tick", "state"
    };
    static const char* const math_fields[] = {
        /* Transcendental CRT/libm results are not bit-stable across every
         * Windows/CPU build.  Keep only exact/basic helpers suitable for
         * rollback decisions; authors still have ordinary Lua arithmetic. */
        "abs", "ceil", "floor", "max", "min"
    };
    static const char* const string_fields[] = {
        "byte", "char", "len", "rep", "reverse", "sub"
    };
    static const char* const table_fields[] = { "concat" };
    static const char* const bit_fields[] = {
        "tobit", "tohex", "bnot", "band", "bor", "bxor", "lshift", "rshift",
        "arshift", "rol", "ror", "bswap"
    };
    static const char* removed_globals[] = {
        "os", "io", "package", "require", "debug", "ffi", "jit",
        "load", "loadfile", "loadstring", "dofile", "module",
        "getfenv", "setfenv", "rawget", "rawset", "getmetatable",
        "setmetatable", "collectgarbage", "gcinfo", "newproxy",
        "coroutine", "pcall", "xpcall", "print", "pairs", "next", "_G",
        "tonumber"
    };
    MapScriptRuntime* runtime = (MapScriptRuntime*)lua_touserdata(L, 1);
    size_t i;

    luaL_openlibs(L);
    if (!luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF)) {
        return luaL_error(L, "could not disable LuaJIT for deterministic callbacks");
    }
    for (i = 0; i < sizeof(removed_globals) / sizeof(removed_globals[0]); ++i) {
        remove_global(L, removed_globals[i]);
    }
    lua_getglobal(L, "math");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "random");
        lua_pushnil(L);
        lua_setfield(L, -2, "randomseed");
    }
    lua_pop(L, 1);
    lua_getglobal(L, "string");
    if (lua_istable(L, -1)) {
        static const char* pattern_functions[] = {
            "dump", "find", "match", "gmatch", "gsub", "format", "lower", "upper"
        };
        for (i = 0; i < sizeof(pattern_functions) / sizeof(pattern_functions[0]); ++i) {
            lua_pushnil(L);
            lua_setfield(L, -2, pattern_functions[i]);
        }
    }
    lua_pop(L, 1);
    lua_getglobal(L, "table");
    if (lua_istable(L, -1)) {
        static const char* mutators[] = {
            "insert", "remove", "sort", "setn", "foreach", "foreachi"
        };
        for (i = 0; i < sizeof(mutators) / sizeof(mutators[0]); ++i) {
            lua_pushnil(L);
            lua_setfield(L, -2, mutators[i]);
        }
    }
    lua_pop(L, 1);
    lua_pushcfunction(L, deterministic_tostring);
    lua_setglobal(L, "tostring");

    create_metatable(L, g_state_metatable_key, state_index, state_newindex,
                     "map.state (rollback-safe scalar store)");
    create_metatable(L, g_object_metatable_key, object_index, object_newindex,
                     "map object");
    create_metatable(L, g_tile_metatable_key, tile_index, tile_newindex,
                     "map tile");

    {
        MapScriptObjectUserdata* object =
            (MapScriptObjectUserdata*)lua_newuserdata(L, sizeof(*object));
        memset(object, 0, sizeof(*object));
        luaL_getmetatable(L, g_object_metatable_key);
        lua_setmetatable(L, -2);
        runtime->object_userdata_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    {
        MapScriptTileUserdata* tile =
            (MapScriptTileUserdata*)lua_newuserdata(L, sizeof(*tile));
        memset(tile, 0, sizeof(*tile));
        luaL_getmetatable(L, g_tile_metatable_key);
        lua_setmetatable(L, -2);
        runtime->tile_userdata_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    lua_newtable(L);
    lua_pushcfunction(L, api_on_contact);
    lua_setfield(L, -2, "on_contact");
    lua_pushcfunction(L, api_on_enter);
    lua_setfield(L, -2, "on_enter");
    lua_pushcfunction(L, api_on_leave);
    lua_setfield(L, -2, "on_leave");
    lua_pushcfunction(L, api_on_tick);
    lua_setfield(L, -2, "on_tick");
    lua_pushcfunction(L, api_sensor);
    lua_setfield(L, -2, "sensor");
    lua_pushcfunction(L, api_random);
    lua_setfield(L, -2, "random");
    lua_pushcfunction(L, api_tick);
    lua_setfield(L, -2, "tick");
    (void)lua_newuserdata(L, 1);
    luaL_getmetatable(L, g_state_metatable_key);
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "state");
    lua_setglobal(L, "map");

    replace_global_with_readonly_fields(L, "map", map_fields,
                                        sizeof(map_fields) / sizeof(map_fields[0]));
    replace_global_with_readonly_fields(L, "math", math_fields,
                                        sizeof(math_fields) / sizeof(math_fields[0]));
    replace_global_with_readonly_fields(L, "string", string_fields,
                                        sizeof(string_fields) / sizeof(string_fields[0]));
    replace_global_with_readonly_fields(L, "table", table_fields,
                                        sizeof(table_fields) / sizeof(table_fields[0]));
    replace_global_with_readonly_fields(L, "bit", bit_fields,
                                        sizeof(bit_fields) / sizeof(bit_fields[0]));
    for (i = 0; i < 5; ++i) {
        lua_getglobal(L, g_safe_table_names[i]);
        if (lua_istable(L, -1)) {
            runtime->safe_table_ref[i] = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
        }
    }
    for (i = 0; i < 8; ++i) {
        lua_getglobal(L, g_safe_function_names[i]);
        if (!lua_isfunction(L, -1)) {
            return luaL_error(L, "map.lua sandbox base helper is unavailable");
        }
        runtime->safe_function_ref[i] = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (!lua_checkstack(L, 32)) return luaL_error(L, "map.lua VM stack allocation failed");
    return 0;
}

static int install_sandbox(MapScriptRuntime* runtime, char* err, size_t err_cap) {
    int status = lua_cpcall(runtime->L, install_sandbox_entry, runtime);
    if (status != 0) {
        const char* detail = lua_tostring(runtime->L, -1);
        set_error(err, err_cap, "could not create map.lua sandbox: %s",
                  detail ? detail : (status == LUA_ERRMEM
                                     ? "memory limit exceeded" : "Lua setup error"));
        lua_pop(runtime->L, 1);
        return 0;
    }
    return 1;
}

static void destroy_runtime(MapScriptRuntime* runtime) {
    if (!runtime) return;
    if (runtime->L) {
        lua_close(runtime->L);
        runtime->L = NULL;
    }
    memset(runtime, 0, sizeof(*runtime));
    free(runtime);
}

static int definition_limits(const MapScriptDefinition* definition,
                             size_t* out_memory,
                             uint32_t* out_instructions,
                             char* err,
                             size_t err_cap) {
    size_t memory = definition->memory_limit_bytes
                  ? definition->memory_limit_bytes : MAP_SCRIPT_DEFAULT_MEMORY_BYTES;
    uint32_t instructions = definition->instruction_budget
                          ? definition->instruction_budget : MAP_SCRIPT_DEFAULT_INSTRUCTIONS;
    if (memory < MAP_SCRIPT_MIN_MEMORY_BYTES || memory > MAP_SCRIPT_MAX_MEMORY_BYTES) {
        set_error(err, err_cap, "map.lua memory limit must be between 256 KiB and 16 MiB");
        return 0;
    }
    if (instructions < 1000u || instructions > MAP_SCRIPT_MAX_INSTRUCTIONS) {
        set_error(err, err_cap, "map.lua instruction budget must be between 1000 and 10000000");
        return 0;
    }
    *out_memory = memory;
    *out_instructions = instructions;
    return 1;
}

static MapScriptRuntime* build_runtime(const MapScriptDefinition* definition,
                                       const MapScriptHost* host,
                                       char* err,
                                       size_t err_cap) {
    MapScriptRuntime* runtime;
    const char* chunk_name;
    int load_status;
    if (!definition) {
        set_error(err, err_cap, "map script definition is required");
        return NULL;
    }
    if (definition->script_id == 0) {
        set_error(err, err_cap, "map.lua script_id must be non-zero");
        return NULL;
    }
    if (!definition->source && definition->source_len != 0) {
        set_error(err, err_cap, "map.lua source pointer is missing");
        return NULL;
    }
    if (definition->source_len > MAP_SCRIPT_SOURCE_MAX) {
        set_error(err, err_cap, "map.lua is larger than 256 KiB");
        return NULL;
    }
    if (definition->source_len && (unsigned char)definition->source[0] == 0x1b) {
        set_error(err, err_cap, "precompiled Lua bytecode is not allowed; provide UTF-8 source");
        return NULL;
    }
    if (source_uses_power_operator(definition->source, definition->source_len)) {
        set_error(err, err_cap,
                  "map.lua power operator '^' is not rollback-deterministic; use explicit multiplication");
        return NULL;
    }
    runtime = (MapScriptRuntime*)calloc(1, sizeof(*runtime));
    if (!runtime) {
        set_error(err, err_cap, "out of memory creating map.lua runtime");
        return NULL;
    }
    runtime->script_id = definition->script_id;
    runtime->rng_state = (host && host->rng_seed) ? host->rng_seed : MAP_SCRIPT_RNG_FALLBACK;
    runtime->locked_env_ref = LUA_NOREF;
    runtime->on_tick_ref = MAP_SCRIPT_CALLBACK_NONE;
    runtime->object_userdata_ref = LUA_NOREF;
    runtime->tile_userdata_ref = LUA_NOREF;
    {
        int safe_index;
        for (safe_index = 0; safe_index < 5; ++safe_index) {
            runtime->safe_table_ref[safe_index] = LUA_NOREF;
        }
        for (safe_index = 0; safe_index < 8; ++safe_index) {
            runtime->safe_function_ref[safe_index] = LUA_NOREF;
        }
    }
    if (host) runtime->host = *host;
    if (!definition_limits(definition, &runtime->memory_limit,
                           &runtime->instruction_budget, err, err_cap) ||
        !copy_bindings(runtime, definition, err, err_cap)) {
        destroy_runtime(runtime);
        return NULL;
    }
    runtime->L = lua_newstate(capped_allocator, runtime);
    if (!runtime->L) {
        set_error(err, err_cap, "map.lua could not create a VM within its memory cap");
        destroy_runtime(runtime);
        return NULL;
    }
    if (!install_sandbox(runtime, err, err_cap)) {
        destroy_runtime(runtime);
        return NULL;
    }
    chunk_name = (definition->chunk_name && definition->chunk_name[0])
               ? definition->chunk_name : "@map.lua";
    load_status = luaL_loadbuffer(runtime->L,
                                  definition->source ? definition->source : "",
                                  definition->source_len,
                                  chunk_name);
    if (load_status != 0) {
        const char* detail = lua_tostring(runtime->L, -1);
        set_error(err, err_cap, "could not compile map.lua: %s",
                  detail ? detail : (load_status == LUA_ERRMEM
                                     ? "memory limit exceeded" : "syntax error"));
        lua_pop(runtime->L, 1);
        destroy_runtime(runtime);
        return NULL;
    }
    runtime->loading = 1;
    if (!protected_call(runtime, 0, 0, "could not initialize map.lua", err, err_cap)) {
        runtime->loading = 0;
        destroy_runtime(runtime);
        return NULL;
    }
    runtime->loading = 0;
    if (!lock_script_environment(runtime, err, err_cap)) {
        destroy_runtime(runtime);
        return NULL;
    }
    runtime->active = 1;
    return runtime;
}

static float contact_radius_for_kind(int object_kind) {
    if (object_kind == MAP_SCRIPT_OBJECT_PLAYER) {
        return MAP_SCRIPT_PLAYER_CONTACT_RADIUS;
    }
    if (object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY) {
        return MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS;
    }
    if (object_kind == MAP_SCRIPT_OBJECT_SWORD) {
        return MAP_SCRIPT_SWORD_CONTACT_RADIUS;
    }
    if (object_kind == MAP_SCRIPT_OBJECT_HAZARD) {
        return MAP_SCRIPT_HAZARD_CONTACT_RADIUS;
    }
    return 0.0f;
}

static uint8_t sensor_bit_for_kind(int object_kind) {
    if (object_kind == MAP_SCRIPT_OBJECT_PLAYER) return MAP_SCRIPT_SENSOR_PLAYER_BIT;
    if (object_kind == MAP_SCRIPT_OBJECT_SWORD) return MAP_SCRIPT_SENSOR_SWORD_BIT;
    if (object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY) return MAP_SCRIPT_SENSOR_DEAD_BODY_BIT;
    if (object_kind == MAP_SCRIPT_OBJECT_HAZARD) return MAP_SCRIPT_SENSOR_HAZARD_BIT;
    return 0;
}

static void push_object_userdata(lua_State* L,
                                 MapScriptRuntime* runtime,
                                 MapScriptObjectView* object,
                                 int object_kind,
                                 float contact_radius) {
    MapScriptObjectUserdata* userdata;
    lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->object_userdata_ref);
    userdata = (MapScriptObjectUserdata*)lua_touserdata(L, -1);
    userdata->runtime = runtime;
    userdata->object = object;
    userdata->object_kind = (uint8_t)object_kind;
    userdata->contact_radius = contact_radius;
}

static void push_tile_userdata(lua_State* L,
                               MapScriptRuntime* runtime,
                               uint16_t binding_index,
                               uint32_t cell_index,
                               int32_t x,
                               int32_t y,
                               int mirrored) {
    MapScriptTileUserdata* userdata;
    lua_rawgeti(L, LUA_REGISTRYINDEX, runtime->tile_userdata_ref);
    userdata = (MapScriptTileUserdata*)lua_touserdata(L, -1);
    userdata->runtime = runtime;
    userdata->binding_index = binding_index;
    userdata->cell_index = cell_index;
    userdata->x = x;
    userdata->y = y;
    userdata->mirrored = (uint8_t)(mirrored != 0);
}

static int invoke_contact_callback(MapScriptRuntime* runtime,
                                   int reference,
                                   MapScriptObjectView* object,
                                   int object_kind,
                                   float contact_radius,
                                   uint16_t binding_index,
                                   uint32_t cell_index,
                                   int32_t x,
                                   int32_t y,
                                   int mirrored,
                                   const char* label,
                                   char* err,
                                   size_t err_cap) {
    lua_State* L;
    if (reference == MAP_SCRIPT_CALLBACK_NONE) return 1;
    L = runtime->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, reference);
    push_object_userdata(L, runtime, object, object_kind, contact_radius);
    push_tile_userdata(L, runtime, binding_index, cell_index, x, y, mirrored);
    return protected_call(runtime, 2, 0, label, err, err_cap);
}

static int invoke_tick_callback(MapScriptRuntime* runtime,
                                char* err,
                                size_t err_cap) {
    if (runtime->on_tick_ref == MAP_SCRIPT_CALLBACK_NONE) return 1;
    lua_rawgeti(runtime->L, LUA_REGISTRYINDEX, runtime->on_tick_ref);
    return protected_call(runtime, 0, 0, "map.on_tick failed", err, err_cap);
}

static void snapshot_payload_restore(MapScriptRuntime* runtime,
                                     const MapScriptSnapshot* snapshot) {
    runtime->tick = snapshot->tick;
    runtime->rng_state = snapshot->rng_state;
    runtime->faulted = (snapshot->flags & MAP_SCRIPT_FLAG_FAULTED) != 0;
    runtime->state_count = snapshot->state_count;
    runtime->override_count = snapshot->override_count;
    runtime->contact_count = snapshot->contact_count;
    runtime->velocity_limit_count = snapshot->velocity_limit_count;
    memcpy(runtime->lifecycle_generation, snapshot->lifecycle_generation,
           sizeof(runtime->lifecycle_generation));
    memcpy(runtime->state, snapshot->state, sizeof(runtime->state));
    memcpy(runtime->overrides, snapshot->overrides, sizeof(runtime->overrides));
    memcpy(runtime->contacts, snapshot->contacts, sizeof(runtime->contacts));
    memcpy(runtime->velocity_limits, snapshot->velocity_limits,
           sizeof(runtime->velocity_limits));
}

static void runtime_fault(MapScriptRuntime* runtime, const char* message) {
    char log_message[512];
    int i;
    if (!runtime || runtime->faulted) return;
    runtime->faulted = 1;
    memset(runtime->overrides, 0, sizeof(runtime->overrides));
    memset(runtime->contacts, 0, sizeof(runtime->contacts));
    memset(runtime->velocity_limits, 0, sizeof(runtime->velocity_limits));
    runtime->override_count = 0;
    runtime->contact_count = 0;
    runtime->velocity_limit_count = 0;
    remember_error(runtime, message);
    snprintf(log_message, sizeof(log_message),
             "map.lua disabled for this rollback timeline: %s",
             message && message[0] ? message : "runtime fault");
    log_message[sizeof(log_message) - 1] = '\0';
    if (runtime->host.log_fn) runtime->host.log_fn(runtime->host.userdata, log_message);
    else fprintf(stderr, "%s\n", log_message);
    /* A fault is terminal until a snapshot from before it is loaded. */
    for (i = 0; i < MAP_SCRIPT_MAX_SPRITE_OVERRIDES; ++i) {
        runtime->overrides[i].in_use = 0;
    }
}

int map_script_object_lifecycle_advance(uint32_t slot) {
    MapScriptRuntime* runtime = g_runtime;
    uint32_t object_id;
    uint32_t generation;
    int i;
    if (!runtime || !runtime->active) return 0;
    if (slot >= MAP_SCRIPT_MAX_LIFECYCLE_SLOTS) return 0;
    if (runtime->lifecycle_generation[slot] == UINT32_MAX) {
        runtime_fault(runtime, "map object lifecycle counter overflowed");
        return 0;
    }
    runtime->lifecycle_generation[slot]++;
    object_id = 2u + slot;
    generation = runtime->lifecycle_generation[slot];
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        if (runtime->velocity_limits[i].in_use &&
            runtime->velocity_limits[i].object_id == object_id &&
            runtime->velocity_limits[i].lifecycle_id != generation) {
            clear_velocity_limit_at(runtime, i);
        }
    }
    return 1;
}

uint32_t map_script_object_lifecycle_current(uint32_t slot) {
    MapScriptRuntime* runtime = g_runtime;
    if (!runtime || !runtime->active || slot >= MAP_SCRIPT_MAX_LIFECYCLE_SLOTS) return 0;
    return runtime->lifecycle_generation[slot];
}

int map_script_validate(const MapScriptDefinition* definition,
                        char* err,
                        size_t err_cap) {
    MapScriptRuntime* temporary;
    if (err && err_cap) err[0] = '\0';
    temporary = build_runtime(definition, NULL, err, err_cap);
    if (!temporary) return 0;
    destroy_runtime(temporary);
    return 1;
}

int map_script_activate(const MapScriptDefinition* definition,
                        const MapScriptHost* host,
                        char* err,
                        size_t err_cap) {
    MapScriptRuntime* replacement;
    MapScriptRuntime* previous;
    if (err && err_cap) err[0] = '\0';
    replacement = build_runtime(definition, host, err, err_cap);
    if (!replacement) {
        if (err && err_cap && err[0]) remember_error(NULL, err);
        return 0;
    }
    previous = g_runtime;
    g_runtime = replacement;
    g_last_error[0] = '\0';
    destroy_runtime(previous);
    return 1;
}

void map_script_deactivate(void) {
    MapScriptRuntime* previous = g_runtime;
    g_runtime = NULL;
    destroy_runtime(previous);
}

int map_script_is_active(void) {
    return g_runtime && g_runtime->active;
}

int map_script_is_faulted(void) {
    return g_runtime && g_runtime->faulted;
}

uint64_t map_script_active_id(void) {
    return g_runtime ? g_runtime->script_id : UINT64_C(0);
}

uint64_t map_script_tick_count(void) {
    return g_runtime ? g_runtime->tick : UINT64_C(0);
}

const char* map_script_last_error(void) {
    if (g_runtime && g_runtime->last_error[0]) return g_runtime->last_error;
    return g_last_error;
}

int map_script_binding_has_sensor(const char* qualified_key) {
    MapScriptRuntime* runtime = g_runtime;
    char normalized[MAP_SCRIPT_TILE_KEY_MAX];
    size_t i;
    if (!runtime || !runtime->active ||
        !normalize_qualified_key(qualified_key, normalized)) {
        return 0;
    }
    for (i = 0; i < runtime->binding_count; ++i) {
        if (strcmp(runtime->bindings[i].qualified_key, normalized) == 0) {
            return runtime->bindings[i].sensor.configured != 0;
        }
    }
    return 0;
}

static int resolve_contact_binding(MapScriptRuntime* runtime,
                                   const MapScriptContactView* contact,
                                   char* err,
                                   size_t err_cap) {
    int symbol_binding = -1;
    int key_binding = -1;
    size_t i;
    char normalized[MAP_SCRIPT_TILE_KEY_MAX];
    if (contact->symbol != '\0') {
        for (i = 0; i < runtime->binding_count; ++i) {
            if (runtime->bindings[i].symbol == contact->symbol) {
                symbol_binding = (int)i;
                break;
            }
        }
        if (symbol_binding < 0) {
            set_error(err, err_cap, "contact uses unknown map tile symbol 0x%02x",
                      (unsigned)(unsigned char)contact->symbol);
            return -1;
        }
    }
    if (contact->qualified_key && contact->qualified_key[0]) {
        if (!normalize_qualified_key(contact->qualified_key, normalized)) {
            set_error(err, err_cap, "contact has an invalid qualified tile key");
            return -1;
        }
        for (i = 0; i < runtime->binding_count; ++i) {
            if (strcmp(runtime->bindings[i].qualified_key, normalized) == 0) {
                key_binding = (int)i;
                break;
            }
        }
        if (key_binding < 0) {
            set_error(err, err_cap, "contact uses unknown map tile key '%s'", normalized);
            return -1;
        }
    }
    if (symbol_binding < 0 && key_binding < 0) {
        set_error(err, err_cap, "contact needs a tile symbol or qualified key");
        return -1;
    }
    if (symbol_binding >= 0 && key_binding >= 0 && symbol_binding != key_binding) {
        set_error(err, err_cap, "contact tile symbol and qualified key disagree");
        return -1;
    }
    return symbol_binding >= 0 ? symbol_binding : key_binding;
}

static int find_contact(MapScriptRuntime* runtime,
                        uint32_t object_id,
                        uint32_t lifecycle_id,
                        uint16_t binding_index,
                        uint32_t cell_index,
                        uint8_t contact_scope) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        MapScriptSnapshotContact* contact = &runtime->contacts[i];
        if (contact->in_use && contact->object_id == object_id &&
            contact->lifecycle_id == lifecycle_id &&
            contact->contact_scope == contact_scope &&
            ((contact_scope == MAP_SCRIPT_CONTACT_SCOPE_BINDING &&
              contact->binding_index == binding_index) ||
             (contact_scope == MAP_SCRIPT_CONTACT_SCOPE_CELL &&
              contact->cell_index == cell_index))) return i;
    }
    return -1;
}

static void contact_store_object(MapScriptSnapshotContact* target,
                                 const MapScriptObjectView* object) {
    target->object_kind = object->object_kind;
    target->object_x = canonical_float(object->x);
    target->object_y = canonical_float(object->y);
    target->object_vx = canonical_float(object->vx);
    target->object_vy = canonical_float(object->vy);
}

static void contact_load_object(const MapScriptSnapshotContact* source,
                                MapScriptObjectView* object) {
    memset(object, 0, sizeof(*object));
    object->object_id = source->object_id;
    object->lifecycle_id = source->lifecycle_id;
    object->object_kind = source->object_kind;
    object->x = source->object_x;
    object->y = source->object_y;
    object->vx = source->object_vx;
    object->vy = source->object_vy;
}

static int dispatch_contact_profile(MapScriptContactView* contact,
                                    int object_kind,
                                    float contact_radius,
                                    char* err,
                                    size_t err_cap) {
    MapScriptRuntime* runtime = g_runtime;
    MapScriptSnapshot before;
    MapScriptObjectView staged;
    MapScriptSnapshotContact* tracked;
    int binding;
    int contact_index;
    int is_enter;
    uint8_t contact_scope;
    int i;
    char callback_error[384];
    if (err && err_cap) err[0] = '\0';
    if (!runtime || !runtime->active) {
        set_error(err, err_cap, "no map.lua runtime is active");
        return 0;
    }
    if (runtime->faulted) {
        set_error(err, err_cap, "%s", runtime->last_error[0]
                  ? runtime->last_error : "map.lua is disabled after a runtime fault");
        return 0;
    }
    if (!contact || !object_physics_valid(contact->object)) {
        set_error(err, err_cap, "contact needs a finite map script object");
        return 0;
    }
    if (!object_profile_valid(contact->object) ||
        object_kind != contact->object->object_kind ||
        sensor_bit_for_kind(object_kind) == 0 ||
        contact_radius != contact_radius_for_kind(object_kind)) {
        set_error(err, err_cap, "contact has an invalid native object profile");
        return 0;
    }
    if (contact->room_mirrored != 0 && contact->room_mirrored != 1) {
        set_error(err, err_cap, "contact room_mirrored must be zero or one");
        return 0;
    }
    binding = resolve_contact_binding(runtime, contact, err, err_cap);
    if (binding < 0) return 0;
    contact_scope = runtime->bindings[binding].sensor.configured
                  ? runtime->bindings[binding].sensor.contact_scope
                  : MAP_SCRIPT_CONTACT_SCOPE_CELL;
    contact_index = find_contact(runtime, contact->object->object_id,
                                 contact->object->lifecycle_id,
                                 (uint16_t)binding, contact->cell_index,
                                 contact_scope);
    if (contact_index >= 0 &&
        runtime->contacts[contact_index].last_seen_tick == runtime->tick) {
        if (contact_scope == MAP_SCRIPT_CONTACT_SCOPE_BINDING) {
            /* The host enumerates cells in deterministic y/x order. The first
             * matching cell owns this tick's callback/tile userdata; further
             * cells merely keep the binding-wide contact alive. */
            if (!object_kinds_share_lifecycle(
                    runtime->contacts[contact_index].object_kind,
                    object_kind) ||
                runtime->contacts[contact_index].binding_index !=
                    (uint16_t)binding) {
                set_error(err, err_cap,
                          "binding-scoped contact changed its native object profile");
                return 0;
            }
            canonicalize_object(contact->object);
            if (!apply_velocity_limit(runtime, contact->object, err, err_cap)) {
                return 0;
            }
            contact_store_object(&runtime->contacts[contact_index],
                                 contact->object);
            return 1;
        }
        set_error(err, err_cap,
                  "duplicate contact dispatch for object %u lifecycle %u and cell %u",
                  contact->object->object_id, contact->object->lifecycle_id,
                  contact->cell_index);
        return 0;
    }
    if (!map_script_snapshot_save(&before, callback_error, sizeof(callback_error))) {
        set_error(err, err_cap, "%s", callback_error);
        return 0;
    }
    staged = *contact->object;
    canonicalize_object(&staged);
    is_enter = contact_index < 0;
    if (is_enter) {
        for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
            if (!runtime->contacts[i].in_use) {
                contact_index = i;
                break;
            }
        }
        if (contact_index < 0) {
            snapshot_payload_restore(runtime, &before);
            runtime_fault(runtime, "active contact limit reached");
            set_error(err, err_cap, "%s", runtime->last_error);
            return 0;
        }
        tracked = &runtime->contacts[contact_index];
        memset(tracked, 0, sizeof(*tracked));
        tracked->in_use = 1;
        tracked->object_kind = (uint8_t)object_kind;
        tracked->binding_index = (uint16_t)binding;
        tracked->object_id = staged.object_id;
        tracked->lifecycle_id = staged.lifecycle_id;
        tracked->cell_index = contact->cell_index;
        tracked->tile_x = contact->tile_x;
        tracked->tile_y = contact->tile_y;
        tracked->room_mirrored = (uint8_t)contact->room_mirrored;
        tracked->contact_scope = contact_scope;
        runtime->contact_count++;
    } else {
        tracked = &runtime->contacts[contact_index];
        if (!object_kinds_share_lifecycle(tracked->object_kind, object_kind) ||
            tracked->binding_index != (uint16_t)binding ||
            tracked->contact_scope != contact_scope) {
            set_error(err, err_cap,
                      "contact changed its active binding or native object profile");
            return 0;
        }
        if (contact_scope == MAP_SCRIPT_CONTACT_SCOPE_CELL) {
            if (tracked->tile_x != contact->tile_x ||
                tracked->tile_y != contact->tile_y ||
                tracked->room_mirrored != (uint8_t)contact->room_mirrored) {
                set_error(err, err_cap,
                          "contact metadata changed for an active object/cell pair");
                return 0;
            }
        } else {
            /* A different representative without an intervening empty tick is
             * a stay, not a fresh enter. Stable host order selects it. */
            tracked->cell_index = contact->cell_index;
            tracked->tile_x = contact->tile_x;
            tracked->tile_y = contact->tile_y;
            tracked->room_mirrored = (uint8_t)contact->room_mirrored;
        }
    }
    tracked->last_seen_tick = runtime->tick;
    contact_store_object(tracked, &staged);
    callback_error[0] = '\0';
    if (is_enter && !invoke_contact_callback(
            runtime, runtime->bindings[binding].callback_ref[MAP_SCRIPT_EVENT_ENTER],
            &staged, object_kind, contact_radius, (uint16_t)binding,
            tracked->cell_index,
            tracked->tile_x, tracked->tile_y, tracked->room_mirrored,
            "map.on_enter failed", callback_error, sizeof(callback_error))) {
        snapshot_payload_restore(runtime, &before);
        runtime_fault(runtime, callback_error);
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    if (!invoke_contact_callback(
            runtime, runtime->bindings[binding].callback_ref[MAP_SCRIPT_EVENT_CONTACT],
            &staged, object_kind, contact_radius, (uint16_t)binding,
            tracked->cell_index,
            tracked->tile_x, tracked->tile_y, tracked->room_mirrored,
            "map.on_contact failed", callback_error, sizeof(callback_error))) {
        snapshot_payload_restore(runtime, &before);
        runtime_fault(runtime, callback_error);
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    canonicalize_object(&staged);
    callback_error[0] = '\0';
    if (!apply_velocity_limit(runtime, &staged,
                              callback_error, sizeof(callback_error))) {
        snapshot_payload_restore(runtime, &before);
        runtime_fault(runtime, callback_error);
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        if (runtime->contacts[i].in_use &&
            runtime->contacts[i].object_id == staged.object_id &&
            runtime->contacts[i].lifecycle_id == staged.lifecycle_id) {
            contact_store_object(&runtime->contacts[i], &staged);
        }
    }
    *contact->object = staged;
    return 1;
}

int map_script_dispatch_contact(MapScriptContactView* contact,
                                char* err,
                                size_t err_cap) {
    if (!contact || !contact->object) {
        if (err && err_cap) err[0] = '\0';
        set_error(err, err_cap, "contact needs a finite map script object");
        return 0;
    }
    return dispatch_contact_profile(contact, contact->object->object_kind,
                                    contact_radius_for_kind(
                                        contact->object->object_kind),
                                    err, err_cap);
}

static int world_float_to_sensor_q(float value, int64_t* out) {
    double scaled;
    double rounded;
    /* Staying inside the exactly represented integer range also makes the
     * float-to-int conversion and every subsequent AABB operation defined. */
    const double exact_integer_limit = 9007199254740991.0; /* 2^53 - 1 */
    if (!out || !isfinite(value)) return 0;
    scaled = (double)value * MAP_SCRIPT_SENSOR_QUANTIZATION;
    if (!isfinite(scaled) || scaled < -exact_integer_limit ||
        scaled > exact_integer_limit) {
        return 0;
    }
    rounded = scaled < 0.0 ? ceil(scaled - 0.5) : floor(scaled + 0.5);
    *out = (int64_t)rounded;
    return 1;
}

int map_script_dispatch_cell_candidate(MapScriptCellCandidateView* candidate,
                                       char* err,
                                       size_t err_cap) {
    MapScriptRuntime* runtime = g_runtime;
    MapScriptContactView contact;
    MapScriptSensorOwned* sensor;
    int binding;
    uint8_t object_bit;
    int64_t center_x_q;
    int64_t center_y_q;
    int64_t tile_origin_x_q;
    int64_t tile_origin_y_q;
    int64_t tile_left_q;
    int64_t tile_top_q;
    int64_t tile_right_q;
    int64_t tile_bottom_q;
    int64_t object_left_q;
    int64_t object_top_q;
    int64_t object_right_q;
    int64_t object_bottom_q;
    int32_t sensor_left_q;
    int32_t sensor_right_q;
    int64_t radius_q;
    if (err && err_cap) err[0] = '\0';
    if (!runtime || !runtime->active) {
        set_error(err, err_cap, "no map.lua runtime is active");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    if (runtime->faulted) {
        set_error(err, err_cap, "%s", runtime->last_error[0]
                  ? runtime->last_error : "map.lua is disabled after a runtime fault");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    if (!candidate || !object_physics_valid(candidate->object)) {
        set_error(err, err_cap, "sensor candidate needs a finite map script object");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    if (candidate->room_mirrored != 0 && candidate->room_mirrored != 1) {
        set_error(err, err_cap, "sensor candidate room_mirrored must be zero or one");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    if (candidate->tile_x < 0 || candidate->tile_y < 0 ||
        candidate->tile_width <= 0 || candidate->tile_height <= 0 ||
        candidate->tile_width > MAP_SCRIPT_SENSOR_TILE_DIM_MAX ||
        candidate->tile_height > MAP_SCRIPT_SENSOR_TILE_DIM_MAX) {
        set_error(err, err_cap,
                  "sensor candidate needs non-negative cell coordinates and 1-4096 pixel tiles");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    object_bit = sensor_bit_for_kind(candidate->object_kind);
    if (!object_profile_valid(candidate->object) || object_bit == 0 ||
        candidate->object_kind != candidate->object->object_kind ||
        candidate->contact_radius != contact_radius_for_kind(candidate->object_kind)) {
        set_error(err, err_cap, "sensor candidate has an invalid native object profile");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    memset(&contact, 0, sizeof(contact));
    contact.object = candidate->object;
    contact.cell_index = candidate->cell_index;
    contact.tile_x = candidate->tile_x;
    contact.tile_y = candidate->tile_y;
    contact.symbol = candidate->symbol;
    contact.room_mirrored = candidate->room_mirrored;
    contact.qualified_key = candidate->qualified_key;
    binding = resolve_contact_binding(runtime, &contact, err, err_cap);
    if (binding < 0) return MAP_SCRIPT_CANDIDATE_ERROR;
    sensor = &runtime->bindings[binding].sensor;
    if (!sensor->configured) return MAP_SCRIPT_CANDIDATE_NO_SENSOR;
    if ((sensor->object_mask & object_bit) == 0) {
        return MAP_SCRIPT_CANDIDATE_OUTSIDE;
    }
    if (!world_float_to_sensor_q(candidate->sensor_x, &center_x_q) ||
        !world_float_to_sensor_q(candidate->sensor_y, &center_y_q)) {
        set_error(err, err_cap, "sensor candidate position is outside the fixed-point range");
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }

    tile_origin_x_q = (int64_t)candidate->tile_x * candidate->tile_width *
                      MAP_SCRIPT_SENSOR_QUANTIZATION;
    tile_origin_y_q = (int64_t)candidate->tile_y * candidate->tile_height *
                      MAP_SCRIPT_SENSOR_QUANTIZATION;
    sensor_left_q = sensor->tile_left_q;
    sensor_right_q = sensor->tile_right_q;
    if (sensor->mirror_with_room && candidate->room_mirrored) {
        sensor_left_q = MAP_SCRIPT_SENSOR_QUANTIZATION - sensor->tile_right_q;
        sensor_right_q = MAP_SCRIPT_SENSOR_QUANTIZATION - sensor->tile_left_q;
    }
    tile_left_q = tile_origin_x_q + (int64_t)sensor_left_q * candidate->tile_width;
    tile_right_q = tile_origin_x_q + (int64_t)sensor_right_q * candidate->tile_width;
    tile_top_q = tile_origin_y_q +
                 (int64_t)sensor->tile_top_q * candidate->tile_height;
    tile_bottom_q = tile_origin_y_q +
                    (int64_t)sensor->tile_bottom_q * candidate->tile_height;

    radius_q = (int64_t)(candidate->contact_radius * MAP_SCRIPT_SENSOR_QUANTIZATION);
    switch (sensor->object_profile) {
        case MAP_SCRIPT_SENSOR_CENTER:
            object_left_q = object_right_q = center_x_q;
            object_top_q = object_bottom_q = center_y_q;
            break;
        case MAP_SCRIPT_SENSOR_BODY:
            object_left_q = center_x_q - radius_q;
            object_right_q = center_x_q + radius_q;
            object_top_q = center_y_q - radius_q;
            object_bottom_q = center_y_q + radius_q;
            break;
        case MAP_SCRIPT_SENSOR_FEET:
            object_left_q = center_x_q - radius_q;
            object_right_q = center_x_q + radius_q;
            object_top_q = object_bottom_q = center_y_q + radius_q;
            break;
        case MAP_SCRIPT_SENSOR_CUSTOM:
            object_left_q = center_x_q +
                            (int64_t)sensor->object_left_q * candidate->tile_width;
            object_right_q = center_x_q +
                             (int64_t)sensor->object_right_q * candidate->tile_width;
            object_top_q = center_y_q +
                           (int64_t)sensor->object_top_q * candidate->tile_height;
            object_bottom_q = center_y_q +
                              (int64_t)sensor->object_bottom_q * candidate->tile_height;
            break;
        default:
            set_error(err, err_cap, "map.sensor has an invalid object profile");
            return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    if (object_right_q < tile_left_q || object_left_q > tile_right_q ||
        object_bottom_q < tile_top_q || object_top_q > tile_bottom_q) {
        return MAP_SCRIPT_CANDIDATE_OUTSIDE;
    }
    if (!dispatch_contact_profile(&contact, candidate->object_kind,
                                  candidate->contact_radius, err, err_cap)) {
        return MAP_SCRIPT_CANDIDATE_ERROR;
    }
    return MAP_SCRIPT_CANDIDATE_DISPATCHED;
}

int map_script_update_object(MapScriptObjectView* object,
                             char* err,
                             size_t err_cap) {
    MapScriptRuntime* runtime = g_runtime;
    MapScriptObjectView canonical;
    int i;
    if (err && err_cap) err[0] = '\0';
    if (!runtime || !runtime->active) {
        set_error(err, err_cap, "no map.lua runtime is active");
        return 0;
    }
    if (runtime->faulted) {
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    if (!object_physics_valid(object)) {
        set_error(err, err_cap, "object refresh needs finite physics values");
        return 0;
    }
    if (!object_profile_valid(object)) {
        set_error(err, err_cap,
                  "object refresh needs a valid native object profile");
        return 0;
    }
    canonical = *object;
    canonicalize_object(&canonical);
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        if (runtime->contacts[i].in_use &&
            runtime->contacts[i].object_id == canonical.object_id &&
            runtime->contacts[i].lifecycle_id == canonical.lifecycle_id &&
            !object_kinds_share_lifecycle(runtime->contacts[i].object_kind,
                                          canonical.object_kind)) {
            set_error(err, err_cap,
                      "object refresh changes an active native object kind");
            return 0;
        }
    }
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        if (runtime->velocity_limits[i].in_use &&
            runtime->velocity_limits[i].object_id == canonical.object_id &&
            runtime->velocity_limits[i].lifecycle_id == canonical.lifecycle_id &&
            !object_kinds_share_lifecycle(
                runtime->velocity_limits[i].object_kind,
                canonical.object_kind)) {
            set_error(err, err_cap,
                      "object refresh changes a velocity-limited native object kind");
            return 0;
        }
    }
    if (!apply_velocity_limit(runtime, &canonical, err, err_cap)) return 0;
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        if (runtime->contacts[i].in_use &&
            runtime->contacts[i].object_id == canonical.object_id &&
            runtime->contacts[i].lifecycle_id == canonical.lifecycle_id) {
            contact_store_object(&runtime->contacts[i], &canonical);
        }
    }
    *object = canonical;
    return 1;
}

typedef struct PendingObjectUpdate {
    int in_use;
    MapScriptObjectView object;
} PendingObjectUpdate;

static PendingObjectUpdate* pending_object(PendingObjectUpdate* pending,
                                           size_t* count,
                                           const MapScriptSnapshotContact* contact) {
    size_t i;
    for (i = 0; i < *count; ++i) {
        if (pending[i].object.object_id == contact->object_id &&
            pending[i].object.lifecycle_id == contact->lifecycle_id) {
            return &pending[i];
        }
    }
    pending[*count].in_use = 1;
    contact_load_object(contact, &pending[*count].object);
    (*count)++;
    return &pending[*count - 1];
}

int map_script_dispatch_tick(char* err, size_t err_cap) {
    MapScriptRuntime* runtime = g_runtime;
    MapScriptSnapshot before;
    PendingObjectUpdate pending[MAP_SCRIPT_MAX_CONTACTS];
    size_t pending_count = 0;
    int i;
    int j;
    char callback_error[384];
    if (err && err_cap) err[0] = '\0';
    if (!runtime || !runtime->active) {
        set_error(err, err_cap, "no map.lua runtime is active");
        return 0;
    }
    if (runtime->faulted) {
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    if (runtime->tick == UINT64_MAX) {
        runtime_fault(runtime, "map.lua tick counter overflowed");
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    if (!map_script_snapshot_save(&before, callback_error, sizeof(callback_error))) {
        set_error(err, err_cap, "%s", callback_error);
        return 0;
    }
    memset(pending, 0, sizeof(pending));
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        MapScriptSnapshotContact* contact = &runtime->contacts[i];
        PendingObjectUpdate* update;
        uint16_t binding;
        if (!contact->in_use || contact->last_seen_tick == runtime->tick) continue;
        binding = contact->binding_index;
        update = pending_object(pending, &pending_count, contact);
        callback_error[0] = '\0';
        if (!invoke_contact_callback(
                runtime, runtime->bindings[binding].callback_ref[MAP_SCRIPT_EVENT_LEAVE],
                &update->object,
                contact->object_kind,
                contact_radius_for_kind(contact->object_kind),
                binding, contact->cell_index,
                contact->tile_x, contact->tile_y, contact->room_mirrored,
                "map.on_leave failed", callback_error, sizeof(callback_error))) {
            snapshot_payload_restore(runtime, &before);
            runtime_fault(runtime, callback_error);
            set_error(err, err_cap, "%s", runtime->last_error);
            return 0;
        }
        memset(contact, 0, sizeof(*contact));
        runtime->contact_count--;
    }
    callback_error[0] = '\0';
    if (!invoke_tick_callback(runtime, callback_error, sizeof(callback_error))) {
        snapshot_payload_restore(runtime, &before);
        runtime_fault(runtime, callback_error);
        set_error(err, err_cap, "%s", runtime->last_error);
        return 0;
    }
    for (i = 0; i < (int)pending_count; ++i) {
        callback_error[0] = '\0';
        canonicalize_object(&pending[i].object);
        if (!apply_velocity_limit(runtime, &pending[i].object,
                                  callback_error, sizeof(callback_error))) {
            snapshot_payload_restore(runtime, &before);
            runtime_fault(runtime, callback_error);
            set_error(err, err_cap, "%s", runtime->last_error);
            return 0;
        }
    }
    runtime->tick++;
    for (i = 0; i < MAP_SCRIPT_MAX_SPRITE_OVERRIDES; ++i) {
        MapScriptSnapshotSpriteOverride* override = &runtime->overrides[i];
        if (override->in_use && override->expires_after_tick <= runtime->tick) {
            memset(override, 0, sizeof(*override));
            runtime->override_count--;
        }
    }
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        MapScriptSnapshotVelocityLimit* limit = &runtime->velocity_limits[i];
        if (limit->in_use && limit->expires_after_tick <= runtime->tick) {
            clear_velocity_limit_at(runtime, i);
        }
    }
    for (i = 0; i < (int)pending_count; ++i) {
        canonicalize_object(&pending[i].object);
        for (j = 0; j < MAP_SCRIPT_MAX_CONTACTS; ++j) {
            if (runtime->contacts[j].in_use &&
                runtime->contacts[j].object_id == pending[i].object.object_id &&
                runtime->contacts[j].lifecycle_id == pending[i].object.lifecycle_id) {
                contact_store_object(&runtime->contacts[j], &pending[i].object);
            }
        }
        if (runtime->host.apply_object_fn) {
            runtime->host.apply_object_fn(runtime->host.userdata, &pending[i].object);
        }
    }
    return 1;
}

int map_script_visual_override(uint32_t cell_index,
                               MapScriptVisualOverride* out) {
    int index;
    if (!g_runtime || !g_runtime->active || g_runtime->faulted) return 0;
    index = find_override(g_runtime, cell_index);
    if (index < 0 ||
        g_runtime->overrides[index].expires_after_tick <= g_runtime->tick) return 0;
    if (out) {
        out->sprite_index = g_runtime->overrides[index].sprite_index;
        out->offset_x = (float)g_runtime->overrides[index].offset_x_q /
                        (float)MAP_SCRIPT_RENDER_OFFSET_QUANTIZATION;
        out->offset_y = (float)g_runtime->overrides[index].offset_y_q /
                        (float)MAP_SCRIPT_RENDER_OFFSET_QUANTIZATION;
        out->expires_after_tick =
            g_runtime->overrides[index].expires_after_tick;
    }
    return 1;
}

int map_script_sprite_override(uint32_t cell_index,
                               int* out_sprite_index,
                               uint64_t* out_expires_after_tick) {
    MapScriptVisualOverride visual;
    if (!map_script_visual_override(cell_index, &visual)) return 0;
    if (out_sprite_index) *out_sprite_index = visual.sprite_index;
    if (out_expires_after_tick) {
        *out_expires_after_tick = visual.expires_after_tick;
    }
    return 1;
}

static uint32_t snapshot_checksum(const MapScriptSnapshot* snapshot) {
    const unsigned char* bytes = (const unsigned char*)snapshot;
    const size_t checksum_begin = offsetof(MapScriptSnapshot, checksum);
    const size_t checksum_end = checksum_begin + sizeof(snapshot->checksum);
    uint32_t crc = UINT32_MAX;
    size_t i;
    int bit;
    for (i = 0; i < sizeof(*snapshot); ++i) {
        uint8_t value = (i >= checksum_begin && i < checksum_end) ? 0 : bytes[i];
        crc ^= value;
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static int bytes_are_zero(const void* memory, size_t size) {
    const unsigned char* bytes = (const unsigned char*)memory;
    size_t i;
    for (i = 0; i < size; ++i) if (bytes[i] != 0) return 0;
    return 1;
}

size_t map_script_snapshot_size(void) {
    return sizeof(MapScriptSnapshot);
}

int map_script_snapshot_save(MapScriptSnapshot* out,
                             char* err,
                             size_t err_cap) {
    MapScriptRuntime* runtime = g_runtime;
    if (err && err_cap) err[0] = '\0';
    if (!out) {
        set_error(err, err_cap, "map script snapshot output is required");
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->magic = MAP_SCRIPT_SNAPSHOT_MAGIC;
    out->version = MAP_SCRIPT_SNAPSHOT_VERSION;
    out->header_size = (uint16_t)offsetof(MapScriptSnapshot, state);
    out->total_size = (uint32_t)sizeof(*out);
    if (runtime && runtime->active) {
        out->script_id = runtime->script_id;
        out->tick = runtime->tick;
        out->rng_state = runtime->rng_state;
        out->flags = runtime->faulted ? MAP_SCRIPT_FLAG_FAULTED : 0;
        out->state_count = runtime->state_count;
        out->override_count = runtime->override_count;
        out->contact_count = runtime->contact_count;
        out->velocity_limit_count = runtime->velocity_limit_count;
        memcpy(out->lifecycle_generation, runtime->lifecycle_generation,
               sizeof(out->lifecycle_generation));
        memcpy(out->state, runtime->state, sizeof(out->state));
        memcpy(out->overrides, runtime->overrides, sizeof(out->overrides));
        memcpy(out->contacts, runtime->contacts, sizeof(out->contacts));
        memcpy(out->velocity_limits, runtime->velocity_limits,
               sizeof(out->velocity_limits));
    }
    out->checksum = snapshot_checksum(out);
    return 1;
}

static int validate_state_entries(const MapScriptSnapshot* snapshot,
                                  char* err,
                                  size_t err_cap) {
    uint16_t count = 0;
    int i;
    int j;
    for (i = 0; i < MAP_SCRIPT_MAX_STATE_ENTRIES; ++i) {
        const MapScriptSnapshotStateEntry* entry = &snapshot->state[i];
        if (!entry->in_use) {
            if (!bytes_are_zero(entry, sizeof(*entry))) {
                set_error(err, err_cap, "snapshot has a non-canonical empty map.state slot");
                return 0;
            }
            continue;
        }
        count++;
        if (entry->in_use != 1 || entry->key_len == 0 ||
            entry->key_len >= MAP_SCRIPT_STATE_KEY_MAX ||
            memchr(entry->key, '\0', entry->key_len) != NULL ||
            !bytes_are_zero(entry->reserved, sizeof(entry->reserved)) ||
            !bytes_are_zero(entry->key + entry->key_len,
                            MAP_SCRIPT_STATE_KEY_MAX - entry->key_len)) {
            set_error(err, err_cap, "snapshot has an invalid map.state key slot");
            return 0;
        }
        for (j = 0; j < i; ++j) {
            const MapScriptSnapshotStateEntry* earlier = &snapshot->state[j];
            if (earlier->in_use && earlier->key_len == entry->key_len &&
                memcmp(earlier->key, entry->key, entry->key_len) == 0) {
                set_error(err, err_cap, "snapshot contains duplicate map.state keys");
                return 0;
            }
        }
        switch (entry->type) {
            case MAP_SCRIPT_STATE_BOOL:
                if (entry->bool_value > 1 || entry->string_len != 0 ||
                    !bytes_are_zero(&entry->number_value, sizeof(entry->number_value)) ||
                    !bytes_are_zero(entry->string_value, sizeof(entry->string_value))) {
                    set_error(err, err_cap, "snapshot has a non-canonical boolean state value");
                    return 0;
                }
                break;
            case MAP_SCRIPT_STATE_NUMBER:
                if (!isfinite(entry->number_value) ||
                    (entry->number_value == 0.0 && signbit(entry->number_value)) ||
                    entry->bool_value != 0 || entry->string_len != 0 ||
                    !bytes_are_zero(entry->string_value, sizeof(entry->string_value))) {
                    set_error(err, err_cap, "snapshot has an invalid numeric state value");
                    return 0;
                }
                break;
            case MAP_SCRIPT_STATE_STRING:
                if (entry->string_len >= MAP_SCRIPT_STATE_STRING_MAX ||
                    entry->bool_value != 0 ||
                    !bytes_are_zero(&entry->number_value, sizeof(entry->number_value)) ||
                    !bytes_are_zero(entry->string_value + entry->string_len,
                                    MAP_SCRIPT_STATE_STRING_MAX - entry->string_len)) {
                    set_error(err, err_cap, "snapshot has an invalid string state value");
                    return 0;
                }
                break;
            default:
                set_error(err, err_cap, "snapshot has an unknown map.state value type");
                return 0;
        }
    }
    if (count != snapshot->state_count) {
        set_error(err, err_cap, "snapshot map.state count does not match its slots");
        return 0;
    }
    return 1;
}

static int validate_overrides(const MapScriptSnapshot* snapshot,
                              char* err,
                              size_t err_cap) {
    const int32_t offset_limit_q =
        MAP_SCRIPT_RENDER_OFFSET_LIMIT * MAP_SCRIPT_RENDER_OFFSET_QUANTIZATION;
    uint16_t count = 0;
    int i;
    int j;
    for (i = 0; i < MAP_SCRIPT_MAX_SPRITE_OVERRIDES; ++i) {
        const MapScriptSnapshotSpriteOverride* item = &snapshot->overrides[i];
        if (!item->in_use) {
            if (!bytes_are_zero(item, sizeof(*item))) {
                set_error(err, err_cap, "snapshot has a non-canonical empty sprite override");
                return 0;
            }
            continue;
        }
        count++;
        if (item->in_use != 1 ||
            !bytes_are_zero(item->reserved, sizeof(item->reserved)) ||
            item->sprite_index < 0 || item->sprite_index > MAP_SCRIPT_SPRITE_MAX ||
            item->offset_x_q < -offset_limit_q ||
            item->offset_x_q > offset_limit_q ||
            item->offset_y_q < -offset_limit_q ||
            item->offset_y_q > offset_limit_q ||
            item->expires_after_tick <= snapshot->tick) {
            set_error(err, err_cap, "snapshot has an invalid temporary sprite override");
            return 0;
        }
        for (j = 0; j < i; ++j) {
            if (snapshot->overrides[j].in_use &&
                snapshot->overrides[j].cell_index == item->cell_index) {
                set_error(err, err_cap, "snapshot has duplicate sprite overrides for one cell");
                return 0;
            }
        }
    }
    if (count != snapshot->override_count) {
        set_error(err, err_cap, "snapshot sprite override count does not match its slots");
        return 0;
    }
    return 1;
}

static int validate_contacts(const MapScriptSnapshot* snapshot,
                             size_t binding_count,
                             const MapScriptRuntime* live_runtime,
                             char* err,
                             size_t err_cap) {
    uint16_t count = 0;
    int i;
    int j;
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        const MapScriptSnapshotContact* contact = &snapshot->contacts[i];
        if (!contact->in_use) {
            if (!bytes_are_zero(contact, sizeof(*contact))) {
                set_error(err, err_cap, "snapshot has a non-canonical empty contact slot");
                return 0;
            }
            continue;
        }
        count++;
        if (contact->in_use != 1 ||
            !object_kind_matches_id(contact->object_id, contact->object_kind) ||
            contact->contact_scope > MAP_SCRIPT_CONTACT_SCOPE_BINDING ||
            !bytes_are_zero(contact->reserved, sizeof(contact->reserved)) ||
            contact->binding_index >= binding_count || contact->room_mirrored > 1 ||
            contact->last_seen_tick > snapshot->tick ||
            !isfinite(contact->object_x) || !isfinite(contact->object_y) ||
            !isfinite(contact->object_vx) || !isfinite(contact->object_vy) ||
            (contact->object_x == 0.0f && signbit(contact->object_x)) ||
            (contact->object_y == 0.0f && signbit(contact->object_y)) ||
            (contact->object_vx == 0.0f && signbit(contact->object_vx)) ||
            (contact->object_vy == 0.0f && signbit(contact->object_vy))) {
            set_error(err, err_cap, "snapshot has invalid active contact data");
            return 0;
        }
        if (live_runtime &&
            contact->contact_scope !=
                (live_runtime->bindings[contact->binding_index].sensor.configured
                    ? live_runtime->bindings[contact->binding_index].sensor.contact_scope
                    : MAP_SCRIPT_CONTACT_SCOPE_CELL)) {
            set_error(err, err_cap,
                      "snapshot contact scope disagrees with its active binding");
            return 0;
        }
        for (j = 0; j < i; ++j) {
            const MapScriptSnapshotContact* earlier = &snapshot->contacts[j];
            if (earlier->in_use && earlier->object_id == contact->object_id &&
                earlier->lifecycle_id == contact->lifecycle_id) {
                if (earlier->object_kind != contact->object_kind) {
                    set_error(err, err_cap,
                              "snapshot object lifecycle changes native kind");
                    return 0;
                }
                if ((contact->contact_scope == MAP_SCRIPT_CONTACT_SCOPE_CELL &&
                     earlier->contact_scope == MAP_SCRIPT_CONTACT_SCOPE_CELL &&
                     earlier->cell_index == contact->cell_index) ||
                    (contact->contact_scope == MAP_SCRIPT_CONTACT_SCOPE_BINDING &&
                     earlier->contact_scope == MAP_SCRIPT_CONTACT_SCOPE_BINDING &&
                     earlier->binding_index == contact->binding_index)) {
                    set_error(err, err_cap,
                              "snapshot has duplicate scoped object contacts");
                    return 0;
                }
            }
        }
    }
    if (count != snapshot->contact_count) {
        set_error(err, err_cap, "snapshot contact count does not match its slots");
        return 0;
    }
    return 1;
}

static int validate_velocity_bound(float value,
                                   int present) {
    if (!present) return bytes_are_zero(&value, sizeof(value));
    return isfinite(value) &&
           !(value == 0.0f && signbit(value)) &&
           value >= -MAP_SCRIPT_VELOCITY_LIMIT_ABS_MAX &&
           value <= MAP_SCRIPT_VELOCITY_LIMIT_ABS_MAX;
}

static int validate_velocity_limits(const MapScriptSnapshot* snapshot,
                                    char* err,
                                    size_t err_cap) {
    uint16_t count = 0;
    int i;
    int j;
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        const MapScriptSnapshotVelocityLimit* limit =
            &snapshot->velocity_limits[i];
        if (!limit->in_use) {
            if (!bytes_are_zero(limit, sizeof(*limit))) {
                set_error(err, err_cap,
                          "snapshot has a non-canonical empty velocity limit");
                return 0;
            }
            continue;
        }
        count++;
        if (limit->in_use != 1 || limit->reserved != 0 ||
            !object_kind_matches_id(limit->object_id, limit->object_kind) ||
            (limit->object_id < 2u && limit->lifecycle_id != 0u) ||
            limit->object_id >= 2u + MAP_SCRIPT_MAX_LIFECYCLE_SLOTS ||
            (limit->object_id >= 2u &&
             limit->lifecycle_id !=
                snapshot->lifecycle_generation[limit->object_id - 2u]) ||
            limit->bound_flags == 0 ||
            (limit->bound_flags & ~MAP_SCRIPT_VELOCITY_LIMIT_ALL_FLAGS) != 0 ||
            limit->expires_after_tick <= snapshot->tick ||
            !validate_velocity_bound(
                limit->min_vx,
                (limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX) != 0) ||
            !validate_velocity_bound(
                limit->max_vx,
                (limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX) != 0) ||
            !validate_velocity_bound(
                limit->min_vy,
                (limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY) != 0) ||
            !validate_velocity_bound(
                limit->max_vy,
                (limit->bound_flags & MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY) != 0)) {
            set_error(err, err_cap,
                      "snapshot has an invalid temporary velocity limit");
            return 0;
        }
        if ((limit->bound_flags &
             (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX |
              MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX)) ==
                (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX |
                 MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX) &&
            limit->min_vx > limit->max_vx) {
            set_error(err, err_cap,
                      "snapshot velocity limit has reversed x bounds");
            return 0;
        }
        if ((limit->bound_flags &
             (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY |
              MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY)) ==
                (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY |
                 MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY) &&
            limit->min_vy > limit->max_vy) {
            set_error(err, err_cap,
                      "snapshot velocity limit has reversed y bounds");
            return 0;
        }
        for (j = 0; j < i; ++j) {
            const MapScriptSnapshotVelocityLimit* earlier =
                &snapshot->velocity_limits[j];
            if (earlier->in_use && earlier->object_id == limit->object_id &&
                earlier->lifecycle_id == limit->lifecycle_id) {
                set_error(err, err_cap,
                          "snapshot has duplicate velocity limits for one object lifecycle");
                return 0;
            }
        }
    }
    if (count != snapshot->velocity_limit_count) {
        set_error(err, err_cap,
                  "snapshot velocity limit count does not match its slots");
        return 0;
    }
    return 1;
}

int map_script_snapshot_validate(const MapScriptSnapshot* snapshot,
                                 uint64_t expected_script_id,
                                 char* err,
                                 size_t err_cap) {
    size_t binding_count = MAP_SCRIPT_MAX_BINDINGS;
    const MapScriptRuntime* live_runtime = NULL;
    if (err && err_cap) err[0] = '\0';
    if (!snapshot) {
        set_error(err, err_cap, "map script snapshot is required");
        return 0;
    }
    if (snapshot->magic != MAP_SCRIPT_SNAPSHOT_MAGIC ||
        snapshot->version != MAP_SCRIPT_SNAPSHOT_VERSION ||
        snapshot->header_size != offsetof(MapScriptSnapshot, state) ||
        snapshot->total_size != sizeof(*snapshot)) {
        set_error(err, err_cap, "map script snapshot header/version is incompatible");
        return 0;
    }
    if (snapshot->checksum != snapshot_checksum(snapshot)) {
        set_error(err, err_cap, "map script snapshot checksum is invalid");
        return 0;
    }
    if (snapshot->script_id != expected_script_id) {
        set_error(err, err_cap,
                  "map script snapshot belongs to a different script/binding manifest");
        return 0;
    }
    if ((snapshot->flags & ~MAP_SCRIPT_FLAG_FAULTED) != 0 ||
        !bytes_are_zero(snapshot->reserved, sizeof(snapshot->reserved))) {
        set_error(err, err_cap, "map script snapshot has non-zero reserved fields");
        return 0;
    }
    if (snapshot->script_id == 0) {
        if (snapshot->tick != 0 || snapshot->rng_state != 0 || snapshot->flags != 0 ||
            snapshot->state_count != 0 || snapshot->override_count != 0 ||
            snapshot->contact_count != 0 || snapshot->velocity_limit_count != 0 ||
            !bytes_are_zero(snapshot->lifecycle_generation,
                            sizeof(snapshot->lifecycle_generation)) ||
            !bytes_are_zero(snapshot->state, sizeof(snapshot->state)) ||
            !bytes_are_zero(snapshot->overrides, sizeof(snapshot->overrides)) ||
            !bytes_are_zero(snapshot->contacts, sizeof(snapshot->contacts)) ||
            !bytes_are_zero(snapshot->velocity_limits,
                            sizeof(snapshot->velocity_limits))) {
            set_error(err, err_cap, "inactive map script snapshot is not canonical");
            return 0;
        }
        return 1;
    }
    if (snapshot->rng_state == 0) {
        set_error(err, err_cap, "active map script snapshot has an invalid RNG state");
        return 0;
    }
    if (g_runtime && g_runtime->active && g_runtime->script_id == snapshot->script_id) {
        binding_count = g_runtime->binding_count;
        live_runtime = g_runtime;
    }
    if ((snapshot->flags & MAP_SCRIPT_FLAG_FAULTED) &&
        (snapshot->override_count != 0 || snapshot->contact_count != 0 ||
         snapshot->velocity_limit_count != 0)) {
        set_error(err, err_cap, "faulted map script snapshot retains active effects");
        return 0;
    }
    return validate_state_entries(snapshot, err, err_cap) &&
           validate_overrides(snapshot, err, err_cap) &&
           validate_contacts(snapshot, binding_count, live_runtime, err, err_cap) &&
           validate_velocity_limits(snapshot, err, err_cap);
}

int map_script_snapshot_load(const MapScriptSnapshot* snapshot,
                             char* err,
                             size_t err_cap) {
    uint64_t expected = (g_runtime && g_runtime->active)
                      ? g_runtime->script_id : UINT64_C(0);
    if (!map_script_snapshot_validate(snapshot, expected, err, err_cap)) return 0;
    if (expected == 0) return 1;
    /* A validated load is a fixed-size POD copy: no Lua execution/allocation. */
    snapshot_payload_restore(g_runtime, snapshot);
    if (g_runtime->faulted) {
        /* Error strings are diagnostics rather than rollback state. Restore a
         * fixed message so a faulted timeline never inherits whichever local
         * callback happened to fail before the load. */
        remember_error(g_runtime, "map.lua fault restored from rollback state");
    } else {
        g_runtime->last_error[0] = '\0';
        g_last_error[0] = '\0';
    }
    return 1;
}
