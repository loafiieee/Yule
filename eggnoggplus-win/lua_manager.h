#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Public API for the mod framework runtime.
// (Used by hooks.c to build the MODS/config UI.)

// Framework API version (bump on breaking Lua API changes)
int lua_manager_framework_api(void);

typedef struct LuaModDiagnostics {
    int trace_events;
    int on_frame_handlers;
    int on_event_handlers;
    int on_layout_handlers;
    int config_entries;
    int bind_entries;
    int storage_entries;
    int audio_chunks;
    int font_registrations;
    int texture_registrations;
    unsigned int approx_memory_bytes;
    unsigned int frame_calls;
    unsigned int event_calls;
    unsigned int layout_calls;
    double frame_last_ms;
    double frame_avg_ms;
    double frame_max_ms;
    double event_last_ms;
    double event_avg_ms;
    double event_max_ms;
    double layout_last_ms;
    double layout_avg_ms;
    double layout_max_ms;
} LuaModDiagnostics;

// Mods
int         lua_manager_get_mod_count(void);
const char* lua_manager_get_mod_id(int mod_index);
const char* lua_manager_get_mod_name(int mod_index);
const char* lua_manager_get_mod_version(int mod_index);
const char* lua_manager_get_mod_author(int mod_index);
const char* lua_manager_get_mod_description(int mod_index);
int         lua_manager_get_mod_enabled(int mod_index);
int         lua_manager_get_mod_error_count(int mod_index);
int         lua_manager_get_mod_dependency_count(int mod_index);
const char* lua_manager_get_mod_dependency_id(int mod_index, int dep_index);
int         lua_manager_get_mod_dependency_optional(int mod_index, int dep_index);
int         lua_manager_mod_dependency_satisfied(int mod_index, int dep_index);
int         lua_manager_get_mod_conflict_count(int mod_index);
const char* lua_manager_get_mod_conflict_id(int mod_index, int conflict_index);
int         lua_manager_mod_conflict_active(int mod_index, int conflict_index);
int         lua_manager_get_mod_diagnostics(int mod_index, LuaModDiagnostics* out_diag);
int         lua_manager_set_mod_trace_events(int mod_index, int enabled);
int         lua_manager_set_mod_enabled(int mod_index, int enabled);

// Config entry types
enum {
  LUA_CFG_NONE   = 0,
  LUA_CFG_BOOL   = 1,
  LUA_CFG_INT    = 2,
  LUA_CFG_FLOAT  = 3,
  LUA_CFG_STRING = 4,
  LUA_CFG_ACTION = 5,
};

// Config
int         lua_manager_get_mod_config_count(int mod_index);
int         lua_manager_get_mod_config_type(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_key(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_label(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_value_str(int mod_index, int entry_index);
int         lua_manager_find_mod_config_index(int mod_index, const char* key);

// Input bindings
int         lua_manager_get_mod_bind_count(int mod_index);
const char* lua_manager_get_mod_bind_key(int mod_index, int bind_index);
const char* lua_manager_get_mod_bind_label(int mod_index, int bind_index);
const char* lua_manager_get_mod_bind_value_str(int mod_index, int bind_index);
int         lua_manager_set_mod_bind_value(int mod_index, int bind_index, int sym);
int         lua_manager_clear_mod_bind_value(int mod_index, int bind_index);
int         lua_manager_mod_bind_has_conflict(int mod_index, int bind_index);
void        lua_manager_on_key_event(int sym, int is_down);

// Mutations (return 1 on success)
int lua_manager_config_toggle_bool(int mod_index, int entry_index);
int lua_manager_config_increment_int(int mod_index, int entry_index, int delta);
int lua_manager_config_increment_float(int mod_index, int entry_index, double delta);
int lua_manager_config_set_string(int mod_index, int entry_index, const char* value);

// Actions
void lua_manager_config_trigger_action(int mod_index, int entry_index);

// -----------------
// Time scaling hook
// -----------------
// Called once per rendered frame. Emits a synthetic "delta_time" event where
// mods may read/modify e.value (seconds).
// Returns the possibly modified dt.
double lua_manager_on_delta_time(double dt_seconds);

// Exposed for documentation purposes; fired internally by lua_manager_on_frame
// when it detects a state transition. Mods register handlers via mod.on_layout().
// (No external callers needed - no new hooks required.)

// Deterministic gameplay update hook, fired from main_update_with_buttons.
void lua_manager_on_tick(void);
void lua_manager_on_tick_post(void);
unsigned long long lua_manager_get_tick_count(void);

// Current time-scale multiplier derived from delta_time (1.0 = normal).
float lua_manager_get_time_scale(void);
int lua_manager_set_time_scale(float scale);
void lua_manager_clear_time_scale(void);
int lua_manager_get_time_scale_manual(float* out_scale);

// Manual reload helpers used by native tools (console/UI).
int lua_manager_reload_mods(void);
int lua_manager_reload_assets(
    int* out_textures_reloaded,
    int* out_textures_failed,
    int* out_textures_restart_required,
    int* out_fonts_reloaded,
    int* out_fonts_failed,
    int* out_fonts_restart_required
);

// Console Lua execution helpers.
int lua_manager_console_eval(const char* code, char* out, int out_sz);
int lua_manager_console_eval_mod(const char* mod_id, const char* code, char* out, int out_sz);
int lua_manager_console_run_file(const char* path, char* out, int out_sz);

#ifdef __cplusplus
}
#endif
