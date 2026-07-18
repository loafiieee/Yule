#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void custom_maps_init(void);
void custom_maps_shutdown(void);
void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void));

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
} CustomMapContentView;

/* Opens a generation-stable metadata view without retaining registry pointers.
 * Returns 1 for a custom selector, 0 for a vanilla selector, and -1 when the
 * selector is invalid. A view is intended for one immediate map-generation
 * pass; view_cell returns -1 if a reload made it stale. */
int custom_maps_content_view_open(int selector, CustomMapContentView* out_view);

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
    int format_version;
    int source_room_count;
    int content_tile_count;
    int content_cell_count;
    int error_count;
    int warning_count;
} CustomMapValidationSummary;

/* Parser-only validation entry point used by tooling/tests. It never commits
 * content or touches engine memory. folder_path is the package directory. */
int custom_maps_validate_package_text(const char* folder_id,
                                      const char* folder_path,
                                      const char* json_text,
                                      const char* map_text,
                                      CustomMapValidationSummary* out_summary);

#ifdef __cplusplus
}
#endif
