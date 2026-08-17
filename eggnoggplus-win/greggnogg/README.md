# Greggnogg

Greggnogg is a standalone, dependency-free browser editor for native
EGGNOGG+ custom maps. It provides a visual 33-by-12 room canvas, native tile
palette, room ordering and appearance controls, live loader/playability checks,
and Yule-compatible package import and export.

The editor is intentionally self-contained in `greggnogg/`. It is not linked
from the repository documentation site and does not participate in that site's
build. It makes no network requests and does not run map scripts. The explicit
Preview action hands a validated package link to the installed Yule protocol
handler; the browser remains sandboxed from the game process and filesystem.

## Run it locally

No package install, bundler, or application server is required. From the
`eggnoggplus-win/` game/source directory (the directory containing
`MAP_FORMAT.md`):

```powershell
Set-Location .\greggnogg
py -m http.server 4173
```

Then open `http://127.0.0.1:4173/` in a current desktop browser. Serving the
folder is recommended over opening `index.html` as a `file:` URL because browser
security rules for local images and downloads vary.

The four JavaScript/CSS files and game atlases are all local. Work is
autosaved to that browser profile's local storage, but local autosave is not an
installed map or a backup: download a package when a revision matters. Changing
the origin or port creates a different browser storage namespace.

## Editor scope

Greggnogg edits `eggnogg-map/v1` packages. It supports:

- one through nine fixed `33x12` source rooms, ordered center-out;
- the complete native V1 glyph palette;
- pencil, eraser, flood-fill, line, rectangle, eyedropper, and symmetry tools;
- room creation, duplication, renaming, deletion, and reordering;
- sword/karate rules, round-end behavior, score and armed-respawn limits;
- per-room ambience plus primary and mirror color banks;
- undo/redo, keyboard operation, grid and mirrored-source previews;
- import from `data.json` plus `data.map`, an extracted folder, or a ZIP; and
- loader-shaped errors and playability warnings before export.

An otherwise valid raw `data.map` can be imported without `data.json`; the
editor supplies placeholder V1 metadata that must be reviewed before export.
Unknown glyphs are errors and are never silently replaced.

Imported V1 files are compiled back to Greggnogg's canonical JSON and map-text
layout. Because the game's current online compatibility key includes the raw
bytes of both files, even whitespace normalization changes that key. The import
summary calls this out, and manifests with unknown compatibility fields are
refused instead of silently dropping those fields.

Greggnogg intentionally authors V1 maps only. V2 packages, custom PNG tiles,
and `map.lua` are detected and refused without changing the current draft or
rewriting the imported files. The earlier browser Tile Lab experiment has been
retired while a better V2 object-authoring model is designed. If an older local
V2 draft is found, Greggnogg preserves its raw local draft under the
`greggnogg:archived-v2-draft` backup key and starts a fresh V1 map.

## Typical workflow

1. Choose **New** or **Import**.
2. Author source rooms from the center outward. Two source rooms become three
   final rooms; three become five; in general `n` becomes `2n - 1`.
3. Draw native glyphs and set the map and room properties. The canvas uses the
   real game atlases; labels call out procedural or runtime-driven previews.
4. Open **Validation** and resolve errors. Errors block export. Warnings point
   out likely playability or native-resource problems but do not rewrite cells.
5. Choose **Preview** for an in-memory test match when the Yule link handler is
   installed.
6. Choose **Export map**. Download the ready-to-extract ZIP or either source
   file separately.
7. Extract the ZIP so the map folder is directly under the game's `maps/`
   directory. Do not leave the ZIP itself in `maps/`; the runtime scans folders,
   not archives.

Keyboard help is built into the `?` dialog. Important shortcuts include
`1`–`5` for drawing tools, `I` for the eyedropper, `/` for palette search,
arrow keys for grid navigation, and the normal undo/redo shortcuts.

## Preview in Yule

Choose **Preview** to open the current valid V1 draft in EGGNOGG+. Greggnogg
packs the canonical `data.json` and `data.map` bytes into one bounded
`yule://preview/v1z/...` link. The installed Yule protocol handler may prompt for
browser permission before opening the game.

The framework decodes and validates both files again with the real custom-map
loader, installs the result only in process memory, and starts that exact map.
It works through the existing single-instance handoff when Yule is already
open. Preview does not write into `maps/`, mark the draft as exported, or add
the temporary map to online manifests. V2 data, filesystem paths, query
parameters, credentials, arbitrary executable arguments, and oversized or
non-canonical payloads are rejected.

## Yule package contract

“Yule format” is a folder contract, not a `.yule` file extension. A V1 install
looks like this:

```text
maps/
  my_map/
    data.json
    data.map
```

The two files must be direct children of one non-reparse map folder and each is
limited to 4 MiB. Folder names beginning with `_` are ignored. `data.json`
declares `format`, display metadata, rules, the center-out `layout.order`, and
room ambience/appearance overrides. `data.map` contains one section for every
ordered room, with exactly twelve quoted rows of exactly thirty-three ASCII
bytes. Spaces are meaningful. Room section IDs and `layout.order` must form an
exact one-to-one match.

Greggnogg's ZIP contains exactly one top-level folder with `data.json` and
`data.map` beneath it. ZIP is a transport convenience only; extraction is part
of installation.

## Asset provenance

No AI-generated or otherwise generated artwork is used. In particular,
Greggnogg does not use the
derived tile previews from `docs-site/map-tiles` and does not chroma-key the
game images. The PNG files already contain indexed transparency, which the
browser decodes directly.

The editor copies only these original runtime atlases:

| Runtime source | Standalone copy | SHA-256 |
| --- | --- | --- |
| `data/tiles.png` | `greggnogg/assets/game/tiles.png` | `bc7203e68fdf244e563bf859e7ffc879359b4917c57cb1cc81455fc55ecfcf94` |
| `data/sprites.png` | `greggnogg/assets/game/sprites.png` | `a358fd13efd3bb39a642dbf41de572b2b773676b8c371a5895f5782069a2de85` |
| `data/misc.png` | `greggnogg/assets/game/misc.png` | `a8d964ea50f13a977752b090dd7aab4540c2d7058e96349a03077a4acf36316a` |
| `data/font8x8.png` | `greggnogg/assets/game/font8x8.png` | `7550e0b4411555d57a3386befc76ba5f5274da47d705f4869d24a2b8103d60ff` |

`glow.png` is not copied: reverse engineering shows the native map draw path
uses `misc` sprite `0x11` for the relevant room particle rather than loading
that file as a map atlas.

## Preview boundary

The renderer reproduces the recovered atlas crops, layer order, room colors,
spatial mirroring, composite recipes, water transforms, chandelier/tentacle
motion, waterfall/scrolling crops, sun stack, and ambience layers. It is still
an editor preview, not a reimplementation of the simulation. Runtime-only
random selection, particle histories, object physics, player-dependent state,
and active goal behavior are represented deterministically or annotated.
Package validation—not a pleasing preview—is the authority for export safety.
See `GAME_MODEL.md` for the derived model and its source trail.

## Repository sources used

The editor model was checked against these project-root paths:

- `MAP_FORMAT.md`
- `custom_maps.c`
- `content_registry.c`
- `tests/custom_maps_v2_test.c`
- `ghidra/eggnoggplus.exe.c`
- `docs/superpowers/specs/2026-07-17-content-registry-map-v2-design.md`
- `docs/superpowers/specs/2026-07-18-map-local-lua-design.md`

## Development checks

From the `eggnoggplus-win/` game/source directory, the dependency-free
JavaScript tests are:

```powershell
cscript //nologo greggnogg\editor-core.test.js
cscript //nologo greggnogg\atlas-renderer.test.js
```

The current native-loader regression suite must only be launched through its
guarded runner, as required by the repository safety policy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_v2_map_test.ps1
```

Never launch `eggnoggplus.exe` or a `build/*.exe` test binary directly.
