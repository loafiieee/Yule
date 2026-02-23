#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>
#include "log.h"
#include "lua_manager.h"
#include "hooks.h"

static lua_State *L = NULL;

// Bump this when you make breaking changes to the Lua mod API.
#define MOD_API_VERSION 1

// Config entry file format version (internal; not exposed)
// The schema/value format is the simple line-based "key: type, value" described in MODDING.md.
// We keep this as v1 until we need a breaking change.
#define MOD_CONFIG_FORMAT_VERSION 1

// Engine symbols used by the lightweight Lua UI overlay.
#define ADDR_STATE_CURRENT         0x405DB0u
#define ADDR_MAD_W                 0x404300u
#define ADDR_MAD_H                 0x404320u
#define ADDR_PLOT_TEXT             0x4304E0u
#define ADDR_TURTLE_SET_ANGLE      0x409000u
#define ADDR_TURTLE_SET_POS        0x409040u
#define ADDR_TURTLE_SET_SCALE      0x409080u
#define ADDR_TURTLE_SET_RGB        0x4091E0u
#define ADDR_TURTLE_SET_RGBA       0x4090C0u
#define ADDR_TURTLE_RESET          0x4092D0u
#define ADDR_BUTTON_GET            0x415D00u
#define ADDR_BUTTON_SET_LAYOUT     0x415F80u
#define ADDR_BUTTON_EX             0x416310u
#define ADDR_BUTTON_COUNT          0x416890u
#define ADDR_BTN_PLAYER_FILTER     0x4325C0u
#define ADDR_MAIN_SPRITE_BATCHES_DRAW 0x431890u
#define ADDR_MAIN_BTN_FRAMED      0x432390u

// Reverse-engineered button struct field offsets (from button_ex in ghidra).
#define BTN_OFS_CENTER_X          0x10
#define BTN_OFS_CENTER_Y          0x14
#define BTN_OFS_FLAGS             0xBC
#define BTN_OFS_LABEL_PTR         0xC8

// Bits in the 0xBC flags field that affect focus/navigation in menu logic.
#define BTN_FLAG_NOCLICK          0x00000100u
#define BTN_FLAG_NOCLICKTHRU      0x00000200u
#define BTN_FLAG_NOEMPTYCLICK     0x00000400u

// Known game state addresses (base game).
#define ADDR_ERROR_STATE           0x448204u
#define ADDR_GAME_STATE            0x448220u
#define ADDR_MAIN_STATE_INITIAL    0x448340u
#define ADDR_MAIN_STATE            0x448350u
#define ADDR_OPTIONS_STATE_PAUSED  0x448388u
#define ADDR_OPTIONS_STATE         0x448398u
#define ADDR_PREGAME_STATE         0x4483A8u
#define ADDR_REMAP_STATE2          0x4483B8u
#define ADDR_REMAP_STATE1          0x4483C8u

typedef void* (__cdecl *fn_state_current_t)(void);
typedef float (__cdecl *fn_mad_dim_t)(void);
typedef void  (__cdecl *fn_plot_text_t)(const char*, int);
typedef void  (__cdecl *fn_turtle_set_angle_t)(double);
typedef void  (__cdecl *fn_turtle_set_pos_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_scale_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_rgb_t)(float, float, float);
typedef void  (__cdecl *fn_turtle_set_rgba_t)(float, float, float, float);
typedef void  (__cdecl *fn_turtle_reset_t)(void);
typedef void* (__cdecl *fn_button_get_t)(int);
typedef void  (__cdecl *fn_button_set_layout_t)(float, float);
typedef void* (__cdecl *fn_button_ex_t)(float, float, uint32_t, const char*, int);
typedef int   (__cdecl *fn_button_count_t)(void);
typedef int   (__cdecl *fn_btn_player_filter_t)(void* btn, int event_code);
typedef void  (__cdecl *fn_main_sprite_batches_draw_t)(void);
typedef int   (__cdecl *fn_main_btn_framed_t)(int btn_ptr, int event_code);
typedef void  (__cdecl *fn_button_set_w_ex_t)(int, float, float);
typedef void  (__cdecl *fn_button_set_h_ex_t)(int, float, float);

static fn_state_current_t   p_state_current   = (fn_state_current_t)(uintptr_t)ADDR_STATE_CURRENT;
static fn_mad_dim_t         p_mad_w           = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_W;
static fn_mad_dim_t         p_mad_h           = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_H;
static fn_plot_text_t       p_plot_text       = (fn_plot_text_t)(uintptr_t)ADDR_PLOT_TEXT;
static fn_turtle_set_angle_t p_turtle_set_angle = (fn_turtle_set_angle_t)(uintptr_t)ADDR_TURTLE_SET_ANGLE;
static fn_turtle_set_pos_t  p_turtle_set_pos  = (fn_turtle_set_pos_t)(uintptr_t)ADDR_TURTLE_SET_POS;
static fn_turtle_set_scale_t p_turtle_set_scale = (fn_turtle_set_scale_t)(uintptr_t)ADDR_TURTLE_SET_SCALE;
static fn_turtle_set_rgb_t  p_turtle_set_rgb  = (fn_turtle_set_rgb_t)(uintptr_t)ADDR_TURTLE_SET_RGB;
static fn_turtle_set_rgba_t p_turtle_set_rgba = (fn_turtle_set_rgba_t)(uintptr_t)ADDR_TURTLE_SET_RGBA;
static fn_turtle_reset_t    p_turtle_reset    = (fn_turtle_reset_t)(uintptr_t)ADDR_TURTLE_RESET;
static fn_button_get_t      p_button_get      = (fn_button_get_t)(uintptr_t)ADDR_BUTTON_GET;
static fn_button_set_layout_t p_button_set_layout = (fn_button_set_layout_t)(uintptr_t)ADDR_BUTTON_SET_LAYOUT;
static fn_button_ex_t       p_button_ex       = (fn_button_ex_t)(uintptr_t)ADDR_BUTTON_EX;
static fn_button_count_t    p_button_count    = (fn_button_count_t)(uintptr_t)ADDR_BUTTON_COUNT;
static fn_btn_player_filter_t p_btn_player_filter = (fn_btn_player_filter_t)(uintptr_t)ADDR_BTN_PLAYER_FILTER;
static fn_main_sprite_batches_draw_t p_main_sprite_batches_draw = (fn_main_sprite_batches_draw_t)(uintptr_t)ADDR_MAIN_SPRITE_BATCHES_DRAW;
static fn_main_btn_framed_t   p_main_btn_framed   = (fn_main_btn_framed_t)(uintptr_t)ADDR_MAIN_BTN_FRAMED;
static fn_button_set_w_ex_t   p_button_set_w_ex   = (fn_button_set_w_ex_t)(uintptr_t)0x4160d0u;
static fn_button_set_h_ex_t   p_button_set_h_ex   = (fn_button_set_h_ex_t)(uintptr_t)0x416140u;

typedef struct UiHitBox {
    float x;
    float y;
    float w;
    float h;
} UiHitBox;

typedef struct UiLayout {
    int active;
    float cursor_x;
    float cursor_y;
    float row_h;
    float gap;
    float width;
    float text_scale;
} UiLayout;

typedef struct UiNativeButton {
    char id[64];
    char state_name[32];
    char label[128];
    float grid_x;
    float grid_y;
    float layout_x;
    float layout_y;
    void* btn_ptr;
    int clicked;
    int warned_non_menu;
} UiNativeButton;

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

    // Immediate-mode UI runtime state.
    UiHitBox* ui_hitboxes;
    int ui_hitbox_count;
    int ui_hitbox_cap;
    void* ui_state_ptr;
    UiLayout ui_layout;
    UiNativeButton** ui_native_buttons;
    int ui_native_count;
    int ui_native_cap;

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
    int  has_min;
    int  has_max;
    double min_value;
    double max_value;
} ConfigEntry;

typedef struct ConfigAction {
    char key[64];
    LuaRefList handlers;     // Lua functions
} ConfigAction;

static LoadedMod* g_mods = NULL;
static int        g_mod_count = 0;
static int        g_mod_cap = 0;

// UI input state shared across mods.
static int g_ui_mouse_x = 0;
static int g_ui_mouse_y = 0;
static int g_ui_mouse_down_left = 0;
static int g_ui_mouse_pressed_left = 0;

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

static void clamp_cfg_value(ConfigEntry* e) {
    if (!e) return;
    if (e->type != LUA_CFG_INT && e->type != LUA_CFG_FLOAT) return;

    double v = atof(e->value);
    if (e->has_min && v < e->min_value) v = e->min_value;
    if (e->has_max && v > e->max_value) v = e->max_value;

    if (e->type == LUA_CFG_INT) {
        int iv = (int)v;
        snprintf(e->value, sizeof(e->value), "%d", iv);
    } else {
        snprintf(e->value, sizeof(e->value), "%.6g", v);
    }
}

static int parse_type_and_bounds(const char* type_src,
                                 int* out_type,
                                 int* out_has_min,
                                 double* out_min,
                                 int* out_has_max,
                                 double* out_max) {
    char type_buf[64];
    char base[32];
    char bounds[48];
    char* lb = NULL;
    char* rb = NULL;

    if (!type_src || !out_type || !out_has_min || !out_min || !out_has_max || !out_max) return 0;

    strncpy(type_buf, type_src, sizeof(type_buf) - 1);
    type_buf[sizeof(type_buf) - 1] = '\0';
    str_trim(type_buf);

    *out_has_min = 0;
    *out_has_max = 0;
    *out_min = 0.0;
    *out_max = 0.0;

    lb = strchr(type_buf, '[');
    rb = lb ? strchr(lb + 1, ']') : NULL;
    if (lb && rb) {
        size_t base_len = (size_t)(lb - type_buf);
        if (base_len >= sizeof(base)) base_len = sizeof(base) - 1;
        memcpy(base, type_buf, base_len);
        base[base_len] = '\0';
        str_trim(base);

        {
            size_t b_len = (size_t)(rb - (lb + 1));
            if (b_len >= sizeof(bounds)) b_len = sizeof(bounds) - 1;
            memcpy(bounds, lb + 1, b_len);
            bounds[b_len] = '\0';
            str_trim(bounds);
        }
    } else {
        strncpy(base, type_buf, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        bounds[0] = '\0';
    }

    *out_type = string_to_type(base);
    if (*out_type == LUA_CFG_NONE) return 0;

    if (bounds[0] && (*out_type == LUA_CFG_INT || *out_type == LUA_CFG_FLOAT)) {
        char bcopy[48];
        char* comma;
        strncpy(bcopy, bounds, sizeof(bcopy) - 1);
        bcopy[sizeof(bcopy) - 1] = '\0';

        comma = strchr(bcopy, ',');
        if (comma) {
            *comma = '\0';
            comma++;
            str_trim(bcopy);
            str_trim(comma);
            if (bcopy[0]) {
                *out_min = atof(bcopy);
                *out_has_min = 1;
            }
            if (comma[0]) {
                *out_max = atof(comma);
                *out_has_max = 1;
            }
            if (*out_has_min && *out_has_max && *out_min > *out_max) {
                double t = *out_min;
                *out_min = *out_max;
                *out_max = t;
            }
        }
    }

    return 1;
}

static void format_type_with_bounds(const ConfigEntry* e, char* out, size_t outsz) {
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!e) return;

    const char* base = type_to_string(e->type);
    if ((e->type == LUA_CFG_INT || e->type == LUA_CFG_FLOAT) && (e->has_min || e->has_max)) {
        char minbuf[64] = {0};
        char maxbuf[64] = {0};
        if (e->has_min) snprintf(minbuf, sizeof(minbuf), "%.6g", e->min_value);
        if (e->has_max) snprintf(maxbuf, sizeof(maxbuf), "%.6g", e->max_value);
        snprintf(out, outsz, "%s[%s,%s]", base, minbuf, maxbuf);
    } else {
        snprintf(out, outsz, "%s", base);
    }
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
        char tbuf[80];
        format_type_with_bounds(e, tbuf, sizeof(tbuf));
        const char* t = tbuf;

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

static char* find_top_level_comma(char* s) {
    int bracket_depth = 0;
    int in_quotes = 0;
    int esc = 0;
    if (!s) return NULL;

    for (; *s; s++) {
        char c = *s;
        if (in_quotes) {
            if (esc) {
                esc = 0;
                continue;
            }
            if (c == '\\') {
                esc = 1;
                continue;
            }
            if (c == '"') {
                in_quotes = 0;
            }
            continue;
        }

        if (c == '"') {
            in_quotes = 1;
            continue;
        }
        if (c == '[') {
            bracket_depth++;
            continue;
        }
        if (c == ']') {
            if (bracket_depth > 0) bracket_depth--;
            continue;
        }
        if (c == ',' && bracket_depth == 0) {
            return s;
        }
    }

    return NULL;
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

        char* comma = find_top_level_comma(rest);
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

        int type = LUA_CFG_NONE;
        int has_min = 0;
        int has_max = 0;
        double min_value = 0.0;
        double max_value = 0.0;
        if (!parse_type_and_bounds(type_str, &type, &has_min, &min_value, &has_max, &max_value)) continue;

        ConfigEntry e;
        memset(&e, 0, sizeof(e));
        strncpy(e.key, key, sizeof(e.key) - 1);
        strncpy(e.label, key, sizeof(e.label) - 1);
        e.type = type;
        e.has_min = has_min;
        e.has_max = has_max;
        e.min_value = min_value;
        e.max_value = max_value;

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

        clamp_cfg_value(&e);

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

    m->ui_hitboxes = NULL;
    m->ui_hitbox_count = 0;
    m->ui_hitbox_cap = 0;
    m->ui_state_ptr = NULL;
    memset(&m->ui_layout, 0, sizeof(m->ui_layout));
    m->ui_native_buttons = NULL;
    m->ui_native_count = 0;
    m->ui_native_cap = 0;

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

static void* ui_current_state_ptr(void) {
    return p_state_current ? p_state_current() : NULL;
}

static int ui_is_menu_state_name(const char* name) {
    if (!name) return 0;
    return (_stricmp(name, "main") == 0 ||
            _stricmp(name, "main_initial") == 0 ||
            _stricmp(name, "options") == 0 ||
            _stricmp(name, "options_paused") == 0 ||
            _stricmp(name, "pregame") == 0 ||
            _stricmp(name, "remap1") == 0 ||
            _stricmp(name, "remap2") == 0 ||
            _stricmp(name, "mods") == 0);
}

static const char* ui_state_name_from_ptr(void* st) {
    uintptr_t p = (uintptr_t)st;
    if (!st) return "none";
    if (hooks_mods_menu_active()) return "mods";
    if (p == (uintptr_t)ADDR_MAIN_STATE) return "main";
    if (p == (uintptr_t)ADDR_MAIN_STATE_INITIAL) return "main_initial";
    if (p == (uintptr_t)ADDR_OPTIONS_STATE) return "options";
    if (p == (uintptr_t)ADDR_OPTIONS_STATE_PAUSED) return "options_paused";
    if (p == (uintptr_t)ADDR_PREGAME_STATE) return "pregame";
    if (p == (uintptr_t)ADDR_REMAP_STATE1) return "remap1";
    if (p == (uintptr_t)ADDR_REMAP_STATE2) return "remap2";
    if (p == (uintptr_t)ADDR_GAME_STATE) return "game";
    if (p == (uintptr_t)ADDR_ERROR_STATE) return "error";
    return "unknown";
}

static int ui_state_matches_name(void* st, const char* name) {
    const char* current = ui_state_name_from_ptr(st);
    if (!name || !name[0]) return 1;
    if (_stricmp(name, current) == 0) return 1;

    // Title-screen scripts often guard UI with is_state("main"). During startup,
    // the game may still report main_initial for a short handoff window.
    if (_stricmp(name, "main") == 0 && _stricmp(current, "main_initial") == 0) return 1;

    if (_stricmp(name, "menu") == 0 && ui_is_menu_state_name(current)) return 1;
    return 0;
}

static float ui_screen_w(void) {
    return p_mad_w ? p_mad_w() : 1280.0f;
}

static float ui_screen_h(void) {
    return p_mad_h ? p_mad_h() : 720.0f;
}

static float ui_approx_text_width(const char* text, float scale) {
    if (!text) return 0.0f;
    return (float)strlen(text) * 9.0f * scale;
}

static void ui_reset_render_state(void) {
    if (p_turtle_reset) {
        p_turtle_reset();
        return;
    }
    if (p_turtle_set_angle) p_turtle_set_angle(0.0);
    if (p_turtle_set_scale) p_turtle_set_scale(1.0, 1.0);
    if (p_turtle_set_rgba) p_turtle_set_rgba(1.0f, 1.0f, 1.0f, 1.0f);
    else if (p_turtle_set_rgb) p_turtle_set_rgb(1.0f, 1.0f, 1.0f);
}

static void ui_draw_text_mode(float x, float y, float scale, float r, float g, float b, const char* text, int mode) {
    if (!text || !text[0] || !p_plot_text || !p_turtle_set_pos || !p_turtle_set_scale || !p_turtle_set_angle) return;
    if (!p_turtle_set_rgb && !p_turtle_set_rgba) return;
    p_turtle_set_angle(0.0);
    p_turtle_set_scale((double)scale, (double)scale);
    if (p_turtle_set_rgb) p_turtle_set_rgb(r, g, b);
    else p_turtle_set_rgba(r, g, b, 1.0f);
    p_turtle_set_pos((double)x, (double)y);
    p_plot_text(text, mode);
}

static void mod_ui_reset_frame(LoadedMod* mod, void* state_ptr) {
    if (!mod) return;
    mod->ui_hitbox_count = 0;
    mod->ui_state_ptr = state_ptr;
}

static void mod_ui_free(LoadedMod* mod) {
    if (!mod) return;
    if (mod->ui_hitboxes) {
        free(mod->ui_hitboxes);
        mod->ui_hitboxes = NULL;
    }
    mod->ui_hitbox_count = 0;
    mod->ui_hitbox_cap = 0;
    mod->ui_state_ptr = NULL;
    memset(&mod->ui_layout, 0, sizeof(mod->ui_layout));

    if (mod->ui_native_buttons) {
        for (int i = 0; i < mod->ui_native_count; i++) {
            if (mod->ui_native_buttons[i]) free(mod->ui_native_buttons[i]);
        }
        free(mod->ui_native_buttons);
        mod->ui_native_buttons = NULL;
    }
    mod->ui_native_count = 0;
    mod->ui_native_cap = 0;
}

static void mod_ui_push_hitbox(LoadedMod* mod, float x, float y, float w, float h) {
    if (!mod) return;
    if (w <= 0.0f || h <= 0.0f) return;
    if (mod->ui_hitbox_count + 1 > mod->ui_hitbox_cap) {
        int newcap = (mod->ui_hitbox_cap == 0) ? 8 : (mod->ui_hitbox_cap * 2);
        UiHitBox* nb = (UiHitBox*)realloc(mod->ui_hitboxes, sizeof(UiHitBox) * newcap);
        if (!nb) return;
        mod->ui_hitboxes = nb;
        mod->ui_hitbox_cap = newcap;
    }
    mod->ui_hitboxes[mod->ui_hitbox_count].x = x;
    mod->ui_hitboxes[mod->ui_hitbox_count].y = y;
    mod->ui_hitboxes[mod->ui_hitbox_count].w = w;
    mod->ui_hitboxes[mod->ui_hitbox_count].h = h;
    mod->ui_hitbox_count++;
}

static int ui_point_in_hitbox(int x, int y, const UiHitBox* hb) {
    float fx = (float)x;
    float fy = (float)y;
    if (!hb) return 0;
    if (fx < hb->x) return 0;
    if (fy < hb->y) return 0;
    if (fx > hb->x + hb->w) return 0;
    if (fy > hb->y + hb->h) return 0;
    return 1;
}

static int ui_hit_any_visible_button(int x, int y) {
    void* state_ptr = ui_current_state_ptr();
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        if (mod->ui_hitbox_count <= 0) continue;
        if (mod->ui_state_ptr != state_ptr) continue;
        for (int i = 0; i < mod->ui_hitbox_count; i++) {
            if (ui_point_in_hitbox(x, y, &mod->ui_hitboxes[i])) return 1;
        }
    }
    return 0;
}

static int ui_engine_button_exists(void* btn_ptr) {
    if (!btn_ptr || !p_button_count || !p_button_get) return 0;
    int count = p_button_count();
    if (count <= 0 || count > 3000) return 0;
    for (int i = 0; i < count; i++) {
        if (p_button_get(i) == btn_ptr) return 1;
    }
    return 0;
}

static void ui_button_apply_flags_hidden(void* btn_ptr, int hidden) {
    if (!btn_ptr) return;
    uint32_t* flags = (uint32_t*)((uint8_t*)btn_ptr + BTN_OFS_FLAGS);
    if (hidden) {
        *flags |= (BTN_FLAG_NOCLICK | BTN_FLAG_NOCLICKTHRU | BTN_FLAG_NOEMPTYCLICK);
    } else {
        *flags &= ~(BTN_FLAG_NOCLICK | BTN_FLAG_NOCLICKTHRU | BTN_FLAG_NOEMPTYCLICK);
    }
}

static int ui_safe_string_readable(const char* s, int maxlen) {
    int i;
    if (!s || maxlen <= 0) return 0;
    if (IsBadStringPtrA(s, (UINT_PTR)maxlen)) return 0;
    for (i = 0; i < maxlen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\0') return 1;
        if (c < 0x09) return 0;
    }
    return 0;
}

static UiNativeButton* mod_ui_native_find(LoadedMod* mod, const char* id, const char* state_name) {
    if (!mod || !id || !id[0] || !state_name || !state_name[0]) return NULL;
    for (int i = 0; i < mod->ui_native_count; i++) {
        UiNativeButton* b = mod->ui_native_buttons[i];
        if (!b) continue;
        if (_stricmp(b->id, id) != 0) continue;
        if (_stricmp(b->state_name, state_name) != 0) continue;
        return b;
    }
    return NULL;
}

static UiNativeButton* mod_ui_native_get_or_add(LoadedMod* mod, const char* id, const char* state_name) {
    UiNativeButton* b;
    if (!mod || !id || !id[0] || !state_name || !state_name[0]) return NULL;

    b = mod_ui_native_find(mod, id, state_name);
    if (b) return b;

    if (mod->ui_native_count + 1 > mod->ui_native_cap) {
        int newcap = (mod->ui_native_cap == 0) ? 8 : (mod->ui_native_cap * 2);
        UiNativeButton** nb = (UiNativeButton**)realloc(mod->ui_native_buttons, sizeof(UiNativeButton*) * newcap);
        if (!nb) return NULL;
        mod->ui_native_buttons = nb;
        mod->ui_native_cap = newcap;
    }

    b = (UiNativeButton*)calloc(1, sizeof(UiNativeButton));
    if (!b) return NULL;

    strncpy(b->id, id, sizeof(b->id) - 1);
    strncpy(b->state_name, state_name, sizeof(b->state_name) - 1);
    b->layout_x = 5.0f;
    b->layout_y = 5.0f;
    b->grid_x = 0.0f;
    b->grid_y = 0.0f;
    b->btn_ptr = NULL;
    b->clicked = 0;
    b->warned_non_menu = 0;

    mod->ui_native_buttons[mod->ui_native_count++] = b;
    return b;
}

static UiNativeButton* ui_native_find_by_btn_ptr(void* btn_ptr) {
    if (!btn_ptr) return NULL;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        for (int bi = 0; bi < mod->ui_native_count; bi++) {
            UiNativeButton* b = mod->ui_native_buttons[bi];
            if (!b) continue;
            if (b->btn_ptr == btn_ptr) return b;
        }
    }
    return NULL;
}

static int ui_native_run_player_filter(void* btn, int event_code) {
    uint32_t* tag_ptr;
    uint32_t old_tag;
    int ok = 0;

    if (!btn) return 0;
    if (!p_btn_player_filter) return 1;

    tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    old_tag = *tag_ptr;

    *tag_ptr = 0x11;
    ok = p_btn_player_filter(btn, event_code);
    if (!ok) {
        *tag_ptr = 0x12;
        ok = p_btn_player_filter(btn, event_code);
    }
    *tag_ptr = old_tag;
    return ok;
}

// Old-style link filter: allow either player selector to activate the same button.
// This mirrors the MODS menu entry button behavior (see hooks.c), but instead of
// switching states it just toggles the UiNativeButton.clicked flag.
//
// NOTE: The selector system does not call the main framed filter. It calls the
// per-player filter function pointer stored on the button struct (observed at
// +0xE4). Native buttons created via button_ex need this field populated.
static int __cdecl ui_native_player_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;

    // Helper: try both selector tags for player 1/2. Some screens use 0x11/0x12,
    // others use 1/2.
    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;
    int ok = 0;

    // Activation: if either selector activates, mark clicked and consume so the
    // engine's default link behavior (if any) cannot fire.
    if (event_code == 3) {
        *tag_ptr = 0x11;
        ok = p_btn_player_filter(btn, event_code);
        if (!ok) {
            *tag_ptr = 0x12;
            ok = p_btn_player_filter(btn, event_code);
        }
        if (!ok) {
            *tag_ptr = 1;
            ok = p_btn_player_filter(btn, event_code);
            if (!ok) {
                *tag_ptr = 2;
                ok = p_btn_player_filter(btn, event_code);
            }
        }

        *tag_ptr = old_tag;

        if (ok) {
            UiNativeButton* b = ui_native_find_by_btn_ptr(btn);
            if (b) b->clicked = 1;
        }

        return 0;
    }

    // Non-activation events: report the button as eligible for either selector.
    *tag_ptr = 0x11;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = 0x12;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = 1;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = 2;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = old_tag;
    return 0;
}

static int __cdecl ui_native_button_filter(void* btn, int event_code) {
    // Use the game's standard framed button behavior so the keyboard selectors
    // (sword cursors) can navigate to mod-created buttons.
    int ret = 0;
    if (p_main_btn_framed) {
        ret = p_main_btn_framed((int)(intptr_t)btn, event_code);
    } else if (p_btn_player_filter) {
        // Fallback: older builds may not have main_btn_framed symbol resolved.
        ret = p_btn_player_filter(btn, event_code);
    }

    // event_code==3 corresponds to activation/click.
    if (event_code == 3 && ret) {
        UiNativeButton* b = ui_native_find_by_btn_ptr(btn);
        if (b) b->clicked = 1;
    }

    return ret;
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
            clamp_cfg_value(e);
            break;
        }
        case LUA_CFG_FLOAT: {
            double v = (double)luaL_checknumber(Ls, 2);
            snprintf(e->value, sizeof(e->value), "%.6g", v);
            clamp_cfg_value(e);
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

static UiLayout* mod_ui_layout_or_default(LoadedMod* mod) {
    if (!mod) return NULL;
    if (!mod->ui_layout.active) {
        mod->ui_layout.active = 1;
        mod->ui_layout.cursor_x = 28.0f;
        mod->ui_layout.cursor_y = 28.0f;
        mod->ui_layout.row_h = 30.0f;
        mod->ui_layout.gap = 6.0f;
        mod->ui_layout.width = 260.0f;
        mod->ui_layout.text_scale = 1.0f;
    }
    return &mod->ui_layout;
}

static int lua_ui_state_name(lua_State* Ls) {
    lua_pushstring(Ls, ui_state_name_from_ptr(ui_current_state_ptr()));
    return 1;
}

static int lua_ui_state_ptr(lua_State* Ls) {
    lua_pushnumber(Ls, (lua_Number)(uintptr_t)ui_current_state_ptr());
    return 1;
}

static int lua_ui_is_state(lua_State* Ls) {
    const char* name = luaL_checkstring(Ls, 1);
    lua_pushboolean(Ls, ui_state_matches_name(ui_current_state_ptr(), name));
    return 1;
}

static int lua_ui_screen_size(lua_State* Ls) {
    lua_pushnumber(Ls, ui_screen_w());
    lua_pushnumber(Ls, ui_screen_h());
    return 2;
}

static int lua_ui_mouse_pos(lua_State* Ls) {
    lua_pushinteger(Ls, g_ui_mouse_x);
    lua_pushinteger(Ls, g_ui_mouse_y);
    return 2;
}

static int lua_ui_layout(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    if (!layout) return 0;

    layout->active = 1;
    layout->cursor_x = (float)luaL_checknumber(Ls, 1);
    layout->cursor_y = (float)luaL_checknumber(Ls, 2);
    layout->row_h = (float)luaL_optnumber(Ls, 3, 30.0);
    layout->gap = (float)luaL_optnumber(Ls, 4, 6.0);
    layout->width = (float)luaL_optnumber(Ls, 5, 260.0);
    layout->text_scale = (float)luaL_optnumber(Ls, 6, 1.0);

    if (layout->row_h < 8.0f) layout->row_h = 8.0f;
    if (layout->gap < 0.0f) layout->gap = 0.0f;
    if (layout->width < 20.0f) layout->width = 20.0f;
    if (layout->text_scale < 0.4f) layout->text_scale = 0.4f;
    if (layout->text_scale > 3.0f) layout->text_scale = 3.0f;
    return 0;
}

static int lua_ui_cursor(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    int n = lua_gettop(Ls);
    if (!layout) return 0;
    if (n >= 1) layout->cursor_x = (float)luaL_checknumber(Ls, 1);
    if (n >= 2) layout->cursor_y = (float)luaL_checknumber(Ls, 2);
    lua_pushnumber(Ls, layout->cursor_x);
    lua_pushnumber(Ls, layout->cursor_y);
    return 2;
}

static int lua_ui_next_row(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    int rows = (int)luaL_optinteger(Ls, 1, 1);
    if (!layout) return 0;
    if (rows < 1) rows = 1;
    layout->cursor_y += (layout->row_h + layout->gap) * (float)rows;
    return 0;
}

static int lua_ui_text(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    const char* text = luaL_checkstring(Ls, 1);
    float r = (float)luaL_optnumber(Ls, 2, 0.95);
    float g = (float)luaL_optnumber(Ls, 3, 0.95);
    float b = (float)luaL_optnumber(Ls, 4, 0.95);
    float scale;
    if (!layout) return 0;
    scale = (float)luaL_optnumber(Ls, 5, layout->text_scale);
    ui_draw_text_mode(layout->cursor_x, layout->cursor_y, scale, r, g, b, text, 0);
    layout->cursor_y += layout->row_h + layout->gap;
    return 0;
}

static int lua_ui_text_at(lua_State* Ls) {
    const char* text = luaL_checkstring(Ls, 1);
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    float scale = (float)luaL_optnumber(Ls, 4, 1.0);
    float r = (float)luaL_optnumber(Ls, 5, 0.95);
    float g = (float)luaL_optnumber(Ls, 6, 0.95);
    float b = (float)luaL_optnumber(Ls, 7, 0.95);
    ui_draw_text_mode(x, y, scale, r, g, b, text, 0);
    return 0;
}

static int ui_button_common(lua_State* Ls,
                            LoadedMod* mod,
                            const char* id,
                            const char* label,
                            float x,
                            float y,
                            float w,
                            float h,
                            float scale) {
    int hovered;
    int clicked;
    char rendered[256];
    float tx;
    float ty;
    float tr = 0.82f, tg = 0.88f, tb = 0.98f;
    (void)id;

    if (!label) label = "";
    if (scale < 0.4f) scale = 0.4f;
    if (scale > 3.0f) scale = 3.0f;
    if (w <= 0.0f) w = ui_approx_text_width(label, scale) + 30.0f;
    if (h <= 0.0f) h = 28.0f;

    hovered = (g_ui_mouse_x >= (int)x &&
               g_ui_mouse_y >= (int)y &&
               g_ui_mouse_x <= (int)(x + w) &&
               g_ui_mouse_y <= (int)(y + h));
    clicked = hovered && g_ui_mouse_pressed_left;

    if (clicked) {
        tr = 1.00f; tg = 0.93f; tb = 0.45f;
    } else if (hovered && g_ui_mouse_down_left) {
        tr = 0.98f; tg = 0.86f; tb = 0.40f;
    } else if (hovered) {
        tr = 0.95f; tg = 0.90f; tb = 0.60f;
    }

    snprintf(rendered, sizeof(rendered), "[ %s ]", label);
    tx = x + 4.0f;
    ty = y + (h * 0.55f);
    ui_draw_text_mode(tx, ty, scale, tr, tg, tb, rendered, 0);
    mod_ui_push_hitbox(mod, x, y, w, h);

    lua_pushboolean(Ls, clicked);
    return 1;
}

static int lua_ui_button(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    const char* id = luaL_checkstring(Ls, 1);
    const char* label = luaL_checkstring(Ls, 2);
    float w;
    float h;
    int ret;

    if (!layout) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    w = (float)luaL_optnumber(Ls, 3, layout->width);
    h = (float)luaL_optnumber(Ls, 4, layout->row_h);
    ret = ui_button_common(Ls, mod, id, label, layout->cursor_x, layout->cursor_y, w, h, layout->text_scale);
    layout->cursor_y += h + layout->gap;
    return ret;
}

static int lua_ui_button_at(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    const char* id = luaL_checkstring(Ls, 1);
    const char* label = luaL_checkstring(Ls, 2);
    float x = (float)luaL_checknumber(Ls, 3);
    float y = (float)luaL_checknumber(Ls, 4);
    float w = (float)luaL_optnumber(Ls, 5, 0.0);
    float h = (float)luaL_optnumber(Ls, 6, 0.0);
    float scale = layout ? layout->text_scale : 1.0f;
    return ui_button_common(Ls, mod, id, label, x, y, w, h, scale);
}

static int lua_ui_native_button(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    const char* label = luaL_checkstring(Ls, 2);
    float grid_x = (float)luaL_checknumber(Ls, 3);
    float grid_y = (float)luaL_checknumber(Ls, 4);
    float layout_x = (float)luaL_optnumber(Ls, 5, 5.0);
    float layout_y = (float)luaL_optnumber(Ls, 6, 5.0);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    // Treat main_initial as main so title-menu native buttons appear immediately
    // on startup and persist across the title state handoff.
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;
    int clicked = 0;

    if (!mod) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    b = mod_ui_native_get_or_add(mod, id, button_state_name);
    if (!b) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    strncpy(b->label, label ? label : "", sizeof(b->label) - 1);
    b->label[sizeof(b->label) - 1] = '\0';
    b->grid_x = grid_x;
    b->grid_y = grid_y;
    b->layout_x = layout_x;
    b->layout_y = layout_y;

    if (b->btn_ptr && !ui_engine_button_exists(b->btn_ptr)) {
        b->btn_ptr = NULL;
    }

    if (!ui_is_menu_state_name(state_name)) {
        if (!b->warned_non_menu) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "ui.native_button('%s') ignored in non-menu state '%s'",
                     b->id, state_name);
            log_mod(mod, "WARN", msg);
            b->warned_non_menu = 1;
        }
        clicked = b->clicked;
        b->clicked = 0;
        lua_pushboolean(Ls, clicked);
        return 1;
    }

    b->warned_non_menu = 0;

    if (!b->btn_ptr && p_button_ex) {
        if (p_button_set_layout) p_button_set_layout(b->layout_x, b->layout_y);
        b->btn_ptr = p_button_ex(b->grid_x, b->grid_y, 0, b->label, (int)(intptr_t)&ui_native_button_filter);
        if (b->btn_ptr) {
            // Match MODS menu button behavior so sword selectors can land on it.
            // +0xE0: link target / state bridge (must be non-null for some menus)
            // +0xE4: per-player filter pointer (used by selectors)
            *(void**)((uint8_t*)b->btn_ptr + 0xE0) = ui_current_state_ptr();
            *(void**)((uint8_t*)b->btn_ptr + 0xE4) = (void*)&ui_native_player_filter_proxy;
        }
    }

    clicked = b->clicked;
    b->clicked = 0;
    lua_pushboolean(Ls, clicked);
    return 1;
}

static int lua_ui_native_set_pos(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    *(float*)((uint8_t*)b->btn_ptr + BTN_OFS_CENTER_X) = x;
    *(float*)((uint8_t*)b->btn_ptr + BTN_OFS_CENTER_Y) = y;

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_set_layout(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float layout_x = (float)luaL_checknumber(Ls, 2);
    float layout_y = (float)luaL_checknumber(Ls, 3);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b) { lua_pushboolean(Ls, 0); return 1; }

    b->layout_x = layout_x;
    b->layout_y = layout_y;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_resize(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float w = (float)luaL_checknumber(Ls, 2);
    float h = (float)luaL_checknumber(Ls, 3);
    float shrink = (float)luaL_optnumber(Ls, 4, 4.0);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod || w <= 0.0f || h <= 0.0f) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    if (shrink < 0.0f) shrink = 0.0f;
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)b->btn_ptr, w, shrink);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)b->btn_ptr, h, shrink);

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_hide(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    int hidden = lua_toboolean(Ls, 2);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    ui_button_apply_flags_hidden(b->btn_ptr, hidden);
    lua_pushboolean(Ls, 1);
    return 1;
}

static void* ui_lua_ptr_to_button(lua_State* Ls, int idx) {
    if (!lua_isnumber(Ls, idx)) return NULL;
    uintptr_t raw = (uintptr_t)lua_tonumber(Ls, idx);
    void* btn = (void*)raw;
    if (!ui_engine_button_exists(btn)) return NULL;
    return btn;
}

static int lua_ui_find_button_by_label(lua_State* Ls) {
    const char* label = luaL_checkstring(Ls, 1);
    int nth = (int)luaL_optinteger(Ls, 2, 1);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    if (!label || !label[0] || nth < 1 || !p_button_count || !p_button_get) {
        lua_pushnil(Ls);
        return 1;
    }

    // During main_initial handoff the button list is not always stable yet.
    if (_stricmp(state_name, "main_initial") == 0) {
        lua_pushnil(Ls);
        return 1;
    }

    int count = p_button_count();
    if (count <= 0 || count > 3000) {
        lua_pushnil(Ls);
        return 1;
    }
    for (int i = 0; i < count; i++) {
        void* btn = p_button_get(i);
        if (!btn) continue;
        const char* txt = *(const char**)((uint8_t*)btn + BTN_OFS_LABEL_PTR);
        if (!ui_safe_string_readable(txt, 128)) continue;
        if (_stricmp(txt, label) != 0) continue;
        nth--;
        if (nth == 0) {
            lua_pushnumber(Ls, (lua_Number)(uintptr_t)btn);
            return 1;
        }
    }

    lua_pushnil(Ls);
    return 1;
}

static int lua_ui_button_rect_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    if (!btn) {
        lua_pushnil(Ls);
        return 1;
    }

    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X));
    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y));
    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + 0x20));
    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + 0x24));
    return 4;
}

static int lua_ui_button_set_pos_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    if (!btn) { lua_pushboolean(Ls, 0); return 1; }
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X) = x;
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y) = y;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_button_resize_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    float w = (float)luaL_checknumber(Ls, 2);
    float h = (float)luaL_checknumber(Ls, 3);
    float shrink = (float)luaL_optnumber(Ls, 4, 4.0);
    if (!btn || w <= 0.0f || h <= 0.0f) { lua_pushboolean(Ls, 0); return 1; }
    if (shrink < 0.0f) shrink = 0.0f;
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)btn, w, shrink);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)btn, h, shrink);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_button_hide_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    int hidden = lua_toboolean(Ls, 2);
    if (!btn) { lua_pushboolean(Ls, 0); return 1; }
    ui_button_apply_flags_hidden(btn, hidden);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_button_remove_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    if (!btn) { lua_pushboolean(Ls, 0); return 1; }

    ui_button_apply_flags_hidden(btn, 1);
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)btn, 1.0f, 0.0f);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)btn, 1.0f, 0.0f);
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X) = -10000.0f;
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y) = -10000.0f;

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_remove(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }

    for (int i = 0; i < mod->ui_native_count; i++) {
        UiNativeButton* b = mod->ui_native_buttons[i];
        if (!b) continue;
        if (_stricmp(b->id, id) != 0) continue;
        if (_stricmp(b->state_name, button_state_name) != 0) continue;

        free(b);
        for (int j = i + 1; j < mod->ui_native_count; j++) {
            mod->ui_native_buttons[j - 1] = mod->ui_native_buttons[j];
        }
        mod->ui_native_count--;
        lua_pushboolean(Ls, 1);
        return 1;
    }

    lua_pushboolean(Ls, 0);
    return 1;
}

static void push_ui_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_state_name, 1); lua_setfield(Ls, -2, "state_name");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_state_ptr, 1);  lua_setfield(Ls, -2, "state_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_is_state, 1);   lua_setfield(Ls, -2, "is_state");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_screen_size, 1);lua_setfield(Ls, -2, "screen_size");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_mouse_pos, 1);  lua_setfield(Ls, -2, "mouse_pos");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_layout, 1);     lua_setfield(Ls, -2, "layout");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_cursor, 1);     lua_setfield(Ls, -2, "cursor");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_next_row, 1);   lua_setfield(Ls, -2, "next_row");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_text, 1);       lua_setfield(Ls, -2, "text");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_text_at, 1);    lua_setfield(Ls, -2, "text_at");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button, 1);     lua_setfield(Ls, -2, "button");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_at, 1);  lua_setfield(Ls, -2, "button_at");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_button, 1);      lua_setfield(Ls, -2, "native_button");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_set_pos, 1);     lua_setfield(Ls, -2, "native_set_pos");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_set_layout, 1);  lua_setfield(Ls, -2, "native_set_layout");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_resize, 1);      lua_setfield(Ls, -2, "native_resize");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_hide, 1);        lua_setfield(Ls, -2, "native_hide");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_remove, 1);      lua_setfield(Ls, -2, "native_remove");

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_find_button_by_label, 1); lua_setfield(Ls, -2, "find_button_by_label");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_rect_ptr, 1);      lua_setfield(Ls, -2, "button_rect_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_set_pos_ptr, 1);   lua_setfield(Ls, -2, "button_set_pos_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_resize_ptr, 1);    lua_setfield(Ls, -2, "button_resize_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_hide_ptr, 1);      lua_setfield(Ls, -2, "button_hide_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_remove_ptr, 1);    lua_setfield(Ls, -2, "button_remove_ptr");
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

    // UI helpers (immediate mode, usable from on_frame in any game state).
    push_ui_api_table(Ls, mod);
    lua_setfield(Ls, -2, "ui");

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

    g_ui_mouse_x = 0;
    g_ui_mouse_y = 0;
    g_ui_mouse_down_left = 0;
    g_ui_mouse_pressed_left = 0;

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
        mod_ui_free(mod);

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

static int ui_handle_event(const char* type, int x, int y, int button) {
    if (!type) return 0;

    if (_stricmp(type, "mousemotion") == 0) {
        g_ui_mouse_x = x;
        g_ui_mouse_y = y;
        return 0;
    }

    if (_stricmp(type, "mousebuttondown") == 0) {
        g_ui_mouse_x = x;
        g_ui_mouse_y = y;
        if (button == 1) {
            g_ui_mouse_down_left = 1;
            g_ui_mouse_pressed_left = 1;
            return ui_hit_any_visible_button(x, y);
        }
        return 0;
    }

    if (_stricmp(type, "mousebuttonup") == 0) {
        g_ui_mouse_x = x;
        g_ui_mouse_y = y;
        if (button == 1) {
            g_ui_mouse_down_left = 0;
            return ui_hit_any_visible_button(x, y);
        }
        return 0;
    }

    return 0;
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
    void* state_ptr = ui_current_state_ptr();

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        mod_ui_reset_frame(mod, state_ptr);
    }

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

    // Reset turtle state before flushing; sprite batching relies on consistent globals.
    ui_reset_render_state();

    // Flush any sprites plotted by mods this frame so they render immediately and
    // don't carry over into the next frame (which can break glow layering).
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }

    ui_reset_render_state();
    g_ui_mouse_pressed_left = 0;
}

// Returns 1 if consumed by any mod (a handler returned true), else 0.
int lua_manager_on_event(const char* type, int sym, int scancode, int modmask, int x, int y, int button) {
    if (!L) return 0;
    int ui_consumed = ui_handle_event(type, x, y, button);

    // Build event table once
    lua_newtable(L);
    lua_pushstring(L, type);      lua_setfield(L, -2, "type");
    lua_pushinteger(L, sym);      lua_setfield(L, -2, "sym");
    lua_pushinteger(L, scancode); lua_setfield(L, -2, "scancode");
    lua_pushinteger(L, modmask);  lua_setfield(L, -2, "mod");
    lua_pushinteger(L, x);        lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);        lua_setfield(L, -2, "y");
    lua_pushinteger(L, button);   lua_setfield(L, -2, "button");

    int consumed = ui_consumed;
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
    if (e->has_min && (double)v < e->min_value) v = (int)e->min_value;
    if (e->has_max && (double)v > e->max_value) v = (int)e->max_value;
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
    if (e->has_min && v < e->min_value) v = e->min_value;
    if (e->has_max && v > e->max_value) v = e->max_value;
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

    // Find registered handlers.
    //
    // We snapshot refs before invoking callbacks so action handlers that mutate
    // config registrations (directly or indirectly) cannot invalidate the
    // ConfigAction pointer while this loop is running.
    for (int ai = 0; ai < m->cfg_action_count; ai++) {
        ConfigAction* a = &m->cfg_actions[ai];
        if (_stricmp(a->key, e->key) != 0) continue;

        if (a->handlers.count <= 0) break;

        int count = a->handlers.count;
        int* refs = (int*)malloc(sizeof(int) * count);
        if (!refs) {
            log_mod(m, "ERROR", "config action handler dispatch failed: out of memory");
            return;
        }

        for (int i = 0; i < count; i++) {
            refs[i] = a->handlers.refs[i];
        }

        for (int hi = 0; hi < count; hi++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[hi]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "config action '%s' error: %s", e->key, err ? err : "(unknown)");
                log_mod(m, "ERROR", buf);
                lua_pop(L, 1);
                m->error_count++;
            }
        }

        free(refs);
        break;
    }
}
