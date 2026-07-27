from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = SOURCE.index(signature)
    brace = SOURCE.index("{", start)
    depth = 0
    for index in range(brace, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def assert_after(body: str, first: str, second: str) -> None:
    assert first in body, first
    assert second in body, second
    assert body.index(second, body.index(first)) > body.index(first)


simulate = function_body("int hooks_simulate_game_ticks(int count, int arg0)")
advance = function_body("int hooks_advance_game_tick(int arg0, int run_framework_tick)")
game_update = function_body("static void __cdecl hooked_game_update(int arg0)")
native_tick = function_body("static int hooks_native_game_tick_callback(void* user)")
run_native_tick = function_body("static int hooks_run_native_game_tick")
apply_all = function_body("static void hooks_apply_content_tile_interactions(void)")
apply_body = function_body("static void hooks_apply_content_interactions_to_body")
kind_for_body = function_body("static int hooks_map_script_kind_for_body")

# These three GAME paths converge on one one-tick primitive. Keeping native
# update and content interaction together makes offline, live GGPO, and
# rollback replay use identical interaction ordering under one FP guard.
assert_after(native_tick, "tick->update(tick->arg0);", "hooks_apply_content_tile_interactions();")
assert "fp_control_run_canonical_tick" in run_native_tick
assert "hooks_run_native_game_tick(real_update, arg0, 1)" in simulate
assert "hooks_run_native_game_tick(real_update, arg0, 1)" in advance
assert "hooks_run_native_game_tick(real_update, arg0, 0)" in game_update
assert "real_update(arg0);" not in simulate
assert "real_update(arg0);" not in advance

# The verified pool is exactly 16 native 0x15c-byte records, bounded by the
# adjacent thing-info block. Type 2 is a physics sword; K is accepted only as
# type 3 with its exact native updater. Unknown records remain excluded.
assert "#define THING_SLOT_COUNT              ((ADDR_THING_INFO - ADDR_THINGS) / THING_SIZE)" in SOURCE
assert "#define ADDR_THINGS                   0x542080u" in SOURCE
assert "#define ADDR_THING_INFO               0x543640u" in SOURCE
assert "thing_slot < THING_SLOT_COUNT" in apply_all
assert "#define THING_TYPE_SWORD              0x02u" in SOURCE
assert "#define THING_TYPE_HAZARD             0x03u" in SOURCE
assert "#define THING_OFS_UPDATE_FN           0x158u" in SOURCE
assert "#define ADDR_HAZARD_ANIM              0x43C450u" in SOURCE
assert "thing[THING_OFS_TYPE] == THING_TYPE_SWORD" in kind_for_body
assert "thing[THING_OFS_TYPE] != THING_TYPE_HAZARD" in kind_for_body
assert "update_fn == (uint32_t)ADDR_HAZARD_ANIM" in kind_for_body
assert "MAP_SCRIPT_OBJECT_HAZARD" in apply_all

# Native player movement restores the center before it enters a solid cell, so
# live and dead players get a half-tile foot boundary plus center. Verified
# sword movement gets one center sample. K participates only in explicit
# programmable sensors and never inherits legacy declarative force behavior.
assert "include_player_foot_probe ? 2 : 1" in apply_body
assert "sample == 1 ? (float)tile_h * 0.5f : 0.0f" in apply_body
assert "object_kind != MAP_SCRIPT_OBJECT_HAZARD" in apply_body
assert "hooks_map_script_kind_for_body(player)" in apply_all
assert "object_kind != MAP_SCRIPT_OBJECT_PLAYER" in apply_all
assert "object_kind != MAP_SCRIPT_OBJECT_DEAD_BODY" in apply_all
assert "object_kind != MAP_SCRIPT_OBJECT_SWORD" in apply_all
assert "object_kind != MAP_SCRIPT_OBJECT_HAZARD" in apply_all
assert "script_object.object_kind = (uint8_t)object_kind" in apply_body
assert "contacts[i].cell_index == interaction.cell_index" in apply_body
assert "content_tiles_apply_interaction_velocity" in apply_body

print("content_force_hooks_static_test: all checks passed")
