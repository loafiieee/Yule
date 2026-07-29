# Framework Cursor Rendering Policy

**Date:** 2026-07-17  
**Status:** Exact native online/default-custom-state renderer implemented and statically guarded; live visual acceptance still required  
**Primary code:** `cursor_ext.c`, `cursor_ext.h`, online integration in `hooks.c`,
custom-state API in `lua_manager.c`, `tests/online_cursor_static_test.py`, and
`tests/online_menu_tick_static_test.py`

## Problem and correction

The first custom-menu cursor implementation copied only the game's `misc[7]` artwork.
The built-in online hub originally used a hand-authored 12-by-12 imitation, and the first
replacement still plotted the real sprite with framework-authored white tint, square-root
scale, and hotspot offset. That did not reproduce the vanilla cursor's animated color,
shadow, global scale, or native transform.

Framework-owned online screens and automatic Lua custom-state cursors now call the exact
native mouse renderer at `0x42FEB0`. `mod.ui.draw_cursor()` with no options (or an empty
option table) uses that same path. Explicit cursor options remain the supported way for a
mod to request a deliberately customized sprite, scale, hotspot, tint, or layer.

## Reverse-engineered native contract

The checked executable establishes the following fixed-address contract:

- `cursor_draw` is at `0x42FEB0`;
- `_global_scale` is at `0x448384`;
- `_mouse_timeout` is at `0x547B64`;
- `turtle_reset` is at `0x4092D0`; and
- uniform `turtle_set_scale(double)` is at `0x409080`.

`cursor_draw(float x, float y, float spin)` also consumes two hidden register inputs. EDX
selects mouse versus player mode, and EAX selects the color versus shadow pass. An
ordinary C function-pointer call cannot express that ABI safely, so `cursor_ext.c` uses a
small 32-bit GCC wrapper that sets the registers and pushes the three float arguments in
the exact native order.

For the mouse cursor, the vanilla main-menu order is:

1. EAX = 1 and EDX = 0: offset black shadow with alpha 0.75;
2. EAX = 0 and EDX = 0: animated color pass.

The color pass is red with a pulsing green channel:

```text
R = 1
G = 0.75 + 0.25 * sin(mad_ticks * 20 degrees)
B = 0
A = 1
```

The native function positions the turtle in unscaled window coordinates, applies its
`(+4, -4)` mouse hotspot transform, applies an additional signed eight-pixel offset for
the shadow, and queues `_misc + 0xc4`. With the native sprite-record stride of `0x1c`,
that address is exactly `misc[7]`. It saves and restores all 96 bytes of turtle state
around each pass.

## Scale and visibility policy

`turtle_reset` clears turtle scale to zero. Before invoking `cursor_draw`, the shared
helper therefore resets render state and explicitly installs the game's live
`_global_scale`, matching the precondition established by `main_buttons_start`. The old
framework square-root scale and manual hotspot adjustment are forbidden in the default
path. Because `turtle_reset` also overwrites the native home state, the helper snapshots
and restores both 96-byte current/home turtle blocks around the complete two-pass draw;
calling the public Lua helper cannot leak the prepared cursor state into later mod draws.

Vanilla suppresses the mouse pass when `_mouse_timeout` is zero. Framework-owned screens
consume SDL mouse motion outside the native main-menu input path, so that counter is not
reliably refreshed there. The framework's cursor policy is always-visible while one of
its mouse-driven screens is open: the helper temporarily changes a non-positive timeout
to one for both native passes and restores the exact prior value before returning. This
retains the native visual effects without leaking or taking ownership of native input
state.

All fixed addresses, code pointers, readable/writable data, and a finite positive native
scale are validated before drawing. Valid large display scales are not artificially
capped. If validation fails, the built-in path draws nothing. It must
not restore the fake rectangle cursor or silently fall back to a different default.

If online background capture is unavailable, the hub renderer may ask native
`main_draw` for a background. The shared helper temporarily sets the native mouse timeout
to zero only around that fallback call and restores it afterward, preventing the fallback
from drawing a cursor underneath the final framework cursor.

## Built-in online ordering

Online drawing is centralized in `hooks_online_cursor_on_pre_swap`. Its predicate covers
the online hub state, every supported native/framework menu over a live online match,
and the portion of an active result/challenge overlay under the mouse. Native menus and
Lua custom states ordinarily draw a cursor earlier in the frame;
outside a late overlay that cursor remains visible, so drawing it again would only darken
the native shadow/antialiased edge. Live online menu simulation is a special case:
`game_update(1)` calls the native two-player cursor disable routine even though it is only
being run invisibly to keep the peer synchronized. The common menu update hook therefore
saves both disable flags at `0x549124`, advances the online frame, and restores their exact
values. It also requests one final native mouse pass for supported live options, both
input-remapping screens, and Mods states, ensuring mouse operation remains available
above the hidden gameplay render. When the mouse overlaps a late panel, the final pass
places the cursor above the panel that covered the earlier draw. `dllmain.c` calls
this renderer after the game's pass, Lua `on_frame`, online cards/toasts/nametags, console,
and updater notification, immediately before the real buffer swap. It queues the native
shadow and color passes once and flushes them once, making the cursor the final framework
layer without branch-specific duplicates.

Result/challenge overlay rendering is latched for the current swap. If a lifetime tick
expires immediately after the panel rendered, the final cursor still sees the latch and
remains above that last visible frame. Both latches are cleared after the final cursor
decision.

## Lua custom-state API

Lua UI exposes:

- `mod.ui.cursor_sprite([index=7])`;
- `mod.ui.draw_cursor([opts])`; and
- `cursor` policy on `mod.ui.define_state`.

Automatic custom-state drawing and `draw_cursor()`/`draw_cursor({})` use the shared exact
native renderer. A successful explicit no-option draw suppresses the later automatic
draw for that frame.

Passing rendering options selects the configurable sprite renderer instead. Supported
options are `sprite`, `index`, `scale`, `size`, `hot_x`, `hot_y`, `tint`, and `layer`. A
registered state may use `cursor = false` to suppress its automatic cursor or
`cursor = { ... }` to draw this configured form and suppress the default after success.
The configurable helper retains a line-cross fallback when its requested sprite cannot
be resolved; that fallback is a mod-authoring API behavior, not a built-in online cursor.

## Invariants

- Default framework cursors invoke native `cursor_draw` at `0x42FEB0`; directly plotting
  `misc[7]` is not an acceptable substitute.
- Mouse mode always uses EDX = 0 and draws EAX = 1 before EAX = 0.
- Default scale comes from `_global_scale`; no custom scale formula or manual hotspot is
  applied.
- The native timeout value is restored exactly after both passes.
- The caller's current and home turtle state blocks are restored exactly after the helper.
- The rectangle/ASCII cursor data and old white one-pass renderer must not exist in
  `hooks.c`.
- Online calls its cursor renderer from exactly one final pre-swap location.
- A hidden live online gameplay tick restores both native player-cursor disable flags.
- Supported in-game online menus include one topmost native mouse pass.
- Overlay-only final drawing is bounded to the overlay under the mouse, preventing an
  unnecessary duplicate over unobscured native/Lua cursors.
- Native background fallback suppresses/restores only its mouse pass, and a rendered-panel
  latch covers the last expiry frame.
- Lua custom states can disable or customize their cursor without causing a second
  automatic draw.

## Verification

`tests/online_cursor_static_test.py` pins the structural regression boundary:

- exact native addresses and the hidden-register wrapper;
- turtle reset followed by live global-scale installation;
- saved/restored timeout and current/home turtle blocks, plus shadow-before-color ordering;
- native `_misc + 0xc4` evidence from the checked Ghidra export;
- removal of the old scale, white tint, manual offset, and direct online sprite plot;
- shared native behavior for online, automatic custom-state, and no-option Lua cursors;
- one online call covering the hub state and mouse-over result/challenge overlays;
- live-menu classification, exact two-flag save/restore around the background game tick,
  and final native mouse coverage for supported in-game menus;
- native-background mouse suppression and last-rendered-overlay latches; and
- final ordering after all other framework pre-swap renderers.

Manual acceptance must compare the online cursor directly with the vanilla main-menu
cursor in the same build:

- verify identical size at every F1 preset, F11, borderless, and fullscreen mode;
- verify the black shadow and red-to-yellow pulse continue over time;
- verify the hotspot under slow movement and clicking at all window/DPI combinations;
- leave the mouse idle and confirm framework screens retain an always-visible cursor;
- inspect login, Settings, Friends, queue/match cards, and challenge/result
  toasts for one topmost cursor with no stale frame;
- during a live online match, open pause Options, both input-remapping pages, and Mods;
  confirm both controller/keyboard selection cursors and the mouse remain visible and
  usable while gameplay continues behind the menu;
- test a `data/misc.png` replacement pack; and
- test Lua custom states with automatic, no-option explicit, disabled, and configured
  cursor policies.

## Out of scope

- Replacing the OS cursor outside rendered game surfaces.
- Cursor themes independent of the misc atlas.
- Changing native pulse timing, color, shadow, or hotspot.
- General UI scaling or hitbox-coordinate bugs unrelated to cursor rendering.
