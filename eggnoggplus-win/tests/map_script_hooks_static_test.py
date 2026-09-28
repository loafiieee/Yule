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
read_player_observation = body("static int hooks_map_script_read_player_observation")
capture_player_commands = body("static void hooks_capture_map_script_previous_commands")
native_tick = body("static int hooks_native_game_tick_callback")
thing_new = body("static void* __cdecl hooked_thing_new")
spawn_thing = body("static int __cdecl hooked_spawn_thing_action")
tile_action = body("static int __cdecl hooked_tile_action_ex")
map_build = body("static void __cdecl hooked_mapgen_build_map(void)")
trigger_mines = body("static void hooks_map_script_trigger_mines")
mine_anim = body("static void __cdecl hooked_mine_anim")
native_attack_kind = body("static int hooks_native_attack_probe_kind")
draw_override = body("static int content_bridge_map_script_visual_override")
hooks_init = body("void hooks_init(void)")
skip_player_body = body("int __cdecl hooks_should_skip_draw_player_body")
player_colour = body("static void __cdecl hooked_game_player_colour")

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
assert "script_host.tile_at_fn = hooks_map_script_tile_at" in map_build
assert "script_host.read_player_observation_fn = hooks_map_script_read_player_observation" in map_build
assert "script_host.trigger_mines_fn = hooks_map_script_trigger_mines" in map_build
tile_at = body("static int hooks_map_script_tile_at")
assert "custom_maps_pinned_tile_at_world" in tile_at
assert "*g_hook_map_selector" in tile_at

# Explicit custom-object mine requests reuse the verified native trigger entry
# only after checking map_coord_tile still identifies mine tile 0x06. This must
# never expose generic tile_action_ex to map Lua.
assert "#define ADDR_TRIGGER_MINE_POS         0x41E900u" in source
assert "#define ADDR_MAP_COORD_TILE           0x434B30u" in source
assert "p_map_coord_tile(x,y)" in trigger_mines
assert "*tile==0x06u" in trigger_mines
assert trigger_mines.index("*tile==0x06u") < trigger_mines.index("p_trigger_mine_pos(x,y)")
assert "p_tile_action_ex" not in trigger_mines

# Native combat probes are mirrored only at verified engine seams. Lua runs
# later at dispatch_tick, never while vanilla's combat stack is active.
assert "#define ADDR_PLAYER_CHECK_OPPONENT_HIT 0x425930u" in source
assert "MAP_SCRIPT_NATIVE_DAMAGE_PUNCH" in source
assert "MAP_SCRIPT_NATIVE_DAMAGE_KICK" in source
assert "MAP_SCRIPT_NATIVE_DAMAGE_SWORD" in source
assert "MAP_SCRIPT_NATIVE_DAMAGE_THROWN_SWORD" in source
assert "MAP_SCRIPT_NATIVE_DAMAGE_SPIKE_BALL" in source
assert "MAP_SCRIPT_NATIVE_DAMAGE_MINE" in source
assert "map_script_submit_native_attack_probe(&probe)" in source
assert "__builtin_return_address(0)" in source
# The idle held-sword probe returns at 0x429e82. Only the verified stab
# animation call sites may become native:sword damage for managed objects.
assert "caller!=0x429F55u&&caller!=0x42A6FAu" in native_attack_kind
assert "(state&0x10u)==0u" in native_attack_kind
assert "0x429E82" not in native_attack_kind.upper()
# Keep the other native attack families pinned to their complete caller sets.
for continuation in ("0x429BF7u", "0x42A572u", "0x42A5E8u", "0x42BD6Bu"):
    assert continuation in native_attack_kind
assert "static const unsigned char expected[]={0x55,0x57,0x56,0x53,0x83,0xEC,0x4C}" in source
assert "probe.radius=10.0f" in source
assert "probe.radius=24.0f" in source
assert "#define ADDR_MINE_ANIM                0x4250C0u" in source
assert "object[0x89]==0" in mine_anim
assert "*g_mad_ticks!=*last_mine_tick" in mine_anim
assert mine_anim.index("if(real)real(thing)") < mine_anim.index("map_script_submit_native_attack_probe(&probe)")
assert "map_script_submit_native_attack_probe" not in trigger_mines
assert "hooked_trigger_mine_pos" not in source
assert "static const unsigned char expected[]={0x55,0x57,0x56,0x53,0x83,0xEC,0x7C}" in source

# Temporary per-cell sprites flow through the generic draw bridge without
# coupling registry/render tests to LuaJIT.
assert "map_script_visual_override" in draw_override
assert "out_override->sprite_index = visual.sprite_index" in draw_override
assert "out_override->offset_x = visual.offset_x" in draw_override
assert "out_override->offset_y = visual.offset_y" in draw_override
assert "content_bridge_map_script_visual_override" in source

# Player presentation is draw-only and reads the rollback-owned map runtime at
# the existing body/color seams. It must not rewrite native palette selection.
assert "map_script_player_presentation" in skip_player_body
assert "MAP_SCRIPT_PLAYER_PRESENTATION_BODY_VISIBLE" in skip_player_body
assert "map_script_player_presentation" in player_colour
assert "MAP_SCRIPT_PLAYER_PRESENTATION_SKIN_TINT" in player_colour
assert "MAP_SCRIPT_PLAYER_PRESENTATION_CLOTHING_TINT" in player_colour
assert "out[0]*=" in player_colour and "out[3]*=" in player_colour

# game_update overwrites its native previous-command byte before map callbacks.
# Capture current commands before every live/replayed native tick, then compare
# the post-tick command against that transient rollback-derived sample.
assert "#define PLAYER_OFS_PREV_CMD_BITS      0x9Eu" in source
assert "#define PLAYER_OFS_CMD_BITS           0x9Fu" in source
assert "#define PLAYER_OFS_FACING             0x98u" in source
assert "#define PLAYER_OFS_HAS_SWORD          0x11u" in source
assert "#define PLAYER_OFS_PREV_COLLISION     0xACu" in source
assert "#define PLAYER_OFS_COLLISION_FLAGS    0xADu" in source
assert "g_map_script_previous_commands[slot]" in capture_player_commands
assert "g_map_script_previous_command_players[slot]=player" in capture_player_commands
assert "player+PLAYER_OFS_CMD_BITS" in capture_player_commands
assert "out->facing=*(const int8_t*)(player+PLAYER_OFS_FACING)" in read_player_observation
assert "out->has_sword=*(const uint8_t*)(player+PLAYER_OFS_HAS_SWORD)==0" in read_player_observation
assert "g_map_script_previous_commands_valid" in read_player_observation
assert "g_map_script_previous_command_players[slot]==player" in read_player_observation
assert "player+PLAYER_OFS_PREV_CMD_BITS" not in read_player_observation
assert "player+PLAYER_OFS_PREV_COLLISION" in read_player_observation
assert native_tick.index("hooks_capture_map_script_previous_commands();") < native_tick.index("tick->update(tick->arg0);")
assert native_tick.index("tick->update(tick->arg0);") < native_tick.index("hooks_apply_content_tile_interactions();")

print("map_script_hooks_static_test: all checks passed")
