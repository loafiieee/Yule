# Completed work archive

This file preserves implementation history that used to live in TODO.txt.
Active work and remaining live acceptance are tracked in TODO.txt and
TESTING.md. Entries here describe source implementation and focused automated
coverage; some still have separate live acceptance work in those active files.

## September 27, 2026

- The Linux/Wine installer was rewritten as a checked Python implementation
  behind a small Bash entry point. It verifies payload hashes before replacing
  framework files, registers and checks the application menu and `yule://`
  handler, verifies native Steam shortcuts, records per-step failures, and
  reports unsupported sandboxed Steam installs explicitly. The Linux ZIP now
  includes that implementation. A WSL integration test covers success,
  rejected desktop registration, malformed Steam VDF, retry, URI forwarding,
  and conservative uninstall; a KDE/Wine user-session check remains in TESTING.md.
- Map-local Lua now offers bounded, rollback-owned point camera and zoom
  controls with native-camera release and readback. The renderer applies them
  only for drawing, including online frames; Greggnogg has matching Game blocks.
  Snapshot, rollback schema, trace diagnostics, native VM, and block tests cover
  this initial authored-camera slice. Live acceptance and follow/easing/bounds
  features remain in TODO.txt.
- The guarded paired UDP relay has seeded burst-loss, reorder,
  duplicate, bandwidth, and mixed profiles with overridable seeds. Each
  profile checks that its impairment occurred and compares 2,048 confirmed
  gameplay states between peers, with an optional uint32 frame-wrap run. A
  second seed passes the complete normal-frame matrix. The relay profiles also
  exercise coordinated correction and an injected would-block send on three
  seeds. Repro validation permits different peer-local automatic input delays while still
  comparing shared frame/prediction and floating-point configuration.
- Symmetrical outer-room opponent defaults are derived from paired placed
  geometry, even when node order changes, the authored start is a side room,
  or extra room designs are unused.
  Explicit Always still overrides the inherited no-spawn default.
  Greggnogg's room and placed-copy inspectors now show the resolved spawn
  behavior, including inherited and symmetrical outer-room rules. Editor tests
  compare the resolved policy before and after ZIP export/import.
- Placed room graph copies can override each player's starting floor/facing and
  replace or clear inherited respawn markers. Greggnogg exposes safe per-copy
  starts in Full map and a room-design/placed-copy marker painting switch in
  the normal editor. Native and editor package tests cover inheritance,
  validation, preview loading, and export/import; live editor acceptance remains.

## September 26, 2026
- Particle emitters support independent linear/ease-in/ease-out/ease-in-out
  motion timing for velocity, acceleration, and randomized spin. The default
  preserves legacy linear paths; native, browser, and package tests cover it.
- Custom ambiance emitters can spawn inside rectangular or elliptical bounds,
  or along a diagonal line. Old packages retain rectangular placement. The
  native renderer, browser preview, editor selector, strict format validators,
  and deterministic shape tests cover this slice.
- Room-layout buttons now support directional keyboard selection, one-cell
  Shift+arrow movement through the same placement validation and undo path as
  dragging, F2 room editing, focus restoration, and spoken position labels.
  The editor-core navigation test covers directional nearest-room selection.
- Opponent Never now prevents new trailing-fighter spawns without removing a
  fighter already alive. Symmetrical outer rooms inherit the traditional
  no-opponent default from their paired layout; an explicit Always overrides it.
  Native regression coverage checks both V1 preview/export policy parity and
  V2 graphs with renamed placed rooms.
- Pit deaths below an authored room now set the same native ungrounded-respawn
  gate as tile deaths. This prevents the native death timer from staying at one
  while the corpse has no floor beneath it. The player-batch writer also reads
  the fixed map selector only for actual room changes; its guarded rollback
  serializer test now runs without an unmapped-address crash.
- Greggnogg object-design deletion removes all placed copies and associated
  callback, block, and movement metadata. Export/import regression coverage
  checks the resulting map. Deleting one copy through the Object inspector
  remains available.
- Greggnogg combines repeated identical notices into one counted notice and
  caps distinct visible notices at four. Symmetrical-layout conversion restores
  moved generated pairs; conflicts involving independent copies, flips, or
  per-copy settings identify the specific placed rooms and remain visible in
  the room options panel.

## September 23, 2026

- Map atlas loading now falls back to a vendored, platform-independent current
  stb_image decoder when Eggnogg's bundled legacy decoder rejects a valid PNG. This covers indexed/palette PNG
  sheets such as ezgif exports while retaining the 8192-pixel, source-rectangle,
  sprite-count, and native-atlas capacity checks. A 1060x910 4-bit indexed PNG
  regression fixture exercises the compatibility path. Map-owned graphics use
  a separate bounded atlas page, leaving Eggnogg's 512x512 built-in page and
  its UV assumptions untouched. Large animations retain their authored frame
  size without corrupting terrain, players, menus, or other native textures.

- Room-graph transitions with GO-controlled focus now follow the crossing
  player when the game has no valid GO owner. Downward connections therefore
  commit the lower room and camera immediately instead of leaving the player in
  the lower room under an upper-room camera until native distance/death logic
  eventually respawns them.

- External map PNGs can now be larger, mixed-layout atlases rather than exact
  16x16 grids. Map-level sheets and individual tiles accept bounded source
  rectangles plus arbitrary 1..512 cell sizes; Greggnogg exposes the crop and
  imports non-grid images as a valid first region. The native loader crops and
  strips padding before atlas upload, includes the resolved rectangle in sheet
  identity, and supports PNGs through 8192x8192 under the existing byte and
  sprite-count budgets. Imported assets can be removed, with dependent tile,
  particle, entity, and room references reset to safe built-ins.

- Free-layout maps can import object pictures without assuming the legacy
  `layout.order` field. The object designer now exposes source-rectangle crop
  controls and an immediate **Remove this PNG** action; the map graphics picker
  also has a visible **Remove selected PNG** action with reference cleanup.

- Variable-room camera following now resolves the exact authored horizontal and
  vertical target before native easing, caps graph-scale travel per tick, and
  snaps negligible residual drift. Falling below a room without a valid bottom
  connection reaches the native out-of-bounds death check at that room's
  authored world-space bottom. Full-map room clicks select the room and open its
  details; double-click remains the direct route into terrain editing.

- Vertical room-graph pits commit an allowed destination before the native
  out-of-bounds check, so a connected room below keeps the crossing player alive
  while a closed pit still kills immediately. Doorways also have
  package-validated both/P1/P2/GO access and
  GO/crossing-player room-focus policies with matching Greggnogg controls and
  native/package regression coverage.

- Full-map room deletion is author-controlled. Greggnogg removes the selected
  instance and attached doorways even from an invalid/disconnected graph,
  chooses a remaining start automatically, retains orphaned object placements
  for visible repair diagnostics, and switches a broken symmetrical pair to
  free placement. Only the final required room is protected.

- Added the missing checked-in `maps/ambiance_demo` acceptance package. It uses
  two editable particle definitions, lifetime color/scale/rotation transitions,
  native-effect composition, two layers and exactly 33 deterministic lanes; the
  full guarded V2 loader suite validates it.

- Fixed the variable-room `game_room_count` detour crash. Its verified native
  prologue contains a relative x86 `CALL`; the trampoline had copied the old
  displacement and jumped from its executable heap allocation to an invalid
  heap address. The trampoline now explicitly rebases that call to native
  `map_tiles_w`, with a dump-address regression assertion and a clean 32-bit
  framework build installed to both the workspace and AppData installation.
  All other installed detour prologues were disassembled against the shipped
  executable and end on complete instruction boundaries without copied relative
  calls or branches.

- Greggnogg keeps the normal room editor focused on contents and puts the
  Symmetrical/Free choice inside Full map. Symmetrical maps now open the same
  live-preview arrangement canvas, add paired flipped rooms, and permit paired
  vertical offsets while holding horizontal symmetry. Both layout types show
  full-scale neighboring rooms in the normal editor; entering a neighbor uses a
  full directional slide. Animated palette previews reuse their canvas nodes so
  animation refreshes no longer replace the pointer target during tile clicks.

## September 22, 2026

- Object and player identities are now first-class typed beginner-block values.
  Named placements and current callback objects can be checked, inspected,
  changed, passed through lists/functions, or removed without crashing on a
  stale reference. Player values support active checks, the full typed
  observation surface, velocity/position changes, damage, and defeat while the
  original direct dropdown blocks remain available for simple scripts.

- Map Lua and beginner blocks can inspect immutable room layout metadata:
  placed-room count/start, stable placed and source identity, normalized graph
  bounds, and mirroring. The zero-based index matches graph player-room
  observations, removing the need for scripts to infer a room from world X.
  `map.move_player_to_room` and its beginner block validate room-local pixels and
  commit the destination room and world position atomically.

- Greggnogg's full-map workflow renders actual terrain and custom objects at
  each placed room's two-dimensional position. Dragging freely rebuilds native
  connections from touching edges, the compact editor bar mirrors X/Y geometry
  and opens room contents directly, and placed source rooms can be resized while
  surrounding rooms shift and adjacency is regenerated.

- Greggnogg beginner logic now has bounded deterministic lists: authors can
  create up to 32 values, inspect length/emptiness, get or search positions, and
  iterate with a lexical item variable. Random selection uses `map.random`,
  sparse/oversized values fail clearly, and list mutation stays unavailable so
  callback-local tables cannot become unbounded rollback state. Repeat-until and
  safe early return from an event/function are also covered by round-trip and
  native VM tests.

- Beginner Data blocks now construct and compose temporary 2D vectors, including
  component reads, add/subtract, scaling, length, and safe normalization. Text
  and boolean conversion are explicit blocks, and strict locale-independent
  decimal conversion supplies an authored fallback. Vector and conversion output
  is covered by browser round trips and the native bounded Lua VM.

- Greggnogg square-root, distance, vector length/normalization, degree
  trigonometry, and point-direction blocks no longer emit math functions absent
  from the deterministic sandbox. They use fixed-iteration arithmetic and
  bounded angle reduction, with browser source checks and native VM execution
  coverage for the generated primitives.

- Room graphs now reject overlapping rooms, ambiguous exits, misaligned or
  out-of-range doorways, invalid starts, and directed-unreachable instances in
  both native and editor validators. The Rooms canvas shows exact numbered
  doorway spans, and placed copies can independently override ambience, native
  graphics, and opponent spawn policy.

- Greggnogg can repair displaced room-graph coordinates directly from exact
  doorway sides, offsets, and source-room dimensions. It solves branches and
  loops, normalizes the resulting canvas bounds, and refuses inconsistent loops,
  disconnected rooms, invalid openings, or overlaps atomically. Inline canvas
  diagnostics identify affected rooms and connections before export.

- Room-graph exits now have rollback-safe runtime locks. Map Lua can set or
  inspect any of 256 canonical connections without consuming author state;
  native crossings respect the bitset and snapshot restore/replay is covered.
  Greggnogg numbers doorway rows and exposes the current map's doors through
  beginner lock action and condition dropdowns. Stable connection IDs prevent
  saved blocks from retargeting when an earlier doorway is removed. The guarded V2 test executable
  also receives an explicit 8 MiB stack so its growing package matrix no longer
  intermittently overflows the Windows default test stack.

## September 21, 2026

- Room graphs are now playable rather than parser-only. The runtime activates
  validated graphs with at most 17 final rooms, resolves horizontal and vertical
  edge crossings (including one-way exits), and retains the graph start,
  two-dimensional bounds, and active native final-room index. Greggnogg has an
  integrated Rooms canvas for conversion, X/Y placement, dragging, branch and
  loop connections, start selection, and safe removal. Custom objects target an
  exact placed-room `instance`; native decoding and browser preview use the same
  dimensions, origin, and mirror state, while ambiguous source-only placements
  fail closed. Guarded C and JavaScript suites cover conversion, traversal,
  placement expansion, one-way loops, invalid edits, and package round trips.

- Native room-definition lookup now maps every graph instance to its exact
  source room and honors the node's explicit primary/mirror appearance bank
  independently from geometry mirroring. Greggnogg's Rooms canvas adds
  fit-to-view, a clickable viewport minimap, placed-copy flip and palette
  controls, and validated numeric doorway start/width/one-way editing.

- The two-dimensional room-layout foundation now separates reusable source
  rooms from final room instances. Greggnogg core provides a bounded room_graph
  validator, exact world-bounds calculation, directed reachability, doorway and
  overlap diagnostics, and lossless conversion from mixed-size mirrored maps.
  A matching pointer-free native graph and validator derive all dimensions from
  validated source rooms and cover the same geometry, exit, reachability, and
  capacity rules in the guarded V2 suite. The format contract and remaining
  native integration sequence are documented in `docs/room-graph-design.md`.
  The native graph layer also resolves a world cell to its unique room and a
  room-edge cell to its directed destination, translating doorway offsets and
  enforcing one-way exits. The package reader parses and validates the complete
  `room_graph` JSON shape and reports its exact resolved bounds. Graph packages
  activate only after all geometry, reachability, engine-capacity, round-policy,
  and object-instance checks succeed.

- The variable-room runtime no longer assumes every final room starts at map
  row zero where a full X/Y position is available. Room-local player movement,
  previous-position state, mine coordinates, extra-tile update/reset, respawn
  selection, authored starts, falling physics cleanup, and native camera easing
  now translate both axes. The pinned map generation retains its validated
  start instance and a compact 256-connection table; callers can resolve an
  edge cell through one-way policy to the exact destination room, side, and
  offset. Existing mirrored maps exercise this path in the V2 regression suite.
  Native game initialization now commits the authored start instance, both
  player room bytes, and the finalized world-space camera together after spawn
  search, so a vertically or horizontally displaced start cannot borrow the
  center room's geometry. World-height queries use the active room's authored
  bottom, with a player-specific room at the native fall-death call site, so
  stacked rooms no longer snap the camera upward or kill a player immediately
  after a valid downward transition. Crossing-player focus also commits the
  destination directly instead of relying on the native GO-only room update.

- Greggnogg core now provides immutable room-graph authoring operations for
  adding a source-room instance directly from an existing edge, deriving its
  touching X/Y position and largest valid doorway, adding validated connections,
  generating collision-free instance IDs, and serializing the canonical
  snake-case manifest shape. Invalid or overlapping edits leave the document
  unchanged. These operations are ready for the full-map canvas UI.

- Greggnogg's shared Map/Object Math toolbox now includes general clamp,
  interpolation, range mapping, point distance/direction, sign, nearest
  rounding, square root, and degree-based sine/cosine/tangent blocks. Inputs
  are evaluated once where needed; reversed clamp limits and zero-length input
  ranges have deterministic behavior. Block serialization and source
  regeneration have focused coverage.

    - Greggnogg deterministic timer blocks: Map logic can declare named completion
      events, start one-shot or repeating countdowns, cancel them, and inspect active/
      remaining state. Reference dropdowns come from the current workspace, while
      missing, duplicate, oversized and over-limit definitions fail before export.
      Editable round-trip and generation tests cover the existing rollback-owned
      Map API 12 timer runtime.

    - Greggnogg now accepts several distinct top-level player-action events together
      with the ordinary game-tick event. The block compiler merges their bodies into
      the runtime's one deterministic `map.on_tick` callback in workspace order,
      rejects exact duplicate handlers, and preserves editable save/reopen round trips.
      The player selector, named pressed/held/released action comparisons, and generated
      callbacks are rebinding-aware; only the separate offline raw-device surface remains.

    - Player replacement-picture rollback validation now checks saved type and
      animation IDs against the currently pinned entity catalog. Even a snapshot with
      a recomputed internal checksum cannot restore an absent visual identity, and a
      rejected combined load leaves the live map/entity state byte-identical.

    - Player replacement-picture blocks now choose animations from the selected
      current-map object's validated catalog. The context-aware dropdown includes
      the default clip and retains missing imported names visibly instead of silently
      changing an author's logic.

    - Player replacement pictures now expose both persistent `finished` state and
      a rollback-safe `just_finished` edge that remains correct when playback speed
      skips frames. Greggnogg's player-picture completion event consumes that edge,
      runs once, supports player 1/player 2/either, and merges with other tick events.

    - Player spawn, respawn and room-transition observations/events are rollback-owned
      runtime state rather than hidden generated map variables. `map.players()` and
      player contacts expose `spawned`, `respawned`, `room_changed`, and
      `previous_room`; Greggnogg offers player 1/player 2/either spawn, respawn,
      room-enter and room-leave events plus matching reporters. Native transition,
      snapshot replay and editable block round-trip tests pass.

    - Greggnogg map-area editing: the Select tool drags tile rectangles and provides
      copy, cut, paste, horizontal/vertical flip and clockwise rotation. Custom object
      copies anchored in the selection move and transform with the tiles, pasted
      instances receive collision-safe unique names, room bounds/native placement
      validation still applies, keyboard shortcuts work, and each operation is one
      undo step. Pure tile/object transform tests and editor syntax/static checks pass.

    - Per-room native tilesets: V2 maps may choose a validated 128+ cell external
      native-layout sheet in `defaults.room` and override it per source room.
      Mirrored rooms resolve through their source, active-room drawing switches the
      atlas safely without changing native physics, and every referenced PNG remains
      in the package/online fingerprint. Greggnogg provides default and room graphics
      pickers plus checked PNG import, and renders the resolved animated sheet on the
      canvas. Native package/pinning/mirroring, hook integration, browser validation,
      import/export and renderer tests pass.

    - Map-scoped online admission now includes validated `entities.json` maps.
      Custom-map keys use a domain-separated 128-bit SHA-256 prefix over exact
      map/config bytes plus loader hashes for external sheets, `map.lua`, and
      `entities.json`; the matchmaking server therefore offers only byte-compatible
      packages. Direct and matched P2P sessions additionally negotiate the frozen
      rollback size and validate the full YMC3 managed-content identity before any
      state mutation. Preview drafts remain excluded, package edits change the key,
      faulted runtimes fail closed, and gameplay mods remain suspended pending the
      broader process-wide manifest. Native/runtime/V2 discovery tests cover inclusion,
      lookup, mismatch identity, atomic restore and deterministic replay; paired live
      soak acceptance remains.

    - Custom particle lifetime transitions now interpolate start/end RGBA color,
      opacity, width/height scale and rotation with linear, ease-in, ease-out or
      ease-in-out curves. Ending scale may reach zero, randomized emitter spin
      composes with authored rotation, and mirrored rooms flip the final transform.
      Greggnogg edits and animates every field in its scrollable particle designer;
      strict browser/native validation, deterministic first/final-tick tests, map
      identity coverage and the ambiance demo use the same backward-compatible V2
      schema.

    - Normal Greggnogg placement editing now selects an individual custom-object
      copy from the map canvas and edits its name, arena side, precise position,
      starting velocity, visibility, initial named animation, scale, visual offset,
      RGBA tint and player-relative draw order in an Object inspector. The strict
      browser/native `entities.json` loaders accept and fingerprint those initial
      values, mirrored expansion preserves them, and map preview composes them with
      the shared object design.

    - Broader deterministic player observations: `map.players()` and managed-object
      player contacts expose native state, room, current/previous collision bitfields,
      and synchronized skin/clothing palette indices alongside motion, grounded state
      and rebinding-aware input edges. Greggnogg's numeric player-value block offers
      each field. Host validation, detached-table behavior, runtime replay and native
      source-hook coverage keep these read-only and rollback-derived.

    - Map API 36 arbitrary object animation clips: each object type can declare up
      to 32 named animations with independent sprite ranges, timing and loop/once/
      ping-pong playback. Selection and exact progress are per instance, enter the
      package fingerprint, serialize in YEW1/v6 and replay under rollback. Lua can
      play/query clips, inspect detached frame/completion status, and register a
      deterministic once-only completion lifecycle callback. Greggnogg edits and
      previews every mode, provides play/current/finished blocks, and exposes
      animation completion as an ordinary object-event tab. Browser validation,
      native decoding/identity/rendering, callback rollback and runtime replay tests pass.

    - Map API 36 general animation speed: every managed object can independently run
      its authored animation from 1/256x through 256x. Exact fractional progress,
      pause behavior and tick resets serialize in the current YEW1/v6 and replay deterministically;
      overflow preflight remains world-atomic. Lua and Greggnogg use the general
      animation-speed property rather than mechanic-specific animation controls.

    - Map API 36 includes the general per-instance object presentation system with
      independent width/height scale, additive picture offsets and rotation, RGBA tint multiplier,
      and authored/behind/front draw-order selection. These compose with type visuals,
      affect rendering only, and serialize in YEW1/v6 for exact rollback. Authored
      and per-instance angles compose and mirror predictably while collision regions
      remain axis-aligned. Greggnogg
      exposes numeric getters/setters, color set/reset and draw-order blocks. Lua,
      entity-world, renderer and block-generation coverage reject invalid values and
      preserve presentation through transaction restoration.

    - Greggnogg player-action events and conditions: maps can react when any player,
      player 1 or player 2 presses, releases or holds jump, attack, a direction or
      menu. Event bodies receive this-player scope; the general condition composes in
      ordinary logic. Generated Lua uses rollback-derived configured game actions,
      survives editor serialization and rejects duplicate tick event roots. Raw
      physical keyboard/controller input remains in the policy-gated backlog above.

    - Map API 36 player positioning: `map.set_player_position` queues finite world
      coordinates during gameplay callbacks, composes with queued velocity changes,
      and exposes the pending position through player/contact observations. The native
      writer preflights both players before any store and translates previous position
      by the same displacement, preventing a teleport from creating false native
      movement. Callback failure, unavailable players and rejected commits restore the
      full transaction. Greggnogg includes fixed-player and this-player blocks; runtime,
      editor-generation and guarded native rollback coverage pass.

    - Map API 33 managed-object damage and optional health: `entity.damage`
      synchronously delivers a
      bounded positive amount plus an optional exact-generation source to a per-type
      `entity.on_damage` callback. `entity.enable_health`, health queries/setters/healing,
      and `entity.on_defeated` provide a framework-owned model without forcing removal
      or another response at zero. Failures restore entity/object state and discard
      queued native player writes; transient lifecycle stack state is reset across
      faults and rollback restoration. Greggnogg includes damage-event, action, amount
      source, health and defeated blocks. Native/runtime replay coverage and a checked-in
      blade/crate example exercise named hitbox/hurtbox filtering, cooldown, health and removal.

    - Map API 36 player presentation/health slice: scripts can hide/show the native
      body, tint skin and clothing separately, and reset presentation through rollback-safe
      APIs. Optional player health supports damage, healing and current/maximum changes;
      damage, health-changed and defeated events are available in Lua and beginner blocks,
      and zero health joins the atomic native defeat commit. Composite sprite/animation
      replacement, scaling, equipment, status effects and editor previews remain in the
      broader player-control backlog above.

    - Map API 34 spawn configuration: `entity.spawn` accepts up to 32 initial
      handle-owned scalar values and installs them before `on_spawn`. Invalid values,
      capacity failure and lifecycle failure remain transaction-atomic; names are
      bytewise sorted before storage rather than depending on Lua table order. Greggnogg adds
      configured creation and current-object-handle blocks, with native VM, rollback,
      serialization/rename and editor round-trip coverage. The checked-in projectile
      package combines owner/damage/lifetime initialization, movement, named attack/
      receiver contact, managed damage, cleanup and byte-identical replay.

    - Map API 35 managed-object signals: `entity.signal` synchronously delivers a
      bounded named message with an optional scalar value and exact-generation
      source to one per-type `entity.on_signal` receiver. Calls share lifecycle
      nesting, execution-budget and full transaction rollback rules with damage.
      Greggnogg includes receiving/sending, name/value/source and text comparison
      blocks plus a direct current-map named-placement sender; object logic metadata
      preserves the new event. Native runtime replay,
      callback failure, editor serialization and import/export regressions pass.

    - V2 custom map ambiances: bounded reusable particle/ambiance catalogs,
      animated sheet visuals, lifetime/fades, spawn rectangles, velocity ranges,
      acceleration, rotation, native-effect composition, alpha/additive blending,
      room mirroring and five explicit draw layers. Stateless lanes derive from
      pinned map identity and game tick without gameplay RNG or rollback state;
      normal map/asset fingerprints cover online compatibility. Greggnogg adds
      the definitions to the ordinary room ambient selector, a focused editor and
      animated room preview. Ambiance/particle tabs, an independently scrolling
      property pane, selected-particle isolation, active-room previewing and a
      bounded single-flight render loop replace the original overloaded editor.
      The particle-sheet picker is capped inside the scroll pane, schema terminology
      is replaced with room-effect authoring labels, and preview API 2 rejects an
      outdated installed `yule://` runtime with an actionable update message before
      upload. Desktop visual QA now uses a compact side preview with the full sheet
      picker/importer disclosed only when needed; narrow screens retain a stacked
      layout. Custom room-effect selection now survives its focused option rebuild,
      the ordinary map canvas receives the complete particle/ambiance/asset catalog,
      built-in animation ranges are validated in both loaders, and all native layers
      use the world camera rather than the screen-space arguments supplied for layers
      4 and 3. The native renderer reports active selections plus bounded sprite-queue
      failures instead of failing silently. The local installed test copy has the
      rebuilt DLL.
      Native core/parser/hook tests, staged-package preview coverage, editor
      validation and the checked-in ambiance_demo fixture pass. Live game/browser
      visual QA remains.

    - Separate persistent Music and SFX master controls in the Framework menu.
      Music scales native/glitch tracks, Yule playlists and mod file music; SFX
      scales native synth output, generated voices and mod file channels. Live
      SDL_mixer channels retain their per-mod/per-play base levels when the master
      changes. Native detour signature checks, routing guards and bounded scaling
      assertions cover the implementation.

    - Greggnogg object-logic variable names now form one catalog across the
      Every tick, When created, and When removed tabs. Names already serialized
      in any callback are restored into every other block workspace, and newly
      created names transfer as soon as the author changes tabs.

    - Map API 12 adds 32 named rollback-owned timers with delayed/repeating
      callbacks, restart/cancel/remaining controls, stable registration-order
      dispatch and cancellation of pending callbacks. Snapshot version 6 validates
      timer state; callback faults restore the tick transaction. Timer replay,
      scheduling, cancellation and corruption tests plus a timed-spring example
      accompany MAP_FORMAT and the searchable API reference.


    - Per-room opponent spawning in V1/V2: native/always/never map defaults and
      source-room overrides, mirrored inheritance, strict parser validation, and
      Greggnogg import/export/settings/history support. Nine narrowly verified
      native gates separate respawn/end-room combat from goal completion while
      preserving score/victory guards and initial/leader respawn behavior.
      Guarded native policy and package tests pass; follow the remaining local/
      two-client acceptance in docs/opponent-spawn-testing.md before release.


    - UI API revision 12 adds a complete bounded list_box control: literal label
      filtering, keyboard/controller paging and first/last navigation, disabled-row
      skipping, automatic selection reveal, pointer selection/activation, draggable
      scrollbar, row-based scroll ownership, fitting labels and themed focus.
      Only visible full rows draw/hit-test; a 200-row browser demo and guarded
      behavior tests cover filtering, navigation, bounds and pointer interaction.
    - Failed map.state assignments now validate a replacement before committing:
      invalid types, oversized strings and nonfinite numbers preserve existing
      state and free capacity. Twelve guarded regression cases cover both paths.


    - UI API revision 11 adds a built-in high_contrast theme and sorted, detached
      theme_names discovery. Partial named themes inherit complete default_dark
      values, preventing missing spacing/text fields from breaking controls.
    - Custom-screen lifecycle callbacks stop after screen changes or definition
      replacement, preventing stale rendering/cursor overrides and repeated leave
      callbacks after redirects. Guarded Lua regressions cover each transition.

    - UI API revision 10: slider keyboard/controller navigation with bounded
      configurable steps and explicit mouse precedence; focused activation for
      icon buttons/checkboxes; visible focus outlines. Input edges stay owned
      by the calling mod. Guarded Lua tests cover limits, disabled/unfocused
      controls, validation, activation, and mixed-input precedence. A complete
      keyboard demo is in docs/examples/ui_navigation.lua. State definitions now
      reject invalid callbacks/registration failures without replacing a working
      screen or duplicating event routers.

    - Classic last-room RNG desync fixed and user-confirmed September 7. The
      alternate creepy sound pitch call at 0x424410 now uses cosmetic RNG; paired
      LAN traces and a native hook regression establish the exact cause.

    - Native display-size cache now follows actual SDL window dimensions after
      fullscreen/focus transitions; the reported 1600x900 viewport with stale
      1537x865 native projection is corrected. Gameplay dimensions stay under
      rollback ownership; live camera/fullscreen acceptance remains required.

    - Interactive progress bars respect disabled state, map vertical pointer input
      to the displayed fill direction, normalize NaN progress, and skip invalid
      bounds. Guarded Lua regression tests cover both orientations and reversal.
    - Delayed duplicate-packet chaos tests now stop draining on duplicate-send
      backpressure while preserving the next pending packet; the guarded receive
      budget fixture verifies exact send count and retained queue ownership.

    - UI API revision 9 adds with_style for temporary control styling, preserving
      callback arguments/results and restoring the full previous stack on success
      or failure, including nested scopes and unbalanced style changes. Guarded
      native Lua tests exercise error identity, nil returns, nesting, coroutine isolation,
      cyclic/deep table rejection, and atomic theme update failure.

    - Windows and Linux/Wine installers create maps/ alongside mods/, explain
      the extracted per-map folder layout, and preserve user maps on uninstall.
      Windows lifecycle and Linux static/syntax checks pass; the POSIX integration
      fixture also asserts map creation and preservation.

    - UI API revision 7: responsive grid bounds with validated dimensions, detached
      output and stable last-row sizing; constant-space list virtualization calculates
      visible rows, scroll limits and selection reveal. Sliders anchor steps at the minimum,
      clamp rounded values and handle fixed/reversed ranges. Embedded UI helpers
      moved from lua_manager.c into ui_helpers.lua with a checked generated C
      header; production bytes execute directly in guarded
      LuaJIT regression tests. Broader navigation/accessibility work remains open.

    - Online hub text fields commit valid drafts before clicking another field,
      Log In/Register, tabs or background. Tab/Shift+Tab and up/down move text
      focus; controller confirm/cancel/navigation work during editing. Invalid
      settings retain their draft and block navigation. Same-field clicks do not
      reset input; Escape cancels. Friend requests require explicit submission.
      Extracted production mouse/key handlers pass isolated no-network tests;
      live login-layout acceptance remains.
    - Map API version 11 adds map.state_clear([prefix]) for puzzle/round resets:
      literal prefix matching, removal counts, immediate slot reuse, strict argument
      validation, and byte-identical snapshot replay covered by guarded Lua tests.
    - Map API version 10 adds map.state_keys(): detached, bytewise-sorted key
      enumeration for inspecting/clearing rollback-owned state. Tests cover empty
      and full state, deletion/slot reuse, copy isolation, argument errors and
      exact snapshot replay. Online peers require matching map API fingerprints.
    - Keyboard binding parser extracted into input_binding.h with strict complete
      decimal parsing, overflow rejection, whitespace handling and canonical-name
      round trips. Malformed numeric names cannot silently become another key or
      unbind an action. The guarded UI/API runner covers the production parser.
    - UI API revision 8: previous/next selection on tabs, item grids and swatch
      grids using owner-routed keyboard/controller edges. Navigation skips disabled
      items, supports optional wrap and exposes a pure navigate_items helper for
      custom controls. Global focus ownership and accessibility remain open.
    - UI hit tests share finite, fractional, half-open bounds across immediate
      widgets and native mouse capture. Invalid bounds cannot capture the screen;
      adjacent controls do not both activate at a shared edge. Disabled sliders
      ignore mouse edits, and tiny steps no longer overflow into the maximum.
    - Paired netcode coverage now replays delayed/reordered datagrams as well as
      immediate traffic. Focused 2,048-frame ordinary and bilateral uint32-wrap
      duplicate/loss/delay runs preserve exact finalized state equality, cross four
      history generations and verify replay rejection. Native entity soaks remain.
    - Greggnogg goal color now uses the existing inline RGB/hex editor, with live
      preview, exact fractional channels and grouped undo; native OS color pickers
      remain excluded. Shared room-color controls retain their previous behavior.
    - Admin persistence: password, ban/unban and rating changes write a detached
      proposed record before publishing memory or notifying clients. Failed writes
      retain old nested data and generate failure audit outcomes. Restore verifies
      recovery bytes and distinguishes incomplete recovery from successful rollback.
    - Server operations: admin maintenance emits correlated, flushed intent/outcome
      audit records without passwords/form contents. Audit-write failure blocks mutation
      or explicitly reports a completed action with missing outcome. Server updates
      preserve JSONL logs. Offline backup/verify/restore tools validate all account,
      rating and secret files, hash snapshots, retain a recovery copy and recover from
      simulated partial-write failure. These do not replace multi-file transactions.
    - Map API version 9 adds tile:get_sprite() for inspecting the active temporary
      sprite, pixel offsets and remaining tick lifetime. Returns detached data or nil;
      mutation cannot alter the native effect. Reset, mutation-isolation and rollback
      replay coverage pass in the guarded map runtime suite.
    - Preview color deployment mismatch: yule:// opened the installed AppData copy,
      whose older DLL did not contain the Eggnogg-color feature. Preview help now
      explains the installed-game target; map generation logs the active RGB override
      for diagnosis. See docs/preview-color-fix-2026-09-05.md for installed-build checks.
      The user confirmed the updated preview now displays the configured color.
    - Map API version 8 adds map.tile_bindings(): detached symbol/key entries in
      manifest order, available during load/callbacks for shared behavior registration.
      Tests cover ordering, mutation isolation, empty lists, argument errors and
      rollback replay. Matching online builds are required.
    - Map API version 7 adds map.has_tile(symbol_or_key), an immutable declaration
      query usable during load/callbacks for optional reusable script behavior.
      Missing native/custom bindings return false; callback references containing
      embedded NULs no longer match a valid prefix. Guarded runtime/replay coverage
      passes. Online peers require the same API compatibility fingerprint.
    - Netcode service-clock wrap: timer initialization at tick zero uses the previous
      serial tick instead of a future tick, avoiding unsigned elapsed-time underflow
      and immediate recovery timeout. Guarded tests cover starting at zero and a
      running checksum timer crossing uint32 wrap through its timeout boundary.
    - Custom-map Eggnogg color: optional rules.eggnogg_color RGB in V1/V2 forces
      stationary/waving Eggnogg and its round-end particles; omitted keeps native
      player-derived colors. Greggnogg adds a toggle/picker, live canvas preview,
      strict import/export validation, draft restoration, and undo/redo. Native
      parsing and editor regressions pass. Browser/game visual acceptance remains.
    - Duplicate initial room spawns: the original game's countdown resets the room
      before setting started, then its first update can reset it again while old room
      is still -1. Skip only that second mode-9 dispatch at countdown 90, preserving
      room-entry bookkeeping and later room revisits. This covers all reset tile
      actions, including sword/K spawns, using rollback-owned native state. Guarded
      lifecycle fixtures and hook wiring pass; see docs/sword-spawn-audit-2026-09-04.md.
    - Map-script authoring helper: map.every(interval[, phase]) provides a deterministic
      periodic predicate with strict integer bounds, optional phase, and no mutable
      timer/RNG state. Map API version 6 participates in the rollback compatibility
      fingerprint. Tests cover phase/default behavior, rollback replay, malformed
      arguments, and integer clock arithmetic beyond Lua's exact numeric tick range.

    - Deeper transport/serializer audit: synchronized initial state cannot be replaced
      by conflicting same-epoch chunks; strict probe JSON rejects duplicate keys,
      trailing garbage, invalid IPv4, and out-of-range unsigned fields. Native snapshot
      capture/apply preflights scalar/slab access and rejects invalid role tags before
      mutation. Canonicalization v7 normalizes erased raw role pointers consistently;
      both peers need matching builds. Full guarded and paired suites pass.
    - Server safety/persistence audit: bound raw UTF-8 control frames, reject repeated
      authentication on one connection, isolate inherited account/token keys, and
      contain malformed dispatch input. Corrupt existing stores/secrets fail startup;
      store writes use flushed atomic replacement. Private admin Host validation
      closes the DNS-rebinding path. Transactional multi-file storage remains open.
    - Framework API revision 6: add owner-scoped mod.font.unregister_glyph and
      mod.texture.unregister, rebuild remaining declarations on removal, and reject
      inactive-owner edits. Fix glyph cache ownership/reservations, canonical texture
      registration keys, truncated paths, image-size overflow, and allocation bookkeeping.
      Native cache regressions and 100 texture reset cycles pass; Lua API wiring has
      static coverage. Live rendering acceptance remains required. Details and test
      evidence: docs/framework-audit-2026-09-04.md.

    - Netcode receive/error-path audit: cap both GGPO and reachability-probe UDP
      polling at 64 attempts per service call, including invalid traffic; skip
      consumed oversized datagrams and asynchronous UDP reset errors within that
      budget. TCP setup now checks Winsock/nonblocking failures, rejects invalid
      targets, and closes failed select polls instead of reporting pending forever.
      Invalid receive buffers no longer close a healthy TCP connection. Probe
      numeric fields reject signs, overflow, and numeric-prefix garbage. Native
      socket regressions, the full authenticated paired suite, and production DLL
      linkage cover the changes. See docs/netcode-audit-2026-09-04.md for scope and
      remaining limitations; the last-room report above is still open.
    - Live LAN admin dashboard: automatically refresh server activity, totals,
      queues/challenges/rematches, social relationships, and filtered account rows
      every two seconds. Preserve focused controls, open maintenance panels, and
      unsaved form values; pause hidden tabs, bound requests, prevent overlap, and
      retry interruptions without clearing the last view. Expired sessions stop
      polling. Same-origin external script and fetch permissions retain CSRF and
      private-client gates. Endpoint and lifecycle tests pass; real-browser visual
      acceptance remains required.

    - Console code cleanup: moved the command list, argument parser, and saved command
      history out of `hooks.c` into small tested modules. Autocomplete now includes every
      command that can actually run, including the GGPO test commands, `mods.cfg.*`
      shortcuts, and `online`. Invalid huge numbers, `nan`, and `inf` are rejected instead
      of reaching framework settings. The production build and full test suite pass.

    - Console image cleanup: moved the pure RGBA box-downsample and blur loops out of
      `hooks.c` into `image_util.c/.h`. OpenGL capture and texture ownership stay in the
      console code, while pixel math now has strict standalone compilation and native
      tests for averaging, clamped edges, alpha, and invalid/in-place inputs. The
      production build and full guarded suite pass.

    - Common text safety cleanup: replaced the private `hooks.c` copy/ellipsis/byte-size
      helpers with a small `text_util.c/.h` module and explicit names at all 143 call
      sites. Bounded copies now safely support overlapping buffers. A native test covers
      null input, truncation, overlap, in-place ellipsis, tiny limits, and byte labels;
      the production build and full guarded suite pass.

    - First source cleanup pass: moved `mod.fs` and the asynchronous `mod.http` worker
      pool out of `lua_manager.c` into focused modules, cutting about 800 unrelated lines
      from the Lua manager. Each module now owns its Windows/Lua includes, limits, API
      registration, and tests while the manager only supplies stable mod ownership and
      lifecycle calls. The filesystem search also no longer mistakes a directory
      junction whose name matches the target for a file. The production build,
      documentation audit, strict standalone compiles, and full guarded suite pass.

    - Filesystem picker consistency: the adjacent folder picker is now an owner-bound
      wide-character dialog with strict UTF-8 titles/results and a neutral default label,
      while recursive lookup skips directory reparse points/junctions in addition to its
      existing depth bound. Both native dialogs attach to the active window. Static
      ownership/Unicode/traversal checks and the public API/spec text pin the behavior.

    - Lua HTTP request-target correctness: strict UTF-8/embedded-NUL validation now runs
      before worker creation; workers reject non-HTTP(S) schemes and URL credentials,
      preserve the query returned separately by `WinHttpCrackUrl`, strip client-only
      fragments, synthesize `/` only for a genuinely empty path, and bound the combined
      wide request target. Static lifecycle/request construction checks, API docs, and the
      HTTP hardening spec pin the fix.

    - Bounded strict `mod.json`: deterministic encode and full-document decode now cover
      finite numbers, UTF-8/Unicode escapes and surrogate pairs, explicit null, preserved
      array/object kind, shallow empty-container helpers, lexically sorted object keys,
      duplicate rejection, and path/byte-aware failures without code evaluation or
      metamethod calls. Both directions enforce 32 levels, 65,536 nodes, 256-KiB strings,
      and 1-MiB input/output, while rejecting cycles, sparse/mixed tables, non-finite
      numbers, malformed UTF-8, ambiguous types, and trailing data. API revision 2 and
      `json.v1`, real LuaJIT native regressions, source/build/docs checks, a design spec,
      and complete examples pin the release contract.

    - Public API-1 compatibility policy and discovery: manifests now distinguish a
      breaking `api_version` major from an additive minimum `api_revision`, may require up
      to 32 validated stable `api_requires` identifiers, and are rejected during both
      discovery and final load before entry-script side effects when requirements cannot
      be met. `mod.api` exposes major, revision, sorted capabilities, `has`, and `require`;
      flat API-1 compatibility fields and the local-only mismatch escape remain. The
      deprecation/removal rules, schema, console diagnostics, native compatibility matrix,
      Lua/build integration checks, and full reference examples are pinned.

    - Removable owner-scoped Lua subscriptions: `mod.on_frame`, `mod.on_tick`,
      `mod.on_tick_post`, `mod.on_event`, `mod.on_layout`, and `config.on_action` now
      return handles with idempotent `remove()`. Dispatch snapshots stable subscription
      IDs, so callbacks can safely remove themselves or later callbacks and newly added
      handlers wait until the next dispatch. Unload still clears every remaining registry
      reference. API revision 4 exposes `events.removable`; a focused native callback-list
      stress test, Lua wiring checks, full runtime link, and API documentation pin the
      registration, removal, memory, and cleanup contract.

    - General owner-safe file picker: `mod.fs.pick_file(options)` provides bounded custom
      display-name/pattern filters, optional All-files exposure and initial filter choice,
      owner-enabled validation, Unicode Win32 dialogs, long UTF-8 result paths, distinct
      cancellation/native-error reporting, and strict pre-dialog rejection of malformed
      UTF-8, embedded NULs, paths, controls, metacharacters, and excessive counts/lengths.
      The character-specific helper remains only as a deprecated API-1 compatibility
      wrapper. Static source/build/docs coverage and complete API examples pin the public
      contract.

    - Clean public-source release path: `tools/export_public_source.ps1` builds a
      flattened, fresh-history Yule source tree from an explicit tracked-file allowlist,
      includes the native framework, online server/admin/LFG/redirect services, installer,
      updater, maps/mod examples, docs site, and tests, and overlays a developer README,
      contribution/security guidance, Git attributes/ignores, and safe example configs.
      It hard-excludes runtime/game binaries, build output, caches, logs, dumps, account/
      rating databases, server secrets, player configs, and the retired cosmetics package,
      scans exported text for credential/private-key patterns, records source provenance,
      and has both working-tree review and committed-HEAD export smoke coverage.

    - Match-protocol-v4 correction/result race repair: native win polling and GAME-to-MAIN
      terminal transitions are deferred while either peer is inside a coordinated
      correction barrier, preventing a joiner's known-divergent READY state from ending
      the match before the host COMMIT can be applied and mutually released. The corrected
      transition is released after bilateral correction completion. A terminal transport
      error now polls an already-authoritative native winner before teardown and otherwise
      sends `match_transport_failure` instead of fabricating a local loss; the server
      resolves bilateral failures as a no-contest and never turns the two peers' failures
      into conflicting winner reports. Static client ordering and real server protocol
      coverage pin both paths.

    - Release-default Discord Rich Presence hardening: the public Yule Application ID
      `1531027934004117664` is compiled into every normal build, presence defaults on when
      no config exists, a valid local ID may override it, and a stale blank/invalid
      override now preserves the compiled default instead of silently making the feature
      unavailable. Native parser and static build/wiring tests pin the shipped ID, default
      state, override behavior, toggle persistence, and privacy contract.

    - Window diagnostics log de-noising: transient SDL focus/attention flag changes no
      longer count as geometry changes, so routine window events cannot spam identical
      `window[event]` INFO lines. Actual display, logical window size, drawable size, and
      DPI-affecting changes remain logged.

    - Hostable developer documentation site: `docs-site/` contains 31 dependency-free
      HTML pages covering installation, framework architecture/building, content/map-Lua
      APIs, V1/V2 maps, configuration, console, audio/Dollchan tracks,
      online/server/LFG/deep links, updater/releases, testing, and security. Its
      developer-oriented second-generation format now uses
      task-based global navigation, per-page contents, page/member search, individual API
      deep links, namespace filters, responsive three-pane layout, and light/dark modes.
      Every public ordinary-mod registration, global/mod alias, exported compatibility
      bridge, higher-level UI/animation method, content transaction, map callback/object/
      tile method, metadata property, and command constant has an individual signature,
      description, parameter/return/failure contract, constraints, and example. A
      dependency-free source/API coverage checker pins 21 namespaces and 269 documented
      members while auditing all 31 pages for structure, links, and fragments.

    - Small toggleable online-match network HUD: Online Settings and
      `ggpo.net hud [on|off]` share a default-off persisted `match_hud` preference. Active
      gameplay/paused Options renders one muted top-right line using the existing smoothed
      peer RTT, local input delay, and rollback count without adding protocol traffic.
      State/config/console/render wiring has focused static coverage; live visual and
      controlled-latency acceptance remains listed above.

    - Owner-safe bounded `mod.http`: opaque handles belong to their creating mod,
      outstanding work is canceled on unload, canceled slots cannot be freed/reused until
      their WinHTTP worker actually publishes completion, worker response buffers remain
      private until publication, and both declared and streaming bodies are capped at
      8 MiB. Successful and HTTP-error terminal polls now include bounded status, final
      URL, redirect count, and allowlisted response-header metadata; automatic redirects
      are capped at five and refuse HTTPS downgrades. Ten-second phase timeouts and the
      eight-worker process ceiling remain. A real concurrent loopback WinHTTP test covers
      redirect metadata, owner isolation, all nine slots, cancel/unload, deferred worker
      cleanup, slot reuse, and stale handles; static coverage pins the limits and wiring.

    - Private-LAN online-server administration baseline: a separate default-loopback,
      dependency-free listener has no password/login page, refuses public/wildcard binds,
      rejects non-private clients, and uses automatic random IP-bound HttpOnly/SameSite
      sessions plus CSRF-protected POST actions. The Yule-styled dashboard provides live
      player/client presence, ordered queues, match participants and network phase,
      challenges/rematches, the full social graph, account search, password reset,
      ban/unban, force disconnect, and rating reset through existing server
      persistence/lifecycle paths without exposing credentials, match tokens, or raw P2P
      endpoints. It shuts down with the service and has real HTTP
      private-session/CSRF/action/rendering tests plus deployment docs.

    - Abandoned public cosmetics transport API removal: `mod.online` now exposes only its
      useful read-only status table. Compile-disabled profile/asset setters, readers,
      applied-revision calls, and cosmetic revision status fields are no longer exported
      or documented, avoiding a dead API-1 compatibility promise.

    - Bounded and Dollchan-compatible tracks in the native music playlist: a marked
      contiguous `data/tune*.txt` file can use the allocation-free bounded numeric grammar
      or an isolated system Chakra JIT with a core-only QuickJS-NG compatibility fallback,
      both matching Dollchan's JavaScript function shape, shortened Math names, `int`,
      scalar/stereo-array returns, bytebeat,
      signed-bytebeat, floatbeat, and funcbeat output conversion. JavaScript tracks have a
      private 64-MiB heap, 8-MiB source cap, adaptive bounded compile/callback watchdogs,
      and no host filesystem/process/network/DOM modules. The Windows JIT is loaded only
      from the system directory with reliable interruption; the fallback has an additional
      256-KiB stack cap and no `quickjs-libc`/std/os. Contiguous blocks of up to 4,096
      source frames cross the JavaScript boundary once, rather than once per sample, while
      preserving sequential formula state and stereo channel holds. JavaScript runs on a
      dedicated producer which completely prefills a bounded eight-block PCM ring; the
      real-time callback only performs nonblocking consumption and source-rate conversion.
      Both engines stream through Eggnogg's stereo callback with
      volume, hot reload, no gameplay RNG, an optional 4,000-48,000 Hz source rate
      (8,000 Hz default), and engine-specific CPU ceilings. The framework upgrades the
      Ghidra-confirmed vanilla 22,050-Hz SDL mixer request to a configurable 48,000-Hz
      default before audio opens,
      preventing destructive downsampling/aliasing for every accepted track rate while
      preserving unrelated explicit audio requests and the vanilla path if detouring fails.
      `music_output_rate`, `music.output_rate`, and optional marker `output_rate` can select
      8,000-192,000 Hz independently of the formula's source rate.
      Native postfix tracks, shuffle,
      and the music setting remain compatible. “Steady On Tim” is restored verbatim from
      Dollchan, declares bytebeat at 44,100 Hz, and its pre-drum reference samples match
      Dollchan byte-for-byte. The shipped 76-KiB “Impromptu” regression declares its
      native 32,000-Hz unsigned-bytebeat format, and consecutive full blocks of it plus
      the current 44,100-Hz unsigned-bytebeat “Last Fountain” match independent
      Dollchan-compatible hashes over both channels and their first 8,192 source frames,
      while compiling/rendering within duration-aware watchdog budgets. Console controls
      list titles/formats/gaps, flag unmarked
      general JavaScript, retally files added
      while open, inspect selection, force a track, and restore shuffle using the
      Ghidra-confirmed native tally/random/settings globals. Parser, original-track,
      stereo/mode/isolation/limit, and static callback/build coverage are in the guarded
      core suite; audible comparison remains in `TESTING.md`.
    - Native `K` hazard spawn-budget prerequisite: Ghidra confirms the 16-record thing
      pool skips slot zero and must reserve two of the 15 allocatable slots for players.
      V1/V2 package validation now counts every `K`, sword, and mine reset spawner in each
      room and rejects a K-bearing room above the conservative 13-object limit with exact
      per-kind diagnostics, before the native unchecked `thing_new(3)` path is reachable.
      Mine-only/sword-only rooms keep their established fail-safe/recycling behavior, so
      existing high-mine maps remain valid. The maximum/count/limit are exposed to package
      tooling and registration logs. Generic `native_glyph: "K"` remains excluded because
      non-map registry owners do not pass this package audit. Guarded parser/runtime-fixture
      tests cover the exact limit, over-limit K, combined spawners, and compatibility.
    - Native custom-map crash containment: package validation now rejects `T`, `t`, and `s`
      anchors whose upward expansion would leave the 33x12 room, preventing the recovered
      `tentacle()` null write. The native reset-spawn action is also detoured at its exact
      six-byte boundary and checks failed sword/hazard allocations before the original
      unchecked write at `0x43CA88`; an exhausted fixed thing pool now skips that spawn and
      records a warning instead of terminating the game. Guarded C and static hook tests
      cover both paths.
    - Greggnogg preview state-boundary safety: an accepted in-memory preview now ends the
      former menu/game update immediately after `game_reset` and the state switch. The
      recovered crash dump proved the old callback resumed into `reset_room`, duplicated
      the center room's eight `K` hazards, and exhausted the thing pool on the second pass.
      Preview launch now reports its destructive transition to both native and custom-state
      callers, with static ordering coverage; installed-map startup behavior is unchanged.
    - Privacy-bounded Discord LFG bridge: the Node control server waits two seconds, then
      posts one configured channel embed only for a player who is still in a public casual/
      competitive queue after normal matchmaking. Challenges never create posts. HTTPS
      buttons use the existing validated friend-challenge and same-queue flows. It never
      receives ratings, match IDs, endpoints, credentials,
      rendezvous/auth data, or arbitrary links; mentions are disabled and there is no
      direct match route, Gateway, intents, commands, or inbound bot listener. Duplicate
      joins coalesce, generation ownership makes create/leave races safe, and exact posts
      are edited in place to remove their buttons and show matched/left/changed/offline/
      shutdown status. Matched posts remain visible for one day and are then deleted;
      other inactive states remain as history. REST
      timeouts, responses, retries, 429 delays, tracked users, and logs are bounded. A
      loopback-only, no-store redirect exposes only the completed canonical public
      `yule://` routes behind deployment TLS; its landing page launches the protocol and
      attempts to close itself, with a visible manual-close fallback when browser policy
      refuses. Node and static tests cover payload privacy,
      lifecycles, rates, strict links/methods/headers/binds, server call ordering, and clean
      shutdown; real Discord/proxy/client acceptance remains above.
    - Expanded audio/music with bounded bytebeat and floatbeat: the existing mod-relative
      file SFX/music, owned channels/volumes, native synth IDs, mixer decoding, and WAV
      fallback now include a purpose-built JS-256-style numeric compiler with decimal/hex/
      binary literals, sample/rate/seconds variables, JavaScript 32-bit bit operators,
      short-circuit logic/conditionals, fixed Math functions, and byte-offset diagnostics.
      It renders canonical mono PCM16 in bytebeat or normalized floatbeat mode with
      gain and click fades, never evaluates general code or writes temporary files, and
      mixes through Eggnogg's already-open audio callback without requiring SDL2_mixer or
      opening a competing legacy SDL device. Expression/node/depth/sample/
      operation limits plus per-mod 16-chunk/4-MiB exact-key caches bound CPU and memory;
      stop, clear, status, validate-without-playback, unload cleanup, and diagnostics are
      exposed through `mod.audio`. The allocation-free/nonblocking callback bridge owns at
      most 8 voices per mod and 32 globally. Strict native grammar/render tests, full proxy
      compile, and static ownership/build tests are complete; audible in-game acceptance
      remains above.
    - Privacy-bounded Discord Rich Presence: a dependency-free local Windows RPC client
      lazily connects to Discord desktop through overlapped, incremental, 64-KiB-bounded
      pipe I/O without blocking gameplay. A closed enum maps menus, local play, hub/sign-in/
      social, queues, prematch, and casual/competitive/private matches to fixed text; the
      payload cannot accept usernames, opponents, maps, Elo, match IDs, endpoints,
      credentials, tokens, party/join secrets, buttons, routes, or arbitrary strings.
      Activity changes coalesce behind a 4.1-second limiter, malformed/closed IPC retries
      at a bounded cadence, settings persist atomically, unavailable deployment config is
      explicit, the public Application ID can be validated/saved/applied immediately with
      `discord.app`, and ordinary quit closes without loader-lock work. Guarded native and static
      tests cover parsing, every activity, privacy exclusions, wire frames, bounds,
      no-wait wiring, settings, shutdown, and production linkage; registered-app/live-client
      acceptance remains above.
    - One-shot updater handoff: the injected updater downloads and verifies the
      complete replacement set, captures each original target's exact size/SHA-256 (or
      absence), and durably stages Journal V3 without renaming a mapped DLL.
      `YuleUpdater.exe` is built and installer-shipped outside `latest.json`; Start Menu,
      Steam, direct launches, and `yule://` route straight to `eggnoggplus.exe`. Once a
      verified transaction is ready, the framework waits for a safe menu, launches the
      helper with its exact PID, and the helper requests orderly `WM_CLOSE`, waits for exit,
      replaces/recovers the mapped proxy, relaunches, and exits without force termination.
      Under the root-specific mutex
      it revalidates pristine staging and original
      preconditions, owns write-through swaps, whole-set commit/rollback, legacy V1/V2 and
      no-journal `.old` recovery, cleanup, regular-file checks, privacy-safe logging, and
      shell-free exact argument forwarding. A pristine transaction with no mutation evidence
      is safely discarded and restaged if its download or original precondition changed;
      once swaps/backups/quarantines exist, unsafe or ambiguous evidence still blocks relaunch
      without guessing. The mod scanner ignores the updater-owned staging directory. The
      maintenance installer refuses a running same-install helper and invokes the bundled,
      verified helper in recovery-only mode before replacing release files whenever staging
      or legacy backups exist; it aborts rather than writing across unresolved evidence. The
      installer records hash ownership and removes only an unchanged
      helper; the release builder requires byte identity with the verified build artifact,
      exact agreement between requested/source/compiled framework versions, payload-first/
      atomic-manifest-last publication, and rejects a helper that imports any replaceable
      runtime DLL, but excludes the helper from its replaceable channel manifest. The
      helper itself refuses disk mutation while another process runs the same installed
      game executable.
      This prevents both self-locking `libwinpthread-1.dll.old` cleanup failures and
      multi-client mapped-DLL commits. Guarded disposable-root
      tests cover partial and complete power-cut states, valid/corrupt commits, safe stale-
      pristine restaging, changed originals, corrupt staging/journals, legacy backup restoration, forwarding edge cases,
      verified install/routing/uninstall, and the existing updater recovery/loopback matrix;
      production HTTPS and abrupt-machine acceptance remain above.
    - Linux/Wine installer: the release ZIP now includes a native Bash installer and safe
      uninstall wrapper for the existing Windows game build. It validates the HTTPS channel,
      exact sizes and SHA-256 hashes, preserves vanilla SDL, installs the same updater helper,
      records hash ownership, and creates an XDG desktop launcher plus `yule://` handler bound
      to one Wine prefix. Detached/file-manager runs accept the documented `Y/n` defaults
      instead of aborting on stdin EOF; Plasma 5/6 application caches are refreshed and the
      launcher/protocol result is verified. Steam shortcut VDF uses canonical keys and is
      read back after its atomic write. Uninstall restores SDL and removes only unchanged
      owned files while preserving maps, mods, saves, and local modifications. POSIX
      integration coverage now exercises closed stdin, a fake Plasma 6 cache, MIME handler,
      Steam account/artwork lifecycle, game, signed channel, and Wine command without
      launching the game. Native Linux builds and distro packages remain future work.
    - Online player palette synchronization: host/P1 and join/P2 capture their local
      built-in skin/clothing choices before every socket attempt, exchange only those two
      bounded IDs through a dedicated fixed-size authenticated packet, and require exact
      bilateral receipt/echo before prematch READY. Socket retry preserves the local tuple
      while clearing stale peer proof; malformed counts, out-of-range IDs, and conflicting
      same-session tuples fail closed. The choices are applied to their server player slots
      only after the final authoritative frame-zero restore, while the generic cosmetic
      profile/asset channel remains compile-disabled and palette settings remain outside
      rollback/checksums. The guarded pair uses deliberately different choices on each
      peer across the prematch matrix, and static coverage pins role capture, restore/apply
      ordering, and serializer separation; live visual assignment remains release QA.
    - Automatic paired first-desync snapshot preservation: the detecting peer and the host
      correction responder each copy the exact retained canonical post-frame boundary
      before correction can replace it, then a background worker writes analyzer-compatible
      binary state plus versioned ASCII metadata under `mods\desync_repros`. A symmetric
      opaque pair tag correlates the files without persisting raw session IDs; metadata
      excludes credentials, endpoints, usernames, and tokens. Temporary files are flushed
      and atomically replaced, copied state is erased, teardown waits for bounded worker
      completion, and allocation/thread failure remains retryable. Bilateral
      executable/framework build IDs, rollback layout IDs/capacity, and a deterministic
      transport-config ID make incompatible reports visible. The guarded short and
      2,047-frame loss/delay correction pairs require exactly one opposite snapshot per
      role and reciprocal identities, while static coverage pins the capture boundary,
      privacy rules, background-I/O ordering, and cleanup. Full-session traces, the
      complete content/map/mod/rules manifest fingerprint, lossless spill for more than
      8,192 pre-divergence chaos events, and sequence-aware network measurements remain
      open above. Each capture also includes an independent reproducible chaos seed and a
      chronological privacy-safe history of simulated drop, delay, and queue-overflow
      events (service tick, packet type, and outcome only); both guarded correction cases
      require the recorded count to equal the complete event total through divergence.
    - Safe LFG launch intents and `yule://` links: strict bounded switches/routes open the
      hub, social inbox, casual/competitive queue, or the existing compatible-map friend
      challenge picker. Auth-required actions wait for remembered/manual login, challenges
      wait for a complete friend snapshot, and every intent expires after two minutes or
      cancels when leaving the hub. A per-login-session, message-only Windows broker owns
      validated activation handoff: a protocol launch forwards to the already-running game,
      grants it foreground permission, and exits before SDL creates a second window. Normal
      direct launches remain multi-instance for local testing, and an intent arriving during
      a match waits instead of becoming an out-of-band forfeit. There is deliberately no raw endpoint, direct-session,
      credential, match-ID, or token route; malformed/conflicting actions and URI injection
      syntax reject the entire intent. The per-user installer handler uses a single quoted
      envelope, refuses an existing owner, never registers after incomplete adoption, and
      uninstall removes only its exact manifest-receipted registry tree. Native parser,
      static runtime, isolated HKCU installer lifecycle, protocol documentation, full proxy
      compilation, and guarded
      source/link coverage are complete; the separate privacy-bounded Discord consumers
      are completed below.
    - Guarded established-match disconnect phases: the ordinary paired case now advances
      deterministic nonzero inputs through frame 2,048 and mutual checksum confirmation
      through frame 2,047 before the host sends an authenticated terminal `BYE`; the joiner
      must receive the explicit peer-disconnected result while still servicing gameplay.
      A second paired case injects a real canonical mismatch and permits shutdown only
      after the normal REQUEST/OFFER correction barrier is active with a host-owned
      nonzero correction ID and snapshot. A third waits until that OFFER has authenticated
      the joiner's RECEIVING phase, covering shutdown during snapshot transfer. The host
      treats RECEIVING as a real bilateral boundary and does not start bulk correction
      chunks until it authenticates that tuple, rather than allowing a fast transfer to
      skip the observable phase. A fourth
      carries the mismatch through snapshot staging and deterministic replay, then shuts
      the host down specifically in COMMIT while the joiner is READY with the same nonzero
      barrier identity and transcript. A fifth advances through receiver APPLIED and shuts
      down in host RELEASE, proving replay completion cannot be mistaken for a successful
      mutual release. A sixth reverses the terminating role: the joiner sends `BYE` from
      RELEASE_ACK and the host attributes it to the exact RELEASE/RELEASE_ACK tuple. The
      authenticated terminal tuple is retained for diagnostics if the host consumed the
      acknowledgment and cleared its active barrier one service tick before `BYE`. Both
      roles prove local transport teardown across every stable correction phase, the hidden two-process
      runner exposes focused modes and includes all six in its full matrix, and native
      entity lifecycle coverage remains explicitly open.
    - Bilateral private rematch: a committed confirmed result creates a bounded
      server-owned offer only while both exact participants remain connected, idle, and
      mutually unblocked. The first explicit vote waits and notifies the opponent; the
      second revalidates the exact prior map through normal matchmaking and starts a fresh
      match with new IDs, roles, seed, rendezvous, and packet-auth data. Rematches are
      private/unranked even after competitive play. Queueing, blocking, disconnecting,
      declining, closing the actionable toast, expiry, or another match cancels the offer;
      terminal replay cannot resurrect it. The compact non-modal result toast supplies
      countdown-aware mouse buttons, `R`/`N`, and distinct controller X/Y actions without
      restoring a fullscreen result screen. The required server capability, deployment
      preflight, isolated wait/accept/decline/expiry/queue/block/disconnect runtime
      coverage, client static contracts, protocol documentation, and guarded compile are complete; live
      two-client acceptance remains listed above.
    - Server-derived friend presence: authenticated server state now distinguishes online,
      casual queue, competitive queue, match setup, active match, and offline without
      trusting a client-published status. Snapshots refresh at every queue/match boundary,
      the Friends UI displays readable color-coded labels, and challenge controls disappear
      while a friend is busy (with matching server-side rejection). Blocked entries still
      expose no presence. The isolated multi-client server test covers queue, setup,
      committed-match, and restored-online transitions; static coverage pins every
      lifecycle refresh and the client labels.
    - Persistent server-backed social block/mute controls: the Friends tab now has a
      focusable mouse/keyboard/controller context menu for Challenge, Mute/Unmute, Block,
      Unfriend, and Unblock. Mutes are private per-friend notification preferences:
      challenges remain visible/actionable but do not create pop-up toasts. Blocks
      atomically remove mutual friendship, both request directions, pending challenges,
      and prevent either blocked pairing in casual or competitive queues. The blocker's
      snapshot exposes only the blocked username, never presence or Elo; the other player
      receives generic unavailability, and unblocking does not restore friendship.
      Normalized lists persist in the account database, the client requires the explicit
      social-controls server capability, deployment preflight checks it, and isolated
      server runtime/static tests cover persistence, privacy, challenge suppression,
      interaction rejection, and queue exclusion.
    - Single canonical capture per simulation boundary: live play, ordinary rollback, and
      coordinated correction now capture each post-tick state once as the next frame's
      retained pre-state, then reuse that exact blob instead of recapturing mutable live
      memory. One checksum-canonical scratch pass produces both the boundary CRC and optional
      component summary, so history, checksum publication, correction snapshots, and desync
      diagnostics cannot observe different versions of the same frame. Initial and received
      authoritative snapshots seed the same boundary cache; failed live ticks restore the
      exact retained pre-state. Correction replay now treats immutable role-local input
      rings as authoritative and uses finalized history only when an acknowledged ring
      generation is absent, preventing stale pre-rollback history or delayed ACK viewpoints
      from trapping a fully received correction barrier. A valid transition delivered on
      the timeout boundary is consumed before no-progress failure. Serializer/analyzer and
      static contracts, four consecutive lossy correction runs, the exact 2,048-frame
      byte-comparison chaos soak, and the complete guarded prematch matrix pass.
    - Authoritative friend-challenge map picker: challenging an online friend now requests
      the complete intersection of both sanitized map manifests, presents every compatible
      map in a cancellable keyboard/controller/mouse left-right picker, and sends the exact
      selected key. Request IDs and exact begin/choice/end counts reject stale or truncated
      responses. Control protocol v3 bounds manifests to 4,096 entries; the server validates
      friendship, presence, and the selected key before creating the challenge, stores and
      displays its map label for both players, then recomputes the intersection at accept
      before forcing that exact map with each client's local selector. Queue matchmaking
      remains random and no longer mutates recent-map history during compatibility probes.
      Static client contracts and an isolated two-client server runtime test cover the full
      picker, invalid-key rejection, challenge/accept flow, and differing local selectors.
    - Yule installer v2 lifecycle: the interactive picker now selects
      `eggnoggplus.exe` directly while `-GamePath` accepts either the executable or folder.
      The release zip includes `UNINSTALL.bat`; its conservative uninstall preflights the
      vanilla forwarder, restores `SDL2_real.dll`, removes the exact recorded Start Menu/
      Steam entries, deletes only safe relative managed files whose current SHA-256 still
      matches the install receipt, and preserves the game, mods, configuration, modified
      files, and untracked files. The v2 manifest records the executable and per-file
      path/hash/size ownership receipt. Channel entries now require bounded safe paths plus
      valid size/SHA-256 metadata, downloads verify both, and an incomplete fresh adoption
      removes newly added exact-hash files and restores vanilla instead of leaving a broken
      half-install. An isolated loopback lifecycle test covers EXE selection, verified
      install, modified/user-file preservation, vanilla restoration, manifest cleanup,
      missing-forwarder fail-closed behavior, and offline first-install rollback.
    - Expanded player-palette readability: the nearly-black custom charcoal choice is now
      a visible dark charcoal (`0.20, 0.22, 0.25`). A static palette regression prevents
      any solid expanded choice from returning to effectively black luminance.
    - Rollback-safe peer-local room/mine palettes: native `game_update` consumes
      `_game_do_lerp_colours` early, but room changes and mine effects can set it later in
      the same tick for the next update. Rollback transport intentionally scrubs that flag
      and `_lerp_time`, while the 48-float `0x541F6C..0x54202B` colour block is
      checksum-excluded presentation state; loading only half of that group could lose the
      pending transition and leave an old room or mine tint visible. Active-match ordinary
      rollback and coordinated correction now capture and atomically restore the coherent
      flag/timer/block as a peer-local sidecar before replay, without changing gameplay
      checksums or preserving a prior map's palette during prematch load. The production
      serializer test covers byte-exact restoration plus invalid/NaN no-partial-write
      rejection, and static coverage pins capture/load/restore ordering. A two-client
      visual soak remains under Runtime acceptance.
    - Multi-generation paired rollback soak: the seeded nonzero-input chaos case now runs
      through frame 2,047, reusing the 512-slot history ring four complete times under 20%
      loss and one-to-eight-tick delay/reordering. Both roles exercise prediction and
      rollback, byte-match every finalized canonical pre-frame state and post-frame
      checksum, and drain cumulative checksum proof in both directions. Each peer streams
      its retained canonical history to a private temporary trace which the supervisor
      parses structurally and compares frame-by-frame before deleting it. The Python runner
      drains both child output pipes concurrently; its old
      sequential `communicate` order could fill the idle child's Windows pipe, block that
      peer inside debug logging, and manufacture a 600-tick network timeout. Failure
      output now includes the complete secret-free `net.diag` snapshot.
    - Paired uint32 frame-wrap rollback soak: a second authenticated 2,048-frame chaos
      session begins at `UINT32_MAX - 1023`, crosses `UINT32_MAX -> 0` halfway through, and
      finishes at frame 1,023. A test-only synchronized epoch rebase preserves the exact
      canonical starting boundary, quarantines pre-rebase packets, and recreates the neutral
      prediction/ACK prefix that a naturally long-running session already owns; production
      sessions still reach the same serial boundary only by advancing normally. Every frame
      in the measured interval travels through the lossy socket path. Both roles predict,
      rollback, survive 20% loss plus one-to-eight-tick delay/reordering, byte-match all
      2,048 canonical states and post-frame checksums across the wrap, and drain cumulative
      checksum proof in both directions. The focused wrap runner, static rebase contract,
      and complete guarded prematch matrix pass.
    - Secret-safe schema-aware peer-trace diff utility: `tools/peer_trace_diff.py` now streams and
      structurally validates the canonical trace format used by the paired chaos runner,
      including strict nonzero/size-bounded records and consecutive uint32 frames across
      wrap. It reports the first divergent record/frame, state length or byte offset,
      finalized post-frame checksum, and SHA-256 of each state without printing state
      contents. Recognized canonical EGG0/v9 states additionally expose SHA-256 for every
      non-overlapping header/map-script/native/player/things/tilemap component, every
      differing entity slot, the changed-tile count, and the exact first header field,
      map-script entry, player field, entity slot/field, or tile index/x/y/cell byte. Strict
      magic/version/count/size/dimension/header validation falls back to the generic report
      for older or arbitrary traces rather than guessing. Native compile-time offset/size
      assertions require a version bump if the mirrored 32-bit layout changes. Plain and
      JSON output retain stable exit codes for identical, divergent, and invalid traces.
      Guarded tests cover exact wrap equality, every schema location class,
      byte/checksum/length/count differences, empty/truncated/oversized input, and broken
      frame sequences. Automatic per-session repro capture remains open above.
    - Long-session forced mismatch and coordinated correction: a guarded paired case enables
      20% loss plus variable delay/reordering from the start, drives changing nonzero input
      past one complete 512-slot ring generation, and injects one-sided canonical divergence
      at frame 600. The asymmetric checksum path requests the host snapshot, agrees on
      correction `D=600`, one bounded resume frame, and the exact replay transcript,
      survives authenticated duplicate replay plus a forced correction-chunk `would-block`
      without advancing its send cursor, and releases cleanly. Both roles then continue
      through frame 2,047 with prediction and rollback, agree on the final checksum, and
      drain the new correction generation's cumulative checksum proof in both directions.
      The original short correction case remains as a fast focused regression; both focused
      modes and the complete guarded prematch matrix pass.
    - Committed disconnect/forfeit and normal-result settlement: deliberate local exits send the
      server's committed `match_abort` forfeit signal instead of an ordinary loss guess, so
      the still-connected opponent receives an immediate confirmed win. The server keeps
      one exact terminal response per connected participant and idempotently replays it
      when a queued P2P-disconnect loss report arrives after resolution, eliminating the
      misleading `invalid or stale match result` status without letting old messages alter
      a new match. Native result detection is pinned to the terminal end countdown (leader/
      loser identity) with positive-target final score as a fallback. Normal reports now
      include the synchronized winning player slot; the server maps that slot through its
      recorded host/join assignment, so an inverted client-local win/loss interpretation
      cannot manufacture conflicting winners. Reporting no longer tears down rollback or
      changes state: a live countdown/final-winner gate also blocks previously queued hub
      handoffs and terminal control messages while GAME owns the presentation. Eggnogg
      completes its native win animation and returns to main first, then the client stops
      transport and opens the online hub. Static client/server checks
      and live TCP protocol coverage pin legacy conflict rejection, slot-authoritative
      agreement, disconnect replay, and the deferred native return.
    - Bounded hostile-NAT UDP relay fallback: the rendezvous server still chooses one
      deterministic direct loopback/LAN/public route first. A genuinely fresh client socket
      generation switches both peers together to a server relay marker on the existing UDP
      port. The client resolves that marker through its already-validated control hostname.
      Relay admission is limited to active match-owned observed endpoints, the server-recorded
      sender player slot, protocol-v17 packet bounds/types, and per-endpoint packet/byte
      budgets; peer HMAC/replay validation remains end to end, and amplification is one for
      one to the authenticated opponent endpoint. Setup aborts revoke immediately; a
      successful relayed result retains only the authenticated endpoints for a 15-second
      native-presentation grace, then revokes them. The isolated runtime test proves initial
      direct routing, symmetric fallback after a fresh-socket retry, and byte-exact binary
      forwarding before and after result consensus. Established mid-game rebinding/resume
      remains open above.
    - State-preserving online-server updater: `online_server/update_server.sh` shallow-clones
      the latest configured branch into a disposable directory, runs Node/static/two-client
      protocol tests before downtime, stages only non-runtime server files, stops systemd for
      the swap, restarts, and runs the local TCP+UDP deployment probe. It never stages account
      JSON, ratings, server secrets, logs, environment files, PID/socket files, `node_modules`,
      or caches. Exact overwritten/new application files are tracked for rollback if start or
      health validation fails; the live runtime state remains in place throughout.
    - State-preserving repository-wide updater: `tools/update_repository.sh` validates a
      fresh clone, accepts fast-forwards only, replaces/prunes tracked repository files, and
      advances the real branch/index without touching the complete live `online_server/`
      tree, runtime configuration/logs/caches/build output, editor settings, extra configured
      preserve paths, or untracked community content. Staged/non-runtime local edits and
      mismatched untracked collisions fail closed, while byte-identical upstream additions
      are adopted for safe first-run bootstrap; dry-run output, post-copy comparison, and
      automatic file/index/ref rollback are covered by isolated throwaway-repository tests.
    - Once-per-frame local-input commit boundary: hook-side physical input remains a
      tentative wall-tick sample until frame-zero, correction, frame-advantage, prediction,
      and checksum-retirement gates all permit simulation. Stalled updates repoll without
      assigning the blocked future frame, while an already committed logical-frame retry
      remains immutable. A guarded two-peer regression holds one peer transport-only,
      forces a prediction hard stall through 32 stale samples, changes the physical value,
      proves only the fresh value is committed on recovery, and checksum-confirms both
      peers afterward. Adaptive input delay and GGPO-style time sync remain open above.
    - Failure-atomic live rollback ticks: after saving a canonical pre-state, a native
      advance that later fails checksum or clean simulation-state capture reloads that
      exact pre-state before the online session aborts. Setup also has an explicit
      `start_state_loaded` no-advance gate instead of relying on earlier hold/layout/link
      ordering. A guarded paired injection fails clean-geometry capture after both native
      ticks, then proves both peers remain on frame zero with byte-identical restored state.
    - Strict online control transport: atomic queued writes/backpressure, strict flat JSON,
      framing limits, independent connect/auth deadlines, secret cleanup, and complete
      all-or-nothing map manifests. A 30-second authenticated heartbeat keeps idle hubs
      and long matches alive across the server's receive-idle timeout. TLS remains the
      release blocker listed above.
    - Authenticated P2P UDP, introduced in v16 and retained by the current v17 wire
      protocol: per-match 256-bit tokens, directional HMAC-SHA-256/128, CSPRNG sessions,
      constant-time verification, replay window, and fail-closed key use.
    - Post-content rollback-state layout freeze: server-managed P2P attempts now begin as
      socket/auth-only held sessions with zero rollback capacity. After the exact selected
      map/content, synchronized seed, native reset, and native start state are installed,
      transactional finalization measures a versioned structural layout, allocates all 512
      history slabs and state-transfer buffers, and rejects a changed refinalization. Peers
      must prove the same non-cryptographic 32-bit structural layout ID and exact capacity;
      setup waits for missing proof and aborts on any mismatch before hold release, after
      which the host captures the authoritative initial state. Retry clears the old proof
      and reannounces the new session's layout. This slice originally reused the formerly
      receiver-unused v16 `last_checksum` slot; current v17 retains it as
      `state_layout_id`. Its semantics are not a broad old-DLL compatibility promise: the
      managed binary fingerprint rejects mixed old/new DLLs. The layout ID describes
      structure, while the server-selected `map_key` chooses the advertised package; its
      32-bit signature is not cryptographic content identity, and the complete
      deterministic compatibility manifest remains open above.
    - Wrap-safe input/history rings: uint32 serial ordering, a single-generation receive
      window (283 retained frames and 228 future frames), stale/far-future rejection,
      generation-aware input/history writes, retained-checksum filtering, and wrap-safe
      rollback/prediction/diagnostic scans. Reordered redundant inputs no longer regress
      the newest command marker or make prediction use a future command. The deterministic
      native test pins both admission edges, 512-slot aliases, out-of-order prediction,
      history preservation, and UINT32_MAX wrap. This foundation is retained by the
      wire-breaking v17 confirmation and recovery protocol described below.
    - Reliable v17 remote-input confirmation: every authenticated INPUT packet carries a
      monotonic cumulative ACK and a 512-bit SACK. Same-frame inputs are immutable; the
      live edge and oldest unresolved holes are resent before the defensive tail; neither
      reordered data nor ACKs can regress prediction or confirmation. Checksum publication,
      comparison, correction detection, and history retirement stay at or below the mutual
      contiguous-input and finalized-history horizon.
    - Hard v17 input/checksum recoverability: prediction stops permanently at the oldest
      unresolved-input boundary while transport continues selective resend, rather than
      resuming after a timeout. Generation-scoped cumulative checksum ACKs advance only
      after successful comparison; live-edge plus go-back-N checksum resend and proof from
      both directions prevent history retirement until delivery completes. A checksum
      no-progress timeout disconnects instead of overwriting required state.
    - Transactional received-state application: complete start/correction transfers are
      transport-canonical prevalidated against the existing rollback-blob schema, size, and
      advertised checksum before mutation. Apply uses a preallocated raw backup, verifies
      the resulting canonical live state, restores and byte-verifies the exact prior state
      on failure, and treats a failed restore as terminal. A guarded native regression now
      drives the production serializer through raw byte-identical roundtrip, canonical
      transport validation and pointer rejection, canonical load scrubbing, and a failed
      preflight with zero partial mutation. This completes raw-blob transaction safety,
      not the still-open typed native schema/adapter.
    - Mutually confirmed v17 correction barrier: REQUEST/OFFER/RECEIVING/READY/COMMIT/
      APPLIED/RELEASE/RELEASE_ACK freezes both simulations on one exact divergence frame,
      continues authenticated input/ACK recovery, stages and validates a complete host
      snapshot without receiver mutation, pins the same role-stable input span on both
      peers, then makes both peers rewind and deterministically replay it. The replay
      transcript covers epoch, generation, D/R frames, snapshot checksum, input pairs,
      and pre/post state checksums; only matching APPLIED/RELEASE proofs retire the old
      checksum generation. Conflicting tuples and failed apply/replay fail closed; policy
      changes are rejected during a barrier, and bounded no-progress disconnects. The
      paired guarded case forces an
      asymmetric divergence under 20% loss, delay/reordering, and authenticated duplicate
      replay, then proves matching snapshot/resume/transcript and drained checksum ACKs.
    - UDP socket-backpressure baseline: raw sends now distinguish physical success,
      retryable `WSAEWOULDBLOCK`/`WSAENOBUFS`, and hard Winsock errors. Retryable pressure
      leaves delayed datagrams and full/delta transfer cursors in place; hard failures are
      counted and cannot strand a delayed-queue entry forever. Every service tick gives
      fresh INPUT/ACK/HELLO traffic the first send opportunity, then drains delayed work,
      and caps initial/correction bulk bursts at eight datagrams. Compiled-out cosmetic
      profile/asset cursors also advance only after accepted sends. `net.diag` and
      `ggpo.net` expose would-block, hard-error, and deferred-work counters. The paired
      correction regression forces a state-chunk would-block under seeded loss/delay,
      proves physical-send count plus full/delta cursors do not advance, then completes
      the bilateral correction with matching transcript/checksum proofs; a static test
      pins error classification, queue retention, control-first order, and cursor rules.
    - Canonical rollback envelope v1 foundation: an independently tested, bounded section
      codec explicitly encodes compatibility identity, controller/player roles, fixed
      16-slot occupancy/allocator metadata, stable behavior IDs, ordered danger-slot
      references, tiles, and the existing MapScript snapshot subdocument. It rejects
      truncation, overflow, directory/reserved corruption, invalid enums/booleans/floats,
      noncanonical free slots, cross-section lifecycle mismatches, wrong session/script
      identity, and noncanonical re-encoding without partially changing its output. This
      is deliberately a foundation only: the opaque active-entity bridge and native
      capture/reconstruction/transaction adapter remain in the P0 item above.
    - Checksum-authoritative gameplay RNG, camera/shake, simulation dimensions, resume-
      input, and mine-activation baseline: rollback canonicalization v6 retains
      `_mrand_seed`, camera X/Y, camera-shake amplitude/decay, `_game_w`, `_game_h`,
      `_resumed`, and the `_mine_anim` last-tick guard at `0x541EFC` instead of masking them
      as presentation state. Native
      `player_update_logic` uses `_game_w * 0.5` as the loser-respawn threshold,
      `game_update_camera` uses it for horizontal clamping, and `game_update` reads
      `_game_h` for vertical camera clamps and camera-relative ambient sampling; `_resumed`
      clears buffered attacks. Score and mine paths deterministically add shake, while
      shake amplitude/decay control whether `game_update_camera` consumes zero or two
      gameplay RNG draws and writes camera X/Y; prior-tick camera X feeds loser respawn.
      `_mine_anim` checks its last-tick guard before the sound call, tile mutation, and
      shake write, so restoring it preserves same-tick second-mine suppression. Only the
      adjacent sword-sound and crowd-cheer debounce stamps remain masked. The crowd timer
      can change whether native `game_update` draws a near-win chant pitch, so the verified
      sound-only return site `0x42C9B8` now uses cosmetic RNG instead of advancing the
      gameplay seed during a rollback replay.
      The old peer-local cosmetic-RNG route for camera shake is removed. Rollback
      apply restores every authoritative field. The online loop captures authoritative
      post-tick RNG/camera/shake/dimensions and restores them before every live, rollback, and
      correction pre-state/tick, preventing local `adjust_layout` writes from becoming
      simulation input; it restores each client's dimensions afterward for rendering.
      Checked 1..4096 extents, authoritative apply preflight, and an all-destinations-first
      combined setter prevent invalid or partially restored camera/geometry state.
      Serializer, paired, and static regressions prove checksum sensitivity, finite-value
      rejection, apply restoration, same-tick mine suppression across save/load, different
      local aspect ratios, out-of-tick shake restoration, simulation/render separation,
      and call ordering. A failed post-tick clean capture restores the saved canonical
      pre-state before abort.
    - Canonical floating-point simulation controls: every native GAME update and its
      deterministic content interactions now run inside one per-tick control guard with
      vanilla-compatible x87 `0x037F` (64-bit extended precision, nearest/even, masked
      exceptions) and MXCSR `0x00001F80` (nearest/even, gradual underflow, DAZ off). The
      exact caller x87 control word and MXCSR are restored on success or reported failure;
      offline live, GGPO live, rollback/correction replay, loopback, local, and self-test
      paths converge on the same primitive. A native hostile-mode/nested/restoration/
      subnormal test and static call-path test cover the boundary, and desync dumps now
      record raw x87 plus MXCSR instead of a truncated `_controlfp` diagnostic.
    - Server match-protocol deployment guard: the client now requires a flat `server_info`
      capability handshake before login, rechecks auth/match version fields, and fails
      before queue/P2P setup on incompatibility. A public TCP+UDP preflight and isolated
      two-client runtime test prove that both peers receive one shared 64-hex match key
      and distinct probe keys. The pre-gate public service passed the
      control-v3/match-v4/P2P-v17 TCP+UDP discovery preflight; the new build-gate
      capability deliberately requires another matching server deployment and public
      preflight before the next client can sign in. The real two-game direct/relay round
      remains in Runtime acceptance. Each authenticated map manifest now also sends
      the client's framework label, exact control/match/P2P tuple, and deterministic
      build/game-EXE/framework-DLL fingerprints. The client requires the server's explicit
      build-gate capability before login. The server records and logs the manifest,
      rejects missing/old fields before queue or challenge entry, filters pairing by exact
      supported protocol and build tuples, rechecks at match creation, and sends the
      checked versions/build diagnostic in `match_found`. This closes both the silent
      v16/v17 mixed-client stall and same-protocol/different-binary late abort without
      pretending incompatible authenticated layouts are safe. Runtime coverage includes
      unadvertised-client and exact-build mismatch rejection, while the
      guarded native transport runner now separately proves current direct and symmetric-
      relay prematch handshakes and cannot hang forever behind its own Windows timeout
      cleanup. Legacy
      unauthenticated fallback remains forbidden.
    - Bounded P2P connection retry: three fresh sockets/mappings, one deterministic
      server-selected loopback/LAN/public route per attempt, authenticated fresh-session
      recovery when only one peer rolls its socket, and clean hub recovery after exhaustion.
    - Prematch preparation behind the countdown: connection, native reset, state transfer,
      and neutral frame zero complete before GAME is shown. Match protocol 3 keeps both
      clients behind the hub until each reports a restorable frame zero and the server
      commits gameplay; every pre-commit timeout, abort, disconnect, stale setup, or
      impossible ready-without-loaded postcondition fails before readiness is published;
      premature result is a no-contest instead of a fake win/loss.
    - Online simulation through the audited pause/options, player-one/player-two native
      input-remapping, console, and Mods overlays, with one tick owner per screen, menu
      input neutralized, and both local control sets merged into the assigned fighter.
    - Server-managed build/map compatibility gates and automatic suspension/guarding of
      framework-managed gameplay Lua for the full match lifecycle.
    - Toast-only online results: completed matches return to one stable hub state with the
      native main menu as Back owner and a compact bottom-right notification; the fullscreen
      win/loss/game-over state and its menu transitions are gone. Provisional toast display
      is separate from exact-match tracking, delayed server confirmation refreshes a hidden
      toast safely, requeue is gated until confirmation, and abort/no-contest clears the
      provisional outcome. Normal results require two reports naming the same winner;
      unilateral timeouts and conflicts are no-contests with no Elo change. A commit plus
      immediate forfeit result in one TCP batch resolves directly without entering a
      server-deleted GAME.
    - V2 custom-map parser and declarative content tile bridge: a sheet/grid can be
      declared once at `tileset` scope and inherited by every listed tile, with optional
      per-tile sheet exceptions. `native_layout` validates only the safety-critical
      128-cell native prefix, accepts arbitrary declared grid geometry and appended custom
      cells, and applies that prefix to every ordinary unlisted map glyph while leaving
      native collision/update logic authoritative. Any printable source symbol (including
      space and vanilla/multi-cell glyphs) can still be explicitly overridden; safe
      single-cell behavior is selected through `native_glyph` or native solid/pass-
      through/hazard presets. Native `K` remains excluded from generic `native_glyph`, but
      direct map markers now pass a strict per-room 13-object combined K/sword/mine
      reset-spawn audit before their unchecked native allocation path is reachable.
      Deterministic
      add/set/clamped force fields affect players
      and verified type-2 swords in the fixed 16-slot native thing pool in offline,
      local/loopback, GGPO-live, and rollback-replay ticks. Players use center/half-tile-
      foot sampling; swords use their native center-only collision point. Exact-cell
      de-duplication, mirrored force direction, atomic reload, and behavior-
      sensitive fingerprints. External packing removes declared gutters and preflights
      the engine's fixed 8,192-record global sprite capacity before entering its unsafe
      allocator. The selectable demo covers the 128-cell native prefix plus appended
      spring frames, an external animated solid tile, unlisted native `@` terrain,
      scripted spring/fan behavior,
      transform/layer, deliberate fallback, and mirrored rooms; one command tests
      registry validation/identity, inherited defaults, native-prefix bounds/flexible grid, legacy
      force math, draw-base restore, hook integration, parser, and fixture. External PNG
      bytes are always hashed into map/online identity; `asset_sha256` remains only an
      optional pin, not an authoring requirement. Native world-to-cell lookup now matches
      the executable's nonnegative truncate/floor boundaries exactly (the removed
      half-cell bias caused right-edge/early scripted contacts). `native_visual: underlay`
      reproduces the native surface-base composition used by mines without inheriting
      mine behavior, keeping ordinary terrain visually continuous beneath custom props.
    - Deterministic V2 `map.lua` behavior foundation: an optional direct 256 KiB script
      keeps JSON focused on visuals/native collision while providing bounded enter,
      contact, leave, and map-tick callbacks; finite object position/velocity changes;
      mirrored exact-cell metadata; temporary per-cell sprites; fixed typed `map.state`;
      and snapshotted RNG. It runs in a separate memory/instruction-capped sandbox with
      no filesystem/network/OS/module/debug/FFI/JIT/dynamic-code access, rejects bytecode,
      the cross-runtime `^`/pow operator, and unsnapshotted callback state, and faults
      closed. Exact retained source bytes and
      canonical bindings affect the domain-separated 128-bit package signature/server
      map key; activation uses the pinned map
      generation. Managed online prematch additionally requires that exact pinned script
      id to be active and healthy before layout publication/READY; failure aborts with a
      clear hub error and server `match_abort` instead of silently dropping behavior. Its
      pointer-free state, object lifecycle generations, explicit object kinds,
      cell/binding-scoped contacts, temporary velocity limits, overrides, clock, RNG, and
      fault flag are embedded in map-script API/snapshot v5 and rollback blob v9/layout
      schema 6. The real VM test executes the
      demo spring/fan callbacks, mirrored direction, timed sprite, enter/stay/leave, and
      snapshot/lifecycle replay. Strict load-time `map.sensor` declarations now provide
      fixed-point tile boxes, center/native-radius-body/feet/custom object boxes,
      compatible player plus explicit alive-player/dead-body/sword/hazard filters, room
      mirroring, inclusive edges, exact-cell or binding-union contact scope, and stable y/x
      neighborhood dispatch from immutable pre-callback coordinates. The demo spring uses
      its complete width and a six-pixel lower-body/probe band; adjoining spring cells are
      one contact. Its alive-player-only filter deliberately excludes dead bodies so they
      can settle and pass the native respawn gate. Its per-kind `-4.0`/`-2.8` targets
      compensate for living-player versus native-thing gravity to yield approximately
      equal 53-pixel rises, while a kind/lifecycle-keyed temporary `min_vy` limit persists
      for eight rollback-owned ticks and catches native kick velocity added after contact
      has ended. The fan uses its full cell.
      Bindings without sensors retain player center/half-tile-foot and sword
      center probes. Read-only object kind, id, and verified contact radius are exposed;
      verified K type-3 point hazards can opt in, while exact type/updater/lifecycle checks
      prevent stale leaves from writing into recycled or differently typed pool slots.
      `tile:set_sprite` also accepts a strict
      optional render table: fixed 1/256-pixel additive X/Y offsets are applied after
      atlas cropping and expire/reset/rollback atomically with the per-cell sprite. The
      demo's active spring uses a render-only user-tuned offset, leaving its terrain,
      collision, and sensor stationary.
      Raw native callbacks, additional native object types, spawned/moving custom
      entities, weapons, and the broader content API remain open above.
    - General window policy source repair: F1 3:2 presets, F11, maximize normalization,
      display-aware fullscreen cache, safe event-boundary recreation, desktop borderless,
      launch-flag conflict handling, and local-only viewport refresh.
    - Secure opt-in credential implementation: exact server/account Windows Credential
      Manager targets, save only after `auth_ok`, deletion on opt-out/rejection/identity
      change, malformed-record cleanup, and secure memory clearing. A valid remembered
      account now authenticates automatically, hides the login form while pending, and
      opens directly into the hub menus with backend-neutral player-facing copy.
    - Exact native framework cursor integration: the fake built-in cursor and later
      white/custom-scale approximation were removed. Online UI, automatic custom-state
      cursors, and no-option `mod.ui.draw_cursor()` now share the game's actual two-pass
      renderer with native global scale, shadow, pulse color, hotspot, and state restore;
      online still queues it only once/topmost. Live online Options, both remap pages, and
      Mods now preserve both native player-cursor enable flags across the hidden gameplay
      tick and receive a final real native mouse pass, so advancing the peer cannot make
      the menu unusable. Live acceptance remains listed above.
    - Active online connectivity troubleshooter: `online.troubleshoot`/`net.trouble`
      independently tests the configured server's TCP control and UDP rendezvous paths
      without blocking the frame loop, validates the exact UDP source/sequence, reports
      RTT plus the server-observed public endpoint, and inspects active Windows IPv4
      adapters for carefully labeled VPN/NAT/CGNAT evidence. Console-first use loads the
      persisted/default target before probing; Windows route selection identifies the
      actual server interface, while fallback prefers a physical adapter so an unrelated
      active Radmin/VPN interface is reported without being mistaken for Eggnogg's route.
      It explicitly distinguishes
      likely 100.64/10 CGNAT from ordinary private-address NAT that requires comparing the
      router WAN address, then appends the secret-free `net.diag` route/attempt/packet
      snapshot. Native loopback/parser/profile smoke tests, static command/privacy/pump
      coverage, build linkage, console docs, testing instructions, and design spec are
      complete; live network-matrix acceptance remains above.
    - Framework version checking: asynchronous launch check, live Mods-menu
      status/retry/install actions, and a bounded dismissible update notification.
    - Automatic transactional framework updates: default-on, SHA-256/size verification,
      bounded downloads, reserved-namespace path safety, V2 integrity journal,
      complete-set verified roll-forward, conservative rollback/quarantine recovery,
      single-writer locking, and restart-required status. The isolated one-command test
      runs a real loopback apply plus reduced-journal and recovery regressions entirely in
      disposable build directories. The direct-import helper gap remains in Feature
      backlog instead of being described as power-fail-safe.
    - Atomic updater-owned config writes that preserve comments and unrelated settings;
      main-menu mode persistence now uses this API instead of rewriting a fixed buffer.
    - Configurable Lua cursor options for deliberate custom sprite/index, scale, hotspot,
      tint, and layer choices, including per-state automatic/disabled/custom policies.
    - Custom-map water ambience/effects: native water glyphs, per-room water colors,
      bubbles, boil, and fume ambience.

    - Content Workshop atlas preview now imports inherited cell dimensions/padding
      from data.json, supports rectangular cells and gutters, and fixes native glyph
      border/stride cropping. Imports preserve work on failure, support undo/redo,
      and persist settings in drafts. Entity loading rejects ambiguous multi-grid
      PNG references; identical declarations still share a sheet. Asset packaging,
      full-map integration and live visual acceptance remain unfinished.

    - Content Workshop complete-map packaging: open one V2 map folder (including
      nested wrappers), load its entities/Lua and preview PNGs, export a complete
      ZIP preserving map/asset/supplementary file bytes and unchanged Lua. Imports
      are atomic and undoable; references/ranges validate before export. Attached
      files remain session-only with an export reminder. Room-local placement,
      native combat/equipment, atlas authoring and live acceptance remain open.

    - Map API 19 exposes authored regions with local/world bounds and player contact
      radius, with detached values, stable ordering and shared execution accounting.
      Launch-pad entity/Lua example uses authored sensor geometry and atomic native
      player velocity writes. Native adapter and real VM tests cover both players,
      exact budgets, stale handles, host failure and rollback replay. Native combat,
      equipment and live sensor/movement acceptance remain unfinished.

    - Greggnogg integration revision: Objects now edits the current map in a
      dialog. Designs enter Custom tiles, normal map tools place/erase instances,
      room edits retain references, and animated visuals render on the map.
      Removed standalone map selection and preset/onboarding flow. Import/export
      carries object callback editor metadata; mismatched manual Lua edits fail
      explicitly. Native/API and live visual acceptance remain separate gates.

    - Integrated custom-map Preview now uploads PNGs/entities/Lua through a
      token-scoped loopback session and starts a validated local map. Staged
      files retire only after registry/engine release, preserving edited and
      unrelated files. Old-process cache cleanup and live browser/game acceptance
      remain open. Map logic offers Blocks and highlighted Advanced Lua;
      object callbacks also have highlighted editing. Invalid blocks stay visible
      when switching modes instead of silently reverting to earlier Lua.

    - Beginner object blocks include animation pause and horizontal flip. Shared
      generated Lua now runs in native tests with entity updates and exact
      snapshot replay; broader blocks coverage and live editor acceptance remain.

    - Object creation/removal blocks now generate native-tested Lua; creation,
      removal, animation pause and movement have shared browser/native fixtures
      and exact snapshot replay coverage. Object type selection still needs a
      current-map picker, and the complete beginner logic API remains unfinished.

    - Object logic now has Blocks/Advanced per lifecycle event, current-map object
      pickers, preserved callback layouts, and supplied event context. Added repeat,
      periodic condition, deterministic random and extended math blocks; generated
      runtime fixtures cover timing/random/loops with exact rollback replay.
      Blockly popup containers now follow the active modal dialog. Live UI acceptance
      and remaining broader content API work are still open.

    - Object block persistence now has package round-trip and rename/restore
      regressions. Metadata shape/size validation rejects malformed layouts;
      stale metadata preserves newer Lua. Nested unsupported blocks reject during
      generation. Live UI acceptance and broader API requirements remain open.

    - Player observation blocks add active-player iteration, scoped position/speed/
      radius values, and current-player velocity/defeat targets. Shared generated
      Lua tests cover both slots, sparse presence and empty queries with atomic
      native commits. Remaining API and live acceptance work is still open.

    - Player/object overlap block checks authored regions by role with circle/AABB
      geometry. Generated native tests cover response, replay, host rejection,
      boundary contact and non-contact corners. Broader API/live gates remain.

    - Object-group blocks select a design and provide scoped instance controls
      inside map/object events, guarding snapshot handles against removal. Native
      tests verify type filtering and replay. Full content API remains unfinished.

    - Object designer motion controls add opt-in gravity, horizontal drag and
      vertical speed limits, with metadata round-trip and exact native replay tests.
      Both Blockly editors now use dark component colors and disable background
      drag-to-pan. Detection-area roles and lack of automatic native terrain
      collision are explicit. Local Map Lua docs updated; published page returned
      HTTP 403 during verification. Live dropdown behavior remains unverified.

Custom-content follow-up (September 10):
  [x] Shared map/per-player block variables: read, set, change, clear, exists;
      generated Lua verified in native player simulation and rollback replay.
  [x] Put detection controls beside preview; disable shared sprite smoothing.
  [x] Verify dropdown interaction after toolbox drag in live Greggnogg; revised
      pointer-up handling passes regression harness but requires live acceptance.
  [x] Named detection regions available to scripts and blocks (API 30): optional
      per-type names are native/editor validated, exposed by entity.regions,
      selectable in current-object touching blocks, and fingerprint-authoritative.
  [x] Solid custom regions with native player/physics response and rollback tests (API 25).
  [x] Double-jump authoring (API 31): verified input/ground state, render-only
      visibility, indicator attachment, collectible recharge, editable example map
      and beginner block workflow; native edge timing and rollback replay are tested.

Custom-content authoring follow-up:
  [x] Variable creation dialog and name dropdowns; serialized name catalog.
  [x] Per-instance variables with removal cleanup and rollback replay (API 24).
  [x] Explicit movement default for new designs; velocity integration opt-in.
  [x] Verify dropdowns after toolbox drag in live UI; restore workspace and
      popup focus roots before opening (automated interaction harness passes).
  [x] General coordinate query matching exact authored tiles or custom objects,
      composable through ordinary equality/OR rather than patrol-specific commands.
  [x] Solid custom hitboxes with native player/physics-object collision response (API 25).
  [x] Expand per-object state capacity beyond shared 64 entries / 12-character keys:
      API 29 provides 512 handle-owned scalar entries with 1-32 byte names,
      deterministic key listing, post-callback removal cleanup, combined-snapshot
      validation and exact replay without enlarging supported online rollback packets.

  [x] API 25 solid-region purpose and editor checkbox: swept native player,
      corpse, sword and hazard blocking; grounded flags, snapshot restoration,
      native wall/floor/ceiling/diagonal/penetration tests.
  Live solid-surface and moving-platform acceptance is tracked in the active
  backlog and TESTING.md.

  [x] API 26 entity.at(x,y), bounded stable point query with mirrored region
      geometry, detached handles and half-open edges; reusable object-at block
      composes through OR and tracks renamed types. Native/runtime tests pass.
  [x] Complete coordinate detector with exact terrain-character lookup (API 28):
      map.tile_at reports exact native/custom source symbols across mirrored rooms;
      one current-map picker block composes tile and object matches through OR.

  [x] API 27 custom physics checks custom Solid regions as well as terrain,
      excludes its own handle, and replays wall/floor stopping deterministically.
      Preserve old generated physics during import until editor upgrade.
