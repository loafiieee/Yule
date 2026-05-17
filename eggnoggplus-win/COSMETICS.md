# Cosmetics And Character Customization Plan

This document scopes the cosmetics/customization system for EGGNOGG+ based on the current mod framework and the Ghidra decompilation in `ghidra/`.

The big goal is: cosmetics should be expressive and visible to both clients, but they must never change gameplay, rollback, hitboxes, state checks, or match determinism.

## Goals

- Add player customization for offline and online play.
- Allow each player to use their own character look instead of both players sharing the same `data/sprites.png` sheet.
- Play nicely with existing texture-pack mods such as `texture_pack_test`.
- Support strict custom spritesheets first, with optional richer animation support later.
- Add many more skin/clothing color options.
- Add animated color effects such as RGB shift, gradients, pulses, metallic looks, etc.
- Add cosmetic categories:
  - Hats
  - Masks
  - Torso items
  - Pants
  - Shoes
- Add a hanger icon button from the main menu that opens a dedicated customization screen.
- Show an enlarged live preview of the character in the customization screen.
- Let users filter cosmetics by category and remove equipped items with an `X` option in each category.
- Make cosmetics look alive where possible, for example hats rotating slightly around the head with drag instead of being glued statically to the sprite.
- Download official cosmetic assets from the server with checksums.
- Verify official cosmetic asset checksums on launch.
- Retry failed official asset downloads up to 3 times before falling back.
- Keep all normal cosmetics available immediately, with no unlock grind.
- Later: allow a temporary ranked crown cosmetic for the current highest-ELO player.
- Later: tie into account profiles, queues, friend challenges, rematches, and leaderboards.

## Non-Goals

- Cosmetics must not change collision, hurtboxes, hitboxes, movement, sword reach, mine interactions, win detection, or any gameplay logic.
- Cosmetics should not be part of rollback state hashes.
- Cosmetic animation should not drive simulation state.
- Official cosmetics should not be moddable by replacing local PNG files.
- Public/ranked custom uploads should not be trusted just because a user has a PNG locally.
- The cosmetics system should not take ownership of `data/sprites.png` through the texture replacement API for normal official cosmetics.

## Current Engine Findings

### Player Sprites

The original game is built around one global player spritesheet.

Relevant decompiled code:

- `ghidra/eggnoggplus.exe.c:25651` in `load_gfx`
- `ghidra/eggnoggplus.exe.c:18001` in `draw_player_body`
- `ghidra/eggnoggplus.exe.c:18064` in `draw_player_swordfight`
- `ghidra/eggnoggplus.exe.c:18148` in `draw_player_run`
- `texture_ext.c` for the current texture replacement system

`load_gfx` does this:

- Creates atlas 0 as `0x200 x 0x200`.
- Loads `data/sprites.png`.
- Doubles the spritesheet height in memory.
- Scans the original half for the key color `128,128,128`.
- Writes matching pixels as white into the generated second half.
- Loads the doubled image as 16x16 sprites.
- Stores the first player sprite pointer in `_sprites`.
- Stores the total player sprite count in `_sprites_count`.

This means the current player sheet is effectively:

- First half: base sprite art.
- Second half: generated clothing/recolor mask layer.
- Every cell: fixed 16x16.
- Frame order: fixed by the original game.

`draw_player_body` draws two layers:

- Base body: `*(int *)(player + 4) * 0x1c + _sprites`
- Recolor mask: `(_sprites_count / 2 + *(int *)(player + 4)) * 0x1c + _sprites`

The tint used for the mask comes from the player object fields at offsets:

- `player + 0xe8`
- `player + 0xec`
- `player + 0xf0`
- `player + 0xf4`

Facing/flip comes from `player + 0x98`.

### Existing Texture Pack System

The current texture replacement path in `texture_ext.c` replaces whole target PNGs as they are loaded. It can replace:

- `data/sprites.png`
- `data/tiles.png`
- `data/misc.png`
- `data/glow.png`

The test mod in `mods/texture_pack_test/main.lua` registers `assets/sprites_test.png`.

This is useful for texture packs, but it is not enough for per-player customization because replacing `data/sprites.png` changes the shared global player atlas for everyone.

Important compatibility detail:

- Texture replacements happen before `load_gfx` generates the second-half recolor mask.
- `texture_ext_on_rgba_load` only applies a replacement when the replacement image dimensions match the original loaded image.
- If `texture_pack_test` replaces `data/sprites.png`, that replacement becomes the effective shared base spritesheet.
- The cosmetics system should treat that effective sheet as the `default` skin.
- Cosmetic hats/masks/etc should load through their own cosmetics asset path, not by registering another replacement for `data/sprites.png`.
- Per-player custom skins should override only the player being rendered. They should not replace the shared atlas for the other player.
- If no per-player skin is selected, the player should render with whatever base spritesheet the active texture pack produced.
- If a texture pack changes `data/misc.png`, `data/tiles.png`, or `data/glow.png`, cosmetics should not assume vanilla art in those sheets unless a specific cosmetic asset depends on it.

### Current Colors

Relevant decompiled code:

- `ghidra/eggnoggplus.exe.c:19527` in `game_player_colour_index`
- `ghidra/eggnoggplus.exe.c:19529` in `game_set_player_colour_index`
- `ghidra/eggnoggplus.exe.c:19859` in `game_player_colour`
- `ghidra/eggnoggplus.exe.c:27453` in `main_layout`
- `ghidra/eggnoggplus.exe.c:27602` in `app_state_init`
- `ghidra/eggnoggplus.exe.c:26778` in `app_state_save`

The game stores four color indices:

- Player 1 skin
- Player 1 clothing
- Player 2 skin
- Player 2 clothing

`app_state_init` loads those from `settings.nogg` and applies them through `game_set_player_colour_index`.

`main_layout` creates four color buttons on the bottom row:

- Player 1 skin
- Player 1 clothing
- Player 2 skin
- Player 2 clothing

`game_set_player_colour_index` appears to wrap color indices around a small fixed palette, probably 14 entries, with one special/fallback entry around index `0xd`.

### Existing Menu/State Hooks

Relevant code:

- `hooks.c` has `GameState` wrappers for custom states.
- `hooks_register_custom_state`
- `hooks_enter_custom_state`
- `hooks_leave_custom_state`
- The existing MODS menu demonstrates a custom menu state and a menu button entry.
- The online hub is already represented as a custom state wrapper.

This means the customization screen should be implemented as a real mod-framework state instead of trying to cram everything into the original main menu.

### Atlas And Rendering Constraints

The base game uses atlas 0 and `main_sprite_batches_draw` only draws atlas 0.

Ghidra shows:

- `atlas_get` supports up to 16 atlas slots by masking the index with `0xf`.
- `load_gfx` currently creates atlas 0 at `512 x 512`.
- `sprite_batch_plot` queues draw commands into the atlas stored in the sprite descriptor.
- `main_sprite_batches_draw` draws atlas 0.

So we have two possible asset strategies:

- Make atlas 0 larger or add cosmetic sprites into it.
- Create a separate cosmetics atlas and hook drawing so that atlas also gets flushed each frame.

The separate cosmetics atlas is cleaner long term, but it requires adding an atlas getter/draw path to the hook layer.

## High-Level Architecture

Add a new customization module:

- `cosmetics_ext.h`
- `cosmetics_ext.c`
- optional `cosmetics_ui.c`
- optional `cosmetics_render.c`
- optional `cosmetics_assets.c`

Keep the system split into four layers:

1. Profile data
2. Asset/cache management
3. Render-only cosmetics
4. UI/state integration

Gameplay code should only receive cosmetic metadata at match setup. It should not care about cosmetics while simulating a frame.

## Data Model

### Character Profile

Each local player should have a `CharacterProfile`.

Suggested fields:

```c
typedef struct CharacterProfile {
    char profile_id[64];
    char display_name[32];

    char sprite_skin_id[64];
    char sprite_skin_sha256[65];

    int skin_color_id;
    int clothing_color_id;
    int skin_effect_id;
    int clothing_effect_id;

    char hat_id[64];
    char mask_id[64];
    char torso_id[64];
    char pants_id[64];
    char shoes_id[64];
} CharacterProfile;
```

For online, do not transmit PNGs during gameplay. Transmit IDs and checksums during the pre-match setup only.

### Sprite Skin

First version should support a strict spritesheet format:

```c
typedef struct SpriteSkin {
    char id[64];
    char name[64];
    char sha256[65];
    char local_path[MAX_PATH];

    int width;
    int height;
    int cell_w;
    int cell_h;
    int frame_count;
    int generated_mask_frame_count;

    int official;
    int allowed_online;
    int allowed_ranked;
    int validation_status;
} SpriteSkin;
```

Strict rules:

- PNG only.
- Same dimensions as the base `data/sprites.png` for v1.
- 16x16 grid.
- Same frame count.
- Same frame ordering.
- Same transparency/key-color behavior.
- Same generated mask convention:
  - `128,128,128` in the base image becomes the recolor mask.
  - No custom collision metadata.
- Pixel bounds must fit inside the original safe silhouette envelope.
- Feet, head, torso, and hand anchor positions must stay within allowed ranges.
- The validator records the checksum and validation result.

The first implementation should reject anything that does not fit this strict format.

### Extended Sprite Skin Manifest

Later, support richer animation with a manifest:

```json
{
  "id": "example_skin",
  "version": 1,
  "base": {
    "cell_w": 16,
    "cell_h": 16,
    "frame_order": "eggnoggplus_vanilla"
  },
  "animations": {
    "idle": {
      "frames": [0, 1, 2, 1],
      "fps": 6,
      "render_only": true
    },
    "run": {
      "frames": [8, 9, 10, 11, 12, 13],
      "fps": 12,
      "render_only": true
    }
  },
  "anchors": {
    "head": { "x": 8, "y": 2 },
    "face": { "x": 8, "y": 5 },
    "torso": { "x": 8, "y": 9 },
    "hips": { "x": 8, "y": 12 },
    "left_foot": { "x": 5, "y": 15 },
    "right_foot": { "x": 11, "y": 15 }
  }
}
```

This should be a later phase because the vanilla renderer pulls fixed frame indices from the player object. Richer animations require either:

- Replacing the player renderer with a custom renderer.
- Drawing extra render-only frames on top of or instead of vanilla frames.

The important rule is that extended animation must be visual only. The simulation still advances using the original state.

### Cosmetic Item

Each official cosmetic should be defined by a manifest entry:

```c
typedef enum CosmeticCategory {
    COSMETIC_HAT,
    COSMETIC_MASK,
    COSMETIC_TORSO,
    COSMETIC_PANTS,
    COSMETIC_SHOES
} CosmeticCategory;

typedef struct CosmeticItem {
    char id[64];
    char name[64];
    CosmeticCategory category;
    char asset_id[64];
    char sha256[65];

    char anchor[32];
    int layer;
    float offset_x;
    float offset_y;
    float scale;
    float max_rotation_deg;
    float drag_strength;
    float spring_strength;
    int mirror_with_facing;

    int official;
    int allowed_online;
    int allowed_ranked;
} CosmeticItem;
```

Layer examples:

- `behind_body`
- `body_back`
- `body_front`
- `face`
- `head`
- `front_hand`
- `front_overlay`

The layer system matters because some cosmetics should appear behind the character while others should appear in front.

## Asset And Cache Plan

Add a local cache directory:

- `mods/cosmetics/cache/`

Possible structure:

```text
mods/cosmetics/
  profile.json
  cache/
    manifest.json
    assets/
      official_hat_crown.png
      official_mask_visor.png
      ...
    skins/
      default.png
      ...
```

Server manifest example:

```json
{
  "version": 1,
  "generated_at": "2026-05-17T00:00:00Z",
  "assets": [
    {
      "id": "hat_crown",
      "type": "cosmetic",
      "category": "hat",
      "url": "https://example.com/cosmetics/hat_crown.png",
      "sha256": "...",
      "allowed_online": true,
      "allowed_ranked": true
    }
  ]
}
```

Launch verification:

1. Load the cached manifest.
2. For each official asset, compute SHA-256.
3. If missing or mismatched, download it again.
4. Retry up to 3 times.
5. If it still fails, mark that asset unavailable and use a safe fallback.
6. Never silently use a mismatched official asset.

Later hardening:

- Sign the manifest.
- Pin a server public key in the client.
- Use content-addressed filenames based on checksum.
- Keep official assets read-only where possible.

## Custom Spritesheet Policy

There are two different ideas that should not be mixed:

1. Local custom skins
2. Official/public/ranked skins

### Local Custom Skins

Local custom skins should be allowed early because they are fun and useful for testing.

Rules:

- They can be used offline.
- They can be used in direct friend/private matches only if both clients agree.
- They should be marked as unverified.
- They should be disabled in ranked until there is a moderation/approval path.

### Official/Public Skins

Official/public skins should come from the server manifest.

Rules:

- They are content-addressed.
- They are checksum verified.
- They can be visible to both clients.
- They can be allowed in ranked.
- They should not be loaded from arbitrary local file edits.

### Upload Moderation

Do not make uploaded skins globally visible automatically.

Safer options:

- Local-only upload.
- Private match upload, both clients opt in.
- Server upload with moderation queue.
- Server upload with manual approval before public/ranked visibility.

This is the part that prevents local PNG replacement from becoming public hate-symbol injection.

## Rendering Plan

### Phase 1 Rendering: Vanilla Body Plus Cosmetic Overlay

Keep the original player body drawing as-is.

Add a render-only overlay pass that draws cosmetics at calculated anchors after the vanilla body is drawn.

Pros:

- Lowest risk.
- Does not affect gameplay.
- Does not require replacing all player animation logic.
- Lets us ship hats/masks/etc before tackling per-player spritesheets.

Cons:

- Cannot fully replace the base body art yet.
- Some items may layer imperfectly around arms/swords until we add more hooks.

Implementation approach:

- Hook or wrap the player draw points:
  - `draw_player_body`
  - `draw_player_run`
  - `draw_player_swordfight`
  - selected direct `sprite_batch_plot(... _sprites ...)` cases inside `draw_things`
- Capture:
  - player pointer
  - current turtle transform
  - facing
  - current frame index
  - current state byte if needed
- After vanilla draw, draw cosmetic layers using `sprite_batch_plot`.
- Use a separate cosmetics atlas if atlas 0 capacity is tight.
- If using a separate atlas, hook `main_sprite_batches_draw` to flush atlas 0 and the cosmetics atlas.

### Phase 2 Rendering: Strict Per-Player Spritesheets

Add strict per-player skin loading.

The `default` skin in this phase should mean "use the already-loaded shared spritesheet," including any active `texture_pack_test` replacement. A player only uses the new per-player skin path after explicitly choosing a custom skin.

Two possible implementation paths:

#### Option A: Temporarily Redirect Global Sprite Pointers

For each player draw:

1. Determine which player is being drawn.
2. Save `_sprites`, `_sprites_id`, and `_sprites_count`.
3. Swap them to that player's loaded skin base.
4. Call the original draw function.
5. Restore the globals.

Pros:

- Smaller first patch.
- Reuses vanilla renderer.
- Easier to keep animation behavior identical.

Cons:

- Fragile if nested rendering or direct draw paths bypass the wrapper.
- Must cover all places that draw player sprites.
- Needs careful testing with menus, replays, online rollback, and game end states.

#### Option B: Custom Player Renderer

Reimplement the relevant pieces of:

- `draw_player_body`
- `draw_player_run`
- `draw_player_swordfight`
- direct player sprite cases in `draw_things`

Pros:

- Correct long-term architecture.
- Lets us handle per-player spritesheets cleanly.
- Easier to add rich cosmetics and extended animation.
- Easier to place layers around body/arms/swords.

Cons:

- More work.
- Needs careful side-by-side testing against vanilla rendering.
- Requires more decompilation cleanup.

Recommendation:

- Use Option A only if it gets us a working prototype quickly.
- Move to Option B before extended animations or serious online cosmetic release.

### Phase 3 Rendering: Extended Animation

Only after strict skins and overlays are stable:

- Add a manifest format for extra animation frames.
- Map vanilla logical states to render-only animations.
- Use render ticks and player state to choose cosmetic animation frames.
- Never write animation frame choices into gameplay state.
- Do not include extended animation frame counters in rollback checks.

Examples:

- Idle breathing.
- Extra idle flourishes.
- More detailed run cycle.
- Attack smear frame.
- Clothing/hat secondary motion.

## Cosmetic Attachment Motion

Cosmetics should have render-only secondary motion.

Example for a hat:

- Anchor to `head`.
- Target position comes from player position, facing, frame, and state.
- Rotation target is based on velocity/facing changes.
- Actual rotation uses a small spring-damper.
- Maximum rotation is clamped.
- Motion state is kept in `CosmeticRuntimeState`, not in gameplay state.

Suggested runtime fields:

```c
typedef struct CosmeticRuntimeState {
    float angle;
    float angle_velocity;
    float x_lag;
    float y_lag;
} CosmeticRuntimeState;
```

Important:

- If rollback rewinds gameplay, cosmetics can snap or smooth visually afterward.
- Cosmetic runtime state should not participate in desync detection.
- Online clients do not need identical cosmetic secondary motion for gameplay correctness, but it should be close enough visually.

## Color Customization Plan

The current color system is tiny and index-based.

There are two possible paths:

### Option A: Extend The Existing Palette

Patch or wrap `game_player_colour` and `game_set_player_colour_index` so color IDs can resolve into a larger custom palette.

Pros:

- Keeps existing color buttons and draw behavior.
- Uses the existing body mask/tint path.

Cons:

- Need to avoid breaking assumptions around the original 14-color wrap.
- May require intercepting button behavior and settings save/load.

### Option B: Leave Vanilla Colors Alone And Override Player Tint Fields

After the game computes player colors, write custom RGBA values into the player fields used by `draw_player_body`:

- `player + 0xe8`
- `player + 0xec`
- `player + 0xf0`
- `player + 0xf4`

Pros:

- Direct control over the rendered tint.
- Easier to support animated effects.
- Avoids patching the original palette table at first.

Cons:

- Need reliable access to both active player objects each frame.
- Need to make sure menu previews and non-game states also use the desired colors.

Recommendation:

- Use Option B for custom profiles/effects.
- Keep original palette indices for compatibility.
- Store richer color/effect choices in `mods/cosmetics/profile.json`.

Suggested color/effect types:

- Solid RGB.
- Two-color gradient.
- Three-color gradient.
- Rainbow/RGB shift.
- Slow hue cycle.
- Pulse.
- Metallic shimmer.
- Fire-like warm shift.
- Ice-like cool shift.
- Team-safe high-contrast presets for online readability.

Effects should be render-only and based on render time/tick, not simulation decisions.

## Customization UI Plan

Add a hanger icon button somewhere on the main menu.

Implementation options:

- Add it directly to `main_layout` by detouring main menu setup.
- Add it through existing hook infrastructure similar to the MODS button path.
- Use a small icon from `_tiles` or add a new icon asset.

Pressing the button enters:

- `cosmetics_state`

The state should include:

- Enlarged live character preview.
- Player/profile selector.
- Category tabs or filters:
  - All
  - Hats
  - Masks
  - Torso
  - Pants
  - Shoes
  - Colors
  - Sprite
- Grid/list of cosmetic items.
- `X` item in every category to remove the equipped item.
- Color editor for skin/clothing.
- Effect picker for skin/clothing.
- Sprite skin picker/import button.
- Back button.

Preview behavior:

- Show the selected player large enough to inspect details.
- Use the real player render path where possible.
- If the real render path is too tied to game state, create a preview dummy player object with safe defaults.
- Add simple idle/run/attack preview toggles later.

Input behavior:

- Controller/keyboard first, since the base game menu is built around buttons/selectors.
- Mouse support later if we want this screen to feel modern.

Save behavior:

- Save profiles to `mods/cosmetics/profile.json`.
- Do not depend on `settings.nogg` for the expanded customization data.
- Keep a compatibility bridge to the original color indices so vanilla menus still have reasonable defaults.

## Online Integration Plan

Before match start, both clients should exchange cosmetic metadata:

```json
{
  "display_name": "player",
  "sprite_skin_id": "default",
  "sprite_skin_sha256": "...",
  "skin_color_id": 12,
  "clothing_color_id": 38,
  "skin_effect_id": 0,
  "clothing_effect_id": 3,
  "cosmetics": {
    "hat": "hat_crown",
    "mask": "",
    "torso": "torso_scarf",
    "pants": "",
    "shoes": "shoes_boots"
  }
}
```

Match startup rules:

- Resolve all official asset IDs before gameplay starts.
- Verify checksums before frame 0.
- If an official asset is missing and cannot be downloaded, replace it with empty/default.
- If a custom local skin is not available to the opponent, replace it with default.
- Never download cosmetic PNGs during active gameplay.
- Never include cosmetic IDs or animation state in rollback frame checks.

Ranked rules:

- Official cosmetics only.
- Approved skins only.
- No unmoderated local uploads.
- Server-granted cosmetics like the highest-ELO crown are entitlement metadata, not local file edits.

Casual/friend rules:

- Official cosmetics allowed.
- Local custom skins can be allowed if both clients opt in.
- If either client rejects or lacks the skin, fall back to default.

## Ranked Crown Plan

Later, the account server can grant a temporary cosmetic:

- `hat_ranked_crown`

Behavior:

- Only the current highest-ELO player has it.
- It is checked at login, queue entry, and match start.
- It is not permanent.
- If someone passes that player in ELO, the server grants it to the new leader.
- The local client treats it like any other official server-granted cosmetic.

## Implementation Phases

### Phase 0: Documentation And Address Map

- Keep this document updated as implementation details change.
- Create a small address/symbol map for the draw/color/menu functions we rely on.
- Document player struct offsets used by cosmetics.

Done when:

- `COSMETICS.md` exists.
- We know which functions need hooks for v1.

### Phase 1: Asset Manifest And Cache

- Add `cosmetics_ext.c/h`.
- Add local profile path helpers.
- Add official manifest loader.
- Add SHA-256 verification.
- Add 3-retry download logic.
- Add safe fallback behavior.
- Add logging for every asset failure.

Done when:

- The game can load/verify a local manifest.
- Bad checksums are detected.
- Missing assets fall back safely.

### Phase 2: Profile Save/Load

- Add `mods/cosmetics/profile.json`.
- Load local player customization at startup.
- Save changes from the customization menu.
- Expose simple console commands for testing:
  - `cosmetics.reload`
  - `cosmetics.profile`
  - `cosmetics.set_hat <id>`
  - `cosmetics.set_color <slot> <id>`

Done when:

- Profiles survive restart.
- Invalid profile entries fall back safely.

### Phase 3: Expanded Colors And Effects

- Add custom color/effect registry.
- Apply render-time colors to player body mask tint.
- Keep original palette indices as fallback.
- Add enough presets that the color UI is worth using.

Done when:

- Players can choose many more colors.
- Animated effects render correctly.
- Gameplay state checks are unchanged.

### Phase 4: Customization Menu

- Add hanger icon to the main menu.
- Add `cosmetics_state`.
- Add character preview.
- Add category filters.
- Add item selection.
- Add `X` removal item per category.
- Add color/effect picker.
- Save profile edits.

Done when:

- A player can enter the screen, equip/remove cosmetics, change colors, preview the result, save, and return.

### Phase 5: Cosmetic Overlay Renderer

- Load cosmetic item sprites into an atlas.
- Add draw hooks around player rendering.
- Draw hats/masks/torso/pants/shoes at anchor positions.
- Add layer ordering.
- Add simple attachment motion for hats and similar items.

Done when:

- Equipped cosmetics appear in match and menu preview.
- They follow player facing and animation.
- They do not affect gameplay or checksums.

### Phase 6: Strict Per-Player Spritesheets

- Add strict sprite skin validator.
- Load validated skin sheets into atlas storage.
- Preserve texture-pack compatibility by keeping the active texture pack as the shared/default skin.
- Implement one of:
  - temporary global sprite pointer swap, or
  - custom player renderer.
- Support one skin per player.
- Preserve vanilla frame order and mask behavior.

Done when:

- Player 1 and Player 2 can use different full spritesheets.
- Skin differences are visible on both clients.
- Invalid or oversized sheets are rejected.
- Desync checks remain unchanged.

### Phase 7: Online Metadata

- Add cosmetic metadata to pre-match handshake.
- Verify official assets before gameplay starts.
- Fall back for missing/unapproved assets.
- Keep cosmetic data outside rollback.
- Add display-name support for future username-over-head rendering.

Done when:

- Both clients see each other's selected cosmetics.
- Missing assets do not block gameplay unless ranked rules require it.
- No cosmetic data is sent during active gameplay.

### Phase 8: Extended Sprite Animations

- Add optional skin manifests.
- Add render-only animation mapping.
- Add extra idle/run/attack frames.
- Add preview animation controls.
- Add authoring/validation tools.

Done when:

- A skin can have richer visual animation without affecting gameplay.

### Phase 9: Server Moderation And Public Uploads

- Add account-linked cosmetic inventory/profile endpoint.
- Add upload endpoint for skins.
- Add approval/moderation workflow.
- Add public approved-skin manifest.
- Add ranked restrictions.

Done when:

- Custom uploads can become public safely.

## Suggested File Layout

```text
eggnoggplus-win/
  COSMETICS.md
  cosmetics_ext.h
  cosmetics_ext.c
  cosmetics_assets.c
  cosmetics_assets.h
  cosmetics_render.c
  cosmetics_render.h
  cosmetics_ui.c
  cosmetics_ui.h
  mods/
    cosmetics/
      profile.json
      cache/
        manifest.json
        assets/
        skins/
```

Keep the implementation modular enough that offline customization works before account/login systems exist.

## Testing Plan

### Offline Tests

- Launch with no profile.
- Launch with valid profile.
- Launch with corrupt profile.
- Equip/remove every category.
- Switch colors/effects repeatedly.
- Validate a good strict spritesheet.
- Reject wrong dimensions.
- Reject wrong frame count.
- Reject oversized silhouette.
- Reject broken PNG.
- Test with texture pack mod enabled.
- Test with texture pack mod disabled.
- Test with one player using the texture-pack/default skin and the other using a strict custom skin.
- Test that cosmetic overlays still appear correctly over a texture-pack spritesheet.
- Test that cosmetic assets do not overwrite or unregister `texture_pack_test` replacements.

### Gameplay Tests

- Full match with no cosmetics.
- Full match with all categories equipped.
- Full match with animated color effects.
- Full match with custom skin on Player 1 only.
- Full match with custom skin on Player 2 only.
- Mines, swords, jumps, wall collisions, deaths, round transitions.
- Confirm no gameplay-state checksum changes from cosmetics.

### Online Tests

- Both clients have all assets.
- One client missing an official asset.
- One client has a checksum mismatch.
- One client uses unapproved local skin.
- Ranked rejects unapproved skin.
- Casual falls back or prompts/opts in.
- High latency match with cosmetics enabled.
- Packet loss match with cosmetics enabled.

### UI Tests

- Small window readability.
- Fullscreen.
- Keyboard/controller navigation.
- Category filters.
- `X` removal items.
- Preview updates immediately.
- Back/return behavior.

## Open Questions For You

These are the details I should ask you about before implementation:

- Should local custom spritesheets be visible in friend matches automatically, or only if both players explicitly allow custom local skins?
- Do you want custom spritesheets to ever be visible in ranked, or should ranked always be official/approved skins only?
- Do you want the first version of custom spritesheets to be strict 16x16 vanilla-compatible only?
- Do you want high-resolution cosmetics, or should cosmetics also be pixel-art 16x16/low-res assets that match the game?
- Should colors/effects apply separately to skin and clothing, or should some effects affect the entire character?
- Should shoes/pants/torso cosmetics be subtle overlays, or can they cover large parts of the base sprite?
- Should masks hide the face completely, or layer over face details?
- What exact categories do you want first: hats/masks only, or all five categories immediately?
- For server-hosted official cosmetics, what domain/storage path should the client use later?
- For moderation, do you want manual approval, friends-only sharing, or both?

## Recommended First Build Slice

Start with the smallest useful version:

1. Add `cosmetics_ext.c/h` with profile save/load.
2. Add a few hardcoded color/effect presets.
3. Add the hanger button and `cosmetics_state`.
4. Add a preview and item/category UI using placeholder cosmetics.
5. Add the overlay renderer for hats and masks.
6. Once overlays are stable, add torso/pants/shoes.
7. Then tackle strict per-player spritesheets.

This order gets visible customization working quickly while avoiding the riskiest part, which is replacing the player renderer before the cosmetic model and UI are proven.
