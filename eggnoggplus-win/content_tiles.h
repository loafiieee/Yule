#pragma once

#include <stddef.h>
#include <stdint.h>

#include "content_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ContentTileRender {
    char owner[CONTENT_OWNER_MAX];
    char key[CONTENT_KEY_MAX];
    char sprite_sheet[CONTENT_SHEET_KEY_MAX];
    int sprite_index;
    int layer;
    int flip_x;
    float offset_x;
    float offset_y;
    float scale_x;
    float scale_y;
    float angle_degrees;
    float tint[4];
} ContentTileRender;

void content_tiles_init(void);
void content_tiles_shutdown(void);

/* Starts metadata for the engine's current 4-byte-per-cell tilemap. Existing
 * metadata is discarded. The tilemap itself is never owned or modified. */
int content_tiles_map_begin(void* tilemap_base,
                            int width,
                            int height,
                            char* err,
                            size_t err_cap);
void content_tiles_map_end(void);

/* Bind a generated engine cell to a registered tile definition. room_mirrored
 * controls optional visual mirroring but never changes native behavior. */
int content_tiles_map_set(int x,
                          int y,
                          const char* qualified_key,
                          int room_mirrored,
                          char* err,
                          size_t err_cap);

/* Removes custom metadata from one active-map cell. This does not alter the
 * engine-owned tile or its native behavior. */
int content_tiles_map_clear(int x, int y, char* err, size_t err_cap);

/* Returns 1 only for a custom cell in native draw mode (mode == 2). Passing a
 * negative x or y skips that coordinate cross-check; the tile pointer is still
 * required to be an exactly aligned cell in the active map. The caller should
 * render out and return handled to suppress the vanilla sprite. */
int content_tiles_render_for_action(const void* tile,
                                    int mode,
                                    int x,
                                    int y,
                                    uint64_t deterministic_tick,
                                    ContentTileRender* out);

int content_tiles_map_active(void);
size_t content_tiles_map_cell_count(void);
size_t content_tiles_map_definition_count(void);

/* Re-resolves definitions after a mod hot reload. Missing definitions become
 * safe native-render fallbacks. Returns the number that remain valid. */
size_t content_tiles_refresh_registry(void);

#ifdef __cplusplus
}
#endif
