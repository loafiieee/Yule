from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
DLLMAIN = (ROOT / "dllmain.c").read_text(encoding="utf-8")
STUBS = (ROOT / "stubs.c").read_text(encoding="utf-8")


def body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated body: {marker}")


refresh = body(HOOKS, "static void hooks_window_refresh_geometry")
pre_swap = body(HOOKS, "void hooks_window_on_pre_swap")
dll_entry = body(DLLMAIN, "BOOL WINAPI DllMain")
poll_event = body(DLLMAIN, "int SDL_PollEvent")
window_keydown = body(HOOKS, "int hooks_window_keydown")
window_event = body(HOOKS, "void hooks_window_event")
window_pump = body(HOOKS, "static void hooks_window_pump")
main_update_hook = body(HOOKS, "static int __cdecl hooked_main_update_with_buttons")
sync_desktop = body(HOOKS, "static int hooks_window_sync_native_desktop_bounds")
apply_mode = body(HOOKS, "static int hooks_window_apply_mode")
set_native_size = body(HOOKS, "static int hooks_window_set_native_size")
destroy_window = body(STUBS, "void __cdecl SDL_DestroyWindow")
destroy_window_compact = "".join(destroy_window.split())

# Event/update-thread ownership: no window policy is started from loader lock,
# and pre-swap may reconcile GL state but may not recreate the SDL window.
assert "hooks_window" not in dll_entry
assert "p_main_set_window" not in pre_swap
assert "p_main_set_fullscreen" not in pre_swap
assert "glViewport(0, 0, dw, dh);" in refresh
assert "g_game_w_native" not in refresh
assert "g_game_h_native" not in refresh
assert "if (ww > 0 && wh > 0)" in refresh
assert "*p_wrapper_display_w = ww;" in refresh
assert "*p_wrapper_display_h = wh;" in refresh
assert "ADDR_WRAPPER_DISPLAY_W         0x44812Cu" in HOOKS
assert "ADDR_WRAPPER_DISPLAY_H         0x448128u" in HOOKS
assert refresh.index("*p_wrapper_display_w = ww;") < refresh.index("glViewport(0, 0, dw, dh);")
geometry_change_test = refresh[
    refresh.index("changed =") : refresh.index("if (dw > 0")
]
assert "flags !=" not in geometry_change_test
assert "last_window_flags = flags" in refresh

# SDL_PollEvent only records/consumes window input. All recreation work runs
# after wrapper_handle_events, at the hooked state-update boundary.
assert "hooks_window_pump();" not in poll_event
assert "hooks_window_pump();" in main_update_hook
assert HOOKS.count("hooks_window_pump();") == 1
assert "hooks_window_keydown(event->key.keysym.sym, event->key.repeat)" in DLLMAIN
assert "case SDL_WINDOWEVENT:" in DLLMAIN
assert "hooks_window_event((int)event->window.event" in DLLMAIN
assert "hooks_window_on_pre_swap();" in DLLMAIN
assert "hooks_window_queue_key_action(action)" in window_keydown
assert "hooks_window_apply_mode" not in window_keydown
assert "hooks_window_cycle_native_size" not in window_keydown
assert "p_main_set_window" not in window_keydown
assert "p_main_set_fullscreen" not in window_keydown

# Borderless is desktop fullscreen after native setup, is verified, and launch
# failures get bounded retries rather than a one-shot permanent failure.
assert "SDL_WINDOW_FULLSCREEN_DESKTOP_FLAG" in HOOKS
assert "hooks_window_mode_matches" in HOOKS
assert "g_window_runtime.launch_attempts >= 5" in HOOKS
assert "static int applied = 0" not in HOOKS
assert "static int settle = 30" not in HOOKS

# Native recreation is redirected to the old window's active display only for
# the duration of the transition; ordinary SDL_GetDisplayBounds(0) semantics
# remain untouched outside that scope.
assert "g_proxy_sdl_display_override = -1" in STUBS
assert "InterlockedExchange(&g_proxy_sdl_display_override, (LONG)display)" in HOOKS
assert "InterlockedExchange(&g_proxy_sdl_display_override, -1)" in HOOKS
assert "centered_mask | ((unsigned int)override_index" in STUBS
assert "naked)) void SDL_GetDisplayBounds" not in STUBS

# Native fullscreen reads cached desktop dimensions before wrapper_set_graphics
# reaches SDL_GetDisplayBounds, so validated active-display values must be
# written first on both fullscreen and borderless entry.
assert "ADDR_WRAPPER_DESKTOP_H         0x4EDB70u" in HOOKS
assert "ADDR_WRAPPER_DESKTOP_W         0x4EDB74u" in HOOKS
assert "if (!hooks_window_display_bounds" in sync_desktop
assert "*p_wrapper_desktop_w = display_w;" in sync_desktop
assert "*p_wrapper_desktop_h = display_h;" in sync_desktop
assert "mode == WINDOW_LAUNCH_FULLSCREEN || mode == WINDOW_LAUNCH_BORDERLESS" in apply_mode
assert apply_mode.index("hooks_window_sync_native_desktop_bounds") < apply_mode.index("p_main_set_fullscreen(1)")

# Maximize is one debounced request for the largest fitting native preset.
# The native-size transition clears the request before its suppress-filtered
# recreation events can be observed as another user resize.
assert "WINDOW_EVENT_MAXIMIZED = 8" in HOOKS
assert "g_window_runtime.pending_maximize = 1;" in window_event
assert "window_policy_largest_preset" in window_pump
assert '"maximize"' in window_pump
assert window_pump.count('"maximize"') == 1
assert "g_window_runtime.pending_maximize = 0;" in set_native_size
assert "WINDOW_TRANSITION_SUPPRESS_MS" in set_native_size

# Destroying the captured main window clears only the matching proxy pointer
# before forwarding to SDL, so no hook can observe the destroyed SDL_Window.
assert "naked)) void SDL_DestroyWindow" not in STUBS
assert (
    "InterlockedCompareExchangePointer((PVOIDvolatile*)&g_proxy_sdl_window,NULL,window);"
    in destroy_window_compact
)
assert "((destroy_t)p_SDL_DestroyWindow)(window);" in destroy_window_compact
assert destroy_window.index("InterlockedCompareExchangePointer") < destroy_window.index("p_SDL_DestroyWindow")

print("window runtime static checks: OK")
