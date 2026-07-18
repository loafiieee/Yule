# Online Match Result Screen

**Date:** 2026-07-17  
**Status:** Implemented; two-client visual/result-report acceptance remains  
**Primary code:** online result lifecycle, rendering, and input in `hooks.c`

## Outcome

A completed server-managed match enters `g_online_result_state`; it no longer
returns early after drawing a small notification. The state keeps pumping the
control connection so `match_report_ack`, the opponent report, final result,
and rating update can arrive while the screen is open.

The full result panel shows the outcome, opponent, map, casual/competitive
summary, rating delta when supplied, server status, and two actions:

- **Requeue** uses the previous casual/competitive queue and remains gated on a
  server-confirmed result. Activating it early keeps the screen open and shows
  the waiting status.
- **Online Hub** is always available and returns to the Play tab.

Keyboard, controller, and mouse share the same selected-button state. Mouse
motion updates selection through checked button rectangles; a left click both
selects and activates. Escape/back chooses Online Hub.

## Responsive layout

Button rectangles come from one `online_result_button_metrics` function used by
both rendering and hit testing. Width, height, gap, side margin, and bottom
margin are clamped against `OnlineLayout`, so the two buttons remain inside the
panel at the 480x320 preset as well as large/fullscreen modes.

The five text rows occupy fractions of the measured space above the buttons
instead of fixed 720p offsets. This prevents the old small-window overlap while
retaining comfortable spacing at larger resolutions.

## Toast fallback

The compact result toast is not the primary end screen. It is rendered only
when result data remains active outside `g_online_result_state`, such as a
server-disconnect path that must return to the hub. Toast aging is disabled
while the full result state is active, so waiting for an opponent/server result
cannot silently expire the screen.

The native framework cursor is drawn once in the centralized final pre-swap
pass for both the full state and fallback toast.

## Invariants

- Normal match completion switches to the real result state.
- Result reporting continues while that state is active.
- Requeue cannot occur before server confirmation.
- Rendered and clickable button rectangles are identical.
- The full state never draws or ages the fallback toast.
- No result path copies or retains P2P authentication secrets.

## Verification

`python tests\online_flow_integration_static_test.py` guards state switching,
full rendering, shared button metrics, mouse selection/activation, and the
outside-state-only toast predicate.

Manual acceptance still needs two clients covering casual/ranked win, loss,
rating delta, delayed opponent report, early Requeue activation, mouse and
controller actions, server disconnect fallback, and 480x320/fullscreen layouts.
