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
    uint32_t cell_index;
    char sprite_sheet[CONTENT_SHEET_KEY_MAX];
    int sprite_index;
    int layer;
    int flip_x;
    int native_visual_underlay;
    float offset_x;
    float offset_y;
    float scale_x;
    float scale_y;
    float angle_degrees;
    float tint[4];
} ContentTileRender;

typedef struct ContentTileInteraction {
    char key[CONTENT_KEY_MAX];
    uint32_t cell_index;
    int x;
    int y;
    int room_mirrored;
    int collision_mode;
    int force_mode;
    uint32_t force_axes;
    float force_x;
    float force_y;
    float max_speed_x;
    float max_speed_y;
} ContentTileInteraction;

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

/* Returns 1 only when a valid bound cell explicitly requests its already-
 * generated native renderer beneath the custom sprite. This is draw-only
 * metadata: it never changes the engine-owned tile or collision behavior. */
int content_tiles_native_visual_underlay_for_action(const void* tile,
                                                     int mode,
                                                     int x,
                                                     int y);

/* Looks up deterministic interaction metadata at a world-space point. The
 * active tilemap is global, so world coordinates map directly to its visual
 * grid using the engine's nonnegative truncate/floor rule: cell (x,y) covers
 * [x*tile_width,(x+1)*tile_width) by
 * [y*tile_height,(y+1)*tile_height).
 * cell_index/x/y identify the exact contacted cell (rather than merely its
 * shared tile definition) for deterministic map-local behavior dispatch.
 * Returns 1 for any valid bound cell; force_axes may be zero when its behavior
 * is supplied entirely by the native collision glyph. */
int content_tiles_interaction_at_world(float world_x,
                                       float world_y,
                                       int tile_width,
                                       int tile_height,
                                       ContentTileInteraction* out);

/* Coordinate form used by bounded contact-sensor enumeration. It returns the
 * same immutable interaction record as the world-space lookup and rejects
 * negative, out-of-map, unbound, or stale cells. */
int content_tiles_interaction_at_cell(int x,
                                      int y,
                                      ContentTileInteraction* out);

/* Pure velocity helper shared by runtime hooks and tests. SET only replaces
 * explicitly authored axes; optional max speeds clamp their absolute values. */
void content_tiles_apply_interaction_velocity(const ContentTileInteraction* interaction,
                                              float* velocity_x,
                                              float* velocity_y);

int content_tiles_map_active(void);
size_t content_tiles_map_cell_count(void);
size_t content_tiles_map_definition_count(void);

/* Re-resolves definitions after a mod hot reload. Missing definitions become
 * safe native-render fallbacks. Returns the number that remain valid. */
size_t content_tiles_refresh_registry(void);

#ifdef __cplusplus
}
#endif
