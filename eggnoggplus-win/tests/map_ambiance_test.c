#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../map_ambiance.h"

static int failures;
#define CHECK(value, message) do { if (!(value)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); failures++; \
} } while (0)

static MapAmbianceCatalog fixture(void) {
    MapAmbianceCatalog catalog;
    MapParticleDefinition* particle;
    MapAmbianceDefinition* ambiance;
    MapAmbianceEmitter* emitter;
    memset(&catalog, 0, sizeof(catalog));
    catalog.identity = UINT64_C(0x123456789abcdef0);
    catalog.particle_count = 1;
    catalog.ambiance_count = 1;
    particle = &catalog.particles[0];
    snprintf(particle->id, sizeof(particle->id), "ember");
    snprintf(particle->name, sizeof(particle->name), "Ember");
    snprintf(particle->sprite_sheet, sizeof(particle->sprite_sheet),
             "particles.png");
    particle->sprite_index = 7;
    particle->frame_count = 4;
    particle->frame_ticks = 3;
    particle->scale_x_q = 256;
    particle->scale_y_q = 128;
    particle->rgba = UINT32_C(0xff8040c0);
    particle->lifetime_ticks = 60;
    particle->fade_in_ticks = 10;
    particle->fade_out_ticks = 12;
    ambiance = &catalog.ambiances[0];
    snprintf(ambiance->id, sizeof(ambiance->id), "embers");
    snprintf(ambiance->name, sizeof(ambiance->name), "Embers");
    ambiance->native_ambient = 6;
    ambiance->emitter_count = 1;
    emitter = &ambiance->emitters[0];
    emitter->particle_index = 0;
    emitter->count = 3;
    emitter->particle_layer = 2;
    emitter->blend = 1;
    emitter->mirror_with_room = 1;
    emitter->area_x_q = 10 * 256;
    emitter->area_y_q = 20 * 256;
    emitter->area_width_q = 100 * 256;
    emitter->area_height_q = 40 * 256;
    emitter->velocity_x.min_q = 0;
    emitter->velocity_x.max_q = 0;
    emitter->velocity_y.min_q = 0;
    emitter->velocity_y.max_q = 0;
    emitter->rotation_speed.min_q = -256;
    emitter->rotation_speed.max_q = 256;
    return catalog;
}

static void test_lifetime_visual_transition(void) {
    MapAmbianceCatalog catalog = fixture();
    MapParticleDefinition* particle = &catalog.particles[0];
    MapAmbianceRenderParticle rendered[4];
    int seen[4] = {0, 0, 0, 0};
    char err[128];
    particle->frame_count = 4;
    particle->frame_ticks = 1;
    particle->lifetime_ticks = 4;
    particle->fade_in_ticks = 0;
    particle->fade_out_ticks = 0;
    particle->end_scale_x_q = 0;
    particle->end_scale_y_q = 512;
    particle->end_rgba = UINT32_C(0x0080ffff);
    particle->start_rotation_q = -90 * MAP_AMBIANCE_FIXED_SCALE;
    particle->end_rotation_q = 90 * MAP_AMBIANCE_FIXED_SCALE;
    particle->transition_flags = MAP_PARTICLE_TRANSITION_SCALE |
        MAP_PARTICLE_TRANSITION_COLOR | MAP_PARTICLE_TRANSITION_ROTATION;
    particle->interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
    catalog.ambiances[0].emitters[0].count = 1;
    catalog.ambiances[0].emitters[0].rotation_speed.min_q = 0;
    catalog.ambiances[0].emitters[0].rotation_speed.max_q = 0;
    CHECK(map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "valid lifetime visual transition rejected");
    for (uint64_t tick = 0; tick < 4; tick++) {
        MapAmbianceRenderParticle out;
        uint32_t cursor = 0;
        int age;
        CHECK(map_ambiance_render_next(&catalog, 0, 0, 0, 0, tick, 2,
                                       &cursor, &out) == 1,
              "transition particle did not render");
        age = out.sprite_index - particle->sprite_index;
        CHECK(age >= 0 && age < 4, "transition age marker was invalid");
        if (age >= 0 && age < 4) {
            rendered[age] = out;
            seen[age] = 1;
        }
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3],
          "transition did not enumerate every lifetime age");
    CHECK(rendered[0].scale_x == 1.0f && rendered[0].scale_y == 0.5f &&
              fabs(rendered[0].angle_degrees + 90.0) < 0.000001 &&
              rendered[0].tint[0] == 1.0f,
          "transition did not preserve its authored start values");
    CHECK(rendered[3].scale_x == 0.0f && rendered[3].scale_y == 2.0f &&
              fabs(rendered[3].angle_degrees - 90.0) < 0.000001 &&
              rendered[3].tint[0] == 0.0f &&
              fabs(rendered[3].tint[1] - 128.0f / 255.0f) < 0.000001f &&
              rendered[3].tint[3] == 1.0f,
          "transition did not reach its authored end values");
    CHECK(fabs(rendered[1].scale_x - 2.0f / 3.0f) < 0.000001f &&
              fabs(rendered[2].angle_degrees - 30.0) < 0.000001,
          "linear lifetime interpolation used the wrong progress");

    particle->interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT;
    {
        MapAmbianceRenderParticle out;
        uint32_t cursor = 0;
        uint64_t tick = 0;
        do {
            cursor = 0;
            CHECK(map_ambiance_render_next(&catalog, 0, 0, 0, 0, tick++, 2,
                                           &cursor, &out) == 1,
                  "eased transition particle did not render");
        } while (out.sprite_index - particle->sprite_index != 1 && tick < 5);
        CHECK(fabs(out.scale_x - (float)(1.0 - 2.0 / 9.0)) < 0.000001f,
              "ease-in-out curve was not applied deterministically");
    }

    particle->interpolation = 4;
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "unknown lifetime interpolation accepted");
    particle->interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
    particle->transition_flags = UINT8_C(0x80);
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "unknown particle transition flag accepted");
}

static void test_emitter_shapes(void) {
    MapAmbianceCatalog catalog = fixture();
    MapAmbianceEmitter* emitter = &catalog.ambiances[0].emitters[0];
    MapAmbianceRenderParticle particle;
    char err[128];
    uint32_t cursor;
    emitter->shape = MAP_AMBIANCE_SHAPE_ELLIPSE;
    CHECK(map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "ellipse emitter rejected");
    for (uint64_t tick = 0; tick < 80; ++tick) {
        cursor = 0;
        CHECK(map_ambiance_render_next(&catalog, 0, 0, 0, 0, tick, 2,
                                       &cursor, &particle) == 1,
              "ellipse emitter did not render");
        /* The fixture has zero velocity and acceleration, so every particle
         * must remain inside its filled ellipse at every lifetime phase. */
        {
            double nx = (particle.world_x - 60.0) / 50.0;
            double ny = (particle.world_y - 40.0) / 20.0;
            CHECK(nx * nx + ny * ny <= 1.00000001,
                  "ellipse emitter escaped its authored shape");
        }
    }
    emitter->shape = MAP_AMBIANCE_SHAPE_LINE;
    CHECK(map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "line emitter rejected");
    cursor = 0;
    CHECK(map_ambiance_render_next(&catalog, 0, 0, 0, 0, 17, 2,
                                   &cursor, &particle) == 1,
          "line emitter did not render");
    CHECK(fabs((particle.world_x - 10.0) / 100.0 -
               (particle.world_y - 20.0) / 40.0) < 0.00000001,
          "line emitter did not use the same progress on both axes");
    emitter->shape = 3;
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "unknown emitter shape was accepted");
}

static void test_emitter_motion_interpolation(void) {
    MapAmbianceCatalog catalog = fixture();
    MapParticleDefinition* particle = &catalog.particles[0];
    MapAmbianceEmitter* emitter = &catalog.ambiances[0].emitters[0];
    char err[128];
    int saw_middle = 0, saw_end = 0;
    particle->lifetime_ticks = 8;
    particle->frame_count = 8;
    particle->frame_ticks = 1;
    particle->fade_in_ticks = 0;
    particle->fade_out_ticks = 0;
    emitter->count = 1;
    emitter->velocity_x.min_q = MAP_AMBIANCE_FIXED_SCALE;
    emitter->velocity_x.max_q = MAP_AMBIANCE_FIXED_SCALE;
    emitter->rotation_speed.min_q = 0;
    emitter->rotation_speed.max_q = 0;
    emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN;
    CHECK(map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "ease-in motion emitter rejected");
    for (uint64_t tick = 0; tick < 8; tick++) {
        MapAmbianceRenderParticle eased, linear;
        uint32_t cursor = 0;
        int age;
        CHECK(map_ambiance_render_next(&catalog, 0, 0, 0, 0, tick, 2,
                                       &cursor, &eased) == 1,
              "eased motion particle did not render");
        age = eased.sprite_index - particle->sprite_index;
        emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_LINEAR;
        cursor = 0;
        CHECK(map_ambiance_render_next(&catalog, 0, 0, 0, 0, tick, 2,
                                       &cursor, &linear) == 1,
              "linear comparison particle did not render");
        emitter->motion_interpolation = MAP_PARTICLE_INTERPOLATION_EASE_IN;
        if (age == 3) {
            CHECK(fabs(eased.world_x - linear.world_x - (9.0 / 7.0 - 3.0)) < 0.000001,
                  "ease-in motion did not remap elapsed movement");
            saw_middle = 1;
        }
        if (age == 7) {
            CHECK(fabs(eased.world_x - linear.world_x) < 0.000001,
                  "eased motion did not reach the same final position");
            saw_end = 1;
        }
    }
    CHECK(saw_middle && saw_end, "motion test missed lifetime ages");
    emitter->motion_interpolation = 4;
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "unknown emitter motion curve was accepted");
}

int main(void) {
    MapAmbianceCatalog catalog = fixture();
    MapAmbianceRenderParticle first;
    MapAmbianceRenderParticle repeat;
    MapAmbianceRenderParticle mirrored;
    char err[128];
    uint32_t cursor = 0;
    uint32_t repeat_cursor = 0;
    uint32_t mirror_cursor = 0;
    int count = 0;
    CHECK(map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "valid catalog rejected");
    CHECK(map_ambiance_render_next(&catalog, 0, 1, 0, 0, 17, 2,
                                   &cursor, &first) == 1,
          "first particle did not render");
    CHECK(map_ambiance_render_next(&catalog, 0, 1, 0, 0, 17, 2,
                                   &repeat_cursor, &repeat) == 1 &&
              memcmp(&first, &repeat, sizeof(first)) == 0,
          "same identity/tick/lane did not render deterministically");
    CHECK(first.world_x >= 10.0 && first.world_x < 110.0 &&
              first.world_y >= 20.0 && first.world_y < 60.0,
          "zero-velocity particle escaped its spawn area");
    CHECK(first.sprite_index >= 7 && first.sprite_index <= 10,
          "animation frame escaped its declared range");
    CHECK(first.tint[3] >= 0.0f && first.tint[3] <= 192.0f / 255.0f,
          "fade alpha escaped its source tint");
    CHECK(first.scale_x == 1.0f && first.scale_y == 0.5f &&
              first.blend == 1,
          "visual scale or blend was lost");
    CHECK(map_ambiance_render_next(&catalog, 0, 1, 2, 1, 17, 2,
                                   &mirror_cursor, &mirrored) == 1,
          "mirrored particle did not render");
    CHECK(fabs((first.world_x + (mirrored.world_x - 2.0 * 528.0)) -
               528.0) < 0.000001,
          "right room did not mirror the authored particle lane");
    CHECK(fabs(first.angle_degrees + mirrored.angle_degrees) < 0.000001 &&
              mirrored.scale_x == -first.scale_x,
          "mirrored rotation/visual orientation was not inverted");

    cursor = 0;
    while (map_ambiance_render_next(&catalog, 0, 1, 0, 0, 17, 2,
                                    &cursor, &repeat) == 1) count++;
    CHECK(count == 3, "renderer did not enumerate each stable lane once");
    cursor = 0;
    CHECK(map_ambiance_render_next(&catalog, 0, 1, 0, 0, 17, 4,
                                   &cursor, &repeat) == 0 && cursor == 3,
          "particle-layer filtering did not consume nonmatching lanes");

    catalog.particles[0].lifetime_ticks = 0;
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "zero particle lifetime accepted");
    catalog = fixture();
    catalog.ambiances[0].emitters[0].count = 513;
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "catalog above the stable-lane limit accepted");
    catalog = fixture();
    snprintf(catalog.particles[0].id,
             sizeof(catalog.particles[0].id), "Bad id");
    CHECK(!map_ambiance_catalog_validate(&catalog, err, sizeof(err)),
          "invalid particle id accepted");

    test_lifetime_visual_transition();
    test_emitter_shapes();
    test_emitter_motion_interpolation();

    if (failures) return 1;
    puts("map ambiance tests: ALL OK");
    return 0;
}
