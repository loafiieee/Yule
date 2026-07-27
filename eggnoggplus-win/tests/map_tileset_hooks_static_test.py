from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")


def body(signature: str) -> str:
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


build = body("static void __cdecl hooked_mapgen_build_map(void)")
configure = body("static void hooks_map_native_tileset_configure(int selector)")
begin = body("static int hooks_map_native_tileset_begin_draw")
fallback = body("static int hooks_plot_native_tile_fallback")
underlay = body("static int hooks_draw_native_tile_underlay")
draw = body("static int __cdecl hooked_tile_action_ex")

# Every map build clears stale metadata.  Only a successfully bound V2 map
# whose validated content view enables native_layout can install a new sheet.
assert build.index("hooks_map_native_tileset_clear();") < build.index("real();")
assert build.index("content_bridge_bind_selector") < build.index(
    "hooks_map_native_tileset_configure(selector);"
)
assert "custom_maps_pinned_content_view(selector, &view)" in configure
assert "view.native_layout" in configure
assert "view.default_sheet_key" in configure
assert "view.default_sheet_sprite_count" in configure
assert "view.default_sheet_sprite_count < 128" in configure

# Atlas ids are not cached across rebuilds.  The external sheet's first sprite
# is resolved each draw and becomes the native `_tiles` pointer temporarily.
assert "lua_manager_content_resolve_sprite(g_map_native_tileset_sheet, 0" in begin
assert "lua_manager_content_resolve_sprite(g_map_native_tileset_sheet, 127" in begin
assert "last_sprite_id != sprite_id + 127" in begin
assert "p_sprite_get" in begin
assert "*out_saved_tiles = *g_layer;" in begin
assert "*g_layer = (int)(intptr_t)sprite;" in begin

# Reproduce map_draw's post-action generic fallback exactly: the tile-info base
# byte and flip byte are signed, the cell frame byte is unsigned, sprites are
# 0x1c-byte records, and the generic plot always targets batch layer zero.
assert "NATIVE_TILE_INFO_COUNT = 33" in fallback
assert "NATIVE_TILE_INFO_STRIDE = 0x2c" in fallback
assert "NATIVE_TILE_INFO_SPRITE_BASE_OFFSET = 4" in fallback
assert "NATIVE_TILE_SPRITE_COUNT = 128" in fallback
assert "NATIVE_TILE_SPRITE_STRIDE = 0x1c" in fallback
assert "ADDR_TILE_INFO +" in fallback
assert "sprite_index = (int)(*native_base_ptr) + (int)tile_bytes[1];" in fallback
assert "sprite_index < 0 || sprite_index >= NATIVE_TILE_SPRITE_COUNT" in fallback
assert "flip = (int)(signed char)tile_bytes[2];" in fallback
assert "plot((int)(intptr_t)sprite_address, flip, 0);" in fallback

# An opt-in native underlay calls the trampoline exactly once before custom
# rendering, completes a zero-return action's generic fallback before restoring
# the map atlas, and restores the complete turtle state so the custom sprite
# keeps its normal transform/tint basis.
assert "memcpy(saved_turtle, turtle, sizeof(saved_turtle));" in underlay
underlay_begin = underlay.index("hooks_map_native_tileset_begin_draw")
underlay_base = underlay.index("native_sprite_base =", underlay_begin)
underlay_call = underlay.index("result = real(tile, mode, x, y, arg5);")
underlay_fallback = underlay.index(
    "hooks_plot_native_tile_fallback", underlay_call
)
underlay_handled = underlay.index("result = 1;", underlay_fallback)
underlay_end = underlay.index("hooks_map_native_tileset_end_draw")
underlay_restore = underlay.index(
    "memcpy(turtle, saved_turtle, sizeof(saved_turtle));"
)
assert underlay.count("real(tile, mode, x, y, arg5)") == 1
assert "g_map_tile_layer ? *g_map_tile_layer : 0" in underlay
assert (
    underlay_begin
    < underlay_base
    < underlay_call
    < underlay_fallback
    < underlay_handled
    < underlay_end
    < underlay_restore
)

query = draw.index("content_tiles_native_visual_underlay_for_action")
underlay_draw = draw.index("hooks_draw_native_tile_underlay")
custom_draw = draw.index("content_bridge_draw_action")
custom_return = draw.index("return 1;", custom_draw)
already_drawn_return = draw.index("if (native_underlay_invoked) return result;")
native_begin = draw.index("hooks_map_native_tileset_begin_draw", already_drawn_return)
native_call = draw.index("result = real(tile, mode, x, y, arg5);", native_begin)
native_fallback = draw.index("hooks_plot_native_tile_fallback", native_call)
native_handled = draw.index("result = 1;", native_fallback)
native_end = draw.index("hooks_map_native_tileset_end_draw", native_handled)
assert query < underlay_draw < custom_draw < custom_return < already_drawn_return
assert (
    already_drawn_return
    < native_begin
    < native_call
    < native_fallback
    < native_handled
    < native_end
)
assert "swapped_tiles && result == 0 && g_layer" in draw
assert "if (mode == 2)" in draw

print("map_tileset_hooks_static_test: all checks passed")
