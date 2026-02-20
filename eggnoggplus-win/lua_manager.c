#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>
#include "log.h"
#include "lua_manager.h"

static lua_State *L = NULL;

// Bump this when you make breaking changes to the Lua mod API.
#define MOD_API_VERSION 1

// Config entry file format version (internal; not exposed)
// The schema/value format is the simple line-based "key: type, value" described in MODDING.md.
// We keep this as v1 until we need a breaking change.
#define MOD_CONFIG_FORMAT_VERSION 1

// =============================
// Data model
// =============================

typedef struct LuaRefList {
    int* refs;
    int  count;
    int  cap;
} LuaRefList;

// Forward declarations for helper functions used before definition
static void reflist_clear(lua_State* Ls, LuaRefList* list);



typedef struct LoadedMod {
    char id[64];
    char name[64];
    char version[32];
    char author[64];
    char description[256];
    char entry[128];
    char folder_path[MAX_PATH];
    int  api_version;

    int  enabled;
    int  error_count;

    // Registry refs
    int  env_ref;       // mod environment table
    int  mod_ref;       // the per-mod 'mod' table
    int  on_load_ref;   // function or LUA_NOREF
    int  on_unload_ref; // function or LUA_NOREF

    LuaRefList on_frame;
    LuaRefList on_event;

    // =============================
    // Optional config (in-game editable)
    // =============================
    char config_rel[128];
    char config_path[MAX_PATH];

    struct ConfigEntry* cfg_entries;
    int cfg_count;
    int cfg_cap;

    struct ConfigAction* cfg_actions;
    int cfg_action_count;
    int cfg_action_cap;
} LoadedMod;

// =============================
// Config model
// =============================

typedef struct ConfigEntry {
    char key[64];
    char label[64];
    int  type;              // LUA_CFG_*
    char value[256];        // current value (empty for actions)
} ConfigEntry;

typedef struct ConfigAction {
    char key[64];
    LuaRefList handlers;     // Lua functions
} ConfigAction;

static LoadedMod* g_mods = NULL;
static int        g_mod_count = 0;
static int        g_mod_cap = 0;

// =============================
// Tiny JSON helpers
// (kept intentionally simple – good enough for our stable mod.json format)
// =============================

static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static int json_find_key(const char* json, const char* key, const char** out_value) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    p = skip_ws(p);
    if (*p != ':') return 0;
    p++;
    p = skip_ws(p);
    *out_value = p;
    return 1;
}

static int json_get_string(const char* json, const char* key, char* out, int out_size) {
    const char* p = NULL;
    if (!json_find_key(json, key, &p)) return 0;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

static int json_get_int(const char* json, const char* key, int* out) {
    const char* p = NULL;
    if (!json_find_key(json, key, &p)) return 0;
    *out = atoi(p);
    return 1;
}

// =============================
// Config parsing (line-based)
// Format:
//   key: bool, true
//   some_string: str, hello
//   do_thing: action
//   action_with_label: action, "Do Thing"
// =============================

static void str_trim(char* s) {
    if (!s) return;
    // left
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    // right
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static void str_lower(char* s) {
    for (; s && *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void unquote_inplace(char* s) {
    str_trim(s);
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        // remove outer quotes
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
        // unescape very small set: \\ and \"
        char out[512];
        size_t oi = 0;
        for (size_t i = 0; s[i] && oi + 1 < sizeof(out); i++) {
            if (s[i] == '\\' && s[i + 1]) {
                i++;
                out[oi++] = s[i];
            } else {
                out[oi++] = s[i];
            }
        }
        out[oi] = '\0';
        strncpy(s, out, 511);
        s[511] = '\0';
    }
}

static int parse_bool(const char* s) {
    if (!s) return 0;
    if (_stricmp(s, "true") == 0) return 1;
    if (_stricmp(s, "yes") == 0) return 1;
    if (_stricmp(s, "on") == 0) return 1;
    if (strcmp(s, "1") == 0) return 1;
    return 0;
}

static const char* type_to_string(int type) {
    switch (type) {
        case LUA_CFG_BOOL:   return "bool";
        case LUA_CFG_INT:    return "int";
        case LUA_CFG_FLOAT:  return "float";
        case LUA_CFG_STRING: return "str";
        case LUA_CFG_ACTION: return "action";
        default: return "none";
    }
}

static int string_to_type(const char* t) {
    if (!t || !t[0]) return LUA_CFG_NONE;
    if (_stricmp(t, "bool") == 0 || _stricmp(t, "boolean") == 0) return LUA_CFG_BOOL;
    if (_stricmp(t, "int") == 0 || _stricmp(t, "integer") == 0) return LUA_CFG_INT;
    if (_stricmp(t, "float") == 0 || _stricmp(t, "number") == 0 || _stricmp(t, "double") == 0) return LUA_CFG_FLOAT;
    if (_stricmp(t, "str") == 0 || _stricmp(t, "string") == 0) return LUA_CFG_STRING;
    if (_stricmp(t, "action") == 0 || _stricmp(t, "button") == 0) return LUA_CFG_ACTION;
    return LUA_CFG_NONE;
}

static void mod_config_clear(lua_State* Ls, LoadedMod* mod) {
    if (!mod) return;
    if (mod->cfg_entries) {
        free(mod->cfg_entries);
        mod->cfg_entries = NULL;
    }
    mod->cfg_count = 0;
    mod->cfg_cap = 0;

    if (mod->cfg_actions) {
        for (int i = 0; i < mod->cfg_action_count; i++) {
            reflist_clear(Ls, &mod->cfg_actions[i].handlers);
        }
        free(mod->cfg_actions);
        mod->cfg_actions = NULL;
    }
    mod->cfg_action_count = 0;
    mod->cfg_action_cap = 0;
}

static void mod_config_push_entry(LoadedMod* mod, const ConfigEntry* e) {
    if (!mod || !e) return;
    if (mod->cfg_count + 1 > mod->cfg_cap) {
        int newcap = (mod->cfg_cap == 0) ? 8 : (mod->cfg_cap * 2);
        ConfigEntry* ne = (ConfigEntry*)realloc(mod->cfg_entries, sizeof(ConfigEntry) * newcap);
        if (!ne) return;
        mod->cfg_entries = ne;
        mod->cfg_cap = newcap;
    }
    mod->cfg_entries[mod->cfg_count++] = *e;
}

static int mod_config_find_index(LoadedMod* mod, const char* key) {
    if (!mod || !key) return -1;
    for (int i = 0; i < mod->cfg_count; i++) {
        if (_stricmp(mod->cfg_entries[i].key, key) == 0) return i;
    }
    return -1;
}

static ConfigAction* mod_config_get_or_add_action(LoadedMod* mod, const char* key) {
    if (!mod || !key) return NULL;
    for (int i = 0; i < mod->cfg_action_count; i++) {
        if (_stricmp(mod->cfg_actions[i].key, key) == 0) return &mod->cfg_actions[i];
    }
    if (mod->cfg_action_count + 1 > mod->cfg_action_cap) {
        int newcap = (mod->cfg_action_cap == 0) ? 4 : (mod->cfg_action_cap * 2);
        ConfigAction* na = (ConfigAction*)realloc(mod->cfg_actions, sizeof(ConfigAction) * newcap);
        if (!na) return NULL;
        mod->cfg_actions = na;
        mod->cfg_action_cap = newcap;
    }
    ConfigAction* a = &mod->cfg_actions[mod->cfg_action_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->key, key, sizeof(a->key) - 1);
    a->key[sizeof(a->key) - 1] = '\0';
    a->handlers.refs = NULL;
    a->handlers.count = 0;
    a->handlers.cap = 0;
    return a;
}

static void cfg_escape_and_quote(const char* in, char* out, size_t outsz) {
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!in) in = "";

    int needs_quotes = 0;
    for (const char* p = in; *p; p++) {
        if (*p == ',' || *p == '"' || isspace((unsigned char)*p) || *p == '#') {
            needs_quotes = 1;
            break;
        }
    }

    if (!needs_quotes) {
        strncpy(out, in, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }

    size_t oi = 0;
    if (oi + 1 < outsz) out[oi++] = '"';
    for (const char* p = in; *p && oi + 2 < outsz; p++) {
        if (*p == '"' || *p == '\\') {
            out[oi++] = '\\';
        }
        out[oi++] = *p;
    }
    if (oi + 1 < outsz) out[oi++] = '"';
    out[oi] = '\0';
}

static int mod_config_save(LoadedMod* mod) {
    if (!mod || !mod->config_path[0]) return 0;
    FILE* f = fopen(mod->config_path, "w");
    if (!f) return 0;

    fprintf(f, "# Eggnogg+ mod config (v%d)\n", MOD_CONFIG_FORMAT_VERSION);
    fprintf(f, "# Format: key: type, value   (or: key: action[, \"Label\"])\n\n");

    for (int i = 0; i < mod->cfg_count; i++) {
        ConfigEntry* e = &mod->cfg_entries[i];
        const char* t = type_to_string(e->type);

        if (e->type == LUA_CFG_ACTION) {
            if (_stricmp(e->label, e->key) != 0 && e->label[0]) {
                char q[512];
                cfg_escape_and_quote(e->label, q, sizeof(q));
                fprintf(f, "%s: %s, %s\n", e->key, t, q);
            } else {
                fprintf(f, "%s: %s\n", e->key, t);
            }
            continue;
        }

        char q[512];
        cfg_escape_and_quote(e->value, q, sizeof(q));
        fprintf(f, "%s: %s, %s\n", e->key, t, q);
    }

    fclose(f);
    return 1;
}

static int mod_config_load(LoadedMod* mod) {
    if (!mod || !mod->config_path[0]) return 0;

    FILE* f = fopen(mod->config_path, "r");
    if (!f) {
        // If config file doesn't exist yet, create an empty stub.
        LOG_INFO("Creating config file for mod %s: %s", mod->id, mod->config_path);
        mod_config_save(mod);
        return 1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        line[strcspn(line, "\r\n")] = '\0';
        str_trim(line);
        if (!line[0]) continue;
        if (line[0] == '#') continue;
        if (line[0] == ';') continue;
        if (line[0] == '/' && line[1] == '/') continue;

        char* colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char key[64];
        strncpy(key, line, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        str_trim(key);
        if (!key[0]) continue;

        char rest[448];
        strncpy(rest, colon + 1, sizeof(rest) - 1);
        rest[sizeof(rest) - 1] = '\0';
        str_trim(rest);
        if (!rest[0]) continue;

        // Split on first comma
        char type_str[64] = {0};
        char value_str[256] = {0};

        char* comma = strchr(rest, ',');
        if (comma) {
            *comma = '\0';
            strncpy(type_str, rest, sizeof(type_str) - 1);
            strncpy(value_str, comma + 1, sizeof(value_str) - 1);
        } else {
            strncpy(type_str, rest, sizeof(type_str) - 1);
            value_str[0] = '\0';
        }

        str_trim(type_str);
        str_trim(value_str);
        unquote_inplace(value_str);

        int type = string_to_type(type_str);
        if (type == LUA_CFG_NONE) continue;

        ConfigEntry e;
        memset(&e, 0, sizeof(e));
        strncpy(e.key, key, sizeof(e.key) - 1);
        strncpy(e.label, key, sizeof(e.label) - 1);
        e.type = type;

        if (type == LUA_CFG_ACTION) {
            // optional label after comma
            if (value_str[0]) {
                strncpy(e.label, value_str, sizeof(e.label) - 1);
            }
            e.value[0] = '\0';
        } else if (type == LUA_CFG_BOOL) {
            int b = parse_bool(value_str);
            snprintf(e.value, sizeof(e.value), "%s", b ? "true" : "false");
        } else {
            strncpy(e.value, value_str, sizeof(e.value) - 1);
        }

        mod_config_push_entry(mod, &e);
    }

    fclose(f);
    return 1;
}

// =============================
// Small helpers
// =============================

static void reflist_push(LuaRefList* list, int ref) {
    if (list->count + 1 > list->cap) {
        int newcap = (list->cap == 0) ? 8 : (list->cap * 2);
        int* newrefs = (int*)realloc(list->refs, sizeof(int) * newcap);
        if (!newrefs) return;
        list->refs = newrefs;
        list->cap = newcap;
    }
    list->refs[list->count++] = ref;
}

static void reflist_clear(lua_State* Ls, LuaRefList* list) {
    if (!list || !list->refs) return;
    for (int i = 0; i < list->count; i++) {
        if (list->refs[i] != LUA_NOREF && list->refs[i] != LUA_REFNIL) {
            luaL_unref(Ls, LUA_REGISTRYINDEX, list->refs[i]);
        }
    }
    free(list->refs);
    list->refs = NULL;
    list->count = 0;
    list->cap = 0;
}

static LoadedMod* mods_add(void) {
    if (g_mod_count + 1 > g_mod_cap) {
        int newcap = (g_mod_cap == 0) ? 8 : (g_mod_cap * 2);
        LoadedMod* nm = (LoadedMod*)realloc(g_mods, sizeof(LoadedMod) * newcap);
        if (!nm) return NULL;
        g_mods = nm;
        g_mod_cap = newcap;
    }
    LoadedMod* m = &g_mods[g_mod_count++];
    memset(m, 0, sizeof(*m));
    m->enabled = 1;
    m->error_count = 0;
    m->env_ref = LUA_NOREF;
    m->mod_ref = LUA_NOREF;
    m->on_load_ref = LUA_NOREF;
    m->on_unload_ref = LUA_NOREF;
    m->api_version = MOD_API_VERSION;

    m->config_rel[0] = '\0';
    m->config_path[0] = '\0';
    m->cfg_entries = NULL;
    m->cfg_count = 0;
    m->cfg_cap = 0;
    m->cfg_actions = NULL;
    m->cfg_action_count = 0;
    m->cfg_action_cap = 0;
    return m;
}

static void mod_set_single_ref(lua_State* Ls, int* slot, int newref) {
    if (!slot) return;
    if (*slot != LUA_NOREF && *slot != LUA_REFNIL) {
        luaL_unref(Ls, LUA_REGISTRYINDEX, *slot);
    }
    *slot = newref;
}

static LoadedMod* mod_from_upvalue(lua_State* Ls) {
    return (LoadedMod*)lua_touserdata(Ls, lua_upvalueindex(1));
}

static void log_mod(LoadedMod* mod, const char* level, const char* msg) {
    if (!mod) {
        log_write(level, "[mod:?] %s", msg);
        return;
    }
    log_write(level, "[mod:%s] %s", mod->id[0] ? mod->id : "?", msg);
}

// =============================
// Lua API (per-mod; functions are closures with a LoadedMod* upvalue)
// =============================

static int lua_mod_log(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* msg = luaL_checkstring(Ls, 1);
    log_mod(mod, "INFO", msg);
    return 0;
}

static int lua_mod_warn(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* msg = luaL_checkstring(Ls, 1);
    log_mod(mod, "WARN", msg);
    return 0;
}

static int lua_mod_error(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* msg = luaL_checkstring(Ls, 1);
    log_mod(mod, "ERROR", msg);
    return 0;
}

static int lua_mod_on_frame(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&mod->on_frame, ref);
    return 0;
}

static int lua_mod_on_event(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&mod->on_event, ref);
    return 0;
}

static int lua_mod_on_load(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    mod_set_single_ref(Ls, &mod->on_load_ref, ref);
    return 0;
}

static int lua_mod_on_unload(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    mod_set_single_ref(Ls, &mod->on_unload_ref, ref);
    return 0;
}

static int lua_mod_info(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    lua_newtable(Ls);
    lua_pushstring(Ls, mod->id);          lua_setfield(Ls, -2, "id");
    lua_pushstring(Ls, mod->name);        lua_setfield(Ls, -2, "name");
    lua_pushstring(Ls, mod->version);     lua_setfield(Ls, -2, "version");
    lua_pushstring(Ls, mod->author);      lua_setfield(Ls, -2, "author");
    lua_pushstring(Ls, mod->description); lua_setfield(Ls, -2, "description");
    lua_pushstring(Ls, mod->entry);       lua_setfield(Ls, -2, "entry");
    lua_pushinteger(Ls, mod->api_version);lua_setfield(Ls, -2, "api_version");
    lua_pushboolean(Ls, mod->enabled);    lua_setfield(Ls, -2, "enabled");
    lua_pushstring(Ls, mod->folder_path); lua_setfield(Ls, -2, "folder_path");
    return 1;
}

static int lua_mod_get_path(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_optstring(Ls, 1, "");
    char out[MAX_PATH];
    if (rel[0] == '\0') {
        snprintf(out, sizeof(out), "%s", mod->folder_path);
    } else {
        snprintf(out, sizeof(out), "%s\\%s", mod->folder_path, rel);
    }
    lua_pushstring(Ls, out);
    return 1;
}

// Like Lua's dofile(), but always runs in *this mod's* environment and is relative to the mod folder.
static int lua_mod_dofile(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", mod->folder_path, rel);

    if (luaL_loadfile(Ls, path) != 0) {
        // Error message already on stack
        return lua_error(Ls);
    }

    // Set the chunk environment to the mod environment
    lua_rawgeti(Ls, LUA_REGISTRYINDEX, mod->env_ref);
    lua_setfenv(Ls, -2);

    if (lua_pcall(Ls, 0, LUA_MULTRET, 0) != 0) {
        return lua_error(Ls);
    }

    // Remove the original argument (so returns match normal dofile())
    int nret = lua_gettop(Ls) - 1;
    lua_remove(Ls, 1);
    return nret;
}

// =============================
// Lua Config API (per-mod)
// =============================

static int lua_cfg_get(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_config_find_index(mod, key);
    if (idx < 0) {
        if (!lua_isnoneornil(Ls, 2)) { lua_pushvalue(Ls, 2); return 1; }
        lua_pushnil(Ls);
        return 1;
    }
    ConfigEntry* e = &mod->cfg_entries[idx];
    switch (e->type) {
        case LUA_CFG_BOOL:
            lua_pushboolean(Ls, parse_bool(e->value));
            return 1;
        case LUA_CFG_INT:
            lua_pushinteger(Ls, atoi(e->value));
            return 1;
        case LUA_CFG_FLOAT:
            lua_pushnumber(Ls, atof(e->value));
            return 1;
        case LUA_CFG_STRING:
            lua_pushstring(Ls, e->value);
            return 1;
        default:
            lua_pushnil(Ls);
            return 1;
    }
}

static int lua_cfg_set(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_config_find_index(mod, key);
    if (idx < 0) {
        lua_pushboolean(Ls, 0);
        return 1;
    }
    ConfigEntry* e = &mod->cfg_entries[idx];
    if (e->type == LUA_CFG_ACTION) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    switch (e->type) {
        case LUA_CFG_BOOL: {
            int b = 0;
            if (lua_isboolean(Ls, 2)) b = lua_toboolean(Ls, 2);
            else if (lua_isnumber(Ls, 2)) b = (lua_tonumber(Ls, 2) != 0);
            else b = parse_bool(luaL_checkstring(Ls, 2));
            snprintf(e->value, sizeof(e->value), "%s", b ? "true" : "false");
            break;
        }
        case LUA_CFG_INT: {
            int v = (int)luaL_checkinteger(Ls, 2);
            snprintf(e->value, sizeof(e->value), "%d", v);
            break;
        }
        case LUA_CFG_FLOAT: {
            double v = (double)luaL_checknumber(Ls, 2);
            snprintf(e->value, sizeof(e->value), "%.6g", v);
            break;
        }
        case LUA_CFG_STRING: {
            const char* s = luaL_checkstring(Ls, 2);
            strncpy(e->value, s, sizeof(e->value) - 1);
            e->value[sizeof(e->value) - 1] = '\0';
            break;
        }
        default:
            lua_pushboolean(Ls, 0);
            return 1;
    }

    mod_config_save(mod);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_cfg_on_action(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    luaL_checktype(Ls, 2, LUA_TFUNCTION);

    // Best-effort warning if schema doesn't declare this as an action
    int idx = mod_config_find_index(mod, key);
    if (idx < 0 || mod->cfg_entries[idx].type != LUA_CFG_ACTION) {
        char buf[256];
        snprintf(buf, sizeof(buf), "config.on_action('%s') registered, but '%s' is not declared as type 'action' in the config file", key, key);
        log_mod(mod, "WARN", buf);
    }

    ConfigAction* a = mod_config_get_or_add_action(mod, key);
    if (!a) return 0;
    lua_pushvalue(Ls, 2);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&a->handlers, ref);
    return 0;
}

static void push_config_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_cfg_get, 1);      lua_setfield(Ls, -2, "get");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_cfg_set, 1);      lua_setfield(Ls, -2, "set");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_cfg_on_action, 1);lua_setfield(Ls, -2, "on_action");

    lua_pushstring(Ls, mod->config_path); lua_setfield(Ls, -2, "path");
}

static void push_mod_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);

    // Functions
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_load,   1); lua_setfield(Ls, -2, "on_load");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_unload, 1); lua_setfield(Ls, -2, "on_unload");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_frame,  1); lua_setfield(Ls, -2, "on_frame");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_event,  1); lua_setfield(Ls, -2, "on_event");

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_log,   1); lua_setfield(Ls, -2, "log");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_warn,  1); lua_setfield(Ls, -2, "warn");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_error, 1); lua_setfield(Ls, -2, "error");

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_info,     1); lua_setfield(Ls, -2, "info");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_get_path, 1); lua_setfield(Ls, -2, "get_path");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_dofile,   1); lua_setfield(Ls, -2, "dofile");

    // Fields (convenience)
    lua_pushstring(Ls, mod->id);      lua_setfield(Ls, -2, "id");
    lua_pushstring(Ls, mod->name);    lua_setfield(Ls, -2, "name");
    lua_pushstring(Ls, mod->version); lua_setfield(Ls, -2, "version");
    lua_pushinteger(Ls, MOD_API_VERSION); lua_setfield(Ls, -2, "framework_api");
}

static int call_lua_ref0(lua_State* Ls, LoadedMod* mod, int ref, const char* where) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 1;
    lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(Ls, 0, 0, 0) != 0) {
        const char* err = lua_tostring(Ls, -1);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s error: %s", where, err ? err : "(unknown)");
        log_mod(mod, "ERROR", buf);
        lua_pop(Ls, 1);
        mod->error_count++;
        return 0;
    }
    return 1;
}

// =============================
// Loading
// =============================

static int load_mod_lua(LoadedMod* mod) {
    char json_path[MAX_PATH];
    snprintf(json_path, sizeof(json_path), "%s\\mod.json", mod->folder_path);

    FILE* f = fopen(json_path, "r");
    if (!f) {
        LOG_WARN("No mod.json found in %s, skipping", mod->id);
        return 0;
    }

    char json[2048] = {0};
    fread(json, 1, sizeof(json) - 1, f);
    fclose(f);

    // Defaults
    snprintf(mod->name, sizeof(mod->name), "%s", mod->id);
    snprintf(mod->version, sizeof(mod->version), "?.?.?");
    snprintf(mod->author, sizeof(mod->author), "Unknown");
    mod->description[0] = '\0';
    mod->entry[0] = '\0';
    mod->api_version = MOD_API_VERSION;

    mod->config_rel[0] = '\0';

    // Parse
    json_get_string(json, "id",          mod->id,          sizeof(mod->id));
    json_get_string(json, "name",        mod->name,        sizeof(mod->name));
    json_get_string(json, "version",     mod->version,     sizeof(mod->version));
    json_get_string(json, "author",      mod->author,      sizeof(mod->author));
    json_get_string(json, "description", mod->description, sizeof(mod->description));
    json_get_string(json, "entry",       mod->entry,       sizeof(mod->entry));
    json_get_int   (json, "api_version", &mod->api_version);
    json_get_string(json, "config",      mod->config_rel,  sizeof(mod->config_rel));

    // Optional config (line-based config file)
    if (mod->config_rel[0]) {
        snprintf(mod->config_path, sizeof(mod->config_path), "%s\\%s", mod->folder_path, mod->config_rel);
        mod_config_load(mod);
    } else {
        mod->config_path[0] = '\0';
    }

    if (mod->entry[0] == '\0') {
        LOG_WARN("mod.json in %s has no entry field, skipping", mod->id);
        return 0;
    }

    if (mod->api_version != MOD_API_VERSION) {
        LOG_WARN("Mod %s requests api_version=%d but framework is %d (loading anyway)",
                 mod->id, mod->api_version, MOD_API_VERSION);
    }

    LOG_INFO("Loading mod: %s (%s) v%s by %s", mod->id, mod->name, mod->version, mod->author);
    if (mod->description[0]) LOG_INFO("  %s", mod->description);

    char lua_path[MAX_PATH];
    snprintf(lua_path, sizeof(lua_path), "%s\\%s", mod->folder_path, mod->entry);

    // Load chunk
    if (luaL_loadfile(L, lua_path) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("Error loading %s: %s", mod->id, err ? err : "(unknown)");
        lua_pop(L, 1);
        return 0;
    }

    // Create environment table for the mod
    lua_newtable(L); // env
    {
        // env metatable: __index = _G
        lua_newtable(L); // mt
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
    }

    // Create per-mod API table and store it in env.mod
    push_mod_api_table(L, mod);
    lua_pushvalue(L, -1);
    mod->mod_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_setfield(L, -2, "mod");

    // Create per-mod config API table and store it in env.config
    push_config_api_table(L, mod);
    lua_setfield(L, -2, "config");

    // Keep env alive
    lua_pushvalue(L, -1);
    mod->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Set env for the chunk (Lua 5.1 / LuaJIT)
    lua_setfenv(L, -2);

    // Execute chunk
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("Error running %s: %s", mod->id, err ? err : "(unknown)");
        lua_pop(L, 1);
        return 0;
    }

    // Call on_load if registered
    call_lua_ref0(L, mod, mod->on_load_ref, "on_load");
    LOG_INFO("Loaded mod: %s", mod->id);
    return 1;
}

// =============================
// Init / Shutdown
// =============================

static void extend_package_path(void) {
    // package.path = package.path .. ";mods\\?\\?.lua;mods\\?\\init.lua"
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, "path");
    const char* old = lua_tostring(L, -1);
    if (!old) old = "";

    char newpath[2048];
    snprintf(newpath, sizeof(newpath), "%s;mods\\?\\?.lua;mods\\?\\init.lua", old);
    lua_pop(L, 1);
    lua_pushstring(L, newpath);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}

void lua_manager_init() {
    L = luaL_newstate();
    luaL_openlibs(L);
    extend_package_path();

    LOG_INFO("Mod framework API version: %d", MOD_API_VERSION);
    LOG_INFO("Scanning mods/ folder...");

    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile("mods\\*", &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_WARN("No mods/ folder found");
        return;
    }

    int scanned = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (fd.cFileName[0] == '_') continue; // allow _template or _disabled folders

        LoadedMod* mod = mods_add();
        if (!mod) break;

        // Default id = folder name (can be overridden by mod.json "id")
        snprintf(mod->id, sizeof(mod->id), "%s", fd.cFileName);
        snprintf(mod->folder_path, sizeof(mod->folder_path), "mods\\%s", fd.cFileName);

        load_mod_lua(mod);
        scanned++;
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);
    LOG_INFO("Done. %d mod folder(s) scanned.", scanned);
}

void lua_manager_shutdown() {
    if (!L) return;

    // Call on_unload for mods (best-effort)
    for (int i = 0; i < g_mod_count; i++) {
        LoadedMod* mod = &g_mods[i];
        if (!mod->enabled) continue;
        call_lua_ref0(L, mod, mod->on_unload_ref, "on_unload");
    }

    // Free refs
    for (int i = 0; i < g_mod_count; i++) {
        LoadedMod* mod = &g_mods[i];
        reflist_clear(L, &mod->on_frame);
        reflist_clear(L, &mod->on_event);

        // Config entries + action handlers
        mod_config_clear(L, mod);

        if (mod->on_load_ref != LUA_NOREF && mod->on_load_ref != LUA_REFNIL)
            luaL_unref(L, LUA_REGISTRYINDEX, mod->on_load_ref);
        if (mod->on_unload_ref != LUA_NOREF && mod->on_unload_ref != LUA_REFNIL)
            luaL_unref(L, LUA_REGISTRYINDEX, mod->on_unload_ref);
        if (mod->env_ref != LUA_NOREF && mod->env_ref != LUA_REFNIL)
            luaL_unref(L, LUA_REGISTRYINDEX, mod->env_ref);
        if (mod->mod_ref != LUA_NOREF && mod->mod_ref != LUA_REFNIL)
            luaL_unref(L, LUA_REGISTRYINDEX, mod->mod_ref);
    }

    free(g_mods);
    g_mods = NULL;
    g_mod_count = 0;
    g_mod_cap = 0;

    lua_close(L);
    L = NULL;
}

// =============================
// Runtime callbacks
// =============================

// Derived from the synthetic "delta_time" event.
static float g_time_scale = 1.0f;

float lua_manager_get_time_scale(void) {
    return g_time_scale;
}

double lua_manager_on_delta_time(double dt_seconds) {
    if (!L) {
        g_time_scale = 1.0f;
        return dt_seconds;
    }

    if (dt_seconds <= 0.0) {
        g_time_scale = 1.0f;
        return dt_seconds;
    }

    // Build event table once
    lua_newtable(L);
    lua_pushstring(L, "delta_time"); lua_setfield(L, -2, "type");
    lua_pushnumber(L, dt_seconds);   lua_setfield(L, -2, "value");

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        for (int i = 0; i < mod->on_event.count; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod->on_event.refs[i]);
            lua_pushvalue(L, -2); // event table
            if (lua_pcall(L, 1, 1, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_event error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
                continue;
            }
            lua_pop(L, 1); // handler return
        }
    }

    // Read back (potentially modified) dt
    double out = dt_seconds;
    lua_getfield(L, -1, "value");
    if (lua_isnumber(L, -1)) {
        out = lua_tonumber(L, -1);
    }
    lua_pop(L, 1); // value
    lua_pop(L, 1); // event

    // Update time scale multiplier
    double scale = out / dt_seconds;
    if (scale < 0.05) scale = 0.05;
    if (scale > 5.0)  scale = 5.0;
    g_time_scale = (float)scale;

    return out;
}

void lua_manager_on_frame() {
    if (!L) return;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        for (int i = 0; i < mod->on_frame.count; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod->on_frame.refs[i]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_frame error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
            }
        }
    }
}

// Returns 1 if consumed by any mod (a handler returned true), else 0.
int lua_manager_on_event(const char* type, int sym, int scancode, int modmask, int x, int y, int button) {
    if (!L) return 0;

    // Build event table once
    lua_newtable(L);
    lua_pushstring(L, type);      lua_setfield(L, -2, "type");
    lua_pushinteger(L, sym);      lua_setfield(L, -2, "sym");
    lua_pushinteger(L, scancode); lua_setfield(L, -2, "scancode");
    lua_pushinteger(L, modmask);  lua_setfield(L, -2, "mod");
    lua_pushinteger(L, x);        lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);        lua_setfield(L, -2, "y");
    lua_pushinteger(L, button);   lua_setfield(L, -2, "button");

    int consumed = 0;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        for (int i = 0; i < mod->on_event.count; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod->on_event.refs[i]);
            lua_pushvalue(L, -2); // event table
            if (lua_pcall(L, 1, 1, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_event error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
                continue;
            }
            if (lua_toboolean(L, -1)) {
                consumed = 1;
            }
            lua_pop(L, 1); // handler return
        }
    }

    lua_pop(L, 1); // event table
    return consumed;
}

// =============================
// Public C API (used by hooks.c)
// =============================

int lua_manager_framework_api(void) {
    return MOD_API_VERSION;
}

int lua_manager_get_mod_count(void) {
    return g_mod_count;
}

static LoadedMod* get_mod_by_index(int mod_index) {
    if (mod_index < 0 || mod_index >= g_mod_count) return NULL;
    return &g_mods[mod_index];
}

const char* lua_manager_get_mod_id(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->id : "";
}

const char* lua_manager_get_mod_name(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->name : "";
}

const char* lua_manager_get_mod_version(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->version : "";
}

int lua_manager_get_mod_enabled(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->enabled : 0;
}

int lua_manager_get_mod_config_count(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->cfg_count : 0;
}

int lua_manager_get_mod_config_type(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return LUA_CFG_NONE;
    if (entry_index < 0 || entry_index >= m->cfg_count) return LUA_CFG_NONE;
    return m->cfg_entries[entry_index].type;
}

const char* lua_manager_get_mod_config_key(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    return m->cfg_entries[entry_index].key;
}

const char* lua_manager_get_mod_config_label(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    return m->cfg_entries[entry_index].label;
}

const char* lua_manager_get_mod_config_value_str(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    return m->cfg_entries[entry_index].value;
}

int lua_manager_config_toggle_bool(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_BOOL) return 0;
    int b = !parse_bool(e->value);
    snprintf(e->value, sizeof(e->value), "%s", b ? "true" : "false");
    return mod_config_save(m);
}

int lua_manager_config_increment_int(int mod_index, int entry_index, int delta) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_INT) return 0;
    int v = atoi(e->value);
    v += delta;
    snprintf(e->value, sizeof(e->value), "%d", v);
    return mod_config_save(m);
}

int lua_manager_config_increment_float(int mod_index, int entry_index, double delta) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_FLOAT) return 0;
    double v = atof(e->value);
    v += delta;
    snprintf(e->value, sizeof(e->value), "%.6g", v);
    return mod_config_save(m);
}

int lua_manager_config_set_string(int mod_index, int entry_index, const char* value) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_STRING) return 0;
    if (!value) value = "";
    strncpy(e->value, value, sizeof(e->value) - 1);
    e->value[sizeof(e->value) - 1] = '\0';
    return mod_config_save(m);
}

void lua_manager_config_trigger_action(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || !L) return;
    if (entry_index < 0 || entry_index >= m->cfg_count) return;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_ACTION) return;

    // Find registered handlers
    for (int ai = 0; ai < m->cfg_action_count; ai++) {
        ConfigAction* a = &m->cfg_actions[ai];
        if (_stricmp(a->key, e->key) != 0) continue;
        for (int hi = 0; hi < a->handlers.count; hi++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, a->handlers.refs[hi]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "config action '%s' error: %s", e->key, err ? err : "(unknown)");
                log_mod(m, "ERROR", buf);
                lua_pop(L, 1);
                m->error_count++;
            }
        }
        break;
    }
}