# Window Sizing, Fullscreen, and Launch Modes

**Date:** 2026-07-17  
**Status:** Implemented and automatically checked; physical display/DPI acceptance remains  
**Primary code:** window runtime in `hooks.c`, event plumbing in `dllmain.c`, SDL wrappers
in `stubs.c`, pure sizing rules in `window_policy.h`

## Goals

The window policy repairs the framework launch arguments and the fragile vanilla F1/F11
paths without changing gameplay-space dimensions or rollback state. Window recreation
must remain on the monitor that owns the current window, user resizing must settle to the
native 3:2 aspect, and drawable changes must update local GL output without becoming
network-synchronized simulation state.

All mutations happen after the SDL window and GL context exist. SDL event handling only
records requests: F1/F11, launch-mode, drag-resize, and maximize recreation work runs at
the hooked main-update boundary after the native event queue has drained. Nothing is
invoked from `DllMain` loader lock or while `SDL_PollEvent` is returning an event.

## Native 3:2 sizing policy

`window_policy.h` contains side-effect-free sizing rules. The supported native presets
are the game's original 3:2 sizes:

| Preset | Size |
|---|---:|
| Small | 480 × 320 |
| Medium | 960 × 640 |
| Large | 1440 × 960 |

Offline F1 is consumed once per physical keypress and cycles these presets. A preset
that cannot fit the active display after conservative frame margins is skipped. The
cycle wraps to 480 × 320.

SDL moved, resized, size-changed, and display-changed events mark geometry dirty. A user
resize is debounced for 180 ms, fit inside the requested rectangle, clamped to
480 × 320..1440 × 960 and to the active display, made exactly 3:2, then applied through
the game's native `main_set_window`. Programmatic transition events are suppressed for
700 ms so native recreation does not cause a resize loop.

Windows maximize is intentionally a one-shot sizing request, not a retained window
state. After the same 180 ms event-burst debounce, it recreates the window exactly once
at the largest fitting native preset (1440 × 960, 960 × 640, or 480 × 320). The native
recreation clears the pending maximize request before its generated resize events and
the 700 ms suppression window prevents a second normalization.

During a live online match all F1–F12 keys are still swallowed by the existing
determinism guard before this policy sees them. F1/F11 changes are intentionally offline
only while GGPO is active.

## Display-aware F11 transitions

F11 toggles through the game's native `main_set_fullscreen`. Before leaving fullscreen,
the saved windowed dimensions are normalized to a 3:2 size that fits the active display.

The vanilla recreation path asks for display 0. To keep the window on its current
monitor, the framework:

1. reads the current SDL window's display index;
2. validates that display's current SDL bounds;
3. writes those dimensions to the native cached desktop width/height used by
   `main_set_fullscreen` before it recreates graphics;
4. installs a one-shot atomic display override;
5. calls the native fullscreen/window routine;
6. wraps `SDL_GetDisplayBounds(0)` and centered `SDL_CreateWindow` coordinates so only
   that transition uses the active display; and
7. immediately clears the override.

Updating the native cache before the call is required on unlike monitors: the original
fullscreen helper reads cached desktop dimensions before `wrapper_set_graphics` asks SDL
for bounds. If active-display bounds cannot be validated, fullscreen entry is deferred
instead of using another monitor's stale dimensions; launch-argument requests enter the
bounded retry path and an explicit F11 request can be retried by the user.

Ordinary calls outside a transition retain normal SDL display-index semantics.

## Launch arguments

The supported case-insensitive, whitespace-delimited tokens are:

- `-borderless` / `--borderless`
- `-fullscreen` / `--fullscreen`
- `-windowed` / `--windowed`

Conflicts are logged and resolved with this precedence:

```text
borderless > fullscreen > windowed
```

The runtime waits 500 ms after the first window appears. It applies the selected mode,
verifies both the native fullscreen state and relevant SDL flags, and on failure retries
up to five total attempts at 300 ms intervals. Explicit F1/F11 input cancels a delayed
launch action so startup policy cannot overwrite the user's choice. Keydown handling
only queues the action; the next safe main-update boundary evaluates the then-current
fullscreen state and performs the transition.

Fullscreen and windowed use the native transition. Borderless first establishes native
fullscreen, then applies `SDL_WINDOW_FULLSCREEN_DESKTOP` (`0x00001001`). Verification
requires both the engine fullscreen state and the desktop-fullscreen flag. If borderless
was requested, later F11 re-entry returns to borderless rather than exclusive fullscreen.

## Drawable and DPI handling

Window and GL drawable dimensions are polled after window events and before swap. When
they change, the framework logs display, logical window size, drawable size, flags,
viewport, and derived X/Y drawable scale.

Only local presentation state is reconciled:

- `glViewport` becomes the full drawable;
- a scissor that covered the previous full viewport is expanded to the new drawable;
- `game_w`, `game_h`, camera values, logical map dimensions, and rollback snapshots are
  not written.

This protects differently scaled peers from synchronizing platform-local DPI geometry.
The bundled real SDL is 2.0.3, so the implementation relies on drawable/window ratios
and window events rather than newer SDL DPI APIs.

## Failure policy and invariants

- Missing SDL/native function pointers leave the mode unapplied and enter the bounded
  retry path; they do not crash or spin forever.
- A failed desktop-fullscreen conversion logs SDL's error. Native fullscreen may remain
  as the playable fallback.
- User and native transition events cannot recursively resize indefinitely.
- Maximize produces one preset normalization and deliberately does not retain the
  platform maximized state.
- Destructive window operations do not run inside `SDL_PollEvent`; queued work runs
  only after native event draining.
- Window/display operations never run from loader lock.
- No policy path mutates networked gameplay geometry.
- The display override is scoped to one native transition and always reset afterward.

## Automated verification

`tests/window_policy_test.c` checks representative presets, largest-preset maximize
selection on large/medium/small displays, and an exhaustive requested size grid for
bounds and exact 3:2 output. Build and launch it only through the guarded runner,
which prepares the MinGW runtime `PATH` and preflights its DLLs:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_core_native_tests.ps1
```

`tests/window_runtime_static_test.py` guards loader-lock ownership, event routing,
queue-only F1/F11 handling, safe-boundary recreation ownership, one-shot maximize
normalization, validated native desktop-cache refresh, bounded launch retries,
desktop-fullscreen verification, display-override scoping, and the rule that viewport
refresh cannot write gameplay dimensions.

The policy test, static runtime test, individual hook/stub/DLL-entry compilation, and a
full integrated DLL link passed on 2026-07-17.

## Manual acceptance

Automated checks cannot emulate physical Windows display behavior. Before release, test:

- F1 cycling and drag-resize settling at each preset;
- maximizing repeatedly on displays that select each of the three presets, confirming
  one recreation per click and that the final window is not maximized;
- F11 out/in from every supported window size;
- every launch flag and conflict combination;
- borderless re-entry after an F11 toggle;
- moving the window between primary/secondary monitors with unlike resolutions/aspects
  before F1/F11, confirming fullscreen uses the destination display's dimensions;
- displays with different resolutions and 100%, 125%, 150%, and 200% scaling;
- window-size versus drawable-size changes and mouse alignment;
- forced `SDL_SetWindowFullscreen` failure and bounded retry logging; and
- two online peers with different local display geometry.

## Out of scope

- Persisting the selected mode/monitor across launches.
- A monitor-selection UI or command-line monitor index.
- Arbitrary aspect ratios or custom preset editing.
- Replacing SDL 2.0.3 with a newer DPI API surface.
- Enabling presentation hotkeys during a live rollback match.
