# Tandy2K

Unmodified `Px437_Tandy2K.ttf` from The Ultimate Oldschool PC Font Pack v2.2.
Copyright (c) 2016-2020 VileR. Licensed under Creative Commons
Attribution-ShareAlike 4.0 International; see LICENSE.txt.

Original project: https://int10h.org/oldschool-pc-fonts/

Downloaded from this public copy because the original download was unavailable:
https://github.com/Izzy3110/oldschool_pc_font_pack_v2.2_FULL/blob/main/ttf-Px/Px437_Tandy2K.ttf

The renderer embeds the unmodified font, registers it privately in memory and
uses a 2x atlas of its original 8x16 glyphs. Menus use the glyph ink bounds with
a consistent one-pixel gap; the console preserves the original fixed-width
cells. Intermediate sizes use filtered sampling, while whole-pixel sizes retain
hard pixel edges. The font file itself is unchanged. No synthetic bold, aspect
stretch or global font installation is applied.

Regenerate the embedded header with `python tools/embed_ui_fonts.py --write`.
