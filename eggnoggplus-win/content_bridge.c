#include <math.h>
#include <stdio.h>
#include <string.h>

#include "content_bridge.h"
#include "content_tiles.h"
#include "custom_maps.h"

#define TURTLE_SCALE_X_OFFSET 0x18u
#define TURTLE_SCALE_Y_OFFSET 0x20u
#define TURTLE_ANGLE_OFFSET   0x28u
#define TURTLE_RGBA_OFFSET    0x50u

typedef struct CustomMapLookupContext {
    CustomMapContentView view;
} CustomMapLookupContext;

static void set_err(char* err, size_t err_cap, const char* text) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", text ? text : "content bridge error");
    err[err_cap - 1] = '\0';
}

static void clear_summary(ContentBridgeBindSummary* summary) {
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
    summary->selector = -1;
}

int content_bridge_map_source_cell(int source_room_count,
                                   int source_room,
                                   int source_x,
                                   int mirrored,
                                   int* out_final_room,
                                   int* out_local_x) {
    int center_room;
    if (!out_final_room || !out_local_x ||
        source_room_count < 1 ||
        source_room_count > CONTENT_BRIDGE_MAX_SOURCE_ROOMS ||
        source_room < 0 || source_room >= source_room_count ||
        source_x < 0 || source_x >= CONTENT_BRIDGE_ROOM_WIDTH ||
        (mirrored != 0 && mirrored != 1) ||
        (source_room == 0 && mirrored)) {
        return 0;
    }
    center_room = source_room_count - 1;
    if (source_room == 0) {
        *out_final_room = center_room;
        *out_local_x = source_x;
    } else if (mirrored) {
        *out_final_room = center_room + source_room;
        *out_local_x = (CONTENT_BRIDGE_ROOM_WIDTH - 1) - source_x;
    } else {
        *out_final_room = center_room - source_room;
        *out_local_x = source_x;
    }
    return 1;
}

static int bind_one_destination(int source_room_count,
                                int source_room,
                                int source_x,
                                int y,
                                int mirrored,
                                const char* key,
                                char* err,
                                size_t err_cap) {
    int final_room;
    int local_x;
    int final_x;
    if (!content_bridge_map_source_cell(source_room_count, source_room,
                                        source_x, mirrored,
                                        &final_room, &local_x)) {
        set_err(err, err_cap, "invalid mirrored source-room mapping");
        return 0;
    }
    final_x = final_room * CONTENT_BRIDGE_ROOM_WIDTH + local_x;
    return content_tiles_map_set(final_x, y, key, mirrored, err, err_cap);
}

int content_bridge_bind_layout(void* tilemap_base,
                               int tilemap_width,
                               int tilemap_height,
                               int source_room_count,
                               ContentBridgeCellLookupFn lookup,
                               void* lookup_user,
                               ContentBridgeBindSummary* out_summary,
                               char* err,
                               size_t err_cap) {
    int expected_width;
    int source_room;
    int y;
    int x;
    ContentBridgeBindSummary summary;
    clear_summary(&summary);
    summary.source_room_count = source_room_count;
    if (source_room_count < 1 ||
        source_room_count > CONTENT_BRIDGE_MAX_SOURCE_ROOMS) {
        set_err(err, err_cap, "source room count is outside the supported 1..9 range");
        goto fail_before_begin;
    }
    expected_width = (source_room_count * 2 - 1) * CONTENT_BRIDGE_ROOM_WIDTH;
    if (!tilemap_base || tilemap_width != expected_width ||
        tilemap_height != CONTENT_BRIDGE_ROOM_HEIGHT) {
        set_err(err, err_cap, "live tilemap dimensions do not match the mirrored source-room layout");
        goto fail_before_begin;
    }
    if (!lookup) {
        set_err(err, err_cap, "content cell lookup callback is required");
        goto fail_before_begin;
    }
    if (!content_tiles_map_begin(tilemap_base, tilemap_width, tilemap_height,
                                 err, err_cap)) {
        goto fail_before_begin;
    }
    summary.metadata_active = 1;

    for (source_room = 0; source_room < source_room_count; source_room++) {
        for (y = 0; y < CONTENT_BRIDGE_ROOM_HEIGHT; y++) {
            for (x = 0; x < CONTENT_BRIDGE_ROOM_WIDTH; x++) {
                char key[CONTENT_KEY_MAX];
                int lookup_result;
                key[0] = '\0';
                lookup_result = lookup(lookup_user, source_room, x, y,
                                       key, sizeof(key));
                if (lookup_result < 0) {
                    set_err(err, err_cap, "content map view changed or failed during binding");
                    goto fail_after_begin;
                }
                if (lookup_result == 0) continue;
                if (!key[0]) {
                    set_err(err, err_cap, "content cell lookup returned an empty key");
                    goto fail_after_begin;
                }
                summary.source_content_cells++;
                if (!bind_one_destination(source_room_count, source_room, x, y,
                                          0, key, err, err_cap)) {
                    goto fail_after_begin;
                }
                summary.bound_content_cells++;
                if (source_room != 0) {
                    if (!bind_one_destination(source_room_count, source_room,
                                              x, y, 1, key, err, err_cap)) {
                        goto fail_after_begin;
                    }
                    summary.bound_content_cells++;
                }
            }
        }
    }
    summary.unique_definitions = content_tiles_map_definition_count();
    if (out_summary) *out_summary = summary;
    return 1;

fail_after_begin:
    summary.metadata_active = 0;
fail_before_begin:
    content_tiles_map_end();
    if (out_summary) *out_summary = summary;
    return 0;
}

static int custom_map_lookup(void* user,
                             int source_room,
                             int x,
                             int y,
                             char* out_key,
                             size_t out_key_size) {
    CustomMapLookupContext* context = (CustomMapLookupContext*)user;
    char native_glyph = '\0';
    if (!context) return -1;
    return custom_maps_content_view_cell(&context->view, source_room, x, y,
                                         out_key, out_key_size,
                                         &native_glyph);
}

int content_bridge_bind_selector(void* tilemap_base,
                                 int tilemap_width,
                                 int tilemap_height,
                                 int selector,
                                 ContentBridgeBindSummary* out_summary,
                                 char* err,
                                 size_t err_cap) {
    CustomMapLookupContext context;
    ContentBridgeBindSummary summary;
    int open_result;
    int result;
    clear_summary(&summary);
    summary.selector = selector;
    memset(&context, 0, sizeof(context));
    open_result = custom_maps_content_view_open(selector, &context.view);
    if (open_result == 0) {
        content_tiles_map_end();
        if (out_summary) *out_summary = summary;
        return 1;
    }
    if (open_result < 0) {
        content_tiles_map_end();
        set_err(err, err_cap, "map selector is not present in the current custom-map registry");
        if (out_summary) *out_summary = summary;
        return 0;
    }
    if (context.view.content_tile_count == 0) {
        content_tiles_map_end();
        summary.source_room_count = context.view.source_room_count;
        summary.custom_maps_generation = context.view.generation;
        if (out_summary) *out_summary = summary;
        return 1;
    }
    result = content_bridge_bind_layout(tilemap_base, tilemap_width,
                                        tilemap_height,
                                        context.view.source_room_count,
                                        custom_map_lookup, &context,
                                        &summary, err, err_cap);
    summary.selector = selector;
    summary.custom_maps_generation = context.view.generation;
    if (result && summary.bound_content_cells == 0) {
        content_tiles_map_end();
        summary.metadata_active = 0;
        summary.unique_definitions = 0;
    }
    if (out_summary) *out_summary = summary;
    return result;
}

static double state_double(const unsigned char* state, size_t offset) {
    double value;
    memcpy(&value, state + offset, sizeof(value));
    return value;
}

static float state_float(const unsigned char* state, size_t offset) {
    float value;
    memcpy(&value, state + offset, sizeof(value));
    return value;
}

static int draw_visual(const ContentTileRender* input,const ContentBridgeDrawOps* ops,int tile_override) {
    ContentTileRender render;
    if(!input) return 0;
    render=*input;
    unsigned char saved_state[CONTENT_BRIDGE_TURTLE_STATE_SIZE];
    double current_scale_x;
    double current_scale_y;
    double current_angle;
    double next_scale_x;
    double next_scale_y;
    double next_angle;
    float next_rgba[4];
    int sprite_id;
    void* sprite;
    int i;

    if(!memchr(render.sprite_sheet,0,sizeof(render.sprite_sheet)) || !render.sprite_sheet[0] || render.sprite_index<0 ||
       !isfinite(render.offset_x) || !isfinite(render.offset_y) || !isfinite(render.angle_degrees) ||
       !isfinite(render.scale_x) || !isfinite(render.scale_y) || !render.scale_x || !render.scale_y) return 0;
    for(i=0;i<4;i++) if(!isfinite(render.tint[i]) || render.tint[i]<0 || render.tint[i]>1) return 0;
    if (!ops || !ops->turtle_state ||
        ops->turtle_state_size < CONTENT_BRIDGE_TURTLE_STATE_SIZE ||
        !ops->resolve_sprite || !ops->sprite_get || !ops->turtle_trans ||
        !ops->turtle_set_angle || !ops->turtle_set_scalex ||
        !ops->turtle_set_scaley || !ops->turtle_set_rgba ||
        !ops->sprite_batch_plot || render.layer < 0 || render.layer > 1) {
        return 0;
    }
    if (tile_override && ops->visual_override) {
        ContentBridgeVisualOverride override;
        override.sprite_index = render.sprite_index;
        override.offset_x = 0.0f;
        override.offset_y = 0.0f;
        if (ops->visual_override(ops->user, render.cell_index, render.key,
                                 render.sprite_index, &override)) {
            if (override.sprite_index < 0 || !isfinite(override.offset_x) ||
                !isfinite(override.offset_y) ||
                fabsf(override.offset_x) > CONTENT_BRIDGE_VISUAL_OFFSET_LIMIT ||
                fabsf(override.offset_y) > CONTENT_BRIDGE_VISUAL_OFFSET_LIMIT) {
                return 0;
            }
            render.sprite_index = override.sprite_index;
            render.offset_x += override.offset_x;
            render.offset_y += override.offset_y;
            if (!isfinite(render.offset_x) || !isfinite(render.offset_y)) return 0;
        }
    }
    sprite_id = -1;
    if (!ops->resolve_sprite(ops->user, render.sprite_sheet,
                             render.sprite_index, &sprite_id) ||
        sprite_id < 0) {
        return 0;
    }
    sprite = ops->sprite_get(ops->user, sprite_id);
    if (!sprite) return 0;

    memcpy(saved_state, ops->turtle_state, sizeof(saved_state));
    current_scale_x = state_double(saved_state, TURTLE_SCALE_X_OFFSET);
    current_scale_y = state_double(saved_state, TURTLE_SCALE_Y_OFFSET);
    current_angle = state_double(saved_state, TURTLE_ANGLE_OFFSET);
    next_scale_x = current_scale_x * (double)render.scale_x;
    next_scale_y = current_scale_y * (double)render.scale_y;
    next_angle = current_angle + (double)render.angle_degrees;
    if (!isfinite(current_scale_x) || !isfinite(current_scale_y) ||
        !isfinite(current_angle) || !isfinite(next_scale_x) ||
        !isfinite(next_scale_y) || !isfinite(next_angle)) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        float current = state_float(saved_state,
                                    TURTLE_RGBA_OFFSET + (size_t)i * sizeof(float));
        next_rgba[i] = current * render.tint[i];
        if (!isfinite(current) || !isfinite(next_rgba[i])) return 0;
    }

    if (!ops->turtle_trans(ops->user, (double)render.offset_x,
                           (double)render.offset_y) ||
        !ops->turtle_set_angle(ops->user, next_angle) ||
        !ops->turtle_set_scalex(ops->user, next_scale_x) ||
        !ops->turtle_set_scaley(ops->user, next_scale_y) ||
        !ops->turtle_set_rgba(ops->user, next_rgba[0], next_rgba[1],
                              next_rgba[2], next_rgba[3]) ||
        !ops->sprite_batch_plot(ops->user, sprite,
                                render.flip_x ? 1 : 0, render.layer)) {
        memcpy(ops->turtle_state, saved_state, sizeof(saved_state));
        return 0;
    }
    memcpy(ops->turtle_state, saved_state, sizeof(saved_state));
    return 1;
}

int content_bridge_draw_action(const void* tile,int mode,int x,int y,uint64_t tick,const ContentBridgeDrawOps* ops) {
    ContentTileRender render;
    return content_tiles_render_for_action(tile,mode,x,y,tick,&render) && draw_visual(&render,ops,1);
}
int content_bridge_draw_visual(const struct ContentTileRender* render,const ContentBridgeDrawOps* ops) {
    return draw_visual(render,ops,0);
}
