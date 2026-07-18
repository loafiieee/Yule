#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../content_bridge.h"
#include "../content_registry.h"
#include "../content_tiles.h"

static int g_failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

typedef struct LookupEntry {
    int source_room;
    int x;
    int y;
    const char* key;
} LookupEntry;

typedef struct LookupState {
    const LookupEntry* entries;
    size_t count;
    int force_error;
    int force_empty_key;
} LookupState;

static ContentTileInput make_tile(const char* id, int sprite_index) {
    ContentTileInput input;
    memset(&input, 0, sizeof(input));
    input.id = id;
    input.name = id;
    input.native_glyph = 'x';
    input.sprite_sheet = "builtin:tiles";
    input.sprite_index = sprite_index;
    input.frame_count = 3;
    input.frame_ticks = 2;
    input.animation_mode = CONTENT_ANIMATION_LOOP;
    input.layer = 1;
    input.flags = CONTENT_TILE_MIRROR_WITH_ROOM;
    input.offset_x = 2.0f;
    input.offset_y = -1.0f;
    input.scale_x = 1.5f;
    input.scale_y = 0.5f;
    input.angle_degrees = 12.0f;
    input.tint[0] = 0.2f;
    input.tint[1] = 0.4f;
    input.tint[2] = 0.6f;
    input.tint[3] = 0.8f;
    input.tint_provided = 1;
    return input;
}

static int register_tiles(void) {
    ContentRegistryTx* tx;
    ContentTileInput center = make_tile("center", 10);
    ContentTileInput outer1 = make_tile("outer1", 20);
    ContentTileInput outer2 = make_tile("outer2", 30);
    ContentTileInput animated = make_tile("animated", 40);
    char err[256];
    tx = content_registry_begin("demo", err, sizeof(err));
    if (!tx) return 0;
    if (!content_registry_tx_register_tile(tx, &center, err, sizeof(err)) ||
        !content_registry_tx_register_tile(tx, &outer1, err, sizeof(err)) ||
        !content_registry_tx_register_tile(tx, &outer2, err, sizeof(err)) ||
        !content_registry_tx_register_tile(tx, &animated, err, sizeof(err))) {
        content_registry_abort(tx);
        return 0;
    }
    if (!content_registry_commit(tx, err, sizeof(err))) {
        content_registry_abort(tx);
        return 0;
    }
    return 1;
}

static int lookup_cell(void* user,
                       int source_room,
                       int x,
                       int y,
                       char* out_key,
                       size_t out_key_size) {
    LookupState* state = (LookupState*)user;
    size_t i;
    if (!state || state->force_error) return -1;
    for (i = 0; i < state->count; i++) {
        const LookupEntry* entry = &state->entries[i];
        if (entry->source_room == source_room && entry->x == x && entry->y == y) {
            if (state->force_empty_key) {
                if (out_key_size) out_key[0] = '\0';
            } else {
                snprintf(out_key, out_key_size, "%s", entry->key);
            }
            return 1;
        }
    }
    if (out_key_size) out_key[0] = '\0';
    return 0;
}

static int render_at(unsigned char* tilemap,
                     int width,
                     int x,
                     int y,
                     ContentTileRender* render) {
    return content_tiles_render_for_action(
        tilemap + ((size_t)y * (size_t)width + (size_t)x) * 4u,
        2, x, y, 0, render);
}

static void test_layout_mapping_and_fail_closed_binding(void) {
    enum { WIDTH = (3 * 2 - 1) * CONTENT_BRIDGE_ROOM_WIDTH };
    static const LookupEntry entries[] = {
        { 0, 2, 3, "demo:center" },
        { 1, 0, 0, "demo:outer1" },
        { 2, 32, 11, "demo:outer2" },
    };
    unsigned char tilemap[WIDTH * CONTENT_BRIDGE_ROOM_HEIGHT * 4];
    LookupState lookup = { entries, sizeof(entries) / sizeof(entries[0]), 0, 0 };
    ContentBridgeBindSummary summary;
    ContentTileRender render;
    char err[256];
    int room;
    int local_x;

    memset(tilemap, 0, sizeof(tilemap));
    CHECK(content_bridge_map_source_cell(3, 0, 2, 0, &room, &local_x));
    CHECK(room == 2 && local_x == 2);
    CHECK(!content_bridge_map_source_cell(3, 0, 2, 1, &room, &local_x));
    CHECK(content_bridge_map_source_cell(3, 1, 0, 0, &room, &local_x));
    CHECK(room == 1 && local_x == 0);
    CHECK(content_bridge_map_source_cell(3, 1, 0, 1, &room, &local_x));
    CHECK(room == 3 && local_x == 32);
    CHECK(content_bridge_map_source_cell(3, 2, 32, 1, &room, &local_x));
    CHECK(room == 4 && local_x == 0);

    CHECK(content_bridge_bind_layout(tilemap, WIDTH,
                                     CONTENT_BRIDGE_ROOM_HEIGHT, 3,
                                     lookup_cell, &lookup, &summary,
                                     err, sizeof(err)));
    CHECK(summary.metadata_active == 1);
    CHECK(summary.source_content_cells == 3);
    CHECK(summary.bound_content_cells == 5);
    CHECK(summary.unique_definitions == 3);
    CHECK(render_at(tilemap, WIDTH, 68, 3, &render));
    CHECK(strcmp(render.key, "demo:center") == 0 && render.flip_x == 0);
    CHECK(render_at(tilemap, WIDTH, 33, 0, &render));
    CHECK(strcmp(render.key, "demo:outer1") == 0 && render.flip_x == 0);
    CHECK(render_at(tilemap, WIDTH, 131, 0, &render));
    CHECK(strcmp(render.key, "demo:outer1") == 0 && render.flip_x == 1);
    CHECK(render_at(tilemap, WIDTH, 32, 11, &render));
    CHECK(strcmp(render.key, "demo:outer2") == 0 && render.flip_x == 0);
    CHECK(render_at(tilemap, WIDTH, 132, 11, &render));
    CHECK(strcmp(render.key, "demo:outer2") == 0 && render.flip_x == 1);
    CHECK(!render_at(tilemap, WIDTH, 69, 3, &render));

    CHECK(content_bridge_bind_selector(tilemap, WIDTH,
                                       CONTENT_BRIDGE_ROOM_HEIGHT, 0,
                                       &summary, err, sizeof(err)));
    CHECK(summary.selector == 0 && summary.metadata_active == 0);
    CHECK(!content_tiles_map_active());
    CHECK(!content_bridge_bind_selector(tilemap, WIDTH,
                                        CONTENT_BRIDGE_ROOM_HEIGHT, -1,
                                        &summary, err, sizeof(err)));
    CHECK(!content_tiles_map_active());

    CHECK(!content_bridge_bind_layout(tilemap, WIDTH - 1,
                                      CONTENT_BRIDGE_ROOM_HEIGHT, 3,
                                      lookup_cell, &lookup, &summary,
                                      err, sizeof(err)));
    CHECK(!content_tiles_map_active());

    lookup.force_error = 1;
    CHECK(!content_bridge_bind_layout(tilemap, WIDTH,
                                      CONTENT_BRIDGE_ROOM_HEIGHT, 3,
                                      lookup_cell, &lookup, &summary,
                                      err, sizeof(err)));
    CHECK(!content_tiles_map_active());
    lookup.force_error = 0;
    lookup.force_empty_key = 1;
    CHECK(!content_bridge_bind_layout(tilemap, WIDTH,
                                      CONTENT_BRIDGE_ROOM_HEIGHT, 3,
                                      lookup_cell, &lookup, &summary,
                                      err, sizeof(err)));
    CHECK(!content_tiles_map_active());
}

#define STATE_SCALE_X_OFFSET 0x18u
#define STATE_SCALE_Y_OFFSET 0x20u
#define STATE_ANGLE_OFFSET   0x28u
#define STATE_RGBA_OFFSET    0x50u

typedef struct DrawState {
    unsigned char turtle[CONTENT_BRIDGE_TURTLE_STATE_SIZE];
    int resolve_calls;
    int resolve_result;
    int resolved_index;
    int sprite_get_calls;
    int sprite_get_result;
    int operation;
    int fail_operation;
    double trans_x;
    double trans_y;
    double angle;
    double scale_x;
    double scale_y;
    float rgba[4];
    int plot_calls;
    int plot_flip;
    int plot_layer;
    void* plot_sprite;
    int sprite_token;
} DrawState;

static void write_double(unsigned char* bytes, size_t offset, double value) {
    memcpy(bytes + offset, &value, sizeof(value));
}

static void write_float(unsigned char* bytes, size_t offset, float value) {
    memcpy(bytes + offset, &value, sizeof(value));
}

static int draw_step(DrawState* state) {
    state->operation++;
    return state->operation != state->fail_operation;
}

static int draw_resolve(void* user,
                        const char* sheet,
                        int index,
                        int* out_sprite_id) {
    DrawState* state = (DrawState*)user;
    state->resolve_calls++;
    state->resolved_index = index;
    CHECK(strcmp(sheet, "builtin:tiles") == 0);
    if (!state->resolve_result) return 0;
    *out_sprite_id = 777;
    return 1;
}

static void* draw_sprite_get(void* user, int sprite_id) {
    DrawState* state = (DrawState*)user;
    state->sprite_get_calls++;
    CHECK(sprite_id == 777);
    return state->sprite_get_result ? &state->sprite_token : NULL;
}

static int draw_trans(void* user, double x, double y) {
    DrawState* state = (DrawState*)user;
    state->trans_x = x;
    state->trans_y = y;
    write_double(state->turtle, 0, x + 100.0);
    return draw_step(state);
}

static int draw_angle(void* user, double value) {
    DrawState* state = (DrawState*)user;
    state->angle = value;
    write_double(state->turtle, STATE_ANGLE_OFFSET, value);
    return draw_step(state);
}

static int draw_scalex(void* user, double value) {
    DrawState* state = (DrawState*)user;
    state->scale_x = value;
    write_double(state->turtle, STATE_SCALE_X_OFFSET, value);
    return draw_step(state);
}

static int draw_scaley(void* user, double value) {
    DrawState* state = (DrawState*)user;
    state->scale_y = value;
    write_double(state->turtle, STATE_SCALE_Y_OFFSET, value);
    return draw_step(state);
}

static int draw_rgba(void* user, float r, float g, float b, float a) {
    DrawState* state = (DrawState*)user;
    int i;
    state->rgba[0] = r;
    state->rgba[1] = g;
    state->rgba[2] = b;
    state->rgba[3] = a;
    for (i = 0; i < 4; i++) {
        write_float(state->turtle, STATE_RGBA_OFFSET + (size_t)i * sizeof(float),
                    state->rgba[i]);
    }
    return draw_step(state);
}

static int draw_plot(void* user, void* sprite, int flip_x, int layer) {
    DrawState* state = (DrawState*)user;
    state->plot_calls++;
    state->plot_sprite = sprite;
    state->plot_flip = flip_x;
    state->plot_layer = layer;
    return draw_step(state);
}

static void init_draw_state(DrawState* state) {
    int i;
    memset(state, 0, sizeof(*state));
    memset(state->turtle, 0xa5, sizeof(state->turtle));
    state->resolve_result = 1;
    state->sprite_get_result = 1;
    write_double(state->turtle, STATE_SCALE_X_OFFSET, 2.0);
    write_double(state->turtle, STATE_SCALE_Y_OFFSET, 3.0);
    write_double(state->turtle, STATE_ANGLE_OFFSET, 15.0);
    for (i = 0; i < 4; i++) write_float(state->turtle, STATE_RGBA_OFFSET + i * 4u, 0.5f);
}

static ContentBridgeDrawOps draw_ops(DrawState* state) {
    ContentBridgeDrawOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.user = state;
    ops.turtle_state = state->turtle;
    ops.turtle_state_size = sizeof(state->turtle);
    ops.resolve_sprite = draw_resolve;
    ops.sprite_get = draw_sprite_get;
    ops.turtle_trans = draw_trans;
    ops.turtle_set_angle = draw_angle;
    ops.turtle_set_scalex = draw_scalex;
    ops.turtle_set_scaley = draw_scaley;
    ops.turtle_set_rgba = draw_rgba;
    ops.sprite_batch_plot = draw_plot;
    return ops;
}

static void test_draw_and_native_fallback(void) {
    unsigned char tilemap[4] = { 0, 0, 0, 0 };
    unsigned char original[CONTENT_BRIDGE_TURTLE_STATE_SIZE];
    DrawState state;
    ContentBridgeDrawOps ops;
    char err[256];

    CHECK(content_tiles_map_begin(tilemap, 1, 1, err, sizeof(err)));
    CHECK(content_tiles_map_set(0, 0, "demo:animated", 1, err, sizeof(err)));
    init_draw_state(&state);
    ops = draw_ops(&state);
    memcpy(original, state.turtle, sizeof(original));
    CHECK(content_bridge_draw_action(tilemap, 2, 0, 0, 4, &ops));
    CHECK(state.resolve_calls == 1 && state.resolved_index == 42);
    CHECK(state.sprite_get_calls == 1 && state.plot_calls == 1);
    CHECK(state.plot_sprite == &state.sprite_token);
    CHECK(state.plot_flip == 1 && state.plot_layer == 1);
    CHECK(fabs(state.trans_x - 2.0) < 0.000001 &&
          fabs(state.trans_y + 1.0) < 0.000001);
    CHECK(fabs(state.angle - 27.0) < 0.000001);
    CHECK(fabs(state.scale_x - 3.0) < 0.000001 &&
          fabs(state.scale_y - 1.5) < 0.000001);
    CHECK(fabsf(state.rgba[0] - 0.1f) < 0.00001f &&
          fabsf(state.rgba[1] - 0.2f) < 0.00001f &&
          fabsf(state.rgba[2] - 0.3f) < 0.00001f &&
          fabsf(state.rgba[3] - 0.4f) < 0.00001f);
    CHECK(memcmp(state.turtle, original, sizeof(original)) == 0);

    init_draw_state(&state);
    ops = draw_ops(&state);
    state.resolve_result = 0;
    memcpy(original, state.turtle, sizeof(original));
    CHECK(!content_bridge_draw_action(tilemap, 2, 0, 0, 0, &ops));
    CHECK(state.operation == 0 && state.plot_calls == 0);
    CHECK(memcmp(state.turtle, original, sizeof(original)) == 0);

    init_draw_state(&state);
    ops = draw_ops(&state);
    state.sprite_get_result = 0;
    CHECK(!content_bridge_draw_action(tilemap, 2, 0, 0, 0, &ops));
    CHECK(state.operation == 0 && state.plot_calls == 0);

    init_draw_state(&state);
    ops = draw_ops(&state);
    state.fail_operation = 3;
    memcpy(original, state.turtle, sizeof(original));
    CHECK(!content_bridge_draw_action(tilemap, 2, 0, 0, 0, &ops));
    CHECK(state.operation == 3 && state.plot_calls == 0);
    CHECK(memcmp(state.turtle, original, sizeof(original)) == 0);

    init_draw_state(&state);
    ops = draw_ops(&state);
    state.fail_operation = 6;
    memcpy(original, state.turtle, sizeof(original));
    CHECK(!content_bridge_draw_action(tilemap, 2, 0, 0, 0, &ops));
    CHECK(state.plot_calls == 1);
    CHECK(memcmp(state.turtle, original, sizeof(original)) == 0);

    init_draw_state(&state);
    ops = draw_ops(&state);
    ops.turtle_set_rgba = NULL;
    CHECK(!content_bridge_draw_action(tilemap, 2, 0, 0, 0, &ops));
    CHECK(state.resolve_calls == 0);
    CHECK(!content_bridge_draw_action(tilemap, 1, 0, 0, 0, &ops));

    init_draw_state(&state);
    ops = draw_ops(&state);
    write_double(state.turtle, STATE_SCALE_X_OFFSET, NAN);
    memcpy(original, state.turtle, sizeof(original));
    CHECK(!content_bridge_draw_action(tilemap, 2, 0, 0, 0, &ops));
    CHECK(state.operation == 0 && state.plot_calls == 0);
    CHECK(memcmp(state.turtle, original, sizeof(original)) == 0);
}

int main(void) {
    content_tiles_shutdown();
    content_registry_shutdown();
    CHECK(register_tiles());
    test_layout_mapping_and_fail_closed_binding();
    test_draw_and_native_fallback();
    content_tiles_shutdown();
    content_registry_shutdown();
    if (g_failures) {
        fprintf(stderr, "%d content bridge test(s) failed\n", g_failures);
        return 1;
    }
    puts("content_bridge_test: all checks passed");
    return 0;
}
