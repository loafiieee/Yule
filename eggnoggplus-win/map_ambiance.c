#include "map_ambiance.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void ambiance_error(char* err, size_t err_cap, const char* message) {
    if (!err || err_cap == 0u) return;
    snprintf(err, err_cap, "%s", message ? message : "invalid ambiance");
    err[err_cap - 1u] = '\0';
}

static int ambiance_id_valid(const char* id) {
    size_t length;
    if (!id || id[0] < 'a' || id[0] > 'z') return 0;
    length = strlen(id);
    if (length == 0u || length > MAP_AMBIANCE_ID_MAX) return 0;
    for (size_t i = 1u; i < length; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

static int fixed_range_valid(MapAmbianceRange range) {
    const int32_t limit = 256 * MAP_AMBIANCE_FIXED_SCALE;
    return range.min_q >= -limit && range.max_q <= limit &&
           range.min_q <= range.max_q;
}

static int particle_scale_valid(int32_t value, int allow_zero) {
    return (allow_zero || value != 0) &&
           value >= -64 * MAP_AMBIANCE_FIXED_SCALE &&
           value <= 64 * MAP_AMBIANCE_FIXED_SCALE;
}

int map_ambiance_catalog_validate(const MapAmbianceCatalog* catalog,
                                  char* err, size_t err_cap) {
    uint32_t lanes = 0u;
    if (err && err_cap) err[0] = '\0';
    if (!catalog || catalog->particle_count > MAP_AMBIANCE_MAX_PARTICLES ||
        catalog->ambiance_count > MAP_AMBIANCE_MAX_DEFINITIONS) {
        ambiance_error(err, err_cap, "ambiance catalog count is invalid");
        return 0;
    }
    for (uint16_t i = 0; i < catalog->particle_count; i++) {
        const MapParticleDefinition* particle = &catalog->particles[i];
        if (!ambiance_id_valid(particle->id) || !particle->name[0] ||
            !particle->sprite_sheet[0] || particle->sprite_index < 0 ||
            particle->frame_count == 0u || particle->frame_count > 256u ||
            particle->frame_ticks == 0u || particle->frame_ticks > 3600u ||
            !particle_scale_valid(particle->scale_x_q, 0) ||
            !particle_scale_valid(particle->scale_y_q, 0) ||
            ((particle->transition_flags & MAP_PARTICLE_TRANSITION_SCALE) &&
             (!particle_scale_valid(particle->end_scale_x_q, 1) ||
              !particle_scale_valid(particle->end_scale_y_q, 1))) ||
            (particle->transition_flags &
             ~(MAP_PARTICLE_TRANSITION_SCALE |
               MAP_PARTICLE_TRANSITION_COLOR |
               MAP_PARTICLE_TRANSITION_ROTATION)) != 0u ||
            particle->interpolation > MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT ||
            particle->reserved[0] != 0u || particle->reserved[1] != 0u ||
            particle->start_rotation_q < -3600 * MAP_AMBIANCE_FIXED_SCALE ||
            particle->start_rotation_q > 3600 * MAP_AMBIANCE_FIXED_SCALE ||
            particle->end_rotation_q < -3600 * MAP_AMBIANCE_FIXED_SCALE ||
            particle->end_rotation_q > 3600 * MAP_AMBIANCE_FIXED_SCALE ||
            particle->lifetime_ticks == 0u ||
            particle->lifetime_ticks > 360000u ||
            particle->fade_in_ticks > particle->lifetime_ticks ||
            particle->fade_out_ticks > particle->lifetime_ticks) {
            ambiance_error(err, err_cap, "particle definition is invalid");
            return 0;
        }
        for (uint16_t other = 0; other < i; other++) {
            if (strcmp(particle->id, catalog->particles[other].id) == 0) {
                ambiance_error(err, err_cap, "duplicate particle id");
                return 0;
            }
        }
    }
    for (uint16_t i = 0; i < catalog->ambiance_count; i++) {
        const MapAmbianceDefinition* ambiance = &catalog->ambiances[i];
        if (!ambiance_id_valid(ambiance->id) || !ambiance->name[0] ||
            ambiance->native_ambient > 9u ||
            ambiance->emitter_count > MAP_AMBIANCE_MAX_EMITTERS) {
            ambiance_error(err, err_cap, "ambiance definition is invalid");
            return 0;
        }
        for (uint16_t other = 0; other < i; other++) {
            if (strcmp(ambiance->id, catalog->ambiances[other].id) == 0) {
                ambiance_error(err, err_cap, "duplicate ambiance id");
                return 0;
            }
        }
        for (uint8_t emitter_index = 0;
             emitter_index < ambiance->emitter_count; emitter_index++) {
            const MapAmbianceEmitter* emitter =
                &ambiance->emitters[emitter_index];
            if (emitter->particle_index >= catalog->particle_count ||
                emitter->count == 0u || emitter->particle_layer > 4u ||
                emitter->blend > 1u ||
                emitter->shape > MAP_AMBIANCE_SHAPE_LINE ||
                emitter->motion_interpolation > MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT ||
                emitter->mirror_with_room > 1u ||
                emitter->area_width_q < 0 || emitter->area_height_q < 0 ||
                !fixed_range_valid(emitter->velocity_x) ||
                !fixed_range_valid(emitter->velocity_y) ||
                !fixed_range_valid(emitter->rotation_speed) ||
                emitter->acceleration_x_q < -64 * MAP_AMBIANCE_FIXED_SCALE ||
                emitter->acceleration_x_q > 64 * MAP_AMBIANCE_FIXED_SCALE ||
                emitter->acceleration_y_q < -64 * MAP_AMBIANCE_FIXED_SCALE ||
                emitter->acceleration_y_q > 64 * MAP_AMBIANCE_FIXED_SCALE) {
                ambiance_error(err, err_cap, "ambiance emitter is invalid");
                return 0;
            }
            lanes += emitter->count;
            if (lanes > MAP_AMBIANCE_MAX_LANES) {
                ambiance_error(err, err_cap,
                               "ambiance catalog exceeds the particle lane limit");
                return 0;
            }
        }
    }
    return 1;
}

static uint64_t ambiance_hash(uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static double ambiance_unit(uint64_t hash) {
    return (double)((hash >> 40) & UINT64_C(0xffffff)) /
           (double)UINT32_C(0x1000000);
}

static double ambiance_range(MapAmbianceRange range, uint64_t hash) {
    double low = (double)range.min_q / MAP_AMBIANCE_FIXED_SCALE;
    double high = (double)range.max_q / MAP_AMBIANCE_FIXED_SCALE;
    return low + (high - low) * ambiance_unit(hash);
}

static double ambiance_ease(double progress, uint8_t interpolation) {
    switch ((MapParticleInterpolation)interpolation) {
        case MAP_PARTICLE_INTERPOLATION_EASE_IN:
            return progress * progress;
        case MAP_PARTICLE_INTERPOLATION_EASE_OUT:
            return 1.0 - (1.0 - progress) * (1.0 - progress);
        case MAP_PARTICLE_INTERPOLATION_EASE_IN_OUT:
            return progress < 0.5
                ? 2.0 * progress * progress
                : 1.0 - 2.0 * (1.0 - progress) * (1.0 - progress);
        default:
            return progress;
    }
}

static double particle_transition_progress(const MapParticleDefinition* particle,
                                           uint32_t age) {
    double progress = particle->lifetime_ticks <= 1u ? 1.0 :
        (double)age / (double)(particle->lifetime_ticks - 1u);
    return ambiance_ease(progress, particle->interpolation);
}

static float particle_color_channel(uint32_t start_rgba, uint32_t end_rgba,
                                    unsigned shift, double progress) {
    double start = (double)((start_rgba >> shift) & 0xffu);
    double end = (double)((end_rgba >> shift) & 0xffu);
    return (float)((start + (end - start) * progress) / 255.0);
}

static uint64_t lane_seed(const MapAmbianceCatalog* catalog,
                          uint16_t ambiance_index, int source_room,
                          uint16_t emitter_index, uint16_t lane_index,
                          uint64_t cycle) {
    uint64_t seed = catalog->identity;
    seed ^= (uint64_t)ambiance_index * UINT64_C(0xd6e8feb86659fd93);
    seed ^= (uint64_t)(uint32_t)source_room * UINT64_C(0xa0761d6478bd642f);
    seed ^= (uint64_t)emitter_index * UINT64_C(0xe7037ed1a0b428db);
    seed ^= (uint64_t)lane_index * UINT64_C(0x8ebc6af09c88c6e3);
    seed ^= cycle * UINT64_C(0x589965cc75374cc3);
    return ambiance_hash(seed);
}

int map_ambiance_render_next(const MapAmbianceCatalog* catalog,
                             uint16_t ambiance_index,
                             int source_room,
                             int final_room,
                             int mirror_room,
                             uint64_t tick,
                             uint8_t particle_layer,
                             uint32_t* cursor,
                             MapAmbianceRenderParticle* out) {
    const MapAmbianceDefinition* ambiance;
    uint32_t flat = 0u;
    if (!catalog || !cursor || !out ||
        ambiance_index >= catalog->ambiance_count || source_room < 0 ||
        final_room < 0 || (mirror_room != 0 && mirror_room != 1) ||
        particle_layer > 4u) return -1;
    ambiance = &catalog->ambiances[ambiance_index];
    for (uint16_t emitter_index = 0;
         emitter_index < ambiance->emitter_count; emitter_index++) {
        const MapAmbianceEmitter* emitter =
            &ambiance->emitters[emitter_index];
        const MapParticleDefinition* particle;
        for (uint16_t lane = 0; lane < emitter->count; lane++, flat++) {
            uint64_t phase_seed;
            uint64_t absolute_tick;
            uint64_t cycle;
            uint32_t age;
            uint64_t seed;
            double local_x;
            double velocity_x;
            double acceleration_x;
            double rotation;
            double progress;
            double motion_age;
            int32_t end_scale_x_q;
            int32_t end_scale_y_q;
            uint32_t end_rgba;
            double alpha = 1.0;
            if (flat < *cursor) continue;
            *cursor = flat + 1u;
            if (emitter->particle_layer != particle_layer ||
                emitter->particle_index >= catalog->particle_count) continue;
            particle = &catalog->particles[emitter->particle_index];
            if (particle->lifetime_ticks == 0u) return -1;
            phase_seed = lane_seed(catalog, ambiance_index, source_room,
                                   emitter_index, lane, 0u);
            absolute_tick = tick +
                phase_seed % (uint64_t)particle->lifetime_ticks;
            age = (uint32_t)(absolute_tick % particle->lifetime_ticks);
            cycle = absolute_tick / particle->lifetime_ticks;
            seed = lane_seed(catalog, ambiance_index, source_room,
                             emitter_index, lane, cycle + 1u);
            progress = particle_transition_progress(particle, age);
            motion_age = (double)age;
            if (emitter->motion_interpolation != MAP_PARTICLE_INTERPOLATION_LINEAR &&
                particle->lifetime_ticks > 1u) {
                motion_age = (particle->lifetime_ticks - 1u) *
                    ambiance_ease((double)age / (particle->lifetime_ticks - 1u),
                                  emitter->motion_interpolation);
            }
            {
                double u = ambiance_unit(ambiance_hash(seed ^ UINT64_C(0x01)));
                double v = ambiance_unit(ambiance_hash(seed ^ UINT64_C(0x02)));
                double unit_x = u, unit_y = v;
                if (emitter->shape == MAP_AMBIANCE_SHAPE_ELLIPSE) {
                    double radius = sqrt(v);
                    double angle = u * 6.2831853071795864769;
                    unit_x = 0.5 + 0.5 * radius * cos(angle);
                    unit_y = 0.5 + 0.5 * radius * sin(angle);
                } else if (emitter->shape == MAP_AMBIANCE_SHAPE_LINE) {
                    unit_y = u;
                }
                local_x = (double)emitter->area_x_q / MAP_AMBIANCE_FIXED_SCALE +
                    (double)emitter->area_width_q / MAP_AMBIANCE_FIXED_SCALE * unit_x;
                out->world_y =
                    (double)emitter->area_y_q / MAP_AMBIANCE_FIXED_SCALE +
                    (double)emitter->area_height_q / MAP_AMBIANCE_FIXED_SCALE * unit_y;
            }
            velocity_x = ambiance_range(
                emitter->velocity_x,
                ambiance_hash(seed ^ UINT64_C(0x03)));
            out->world_y += ambiance_range(
                emitter->velocity_y,
                ambiance_hash(seed ^ UINT64_C(0x04))) * motion_age;
            acceleration_x = (double)emitter->acceleration_x_q /
                             MAP_AMBIANCE_FIXED_SCALE;
            local_x += velocity_x * motion_age +
                       acceleration_x * motion_age * motion_age * 0.5;
            out->world_y +=
                (double)emitter->acceleration_y_q /
                MAP_AMBIANCE_FIXED_SCALE * motion_age * motion_age * 0.5;
            rotation = (double)particle->start_rotation_q /
                           MAP_AMBIANCE_FIXED_SCALE;
            if (particle->transition_flags & MAP_PARTICLE_TRANSITION_ROTATION) {
                rotation += ((double)particle->end_rotation_q -
                             particle->start_rotation_q) /
                            MAP_AMBIANCE_FIXED_SCALE * progress;
            }
            rotation += ambiance_range(
                emitter->rotation_speed,
                ambiance_hash(seed ^ UINT64_C(0x05))) * motion_age;
            if (mirror_room && emitter->mirror_with_room) {
                local_x = MAP_AMBIANCE_ROOM_WIDTH - local_x;
                rotation = -rotation;
            }
            out->world_x = final_room * (double)MAP_AMBIANCE_ROOM_WIDTH +
                           local_x;
            if (particle->fade_in_ticks > 0u &&
                age < particle->fade_in_ticks) {
                alpha *= (double)age / particle->fade_in_ticks;
            }
            if (particle->fade_out_ticks > 0u &&
                particle->lifetime_ticks - age <= particle->fade_out_ticks) {
                alpha *= (double)(particle->lifetime_ticks - age) /
                         particle->fade_out_ticks;
            }
            end_rgba = (particle->transition_flags &
                        MAP_PARTICLE_TRANSITION_COLOR)
                ? particle->end_rgba : particle->rgba;
            out->tint[0] = particle_color_channel(
                particle->rgba, end_rgba, 24u, progress);
            out->tint[1] = particle_color_channel(
                particle->rgba, end_rgba, 16u, progress);
            out->tint[2] = particle_color_channel(
                particle->rgba, end_rgba, 8u, progress);
            out->tint[3] = particle_color_channel(
                particle->rgba, end_rgba, 0u, progress) * (float)alpha;
            out->particle_index = emitter->particle_index;
            out->emitter_index = emitter_index;
            out->lane_index = lane;
            out->particle_layer = emitter->particle_layer;
            out->blend = emitter->blend;
            out->sprite_index = particle->sprite_index +
                (int32_t)((age / particle->frame_ticks) %
                          particle->frame_count);
            out->angle_degrees = rotation;
            end_scale_x_q = (particle->transition_flags &
                             MAP_PARTICLE_TRANSITION_SCALE)
                ? particle->end_scale_x_q : particle->scale_x_q;
            end_scale_y_q = (particle->transition_flags &
                             MAP_PARTICLE_TRANSITION_SCALE)
                ? particle->end_scale_y_q : particle->scale_y_q;
            out->scale_x = (float)((double)particle->scale_x_q +
                ((double)end_scale_x_q - particle->scale_x_q) * progress) /
                MAP_AMBIANCE_FIXED_SCALE;
            out->scale_y = (float)((double)particle->scale_y_q +
                ((double)end_scale_y_q - particle->scale_y_q) * progress) /
                MAP_AMBIANCE_FIXED_SCALE;
            if (mirror_room && emitter->mirror_with_room) {
                out->scale_x = -out->scale_x;
            }
            return 1;
        }
    }
    return 0;
}
