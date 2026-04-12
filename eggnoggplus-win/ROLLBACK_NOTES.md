# Eggnogg+ Online rollback refactor notes

## What changed

The old online sync path was an input relay plus periodic positional/auth snapshots:

- sample local input
- send it every frame
- inject the latest remote input into the other player slot
- periodically send snapshots and apply them as delayed corrections

That model is not true rollback netcode. It can feel okay at low ping, but it degrades into delayed or rubber-banded play when inputs arrive late.

The new sync path in `mods/online/lib/sync.lua` is built around rollback + prediction:

- both local and remote player slots are driven by scheduled deterministic inputs
- raw native input is blocked for both players during online matches
- every gameplay frame stores a full **pre-tick** snapshot in a rolling history buffer
- missing remote input is predicted from the last resolved remote command
- when the real remote input arrives for a frame we already simulated, the code compares it against the prediction
- on mismatch, the game restores the saved pre-tick snapshot for that frame and re-simulates forward with the corrected input history

## Interpolation / correction strategy

With the APIs exposed by the framework, there is not a separate render-only transform layer for players/entities, so pure “visual-only interpolation” is limited.

What is implemented instead:

- authority sends an occasional correction snapshot for a historical pre-tick frame
- the non-authority peer only uses that as a desync repair anchor, not as the primary sync model
- if a correction arrives outside the rollback window, it is applied directly but the code copies current positions into `prev_x/prev_y` where possible to reduce the visible pop

So the match is rollback/prediction first, with interpolated emergency correction as a fallback.

## Files changed

- `mods/online/lib/sync.lua`
  - rewritten from snapshot-relay sync into rollback/prediction sync
- `mods/online/main.lua`
  - handles rollback failure cleanly and updates HUD output
- `mods/online/lib/proto.lua`
  - logs correction snapshots more clearly
- `mods/online/config.cfg`
  - exposes rollback tuning knobs

## New config knobs

- `rollback_history_frames`
  - how many pre-tick snapshots / input frames to keep in memory
- `correction_interval`
  - how often authority sends a historical correction snapshot
  - set to `0` to disable correction snapshots entirely

## Practical caveats

- I validated Lua syntax statically, but I could not run the Windows game binary in this environment, so runtime tuning still needs in-game testing.
- If the underlying simulation has hidden nondeterminism outside the captured snapshot state, you may still see correction rollbacks. The periodic authority correction is there to keep those from drifting forever.
- If rollback frequency is too high, try:
  - lowering latency / packet delay on the relay server
  - increasing `rollback_history_frames`
  - increasing `correction_interval` so corrections are rarer

## Suggested first test plan

1. Run two local clients against the same server.
2. Confirm both players still move and attack correctly at low latency.
3. Add artificial latency / jitter.
4. Watch HUD counters:
   - `rb` = rollbacks
   - `miss` = prediction misses
   - `corr` = authority corrections sent/received/applied
   - `rsim` = total re-simulated frames
5. Check for desync symptoms around:
   - sword pickup / throw
   - simultaneous jumps / attacks
   - countdown / round reset transitions
   - room transitions or any map-specific entities
