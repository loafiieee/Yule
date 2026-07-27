#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../map_script.h"

static int g_failures;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        ++g_failures; \
    } \
} while (0)

typedef struct SpringHost {
    int apply_count;
    MapScriptObjectView last_applied;
} SpringHost;

static void spring_apply(void* userdata, const MapScriptObjectView* object) {
    SpringHost* host = (SpringHost*)userdata;
    ++host->apply_count;
    host->last_applied = *object;
}

static char* read_source(const char* path, size_t* out_length) {
    FILE* file;
    long file_length;
    char* source;
    size_t length;

    *out_length = 0;
    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "FAIL: could not open checked-in spring script: %s\n", path);
        ++g_failures;
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "FAIL: could not measure checked-in spring script: %s\n", path);
        ++g_failures;
        fclose(file);
        return NULL;
    }
    length = (size_t)file_length;
    if (length == 0 || length > MAP_SCRIPT_SOURCE_MAX) {
        fprintf(stderr, "FAIL: checked-in spring script has invalid size: %lu\n",
                (unsigned long)length);
        ++g_failures;
        fclose(file);
        return NULL;
    }
    source = (char*)malloc(length + 1u);
    if (!source) {
        fprintf(stderr, "FAIL: could not allocate checked-in spring script buffer\n");
        ++g_failures;
        fclose(file);
        return NULL;
    }
    if (fread(source, 1, length, file) != length) {
        fprintf(stderr, "FAIL: could not read checked-in spring script: %s\n", path);
        ++g_failures;
        free(source);
        fclose(file);
        return NULL;
    }
    fclose(file);
    source[length] = '\0';
    *out_length = length;
    return source;
}

static MapScriptDefinition spring_definition(
        const char* source,
        size_t source_length,
        const MapScriptTileBinding* bindings,
        size_t binding_count) {
    MapScriptDefinition definition;
    memset(&definition, 0, sizeof(definition));
    definition.script_id = UINT64_C(0x737072696e674632);
    definition.chunk_name = "@maps/v2_symbolic_demo/map.lua";
    definition.source = source;
    definition.source_len = source_length;
    definition.bindings = bindings;
    definition.binding_count = binding_count;
    return definition;
}

static float radius_for_kind(int kind) {
    switch (kind) {
        case MAP_SCRIPT_OBJECT_PLAYER:
            return MAP_SCRIPT_PLAYER_CONTACT_RADIUS;
        case MAP_SCRIPT_OBJECT_DEAD_BODY:
            return MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS;
        case MAP_SCRIPT_OBJECT_SWORD:
            return MAP_SCRIPT_SWORD_CONTACT_RADIUS;
        case MAP_SCRIPT_OBJECT_HAZARD:
            return MAP_SCRIPT_HAZARD_CONTACT_RADIUS;
        default:
            return -1.0f;
    }
}

static uint32_t id_for_kind(int kind) {
    switch (kind) {
        case MAP_SCRIPT_OBJECT_PLAYER:
            return 0;
        case MAP_SCRIPT_OBJECT_DEAD_BODY:
            return 1;
        case MAP_SCRIPT_OBJECT_SWORD:
            return 2;
        case MAP_SCRIPT_OBJECT_HAZARD:
            return 3;
        default:
            return UINT32_MAX;
    }
}

static void initialize_surface_candidate(MapScriptCellCandidateView* candidate,
                                         MapScriptObjectView* object,
                                         int kind,
                                         uint32_t cell_index) {
    memset(object, 0, sizeof(*object));
    object->object_id = id_for_kind(kind);
    object->lifecycle_id = 0;
    object->object_kind = (uint8_t)kind;
    object->x = 24.0f;
    object->y = 10.0f;

    memset(candidate, 0, sizeof(*candidate));
    candidate->object = object;
    candidate->object_kind = kind;
    candidate->contact_radius = radius_for_kind(kind);
    candidate->sensor_x = object->x;
    candidate->sensor_y = object->y;
    candidate->cell_index = cell_index;
    candidate->tile_x = 1;
    candidate->tile_y = 1;
    candidate->tile_width = 16;
    candidate->tile_height = 16;
    candidate->symbol = '>';
    candidate->qualified_key = "v2_symbolic_demo:spring";
}

static float simulate_native_apex(float initial_vy, float gravity) {
    float displacement = 0.0f;
    float vy = initial_vy;
    int ticks = 0;
    while (vy < 0.0f && ticks < 256) {
        displacement += vy;
        vy += gravity;
        ++ticks;
    }
    CHECK(ticks > 0 && ticks < 256);
    return -displacement;
}

static const MapScriptSnapshotVelocityLimit* find_velocity_limit(
        const MapScriptSnapshot* snapshot,
        uint32_t object_id,
        uint32_t lifecycle_id) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        const MapScriptSnapshotVelocityLimit* limit = &snapshot->velocity_limits[i];
        if (limit->in_use && limit->object_id == object_id &&
            limit->lifecycle_id == lifecycle_id) {
            return limit;
        }
    }
    return NULL;
}

static int activate_spring(const MapScriptDefinition* definition,
                           SpringHost* host,
                           char* error,
                           size_t error_capacity) {
    MapScriptHost host_api;
    memset(host, 0, sizeof(*host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.apply_object_fn = spring_apply;
    host_api.userdata = host;
    if (!map_script_activate(definition, &host_api, error, error_capacity)) {
        fprintf(stderr, "FAIL: checked-in spring script did not activate: %s\n", error);
        ++g_failures;
        return 0;
    }
    return 1;
}

static void test_launch_profiles(const MapScriptDefinition* definition) {
    static const int kinds[] = {
        MAP_SCRIPT_OBJECT_PLAYER,
        MAP_SCRIPT_OBJECT_SWORD,
        MAP_SCRIPT_OBJECT_HAZARD
    };
    SpringHost host;
    MapScriptObjectView objects[3];
    MapScriptCellCandidateView candidates[3];
    MapScriptSnapshot snapshot;
    char error[512];
    size_t i;

    if (!activate_spring(definition, &host, error, sizeof(error))) return;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        float expected = kinds[i] == MAP_SCRIPT_OBJECT_PLAYER ? -4.0f : -2.8f;
        float gravity = kinds[i] == MAP_SCRIPT_OBJECT_PLAYER ? 0.15f : 0.075f;
        float rise;
        initialize_surface_candidate(&candidates[i], &objects[i], kinds[i],
                                     UINT32_C(100) + (uint32_t)i);
        CHECK(map_script_dispatch_cell_candidate(&candidates[i], error,
                                                  sizeof(error)) ==
              MAP_SCRIPT_CANDIDATE_DISPATCHED);
        CHECK(fabsf(objects[i].vy - expected) < 0.00001f);
        rise = simulate_native_apex(objects[i].vy, gravity);
        CHECK(rise >= 48.0f && rise <= 64.0f);
    }

    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 3);
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        const MapScriptSnapshotVelocityLimit* limit = find_velocity_limit(
            &snapshot, objects[i].object_id, objects[i].lifecycle_id);
        float expected = kinds[i] == MAP_SCRIPT_OBJECT_PLAYER ? -4.0f : -2.8f;
        CHECK(limit != NULL);
        if (limit) {
            CHECK(limit->object_kind == kinds[i]);
            CHECK(limit->bound_flags == MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY);
            CHECK(fabsf(limit->min_vy - expected) < 0.00001f);
            CHECK(limit->expires_after_tick == 9);
        }
    }
    map_script_deactivate();
}

static void test_dead_body_is_ignored(const MapScriptDefinition* definition) {
    SpringHost host;
    MapScriptObjectView corpse;
    MapScriptCellCandidateView candidate;
    MapScriptSnapshot snapshot;
    char error[512];
    int tick;

    if (!activate_spring(definition, &host, error, sizeof(error))) return;
    initialize_surface_candidate(&candidate, &corpse,
                                 MAP_SCRIPT_OBJECT_DEAD_BODY, 150);
    corpse.vy = 0.75f;

    /* The native respawn gate may wait for a corpse to settle. A spring must
     * never create or renew contact/launch state for a dead player record. */
    for (tick = 0; tick < 12; ++tick) {
        CHECK(map_script_update_object(&corpse, error, sizeof(error)));
        CHECK(map_script_dispatch_cell_candidate(&candidate, error,
                                                  sizeof(error)) ==
              MAP_SCRIPT_CANDIDATE_OUTSIDE);
        CHECK(corpse.vy == 0.75f);
        CHECK(map_script_dispatch_tick(error, sizeof(error)));
    }

    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.state_count == 0);
    CHECK(snapshot.contact_count == 0);
    CHECK(snapshot.override_count == 0);
    CHECK(snapshot.velocity_limit_count == 0);
    CHECK(host.apply_count == 0);
    map_script_deactivate();
}

static void test_delayed_kick_limit(const MapScriptDefinition* definition) {
    SpringHost host;
    MapScriptObjectView player;
    MapScriptCellCandidateView candidate;
    MapScriptSnapshot launched;
    MapScriptSnapshot baseline;
    MapScriptSnapshot future_a;
    MapScriptSnapshot future_b;
    const MapScriptSnapshotVelocityLimit* limit;
    char error[512];

    if (!activate_spring(definition, &host, error, sizeof(error))) return;
    initialize_surface_candidate(&candidate, &player, MAP_SCRIPT_OBJECT_PLAYER, 200);
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(player.vy == -4.0f);
    CHECK(map_script_snapshot_save(&launched, error, sizeof(error)));
    CHECK(launched.tick == 0);
    CHECK(launched.velocity_limit_count == 1);
    limit = find_velocity_limit(&launched, player.object_id, player.lifecycle_id);
    CHECK(limit != NULL);
    if (limit) {
        CHECK(limit->object_kind == MAP_SCRIPT_OBJECT_PLAYER);
        CHECK(limit->bound_flags == MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY);
        CHECK(limit->min_vy == -4.0f);
        CHECK(limit->expires_after_tick == 9);
    }

    /* Finish the contact tick, then omit the spring candidate to produce a
     * real leave. One additional empty tick ensures the native kick model is
     * exercised after both on_contact and on_leave are already unavailable. */
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_update_object(&player, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(host.apply_count == 1);
    CHECK(host.last_applied.object_id == player.object_id);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_tick_count() == 3);
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(baseline.contact_count == 0);
    CHECK(baseline.velocity_limit_count == 1);

    /* Model the airborne unarmed kick's extra -1.5 on a later native tick.
     * The rollback-owned policy must still cap the launch after contact ends. */
    player.vy = -4.0f;
    player.vy -= 1.5f;
    CHECK(player.vy == -5.5f);
    CHECK(map_script_update_object(&player, error, sizeof(error)));
    CHECK(player.vy == -4.0f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future_a, error, sizeof(error)));

    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    player.vy = -4.0f;
    player.vy -= 1.5f;
    CHECK(player.vy == -5.5f);
    CHECK(map_script_update_object(&player, error, sizeof(error)));
    CHECK(player.vy == -4.0f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future_b, error, sizeof(error)));
    CHECK(memcmp(&future_a, &future_b, sizeof(future_a)) == 0);

    while (map_script_tick_count() < 9) {
        CHECK(map_script_dispatch_tick(error, sizeof(error)));
    }
    CHECK(map_script_snapshot_save(&launched, error, sizeof(error)));
    CHECK(launched.velocity_limit_count == 0);
    player.vy = -4.0f;
    player.vy -= 1.5f;
    CHECK(player.vy == -5.5f);
    CHECK(map_script_update_object(&player, error, sizeof(error)));
    CHECK(player.vy == -5.5f);
    map_script_deactivate();
}

int main(void) {
    static const MapScriptTileBinding bindings[] = {
        { '>', "v2_symbolic_demo:spring" },
        { '}', "v2_symbolic_demo:fan" }
    };
    const char* source_path = "maps/v2_symbolic_demo/map.lua";
    char* source;
    size_t source_length;
    MapScriptDefinition definition;
    char error[512];

    source = read_source(source_path, &source_length);
    if (!source) return 1;
    definition = spring_definition(source, source_length, bindings,
                                   sizeof(bindings) / sizeof(bindings[0]));
    if (!map_script_validate(&definition, error, sizeof(error))) {
        fprintf(stderr, "FAIL: checked-in spring script did not validate: %s\n", error);
        ++g_failures;
    } else {
        test_launch_profiles(&definition);
        test_dead_body_is_ignored(&definition);
        test_delayed_kick_limit(&definition);
    }
    map_script_deactivate();
    free(source);
    if (g_failures != 0) {
        fprintf(stderr, "%d spring demo test(s) failed\n", g_failures);
        return 1;
    }
    puts("spring demo fixture tests passed");
    return 0;
}
