#ifndef CURSOR_EXT_H
#define CURSOR_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the game's exact vanilla mouse cursor at an unscaled window-space
 * position. This uses the native two-pass shadow/color renderer, native global
 * scale, pulse timing, and hotspot transform. Framework-owned custom screens
 * keep their cursor visible even when the vanilla main-menu inactivity counter
 * is zero; the helper restores that counter plus the caller's current/home
 * turtle state before returning.
 *
 * Returns 1 when the native renderer was invoked, or 0 when the fixed-address
 * dependencies fail validation. */
int cursor_ext_draw_vanilla_mouse(float x, float y);

/* Invoke a native fallback draw while suppressing only its mouse cursor pass.
 * This is used when a framework screen must ask main_draw for a background and
 * will queue its own final cursor afterward. Returns 1 when callback execution
 * completed with the timeout restored, or 0 when validation failed. */
typedef void (*cursor_ext_draw_callback)(void);
int cursor_ext_call_with_vanilla_mouse_hidden(cursor_ext_draw_callback draw);

#ifdef __cplusplus
}
#endif

#endif
