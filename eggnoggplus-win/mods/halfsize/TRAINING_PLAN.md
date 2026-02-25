# Halfsize VS-AI Training Plan (PyTorch + CUDA)

This plan builds a **competent model without running Torch inside the game**.
The game/mod remains a lightweight inference host; training happens offline with PyTorch.

## 1) Goals

- Learn from your real play style by fighting you.
- Stop unsafe controls (e.g. START/PAUSE) during gameplay.
- Produce an exported model that can be embedded as Lua weights + tiny forward pass.

## 2) Action space (safe by design)

Only these action bits are legal for the model:

- `JUMP`
- `LEFT_A`, `RIGHT_A`, `LEFT_B`, `RIGHT_B`
- `ATTACK`

`START` is explicitly forbidden at both train-time and inference-time.

## 3) Observation vector (per frame)

Start with compact, stable inputs from `mod.game.snapshot`:

- player velocity: `player_vx`, `player_vy`
- enemy relative position/velocity: `enemy_dx`, `enemy_dy`, `enemy_vx`, `enemy_vy`
- booleans: `player_has_sword`, `enemy_has_sword`
- nearest sword relative: `nearest_sword_dx`, `nearest_sword_dy` (or sentinel values)
- small history stack (last N=4 observations)

Optional later:
- room index one-hot or embedding
- reduced tile context around player (small local patch)

## 4) Data collection against human

During VS AI matches, log tuples each frame:

- `obs_t`
- human action mask (for your side)
- AI action mask (for current bot)
- reward components (if available)
- done flag / round result

Recommended format: line-delimited JSON (`.jsonl`) so sessions can be appended safely.

## 5) Two-stage training recipe

### Stage A: Behavior cloning warm-start

- Train model to imitate **your human action mask** from observations.
- Loss: multi-label BCE over action bits.
- This gives stable, human-like priors quickly.

### Stage B: RL fine-tuning (self-play + human replay mix)

- Initialize from BC weights.
- Use PPO or IMPALA-style actor-critic in PyTorch.
- Keep replay mix, e.g.:
  - 60% self-play rollouts
  - 40% sampled human demos
- Add entropy early, decay over time.

## 6) Reward design (dense + sparse)

- `+` enemy hit / sword possession progress
- `-` getting hit / disarmed / corner trapped
- `+` win round (sparse terminal)
- tiny shaping for approaching sword when unarmed
- zero reward for START (should never be in action space)

## 7) Model architecture (CUDA friendly)

- MLP baseline: `[obs_dim] -> 256 -> 256 -> heads`
- Heads:
  - policy logits for each action bit (multi-binary)
  - value head (scalar)

Why MLP first: easy export to Lua and fast iteration.

## 8) Export path to Lua runtime

After training:

1. Save PyTorch checkpoint.
2. Export only inference tensors (weights/biases) to JSON.
3. Convert JSON to Lua table file.
4. In mod, run tiny forward pass (matmul + tanh/relu + sigmoid).
5. Threshold logits to action bits, then apply safety mask.

## 9) Anti-pause / control safety gates

At inference-time in mod:

- mask output with allowed gameplay bits only
- run overrides only in gameplay state (`state_name == "game"`)
- clear overrides on menu/pause/results transitions

## 10) Milestones

1. **M1**: Add robust logging + safety masks.
2. **M2**: Train BC model on your play data (GPU).
3. **M3**: Add PPO fine-tuning with self-play.
4. **M4**: Export-to-Lua inference and A/B against heuristic bot.
5. **M5**: Iterate with hard negative scenarios you report.

## 11) Practical training settings (starting point)

- Batch size: 4096 frames (BC), 131072 rollout steps (RL updates)
- Optimizer: AdamW
- LR: `3e-4` then cosine decay
- Gradient clip: `1.0`
- Mixed precision: enabled (CUDA)
- Eval every N updates with fixed seeds + recorded human challenge set

## 12) What "competent" means (acceptance)

- Beats current heuristic bot >80% in mirror test.
- No START/PAUSE actions in 100% of eval frames.
- Against you, visibly better spacing and attack timing over baseline.
