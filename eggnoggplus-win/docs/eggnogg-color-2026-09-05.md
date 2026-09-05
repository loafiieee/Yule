# Custom-map Eggnogg color

V1/V2 map packages now accept optional `rules.eggnogg_color: [r,g,b]`.
Channels must be finite numbers in 0..1. Omission preserves native automatic
color selection. The map-wide override covers E/^ tiles and their round-end
particles, including mirrored rooms. Team goals, player colors, and tentacles
retain native behavior.

The runtime hooks `game_eggnogg_colour` at 0x4208B0. The verified first nine
bytes are `sub esp,0x30` followed by `mov ecx,[0x54a360]`; the trampoline copies
whole instructions. No override delegates to the original function. Override
lookup reads only the registry generation pinned by map generation and requires
the matching selector. Switching maps clears or changes that pinned state;
filesystem reloads cannot change the running map's override inadvertently.
The original function's callers populate E/^ tile colors and goal particles.
No gameplay state, goal collision, or scoring logic is changed.

Greggnogg exposes a checkbox/picker in the Map inspector, renders the override,
preserves imported channel precision, supports undo/redo through normal document
commits, restores drafts, and omits the setting after disabling it. Manifest JSON
already contributes to package identity, so changing the color changes that
identity without introducing a second fingerprint format.

Validation:

- Guarded V2 suite passed, including V1/V2 parser coverage, strict malformed RGB
  cases, unchanged-output fallback, and pinned-generation/static hook checks.
- Greggnogg editor-core suite passed (75 assertions); renderer suite passed.
- Production DLL linked to `build/SDL2_eggnogg_color.dll`.
- Follow-up authenticated paired netcode suite passed. Its strengthened initial
  state tests now require an actual rejection-counter increment; malformed frame,
  correction ID, delta-only, and unknown-flag cases are covered. Unknown flags
  increment the existing dropped-state diagnostic.

Logs: `build/eggnogg_color_v2.log`, `build/eggnogg_color_link.log`, and
`build/netcode_followup_20260905.log`.

No installed DLL was replaced and no game was launched by this work. Browser
discovery returned no browsers; the Windows UI helper could not connect to its
native pipe. Live editor/game visual acceptance is documented in `TESTING.md`.

The netcode review also identified stale-revision handling in the compiled-off
cosmetics transfer path. Before re-enabling it, add serial revision ordering,
same-revision metadata immutability, and reordered-transfer tests. It is not an
active gameplay defect while `GGPO_NET_ENABLE_COSMETICS` remains zero.

## Continued backlog work

The combined build is `build/SDL2_todo_features.dll`. It also contains
`map.has_tile(reference)` (Map API version 7) and strict rejection of embedded-NUL
tile references. This allows one reusable map script to register behavior only
for the custom bindings its package declares. Native runtime and rollback replay
tests pass; the query does not inspect active-room placements. The API version is
mixed into the production compatibility key, so both online clients need the new
build. The earlier color-only candidate predates this API addition.

A subsequent netcode pass fixed initialization of timers at service tick zero.
Zero represents an inactive timer; replacing it with future tick 1 made unsigned
elapsed subtraction look like UINT32_MAX ticks and could immediately time out.
Using preceding serial tick UINT32_MAX avoids that underflow, with at most one
tick of conservative age. Regression coverage checks both starting at zero and
an already-running checksum timer crossing wrap through the timeout boundary.
The focused guarded frame-ring test passed (`build/netcode_clock_wrap.log`).
The final full paired suite also passed after this timer change, including chaos,
uint32 wrap, long correction, restart, and rejection (`build/todo_final_paired.log`).
The combined build and guarded V2 suite passed (`build/todo_final_link.log` and
`build/todo_final_v2.log`).

The separate [V2 authoring proposal](greggnogg-v2-authoring-design.md) covers
assets, drawing, animation, behavior ownership, deletion, recovery, identity,
validation, and usability scenarios. It has been checked against current runtime
scope and format limits; product acceptance and prototype usability work remain
open. No V2 editor mode or programmable entity feature was enabled.
