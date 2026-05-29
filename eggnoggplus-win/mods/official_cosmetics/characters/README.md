# Custom Characters

Use the `IMPORT ZIP` or `IMPORT FOLDER` buttons in the cosmetics menu. Each package needs a `character.json` file and a PNG spritesheet. Characters can be selected independently for player 1, player 2, or your online profile.

Online matches send the selected online profile's sheet to the other client at match startup. The receiver verifies the sheet SHA-256 before caching and loading it.

`Example Knight` is bundled in the menu. `example_character_package.zip` is the same kind of package the import button expects.

Example `character.json`:

```json
{
  "id": "my_character",
  "name": "My Character",
  "sheet": "my_character.png",
  "cell_w": 64,
  "cell_h": 64,
  "target_w": 16,
  "target_h": 16,
  "fps": 12,
  "animations": {
    "idle": { "frames": [0, 1, 2, 3, 4, 5], "fps": 10 },
    "run": { "frames": [6, 7, 8, 9, 10, 11], "fps": 14 },
    "jump": { "frames": [12], "fps": 1 },
    "fall": { "frames": [13], "fps": 1 },
    "duck": { "frames": [14], "fps": 1 },
    "prone": { "frames": [15], "fps": 1 },
    "stun": { "frames": [16, 17], "fps": 8 },
    "dead": { "frames": { "choose": [18, 19, 20] }, "fps": 1 },
    "eggnogg": { "frames": [19, 20, 21, 22], "fps": 8 }
  },
  "frame_map": {
    "35": "stun",
    "36": "stun"
  },
  "hat_anchor": {
    "x": 0,
    "y": 0,
    "idle": { "bob_x": 0, "bob_y": 0 },
    "run": { "bob_y": 0.35, "fps": 16 },
    "duck": { "y": 4 },
    "prone": { "y": 7 }
  },
  "sword_anchor": {
    "x": 0,
    "y": 0,
    "idle": { "bob_x": 0, "bob_y": 0 }
  }
}
```

Fields:

- For bundled `manifest.json` entries, `sheet` is relative to this mod folder.
- For import packages, `sheet` is relative to the folder containing `character.json`.
- `cell_w` and `cell_h` are the source frame size in the PNG.
- `target_w` and `target_h` are the in-game size. Keep them at `16` by default so the art stays aligned to the normal hitbox.
- `animations` can use any number of frames. A 30-frame idle animation is valid.
- `frames` can also use a random choice object. Use `"frames": { "choose": [18, 19, 20] }` to pick one still frame, or `"frames": { "choose": [[18, 19], [20, 21, 22]] }` to pick one random animation group.
- `frame_map` is optional. It maps vanilla sprite frame numbers to one of your custom animation names for special poses.
- `hat_anchor` is optional. Custom characters default to a still hat anchor instead of vanilla idle bob. Use `x`/`y` for base adjustment, and per-animation entries like `idle`, `run`, `duck`, or `prone` for pose offsets or `bob_x`/`bob_y` motion.
- `sword_anchor` is optional and uses the same shape as `hat_anchor`. Custom characters default to no vanilla sword idle sway; use this to add character-specific sword bob if the art needs it.
- Custom sheets are capped at 1 MiB for online transfer.
- Frames draw as the visible replacement layer over the normal player, so keep the body area opaque wherever the vanilla body should be covered.
