#pragma once

#include <stddef.h>
#include <stdint.h>

#include "map_script.h"
#include "map_ambiance.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CUSTOM_MAP_MAX_FINAL_ROOMS 64
#define CUSTOM_MAP_MAX_CONNECTIONS 256
/* The supported executable reserves exactly 0x4444 bytes for 17 room-info
 * entries (17 * 0x404). The schema keeps a larger tooling ceiling, but a map
 * admitted to gameplay must stay within this native limit. */
#define CUSTOM_MAP_ENGINE_MAX_FINAL_ROOMS 17

#ifdef CUSTOM_MAPS_TESTING
/* Fixture-only pinning without native fixed-address mapgen writes. */
int custom_maps_test_pin_folder(const char* folder_id);
#endif
void custom_maps_init(void);
void custom_maps_shutdown(void);
/* Visual override from the installed map generation; no filesystem polling.
 * Returns zero without touching output when the selected map uses native color. */
int custom_maps_pinned_eggnogg_color(int selector, float out_rgb[3]);
void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void));

/* Load the staged maps/_greggnogg_previews/<32-lowercase-hex-token> package.
 * Authored namespaces are retained; preview overrides stay offline-only. */
int custom_maps_install_preview_folder(const char* token,int* selector,char* error,size_t error_size);
void custom_maps_clear_preview(void);
/* Validate and install the legacy in-memory V1 text preview. */
int custom_maps_install_preview_text(const char* json_text,
                                     const char* map_text,
                                     int* out_selector,
                                     char* err,
                                     size_t err_cap);

/* Build a JSON array suitable for the online control server:
   [{key,selector,label,kind}, ...]. Returns bytes that would have been written,
   excluding the trailing NUL. */
int custom_maps_build_manifest_json(char* out, size_t out_sz);

/* Resolve an online map key such as "vanilla:2" or "custom:<id>:<sig>" to the
   local map selector. Returns 1 on success. */
int custom_maps_selector_for_key(const char* key, int* out_selector);

/* Total number of valid map selector values (vanilla + registered custom). */
int custom_maps_total_selectors(void);
uint64_t custom_maps_generation(void);

typedef struct CustomMapContentView {
    uint64_t generation;
    int selector;
    int format_version;
    int source_room_count;
    int variable_rooms;
    int room_graph;
    int room_width[9];
    int room_height[9];
    int final_room_count;
    int graph_start_room;
    int connection_count;
    int layout_width;
    int layout_height;
    int final_source_room[CUSTOM_MAP_MAX_FINAL_ROOMS];
    int final_x[CUSTOM_MAP_MAX_FINAL_ROOMS];
    int final_y[CUSTOM_MAP_MAX_FINAL_ROOMS];
    int final_mirror_x[CUSTOM_MAP_MAX_FINAL_ROOMS];
    int content_tile_count;
    /* Empty/zero when tileset.sprite_sheet is not declared. External defaults
     * use their qualified map sheet key and expose the validated grid count. */
    char default_sheet_key[128];
    int default_sheet_sprite_count;
    int native_layout;
} CustomMapContentView;

/* Opens a generation-stable metadata view without retaining registry pointers.
 * Returns 1 for a custom selector, 0 for a vanilla selector, and -1 when the
 * selector is invalid. A view is intended for one immediate map-generation
 * pass; view_cell returns -1 if a reload made it stale. */
int custom_maps_content_view_open(int selector, CustomMapContentView* out_view);

/* Copies metadata from the exact custom-map registry generation pinned by
 * mapgen_init. It never polls the filesystem. Use this after live content
 * binding when native-layout setup must match the installed room definitions.
 * Returns 1 custom, 0 vanilla, -1 when no matching custom map is pinned. */
int custom_maps_pinned_content_view(int selector,
                                    CustomMapContentView* out_view);

/* Reads the script identity from the exact registry generation pinned by
 * mapgen_init. Returns 1 for a pinned custom selector (with zero meaning that
 * the package has no map.lua), 0 for vanilla, and -1 when no matching custom
 * map is pinned. This never polls or rebuilds the registry. */
int custom_maps_pinned_script_id(int selector, uint64_t* out_script_id);

/* Copies the online identity from the exact map generation pinned by
 * mapgen_init. Vanilla selectors are formatted as "vanilla:<index>" without
 * consulting the mutable registry. Returns 1 on success and -1 when a custom
 * selector has no matching pin. This never polls or rebuilds the registry. */
int custom_maps_pinned_online_key(int selector, char* out_key, size_t out_key_size);

/* Returns 1 for a symbolic content cell, 0 for an ordinary native cell, and
 * -1 for invalid coordinates or a stale/invalid view. Source room indices use
 * layout.order (center outward); coordinates are 0-based. This function does
 * not poll the filesystem, so a full map bind incurs exactly one reload poll. */
int custom_maps_content_view_cell(const CustomMapContentView* view,
                                  int source_room,
                                  int x,
                                  int y,
                                  char* out_key,
                                  size_t out_key_size,
                                  char* out_native_glyph);

/* Resolves a live world-space pixel coordinate against the exact pinned map
 * generation and returns its one-byte authored source glyph. This deliberately
 * preserves custom V2 symbols instead of their native collision fallback.
 * Mirrored outer rooms are mapped back to their authored source cell. Returns
 * 1 for a cell, 0 outside the map, and -1 for invalid arguments or unavailable
 * pinned content. */
int custom_maps_pinned_tile_at_world(int selector,
                                     double world_x,
                                     double world_y,
                                     char* out_reference,
                                     size_t out_reference_size);

/* Rebuilds the live native tilemap for a pinned variable_cells map after the
 * stock builder has initialized tile definitions and room metadata. Returns
 * 1 when a variable layout was rebuilt, 0 for fixed/vanilla maps, and -1 on
 * allocation or native-state failure. */
int custom_maps_rebuild_variable_map(int selector);

/* Resolves variable room bounds in live world pixels. Returns 1 for a pinned
 * variable layout, 0 for fixed/vanilla layouts, and -1 when x is outside it. */
int custom_maps_variable_room_bounds(int selector, double world_x,
                                     int* out_final_room,
                                     int* out_start_px,
                                     int* out_width_px,
                                     int* out_height_px);
/* Resolves an authored final room using both world axes. The graph is
 * normalized so its minimum authored X/Y becomes world pixel 0/0. */
int custom_maps_variable_room_at(int selector, double world_x, double world_y,
                                 int* out_final_room,
                                 int* out_start_x_px,
                                 int* out_start_y_px,
                                 int* out_width_px,
                                 int* out_height_px);
int custom_maps_variable_room_bounds_for_index(int selector, int final_room,
                                               int* out_start_px,
                                               int* out_width_px,
                                               int* out_height_px);
int custom_maps_variable_room_bounds_2d_for_index(int selector, int final_room,
                                                  int* out_start_x_px,
                                                  int* out_start_y_px,
                                                  int* out_width_px,
                                                  int* out_height_px);
int custom_maps_variable_room_count(int selector);
int custom_maps_start_room(int selector);
int custom_maps_uses_room_graph(int selector);

/* Resolves the authored source definition and independent appearance bank for
 * one placed room in the exact map generation pinned by mapgen_init. Returns
 * 1 for an active room_graph instance, 0 for a non-graph map, and -1 for an
 * invalid selector, room, or stale pin. Geometry mirroring is deliberately a
 * separate property and does not affect out_appearance_mirror. */
int custom_maps_pinned_room_definition(int selector, int final_room,
                                        int* out_source_room,
                                        int* out_appearance_mirror);

/* Reports an explicit ambient override owned by one placed graph node. The
 * value is the native 0..9 ambient used by its source room definition; custom
 * particle ambiance is resolved separately by custom_maps_pinned_ambiance. */
int custom_maps_pinned_room_ambient_override(int selector, int final_room,
                                             int* out_ambient);

/* Resolves a traversable graph opening at one cell along a room edge. Sides
 * are left=0, right=1, top=2, bottom=3. The destination offset is translated
 * through the authored opening. Returns 1 for an exit, 0 for a closed edge
 * cell, and -1 for invalid/ambiguous input. */
int custom_maps_room_exit(int selector, int current_room, int side,
                          int edge_offset, int* out_room, int* out_side,
                          int* out_offset, int* out_connection);

/* Resolves an explicit graph edge crossed by a player position. The supplied
 * old position must be inside current_room; the new position may lie just
 * beyond one edge. Returns 1 and the destination room for a traversable
 * opening, 0 when no graph edge was crossed, and -1 for invalid/ambiguous
 * input. Mirrored layouts return 0 because their native horizontal behavior
 * remains authoritative. */
int custom_maps_resolve_room_transition(int selector, int current_room,
                                        double old_x, double old_y,
                                        double new_x, double new_y,
                                        int* out_room,
                                        int* out_connection);

/* Returns the immutable traversal policy for one explicit graph connection.
 * players: 0 both, 1 player 1, 2 player 2, 3 current GO player.
 * focus: 0 only GO changes the focused room, 1 whichever player crosses. */
int custom_maps_room_connection_policy(int selector, int connection,
                                       int* out_players, int* out_focus);

/* Applies room respawn markers after the native floor search. Allowed markers
 * form a per-player whitelist; deny markers remove matching native floor
 * candidates. Coordinates are the player's live world position. */
int custom_maps_adjust_spawn_position(int selector, int final_room,
                                      int player_index,
                                      float* inout_x, float* inout_y);
int custom_maps_player_start_position(int selector, int player_index,
                                      float near_x, float* out_x, float* out_y,
                                      int* out_facing);

/* Convenience one-cell query. It opens a fresh view and maps all errors to 0.
 * Bulk renderer/map-generation code should use the view API above. */
int custom_maps_content_cell_for_selector(int selector,
                                          int source_room,
                                          int x,
                                          int y,
                                          char* out_key,
                                          size_t out_key_size,
                                          char* out_native_glyph);

typedef struct CustomMapContentSheetInfo {
    char key[128];
    char full_path[260];
    char asset_sha256[65];
    int cell_w;
    int cell_h;
    int padding;
    int source_x;
    int source_y;
    int source_w;
    int source_h;
    int sprite_count;
    unsigned int atlas_flags;
} CustomMapContentSheetInfo;

/* Enumerates validated external v2 sheets for graphics-atlas injection. The
 * returned metadata is a copy and becomes stale after a map rescan. */
int custom_maps_content_sheet_count(void);
int custom_maps_content_sheet_get(int index, CustomMapContentSheetInfo* out_info);
int custom_maps_content_sheet_for_key(const char* sheet_key,
                                      char* out_full_path,
                                      size_t out_full_path_size,
                                      char* out_sha256,
                                      size_t out_sha256_size);

/* Immutable pinned package policy for a generated room; no filesystem access.
 * A placed room override wins over its source room and map default. Values are
 * 0 native/default, 1 always respawn, 2 never respawn. Invalid/vanilla
 * selectors return native behavior. */
int custom_maps_opponent_spawn_policy(int selector, int final_room);

/* Returns the immutable custom ambiance selected for a generated room. The
 * catalog remains valid for the current pinned map generation. Returns 1 when
 * selected, 0 for native-only ambience, and -1 for invalid/unpinned input. */
int custom_maps_pinned_ambiance(int selector, int final_room,
                                const MapAmbianceCatalog** out_catalog,
                                uint16_t* out_ambiance_index,
                                int* out_source_room,
                                int* out_mirror_room);

/* Resolves the native-layout sheet for one generated room from its placed-room
 * override, source-room override, defaults.room, then the legacy map-wide
 * tileset.native_layout setting. Returns 1 with a validated 128+ sprite sheet,
 * 0 for vanilla native tile presentation, and -1 for invalid or unpinned input. */
int custom_maps_pinned_native_tileset(int selector, int final_room,
                                      char* out_sheet_key,
                                      size_t out_sheet_key_size,
                                      int* out_sprite_count);

typedef struct CustomMapValidationSummary {
    int final_opponent_spawn[CUSTOM_MAP_MAX_FINAL_ROOMS];
    int opponent_spawn[9]; /* resolved source-room policies */
    int has_eggnogg_color;
    float eggnogg_color[3];
    int format_version;
    int source_room_count;
    int final_room_count;
    int room_graph;
    int graph_start_room;
    int connection_count;
    int layout_bounds_x;
    int layout_bounds_y;
    int layout_width;
    int layout_height;
    int content_tile_count;
    int content_cell_count;
    int particle_definition_count;
    int ambiance_definition_count;
    int ambiance_lane_count;
    char default_sheet_key[128];
    int default_sheet_sprite_count;
    int native_layout;
    /* Conservative per-active-room audit of K, sword, and mine reset spawners.
     * The supported executable has 15 allocatable thing slots after slot zero;
     * two are reserved for players, so rooms containing unsafe K markers may
     * never exceed 13 combined spawners. */
    int max_native_room_spawns;
    int native_room_spawn_limit;
    int native_k_marker_count;
    int has_script;
    size_t script_size;
    int has_entities;
    size_t entity_size;
    char entity_sha256[65];
    uint64_t script_id;
    char script_full_path[260];
    char script_sha256[65];
    int error_count;
    int warning_count;
} CustomMapValidationSummary;

/* Package validation entry point used by tooling/tests. It never commits
 * content or touches engine memory. folder_path is the package directory and
 * its optional direct map.lua is discovered and sandbox-validated for v2. */
int custom_maps_validate_package_text(const char* folder_id,
                                      const char* folder_path,
                                      const char* json_text,
                                      const char* map_text,
                                      CustomMapValidationSummary* out_summary);

/* Called only after the selected map's content bridge has finished binding.
 * Vanilla maps and custom maps without map.lua deactivate any prior script.
 * This does not rescan the filesystem; it uses the registry generation pinned
 * by custom_maps_handle_mapgen_init for the live native room definitions. */
int custom_maps_activate_script_for_selector(int selector,
                                             const MapScriptHost* host,
                                             char* err,
                                             size_t err_cap);
void custom_maps_deactivate_script(void);

#ifdef __cplusplus
}
#endif

/* Resolve a direct declared PNG filename within the pinned package only. */
int custom_maps_pinned_visual_sheet(const char* filename,int sprite,char* key,size_t capacity);

/* Whether a staged session is still referenced by the selected preview,
 * current registry, or engine-pinned map. Safe at main-thread update boundaries. */
int custom_maps_preview_session_in_use(const char* token);
