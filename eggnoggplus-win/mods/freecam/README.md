# Free Camera

The package loads automatically from `mods/freecam`. During a match, press
**Shift+G** to toggle it. The view starts at the native camera position.

| Control | Action |
| --- | --- |
| W/A/S/D or arrow keys | Pan in any direction |
| Shift / Ctrl | Pan faster / more precisely |
| Hold middle mouse button and drag | Pan with the mouse |
| Mouse wheel or +/- | Zoom in or out (0.25x to 4x) |
| C | Recenter on the native camera, keeping zoom |
| R | Recenter and reset zoom to 1x |
| Shift+G or Escape | Return to the native camera |

The mod changes rendering only. It does not move players, change the native
camera used by simulation, or let players cross collision boundaries. Camera
coordinates can pass through room edges and gaps. The game's renderer may leave
areas blank when it has no map or has culled objects outside its usual view,
especially at low zoom. The override is disabled during managed online play.

The speed, zoom step, and HUD can be changed in the mod's config.
Four switches in **Options > Mods > Free Camera** control scene visibility
while freecam is active: **hide_players**, **hide_head_indicators**,
**hide_go_arrow**, and **hide_pause_button**. They are enabled in the included
config and take effect on the next frame. Hiding players also hides their
off-screen head indicators; the head switch can hide just the indicators while
players remain visible. Hiding the pause button only removes its drawing;
pausing still works normally.
