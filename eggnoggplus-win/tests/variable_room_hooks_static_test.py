from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
hooks = (ROOT / "hooks.c").read_text(encoding="utf-8")
maps = (ROOT / "custom_maps.c").read_text(encoding="utf-8")
header = (ROOT / "custom_maps.h").read_text(encoding="utf-8")


required_hooks = [
    "hooked_thing_roomnum",
    "hooked_game_room_count",
    "hooked_map_pixels_h",
    "hooked_game_update_camera",
    "hooked_skeleton_statue",
    "hooked_game_render",
    "hooked_mapgen_room_info",
    "hooks_room_info_index_width",
    "hooked_find_good_spot",
    "hooked_game_init",
    "hooks_commit_authored_start_room",
    "hooked_player_update_movement",
    "hooked_sword_update_movement",
    "hooked_hazard_update_movement",
    "hooks_remove_thing_below_authored_room",
    "hooks_variable_room_collision_bounds",
    "hooks_install_variable_room_collision_bounds",
    "g_variable_room_movement_tiles[128 * 64]",
    "custom_maps_adjust_spawn_position",
    "custom_maps_player_start_position",
    "map_script_exit_locked",
    "custom_maps_room_connection_policy",
    "preferred_spawn_x",
    "preferred_spawn_y",
    "ADDR_PLAYER_UPDATE_MOVEMENT",
    "ADDR_MAPGEN_ROOM_INFO",
    "hooked_map_tile_raw",
    "hooks_update_variable_room_extra_tiles",
    "hooked_reset_room",
]
for token in required_hooks:
    assert token in hooks, f"missing variable-room hook integration: {token}"

assert "Native find_good_spot clamps every search to columns 1..31" in hooks
assert "preferred_spawn_x = before_x" in hooks
assert "preferred_spawn_y = (float)start_y_px +" in hooks
assert "(float)height_px * 0.5f" in hooks
map_tile_hook = hooks[hooks.index("static void* __cdecl hooked_map_tile_raw"):
                      hooks.index("static void __attribute__((regparm(1))) hooked_reset_room")]
assert "caller == 0x42CA38u || caller == 0x42CCEFu" in map_tile_hook
assert "caller == 0x42CCEFu || caller == 0x41E9B4u" not in map_tile_hook, (
    "trigger_mine_pos already supplies a real world/local tile coordinate"
)

required_map_runtime = [
    "custom_maps_rebuild_variable_map",
    "custom_maps_variable_room_bounds",
    "custom_maps_variable_room_count",
    "SPAWN_MARKER_ALLOW_P1",
    "SPAWN_MARKER_ALLOW_P2",
    "CUSTOM_MAP_MAX_SPAWN_MARKERS 128",
]
for token in required_map_runtime:
    assert token in maps, f"missing variable-room runtime support: {token}"

for declaration in [
    "int custom_maps_adjust_spawn_position",
    "int custom_maps_player_start_position",
    "int custom_maps_variable_room_bounds",
    "int custom_maps_variable_room_bounds_for_index",
    "int custom_maps_variable_room_bounds_2d_for_index",
    "int custom_maps_variable_room_at",
    "int custom_maps_start_room",
    "int custom_maps_variable_room_count",
    "int custom_maps_pinned_room_definition",
]:
    assert declaration in header, f"missing public declaration: {declaration}"

assert "thing_roomnum_prologue" in hooks
assert "room_count_prologue" in hooks
assert "static int relocate_detour_rel32" in hooks
relocate = hooks[
    hooks.index("static int relocate_detour_rel32"):
    hooks.index("static int install_detour_relocated_rel32")
]
assert "original = d->original + instruction_offset;" in relocate
assert "original_ip = (uint8_t*)d->target + instruction_offset;" in relocate
assert "destination = (uintptr_t)(original_ip + 5)" in relocate
assert "original = (uint8_t*)d->target + instruction_offset;" not in relocate
assert "static int install_detour_relocated_rel32" in hooks
assert "!install_detour_relocated_rel32(" in hooks
assert "sizeof(room_count_prologue), 3u" in hooks
assert "VirtualFree(d->trampoline, 0, MEM_RELEASE)" in hooks
# The verified native CALL targets map_tiles_w at 0x434940. Copying its old
# +0x5208 displacement to the dump's 0x032b0000 trampoline reproduces the
# exact bad EIP; rebasing the displacement keeps the original destination.
native_call = 0x42F733
trampoline_call = 0x032B0003
old_displacement = 0x5208
assert trampoline_call + 5 + old_displacement == 0x032B5210
destination = native_call + 5 + old_displacement
rebased_displacement = destination - (trampoline_call + 5)
assert destination == 0x434940
assert trampoline_call + 5 + rebased_displacement == destination
assert "map_pixels_h_prologue" in hooks
assert "camera_prologue" in hooks
assert "skeleton_prologue" in hooks
assert "game_render_prologue" in hooks
assert "spawn_prologue" in hooks
assert "movement_prologue" in hooks
assert "map_tile_prologue" in hooks
assert "reset_room_prologue" in hooks
assert "physics_movement_prologue" in hooks
assert "room_info_prologue" in hooks
collision_bounds = hooks[hooks.index("hooks_variable_room_collision_bounds(void)"):
                         hooks.index("/* Verified native call sites")]
for token in [
    "custom_maps_variable_room_bounds_for_index(selector, room",
    "((uint64_t)(uint32_t)start_px << 32)",
    "(uint32_t)(width_px - 1)",
    "0x0042D6A1u",
    "0x0F, 0xAF, 0xD0",
    "FlushInstructionCache",
]:
    assert token in collision_bounds, f"variable-room collision clamp patch is incomplete: {token}"
assert "if (!hooks_install_variable_room_collision_bounds())" in hooks
assert "InterlockedExchange(&g_variable_room_local_movement, 1)" in hooks
assert "*g_tilemap_data_ptr = (uintptr_t)g_variable_room_movement_tiles" in hooks
movement_hook = hooks[hooks.index("static void __attribute__((regparm(1))) hooked_player_update_movement"):
                      hooks.index("static void hooks_remove_thing_below_authored_room")]
for token in [
    "old_world_x = *x;",
    "old_world_y = *y;",
    "custom_maps_resolve_room_transition",
    "(unsigned char)destination_room",
    "map_script_exit_locked((uint32_t)transition_connection)",
    "hooks_room_connection_allows((uintptr_t)player",
    "hooks_room_graph_has_go_player",
    "no-go fallback",
    "crossing_focus",
    "*x = old_world_x;",
    "*y = old_world_y;",
]:
    assert token in movement_hook, f"room-graph movement transition is incomplete: {token}"
# Player byte +0x71 is a native combat-hit bitfield. Bit 0x04 adds freeze
# frames and kills the player, so room/pit handling must never touch it.
assert "old_pending_death" not in movement_hook
assert "player + 0x71" not in movement_hook
camera_hook = hooks[hooks.index("static void __cdecl hooked_game_update_camera"):
                    hooks.index("static int __attribute__((regparm(2))) hooked_find_good_spot")]
for token in [
    "custom_maps_variable_room_bounds_2d_for_index",
    "camera_width = (int)ceilf(*g_game_w_native);",
    "coordinate_offset = ((float)camera_width - (float)width_px) * 0.5f;",
    "saved_camera - (float)start_px + coordinate_offset",
    "(float)start_px - coordinate_offset",
    "target_y = (float)start_y_px + (float)height_px * 0.5f;",
    "target_y = (float)(start_y_px + height_px) - half_view;",
    "saved_camera_y - (float)start_y_px",
    "target_y - (float)start_y_px",
    "*g_camera_y += (float)start_y_px;",
    "saved_camera + 32.0f",
    "saved_camera_y + 24.0f",
    "fabsf(*g_camera_x - world_target_x) < 0.05f",
]:
    assert token in camera_hook, f"narrow variable-room camera is not centered safely: {token}"
respawn_hook = hooks[hooks.index("static int __attribute__((regparm(2))) hooked_find_good_spot"):
                     hooks.index("static void __cdecl hooked_eggnogg_colour")]
for token in [
    "*g_tilemap_width = width;",
    "*g_tilemap_height = height;",
    "*g_room_pixel_width = width_px;",
    "*g_native_map_h = height;",
    "*x -= (float)start_px;",
    "*y -= (float)start_y_px;",
    "*x += (float)start_px;",
    "*y += (float)start_y_px;",
    "(unsigned char)spawn_room",
    "*g_game_active_room = spawn_room;",
]:
    assert token in respawn_hook, f"variable-room respawn search is not localized: {token}"
assert respawn_hook.index("*x -= (float)start_px;") < respawn_hook.index(
    "p_find_good_spot_trampoline(player, direction)")
assert respawn_hook.index("*x += (float)start_px;") > respawn_hook.index(
    "p_find_good_spot_trampoline(player, direction)")
assert "if (!initial_spawn && g_game_active_room" in respawn_hook
assert respawn_hook.index("if (!initial_spawn && g_game_active_room") < respawn_hook.index(
    "custom_maps_variable_room_bounds(selector, before_x")
tile_action_hook = hooks[hooks.index("static int __cdecl hooked_tile_action_ex"):
                         hooks.index("static int __cdecl hooked_high_water_action")]
for token in [
    "mode == 5",
    "g_variable_room_local_movement",
    "g_variable_room_player_query_room",
    "custom_maps_variable_room_bounds_2d_for_index",
    "x >= width_px / 16",
    "y >= height_px / 16",
    "mine activation rejected",
    "x += start_x_px / 16",
    "y += start_y_px / 16",
]:
    assert token in tile_action_hook, f"room-local mine activation loses world X: {token}"
map_tile_hook = hooks[hooks.index("static void* __cdecl hooked_map_tile_raw"):
                      hooks.index("static void __attribute__((regparm(1))) hooked_reset_room")]
assert "g_variable_room_local_movement" in map_tile_hook
assert map_tile_hook.index("g_variable_room_local_movement") < map_tile_hook.index(
    "caller == 0x42CA38u")
height_hook_start = hooks.index("static int __cdecl hooked_map_pixels_h")
height_hook = hooks[height_hook_start:
                    hooks.index("static uint32_t g_variable_room_movement_tiles", height_hook_start)]
for token in [
    "caller == 0x42AB5Au",
    "g_variable_room_player_query_room",
    "custom_maps_variable_room_bounds_2d_for_index",
    "return start_y_px + height_px;",
    "active room's world-space bottom",
    "height_px > 0",
    "p_map_pixels_h_trampoline",
]:
    assert token in height_hook, f"variable-room native height query is incomplete: {token}"
start_room_hook = hooks[hooks.index("static void hooks_commit_authored_start_room"):
                        hooks.index("static int content_bridge_resolve_sprite")]
for token in [
    "custom_maps_start_room(selector)",
    "custom_maps_variable_room_bounds_2d_for_index",
    "PLAYER_OFS_ROOM",
    "*g_game_active_room = room;",
    "*g_game_old_active_room = -1;",
    "*g_camera_x = camera_x / (float)camera_samples;",
    "*g_camera_y = camera_y / (float)camera_samples;",
]:
    assert token in start_room_hook, f"authored start-room initialization is incomplete: {token}"
assert "game_init_prologue" in hooks
assert "(void*)&hooked_game_init" in hooks
physics_hook = hooks[hooks.index("static void hooks_remove_thing_below_authored_room"):
                     hooks.index("static void __cdecl hooked_game_update_camera")]
for token in [
    "custom_maps_variable_room_bounds(selector, x",
    "custom_maps_variable_room_bounds_2d_for_index",
    "y > (float)(start_y_px + height_px)",
    "p_thing_free(thing)",
    "p_sword_update_movement_trampoline(sword)",
    "p_hazard_update_movement_trampoline(hazard)",
]:
    assert token in physics_hook, f"short-room physics cleanup is incomplete: {token}"
death_hook = hooks[hooks.index("static void __cdecl hooked_player_die"):
                   hooks.index("void hooks_get_combat_ledger")]
for token in [
    "PLAYER_OFS_RESPAWN_UNGROUNDED",
    "custom_maps_variable_room_bounds_2d_for_index",
    "y > (float)(start_y + height)",
    "if (real) real(player_ptr);",
    "authored_pit_fall",
    "+ PLAYER_OFS_RESPAWN_UNGROUNDED) = 1u;",
]:
    assert token in death_hook, f"pit death must finish through native ungrounded respawn: {token}"
assert death_hook.index("authored_pit_fall =") < death_hook.index("if (real) real(player_ptr);")
assert death_hook.index("if (real) real(player_ptr);") < death_hook.index(
    "+ PLAYER_OFS_RESPAWN_UNGROUNDED) = 1u;")
player_batch = hooks[hooks.index("static int hooks_map_script_commit_player_batch"):
                     hooks.index("static int hooks_map_script_apply_player_velocities")]
assert "int selector=-1;" in player_batch
assert player_batch.index("if(room_mask) {") < player_batch.index("selector=*g_hook_map_selector;")
assert "IsBadReadPtr((const void*)g_hook_map_selector" in player_batch
build_hook = hooks[hooks.index("static void __cdecl hooked_mapgen_build_map"):
                   hooks.index("static int __cdecl hooked_thing_roomnum")]
assert "int selector = g_hook_map_selector ? *g_hook_map_selector : -1;" in build_hook
assert build_hook.index("int selector =") < build_hook.index("custom_maps_rebuild_variable_map(selector)")
assert "int selector = g_hook_map_selector" not in hooks[
    hooks.index("typedef struct OnlinePendingMatch"):hooks.index("} OnlinePendingMatch;")]

room_info_hook = hooks[hooks.index("static void* __cdecl hooked_mapgen_room_info"):
                       hooks.index("static void __cdecl hooked_mapgen_build_map")]
for token in [
    "custom_maps_pinned_room_definition",
    "canonical_room = (roomdef_count - 1) - source_room",
    "native_mirror = final_room < roomdef_count - 1",
    "NATIVE_ROOMDEF_PRIMARY_WORD",
    "NATIVE_ROOMDEF_MIRROR_WORD",
    "memcpy(scratch, source",
]:
    assert token in room_info_hook, f"room-graph roomdef lookup is incomplete: {token}"

print("variable_room_hooks_static_test: all checks passed")
