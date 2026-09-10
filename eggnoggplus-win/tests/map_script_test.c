#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../map_script.h"

static int g_failures;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        ++g_failures; \
    } \
} while (0)

typedef struct TestHost {
    int log_count;
    int apply_count;
    char last_log[512];
    MapScriptObjectView applied;
} TestHost;

static void test_log(void* userdata, const char* message) {
    TestHost* host = (TestHost*)userdata;
    ++host->log_count;
    snprintf(host->last_log, sizeof(host->last_log), "%s", message ? message : "");
}

static void test_apply(void* userdata, const MapScriptObjectView* object) {
    TestHost* host = (TestHost*)userdata;
    ++host->apply_count;
    host->applied = *object;
}

static MapScriptDefinition make_definition(uint64_t id,
                                           const char* source,
                                           const MapScriptTileBinding* bindings,
                                           size_t binding_count) {
    MapScriptDefinition definition;
    memset(&definition, 0, sizeof(definition));
    definition.script_id = id;
    definition.chunk_name = "@tests/map.lua";
    definition.source = source;
    definition.source_len = strlen(source);
    definition.bindings = bindings;
    definition.binding_count = binding_count;
    return definition;
}

static const MapScriptSnapshotStateEntry* state_entry(const MapScriptSnapshot* snapshot,
                                                       const char* key) {
    size_t length = strlen(key);
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_STATE_ENTRIES; ++i) {
        const MapScriptSnapshotStateEntry* entry = &snapshot->state[i];
        if (entry->in_use && entry->key_len == length &&
            memcmp(entry->key, key, length) == 0) return entry;
    }
    return NULL;
}

static const MapScriptSnapshotContact* first_snapshot_contact(
        const MapScriptSnapshot* snapshot) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_CONTACTS; ++i) {
        if (snapshot->contacts[i].in_use) return &snapshot->contacts[i];
    }
    return NULL;
}

static const MapScriptSnapshotVelocityLimit* first_velocity_limit(
        const MapScriptSnapshot* snapshot) {
    int i;
    for (i = 0; i < MAP_SCRIPT_MAX_VELOCITY_LIMITS; ++i) {
        if (snapshot->velocity_limits[i].in_use) {
            return &snapshot->velocity_limits[i];
        }
    }
    return NULL;
}

static void expect_validation_failure(uint64_t id,
                                      const char* source,
                                      const MapScriptTileBinding* bindings,
                                      size_t binding_count,
                                      const char* expected_text) {
    MapScriptDefinition definition = make_definition(id, source, bindings, binding_count);
    char error[512];
    CHECK(!map_script_validate(&definition, error, sizeof(error)));
    if (strstr(error, expected_text) == NULL) {
        fprintf(stderr, "validation error was: %s (expected text: %s)\n",
                error, expected_text);
    }
    CHECK(strstr(error, expected_text) != NULL);
}

static void test_inactive_snapshot(void) {
    MapScriptSnapshot snapshot;
    MapScriptSnapshot corrupted;
    char error[256];
    map_script_deactivate();
    CHECK(map_script_snapshot_size() == sizeof(MapScriptSnapshot));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.script_id == 0);
    CHECK(map_script_snapshot_validate(&snapshot, 0, error, sizeof(error)));
    CHECK(map_script_snapshot_load(&snapshot, error, sizeof(error)));
    corrupted = snapshot;
    corrupted.reserved[0] = 1;
    CHECK(!map_script_snapshot_validate(&corrupted, 0, error, sizeof(error)));
}

static void test_validation_barriers(const MapScriptTileBinding* bindings,
                                     size_t binding_count) {
    static const char bytecode[] = { 0x1b, 'L', 'J', 0 };
    static const char harmless_carets[] =
        "map.state.short = '^'\n"
        "map.state.long = [=[caret ^ in a long string]=]\n"
        "-- caret ^ in a line comment\n"
        "--[=[ caret ^ in a long comment ]=]\n";
    MapScriptDefinition bytecode_definition = make_definition(90, bytecode, bindings,
                                                               binding_count);
    MapScriptDefinition harmless_definition = make_definition(
        100, harmless_carets, bindings, binding_count);
    char error[512];
    bytecode_definition.source_len = 3;
    CHECK(!map_script_validate(&bytecode_definition, error, sizeof(error)));
    CHECK(strstr(error, "bytecode") != NULL);

    expect_validation_failure(91,
        "map.on_contact('?', function(object, tile) end)",
        bindings, binding_count, "unknown map tile");
    expect_validation_failure(92,
        "map.on_contact('S', function(object, tile) end)\n"
        "map.on_contact('demo:spring', function(object, tile) end)",
        bindings, binding_count, "duplicate callback");
    expect_validation_failure(93,
        "local hidden = 0\n"
        "map.on_tick(function() hidden = hidden + 1 end)",
        bindings, binding_count, "upvalues");
    expect_validation_failure(94,
        "cache = {}\nmap.on_tick(function() end)",
        bindings, binding_count, "global table 'cache'");
    expect_validation_failure(95,
        "math.abs = function(value) return value end",
        bindings, binding_count, "read-only");
    expect_validation_failure(96,
        "table.insert(math, { n = 0 })",
        bindings, binding_count, "nil value");
    expect_validation_failure(97, "map = nil", bindings, binding_count,
                              "replaced reserved table 'map'");
    expect_validation_failure(98, "math = 1", bindings, binding_count,
                              "replaced reserved table 'math'");
    expect_validation_failure(99, "while true do end", bindings, binding_count,
                              "instruction budget");
    expect_validation_failure(101,
        "map.on_tick(function() map.state.power = 2 ^ 8 end)",
        bindings, binding_count, "power operator");
    CHECK(map_script_validate(&harmless_definition, error, sizeof(error)));
}

static void test_sensor_validation(const MapScriptTileBinding* bindings,
                                   size_t binding_count) {
    char error[512];
    MapScriptDefinition valid = make_definition(420,
        "map.sensor('S', {\n"
        "  tile_box = { left = 0, top = 0.75, right = 1, bottom = 1 },\n"
        "  object_box = 'feet',\n"
        "  objects = { 'alive_player', 'dead_body', 'sword', 'hazard' },\n"
        "  mirror_with_room = true, contact_scope = 'binding'\n"
        "})\n",
        bindings, binding_count);
    MapScriptDefinition split_players = make_definition(441,
        "map.sensor('S', { objects = { 'alive_player', 'dead_body' } })",
        bindings, binding_count);
    CHECK(MAP_SCRIPT_API_VERSION == UINT32_C(27));
    CHECK(map_script_validate(&valid, error, sizeof(error)));
    CHECK(map_script_validate(&split_players, error, sizeof(error)));
    expect_validation_failure(421,
        "map.sensor('?', {})", bindings, binding_count, "unknown map tile");
    expect_validation_failure(422,
        "map.sensor('S', {})\nmap.sensor('demo:spring', {})",
        bindings, binding_count, "duplicate map.sensor");
    expect_validation_failure(423,
        "map.sensor('S', { mystery = true })",
        bindings, binding_count, "unknown field 'mystery'");
    expect_validation_failure(424,
        "map.sensor('S', { tile_box = { left=0, top=0, right=1 } })",
        bindings, binding_count, "requires left, top, right, and bottom");
    expect_validation_failure(425,
        "map.sensor('S', { tile_box = { left=0, top=0, right=1, bottom=1, x=0 } })",
        bindings, binding_count, "unknown field 'x'");
    expect_validation_failure(426,
        "map.sensor('S', { tile_box = { left=0.5001, top=0, right=0.5002, bottom=1 } })",
        bindings, binding_count, "positive width and height");
    expect_validation_failure(427,
        "map.sensor('S', { tile_box = { left=-2.01, top=0, right=1, bottom=1 } })",
        bindings, binding_count, "supported range");
    expect_validation_failure(428,
        "map.sensor('S', { tile_box = { left=0/0, top=0, right=1, bottom=1 } })",
        bindings, binding_count, "non-finite");
    expect_validation_failure(429,
        "map.sensor('S', { object_box = 'head' })",
        bindings, binding_count, "unknown profile 'head'");
    expect_validation_failure(430,
        "map.sensor('S', { object_box = { left=0, top=0, right=0, bottom=1 } })",
        bindings, binding_count, "positive width and height");
    expect_validation_failure(431,
        "map.sensor('S', { object_box = { left=-2.1, top=0, right=1, bottom=1 } })",
        bindings, binding_count, "supported range");
    expect_validation_failure(432,
        "map.sensor('S', { objects = {} })",
        bindings, binding_count, "cannot be empty");
    expect_validation_failure(433,
        "map.sensor('S', { objects = { [2] = 'player' } })",
        bindings, binding_count, "must not be sparse");
    expect_validation_failure(434,
        "map.sensor('S', { objects = { 'player', 'player' } })",
        bindings, binding_count, "duplicate kind");
    expect_validation_failure(435,
        "map.sensor('S', { objects = { 'crate' } })",
        bindings, binding_count, "unknown kind 'crate'");
    expect_validation_failure(436,
        "map.sensor('S', { objects = { 1 } })",
        bindings, binding_count, "entries must be");
    expect_validation_failure(437,
        "map.sensor('S', { mirror_with_room = 1 })",
        bindings, binding_count, "must be boolean");
    expect_validation_failure(438,
        "map.sensor('S', { object_box = 'feet\\000extra' })",
        bindings, binding_count, "unknown profile");
    expect_validation_failure(439,
        "map.sensor('S', { objects = { 'player\\000extra' } })",
        bindings, binding_count, "unknown kind");
    expect_validation_failure(440,
        "map.sensor('S', { ['tile_box\\000extra'] = {} })",
        bindings, binding_count, "unknown field");
    expect_validation_failure(442,
        "map.sensor('S', { objects = { 'player', 'alive_player' } })",
        bindings, binding_count, "duplicate kind");
    expect_validation_failure(443,
        "map.sensor('S', { objects = { 'player', 'dead_body' } })",
        bindings, binding_count, "duplicate kind");
    expect_validation_failure(444,
        "map.sensor('S', { objects = { 'hazard', 'hazard' } })",
        bindings, binding_count, "duplicate kind");
    expect_validation_failure(445,
        "map.sensor('S', { contact_scope = 'room' })",
        bindings, binding_count, "must be 'cell' or 'binding'");
    expect_validation_failure(446,
        "map.sensor('S', { contact_scope = 1 })",
        bindings, binding_count, "map.sensor contact_scope");
    expect_validation_failure(447,
        "map.sensor('S', { contact_scope = 'binding\\000extra' })",
        bindings, binding_count, "must be 'cell' or 'binding'");
}

static void initialize_candidate(MapScriptCellCandidateView* candidate,
                                 MapScriptObjectView* object,
                                 int object_kind,
                                 float contact_radius,
                                 float sensor_x,
                                 float sensor_y,
                                 uint32_t cell_index,
                                 int32_t tile_x,
                                 int32_t tile_y,
                                 char symbol,
                                 const char* key) {
    memset(candidate, 0, sizeof(*candidate));
    candidate->object = object;
    object->object_kind = (uint8_t)object_kind;
    candidate->object_kind = object_kind;
    candidate->contact_radius = contact_radius;
    candidate->sensor_x = sensor_x;
    candidate->sensor_y = sensor_y;
    candidate->cell_index = cell_index;
    candidate->tile_x = tile_x;
    candidate->tile_y = tile_y;
    candidate->tile_width = 16;
    candidate->tile_height = 16;
    candidate->symbol = symbol;
    candidate->qualified_key = key;
}

static void test_sensor_runtime(const MapScriptTileBinding* bindings,
                                size_t binding_count) {
    static const char source[] =
        "map.sensor('S', {\n"
        "  tile_box={left=0, top=0.75, right=1, bottom=1},\n"
        "  object_box='feet', objects={'player'}\n"
        "})\n"
        "map.sensor('F', { object_box='body', objects={'sword'} })\n"
        "map.sensor(' ', {\n"
        "  tile_box={left=0, top=0, right=0.25, bottom=1},\n"
        "  object_box='center', objects={'player'}, mirror_with_room=true\n"
        "})\n"
        "map.on_enter('S', function(object, tile)\n"
        "  map.state.kind = object.kind\n"
        "  map.state.id = object.id\n"
        "  map.state.radius = object.contact_radius\n"
        "  object.vy = -9\n"
        "end)\n"
        "map.on_contact('F', function(object, tile) object.vx = 3 end)\n"
        "map.on_contact(' ', function(object, tile)\n"
        "  map.state.mirrored = tile.mirrored\n"
        "end)\n";
    static const char custom_source[] =
        "map.sensor('F', {\n"
        "  object_box={left=-0.5, top=-0.5, right=0.5, bottom=0.5},\n"
        "  objects={'sword'}\n"
        "})\n"
        "map.on_contact('F', function(object, tile) object.vy = object.vy + 2 end)\n";
    static const char readonly_source[] =
        "map.sensor('S', { objects={'player'} })\n"
        "map.on_contact('S', function(object, tile) object.kind = 'sword' end)\n";
    MapScriptDefinition definition = make_definition(450, source, bindings, binding_count);
    MapScriptObjectView player;
    MapScriptObjectView sword;
    MapScriptCellCandidateView candidate;
    MapScriptSnapshot snapshot;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];

    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    CHECK(map_script_binding_has_sensor("DEMO:SPRING"));
    CHECK(map_script_binding_has_sensor("demo:fan"));
    CHECK(map_script_binding_has_sensor("demo:background"));
    CHECK(!map_script_binding_has_sensor("demo:missing"));
    CHECK(!map_script_binding_has_sensor("not a key"));

    memset(&player, 0, sizeof(player));
    player.object_id = 0;
    player.x = 31.0f;
    player.y = 22.0f;
    initialize_candidate(&candidate, &player, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         31.0f, 22.0f, 100, 1, 1, 'S', "DEMO:SPRING");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(player.vy == -9.0f);
    /* A full-width feet segment reaches the spring from its right side, but
     * stops triggering even one fixed-point step above the authored surface. */
    candidate.sensor_y = 21.99f;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);

    memset(&sword, 0, sizeof(sword));
    sword.object_id = 2;
    initialize_candidate(&candidate, &sword, MAP_SCRIPT_OBJECT_SWORD,
                         MAP_SCRIPT_SWORD_CONTACT_RADIUS,
                         31.0f, 22.0f, 101, 1, 1, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);

    sword.x = 20.0f;
    sword.y = 8.0f;
    initialize_candidate(&candidate, &sword, MAP_SCRIPT_OBJECT_SWORD,
                         MAP_SCRIPT_SWORD_CONTACT_RADIUS,
                         20.0f, 8.0f, 102, 0, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(sword.vx == 3.0f); /* body right edge touches the tile at x=16 */

    memset(&player, 0, sizeof(player));
    player.object_id = 1;
    player.x = 30.0f;
    player.y = 8.0f;
    initialize_candidate(&candidate, &player, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         30.0f, 8.0f, 103, 1, 0, ' ', "demo:background");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);
    candidate.room_mirrored = 1;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    entry = state_entry(&snapshot, "kind");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_STRING &&
          entry->string_len == 6 && memcmp(entry->string_value, "player", 6) == 0);
    entry = state_entry(&snapshot, "id");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_NUMBER && entry->number_value == 0.0);
    entry = state_entry(&snapshot, "radius");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_NUMBER && entry->number_value == 6.0);
    entry = state_entry(&snapshot, "mirrored");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_BOOL && entry->bool_value == 1);

    definition = make_definition(451, custom_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&player, 0, sizeof(player));
    player.object_id = 0;
    initialize_candidate(&candidate, &player, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         8.0f, 8.0f, 110, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_NO_SENSOR);
    CHECK(!map_script_binding_has_sensor("demo:spring"));
    memset(&sword, 0, sizeof(sword));
    sword.object_id = 2;
    sword.x = 20.0f;
    sword.y = 8.0f;
    initialize_candidate(&candidate, &sword, MAP_SCRIPT_OBJECT_SWORD,
                         MAP_SCRIPT_SWORD_CONTACT_RADIUS,
                         20.0f, 8.0f, 111, 0, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(sword.vy == 2.0f); /* custom half-tile AABB reaches back to x=12 */
    candidate.contact_radius = MAP_SCRIPT_PLAYER_CONTACT_RADIUS;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_ERROR);
    CHECK(strstr(error, "native object profile") != NULL);

    definition = make_definition(452, readonly_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&player, 0, sizeof(player));
    player.object_id = 0;
    initialize_candidate(&candidate, &player, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         8.0f, 8.0f, 120, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_ERROR);
    CHECK(map_script_is_faulted());
    CHECK(strstr(error, "read-only") != NULL);
}

static void test_object_profiles(const MapScriptTileBinding* bindings,
                                 size_t binding_count) {
    static const char source[] =
        "map.sensor('S', { objects={'player'} })\n"
        "map.sensor('F', { objects={'alive_player','hazard'} })\n"
        "map.on_contact('S', function(object, tile)\n"
        "  map.state.kind = object.kind\n"
        "  map.state.radius = object.contact_radius\n"
        "end)\n"
        "map.on_contact('F', function(object, tile)\n"
        "  map.state.kind = object.kind\n"
        "  map.state.radius = object.contact_radius\n"
        "end)\n";
    MapScriptDefinition definition = make_definition(453, source, bindings,
                                                       binding_count);
    MapScriptObjectView object;
    MapScriptCellCandidateView candidate;
    MapScriptSnapshot snapshot;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];

    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));

    /* The backwards-compatible player selector deliberately includes corpses. */
    memset(&object, 0, sizeof(object));
    object.object_id = 0;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_DEAD_BODY,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         8.0f, 8.0f, 130, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);

    /* The strict live selector excludes that same dead body. */
    candidate.cell_index = 131;
    candidate.tile_x = 1;
    candidate.symbol = 'F';
    candidate.qualified_key = "demo:fan";
    candidate.sensor_x = 24.0f;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);

    memset(&object, 0, sizeof(object));
    object.object_id = 1;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         24.0f, 8.0f, 132, 1, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);

    /* K hazards are explicit radius-zero point objects, never part of defaults. */
    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_HAZARD,
                         MAP_SCRIPT_HAZARD_CONTACT_RADIUS,
                         40.0f, 8.0f, 133, 2, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    entry = state_entry(&snapshot, "kind");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_STRING &&
          entry->string_len == 6 && memcmp(entry->string_value, "hazard", 6) == 0);
    entry = state_entry(&snapshot, "radius");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_NUMBER &&
          entry->number_value == 0.0);

    /* Kind, id domain, radius, and reserved bytes are one validated profile. */
    memset(&object, 0, sizeof(object));
    object.object_id = 0;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_HAZARD,
                         MAP_SCRIPT_HAZARD_CONTACT_RADIUS,
                         8.0f, 8.0f, 134, 0, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_ERROR);
    CHECK(strstr(error, "invalid native object profile") != NULL);

    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_HAZARD,
                         1.0f, 8.0f, 8.0f, 135, 0, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_ERROR);
    CHECK(strstr(error, "invalid native object profile") != NULL);

    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_HAZARD,
                         MAP_SCRIPT_HAZARD_CONTACT_RADIUS,
                         8.0f, 8.0f, 136, 0, 0, 'F', "demo:fan");
    candidate.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_ERROR);
    CHECK(strstr(error, "invalid native object profile") != NULL);

    candidate.object_kind = MAP_SCRIPT_OBJECT_HAZARD;
    object.reserved[0] = 1;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_ERROR);
    CHECK(strstr(error, "invalid native object profile") != NULL);
    CHECK(!map_script_update_object(&object, error, sizeof(error)));
    CHECK(strstr(error, "valid native object profile") != NULL);
}

static void test_spring_probe_geometry(const MapScriptTileBinding* bindings,
                                       size_t binding_count) {
    static const char source[] =
        "map.sensor('S', {\n"
        "  tile_box={left=0,top=0,right=1,bottom=0.375},\n"
        "  object_box={left=-0.375,top=0,right=0.375,bottom=0.375},\n"
        "  objects={'player','sword','hazard'}, contact_scope='binding'\n"
        "})\n"
        "map.on_enter('S', function(object, tile) object.vy = -15 end)\n";
    MapScriptDefinition definition = make_definition(458, source, bindings,
                                                       binding_count);
    MapScriptObjectView object;
    MapScriptCellCandidateView candidate;
    char error[512];

    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));

    /* The lower probe first touches the six-pixel band at center_y=10. It
     * remains outside one fixed-point-safe step above that boundary. */
    memset(&object, 0, sizeof(object));
    object.object_id = 0;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         24.0f, 9.99f, 160, 1, 1, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);
    candidate.sensor_y = 10.0f;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(object.vy == -15.0f);

    /* A grounded player walking horizontally reaches the pad with the same
     * six-pixel side edge; it was missed by the former four-pixel tile band. */
    memset(&object, 0, sizeof(object));
    object.object_id = 1;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         9.99f, 15.85f, 161, 1, 1, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);
    candidate.sensor_x = 10.0f;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(object.vy == -15.0f);

    /* The custom box—not native radius—gives K's radius-zero point mass the
     * same downward reach, with the identical no-premature boundary. */
    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_HAZARD,
                         MAP_SCRIPT_HAZARD_CONTACT_RADIUS,
                         24.0f, 9.99f, 162, 1, 1, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);
    candidate.sensor_y = 10.0f;
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(object.vy == -15.0f);
}

static void test_binding_scope_runtime(const MapScriptTileBinding* bindings,
                                       size_t binding_count) {
    static const char source[] =
        "map.state.enters = 0\n"
        "map.state.contacts = 0\n"
        "map.state.leaves = 0\n"
        "map.sensor('S', {\n"
        "  object_box='center', objects={'hazard'}, contact_scope='binding'\n"
        "})\n"
        "map.on_enter('S', function(object, tile)\n"
        "  map.state.enters = map.state.enters + 1\n"
        "  map.state.enter_x = tile.x\n"
        "  object.vy = -15\n"
        "end)\n"
        "map.on_contact('S', function(object, tile)\n"
        "  map.state.contacts = map.state.contacts + 1\n"
        "  map.state.contact_x = tile.x\n"
        "  if object.vy < -15 then object.vy = -15 end\n"
        "end)\n"
        "map.on_leave('S', function(object, tile)\n"
        "  map.state.leaves = map.state.leaves + 1\n"
        "  map.state.leave_x = tile.x\n"
        "  if object.vy < -15 then object.vy = -15 end\n"
        "end)\n";
    MapScriptDefinition definition = make_definition(454, source, bindings,
                                                       binding_count);
    MapScriptHost host_api;
    TestHost host;
    MapScriptObjectView hazard;
    MapScriptCellCandidateView candidate_a;
    MapScriptCellCandidateView candidate_b;
    MapScriptSnapshot first_tick;
    MapScriptSnapshot transition_a;
    MapScriptSnapshot transition_b;
    MapScriptSnapshot after_leave;
    const MapScriptSnapshotContact* tracked;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];

    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.apply_object_fn = test_apply;
    host_api.userdata = &host;
    CHECK(map_script_activate(&definition, &host_api, error, sizeof(error)));

    memset(&hazard, 0, sizeof(hazard));
    hazard.object_id = 2;
    hazard.lifecycle_id = 7;
    hazard.x = 16.0f;
    hazard.y = 8.0f;
    initialize_candidate(&candidate_a, &hazard, MAP_SCRIPT_OBJECT_HAZARD,
                         MAP_SCRIPT_HAZARD_CONTACT_RADIUS,
                         16.0f, 8.0f, 140, 0, 0, 'S', "demo:spring");
    initialize_candidate(&candidate_b, &hazard, MAP_SCRIPT_OBJECT_HAZARD,
                         MAP_SCRIPT_HAZARD_CONTACT_RADIUS,
                         16.0f, 8.0f, 141, 1, 0, 'S', "demo:spring");

    /* Inclusive edges touch both adjacent cells. Binding scope chooses the
     * first stable candidate and coalesces the second without another event. */
    CHECK(map_script_dispatch_cell_candidate(&candidate_a, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(hazard.vy == -15.0f);
    CHECK(map_script_dispatch_cell_candidate(&candidate_b, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&first_tick, error, sizeof(error)));
    CHECK(first_tick.contact_count == 1);
    tracked = first_snapshot_contact(&first_tick);
    CHECK(tracked && tracked->object_kind == MAP_SCRIPT_OBJECT_HAZARD);
    CHECK(tracked && tracked->contact_scope == MAP_SCRIPT_CONTACT_SCOPE_BINDING);
    CHECK(tracked && tracked->cell_index == 140 && tracked->tile_x == 0);
    entry = state_entry(&first_tick, "enters");
    CHECK(entry && entry->number_value == 1.0);
    entry = state_entry(&first_tick, "contacts");
    CHECK(entry && entry->number_value == 1.0);

    /* Moving from cell A to adjacent cell B is a stay on the binding. It also
     * models the native kick's next-tick -1.5: on_contact clamps it to -15. */
    hazard.vy = -16.5f;
    CHECK(map_script_update_object(&hazard, error, sizeof(error)));
    CHECK(map_script_dispatch_cell_candidate(&candidate_b, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(hazard.vy == -15.0f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&transition_a, error, sizeof(error)));
    CHECK(transition_a.contact_count == 1);
    tracked = first_snapshot_contact(&transition_a);
    CHECK(tracked && tracked->cell_index == 141 && tracked->tile_x == 1);
    CHECK(tracked && tracked->object_kind == MAP_SCRIPT_OBJECT_HAZARD);
    CHECK(tracked && tracked->contact_scope == MAP_SCRIPT_CONTACT_SCOPE_BINDING);
    entry = state_entry(&transition_a, "enters");
    CHECK(entry && entry->number_value == 1.0);
    entry = state_entry(&transition_a, "contacts");
    CHECK(entry && entry->number_value == 2.0);
    entry = state_entry(&transition_a, "leaves");
    CHECK(entry && entry->number_value == 0.0);

    /* The representative transition and velocity clamp replay byte-for-byte. */
    CHECK(map_script_snapshot_load(&first_tick, error, sizeof(error)));
    hazard.vy = -16.5f;
    CHECK(map_script_update_object(&hazard, error, sizeof(error)));
    CHECK(map_script_dispatch_cell_candidate(&candidate_b, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(hazard.vy == -15.0f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&transition_b, error, sizeof(error)));
    CHECK(memcmp(&transition_a, &transition_b, sizeof(transition_a)) == 0);

    /* No cell on the binding this tick produces one leave using B's current
     * metadata. The leave clamp catches a kick applied after exiting the pad. */
    memset(&host, 0, sizeof(host));
    hazard.vy = -16.5f;
    CHECK(map_script_update_object(&hazard, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(host.apply_count == 1);
    CHECK(host.applied.object_id == hazard.object_id);
    CHECK(host.applied.object_kind == MAP_SCRIPT_OBJECT_HAZARD);
    CHECK(host.applied.vy == -15.0f);
    CHECK(map_script_snapshot_save(&after_leave, error, sizeof(error)));
    CHECK(after_leave.contact_count == 0);
    entry = state_entry(&after_leave, "leaves");
    CHECK(entry && entry->number_value == 1.0);
    entry = state_entry(&after_leave, "leave_x");
    CHECK(entry && entry->number_value == 1.0);
}

static void test_player_kind_transitions(const MapScriptTileBinding* bindings,
                                         size_t binding_count) {
    static const char legacy_source[] =
        "map.state.enters = 0\n"
        "map.state.leaves = 0\n"
        "map.sensor('S', { objects={'player'}, contact_scope='binding' })\n"
        "map.on_enter('S', function(object, tile)\n"
        "  map.state.enters = map.state.enters + 1\n"
        "end)\n"
        "map.on_contact('S', function(object, tile)\n"
        "  map.state.kind = object.kind\n"
        "end)\n"
        "map.on_leave('S', function(object, tile)\n"
        "  map.state.leaves = map.state.leaves + 1\n"
        "end)\n";
    static const char alive_source[] =
        "map.sensor('S', { objects={'alive_player'} })\n"
        "map.on_leave('S', function(object, tile)\n"
        "  map.state.leave_kind = object.kind\n"
        "  if object.vy < -15 then object.vy = -15 end\n"
        "end)\n";
    static const char pooled_source[] =
        "map.sensor('F', { objects={'sword','hazard'} })\n"
        "map.on_contact('F', function(object, tile) end)\n";
    MapScriptDefinition definition;
    MapScriptHost host_api;
    TestHost host;
    MapScriptObjectView object;
    MapScriptCellCandidateView candidate;
    MapScriptSnapshot snapshot;
    const MapScriptSnapshotContact* tracked;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];

    /* The legacy player selector keeps one contact through live->dead. The
     * callback and rollback snapshot still receive the refreshed dead kind. */
    definition = make_definition(455, legacy_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 0;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         8.0f, 8.0f, 150, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    object.object_kind = MAP_SCRIPT_OBJECT_DEAD_BODY;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_DEAD_BODY,
                         MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS,
                         8.0f, 8.0f, 150, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.contact_count == 1);
    tracked = first_snapshot_contact(&snapshot);
    CHECK(tracked && tracked->object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY);
    entry = state_entry(&snapshot, "enters");
    CHECK(entry && entry->number_value == 1.0);
    entry = state_entry(&snapshot, "leaves");
    CHECK(entry && entry->number_value == 0.0);
    entry = state_entry(&snapshot, "kind");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_STRING &&
          entry->string_len == 9 &&
          memcmp(entry->string_value, "dead_body", 9) == 0);

    /* An alive-only sensor ends instead. Its synthesized leave sees the current
     * dead body and can safely clamp the post-launch kick before host apply. */
    definition = make_definition(456, alive_source, bindings, binding_count);
    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.apply_object_fn = test_apply;
    host_api.userdata = &host;
    CHECK(map_script_activate(&definition, &host_api, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 1;
    object.vy = -15.0f;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_PLAYER,
                         MAP_SCRIPT_PLAYER_CONTACT_RADIUS,
                         8.0f, 8.0f, 151, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    object.object_kind = MAP_SCRIPT_OBJECT_DEAD_BODY;
    object.vy = -16.5f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_DEAD_BODY,
                         MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS,
                         8.0f, 8.0f, 151, 0, 0, 'S', "demo:spring");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_OUTSIDE);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(host.apply_count == 1);
    CHECK(host.applied.object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY);
    CHECK(host.applied.vy == -15.0f);
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    entry = state_entry(&snapshot, "leave_kind");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_STRING &&
          entry->string_len == 9 &&
          memcmp(entry->string_value, "dead_body", 9) == 0);

    /* Pooled sword/hazard class changes require allocator lifecycle advance. */
    definition = make_definition(457, pooled_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    initialize_candidate(&candidate, &object, MAP_SCRIPT_OBJECT_SWORD,
                         MAP_SCRIPT_SWORD_CONTACT_RADIUS,
                         8.0f, 8.0f, 152, 0, 0, 'F', "demo:fan");
    CHECK(map_script_dispatch_cell_candidate(&candidate, error, sizeof(error)) ==
          MAP_SCRIPT_CANDIDATE_DISPATCHED);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    object.object_kind = MAP_SCRIPT_OBJECT_HAZARD;
    CHECK(!map_script_update_object(&object, error, sizeof(error)));
    CHECK(strstr(error, "changes an active native object kind") != NULL);
}

static void test_sprite_option_faults(const MapScriptTileBinding* bindings,
                                      size_t binding_count) {
    static const struct {
        const char* source;
        const char* expected;
    } cases[] = {
        {
            "map.on_enter('S', function(object, tile) tile:get_sprite(1) end)",
            "get_sprite expects no arguments"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, 'bad') end)",
            "options must be a table"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, { offset_z = 1 }) end)",
            "unknown field"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, { offset_y = 'up' }) end)",
            "must be a number"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, { offset_y = 4097 }) end)",
            "outside -4096..4096"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, { offset_y = 0/0 }) end)",
            "non-finite"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, { ['offset_y\\000extra'] = -7 }) end)",
            "unknown field"
        },
        {
            "map.on_enter('S', function(object, tile) "
            "tile:set_sprite(7, 2, {}, 4) end)",
            "optional options"
        }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        MapScriptDefinition definition = make_definition(
            UINT64_C(470) + (uint64_t)i, cases[i].source,
            bindings, binding_count);
        MapScriptObjectView object;
        MapScriptContactView contact;
        char error[512];
        CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
        memset(&object, 0, sizeof(object));
        object.object_id = 0;
        object.object_kind = MAP_SCRIPT_OBJECT_PLAYER;
        memset(&contact, 0, sizeof(contact));
        contact.object = &object;
        contact.cell_index = 44u;
        contact.symbol = 'S';
        contact.qualified_key = "demo:spring";
        CHECK(!map_script_dispatch_contact(&contact, error, sizeof(error)));
        CHECK(map_script_is_faulted());
        CHECK(strstr(error, cases[i].expected) != NULL);
        map_script_deactivate();
    }
}

static void test_demo_runtime(const MapScriptTileBinding* bindings,
                              size_t binding_count) {
    static const char source[] =
        "map.state.enters = 0\n"
        "map.state.ticks = 0\n"
        "map.on_enter('demo:spring', function(object, tile)\n"
        "  map.state.enters = map.state.enters + 1\n"
        "  map.state.object_name = tostring(object)\n"
        "  map.state.tile_name = tostring(tile)\n"
        "  object:set_velocity(object.vx, -8)\n"
        "  tile:set_sprite(7, 2, { offset_x = 1.25, offset_y = -7 })\n"
        "  local visual = tile:get_sprite()\n"
        "  assert(visual.sprite_index == 7 and visual.offset_x == 1.25 and visual.offset_y == -7 and visual.ticks_left == 3)\n"
        "  visual.sprite_index = 99\n"
        "  assert(tile:get_sprite().sprite_index == 7)\n"
        "  tile:reset_sprite()\n"
        "  assert(tile:get_sprite() == nil)\n"
        "  tile:set_sprite(7, 2, { offset_x = 1.25, offset_y = -7 })\n"
        "end)\n"
        "map.on_leave('S', function(object, tile)\n"
        "  object:add_velocity(0, 1)\n"
        "  map.state.left_key = tile.key\n"
        "end)\n"
        "map.on_contact('F', function(object, tile)\n"
        "  local force = tile.mirrored and -0.75 or 0.75\n"
        "  object.vx = math.max(-3, math.min(3, object.vx + force))\n"
        "end)\n"
        "map.on_tick(function()\n"
        "  map.state.ticks = map.state.ticks + 1\n"
        "  map.state.roll = map.random(1, 100)\n"
        "end)\n";
    MapScriptDefinition definition = make_definition(UINT64_C(0x123456789abcdef0),
                                                       source, bindings, binding_count);
    MapScriptHost host_api;
    TestHost host;
    MapScriptObjectView object;
    MapScriptContactView contact;
    MapScriptSnapshot baseline;
    MapScriptSnapshot future_a;
    MapScriptSnapshot future_b;
    const MapScriptSnapshotStateEntry* entry;
    MapScriptVisualOverride visual;
    int sprite = -1;
    uint64_t expires = 0;
    char error[512];

    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.rng_seed = UINT32_C(0x13579bdf);
    host_api.log_fn = test_log;
    host_api.apply_object_fn = test_apply;
    host_api.userdata = &host;
    if (!map_script_validate(&definition, error, sizeof(error))) {
        fprintf(stderr, "demo validation failed: %s\n", error);
        CHECK(0);
    }
    if (!map_script_activate(&definition, &host_api, error, sizeof(error))) {
        fprintf(stderr, "demo activation failed: %s\n", error);
        CHECK(0);
    }
    CHECK(map_script_is_active());
    CHECK(!map_script_is_faulted());
    CHECK(map_script_active_id() == definition.script_id);

    memset(&object, 0, sizeof(object));
    object.object_id = 42;
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    object.x = 10.0f;
    object.y = 20.0f;
    object.vx = 0.0f;
    object.vy = -0.0f;
    memset(&contact, 0, sizeof(contact));
    contact.object = &object;
    contact.cell_index = 10;
    contact.tile_x = 3;
    contact.tile_y = 4;
    contact.symbol = 'S';
    contact.qualified_key = "DEMO:SPRING";
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(object.vy == -8.0f);
    CHECK(map_script_sprite_override(10, &sprite, &expires));
    CHECK(sprite == 7 && expires == 3);
    CHECK(map_script_visual_override(10, &visual));
    CHECK(visual.sprite_index == 7 && visual.expires_after_tick == 3);
    CHECK(fabsf(visual.offset_x - 1.25f) < 0.00001f);
    CHECK(fabsf(visual.offset_y + 7.0f) < 0.00001f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_tick_count() == 1);

    object.vx = 0.5f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    contact.cell_index = 11;
    contact.tile_x = 4;
    contact.symbol = 'F';
    contact.qualified_key = "demo:fan";
    contact.room_mirrored = 0;
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(fabsf(object.vx - 1.25f) < 0.00001f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(host.apply_count == 1);
    CHECK(host.applied.object_id == 42);
    CHECK(fabsf(host.applied.vx - 1.25f) < 0.00001f);
    CHECK(host.applied.vy == -7.0f);

    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(map_script_snapshot_validate(&baseline, definition.script_id,
                                       error, sizeof(error)));
    entry = state_entry(&baseline, "enters");
    CHECK(entry && entry->type == MAP_SCRIPT_STATE_NUMBER && entry->number_value == 1.0);
    entry = state_entry(&baseline, "object_name");
    CHECK(entry && memcmp(entry->string_value, "object:42:0", 11) == 0);
    CHECK(map_script_sprite_override(10, &sprite, &expires));
    CHECK(map_script_visual_override(10, &visual));
    CHECK(fabsf(visual.offset_x - 1.25f) < 0.00001f &&
          fabsf(visual.offset_y + 7.0f) < 0.00001f);

    object = host.applied;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    contact.object = &object;
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future_a, error, sizeof(error)));
    CHECK(!map_script_sprite_override(10, &sprite, &expires));
    CHECK(!map_script_visual_override(10, &visual));

    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    object = host.applied;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    contact.object = &object;
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future_b, error, sizeof(error)));
    CHECK(memcmp(&future_a, &future_b, sizeof(future_a)) == 0);

    /* The same fan reverses without map-coordinate assumptions in a mirrored room. */
    object.object_id = 77;
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    object.x = 0.0f;
    object.y = 0.0f;
    object.vx = 1.0f;
    object.vy = 0.0f;
    contact.object = &object;
    contact.cell_index = 12;
    contact.tile_x = 5;
    contact.room_mirrored = 1;
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(fabsf(object.vx - 0.25f) < 0.00001f);
    CHECK(host.log_count == 0);
}

static uint32_t test_snapshot_checksum(const MapScriptSnapshot* snapshot) {
    const unsigned char* bytes = (const unsigned char*)snapshot;
    const size_t checksum_begin = offsetof(MapScriptSnapshot, checksum);
    const size_t checksum_end = checksum_begin + sizeof(snapshot->checksum);
    uint32_t crc = UINT32_MAX;
    size_t i;
    int bit;
    for (i = 0; i < sizeof(*snapshot); ++i) {
        uint8_t value = (i >= checksum_begin && i < checksum_end) ? 0 : bytes[i];
        crc ^= value;
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static void test_velocity_limits(const MapScriptTileBinding* bindings,
                                 size_t binding_count) {
    static const char source[] =
        "map.on_enter('S', function(object, tile)\n"
        "  object:set_velocity_limits({min_vx=-2,max_vx=2,min_vy=-15,max_vy=15}, 2)\n"
        "  object:set_velocity(63, -63)\n"
        "end)\n"
        "map.on_contact('S', function(object, tile)\n"
        "  object:add_velocity(63, -63)\n"
        "end)\n"
        "map.on_leave('S', function(object, tile)\n"
        "  object:set_velocity(63, -63)\n"
        "end)\n";
    static const char transition_source[] =
        "map.on_contact('S', function(object, tile)\n"
        "  object:set_velocity_limits({max_vy=4}, 8)\n"
        "  object.vy = 40\n"
        "end)\n";
    static const char clear_source[] =
        "map.on_contact('F', function(object, tile)\n"
        "  object:set_velocity_limits({min_vy=-8}, 8)\n"
        "  object:clear_velocity_limits()\n"
        "  object:clear_velocity_limits()\n"
        "  object.vy = -60\n"
        "end)\n";
    static const char fault_source[] =
        "map.state.armed = false\n"
        "map.on_contact('S', function(object, tile)\n"
        "  if map.state.armed then error('intentional limit fault') end\n"
        "  object:set_velocity_limits({min_vy=-8}, 8)\n"
        "  map.state.armed = true\n"
        "end)\n";
    MapScriptDefinition definition = make_definition(480, source, bindings,
                                                       binding_count);
    MapScriptHost host_api;
    TestHost host;
    MapScriptObjectView object;
    MapScriptContactView contact;
    MapScriptSnapshot protected_state;
    MapScriptSnapshot snapshot;
    MapScriptSnapshot corrupted;
    const MapScriptSnapshotVelocityLimit* limit;
    char error[512];

    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.apply_object_fn = test_apply;
    host_api.userdata = &host;
    CHECK(MAP_SCRIPT_SNAPSHOT_VERSION == 6u);
    CHECK(map_script_activate(&definition, &host_api, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    object.lifecycle_id = map_script_object_lifecycle_current(0);
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    memset(&contact, 0, sizeof(contact));
    contact.object = &object;
    contact.cell_index = 400;
    contact.tile_x = 1;
    contact.tile_y = 1;
    contact.symbol = 'S';
    contact.qualified_key = "demo:spring";
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    /* The enter callback installs and immediately applies the policy; the
     * later contact callback cannot bypass it in the same dispatch. */
    CHECK(object.vx == 2.0f && object.vy == -15.0f);
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 1);
    limit = first_velocity_limit(&snapshot);
    CHECK(limit && limit->object_id == 2 && limit->lifecycle_id == 0 &&
          limit->object_kind == MAP_SCRIPT_OBJECT_SWORD &&
          limit->bound_flags ==
              (MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX |
               MAP_SCRIPT_VELOCITY_LIMIT_MAX_VX |
               MAP_SCRIPT_VELOCITY_LIMIT_MIN_VY |
               MAP_SCRIPT_VELOCITY_LIMIT_MAX_VY) &&
          limit->min_vx == -2.0f && limit->max_vx == 2.0f &&
          limit->min_vy == -15.0f && limit->max_vy == 15.0f &&
          limit->expires_after_tick == 3);

    /* Validation covers count, absent-bound canonical zero, and the exact
     * snapshotted pool generation rather than accepting a stale policy. */
    corrupted = snapshot;
    corrupted.velocity_limit_count = 0;
    corrupted.checksum = test_snapshot_checksum(&corrupted);
    CHECK(!map_script_snapshot_validate(&corrupted, definition.script_id,
                                        error, sizeof(error)));
    corrupted = snapshot;
    corrupted.velocity_limits[0].bound_flags &=
        (uint8_t)~MAP_SCRIPT_VELOCITY_LIMIT_MIN_VX;
    corrupted.checksum = test_snapshot_checksum(&corrupted);
    CHECK(!map_script_snapshot_validate(&corrupted, definition.script_id,
                                        error, sizeof(error)));
    corrupted = snapshot;
    corrupted.velocity_limits[0].lifecycle_id = 1;
    corrupted.checksum = test_snapshot_checksum(&corrupted);
    CHECK(!map_script_snapshot_validate(&corrupted, definition.script_id,
                                        error, sizeof(error)));

    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&protected_state, error, sizeof(error)));
    object.vx = -60.0f;
    object.vy = 60.0f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    CHECK(object.vx == -2.0f && object.vy == 15.0f);
    object.object_kind = MAP_SCRIPT_OBJECT_HAZARD;
    CHECK(!map_script_update_object(&object, error, sizeof(error)));
    CHECK(strstr(error, "native object kind") != NULL);
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;

    /* A synthesized leave runs after the native update. Its callback is also
     * clamped before the host receives the object. */
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(host.apply_count == 1);
    CHECK(host.applied.vx == 2.0f && host.applied.vy == -15.0f);
    object = host.applied;
    object.vx = -60.0f;
    object.vy = 60.0f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    CHECK(object.vx == -2.0f && object.vy == 15.0f);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 0);
    object.vx = -60.0f;
    object.vy = 60.0f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    CHECK(object.vx == -60.0f && object.vy == 60.0f);

    /* Rollback restores the policy itself, not merely the currently clamped
     * velocity, and reproduces its remaining deterministic lifetime. */
    CHECK(map_script_snapshot_load(&protected_state, error, sizeof(error)));
    object.vx = -60.0f;
    object.vy = 60.0f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    CHECK(object.vx == -2.0f && object.vy == 15.0f);

    definition = make_definition(481, transition_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 0;
    object.object_kind = MAP_SCRIPT_OBJECT_PLAYER;
    contact.object = &object;
    contact.cell_index = 401;
    contact.symbol = 'S';
    contact.qualified_key = "demo:spring";
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(object.vy == 4.0f);
    object.object_kind = MAP_SCRIPT_OBJECT_DEAD_BODY;
    object.vy = 40.0f;
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    CHECK(object.vy == 4.0f);
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    limit = first_velocity_limit(&snapshot);
    CHECK(limit && limit->object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY);

    definition = make_definition(482, clear_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    object.lifecycle_id = map_script_object_lifecycle_current(0);
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    contact.object = &object;
    contact.cell_index = 402;
    contact.symbol = 'F';
    contact.qualified_key = "demo:fan";
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(object.vy == -60.0f);
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 0);

    /* A pool allocation generation change eagerly frees an old policy so all
     * 16 reusable slots can churn without consuming the fixed capacity. */
    definition = make_definition(483, transition_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 2;
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    contact.object = &object;
    contact.cell_index = 403;
    contact.symbol = 'S';
    contact.qualified_key = "demo:spring";
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 1);
    CHECK(map_script_object_lifecycle_advance(0));
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 0);

    /* A later callback fault cannot leave the pre-existing native policy
     * active on a timeline whose script VM has been disabled. */
    definition = make_definition(484, fault_source, bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    memset(&object, 0, sizeof(object));
    object.object_id = 0;
    object.object_kind = MAP_SCRIPT_OBJECT_PLAYER;
    contact.object = &object;
    contact.cell_index = 404;
    contact.symbol = 'S';
    contact.qualified_key = "demo:spring";
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_update_object(&object, error, sizeof(error)));
    CHECK(!map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_is_faulted());
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK(snapshot.velocity_limit_count == 0);
    CHECK(map_script_snapshot_validate(&snapshot, definition.script_id,
                                       error, sizeof(error)));
}

static void test_reused_slot_lifecycle(const MapScriptTileBinding* bindings,
                                       size_t binding_count) {
    static const char source[] =
        "map.state.enters = 0\n"
        "map.state.leaves = 0\n"
        "map.on_enter('S', function(object, tile)\n"
        "  map.state.enters = map.state.enters + 1\n"
        "end)\n"
        "map.on_leave('S', function(object, tile)\n"
        "  map.state.leaves = map.state.leaves + 1\n"
        "  object:add_velocity(0, 1)\n"
        "end)\n";
    MapScriptDefinition definition = make_definition(150, source, bindings, binding_count);
    MapScriptHost host_api;
    TestHost host;
    MapScriptObjectView object;
    MapScriptContactView contact;
    MapScriptSnapshot baseline;
    MapScriptSnapshot future_a;
    MapScriptSnapshot future_b;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];
    const uint32_t slot = 3;

    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.apply_object_fn = test_apply;
    host_api.userdata = &host;
    CHECK(map_script_activate(&definition, &host_api, error, sizeof(error)));
    CHECK(map_script_object_lifecycle_current(slot) == 0);

    memset(&object, 0, sizeof(object));
    object.object_id = 2u + slot;
    object.object_kind = MAP_SCRIPT_OBJECT_SWORD;
    object.lifecycle_id = map_script_object_lifecycle_current(slot);
    object.x = 10.0f;
    object.y = 20.0f;
    memset(&contact, 0, sizeof(contact));
    contact.object = &object;
    contact.cell_index = 50;
    contact.tile_x = 2;
    contact.tile_y = 3;
    contact.symbol = 'S';
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(baseline.lifecycle_generation[slot] == 0);

    CHECK(map_script_object_lifecycle_advance(slot));
    CHECK(map_script_object_lifecycle_current(slot) == 1);
    object.lifecycle_id = map_script_object_lifecycle_current(slot);
    object.vy = 0.0f;
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    /* A reused slot on the same cell is a new enter, never a stay. The stale
     * occupant receives its deterministic leave at the end of this tick. */
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(host.apply_count == 1);
    CHECK(host.applied.object_id == object.object_id);
    CHECK(host.applied.lifecycle_id == 0);
    CHECK(host.applied.vy == 1.0f);
    CHECK(map_script_snapshot_save(&future_a, error, sizeof(error)));
    entry = state_entry(&future_a, "enters");
    CHECK(entry && entry->number_value == 2.0);
    entry = state_entry(&future_a, "leaves");
    CHECK(entry && entry->number_value == 1.0);

    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    CHECK(map_script_object_lifecycle_current(slot) == 0);
    memset(&host, 0, sizeof(host));
    CHECK(map_script_object_lifecycle_advance(slot));
    object.lifecycle_id = map_script_object_lifecycle_current(slot);
    object.vy = 0.0f;
    CHECK(map_script_dispatch_contact(&contact, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future_b, error, sizeof(error)));
    CHECK(memcmp(&future_a, &future_b, sizeof(future_a)) == 0);
}

static void test_runtime_fault(const MapScriptTileBinding* bindings,
                               size_t binding_count) {
    static const char source[] =
        "map.on_tick(function() while true do end end)";
    MapScriptDefinition definition = make_definition(200, source, bindings, binding_count);
    MapScriptSnapshot before_fault;
    MapScriptSnapshot snapshot;
    TestHost host;
    MapScriptHost host_api;
    char error[512];
    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    definition.instruction_budget = 2000;
    host_api.log_fn = test_log;
    host_api.userdata = &host;
    CHECK(map_script_activate(&definition, &host_api, error, sizeof(error)));
    CHECK(map_script_snapshot_save(&before_fault, error, sizeof(error)));
    CHECK(!map_script_snapshot_validate(&before_fault, definition.script_id + 1,
                                        error, sizeof(error)));
    CHECK(!map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_is_faulted());
    CHECK(host.log_count == 1);
    CHECK(strstr(host.last_log, "instruction budget") != NULL);
    CHECK(map_script_snapshot_save(&snapshot, error, sizeof(error)));
    CHECK((snapshot.flags & 1u) != 0);
    CHECK(snapshot.contact_count == 0 && snapshot.override_count == 0);
    CHECK(map_script_snapshot_validate(&snapshot, definition.script_id,
                                       error, sizeof(error)));
    CHECK(map_script_snapshot_load(&before_fault, error, sizeof(error)));
    CHECK(!map_script_is_faulted());
    CHECK(map_script_last_error()[0] == '\0');
    CHECK(!map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_is_faulted());
    CHECK(host.log_count == 2);
    CHECK(map_script_snapshot_load(&snapshot, error, sizeof(error)));
    CHECK(map_script_is_faulted());
    CHECK(strcmp(map_script_last_error(),
                 "map.lua fault restored from rollback state") == 0);
}

static void test_memory_limits(const MapScriptTileBinding* bindings,
                               size_t binding_count) {
    static const char load_source[] =
        "huge_value = string.rep('x', 1048576)";
    static const char callback_source[] =
        "map.on_tick(function() local value = string.rep('x', 1048576) end)";
    MapScriptDefinition definition = make_definition(300, load_source,
                                                       bindings, binding_count);
    MapScriptHost host_api;
    TestHost host;
    char error[512];
    definition.memory_limit_bytes = 256u * 1024u;
    CHECK(!map_script_validate(&definition, error, sizeof(error)));
    CHECK(strstr(error, "memory") != NULL);

    memset(&host, 0, sizeof(host));
    memset(&host_api, 0, sizeof(host_api));
    host_api.log_fn = test_log;
    host_api.userdata = &host;
    definition = make_definition(301, callback_source, bindings, binding_count);
    definition.memory_limit_bytes = 256u * 1024u;
    CHECK(map_script_activate(&definition, &host_api, error, sizeof(error)));
    CHECK(!map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_is_faulted());
    CHECK(host.log_count == 1);
    CHECK(strstr(error, "memory") != NULL);
}

static void test_periodic_clock(const MapScriptTileBinding* bindings,
                                size_t binding_count) {
    static const char source[] =
        "map.on_tick(function() "
        "if map.every(3) then map.state.base = (map.state.base or 0) + 1 end "
        "if map.every(3, 2) then map.state.shift = (map.state.shift or 0) + 1 end "
        "map.state.wide = map.every(4294967295, 1) "
        "end)";
    static const char* invalid[] = {
        "map.every()", "map.every(0)", "map.every(-1)",
        "map.every(1.5)", "map.every(4294967296)", "map.every('3')",
        "map.every(3, -1)", "map.every(3, 3)", "map.every(3, 0.5)",
        "map.every(3, nil)", "map.every(3, 0, 1)",
        "map.every(0/0)", "map.every(3, 0/0)", "map.every(1/0)"
    };
    MapScriptDefinition definition = make_definition(400, source, bindings, binding_count);
    MapScriptSnapshot baseline, future, replay;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];
    size_t i;
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    for (i = 0; i < 6; ++i) CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future, error, sizeof(error)));
    entry = state_entry(&future, "base");
    CHECK(entry && entry->number_value == 3.0);
    entry = state_entry(&future, "shift");
    CHECK(entry && entry->number_value == 2.0);
    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    for (i = 0; i < 6; ++i) CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&replay, error, sizeof(error)));
    CHECK(memcmp(&future, &replay, sizeof(future)) == 0);
    baseline.tick = UINT64_C(9007199254740991) + 2u;
    baseline.checksum = test_snapshot_checksum(&baseline);
    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&replay, error, sizeof(error)));
    entry = state_entry(&replay, "base");
    CHECK(entry && entry->number_value == 2.0);
    entry = state_entry(&replay, "wide");
    CHECK(entry && entry->bool_value ==
          (baseline.tick % UINT64_C(4294967295) == 1u));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        expect_validation_failure(401 + i, invalid[i], bindings, binding_count, "map.every");
    }
}

static void test_optional_bindings(const MapScriptTileBinding* bindings,
                                    size_t binding_count) {
    static const char source[] =
        "assert(map.has_tile('S') and map.has_tile('demo:spring')) "
        "local list = map.tile_bindings() "
        "assert(#list == 3 and list[1].symbol == 'S' and list[1].key == 'demo:spring') "
        "assert(list[2].symbol == 'F' and list[3].symbol == ' ') "
        "list[1].key = 'changed' list[2] = nil "
        "assert(map.tile_bindings()[1].key == 'demo:spring' and #map.tile_bindings() == 3) "
        "assert(not map.has_tile('X') and not map.has_tile('@')) "
        "assert(not map.has_tile('') and not map.has_tile('demo:spring' .. string.char(0) .. 'x')) "
        "if map.has_tile('X') then map.on_contact('X', function() end) end "
        "map.on_tick(function() map.state.present = map.has_tile('S') "
        "map.state.first_key = map.tile_bindings()[1].key end)";
    MapScriptDefinition definition = make_definition(500, source, bindings, binding_count);
    MapScriptSnapshot baseline, future, replay;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future, error, sizeof(error)));
    entry = state_entry(&future, "present");
    CHECK(entry && entry->bool_value == 1);
    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&replay, error, sizeof(error)));
    CHECK(memcmp(&future, &replay, sizeof(future)) == 0);
    expect_validation_failure(501, "map.has_tile(1)", bindings, binding_count, "map.has_tile");
    expect_validation_failure(502, "map.has_tile('S', 'F')", bindings, binding_count, "map.has_tile");
    expect_validation_failure(503,
        "map.on_contact('demo:spring' .. string.char(0) .. 'x', function() end)",
        bindings, binding_count, "unknown map tile");
    expect_validation_failure(504, "map.tile_bindings('S')", bindings, binding_count, "map.tile_bindings");
    definition = make_definition(505, "assert(#map.tile_bindings() == 0)", NULL, 0);
    CHECK(map_script_validate(&definition, error, sizeof(error)));
}

static void test_state_keys(const MapScriptTileBinding* bindings, size_t binding_count) {
    static const char source[] =
        "assert(#map.state_keys() == 0) "
        "map.state.z = 1 map.state.a = false map.state.m = 'value' "
        "local keys = map.state_keys() assert(#keys == 3 and keys[1] == 'a' and keys[2] == 'm' and keys[3] == 'z') "
        "keys[1] = 'changed' assert(map.state_keys()[1] == 'a') "
        "map.state.a = nil map.state.b = true "
        "assert(map.state_keys()[1] == 'b') "
        "map.on_tick(function() local keys = map.state_keys() "
        "for _, key in ipairs(keys) do map.state[key] = nil end "
        "map.state.first = keys[1] map.state.count = #keys end)";
    MapScriptDefinition definition = make_definition(600, source, bindings, binding_count);
    MapScriptSnapshot baseline, future, replay;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future, error, sizeof(error)));
    entry = state_entry(&future, "count");
    CHECK(entry && entry->number_value == 3);
    entry = state_entry(&future, "first");
    CHECK(entry && strcmp(entry->string_value, "b") == 0);
    CHECK(future.state_count == 2);
    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&replay, error, sizeof(error)));
    CHECK(memcmp(&future, &replay, sizeof(future)) == 0);
    expect_validation_failure(601, "map.state_keys(1)", bindings, binding_count, "map.state_keys");
    definition = make_definition(602,
        "for i = 1, 64 do map.state['k' .. i] = i end "
        "local keys = map.state_keys() assert(#keys == 64 and keys[1] == 'k1' and keys[64] == 'k9') "
        "map.state.k10 = nil assert(#map.state_keys() == 63)", bindings, binding_count);
    CHECK(map_script_validate(&definition, error, sizeof(error)));
}

static void test_state_clear(const MapScriptTileBinding* bindings, size_t binding_count) {
    static const char source[] =
        "assert(map.state_clear() == 0) "
        "map.state.round_a = 1 map.state.round_b = false map.state.score = 7 "
        "assert(map.state_clear('missing') == 0) "
        "assert(map.state_clear('round_') == 2 and map.state.score == 7) "
        "assert(map.state_clear('') == 1) "
        "for i = 1, 64 do map.state['k' .. i] = i end "
        "assert(map.state_clear('k') == 64) "
        "for i = 1, 64 do map.state['r' .. i] = i end "
        "map.on_tick(function() local n = map.state_clear('r') "
        "assert(n == 64) map.state.removed = n end)";
    MapScriptDefinition definition = make_definition(610, source, bindings, binding_count);
    MapScriptSnapshot baseline, future, replay;
    const MapScriptSnapshotStateEntry* entry;
    char error[512];
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(baseline.state_count == 64);
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future, error, sizeof(error)));
    entry = state_entry(&future, "removed");
    CHECK(future.state_count == 1 && entry && entry->number_value == 64);
    CHECK(map_script_snapshot_load(&baseline, error, sizeof(error)));
    CHECK(map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&replay, error, sizeof(error)));
    CHECK(memcmp(&future, &replay, sizeof(future)) == 0);
    definition = make_definition(616,
        "map.state.keep = 42 map.on_tick(function() map.state_clear('', true) end)",
        bindings, binding_count);
    CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
    CHECK(!map_script_dispatch_tick(error, sizeof(error)));
    CHECK(map_script_snapshot_save(&future, error, sizeof(error)));
    CHECK(future.state_count == baseline.state_count);
    CHECK(memcmp(future.state, baseline.state, sizeof(future.state)) == 0);
    expect_validation_failure(611, "map.state_clear(1)", bindings, binding_count, "prefix");
    expect_validation_failure(612, "map.state_clear(nil)", bindings, binding_count, "prefix");
    expect_validation_failure(613, "map.state_clear('', '')", bindings, binding_count, "prefix");
    expect_validation_failure(614, "map.state_clear(string.rep('a', 32))", bindings, binding_count, "prefix");
    expect_validation_failure(615, "map.state_clear(string.char(0))", bindings, binding_count, "prefix");
}

static void test_state_assignment_atomicity(const MapScriptTileBinding* bindings,
                                            size_t binding_count) {
    static const char* const invalid_values[] = {
        "{}", "function() end", "0/0", "1/0", "-1/0", "string.rep('x', 64)"
    };
    size_t value_index;
    int existing;
    char source[1024];
    char error[512];
    MapScriptSnapshot baseline, future;
    for (existing = 0; existing <= 1; ++existing) {
        for (value_index = 0;
             value_index < sizeof(invalid_values) / sizeof(invalid_values[0]);
             ++value_index) {
            MapScriptDefinition definition;
            snprintf(source, sizeof(source),
                "map.state.keep = 42 %s "
                "map.on_tick(function() map.state.target = %s end)",
                existing ? "map.state.target = 'original'" : "",
                invalid_values[value_index]);
            definition = make_definition(620 + (uint32_t)value_index + 10 * existing,
                                         source, bindings, binding_count);
            CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
            CHECK(map_script_snapshot_save(&baseline, error, sizeof(error)));
            CHECK(!map_script_dispatch_tick(error, sizeof(error)));
            CHECK(map_script_snapshot_save(&future, error, sizeof(error)));
            CHECK(future.state_count == baseline.state_count);
            CHECK(memcmp(future.state, baseline.state, sizeof(future.state)) == 0);
        }
    }
}

static void test_timers(const MapScriptTileBinding* bindings, size_t binding_count) {
    const char* source =
        "map.state.n=0 map.state.order='' "
        "map.on_timer('pulse',function() map.state.n=map.state.n+1 "
        "map.state.order=map.state.order..'p' end) "
        "map.on_timer('stop',function() map.state.order=map.state.order..'s' "
        "map.timer_cancel('pulse') end) "
        "map.timer_start('pulse',1,2) map.timer_start('stop',5) "
        "map.on_tick(function() map.state.remaining=map.timer_remaining('pulse') end)";
    MapScriptDefinition definition = make_definition(650,source,bindings,binding_count);
    MapScriptSnapshot baseline, future, replay, bad;
    char error[512];
    CHECK(map_script_activate(&definition,NULL,error,sizeof(error)));
    CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline,error,sizeof(error)));
    for (int i=0;i<5;++i) CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(state_entry(&future,"n")->number_value==3);
    CHECK(strcmp(state_entry(&future,"order")->string_value,"ppps")==0);
    CHECK(future.timers[0].remaining==0 && future.timers[0].interval==0);
    CHECK(map_script_snapshot_load(&baseline,error,sizeof(error)));
    for (int i=0;i<5;++i) CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&replay,error,sizeof(error)));
    CHECK(memcmp(&future,&replay,sizeof(future))==0);
    bad=baseline; bad.timers[31].remaining=1; bad.checksum=test_snapshot_checksum(&bad);
    CHECK(!map_script_snapshot_load(&bad,error,sizeof(error)));
    CHECK(map_script_snapshot_save(&replay,error,sizeof(error)));
    CHECK(memcmp(&future,&replay,sizeof(future))==0);
    definition=make_definition(651,
        "map.on_timer('a',function() assert(map.timer_cancel('b')) "
        "map.timer_start('a',1) end) "
        "map.on_timer('b',function() error('cancel failed') end) "
        "map.timer_start('a',1) map.timer_start('b',1)",bindings,binding_count);
    CHECK(map_script_activate(&definition,NULL,error,sizeof(error)));
    CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(future.timers[0].remaining==1 && future.timers[1].remaining==0);
    definition=make_definition(652,
        "map.state.n=0 map.on_timer('a',function() map.state.n=1 error('timer fault') end) "
        "map.timer_start('a',1)",bindings,binding_count);
    CHECK(map_script_activate(&definition,NULL,error,sizeof(error)));
    CHECK(map_script_snapshot_save(&baseline,error,sizeof(error)));
    CHECK(!map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(state_entry(&future,"n")->number_value==0 && future.timers[0].remaining==1);
    CHECK(map_script_snapshot_load(&baseline,error,sizeof(error)));
    CHECK(!map_script_dispatch_tick(error,sizeof(error)));
    definition=make_definition(659,
        "map.on_timer('a',function() map.timer_start('b',2) end) "
        "map.on_timer('b',function() map.state.fired=true end) "
        "map.timer_start('a',1) map.timer_start('b',1)",bindings,binding_count);
    CHECK(map_script_activate(&definition,NULL,error,sizeof(error)));
    CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(!state_entry(&future,"fired") && future.timers[1].remaining==2);
    CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(state_entry(&future,"fired") && state_entry(&future,"fired")->bool_value);
    definition=make_definition(660,
        "map.on_timer('a',function() end) map.timer_start('a',8,4) "
        "map.on_tick(function() map.timer_start('a',3,-1) end)",bindings,binding_count);
    CHECK(map_script_activate(&definition,NULL,error,sizeof(error)));
    CHECK(!map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(future.timers[0].remaining==8 && future.timers[0].interval==4);
    definition=make_definition(661,
        "for i=1,32 do map.on_timer('t'..i,function() end) map.timer_start('t'..i,1000000000) end",
        bindings,binding_count);
    CHECK(map_script_activate(&definition,NULL,error,sizeof(error)));
    CHECK(map_script_dispatch_tick(error,sizeof(error)));
    CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
    CHECK(future.timer_count==32 && future.timers[31].remaining==999999999);
    {
        char example[4096];
        FILE* file = fopen("docs/examples/map_timed_spring.lua", "rb");
        CHECK(file != NULL);
        if (file) {
            size_t size = fread(example, 1, sizeof(example)-1, file);
            example[size] = 0;
            fclose(file);
            definition = make_definition(662, example, bindings, binding_count);
            CHECK(map_script_activate(&definition, NULL, error, sizeof(error)));
            for (int i=0; i<120; ++i) CHECK(map_script_dispatch_tick(error,sizeof(error)));
            CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
            CHECK(state_entry(&future,"spring_active")->bool_value);
            for (int i=0; i<60; ++i) CHECK(map_script_dispatch_tick(error,sizeof(error)));
            CHECK(map_script_snapshot_save(&future,error,sizeof(error)));
            CHECK(!state_entry(&future,"spring_active")->bool_value);
        }
    }
    expect_validation_failure(653,"map.on_timer('x',function() end) map.on_timer('x',function() end)",bindings,binding_count,"duplicate");
    expect_validation_failure(654,"map.timer_start('unknown',1)",bindings,binding_count,"unknown");
    expect_validation_failure(655,"local n=1 map.on_timer('x',function() n=n+1 end)",bindings,binding_count,"upvalues");
    expect_validation_failure(656,"map.on_timer('x',function() end) map.timer_start('x',0)",bindings,binding_count,"integer range");
    expect_validation_failure(657,"map.on_timer('x',function() end) map.timer_start('x',1,0/0)",bindings,binding_count,"integer range");
    expect_validation_failure(658,"for i=1,33 do map.on_timer('t'..i,function() end) end",bindings,binding_count,"limit");
}

int main(void) {
    static const MapScriptTileBinding bindings[] = {
        { 'S', "demo:spring" },
        { 'F', "demo:fan" },
        { ' ', "demo:background" }
    };
    test_inactive_snapshot();
    test_validation_barriers(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_sensor_validation(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_sensor_runtime(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_object_profiles(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_spring_probe_geometry(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_binding_scope_runtime(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_player_kind_transitions(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_sprite_option_faults(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_demo_runtime(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_velocity_limits(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_reused_slot_lifecycle(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_runtime_fault(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_memory_limits(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_periodic_clock(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_optional_bindings(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_state_keys(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_timers(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_state_clear(bindings, sizeof(bindings) / sizeof(bindings[0]));
    test_state_assignment_atomicity(bindings, sizeof(bindings) / sizeof(bindings[0]));
    map_script_deactivate();
    test_inactive_snapshot();
    if (g_failures) {
        fprintf(stderr, "%d map script test(s) failed\n", g_failures);
        return 1;
    }
    puts("map_script_test: all checks passed");
    return 0;
}
