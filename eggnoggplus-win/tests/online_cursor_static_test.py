from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")
DLLMAIN = (ROOT / "dllmain.c").read_text(encoding="utf-8")
CURSOR_EXT = (ROOT / "cursor_ext.c").read_text(encoding="utf-8")
CURSOR_EXT_H = (ROOT / "cursor_ext.h").read_text(encoding="utf-8")
LUA = (ROOT / "lua_manager.c").read_text(encoding="utf-8")
BUILD = (ROOT / "compile.sh").read_text(encoding="utf-8")
GHIDRA = (ROOT / "ghidra" / "eggnoggplus.exe.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    start = SOURCE.index(marker)
    brace = SOURCE.index("{", start)
    depth = 0
    for pos in range(brace, len(SOURCE)):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[brace + 1 : pos]
    raise AssertionError(f"unterminated function: {name}")


cursor = function_body("static void online_hub_draw_cursor")
cursor_background = function_body("static void online_hub_render_background")
cursor_overlay = function_body("static int online_cursor_over_active_overlay")
cursor_pre_swap = function_body("void hooks_online_cursor_on_pre_swap")

# Online must use the game's full native mouse renderer, not merely reuse its
# artwork with a framework-authored scale/tint/hotspot approximation.
assert "cursor_ext_draw_vanilla_mouse(g_online_mouse_x, g_online_mouse_y)" in cursor
assert "online_hub_native_cursor_scale" not in SOURCE
assert "8.0f * scale" not in cursor
assert "p_turtle_set_rgba(1.0f, 1.0f, 1.0f, 1.0f)" not in cursor
assert "p_sprite_batch_plot" not in cursor
assert '"YYY........."' not in SOURCE

# Exact reverse-engineered native ABI and preconditions: menu cursor_draw,
# global layout scale, temporary mouse visibility, EDX=0 mouse mode, then the
# EAX=1 shadow pass followed by EAX=0 animated color pass.
assert "#define ADDR_CURSOR_DRAW       0x42FEB0u" in CURSOR_EXT
assert "#define ADDR_GLOBAL_SCALE      0x448384u" in CURSOR_EXT
assert "#define ADDR_MOUSE_TIMEOUT     0x547B64u" in CURSOR_EXT
assert '"movl 16(%esp), %eax\\n\\t"' in CURSOR_EXT
assert '"xorl %edx, %edx\\n\\t"' in CURSOR_EXT
assert CURSOR_EXT.count('"pushl 12(%esp)\\n\\t"') == 3
assert '"movl $0x42FEB0, %ecx\\n\\t"' in CURSOR_EXT
assert "turtle_reset();" in CURSOR_EXT
assert "turtle_set_scale((double)scale);" in CURSOR_EXT
assert "if (!(scale > 0.0f) || scale > FLT_MAX) return 0;" in CURSOR_EXT
assert "scale < 0.25f" not in CURSOR_EXT
assert "scale > 16.0f" not in CURSOR_EXT
assert "#define ADDR_TURTLE_HOME       0x448060u" in CURSOR_EXT
assert "#define ADDR_TURTLE_STATE      0x4480C0u" in CURSOR_EXT
assert "#define TURTLE_STATE_SIZE      0x60u" in CURSOR_EXT
assert "memcpy(saved_turtle_home, turtle_home, TURTLE_STATE_SIZE);" in CURSOR_EXT
assert "memcpy(saved_turtle_state, turtle_state, TURTLE_STATE_SIZE);" in CURSOR_EXT
assert "saved_mouse_timeout = *mouse_timeout;" in CURSOR_EXT
shadow = CURSOR_EXT.index("cursor_ext_native_mouse_pass(x, y, 0.0f, 1);")
color = CURSOR_EXT.index("cursor_ext_native_mouse_pass(x, y, 0.0f, 0);")
restore_timeout = CURSOR_EXT.index("*mouse_timeout = saved_mouse_timeout;", color)
restore_state = CURSOR_EXT.index("memcpy(turtle_state, saved_turtle_state, TURTLE_STATE_SIZE);", restore_timeout)
restore_home = CURSOR_EXT.index("memcpy(turtle_home, saved_turtle_home, TURTLE_STATE_SIZE);", restore_state)
assert shadow < color < restore_timeout < restore_state < restore_home
assert "sprite_batch_plot(_misc + 0xc4,0,0);" in GHIDRA
assert "cursor_ext_call_with_vanilla_mouse_hidden" in CURSOR_EXT_H
assert "*mouse_timeout = 0;" in CURSOR_EXT
assert "cursor_ext_call_with_vanilla_mouse_hidden(p_main_draw)" in cursor_background
assert "p_main_draw();" not in cursor_background

# Automatic Lua custom-state cursors share the exact native default. Explicit
# mod.ui.draw_cursor with no/empty options uses the same helper, while explicit
# rendering options remain customizable by design.
default_lua_start = LUA.index("static void ui_draw_default_custom_state_cursor")
default_lua_end = LUA.index("static void ui_draw_text_mode_alpha", default_lua_start)
default_lua = LUA[default_lua_start:default_lua_end]
assert "cursor_ext_draw_vanilla_mouse" in default_lua
assert "p_sprite_batch_plot" not in default_lua
native_binding_start = LUA.index("static int lua_ui_draw_native_cursor")
native_binding_end = LUA.index("static int lua_ui_hitbox", native_binding_start)
native_binding = LUA[native_binding_start:native_binding_end]
assert "cursor_ext_draw_vanilla_mouse" in native_binding
assert "g_ui_default_custom_cursor_suppressed = 1" in native_binding
assert 'lua_setfield(Ls, -2, "_draw_native_cursor")' in LUA
assert "if opts == nil or (type(opts) == 'table' and next(opts) == nil) then" in LUA
assert "ui._draw_native_cursor and ui._draw_native_cursor()" in LUA
assert "cursor_ext.c" in BUILD

# One dedicated final pre-swap draw keeps the native cursor above every online
# branch, Lua on_frame, nametags, console rendering, and updater notifications.
assert SOURCE.count("online_hub_draw_cursor();") == 1
assert "state_ptr == (void*)&g_online_hub_state" in cursor_pre_swap
assert "state_ptr == (void*)&g_online_result_state" in cursor_pre_swap
assert "online_cursor_over_active_overlay(state_ptr" in cursor_pre_swap
assert "g_online_result.active" in cursor_overlay
assert "g_online_result_toast_rendered_this_swap" in cursor_overlay
assert "online_result_toast_metrics" in cursor_overlay
assert "g_online_challenge_toast.active" in cursor_overlay
assert "g_online_challenge_toast_rendered_this_swap" in cursor_overlay
assert "online_challenge_toast_metrics" in cursor_overlay
assert cursor_overlay.count("x >= bx && x <= bx + bw && y >= by && y <= by + bh") == 2
cursor_call = cursor_pre_swap.index("online_hub_draw_cursor();")
batch_flush = cursor_pre_swap.index("p_main_sprite_batches_draw()", cursor_call)
assert cursor_call < batch_flush
assert "g_online_result_toast_rendered_this_swap = 0" in cursor_pre_swap
assert "g_online_challenge_toast_rendered_this_swap = 0" in cursor_pre_swap

online_call = DLLMAIN.index("hooks_online_on_pre_swap();")
console_call = DLLMAIN.index("hooks_console_on_pre_swap();", online_call)
update_call = DLLMAIN.index("hooks_update_on_pre_swap();", console_call)
final_cursor_call = DLLMAIN.index("hooks_online_cursor_on_pre_swap();", update_call)
swap_call = DLLMAIN.index("real_SwapWindow(window)", final_cursor_call)
assert online_call < console_call < update_call < final_cursor_call < swap_call

print("online native cursor static checks: OK")
