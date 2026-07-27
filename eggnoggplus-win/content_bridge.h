#pragma once

#include <stddef.h>
#include <stdint.h>

#include "content_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTENT_BRIDGE_ROOM_WIDTH 33
#define CONTENT_BRIDGE_ROOM_HEIGHT 12
#define CONTENT_BRIDGE_MAX_SOURCE_ROOMS 9
#define CONTENT_BRIDGE_TURTLE_STATE_SIZE 96u
#define CONTENT_BRIDGE_VISUAL_OFFSET_LIMIT 4096.0f

typedef struct ContentBridgeBindSummary {
    int selector;
    int source_room_count;
    int metadata_active;
    size_t source_content_cells;
    size_t bound_content_cells;
    size_t unique_definitions;
    uint64_t custom_maps_generation;
} ContentBridgeBindSummary;

/* Return 1 for a content cell, 0 for a native-only cell, and -1 on failure. */
typedef int (*ContentBridgeCellLookupFn)(void* user,
                                         int source_room,
                                         int x,
                                         int y,
                                         char* out_key,
                                         size_t out_key_size);

/* Pure layout mapping used by the binder and tests. source room zero maps to
 * the center and is never mirrored. Outer source rooms map left unchanged and
 * right with local x reversed. */
int content_bridge_map_source_cell(int source_room_count,
                                   int source_room,
                                   int source_x,
                                   int mirrored,
                                   int* out_final_room,
                                   int* out_local_x);

/* Starts and completely populates content_tiles metadata for an already-built
 * native tilemap. Any invalid dimensions, stale lookup, missing definition, or
 * allocation failure tears the metadata back down, preserving native drawing. */
int content_bridge_bind_layout(void* tilemap_base,
                               int tilemap_width,
                               int tilemap_height,
                               int source_room_count,
                               ContentBridgeCellLookupFn lookup,
                               void* lookup_user,
                               ContentBridgeBindSummary* out_summary,
                               char* err,
                               size_t err_cap);

/* Production map-package wrapper. It performs one custom-map reload poll, then
 * binds through a generation-stable view. Vanilla selectors intentionally end
 * any prior content metadata and succeed with metadata_active == 0. */
int content_bridge_bind_selector(void* tilemap_base,
                                 int tilemap_width,
                                 int tilemap_height,
                                 int selector,
                                 ContentBridgeBindSummary* out_summary,
                                 char* err,
                                 size_t err_cap);

typedef int (*ContentBridgeResolveSpriteFn)(void* user,
                                            const char* sheet_key,
                                            int sheet_index,
                                            int* out_sprite_id);
typedef void* (*ContentBridgeSpriteGetFn)(void* user, int sprite_id);
typedef int (*ContentBridgeTurtleTransFn)(void* user, double x, double y);
typedef int (*ContentBridgeTurtleSetScalarFn)(void* user, double value);
typedef int (*ContentBridgeTurtleSetRgbaFn)(void* user,
                                            float r,
                                            float g,
                                            float b,
                                            float a);
typedef int (*ContentBridgeSpriteBatchPlotFn)(void* user,
                                              void* sprite,
                                              int flip_x,
                                              int layer);
typedef struct ContentBridgeVisualOverride {
    int sprite_index;
    float offset_x;                  /* additive render pixels */
    float offset_y;
} ContentBridgeVisualOverride;

typedef int (*ContentBridgeVisualOverrideFn)(
    void* user,
    uint32_t cell_index,
    const char* tile_key,
    int current_sprite_index,
    ContentBridgeVisualOverride* out_override);

typedef struct ContentBridgeDrawOps {
    void* user;
    void* turtle_state;
    size_t turtle_state_size;
    ContentBridgeResolveSpriteFn resolve_sprite;
    ContentBridgeSpriteGetFn sprite_get;
    ContentBridgeTurtleTransFn turtle_trans;
    ContentBridgeTurtleSetScalarFn turtle_set_angle;
    ContentBridgeTurtleSetScalarFn turtle_set_scalex;
    ContentBridgeTurtleSetScalarFn turtle_set_scaley;
    ContentBridgeTurtleSetRgbaFn turtle_set_rgba;
    ContentBridgeSpriteBatchPlotFn sprite_batch_plot;
    /* Optional deterministic per-cell visual override. Returning zero keeps
     * the definition/animation-selected sprite and transform. Offsets are
     * additive destination pixels and never move collision/native underlay. */
    ContentBridgeVisualOverrideFn visual_override;
} ContentBridgeDrawOps;

/* Resolves and draws one custom tile from native tile_action_ex draw mode.
 * deterministic_tick must be simulation state (the engine game tick), never
 * wall/render time. Returns 1 only after a sprite was queued; every other path
 * returns 0 so tile_action_ex retains its vanilla fallback. */
int content_bridge_draw_action(const void* tile,
                               int mode,
                               int x,
                               int y,
                               uint64_t deterministic_tick,
                               const ContentBridgeDrawOps* ops);

#ifdef __cplusplus
}
#endif
