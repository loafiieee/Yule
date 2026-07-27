#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../content_registry.h"
#include "../content_tiles.h"

static int g_failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

static ContentTileInput animated_tile(int first_sprite) {
    ContentTileInput input;
    memset(&input, 0, sizeof(input));
    input.id = "animated";
    input.name = "Animated Tile";
    input.native_glyph = 'x';
    input.sprite_sheet = "builtin:tiles";
    input.sprite_index = first_sprite;
    input.frame_count = 3;
    input.frame_ticks = 2;
    input.animation_mode = CONTENT_ANIMATION_PING_PONG;
    input.flags = CONTENT_TILE_MIRROR_WITH_ROOM |
                  CONTENT_TILE_NATIVE_VISUAL_UNDERLAY;
    input.force_mode = CONTENT_FORCE_ADD;
    input.force_axes = CONTENT_FORCE_AXIS_X | CONTENT_FORCE_AXIS_Y;
    input.force_x = 0.5f;
    input.force_y = -1.0f;
    input.max_speed_x = 2.0f;
    input.max_speed_y = 3.0f;
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

static int register_owner_tile(int first_sprite) {
    ContentRegistryTx* tx;
    ContentTileInput input = animated_tile(first_sprite);
    char err[256];
    tx = content_registry_begin("demo", err, sizeof(err));
    if (!tx) return 0;
    if (!content_registry_tx_register_tile(tx, &input, err, sizeof(err)) ||
        !content_registry_commit(tx, err, sizeof(err))) {
        content_registry_abort(tx);
        return 0;
    }
    return 1;
}

int main(void) {
    unsigned char fake_tilemap[3 * 2 * 4];
    ContentTileRender render;
    char err[256];

    memset(fake_tilemap, 0, sizeof(fake_tilemap));
    content_registry_shutdown();
    content_tiles_shutdown();
    CHECK(register_owner_tile(10));
    CHECK(content_tiles_map_begin(fake_tilemap, 3, 2, err, sizeof(err)));
    CHECK(content_tiles_map_cell_count() == 6);
    CHECK(content_tiles_map_set(1, 1, "DEMO:ANIMATED", 1, err, sizeof(err)));
    CHECK(content_tiles_map_definition_count() == 1);

    {
        ContentTileInteraction interaction;
        float vx = 1.8f;
        float vy = -2.8f;
        /* map_coord_tile truncates nonnegative world/tile coordinates: the
         * visual cell (1,1) is exactly [16,32) on each axis. This is important
         * for a scripted tile's right/bottom halves, which the former +0.5
         * approximation incorrectly assigned to adjacent metadata cells.
         * Mirroring also reverses force_x. */
        CHECK(!content_tiles_interaction_at_world(15.999f, 16.0f, 16, 16, &interaction));
        CHECK(!content_tiles_interaction_at_world(16.0f, 15.999f, 16, 16, &interaction));
        CHECK(!content_tiles_interaction_at_world(NAN, 16.0f, 16, 16, &interaction));
        CHECK(!content_tiles_interaction_at_world(INFINITY, 16.0f, 16, 16, &interaction));
        CHECK(!content_tiles_interaction_at_world(3.4e38f, 16.0f, 16, 16, &interaction));
        CHECK(content_tiles_interaction_at_world(16.0f, 16.0f, 16, 16, &interaction));
        CHECK(strcmp(interaction.key, "demo:animated") == 0);
        CHECK(interaction.cell_index == 4u);
        CHECK(interaction.x == 1 && interaction.y == 1);
        CHECK(interaction.room_mirrored == 1);
        CHECK(interaction.collision_mode == CONTENT_COLLISION_NATIVE);
        CHECK(interaction.force_x == -0.5f && interaction.force_y == -1.0f);
        CHECK(content_tiles_interaction_at_world(31.999f, 31.999f, 16, 16, &interaction));
        CHECK(interaction.cell_index == 4u);
        CHECK(content_tiles_interaction_at_cell(1, 1, &interaction));
        CHECK(interaction.cell_index == 4u && interaction.x == 1 && interaction.y == 1);
        CHECK(!content_tiles_interaction_at_cell(-1, 1, &interaction));
        CHECK(!content_tiles_interaction_at_cell(1, -1, &interaction));
        CHECK(!content_tiles_interaction_at_cell(3, 1, &interaction));
        CHECK(!content_tiles_interaction_at_cell(1, 2, &interaction));
        CHECK(!content_tiles_interaction_at_world(32.0f, 16.0f, 16, 16, &interaction));
        CHECK(!content_tiles_interaction_at_world(16.0f, 32.0f, 16, 16, &interaction));
        content_tiles_apply_interaction_velocity(&interaction, &vx, &vy);
        CHECK(fabsf(vx - 1.3f) < 0.00001f);
        CHECK(fabsf(vy - -3.0f) < 0.00001f);
    }

    CHECK(!content_tiles_render_for_action(fake_tilemap + 16, 1, 1, 1, 0, &render));
    CHECK(!content_tiles_render_for_action(fake_tilemap + 16, 2, 0, 1, 0, &render));
    CHECK(!content_tiles_render_for_action(fake_tilemap + 1, 2, -1, -1, 0, &render));
    CHECK(!content_tiles_native_visual_underlay_for_action(fake_tilemap + 16,
                                                            1, 1, 1));
    CHECK(!content_tiles_native_visual_underlay_for_action(fake_tilemap + 16,
                                                            2, 0, 1));
    CHECK(content_tiles_native_visual_underlay_for_action(fake_tilemap + 16,
                                                           2, 1, 1));
    CHECK(content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 0, &render));
    CHECK(render.cell_index == 4u);
    CHECK(render.sprite_index == 10);
    CHECK(render.flip_x == 1);
    CHECK(render.native_visual_underlay == 1);
    CHECK(render.offset_x == 2.0f && render.offset_y == -1.0f);
    CHECK(render.scale_x == 1.5f && render.scale_y == 0.5f);
    CHECK(fabsf(render.tint[0] - 0.2f) < 0.00001f &&
          fabsf(render.tint[3] - 0.8f) < 0.00001f);

    CHECK(content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 2, &render));
    CHECK(render.sprite_index == 11);
    CHECK(content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 4, &render));
    CHECK(render.sprite_index == 12);
    CHECK(content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 6, &render));
    CHECK(render.sprite_index == 11);

    CHECK(content_tiles_map_clear(1, 1, err, sizeof(err)));
    CHECK(!content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 0, &render));
    CHECK(content_tiles_map_set(1, 1, "demo:animated", 1, err, sizeof(err)));

    /* Registry replacement is picked up without rebuilding map metadata. */
    CHECK(register_owner_tile(20));
    CHECK(content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 0, &render));
    CHECK(render.sprite_index == 20);

    /* A same-glyph atomic reload updates interaction behavior in place. SET
     * only replaces explicitly declared axes. */
    {
        ContentRegistryTx* tx;
        ContentTileInput changed = animated_tile(20);
        ContentTileInteraction interaction;
        float vx = 1.25f;
        float vy = 4.0f;
        changed.force_mode = CONTENT_FORCE_SET;
        changed.force_axes = CONTENT_FORCE_AXIS_Y;
        changed.force_x = 0.0f;
        changed.max_speed_x = 0.0f;
        changed.force_y = -8.0f;
        changed.max_speed_y = 12.0f;
        tx = content_registry_begin("demo", err, sizeof(err));
        CHECK(tx != NULL);
        CHECK(content_registry_tx_register_tile(tx, &changed, err, sizeof(err)));
        CHECK(content_registry_commit(tx, err, sizeof(err)));
        CHECK(content_tiles_interaction_at_world(16.0f, 16.0f, 16, 16, &interaction));
        content_tiles_apply_interaction_velocity(&interaction, &vx, &vy);
        CHECK(vx == 1.25f && vy == -8.0f);
    }

    /* A hot-reload cannot silently change behavior of an already-generated
     * native cell. It degrades to the native renderer until map regeneration. */
    {
        ContentRegistryTx* tx;
        ContentTileInput changed = animated_tile(30);
        changed.native_glyph = 'X';
        tx = content_registry_begin("demo", err, sizeof(err));
        CHECK(tx != NULL);
        CHECK(content_registry_tx_register_tile(tx, &changed, err, sizeof(err)));
        CHECK(content_registry_commit(tx, err, sizeof(err)));
        CHECK(!content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 0, &render));
        CHECK(!content_tiles_native_visual_underlay_for_action(fake_tilemap + 16,
                                                                2, 1, 1));
    }

    /* Removing an owner cannot leave a stale definition pointer. */
    CHECK(content_registry_remove_owner("demo", err, sizeof(err)));
    CHECK(!content_tiles_render_for_action(fake_tilemap + 16, 2, 1, 1, 0, &render));

    content_tiles_map_end();
    CHECK(!content_tiles_map_active());
    content_tiles_shutdown();
    content_registry_shutdown();
    if (g_failures != 0) {
        fprintf(stderr, "%d content tile test(s) failed\n", g_failures);
        return 1;
    }
    puts("content_tiles_test: all checks passed");
    return 0;
}
