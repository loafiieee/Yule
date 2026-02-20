#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Public API for the mod framework runtime.
// (Used by hooks.c to build the MODS/config UI.)

// Framework API version (bump on breaking Lua API changes)
int lua_manager_framework_api(void);

// Mods
int         lua_manager_get_mod_count(void);
const char* lua_manager_get_mod_id(int mod_index);
const char* lua_manager_get_mod_name(int mod_index);
const char* lua_manager_get_mod_version(int mod_index);
int         lua_manager_get_mod_enabled(int mod_index);

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

// Current time-scale multiplier derived from delta_time (1.0 = normal).
float lua_manager_get_time_scale(void);

#ifdef __cplusplus
}
#endif
