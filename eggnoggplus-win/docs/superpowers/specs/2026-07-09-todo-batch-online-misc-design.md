# TODO Batch: Online Reliability + Misc Fixes — Design

**Date:** 2026-07-09
**Status:** Approved (user approved with two revisions: -borderless = borderless fullscreen;
mod online-safety is classified automatically by the framework, not self-declared)
**Scope:** Six items from TODO.txt — P2P connect retry, auto-disable gameplay mods online,
prematch work during countdown, launch version check + update button + auto-update,
-borderless launch arg, online gameplay viewport-shift bug.

## Global constraint

Per the 2026-06-18 revert: **no changes to in-match netcode semantics.** Input delay stays
one-shot, stalls must guarantee progress, no timesync changes. Everything here touches only
the pre-match phase, mod policy enforcement, or non-gameplay code.

## 1. P2P connect retry

Retries are orchestrated in the hooks.c online layer; `ggpo_net` stays a dumb
single-attempt session.

- Verified in `online_server/server.js`: the server re-relays `p2p_peer` whenever a
  client's UDP probe arrives from a new endpoint (`p2p_notified` keys on the endpoint),
  so a fresh socket automatically propagates to the peer. **No server changes.**
- Connect phase in hooks.c: after P2P starts, if `ggpo_net` is not `connected` within
  ~8 seconds, tear the session down, restart with `local_port = 0` (fresh ephemeral port
  → fresh NAT mapping), re-probe the server, punch again.
- Up to **3 attempts total**. Hub status shows "Connecting… (attempt N/3)".
- After the final failure: existing failure path (status + return to hub), plus the
  `net.diag` verdict is logged automatically.

## 2. Auto-disable gameplay mods during online play

**No manifest flag.** The framework classifies mods automatically by which API functions
they actually use, and enforces at runtime.

- **API audit:** every Lua binding exposed to mods (lua_manager.c) is tagged either
  MUTATING (writes gameplay state: player/thing/tile setters, RNG seed, input injection,
  spawn, timescale, etc.) or SAFE (read-only getters, drawing, UI, config, storage,
  audio, logging).
- **Registration-based classification:** registering as a bot provider or registering an
  input-override hook marks the mod gameplay-affecting immediately.
- **Usage-based classification:** the first call to any MUTATING binding (online or not)
  sets a sticky per-mod `uses_gameplay_api` for the session.
- **At online match start:** every mod classified gameplay-affecting is **suspended** —
  all its Lua dispatch (ticks, hooks, input, draws, console effects) is skipped.
- **During the match:** if a still-active mod makes its first MUTATING call mid-match,
  the call is **blocked before it mutates anything** (no-op + log), and the mod is
  suspended on the spot. Determinism holds because the mutation never lands.
- Suspension is **in-memory only**: nothing written to `modframework.cfg`, no
  `on_unload`/`on_load` side effects, automatic restore at match end/abort. A crash
  mid-match cannot leave mods disabled.
- Mods menu: suspended mods shown greyed with an "(online)" tag; enable/disable toggles
  locked during a match.
- `_official_cosmetics` needs no special-casing — it stays active only because it calls
  no MUTATING functions, same rule as everyone.

## 3. Prematch work during the countdown

Start the punch at match-assignment time; gate gameplay start behind entering game state.

- Today: 2.5 s countdown → menu auto-navigation → game state → then socket/probe/punch.
- Change: `online_server_begin_pending_match` starts the P2P session (socket, server
  probe, candidate exchange, HELLO punch) immediately, so the punch runs during the
  countdown. Retry attempts (#1) also fit inside the countdown window.
- New **hold gate** in ggpo_net: the session connects but does not begin state sync or
  frame exchange until released. The launch pump releases it when the client reaches
  game state. The deterministic start path (seed, map selector) is unchanged.

## 4. Version check + update button + auto-update

Adopt the installer's release channel contract in-game
(`https://loafiieee.com/yule/releases/latest.json`, sha256 per file — see
2026-07-02-install-script-design.md); stage-and-swap updates.

- Add a `FRAMEWORK_VERSION` constant in code — single source of truth (channel currently
  says `1.0`; code has no version today).
- On launch: async fetch of `latest.json` (winhttp already linked). Version mismatch →
  small main-menu notification + **Update button in the mods config menu**.
- Update: download to a staging dir, verify sha256 (retry once, per the installer
  contract). Apply by **rename-swap**: rename loaded `SDL2.dll` → `.old` (legal on
  Windows), move staged files in, delete `.old` files on next boot.
- Update button applies immediately and prompts for restart. **Auto-update default ON**
  (cfg toggle shown next to the Update button): applies silently at launch so the next
  launch is current.
- Offline/fetch failure: silent no-op (log only), never blocks launch.

## 5. `-borderless` launch arg

Never existed anywhere (exe or framework) — this is an add, not a fix. Follows the
existing `-fullscreen`/`-windowed` pattern in hooks.c (`hooks_apply_window_launch_args`):

- After the window settles: **borderless fullscreen** — remove the border, resize to the
  desktop bounds at position (0,0), and route the resize through the game's own path so
  the GL viewport/letterbox is correct.
- Mutually exclusive with `-fullscreen` (exclusive fullscreen wins if both are passed).
- An in-game borderless toggle is out of scope for this batch.

## 6. Online gameplay viewport shift

Intermittent, no solid repro ("viewport looks wrong, gameplay only" — user report).
Spec'd as **diagnosis-first**, not a predetermined fix:

- Instrument: log window size, GL viewport, and letterbox params at online-gameplay
  entry and on every change during a match; compare the online entry path (programmatic
  button navigation) against normal play.
- Prime suspects: auto-navigation skipping a viewport recalc that a real click triggers;
  a window-size event landing during connect/state-sync being dropped.
- Deliverable: root-cause fix, plus a cheap defensive guard (re-apply the game's own
  window-size routine when online gameplay begins) if the root cause is timing-dependent.

## Implementation order

5 → 4 → 2 → 1 → 3 → 6. Quick wins first; 1 and 3 land together in the connect phase;
the unbounded-diagnosis item last.

## Testing

- Build with the standard mingw32 command (ONLINE_MULTIPLAYER.md §Build Command);
  `build/SDL2_test.dll` + `SDL2.dll`.
- 1/3: two-instance localhost match (F6/F7 harness + hub flow); kill one candidate path
  to force a retry; verify attempt counter, fresh port per attempt, server re-relay,
  countdown-overlapped connect, and that state sync still starts only in game state.
- 2: run with ai_opponent + speedhack + _official_cosmetics enabled; enter online match;
  verify the first two suspend, cosmetics stay, mid-match MUTATING call from a test mod
  is blocked + suspends it, everything restores after the match, cfg untouched.
- 4: point the channel URL at a local server with a bumped latest.json; verify
  notification, button update, auto-update swap on next launch, sha256-mismatch
  rejection, offline no-op.
- 5: launch with -borderless; verify desktop-sized borderless window, correct viewport,
  precedence vs -fullscreen.
- 6: reproduce with instrumentation enabled; fix per findings.
