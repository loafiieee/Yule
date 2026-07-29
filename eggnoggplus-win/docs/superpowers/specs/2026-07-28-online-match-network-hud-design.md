# Online Match Network HUD Design

**Date:** 2026-07-28  
**Status:** Source implementation and focused static coverage complete; live visual
acceptance remains required  
**Primary code:** `hooks.c`, `mods/online_hub.cfg`,
`tests/online_match_hud_static_test.py`

## Goal and surface

Online matches have an optional, deliberately small diagnostics strip. It is disabled
by default, can be toggled in Online > Settings as **Match Network HUD**, and is also
controlled with `ggpo.net hud [on|off]`. The value persists as `match_hud=0|1` in
`mods/online_hub.cfg`.

## Display contract

The top-right translucent strip contains only:

- smoothed peer round-trip time in milliseconds, or `--` until a sample exists;
- current local input delay in frames;
- cumulative rollback count for the current session.

It reuses `ggpo_net_rtt_ticks()`, whose service ticks track the game's approximately
60 Hz simulation cadence, and rounds the conversion to milliseconds. No extra packet,
control message, server field, protocol version, or personally identifying value is
introduced. Richer jitter/loss/route health remains future netcode work.

## Rendering and lifecycle

The HUD draws only while both the server-managed match and P2P session are active and
the current state is gameplay or paused Options. It shares the existing late online
overlay renderer and one final sprite-batch flush with player nametags. The panel uses
screen coordinates, readable UI scaling, a muted fixed color, and no animation so it
does not compete with gameplay.

## Verification

Static coverage pins the default-off config, load/save and Settings rows, console
persistence, gameplay state gate, RTT conversion, displayed fields, and translucent
render path. Live acceptance must toggle it from both surfaces at multiple window/DPI
sizes, confirm it disappears immediately when disabled and after the match ends, and
compare its displayed ping with an external controlled-latency test.
