from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "hooks.c").read_text(encoding="utf-8")


def body(signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def last_body(signature: str) -> str:
    start = source.rindex(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


apply_one = body("static void hooks_apply_content_interactions_to_body")
apply_all = body("static void hooks_apply_content_tile_interactions(void)")
apply_object = last_body("static void hooks_map_script_apply_object")
body_for_object = body("static uintptr_t hooks_map_script_body_for_object")
kind_for_body = body("static int hooks_map_script_kind_for_body")
read_radius = body("static int hooks_map_script_read_contact_radius")
thing_new = body("static void* __cdecl hooked_thing_new")
spawn_thing = body("static int __cdecl hooked_spawn_thing_action")
tile_action = body("static int __cdecl hooked_tile_action_ex")
map_build = body("static void __cdecl hooked_mapgen_build_map(void)")
draw_override = body("static int content_bridge_map_script_visual_override")
hooks_init = body("void hooks_init(void)")

# Declarative force compatibility keeps the engine-matching center/half-tile
# probes. An object refresh follows those forces and precedes all callbacks so
# synthesized leave sees current post-native physics.
assert "content_tiles_apply_interaction_velocity" in apply_one
assert "map_script_update_object(&script_object" in apply_one
assert apply_one.index("content_tiles_apply_interaction_velocity") < apply_one.index(
    "map_script_update_object(&script_object"
)
assert "script_object.lifecycle_id = lifecycle_id" in apply_one
assert "script_object.object_kind = (uint8_t)object_kind" in apply_one

# Programmable sensors inspect every potentially intersecting bound cell in
# stable y-then-x order. Five cells is the complete reach implied by the strict
# authored tile/object box limits. Cell metadata—not another shifted world
# sample—owns exact identity and coordinates.
assert "#define MAP_SCRIPT_SENSOR_CELL_RADIUS 5" in source
assert "center_x - MAP_SCRIPT_SENSOR_CELL_RADIUS" in apply_one
assert "center_x + MAP_SCRIPT_SENSOR_CELL_RADIUS" in apply_one
assert "center_y - MAP_SCRIPT_SENSOR_CELL_RADIUS" in apply_one
assert "center_y + MAP_SCRIPT_SENSOR_CELL_RADIUS" in apply_one
assert apply_one.index("for (cell_y = first_y;") < apply_one.index(
    "for (cell_x = first_x;"
)
assert "content_tiles_interaction_at_cell(cell_x, cell_y" in apply_one

# Geometry coordinates are frozen before the first callback, even though
# staged object writes from callbacks intentionally compose in map order.
assert "sensor_x = script_object.x;" in apply_one
assert "sensor_y = script_object.y;" in apply_one
assert apply_one.index("sensor_x = script_object.x;") < apply_one.index(
    "for (cell_y = first_y;"
)
for field in (
    "candidate.sensor_x = sensor_x",
    "candidate.sensor_y = sensor_y",
    "candidate.cell_index = interaction.cell_index",
    "candidate.tile_x = interaction.x",
    "candidate.tile_y = interaction.y",
    "candidate.tile_width = tile_w",
    "candidate.tile_height = tile_h",
    "candidate.room_mirrored = interaction.room_mirrored",
    "candidate.qualified_key = interaction.key",
):
    assert field in apply_one
assert "map_script_binding_has_sensor(interaction.key)" in apply_one
assert "map_script_dispatch_cell_candidate(&candidate" in apply_one

# A configured sensor replaces legacy point sampling for that binding. A tile
# with no sensor still receives exactly its old de-duplicated contact, routed
# from the same stable cell enumeration rather than being double-dispatched.
sensor_branch = apply_one.index("if (map_script_binding_has_sensor(interaction.key))")
legacy_branch = apply_one.index("else if (legacy_index >= 0)", sensor_branch)
assert sensor_branch < apply_one.index("map_script_dispatch_cell_candidate", sensor_branch)
assert legacy_branch < apply_one.index("map_script_dispatch_contact", legacy_branch)

# Ghidra pins thing+0x6c as radius 6 for live/dead players, 4 for swords, and
# zero for K's point mass. Type 3 alone is insufficient: K also needs its exact
# updater. Host-side verification fails closed if any profile differs.
assert "#define THING_OFS_CONTACT_RADIUS      0x6Cu" in source
assert "MAP_SCRIPT_PLAYER_CONTACT_RADIUS" in read_radius
assert "MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS" in read_radius
assert "MAP_SCRIPT_SWORD_CONTACT_RADIUS" in read_radius
assert "MAP_SCRIPT_HAZARD_CONTACT_RADIUS" in read_radius
assert "actual != expected" in read_radius
assert "#define PLAYER_OFS_STATE_ID           0x78u" in source
assert "#define PLAYER_STATE_DEAD_BODY        0x08u" in source
assert "#define THING_TYPE_HAZARD             0x03u" in source
assert "#define THING_OFS_UPDATE_FN           0x158u" in source
assert "#define ADDR_HAZARD_ANIM              0x43C450u" in source
assert "PLAYER_STATE_DEAD_BODY" in kind_for_body
assert "update_fn == (uint32_t)ADDR_HAZARD_ANIM" in kind_for_body
assert "candidate.contact_radius = contact_radius" in apply_one
assert "candidate.object_kind = object_kind" in apply_one
assert "object_kind != MAP_SCRIPT_OBJECT_HAZARD" in apply_one
assert "hooks_map_script_kind_for_body(player)" in apply_all
assert "MAP_SCRIPT_OBJECT_DEAD_BODY" in apply_all
assert "MAP_SCRIPT_OBJECT_HAZARD" in apply_all

# Stable host ids cover both players and the verified sword/K subset of the
# fixed thing pool. Reusable slots additionally carry a rollback-owned lifecycle
# id, while exact object kind is carried independently in every object view.
assert "(uint32_t)player_index" in apply_all
assert "2u + thing_slot" in apply_all
assert "map_script_object_lifecycle_current(thing_slot)" in apply_all
assert "g_thing_lifecycle_tracking_enabled" in apply_all
assert "map_script_dispatch_tick" in apply_all
assert source.count("hooks_apply_content_tile_interactions();") == 1
assert source.count("hooks_run_native_game_tick(real_update, arg0") == 3

# Every successful native allocation advances exactly one fixed-slot lifecycle.
# The 10-byte thing_new detour boundary is pinned from static disassembly.
assert "result = real(type);" in thing_new
assert thing_new.index("if (!result") < thing_new.index(
    "map_script_object_lifecycle_advance(slot)"
)
assert "offset % THING_SIZE" in thing_new
assert "#define ADDR_THING_NEW                0x41FD40u" in source
assert "&g_thing_new_detour" in hooks_init
assert "(void*)&hooked_thing_new" in hooks_init
assert "10" in hooks_init[hooks_init.index("&g_thing_new_detour"):]

# Native spawn_thing_action dereferences a failed thing_new/sword_new result at
# 0x43CA88. The framework replacement must test allocation before any thing
# field write and retain the exact six-byte entry detour boundary.
assert "ADDR_SPAWN_THING_ACTION       0x43CA20u" in source
assert "if (!thing)" in spawn_thing
assert spawn_thing.index("if (!thing)") < spawn_thing.index(
    "thing + THING_OFS_SPRITE"
)
assert "thing pool exhausted" in spawn_thing
assert "&g_spawn_thing_action_detour" in hooks_init
assert "(void*)&hooked_spawn_thing_action" in hooks_init
spawn_install = hooks_init[hooks_init.index("&g_spawn_thing_action_detour") :]
assert "6" in spawn_install[:500]
# The native dispatcher invokes the registered action pointer internally, so
# reset-mode sword/K tiles must be intercepted before the generic trampoline;
# otherwise that call bypasses the entry detour above.
assert "mode == 9" in tile_action
assert "native_room_reset_is_duplicate_initial(" in tile_action
assert "mode, *g_game_started, *g_game_old_active_room," in tile_action
assert "*g_game_start_countdown" in tile_action
assert tile_action.index("native_room_reset_is_duplicate_initial(") < tile_action.index(
    "return hooked_spawn_thing_action"
)
assert "((const unsigned char*)tile)[0] == 0x1cu" in tile_action
assert "return hooked_spawn_thing_action(tile, mode, x, y, arg5);" in tile_action
assert tile_action.index("return hooked_spawn_thing_action") < tile_action.index(
    "result = real(tile, mode, x, y, arg5);"
)

# A synthesized leave for an old occupant is allowed to run, but its staged
# physics must not be written onto a newer occupant of the same pool slot.
assert "hooks_map_script_body_for_object(object)" in apply_object
assert "object->lifecycle_id != map_script_object_lifecycle_current(object_id)" in body_for_object
assert "current_kind != object->object_kind" in body_for_object
assert "object->object_kind != MAP_SCRIPT_OBJECT_DEAD_BODY" in body_for_object
assert "object->object_kind != MAP_SCRIPT_OBJECT_HAZARD" in body_for_object

# Scripted position changes translate native previous position by the same
# delta. This preserves point-mass displacement for K and avoids an artificial
# velocity/teleport impulse on the following native update.
assert "#define THING_OFS_PREV_X              0x2Cu" in source
assert "#define THING_OFS_PREV_Y              0x30u" in source
assert "previous_x + delta_x" in apply_object
assert "previous_y + delta_y" in apply_object
assert apply_object.index("translated_prev_x") < apply_object.index(
    "*(float*)(body + THING_OFS_X) = object->x"
)

# Every map-generation failure/vanilla path clears the prior VM; successful
# binding activates only the exact generation pinned by custom_maps.
assert map_build.count("custom_maps_deactivate_script();") >= 3
assert "custom_maps_activate_script_for_selector(selector, &script_host" in map_build
assert "script_host.rng_seed" in map_build
assert "script_host.apply_object_fn = hooks_map_script_apply_object" in map_build

# Temporary per-cell sprites flow through the generic draw bridge without
# coupling registry/render tests to LuaJIT.
assert "map_script_visual_override" in draw_override
assert "out_override->sprite_index = visual.sprite_index" in draw_override
assert "out_override->offset_x = visual.offset_x" in draw_override
assert "out_override->offset_y = visual.offset_y" in draw_override
assert "content_bridge_map_script_visual_override" in source

print("map_script_hooks_static_test: all checks passed")
