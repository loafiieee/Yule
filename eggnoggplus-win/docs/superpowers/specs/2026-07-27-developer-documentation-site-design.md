# Developer documentation site

## Purpose

Provide a hostable reference for people building mods, maps, integrations, and server
deployments with Eggnogg+. The site is documentation, not a feature showcase. It uses
neutral technical language and prioritizes contracts, limits, examples, and failure modes.

## Delivery contract

`docs-site/` is a dependency-free static site. `index.html` is the entry point; every page
uses the checked-in HTML, CSS, and JavaScript, with optional Google-hosted copies of the
same Press Start 2P and Space Grotesk families used by the public Yule page. Complete
system/monospace fallbacks preserve layout and function when fonts are unavailable. The
directory can be copied directly to an ordinary static host without a build step. Local
preview instructions live in `docs-site/README.md`.

The third-generation layout is a documentation product rather than a feature showcase or
a set of generically styled articles:

- a persistent task-oriented global navigation separates Start, Learn, Framework API,
  Reference, and Maintain;
- the homepage opens with a compact task-oriented reference header, coverage terminal,
  and numbered work routes rather than marketing claims;
- each page gets a generated local table of contents;
- search indexes both pages and individual API members, returning stable deep links;
- the API is separated by working area and namespace instead of compressed into one Lua
  summary page; and
- every callable renders as its own contract record containing a signature, parameters,
  return/failure behavior, constraints, tags, stable anchor, and runnable example.

`api-data.js` is the canonical presentation data for callable contracts. `app.js` renders
the shared shell, search, responsive navigation, local contents, API filters, and contract
records without a framework or build step. Semantic guide/reference prose remains in its
own HTML.

## Yule visual language

The site follows the public Yule page's actual design system while preserving the density
required by a developer reference:

- near-black burgundy backgrounds (`#0e0a0b`, `#150e10`) and dark red-brown panels;
- ember red (`#ff5340`) as the sole primary accent, with cream text and muted rose-gray
  secondary text—not the previous blue/gold or generic teal documentation palette;
- Press Start 2P only for short labels, buttons, and product marks, with Space Grotesk for
  readable prose and API contracts;
- square two-pixel black borders, subtle inset highlights, console-like code surfaces,
  and no rounded SaaS-dashboard cards;
- a compact product utility bar, directory-like left navigation, reading column, and
  optional local contents rail; and
- responsive drawer navigation that keeps search and the API index available on narrow
  screens.

Brand treatment remains subordinate to the content. There is no screenshot hero,
feature pitch, or promotional copy in the documentation.

The site includes:

- installation, first launch, configuration, console use, and focused troubleshooting;
- framework architecture, source layout, building, testing, and release workflow;
- every public ordinary-mod Lua registration, global/mod-table alias, internal exported
  compatibility bridge, higher-level UI helper, animation function/instance method, and
  public metadata/constant surface;
- the transactional content registry and complete map-local Lua callback/object/tile API;
- V1 and V2 map schemas, glyphs, per-map tilesets, custom tiles, limits, and diagnostics;
- audio, native tracks, bytebeat/floatbeat, and Dollchan-compatible track authoring;
- online client behavior, exact protocol/build admission, server deployment, Discord LFG,
  deep links, updater publishing/recovery, and security boundaries.

Installation and troubleshooting remain small supporting sections. Navigation and search
are organized around developer tasks and reference material.

## Compatibility documentation

Online pages must distinguish the informational release label from enforceable
compatibility. The current client requires `cap_client_build_gate: 1`; authenticated
manifests send control/match/P2P versions plus deterministic `build_id`, `game_exe_id`,
and `framework_dll_id` fingerprints. Matchmaking requires exact protocol and build tuples.
P2P v16 and v17 are intentionally wire-incompatible.

## Accessibility and validation

Each page has one title, one primary heading, one main landmark, keyboard-reachable
navigation, responsive tables, the Yule dark presentation, and reduced-motion handling. Search,
namespace filtering, deep links, and copy controls work without external libraries.
Automated static review must report:

- no broken local links or fragments;
- no duplicate IDs or missing structural landmarks;
- no pages missing from navigation/search;
- valid JavaScript syntax;
- no unfinished placeholder copy;
- the Yule burgundy/ember tokens, typography roles, task-first home markers, and absence
  of the retired blue/gold/theme-switch shell;
- every source-registered ordinary-mod, content-transaction, higher-level UI/animation,
  and map-script callable has a detailed data record; and
- every data record contains a signature, description, return contract, and example.
