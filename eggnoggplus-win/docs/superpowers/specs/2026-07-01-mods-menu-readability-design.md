# Mods/Mod-config menu readability (beautify pass, slice 1)

## Problem
The mods menu (`mods_render`) draws the live vanilla menu scene and then the mod-manager
text directly on top with **no background/dim** ("let the swords render normally"). The
result is hard to read — bright, busy background behind the text. A previous attempt to add
a console-style blur froze the animated **sword cursors** (the "sword selector"), because the
console background is a static `glReadPixels` snapshot.

## Goal
Make the mod list / config text clearly readable **without freezing** the animated sword
cursors, and keep the swords **bright** rather than dimmed.

## Key facts (from exploration)
- `mods_render` draws: `p_main_draw()` (scene incl. sword cursors) → `p_menu_common_render()`
  → `p_main_sprite_batches_draw()` (flush) → `render_rows()` (mod text) → flush.
- The "sword selector" = the vanilla menu **cursors**, repositioned by `mods_cursor_tick`
  (`g_cursor_tx`) to point at the selected row, rendered by `cursor_draw` (0x41b240).
- `cursor_draw` is a **pure render** (turtle transforms + `sprite_batch_plot`; no RNG, no
  game-state writes), so it is **safe to call again** to re-draw the swords over a dim.

## Design (per frame, no snapshot)
1. **Live scene** — `p_main_draw()` as today (background + swords animate).
2. **Dim scrim** — full-screen semi-transparent dark quad over the scene.
3. **Swords back on top** — re-draw the sword cursors over the dim so they stay bright and
   keep sliding to the selected row.
4. **Rows panel + selection highlight** — a darker panel behind the mod-list/config column
   (sized from `ModsLayout`), a highlight bar on the selected row, and a bit more
   spacing/contrast, then `render_rows()` on top.

All drawn every frame via existing immediate-mode rect/quad helpers — nothing is captured,
so nothing freezes. Tunable constants (dim opacity, panel color/alpha, highlight color)
grouped at the top of the render for quick adjustment after seeing it live.

## Fallback
If re-drawing the cursors turns out fiddly (arg mismatch / artifacts), fall back to plain
option C: the full-screen dim covers the swords too, but they still **animate** (not frozen)
— strictly better than the snapshot approach.

## Out of scope (later slices)
Other menus (online hub, main-menu polish), hover/press feedback, transitions/animations.
Custom-map water effects is a separate task after this.
