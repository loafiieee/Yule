from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NET = (ROOT / "ggpo_net.c").read_text(encoding="utf-8")
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
LUA_HEADER = (ROOT / "lua_manager.h").read_text(encoding="utf-8")
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


capture = function_body(NET, "static int ggpo_net_capture_clean_sim_state")
restore = function_body(NET, "static int ggpo_net_restore_clean_sim_state")
start_load = function_body(NET, "static int ggpo_net_load_start_state_if_ready")
prepare = function_body(NET, "int ggpo_net_prepare_prematch_start")
state_chunk = function_body(NET, "static void ggpo_net_handle_state_chunk")
tick_abort = function_body(NET, "static int ggpo_net_abort_live_tick_and_restore")
rollback = function_body(NET, "static int ggpo_net_apply_rollback_if_needed")
correction = function_body(NET, "static int ggpo_net_apply_correction_and_replay")
live = function_body(NET, "int ggpo_net_advance(")
width_get = function_body(LUA, "int lua_manager_game_width")
width_set = function_body(LUA, "int lua_manager_game_set_width")
height_get = function_body(LUA, "int lua_manager_game_height")
height_set = function_body(LUA, "int lua_manager_game_set_height")
geometry_set = function_body(LUA, "int lua_manager_game_set_geometry")
sim_set = function_body(LUA, "int lua_manager_game_set_sim_state")
palette_capture_fn = function_body(LUA, "int lua_manager_game_palette_capture")
palette_restore_fn = function_body(LUA, "int lua_manager_game_palette_restore")
cosmetic_rng_classifier = function_body(
    HOOKS, "static int hooks_rng_caller_is_cosmetic"
)

# Both extents are written from local drawable geometry by native layout code
# but consumed by gameplay. Keep their checked accessors and the clean post-tick
# simulation snapshot as one inseparable boundary.
assert "int lua_manager_game_width(float* out_width);" in LUA_HEADER
assert "int lua_manager_game_set_width(float width);" in LUA_HEADER
assert "int lua_manager_game_height(float* out_height);" in LUA_HEADER
assert "int lua_manager_game_set_height(float height);" in LUA_HEADER
assert "int lua_manager_game_set_geometry(float game_w, float game_h);" in LUA_HEADER
assert "int lua_manager_game_set_sim_state(uint32_t seed" in LUA_HEADER
assert "typedef struct LuaGamePaletteState" in LUA_HEADER
assert "int lua_manager_game_palette_capture(LuaGamePaletteState* out_state);" in LUA_HEADER
assert (
    "int lua_manager_game_palette_restore(const LuaGamePaletteState* state);"
    in LUA_HEADER
)
assert "ptr_readable((const void*)p_game_w" in width_get
assert "game_sim_extent_valid(width)" in width_get
assert "ptr_writable((void*)p_game_w" in width_set
assert "game_sim_extent_valid(width)" in width_set
assert "ptr_readable((const void*)p_game_h" in height_get
assert "game_sim_extent_valid(height)" in height_get
assert "ptr_writable((void*)p_game_h" in height_set
assert "game_sim_extent_valid(height)" in height_set
assert "ptr_writable((void*)p_game_w" in geometry_set
assert "ptr_writable((void*)p_game_h" in geometry_set
assert geometry_set.index("ptr_writable((void*)p_game_h") < geometry_set.index(
    "*p_game_w = game_w"
)
first_write = sim_set.index("*p_mrand_seed = seed")
for preflight in (
    "ptr_writable((void*)p_mrand_seed",
    "ptr_writable((void*)p_camera_x",
    "ptr_writable((void*)p_camera_y",
    "ptr_writable((void*)p_camera_shake",
    "ptr_writable((void*)p_camera_shake_decay",
    "ptr_writable((void*)p_game_w",
    "ptr_writable((void*)p_game_h",
):
    assert sim_set.index(preflight) < first_write
assert "game_palette_state_valid(out_state)" in palette_capture_fn
palette_first_write = palette_restore_fn.index("memcpy(colours")
for preflight in (
    "game_palette_state_valid(state)",
    "ptr_writable((void*)p_game_do_lerp_colours",
    "ptr_writable((void*)p_lerp_time",
    "ptr_writable(colours",
):
    assert palette_restore_fn.index(preflight) < palette_first_write
assert "lua_manager_game_width(&game_w)" in capture
assert "lua_manager_game_height(&game_h)" in capture
assert "lua_manager_game_camera_shake(&camera_shake" in capture
assert "g_net.clean_camera_shake = camera_shake" in capture
assert "g_net.clean_camera_shake_decay = camera_shake_decay" in capture
assert "g_net.clean_game_w = game_w" in capture
assert "g_net.clean_game_h = game_h" in capture
assert "g_net.have_clean_sim_state = 1" in capture
assert "g_net.have_clean_sim_state = 0" not in capture
assert "lua_manager_game_set_sim_state(g_net.clean_mrand_seed" in restore
assert "g_net.clean_game_w" in restore and "g_net.clean_game_h" in restore
assert "g_net.clean_camera_shake" in restore
assert "g_net.clean_camera_shake_decay" in restore
assert "lua_manager_game_set_rng_seed" not in restore
assert "lua_manager_game_set_camera" not in restore
assert "lua_manager_game_set_width" not in restore
assert "rollback_canonicalization_version = 6u" in LUA
assert "ADDR_MINE_ANIM_LAST_TICK" in LUA
assert "full_state_zero_transient_range(hdr, ADDR_MINE_ANIM_LAST_TICK" not in LUA
assert "ADDR_SOUND_DEDUP_TIMERS_B" not in LUA
assert "hdr->game_h = 0.0f" not in LUA
assert "hdr->camera_shake = 0.0f" not in LUA
assert "hdr->camera_shake_decay = 0.0f" not in LUA
assert "!ptr_writable((void*)p_game_h" in LUA
assert "have_clean_mrand_seed" not in NET
assert "0x004222A0u" not in cosmetic_rng_classifier
assert "case 0x0042C9B8u:" in cosmetic_rng_classifier
assert "near-win chant sound pitch" in cosmetic_rng_classifier
assert "Camera shake is" in cosmetic_rng_classifier
assert "rollback-owned gameplay RNG" in cosmetic_rng_classifier

# An authoritative start load establishes the first clean snapshot. Live,
# rollback, and correction paths must restore it before saving a pre-state and
# recapture only after the native tick succeeds.
assert start_load.index("ggpo_net_apply_received_state_transaction") < start_load.index(
    "ggpo_net_capture_clean_sim_state"
)
assert start_load.index("ggpo_net_capture_clean_sim_state") < start_load.index(
    "ggpo_net_restore_local_render_geometry"
)
assert prepare.index("ggpo_net_capture_local_render_geometry") < prepare.index(
    "ggpo_net_load_start_state_if_ready"
)
prepare_load = prepare.index("ggpo_net_load_start_state_if_ready")
prepare_loaded_guard = prepare.index("if (!g_net.start_state_loaded)", prepare_load)
prepare_publish = prepare.index("ggpo_net_track_remote_cmd", prepare_loaded_guard)
assert prepare_load < prepare_loaded_guard < prepare_publish
chunk_local = state_chunk.index("ggpo_net_capture_local_render_geometry")
chunk_apply = state_chunk.index("ggpo_net_apply_received_state_transaction", chunk_local)
chunk_restore = state_chunk.index("ggpo_net_restore_local_render_geometry", chunk_apply)
assert chunk_local < chunk_apply < chunk_restore
load_call = live.index("ggpo_net_load_start_state_if_ready")
live_local_capture = live.index("ggpo_net_capture_local_render_geometry")
loaded_guard = live.index("if (!g_net.start_state_loaded)", load_call)
input_commit = live.index("ggpo_net_queue_local_input", loaded_guard)
assert live_local_capture < load_call < loaded_guard < input_commit

assert tick_abort.index("ggpo_ext_load_game_state") < tick_abort.index(
    "ggpo_net_restore_clean_sim_state"
)
assert tick_abort.index("ggpo_net_restore_clean_sim_state") < tick_abort.index(
    "ggpo_net_restore_local_render_geometry"
)
assert "g_net.peer_disconnected = 1" in tick_abort

live_restore = live.index("ggpo_net_restore_clean_sim_state")
live_save = live.index("ggpo_net_save_pre_state", live_restore)
live_tick = live.index("ggpo_ext_advance_frame", live_save)
live_capture = live.index("ggpo_net_capture_clean_sim_state", live_tick)
assert live_restore < live_save < live_tick < live_capture
live_boundary = live.index("ggpo_net_capture_post_state", live_capture)
live_local_restore = live.index("ggpo_net_restore_local_render_geometry", live_boundary)
assert live_capture < live_boundary < live_local_restore
assert live.count("ggpo_net_abort_live_tick_and_restore") == 4
assert live.count("ggpo_net_capture_local_render_geometry") == 1

palette_capture = rollback.index("lua_manager_game_palette_capture")
rollback_load = rollback.index("ggpo_ext_load_game_state", palette_capture)
palette_restore = rollback.index("lua_manager_game_palette_restore", rollback_load)
rollback_capture = rollback.index("ggpo_net_capture_clean_sim_state", palette_restore)
rollback_restore = rollback.index("ggpo_net_restore_clean_sim_state", rollback_capture)
rollback_save = rollback.index("ggpo_net_save_pre_state", rollback_restore)
rollback_tick = rollback.index("ggpo_net_replay_frame", rollback_save)
rollback_recapture = rollback.index("ggpo_net_capture_clean_sim_state", rollback_tick)
rollback_boundary = rollback.index("ggpo_net_capture_post_state", rollback_recapture)
assert (
    palette_capture
    < rollback_load
    < palette_restore
    < rollback_capture
    < rollback_restore
    < rollback_save
    < rollback_tick
    < rollback_recapture
    < rollback_boundary
)

correction_palette_capture = correction.index("lua_manager_game_palette_capture")
correction_load = correction.index(
    "ggpo_net_apply_received_state_transaction", correction_palette_capture
)
correction_palette_restore = correction.index(
    "lua_manager_game_palette_restore", correction_load
)
correction_capture = correction.index(
    "ggpo_net_capture_clean_sim_state", correction_palette_restore
)
correction_restore = correction.index("ggpo_net_restore_clean_sim_state", correction_capture)
correction_save = correction.index("ggpo_net_save_pre_state", correction_restore)
correction_tick = correction.index("ggpo_ext_advance_frame", correction_save)
correction_recapture = correction.index(
    "ggpo_net_capture_clean_sim_state", correction_tick
)
correction_boundary = correction.index(
    "ggpo_net_capture_post_state", correction_recapture
)
assert (
    correction_palette_capture
    < correction_load
    < correction_palette_restore
    < correction_capture
    < correction_restore
    < correction_save
    < correction_tick
    < correction_recapture
    < correction_boundary
)

print("simulation_geometry_static_test: all checks passed")
