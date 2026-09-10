# Greggnogg V2 authoring proposal

Status: interaction-design draft, 2026-09-05. Update 2026-09-07: the user delegated the application choice; a separate advanced workspace is selected for the broad custom-content system (see custom-content-system.md). Prototype/usability review and runtime integration remain outstanding. This is a proposal for review and usability testing, not an accepted product decision or a shipped feature. V1 remains the only authoring mode exposed in Greggnogg. Completing this draft does not complete the TODO requirement to review the design, choose the application model, and test a prototype before enabling export.

## Recommendation and scope

Build a separate V2 authoring application within the standalone Greggnogg project, with its own entry point, document model, storage namespace, and explicit import workflow. Share the tested map-format core, native atlas renderer, and accessible control primitives. Do not put another V2 toggle in the V1 inspector. V1 maps remain quick room-editing documents; V2 projects have a dependency graph of art, reusable tile definitions, placements, and one map script.

Call the initial authoring unit a **tile type**, not an entity. A tile type is painted into fixed map cells; its sprite can animate or move visually while its native cell stays in place. Instances share the type's definition. Existing runtime support includes symbolic tiles, native collision presets/fallback, map-local sheets, deterministic animation and tile behavior, contact sensors, and velocity changes to verified existing native objects. It does not provide programmable moving entities, arbitrary allocation/lifecycle, independent physics bodies, combat hitboxes/hurtboxes, or a replacement collision solver. Those require a separate runtime design and implementation before they appear as authorable controls.

Alternative for review: an explicitly separate workspace inside one application could share navigation, but still requires isolated documents and an import transaction. The prototype should compare discoverability against the separate entry point; do not decide the product architecture from implementation convenience alone.

## Document and workspace

The project has five first-class areas: **Map**, **Tile types**, **Assets**, **Behavior**, and **Problems**. Opening a tile from the map selects its placement and offers "Edit tile type (N placements)" separately. The map inspector owns identity, rules, source-room order, ambience, and appearance. The tile inspector owns artwork, native behavior, animation, transforms, and contact behavior. A breadcrumb always identifies the editing scope.

The project model stores stable internal editor IDs for assets and definitions, separate from exported map ID, local tile ID, and one-byte symbol. Placements reference definitions internally and compile to symbols at export. The original imported bytes and last exported package are retained separately from the editable model. Editor-only metadata must not silently enter runtime manifests; use a separately downloaded project file for layout, selections, recovery history, and other authoring metadata.

One undo transaction covers a user intention across all areas, including sheet edits, symbol reassignment, dependent placement changes, and script edits explicitly accepted during a refactor. Navigation and timeline scrubbing are not undo entries. Unsaved/downloaded state is shown independently from local autosave state.

## End-to-end creation flow

1. Create a V2 project, naming the map and its stable map ID. Explain the namespace consequence next to the ID, not on every save.
2. Create the source rooms with the familiar V1 tools. Native tiles remain available.
3. Choose "New tile type": decorative, solid, hazard, or explicit native behavior. The choice initializes visible editable fields; it does not create hidden script code.
4. Choose built-in art, import a sheet, or draw a sprite. A useful single-frame result should require no timeline setup.
5. Inspect native fallback and collision before painting. Paint the type into one room and inspect its mirrored room.
6. Add animation or contact behavior only when needed. Test each addition in isolation before a full play session.
7. Resolve package errors, download the package, and install/test it through the existing supported workflow. The current V1 preview URI cannot transport V2 assets or scripts; this design does not pretend otherwise.

## Assets and drawing

Asset import is staged: decode the PNG, show dimensions and grid, choose cell width/height and padding, then display the resulting cells before committing. Explain partial/invalid grid and out-of-range frame errors beside the selection. Defaults can be inherited from the map sheet; a per-tile exception must be visibly marked and individually resettable.

Offer a built-in sheet browser and external sheet library. Rename is distinct from replace-content. Replacing a sheet presents every affected tile and its frame range before applying the replacement as one undoable operation. Never silently resize the grid or remap frame indices. Keep an invalid replacement in the staged asset editor while the last committed sheet remains usable.

The initial drawing surface supports pencil, erase, fill, rectangular selection, copy/paste, and frame duplication with a zoomed pixel grid. Transparency is explicit. Drawing edits a project-owned working image; imported original bytes remain available for recovery. Committing a changed PNG intentionally changes package content identity. Do not re-encode an unchanged imported PNG merely because the project was opened or exported.

A native-layout sheet is a separate advanced operation: show the 128-cell native prefix with labeled slots and preserve its order. It reskins native glyphs without changing their behavior. Require an external sheet with sufficient cells, and show additional custom frames after the prefix. Do not imply that painting over a native hazard sprite removes its hazard behavior.

## Animation and presentation

The runtime uses consecutive frames, a common integer frame duration, and loop, ping-pong, or once playback. The first timeline must expose exactly these capabilities: first frame, count, ticks per frame, mode, and deterministic per-cell phase. No arbitrary keyframes, uneven durations, or cross-sheet tracks that cannot be exported faithfully. If nonconsecutive selected art is packed into a new contiguous strip, show that as an explicit asset-generating action.

Provide play/pause, tick scrubbing, and step-one-tick controls. Scrubbing is a visual preview, not native physics or Lua execution. "Once" describes runtime clock-based frame selection; do not label it "on contact" without an actual script that sets the intended temporary sprite state.

Show tint, offset, scale, rotation, layer, and room mirroring. Display the sprite bounds and fixed native cell together. The native-visual choice is "Replace native art" or "Draw over native art"; both preserve the selected native behavior. A visual offset must never drag the collision overlay. A side-by-side mirrored preview shows art mirroring and any separately configured contact/force mirroring.

## Collision and fallback

Choose solid, pass-through, hazard, or a supported single-cell native behavior. The UI must use the loader's allowed native-glyph set, not the entire V1 palette. Explain the generated native cell and show its art with custom rendering disabled. Native underlay is presentation, not permission to inherit arbitrary behavior.

Provide overlays with distinct labels: native cell/behavior, contact sensor, and artwork. Do not call the sensor a physics body. The sensor editor uses the existing bounded `map.sensor` rectangle model, object selector profiles, and cell/binding contact scope. Quantization and positive-extent errors must be displayed before committing. It cannot edit native sprite-derived combat boxes.

A "Missing custom art" preview disables the custom layer locally to demonstrate fallback. A "Script unavailable" explanation shows which behavior is declarative and which requires Lua. This is a simulation of fallback presentation, not proof that a runtime script failure was reproduced.

## Forces and tile/map behavior

For a basic conveyor or fan, expose the existing declarative force fields with independent axis toggles, add/set mode, and optional per-axis clamp. Missing X differs from explicitly setting X to zero. Units are velocity per game tick, positive Y points down, and a zero clamp means unclamped. Mirrored horizontal force reversal is explicit. Do not describe a fan as a moving entity.

Advanced contact behavior uses the one map-local `map.lua`. Tile cards may link to registered enter/contact/leave callbacks and the sensor bound to their qualified key. Map-wide tick behavior belongs in the Behavior workspace. A tile callback applies to all placements of that binding; binding scope unions contact across its matching cells, while cell scope tracks each cell separately.

Offer small opt-in templates for a fan, spring, periodic visual pulse, and contact counter. Templates show the exact Lua before insertion and use `map.state`, `map.random()`, and `map.every()` where appropriate. They must not silently combine a declarative force and a generated callback that both apply the same effect. Show both authored mechanisms if an imported project intentionally uses them.

There is one source of truth for script text. Generated templates become ordinary editable code after insertion; the form does not later overwrite user edits. Callback discovery is navigation assistance, not proof that arbitrary Lua can be losslessly refactored. Sensor form edits may generate a proposed code patch for an identifiable declaration; ambiguous or hand-computed declarations stay code-owned.

The browser does not execute map scripts. Syntax/API assistance must be labeled separately from native runtime validation. Explain instruction/memory limits, unavailable APIs, rollback-owned state, deterministic ordering, and fault fallback in contextual help. A future runtime test bridge needs its own validated V2 transport contract; do not reuse the V1 URI or launch the game automatically.

## Deletion, renaming, and identity

Deleting a tile opens a dependency view: placement count by room, references in known behavior declarations, and assets that would become unused. Choose a replacement type or erase placements. A used type cannot disappear leaving unrecognized symbols. Potential script references are shown, including an explicit "unresolved code references" state; never silently perform string replacement inside arbitrary Lua.

Deleting an asset is blocked while definitions or native-layout defaults reference it, unless the same transaction supplies replacements. "Remove unused assets" lists exact files first. Import unknown files remain retained; they are not assumed unused merely because the editor cannot interpret them.

Display names can change freely. Map IDs and local tile IDs are stable compatibility names. Renaming either offers a staged reference report and updates only structurally owned references; unresolved code requires editing before export. Symbol reassignment updates every placement atomically and rejects duplicates. Overriding a vanilla symbol shows that all matching authored cells receive the binding, not just a selected placement.

Export displays the exact output file list and whether the content differs from the last export. Current online compatibility depends on manifest/map bytes plus retained script and asset content; even canonical whitespace changes can change the map key. Do not call the current 32-bit compatibility key a cryptographic integrity guarantee. Runtime asset digests are computed automatically; authors should not have to paste hashes for normal creation.

## Validation and recovery

Validation has three visible levels: local field errors, project/package errors, and runtime-only checks. Selecting a problem navigates to its field, frame, placement, or script reference. Errors block export but not saving the editable project or downloading originals. Warnings require no forced acknowledgment loop.

The exporter must share loader conformance fixtures rather than implement permissive approximations. In particular cover namespace and symbol uniqueness, sheet path/grid/frame bounds, allowed native fallback, 64-definition and 16 external-grid limits, room geometry, script limits, native reset-spawner budget, and unsupported/unknown fields. Read limits from shared schema data where practical; this draft is not a replacement schema.

Imports parse into a staging document. Show unsupported fields, missing files, original file inventory, and whether export can preserve all required semantics. If not, offer preserved read-only inspection or download originals; never commit a destructive partial import. V1-to-V2 creates a separate project; returning to V1 requires an explicit supported conversion report, not a mode switch.

Autosave uses a separate V2 namespace, versioned snapshots, and a last-known-good project. Report quota failures without claiming the project was saved. Keep the working document in memory and offer a downloadable recovery file. On reload, offer recovery and original import independently. Unsupported future project versions open read-only with original download. Existing `greggnogg:archived-v2-draft` backups remain untouched unless an explicit migration successfully stages and validates them.

## Prototype and review gates

First prototype these interactions separately with fixture data and export disabled. Use mouse and keyboard-only runs; preserve focus after dialogs, announce dependency errors, provide numeric alternatives to dragging sensor edges, and do not rely on color alone to distinguish overlays.

Usability scenarios and observable success criteria:

- Create a two-room map, import a sheet, author an animated spring, and paint mirrored copies. The author can predict which parts move and which collide, find the callback, and explain mirror behavior without developer guidance.
- Make a decorative tile solid, then enable native underlay. The author can independently identify artwork and native behavior and recover the original configuration with undo.
- Replace a sheet with fewer frames. The old committed image remains recoverable, all affected types are listed, and no frame silently changes meaning.
- Rename a used tile and delete another with script references. Every placement is accounted for; unresolved code remains visible; cancel leaves the entire project unchanged.
- Import an unfamiliar V2 package with unknown fields or extra assets. No original data disappears, and the author can distinguish inspection from editable/exportable support.
- Configure a fan to affect living players but not corpses or swords, spanning adjacent cells with binding scope. The author can explain why the effect runs once per binding contact and identify which controls require Lua.
- Attempt to create a freely moving custom enemy. The workspace clearly explains the unavailable runtime capability instead of steering the author toward sprite offsets, force tiles, or a `K` marker workaround.
- Recover after a browser reload, failed autosave, and canceled import. The intended draft and original package remain distinguishable and downloadable.

Record completion, mistakes, backtracking, and participant explanations rather than accepting a screenshot as usability evidence. Review the application/mode choice and terminology with the user after the prototype exists. Only then connect package export, add fixture round-trips and native guarded validation, and expose a V2 entry point. Native gameplay/rendering and online compatibility checks remain separate release gates.

## Local references

- [Feature backlog](../TODO.txt): V2 redesign and future entity boundaries.
- [Greggnogg README](../greggnogg/README.md): shipped V1 scope, draft preservation, and preview transport.
- [Map format](../MAP_FORMAT.md): current V2 schema, sensors, Lua restrictions, compatibility, and fallback.
- [V2 registry design](superpowers/specs/2026-07-17-content-registry-map-v2-design.md) and [map-local Lua design](superpowers/specs/2026-07-18-map-local-lua-design.md): historical design context; current runtime and format documentation take precedence.
