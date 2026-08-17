#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Public API for the mod framework runtime.
// (Used by hooks.c to build the MODS/config UI.)

// Framework API major/revision. Major changes are breaking; revisions add.
int lua_manager_framework_api(void);
int lua_manager_framework_api_revision(void);
int lua_manager_framework_api_capability_count(void);

/* Resolve a mod.content symbolic sheet key (owner:sheet) after the current
 * atlas build. Returns 0 while the owner/sheet is absent or not yet packed. */
int lua_manager_content_resolve_sprite(const char* qualified_sheet,
                                       int sprite_index,
                                       int* out_sprite_id);

// True when any enabled mod has called mod.game.register_bot_provider().
int lua_manager_has_bot_provider(void);

// Mod-registered main-menu modes (PLAY/ONLINE are framework built-ins).
// Slots are stable; info() returns 0 for empty/inactive slots. activate()
// runs the mode's Lua on_activate(player_index) and returns 1 when the menu
// should proceed with the native START flow.
int lua_manager_menu_mode_count(void);
int lua_manager_menu_mode_info(int idx, const char** out_id, const char** out_label,
                               float* out_r, float* out_g, float* out_b);
int lua_manager_menu_mode_activate(int idx, int player_index);

typedef struct LuaModDiagnostics {
    int trace_events;
    int on_frame_handlers;
    int on_event_handlers;
    int on_layout_handlers;
    int config_entries;
    int bind_entries;
    int storage_entries;
    int audio_chunks;
    int audio_generated_chunks;
    unsigned int audio_generated_pcm_bytes;
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
// Runtime safety classification used by online rollback matches. A mod becomes
// gameplay-affecting automatically when it registers deterministic tick hooks,
// registers a bot/menu-mode provider or custom content, changes delta time, or
// calls a mutating mod.game API. Gameplay-affecting mods are suspended while
// the online guard is active; cosmetic-only mods continue to receive
// frame/event/layout callbacks.
int         lua_manager_get_mod_gameplay_affecting(int mod_index);
int         lua_manager_get_mod_suspended(int mod_index);
const char* lua_manager_get_mod_suspend_reason(int mod_index);
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

// Online lifecycle guard. begin/end are idempotent and are intended to bracket
// the entire matchmaking/countdown/match window (begin as soon as a match is
// assigned, end only after every online/retry session has been torn down).
// begin returns the number of already-classified gameplay mods being suspended.
int  lua_manager_online_suspend_begin(void);
void lua_manager_online_suspend_end(void);
int  lua_manager_online_suspend_active(void);

// Config entry types
enum {
  LUA_CFG_NONE   = 0,
  LUA_CFG_BOOL   = 1,
  LUA_CFG_INT    = 2,
  LUA_CFG_FLOAT  = 3,
  LUA_CFG_STRING = 4,
  LUA_CFG_ACTION = 5,
  LUA_CFG_OPTIONS = 6,   // enum: one of a fixed list, declared as options[a, b, c]
};

// Config
int         lua_manager_get_mod_config_count(int mod_index);
int         lua_manager_get_mod_config_type(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_key(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_label(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_value_str(int mod_index, int entry_index);
int         lua_manager_find_mod_config_index(int mod_index, const char* key);
int         lua_manager_get_mod_config_option_count(int mod_index, int entry_index);
const char* lua_manager_get_mod_config_option(int mod_index, int entry_index, int option_index);

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
int lua_manager_config_cycle_option(int mod_index, int entry_index, int delta);
int lua_manager_config_set_option(int mod_index, int entry_index, const char* value);

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

// Deterministic gameplay update hook, fired from native game_update.
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

// Called by the atlas_upload hook while load_gfx still has a live packing atlas.
void lua_manager_before_atlas_upload(int atlas_ptr);

// Console Lua execution helpers.
int lua_manager_console_eval(const char* code, char* out, int out_sz);
int lua_manager_console_eval_mod(const char* mod_id, const char* code, char* out, int out_sz);
int lua_manager_console_run_file(const char* path, char* out, int out_sz);

/* Owner-scoped mod console commands. Execute returns 1 when a command ran,
 * -1 when it was found but rejected/failed, and 0 when it was not found. */
int lua_manager_console_command_count(void);
const char* lua_manager_console_command_at(int index);
int lua_manager_console_command_help(const char* name,
                                     char* help, int help_sz,
                                     char* usage, int usage_sz);
int lua_manager_console_execute_command(const char* name, const char* args,
                                        char* out, int out_sz);

/* Allocation-free/nonblocking generated-PCM bridge used by Eggnogg's existing
 * audio callback. Generated bytebeat never opens a competing SDL audio device. */
int lua_manager_audio_generated_active(void);
void lua_manager_audio_mix_generated(int16_t* samples, int frame_count,
                                     int output_rate);

// Native gameplay-state serialization API used by rollback/network code.
#define LUA_ROLLBACK_SUMMARY_THING_SLOTS 16

typedef struct LuaGameStateRollbackSummary {
    uint32_t full_crc;
    uint32_t header_crc;
    uint32_t transient_crc;
    uint32_t thing_info_crc;
    uint32_t room_info_crc;
    uint32_t particle_crc;
    uint32_t players_crc;
    uint32_t things_crc;
    uint32_t tilemap_crc;
    uint32_t active_room;
    uint32_t native_game_ticks;
    uint32_t rng_seed;
    uint32_t seed;
    uint32_t thing_count;
    uint32_t map_selector;
    uint32_t round_end_any;
    uint32_t score_p0;
    uint32_t score_p1;
    uint32_t player0_crc;
    uint32_t player1_crc;
    uint32_t thing_slot_crc[LUA_ROLLBACK_SUMMARY_THING_SLOTS];
} LuaGameStateRollbackSummary;

/* Peer-local room/mine palette interpolation. These values are deliberately
 * absent from gameplay checksums, but a local rollback must preserve the
 * coherent pending flag, timer, and colour block together. */
#define LUA_GAME_PALETTE_FLOATS 48
typedef struct LuaGamePaletteState {
    int32_t pending;
    int32_t lerp_time;
    float colours[LUA_GAME_PALETTE_FLOATS];
} LuaGamePaletteState;

size_t lua_manager_game_state_size(void);
/* Non-cryptographic identity of the serialized field schema and the live map's
 * structural layout. Returns zero until a valid tilemap has been installed. */
uint32_t lua_manager_game_state_layout_fingerprint(void);
int lua_manager_game_state_save(void* dst, size_t dst_len, size_t* out_len, char* err, size_t err_cap);
int lua_manager_game_state_load(const void* src, size_t src_len, char* err, size_t err_cap);
int lua_manager_game_state_load_rollback(const void* src, size_t src_len, char* err, size_t err_cap);
int lua_manager_game_state_checksum(uint32_t* out_crc, char* err, size_t err_cap);
int lua_manager_game_state_rollback_checksum(uint32_t* out_crc, char* err, size_t err_cap);
int lua_manager_game_state_rollback_summary(LuaGameStateRollbackSummary* out_summary, char* err, size_t err_cap);
int lua_manager_game_state_canonicalize_rollback(void* blob, size_t blob_len, char* err, size_t err_cap);
/* Analyze one already captured/canonicalized rollback boundary. The checksum
 * and optional component summary are derived from the same checksum-canonical
 * scratch copy, so diagnostics never recapture mutable live state. */
int lua_manager_game_state_analyze_canonical_rollback_blob(
    const void* blob, size_t blob_len, uint32_t* out_crc,
    LuaGameStateRollbackSummary* out_summary,
    char* err, size_t err_cap);
/* Validate every precondition required by rollback load without mutating live
 * state, then checksum a canonicalized scratch copy of the supplied blob. */
int lua_manager_game_state_validate_rollback_blob(const void* blob, size_t blob_len,
                                                  uint32_t* out_crc,
                                                  char* err, size_t err_cap);
/* Network snapshots must already be in the pointer-free rollback transport
 * form produced by game_state_canonicalize_rollback; checksum-only masks are
 * still applied on a scratch copy. */
int lua_manager_game_state_validate_rollback_transport_blob(
    const void* blob, size_t blob_len, uint32_t* out_crc,
    char* err, size_t err_cap);
const char* lua_manager_game_state_offset_name(size_t offset);
int lua_manager_game_rng_seed(uint32_t* out_seed);
int lua_manager_game_set_rng_seed(uint32_t seed);
int lua_manager_game_camera(float* out_x, float* out_y);
int lua_manager_game_set_camera(float x, float y);
int lua_manager_game_camera_shake(float* out_shake, float* out_decay);
int lua_manager_game_palette_capture(LuaGamePaletteState* out_state);
int lua_manager_game_palette_restore(const LuaGamePaletteState* state);
/* Native _game_w/_game_h are derived from local layout outside gameplay ticks
 * but are also read by native simulation. Online rollback pins them through
 * these checked accessors; local drawable/viewport ownership remains separate. */
int lua_manager_game_width(float* out_width);
int lua_manager_game_set_width(float width);
int lua_manager_game_height(float* out_height);
int lua_manager_game_set_height(float height);
int lua_manager_game_set_geometry(float game_w, float game_h);
/* Preflight then restore every out-of-tick simulation value as one operation.
 * No destination is changed when validation fails. */
int lua_manager_game_set_sim_state(uint32_t seed, float camera_x,
                                   float camera_y, float camera_shake,
                                   float camera_shake_decay, float game_w,
                                   float game_h);
/* Diagnostic: dump per-row/per-column tilemap CRCs (tagged p=<player>) to the
 * append dump file, to localize non-RNG "changed=tilemap" desyncs. */
void lua_manager_game_dump_tilemap_diag(int player);
/* Frame-accurate variant: dump the tilemap from a SAVED state blob (same frame on
 * both peers -> clean diff, no cosmetic-animation frame skew). */
void lua_manager_game_dump_tilemap_blob(int player, unsigned int frame, const void* blob, size_t blob_len);
int lua_manager_game_native_ticks(uint32_t* out_ticks);
int lua_manager_game_set_native_ticks(uint32_t ticks);

#ifdef EGGNOGGPLUS_SERIALIZER_TESTING
/* Test-only address seam for exercising the production serializer without a
 * running game image. Release builds do not expose or compile this function. */
int lua_manager_test_bind_native_state(void* base, size_t size,
                                       char* err, size_t err_cap);
#endif

#ifdef __cplusplus
}
#endif
