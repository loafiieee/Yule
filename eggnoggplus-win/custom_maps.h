#pragma once

#include <stddef.h>
#include <stdint.h>

#include "map_script.h"

#ifdef __cplusplus
extern "C" {
#endif

void custom_maps_init(void);
void custom_maps_shutdown(void);
/* Visual override from the installed map generation; no filesystem polling.
 * Returns zero without touching output when the selected map uses native color. */
int custom_maps_pinned_eggnogg_color(int selector, float out_rgb[3]);
void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void));

/*
 * Validate and install a V1 package carried by a local preview launch. The
 * package is process-local, never written to maps/, and excluded from online
 * manifests. On success out_selector receives its immediately usable selector.
 */
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

typedef struct CustomMapValidationSummary {
    int has_eggnogg_color;
    float eggnogg_color[3];
    int format_version;
    int source_room_count;
    int content_tile_count;
    int content_cell_count;
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
