# Online Match Result Screen

**Date:** 2026-07-17  
**Status:** Implemented; transient-state loop fixed 2026-07-18; two-client visual/result-report acceptance remains
**Primary code:** online result lifecycle, rendering, and input in `hooks.c`

## Outcome

A completed server-managed match enters `g_online_result_state`; it no longer
returns early after drawing a small notification. The state keeps pumping the
control connection so `match_report_ack`, the opponent report, final result,
and rating update can arrive while the screen is open.

This is not a replacement for the game's normal local round-end presentation.
It exists at the end of a server-managed online match because the client must
keep the control connection alive while both reports are reconciled, show the
server-confirmed result/rating change, and offer a one-step requeue. A failure
before synchronized gameplay begins returns directly to the hub and must not
create a result screen.

Match protocol 3 makes that boundary explicit. Both clients remain pending
until each reports a locally restorable synchronized frame zero and the server
broadcasts the exact-match gameplay commit. `match_result` is admitted only for
the exact active or start-barrier match carrying that commit. Before it, abort,
timeout, disconnect, stale setup, or even a premature `match_end` is a
no-contest `match_abort` and returns to one stable hub state without
result/rating UI.

Normal completion requires both clients to name the same winner. A single
report that is not confirmed within the bounded report window, or two reports
that conflict, cancels as a no-contest with no Elo mutation. This removes
insertion-order and unilateral-report authority; fully authoritative result
validation is still a production server requirement.

TCP may deliver the server's `match_started {committed:1}` and an immediate
post-commit disconnect/forfeit `match_result` in one receive batch. In that
case the client captures the pending match metadata, stops P2P, and opens the
confirmed result directly. It must not discard the result or switch into GAME
for a match the server has already retired.

The full result panel shows the outcome, opponent, map, casual/competitive
summary, rating delta when supplied, server status, and two actions:

- **Requeue** uses the previous casual/competitive queue and remains gated on a
  server-confirmed result. Activating it early keeps the screen open and shows
  the waiting status.
- **Online Hub** is always available and returns to the Play tab.

Keyboard, controller, and mouse share the same selected-button state. Mouse
motion updates selection through checked button rectangles; a left click both
selects and activates. Escape/back chooses Online Hub.

## Navigation ownership

The result state and online hub are transient sibling states; neither is a
valid return owner for the other. Selecting **Online Hub** clears the result,
opens the Play tab, and gives the hub a stable return destination (normally the
native main menu). Escape, the Back row, and controller Back all use the same
sanitized close path.

The hub open handoff captures the real owner before switching states. Its enter
callback preserves that owner when `p_state_last()` is the result, hub, or
console state. As defense in depth, an attempted transition into an inactive
result is redirected to the sanitized owner, and the result enter callback
schedules the hub instead of fabricating a generic `GAME OVER` page. This
prevents the former Result -> Hub -> Result menu loop.

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
- Prematch/connect failures never manufacture a result state.
- Only an exact-ID server-committed match may report or accept a result.
- Result reporting continues while that state is active.
- Requeue cannot occur before server confirmation.
- Result, hub, and console states can never be retained as the hub's Back target.
- An inactive result state cannot be resurrected as a generic `GAME OVER` page.
- Rendered and clickable button rectangles are identical.
- The full state never draws or ages the fallback toast.
- No result path copies or retains P2P authentication secrets.

## Verification

`python tests\online_flow_integration_static_test.py` guards state switching,
return-target sanitization, inactive-result rejection, deterministic
keyboard/controller/mouse hub exits, full rendering, shared button metrics,
mouse selection/activation, and the outside-state-only toast predicate.

Manual acceptance still needs two clients covering casual/ranked win, loss,
rating delta, delayed opponent report, early Requeue activation, mouse and
controller actions, server disconnect fallback, and 480x320/fullscreen layouts.
