# AI Opponent / Singleplayer Mode — Design

**Date:** 2026-07-01
**Status:** Approved by user (sections 1–2 approved as-is; section 3 revised to ML-based per user request, revision approved)

## Goal

Let one person play EGGNOGG+ locally against a learned AI opponent. The AI is a real
ML policy (trained neural network), not a scripted state machine. Entry point is a
mode-cycling PLAY button on the main menu. The mod ships pre-trained and can keep
training in-engine.

## Non-goals

- No AI in online/GGPO matches (bot APIs are gated offline; menu path must never arm
  the flag for online play).
- No gradient-based RL stack (PPO/DQN); training is neuroevolution by design choice.
- No changes to native gameplay logic, netcode, or sync-sensitive code paths
  (hard constraint — see revert of 2026-06-18; this feature must be purely additive).
- No AI participation in map selection (human picks the map).

## Architecture

Two components with a deliberately thin interface:

1. **C framework bridge** (hooks.c + lua_manager.c): main-menu mode selector and an
   "AI match" flag exposed to Lua. Contains zero bot logic.
2. **Lua mod `mods/ai_opponent/`**: all ML — policy inference, feature extraction,
   action mapping, training, persistence. Hot-reloadable; tunable without rebuilding
   the DLL.

Interface between them (new `mod.game` bindings):
- `mod.game.ai_match() -> { active = bool, ai_player = 0|1, training = bool } | nil`
  — state of the flag armed by the menu. Cleared when returning to menu.
- C-side query `lua_manager_has_bot_provider()` — true when an enabled mod has called
  `mod.game.register_bot_provider()`. Gates whether VS AI / TRAIN modes appear in the
  menu cycle.

## Component 1: Mode-cycling PLAY button (C)

Replaces the current START + injected ONLINE button pair with a single button plus
two custom-drawn triangle arrows above/below it (selector glyphs, not button boxes).

- **Modes:** PLAY → ONLINE → VS AI → TRAIN (cycle wraps). VS AI and TRAIN are skipped
  when no bot provider mod is enabled.
- **Visuals:** button text and accent color change per mode (PLAY: native styling;
  ONLINE: existing online accent; VS AI / TRAIN: distinct accents + small glyph).
  Arrows are drawn in the same UI style as other framework overlays.
- **Input:** arrows are keyboard-navigable (up/down from the button focuses an arrow,
  activate = cycle) and mouse-clickable. This reuses the native button/filter
  machinery (`btn_player_filter` proxy pattern already used by ONLINE/MODS entries).
- **Activation behavior:**
  - PLAY → native map select (unchanged native flow).
  - ONLINE → online hub (existing behavior, relocated onto this button).
  - VS AI → native map select with the AI flag armed. The activating player is
    detected via the existing filter-tag mechanism (0x11 = P1 selector,
    0x12 = P2 selector); the AI takes the **opposite** player. Mouse activation
    defaults to human = P1, AI = P2.
  - TRAIN → native map select with the AI flag armed in training mode (both players
    AI-driven, accelerated).
- **Flag lifecycle:** armed at activation, consumed by the Lua mod during the match,
  cleared by the C side when the game state returns to menu. Never armed when an
  online session is active or pending.
- **Persistence:** last selected mode saved in `modframework.cfg`.

## Component 2: Bot runtime (Lua mod `ai_opponent`)

Runs in `mod.on_tick`. When `ai_match().active` and gameplay is running, it computes
one action per tick for the AI player and injects it via
`mod.game.set_input(ai_player, mask, 1, true)`. Inert otherwise; clears overrides on
match end, mod unload, and hot reload.

### Policy

- Small MLP in pure Lua (LuaJIT-friendly flat arrays): ~30 input features → 2 hidden
  layers (~32, ~16, tanh) → logits over ~12 discrete actions. Deterministic argmax at
  play time (plus difficulty noise, below).
- **Hybrid scaffold:** a thin scripted layer that (a) detects degenerate states —
  stuck against geometry for N ticks, respawn walk-in, off-screen recovery — and
  issues recovery moves, and (b) maps the chosen discrete action to command bits.
  All tactical/strategic decisions come from the network.

### Observations (normalized floats)

- Relative opponent position (dx, dy) and velocity; own velocity.
- Own/opponent: facing, has_sword, state one-hots (attacking, dying/respawning,
  stunned), grounded, wall-left/right flags.
- Goal direction for the AI player, leader status, room-progress signal.
- Distance/direction to nearest loose sword (from `entities()`).
- Short-range tile probes via world/tile queries: solid/hazard ahead (2 ranges),
  below front foot, at head height, along jump arc — enough to learn platforming
  and spike/lava avoidance without global map input.

### Actions (~12 discrete)

idle, run L, run R, jump neutral/L/R, attack, attack+forward, crouch (down),
stance up, stance down, down+forward (divekick-style), throw-style attack
(exact list finalized against the game's real move set during implementation).
Each maps to a cmd bitmask (JUMP 0x01, ATTACK 0x02, RIGHT 0x04, LEFT 0x08,
UP 0x10, DOWN 0x20).

### Difficulty (Options → Mods, `config.cfg`)

`difficulty: options[easy, normal, hard], normal`

- Each level binds to a different training checkpoint (early/mid/late generation).
- Plus an inference-time humanizer scaled per level: reaction latency (policy sees
  observations delayed by k ticks), small epsilon of action noise, and a per-level
  cap on consecutive attack actions. Hard ≈ latest checkpoint, minimal humanizer.

## Component 3: Training (self-play neuroevolution)

- **Algorithm:** population-based neuroevolution (ES/GA hybrid): population 32–64
  genomes (weight vectors), elitism, Gaussian weight mutation, optional uniform
  crossover. Chosen over gradient RL because it handles sparse rewards robustly, is
  simple to implement correctly in pure Lua, and generations naturally provide
  difficulty checkpoints.
- **Self-play evaluation:** each genome plays short matches vs. sampled opponents
  (current population members + past checkpoints) — both players driven by policies.
- **Acceleration:** TRAIN mode starts a local match, then the mod drives episodes
  headless: per sim tick, set both players' inputs, `simulate_ticks(1)`; episode
  reset via `full_state_blob()` / `apply_full_state_blob()` captured at match start.
  Many sim ticks per rendered frame (budgeted per frame to keep the UI responsive).
- **Fitness shaping:** + hits/kills, + disarms, + leader time, + room progress toward
  goal, − deaths, − stalling (no-progress time). Exact weights tuned during training.
- **TRAIN mode overlay** (`mod.ui` in `on_frame`): generation, best/mean fitness,
  sim-speed multiplier, current matchup; minimal controls (pause training, save
  checkpoint, exit note). Exiting the match stops training cleanly.
- **Learning from the human:** VS AI matches record per-tick experience
  (observations, actions, outcomes) to storage (ring buffer, size-capped). Training
  mode uses recorded human matches as additional evaluation episodes (fitness vs.
  human trajectories), biasing selection toward policies that handle the user's
  habits. (Explicitly modest: no online adaptation mid-match.)
- **Shipping:** the mod ships with three trained checkpoints (easy/normal/hard)
  produced during development.

## Persistence

- **Weights/checkpoints:** mod storage (`storage.cfg` key/value) — one key per
  checkpoint, base64-encoded float32 weight blob, chunked across keys if a size
  limit is found. Schema versioned via `storage.set_schema`.
- **Config:** `config.cfg` (difficulty, training budget per frame, overlay on/off).
- **Shipped defaults:** pre-trained checkpoints included as data file(s) in the mod
  folder, imported into storage on first load.

## Error handling

- Mod disabled/missing → VS AI / TRAIN modes absent from the menu cycle; PLAY and
  ONLINE unaffected.
- Flag armed but mod errors at match start → AI player simply receives no input;
  mod logs the error; match remains playable/quittable. No crash path in C.
- Hot reload mid-match → `on_unload` clears input overrides; reloaded mod re-reads
  `ai_match()` and resumes if still active.
- Online session active or pending → menu never arms the flag; bot APIs remain
  gated by the framework as today.

## Known risks (verify first during implementation)

1. **Input injection under `simulate_ticks`:** docs state it does not re-enter
   `on_tick`; training relies on set-inputs → `simulate_ticks(1)` ordering applying
   inputs for that tick. Verify with a movement probe before building training.
2. **Storage size limits** for weight blobs (fallback: chunked keys or a data file
   in the mod folder loaded at startup).
3. **Menu button rework** touches working menu code (START/ONLINE layout dance at
   `add_online_button_to_main`); needs careful regression of PLAY and ONLINE paths,
   keyboard + mouse, both player selectors, and window-resize re-layout.
4. **Self-play compute:** if in-engine sim throughput is too low for good policies
   within reasonable wall time, reduce scope: smaller net, curriculum (spawn players
   in one room), longer overnight TRAIN runs. Difficulty tiers still work with a
   modestly-skilled hard checkpoint.

## Testing

- **C bridge:** manual matrix — cycle modes (keyboard arrows, mouse), activate each
  mode, VS AI from P1 controls / P2 controls / mouse, resize re-layout, modes hidden
  when mod disabled, ONLINE unchanged, no flag during online session.
- **Lua unit-ish tests (in-mod, console-triggered):** feature extractor bounds,
  action→mask mapping, network forward pass determinism, storage round-trip of
  weights.
- **Training smoke test:** fitness strictly improves over N generations from random
  init in a fixed-seed scenario; checkpoint save/load reproduces behavior.
- **Play verification:** full VS AI match start-to-victory on each difficulty;
  AI respawns, picks up swords, crosses rooms, doesn't soft-lock; human can play as
  either P1 or P2.
