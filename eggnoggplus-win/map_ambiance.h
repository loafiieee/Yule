#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAP_AMBIANCE_MAX_PARTICLES 64u
#define MAP_AMBIANCE_MAX_DEFINITIONS 32u
#define MAP_AMBIANCE_MAX_EMITTERS 16u
#define MAP_AMBIANCE_MAX_LANES 512u
#define MAP_AMBIANCE_ID_MAX 32u
#define MAP_AMBIANCE_NAME_MAX 64u
#define MAP_AMBIANCE_SHEET_MAX 128u
#define MAP_AMBIANCE_FIXED_SCALE 256
#define MAP_AMBIANCE_ROOM_WIDTH 528

#define MAP_PARTICLE_TRANSITION_SCALE UINT8_C(0x01)
#define MAP_PARTICLE_TRANSITION_COLOR UINT8_C(0x02)
#define MAP_PARTICLE_TRANSITION_ROTATION UINT8_C(0x04)

typedef enum MapParticleInterpolation {
    MAP_PARTICLE_INTERPOLATION_LINEAR = 0,
    MAP_PARTICLE_INTERPOLATION_EASE_IN = 1,
    MAP_PARTICLE_INTERPOLATION_EASE_OUT = 2,
    MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT = 3
} MapParticleInterpolation;

typedef enum MapAmbianceEmitterShape {
    MAP_AMBIANCE_SHAPE_RECTANGLE = 0,
    MAP_AMBIANCE_SHAPE_ELLIPSE = 1,
    MAP_AMBIANCE_SHAPE_LINE = 2
} MapAmbianceEmitterShape;

typedef struct MapAmbianceRange {
    int32_t min_q;
    int32_t max_q;
} MapAmbianceRange;

typedef struct MapParticleDefinition {
    char id[MAP_AMBIANCE_ID_MAX + 1u];
    char name[MAP_AMBIANCE_NAME_MAX + 1u];
    char sprite_sheet[MAP_AMBIANCE_SHEET_MAX];
    int32_t sprite_index;
    uint16_t frame_count;
    uint16_t frame_ticks;
    int32_t scale_x_q;
    int32_t scale_y_q;
    uint32_t rgba;
    int32_t end_scale_x_q;
    int32_t end_scale_y_q;
    uint32_t end_rgba;
    int32_t start_rotation_q;
    int32_t end_rotation_q;
    uint8_t transition_flags;
    uint8_t interpolation;
    uint8_t reserved[2];
    uint32_t lifetime_ticks;
    uint32_t fade_in_ticks;
    uint32_t fade_out_ticks;
} MapParticleDefinition;

typedef struct MapAmbianceEmitter {
    uint16_t particle_index;
    uint16_t count;
    uint8_t particle_layer;
    uint8_t blend;
    uint8_t mirror_with_room;
    uint8_t shape;
    uint8_t motion_interpolation;
    int32_t area_x_q;
    int32_t area_y_q;
    int32_t area_width_q;
    int32_t area_height_q;
    MapAmbianceRange velocity_x;
    MapAmbianceRange velocity_y;
    int32_t acceleration_x_q;
    int32_t acceleration_y_q;
    MapAmbianceRange rotation_speed;
} MapAmbianceEmitter;

typedef struct MapAmbianceDefinition {
    char id[MAP_AMBIANCE_ID_MAX + 1u];
    char name[MAP_AMBIANCE_NAME_MAX + 1u];
    uint8_t native_ambient;
    uint8_t emitter_count;
    uint8_t reserved[2];
    MapAmbianceEmitter emitters[MAP_AMBIANCE_MAX_EMITTERS];
} MapAmbianceDefinition;

typedef struct MapAmbianceCatalog {
    uint64_t identity;
    uint16_t particle_count;
    uint16_t ambiance_count;
    MapParticleDefinition particles[MAP_AMBIANCE_MAX_PARTICLES];
    MapAmbianceDefinition ambiances[MAP_AMBIANCE_MAX_DEFINITIONS];
} MapAmbianceCatalog;

typedef struct MapAmbianceRenderParticle {
    uint16_t particle_index;
    uint16_t emitter_index;
    uint16_t lane_index;
    uint8_t particle_layer;
    uint8_t blend;
    int32_t sprite_index;
    double world_x;
    double world_y;
    double angle_degrees;
    float scale_x;
    float scale_y;
    float tint[4];
} MapAmbianceRenderParticle;

/* Validates the bounded POD catalog independently of JSON parsing and assets. */
int map_ambiance_catalog_validate(const MapAmbianceCatalog* catalog,
                                  char* err, size_t err_cap);

/* Stateless renderer. cursor starts at zero and is retained between calls.
 * It returns one particle, zero at end, and -1 for invalid input. Particle
 * positions depend only on catalog identity, source room, tick, and lane. */
int map_ambiance_render_next(const MapAmbianceCatalog* catalog,
                             uint16_t ambiance_index,
                             int source_room,
                             int final_room,
                             int mirror_room,
                             uint64_t tick,
                             uint8_t particle_layer,
                             uint32_t* cursor,
                             MapAmbianceRenderParticle* out);

#ifdef __cplusplus
}
#endif
