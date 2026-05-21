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

### Second-Pass Audit Notes

This pass checked the plan against the decompilation and the current framework code more closely.

Confirmed function addresses:

| Purpose | Function | Address |
| --- | --- | --- |
| Get atlas slot | `atlas_get` | `0x4013e0` |
| Upload atlas to GL | `atlas_upload` | `0x401480` |
| Create atlas texture | `atlas_create_texture` | `0x4017b0` |
| Add RGBA spritesheet | `atlas_add_spritesheet_from_rgba` | `0x402330` |
| Get sprite descriptor | `sprite_get` | `0x405d20` |
| Get sprite count | `sprite_count` | `0x405d60` |
| Queue sprite draw | `sprite_batch_plot` | `0x405890` |
| Flush atlas batch | `sprite_batch_draw` | `0x405a90` |
| Player body draw | `draw_player_body` | `0x41bdd0` |
| Player swordfight draw | `draw_player_swordfight` | `0x41bf50` |
| Player run draw | `draw_player_run` | `0x41c240` |
| Full thing draw loop | `draw_things` | `0x41c3f0` |
| Player color lookup | `game_player_colour` | `0x4207c0` |
| Graphics load | `load_gfx` | `0x42fb00` |

Confirmed globals:

| Purpose | Symbol | Address |
| --- | --- | --- |
| Player sprite base pointer | `_sprites` | `0x55a140` |
| Player sprite count | `_sprites_count` | `0x547b90` |
| Player sprite base id | `_sprites_id` | `0x547b94` |
| Misc sprite base pointer | `_misc` | `0x55a344` |
| Tile sprite base pointer | `_tiles` | `0x55a33c` |
| Glyph sprite base pointer | `_glyphs` | `0x55a384` |
| Game tick counter | `_game_ticks` | `0x547ba0` |
| Thing array | `_things` | `0x542080` |
| Player pointer array | player slots | `0x542058` |
| Camera X | `_camera` | `0x55a360` |
| Game width | `_game_w` | `0x55a394` |

Confirmed player/thing layout:

| Field | Offset |
| --- | --- |
| Struct size | `0x15c` |
| Active flag | `thing + 0x00` |
| Thing type | `thing + 0x01` |
| Player type value | `1` |
| Sprite/frame index | `player + 0x04` |
| Has sword state | `player + 0x11` |
| X/Y | `player + 0x24`, `player + 0x28` |
| Velocity X/Y | `player + 0x34`, `player + 0x38` |
| State id | `player + 0x78` |
| Facing sign | `player + 0x98` |
| Room index | `player + 0x9b` |
| Collision flags | `player + 0xad` |
| Render base RGBA | `player + 0xd8` through `player + 0xe4` |
| Clothing/mask RGBA | `player + 0xe8` through `player + 0xf4` |

Important correction: `draw_things` has several direct `sprite_batch_plot(... _sprites ...)` calls, not only calls through `draw_player_body`, `draw_player_run`, and `draw_player_swordfight`. That means a full per-player skin system cannot be perfectly implemented by hooking `draw_player_body` alone.

Important rollback note: the framework currently copies both full player structs into the full-state blob (`PLAYER_SIZE = 0x15c`). Cosmetic data stored outside those structs is safe. Cosmetic data written into player struct bytes is not automatically safe unless the rollback canonicalization path ignores it.

### Current Framework UI Findings

The framework already has enough UI support to prototype the customization screen:

- `mod.ui.create_state(name)`
- `mod.ui.enter_state(name)`
- `mod.ui.leave_state()`
- `mod.ui.text_at`
- `mod.ui.button_at`
- `mod.ui.native_button`
- `mod.ui.draw_sprite`
- `mod.ui.sheet_base`
- `mod.ui.sprite_id`
- `mod.ui.mouse_pos`
- `mod.ui.begin_overlay`
- `mod.ui.end_overlay`
- `mod.game.snapshot`

The current immediate-mode UI is intentionally simple. Buttons are rendered as bracketed text and hitboxes. Native buttons support selector swords and engine navigation, but custom states still need nicer layout/widgets for the kind of cosmetic browser we want.

## Exact Implementation Outline

### Hats-Only First Slice

The first implementation is intentionally limited to headwear:

- Add a small main-menu icon button that opens a hats customization screen.
- Store separate local selections for Player 1 and Player 2.
- Show a live character preview on the left side of the hats screen.
- Draw selected hats as render-only overlay sprites anchored above each player's head during gameplay.
- Let each hat definition opt into or out of motion. Motion-enabled hats can add small velocity drag, bob, and tilt; fixed hats such as halos or tall hats stay locked to the head anchor.
- Keep hat state outside gameplay structs and rollback blobs.

This is a prototype render path, not the final online asset pipeline. It uses local bundled assets first so UI, profile storage, and head anchoring can be tested before checksum-backed server delivery exists.

### 1. Do Not Build Cosmetics As A Texture Replacement

The texture-pack system is still correct for global texture packs. Cosmetics should not register a `data/sprites.png` replacement in normal operation.

Use these meanings:

- `default skin`: whatever `_sprites` points at after `load_gfx`, including active texture-pack replacements.
- `custom skin`: a separately loaded spritesheet selected by one player.
- `official cosmetic`: a separately loaded asset from the cosmetics manifest/cache.

### 2. Add Native Cosmetics Core

Add:

- `cosmetics_ext.h`
- `cosmetics_ext.c`
- `cosmetics_assets.c/h`
- `cosmetics_render.c/h`
- `cosmetics_profile.c/h`

This should be native C, not only Lua, because it needs:

- checksum verification
- asset cache management
- atlas loading
- draw hooks
- fast per-frame runtime state
- eventual online metadata integration

Lua can drive the customization UI, but the core cosmetic data/render system should be native.

### 3. Add A Cosmetics Atlas Path

Expose or wrap these engine functions in native code:

- `atlas_get`
- `atlas_create_texture`
- `atlas_upload`
- `atlas_add_spritesheet_from_rgba`
- `sprite_get`
- `sprite_count`
- `sprite_batch_draw`

Recommended:

- Use atlas 1 for cosmetics.
- Create it as `1024 x 1024` initially.
- Load official cosmetic item sprites into atlas 1.
- Load strict custom player skins into atlas 1 or another dedicated atlas.
- Hook `main_sprite_batches_draw` or add a post-render flush so atlas 1 is drawn every frame after atlas 0.

Why not atlas 0 first:

- The game creates atlas 0 as `512 x 512`.
- The vanilla sheets already consume a lot of it.
- Texture packs still need atlas 0 to behave like vanilla.
- A separate atlas keeps cosmetics from breaking vanilla art and texture-pack behavior.

### 4. Add Strict Skin Loader

For player skins, duplicate the `load_gfx` behavior:

1. Load the PNG as RGBA.
2. Validate dimensions and frame grid.
3. Allocate a new image with double height.
4. Copy the original pixels into the first half.
5. Clear the second half.
6. For every pixel equal to `128,128,128`, write white `255,255,255,255` into the matching pixel in the second half.
7. Call `atlas_add_spritesheet_from_rgba(atlas, 16, 16, 0, 1, image)`.
8. Record:
   - `base_id = sprite_count_before`
   - `base_ptr = sprite_get(base_id)`
   - `count = sprite_count_after - sprite_count_before`
   - `mask_offset = count / 2`
9. Upload the atlas.

This reproduces vanilla mask generation while keeping custom skins separate from the shared texture-pack/default sheet.

### 5. Keep The Default Skin Zero-Cost

If a player has no custom skin:

- Do not load another spritesheet.
- Use the existing `_sprites`, `_sprites_id`, and `_sprites_count`.
- This automatically respects `texture_pack_test`.

### 6. Use Lua For The First Customization Screen

The first UI can be a Lua-driven custom state:

```lua
mod.ui.create_state("cosmetics")

mod.on_frame(function()
  if mod.ui.state_name() ~= "cosmetics" then return end

  mod.ui.begin_overlay()
  -- draw preview, category buttons, color swatches, item grid
  mod.ui.end_overlay()
end)
```

Add the main-menu hanger entry using `mod.ui.native_button` or a small native hook that creates an engine-backed button.

This gives us a fast iteration loop for layout and UX while the native cosmetics core matures.

### 7. Prototype Gameplay Cosmetics As A Top Overlay Only

For the first visible gameplay test, draw cosmetics after the game render using:

- `mod.game.snapshot`
- camera/world-to-screen helpers
- `mod.ui.draw_sprite`
- `mod.ui.begin_overlay`
- `mod.ui.end_overlay`

This will draw hats/masks above everything, so it is not final. But it is useful for validating:

- profile data
- asset loading
- item selection
- online metadata
- basic anchor math
- attachment drag/rotation feel

### 8. Move Gameplay Cosmetics Into Native Rendering

For final layering, the renderer needs native hooks.

Short-term native hook option:

- Detour `draw_player_body` with a GCC naked wrapper.
- Preserve registers/flags.
- Capture `EAX` as the current player pointer.
- Set `EAX` back before calling the trampoline because the original function expects the player in `EAX`.
- Draw front-layer cosmetics after the original body draw.
- Use guards to avoid drawing a hat twice when a state calls `draw_player_body` more than once.

This is good enough for hats/masks prototypes, but not a complete system.

Why it is incomplete:

- `draw_things` directly draws player sprites in some attack/death/special states.
- Some states call `draw_player_body` multiple times.
- Per-player skins must affect every `_sprites` draw for that player, not just the main body helper.

Long-term native hook option:

- Build a custom player renderer from the decompiled `draw_things` player branch.
- Keep vanilla math for transforms/states.
- Replace `_sprites` lookups with the chosen player's skin base.
- Insert cosmetic layers intentionally:
  - behind body
  - body back
  - body front
  - mask/face
  - hat/head
  - weapon/front overlays

This is the correct path before shipping serious online cosmetics.

### 9. Treat Global Sprite Pointer Swapping As Prototype-Only

There is a tempting shortcut:

1. Save `_sprites`, `_sprites_id`, `_sprites_count`.
2. Swap them to the current player's skin.
3. Call the original draw function.
4. Restore the globals.

This can work for a narrow prototype because `_sprites` is at `0x55a140` and `_sprites_count` is at `0x547b90`.

But it should not be the final design because:

- It is fragile around nested draw calls.
- It misses direct `_sprites` draws unless every path is wrapped.
- It is risky if another mod/render path draws during the swap.
- It makes future extended animations harder.

Use it only if we need a quick proof-of-concept for strict per-player skins.

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

### Online Hub Profile Note

When the online hub is added, cosmetics should also support a single local account/profile loadout that is independent of the match slot. In an online match the client only controls one character, and players will usually expect their selected cosmetics and colors to follow them whether matchmaking assigns them to Player 1 or Player 2.

The P1/P2 local selections are useful for offline play and local testing. Online profile data should map the authenticated local user to the controlled slot during match setup, then receive the remote player's cosmetic IDs/checksums from the server.

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

There are two versions of this phase:

- Prototype: draw as a top-level overlay after the game render.
- Final overlay: draw from native player-render hooks so layering is correct.

Pros:

- Lowest risk.
- Does not affect gameplay.
- Does not require replacing all player animation logic.
- Lets us ship hats/masks/etc before tackling per-player spritesheets.

Cons:

- Cannot fully replace the base body art yet.
- Some items may layer imperfectly around arms/swords until we add more hooks.

Implementation approach:

- For the prototype, use `mod.game.snapshot`, world-to-screen helpers, and `mod.ui.draw_sprite`.
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

### Option B: Render-Only Color Override

The tempting version of this is to write custom RGBA values into the player fields used by `draw_player_body`:

- `player + 0xe8`
- `player + 0xec`
- `player + 0xf0`
- `player + 0xf4`

But the framework currently snapshots the full `0x15c` player struct for rollback/full-state checks. That means these bytes are part of the state blob today. Writing animated cosmetic colors directly into the player struct would make cosmetic presentation part of rollback/checksum behavior unless we explicitly canonicalize or ignore those offsets.

The safer version is:

- Keep vanilla player color fields as simulation-owned values.
- Store expanded colors/effects in `CharacterProfile`.
- During custom rendering, set the turtle color from the profile before drawing the mask layer.
- Do not write animated cosmetic color values into the player struct during gameplay.

Pros:

- Direct control over the rendered tint once the renderer hook/custom renderer exists.
- Easier to support animated effects.
- Keeps cosmetic effects outside rollback state.

Cons:

- Requires a render hook or custom player renderer for true gameplay use.
- A quick prototype that writes player fields must also update rollback canonicalization, or it risks cosmetic-state desync noise.

Recommendation:

- Use Option B for custom profiles/effects.
- Keep original palette indices for compatibility.
- Store richer color/effect choices in `mods/cosmetics/profile.json`.
- Do not mutate `player + 0xe8..0xf4` for animated cosmetic effects unless rollback canonicalization has been updated.

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

## Framework UI API Upgrade Outline

The current `mod.ui` API is good enough for debug UI and a first prototype, but the cosmetics screen wants a nicer immediate-mode UI layer.

The upgrades below should be general framework features, not cosmetics-only hacks.

### Low-Level Drawing Primitives

Add:

```lua
mod.ui.rect(x, y, w, h, opts)
mod.ui.line(x1, y1, x2, y2, opts)
mod.ui.border(x, y, w, h, opts)
mod.ui.measure_text(text, scale) -> w, h
mod.ui.draw_image(asset_id, x, y, opts)
```

Needed for:

- nice panels
- category tabs
- selected item outlines
- color swatches
- sliders
- scrollbars
- preview frames
- icon-only buttons

Implementation note:

- `rect` can be implemented either as a tiny white sprite tinted/scaled, or as a new immediate quad renderer.
- `measure_text` can use existing glyph width helpers if exposed, or the current approximate text width as a temporary fallback.

### Asset/Spritesheet UI API

Add a proper asset API instead of relying only on built-in sheet names:

```lua
mod.assets.load_image(id, path, opts) -> bool, err
mod.assets.load_spritesheet(id, path, opts) -> sheet, err
mod.assets.unload(id)
mod.assets.info(id) -> table

mod.ui.sheet_base("cosmetics:hat_crown") -> base_id
mod.ui.sprite_id("cosmetics:hat_crown", frame) -> sprite_id
```

Suggested options:

```lua
{
  atlas = "cosmetics",
  cell_w = 16,
  cell_h = 16,
  padding = 1,
  generate_player_mask = false,
  checksum = "...",
}
```

Needed for:

- cosmetic icons
- item sprites
- custom player skins
- preview images
- future server-downloaded assets

### Style System

Add scoped styles:

```lua
mod.ui.push_style({
  bg = {0.08, 0.09, 0.10, 0.92},
  fg = {0.92, 0.94, 0.96, 1.0},
  accent = {1.0, 0.78, 0.25, 1.0},
  border = {0.25, 0.28, 0.32, 1.0},
  radius = 4,
  pad = 8,
})

mod.ui.pop_style()
```

Or keep it simpler:

```lua
mod.ui.set_theme("default_dark")
mod.ui.theme({
  accent = {1, 0.78, 0.25, 1}
})
```

This keeps custom states from each inventing their own colors and button shapes.

### Better Widgets

Add immediate-mode widgets that return `value, changed`:

```lua
local value, changed = mod.ui.slider("scale", value, 0.5, 2.0, opts)
local enabled, changed = mod.ui.checkbox("enabled", enabled, opts)
local selected, changed = mod.ui.tabs("category", tabs, selected, opts)
local selected, changed = mod.ui.segmented("mode", modes, selected, opts)
local selected, changed = mod.ui.dropdown("skin", items, selected, opts)
local selected, changed = mod.ui.swatch_grid("colors", colors, selected, opts)
local selected, changed = mod.ui.item_grid("hats", items, selected, opts)
```

Minimum widgets for cosmetics:

- `icon_button`
- `tabs` or `segmented`
- `swatch_grid`
- `item_grid`
- `slider`
- `checkbox`
- `scroll_area`
- `tooltip`

### Focus And Navigation

Custom states need keyboard/controller navigation that is not tied to vanilla button structs.

Add:

```lua
mod.ui.focus_next()
mod.ui.focus_prev()
mod.ui.focus_id() -> id
mod.ui.set_focus(id)
mod.ui.nav_grid(id, cols, rows, opts)
mod.ui.activate_focused() -> id
```

Desired behavior:

- Arrow keys/controller directions move focus.
- Confirm activates focused widget.
- Back leaves the custom state or closes a submenu.
- Mouse hover can move focus, but should not be required.
- Sliders respond to left/right.
- Scroll areas respond to wheel and controller shoulder buttons.

This matters because cosmetics must be usable from the couch/controller path, not just mouse.

### Custom State Lifecycle

Current custom states are usable, but mods have to branch inside `on_frame` and `on_event`.

Add higher-level state helpers:

```lua
mod.ui.define_state("cosmetics", {
  enter = function() end,
  update = function(dt) end,
  render = function() end,
  event = function(e) return false end,
  leave = function() end,
})

mod.ui.enter_state("cosmetics")
```

Or add event hooks:

```lua
mod.on_state_enter("cosmetics", fn)
mod.on_state_render("cosmetics", fn)
mod.on_state_leave("cosmetics", fn)
```

This would make custom states easier to write and hot-reload.

### Layout Helpers

Add:

```lua
mod.ui.begin_panel(id, x, y, w, h, opts)
mod.ui.end_panel()
mod.ui.columns(id, widths_or_count, opts)
mod.ui.same_line(gap)
mod.ui.spacing(px)
mod.ui.begin_scroll(id, x, y, w, h, opts)
mod.ui.end_scroll()
mod.ui.grid(id, x, y, cols, cell_w, cell_h, gap, opts)
```

Needed for:

- cosmetic category sidebar
- item grid
- color palette
- preview panel
- responsive small-window layout

### Clipping And Scroll Areas

The cosmetic item list will be too large for one screen.

Add:

```lua
mod.ui.push_clip(x, y, w, h)
mod.ui.pop_clip()
mod.ui.scroll_area(id, x, y, w, h, content_h, opts) -> scroll_y
```

This may require exposing GL scissor or adding a simple clipping layer in the overlay renderer.

### Player Preview Helpers

Add framework helpers specifically useful for game UI:

```lua
mod.game.player_ptr(index) -> ptr
mod.game.camera() -> x, y
mod.game.world_to_screen(x, y, room_index) -> sx, sy
mod.ui.draw_player_preview(profile_or_index, x, y, opts)
```

`draw_player_preview` can start as a cosmetics-only native function and later become public API if useful.

Needed for:

- live enlarged customization preview
- username-above-head later
- local `V` marker later
- testing cosmetics in-game without hardcoding camera math in Lua

### Native Button Styling

The decompiled button system already supports per-button colors and backing sprites:

- background color
- foreground/text color
- highlighted background/foreground
- disabled background/foreground
- backing sprite
- pulse amount/speed
- text scale
- width/height

Current Lua exposes some size/text mutation but not the full style surface.

Add:

```lua
mod.ui.native_style(id, {
  bg = {...},
  fg = {...},
  hi_bg = {...},
  hi_fg = {...},
  disabled_bg = {...},
  disabled_fg = {...},
  backing_sprite = sprite_id,
  pulse_amount = 0.15,
  pulse_speed = 1.0,
})
```

This would let the hanger button and future online hub buttons look intentional while still using engine navigation.

### Documentation Cleanup

`mod.ui.begin_overlay` and `mod.ui.end_overlay` exist in code but should be documented in `MODDING.md`.

Also document:

- custom state lifecycle
- which widgets are immediate-mode only
- which widgets use native engine buttons
- how mouse/controller focus interacts
- how to avoid drawing under tiles

## Current Hats Server Contract

The current hats-only implementation fetches official cosmetic metadata from:

- `https://loafiieee.com/eggnogg/cosmetics/v1/manifest.json`
- `https://loafiieee.com/eggnogg/cosmetics/v1/assets/hats.png`

Host exactly those files for the first pass. The client downloads the manifest, reads the expected SHA-256 for `hats.png`, downloads the PNG, checks the bytes, then caches the verified PNG in the mod cache. If the game starts offline, it keeps using bundled hats and retries the server check later. Before an online match, the future online hub should call the cosmetics interop API and wait until `validate_for_online()` returns true.

Example `manifest.json`:

```json
{
  "schema": 1,
  "version": "2026-05-21.1",
  "assets": {
    "hats": {
      "url": "https://loafiieee.com/eggnogg/cosmetics/v1/assets/hats.png",
      "sha256": "3bbc38cdc3b540ea337e75a4800d84c47c5c199e456cfca5312c1bf662ca0245",
      "cell_w": 32,
      "cell_h": 32
    }
  },
  "hats": [
    { "id": "cap", "name": "Cap", "sprite_index": 0, "motion": true, "scale": 0.64, "y": 10.2, "bob": 0.7, "tilt": 7.0, "drag": 0.40, "allowed_online": true },
    { "id": "crown", "name": "Crown", "sprite_index": 1, "motion": true, "scale": 0.68, "y": 10.8, "bob": 0.45, "tilt": 5.0, "drag": 0.25, "allowed_online": true },
    { "id": "halo", "name": "Halo", "sprite_index": 2, "motion": false, "scale": 0.72, "y": 11.4, "bob": 0.0, "tilt": 0.0, "drag": 0.0, "allowed_online": true },
    { "id": "beanie", "name": "Beanie", "sprite_index": 3, "motion": true, "scale": 0.66, "y": 10.4, "bob": 0.75, "tilt": 6.0, "drag": 0.35, "allowed_online": true },
    { "id": "top_hat", "name": "Top Hat", "sprite_index": 4, "motion": false, "scale": 0.72, "y": 10.8, "bob": 0.0, "tilt": 0.0, "drag": 0.0, "allowed_online": true },
    { "id": "visor", "name": "Visor", "sprite_index": 5, "motion": true, "scale": 0.62, "y": 9.6, "bob": 0.35, "tilt": 4.0, "drag": 0.20, "allowed_online": true }
  ]
}
```

The current bundled `assets/hats.png` SHA-256 is:

```text
3bbc38cdc3b540ea337e75a4800d84c47c5c199e456cfca5312c1bf662ca0245
```

If `hats.png` changes, update the manifest SHA before deploying it. The client treats HTTPS plus the manifest-provided SHA-256 as the first server authority. A later hardening pass should replace this with a true signed manifest, for example `manifest.sig` verified against a public key pinned in the client.

Interop exposed by the hats mod:

```lua
local api = mod.interop.require("official_cosmetics:api", ">=1.0.0 <2.0.0")
local ok, status = api.validate_for_online()
local local_profile = api.get_online_profile()
api.set_match_profile("p1", local_profile)
api.set_match_profile("p2", remote_profile)
```

`get_online_profile()` returns one online profile independent of local Player 1 / Player 2 menu slots. The online hub should map that profile onto whichever match slot the local client controls, and should send only IDs, colors, manifest URL, and SHA-256 to the other client.

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

Current implementation status:

- Not implemented in the current tree after rollback to the UI API foundation work.

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

Current implementation status:

- Not implemented in the current tree after rollback to the UI API foundation work.

### Parallel Track: UI API Foundations

This can start before the final cosmetics renderer.

Initial implementation status:

- `MODDING.md` now documents `begin_overlay` and `end_overlay`.
- Added low-level `mod.ui.rect`, `mod.ui.border`, `mod.ui.line`, `mod.ui.measure_text`, `mod.ui.hitbox`, and `mod.ui.mouse_buttons`.
- Added Lua-side style helpers and first-pass widgets: `icon_button`, `tabs`/`segmented`, `swatch_grid`, `item_grid`, `slider`, `checkbox`, and `tooltip`.
- Added `mod.ui.define_state` for per-mod custom state lifecycle callbacks.
- Added readable text scaling, measured wrapping helpers, an automatic custom-state mouse cursor, and a base `tile_preview` fallback.
- Added `mod.assets.load_spritesheet`, `mod.assets.sprite_id`, and `mod.assets.info` for loading mod-owned PNG sprites into the live atlas through the `atlas_upload` hook while the engine packer is still valid.
- Added `mods/ui_api_test` as a custom state for exercising old and new UI APIs.
- Remaining UI foundation work: keyboard/controller focus/navigation, clipping/scroll areas, and native callback-accurate tile previews.

- Document `mod.ui.begin_overlay` and `mod.ui.end_overlay`.
- Add low-level `rect`, `border`, and `measure_text`.
- Add style/theme helpers.
- Add `icon_button`, `tabs`, `swatch_grid`, `item_grid`, and `slider`.
- Add focus/navigation for custom states.
- Add `scroll_area` or at least a simple clipped scroll grid.
- Add custom state lifecycle helpers.
- Add `mod.assets.load_spritesheet` or a native cosmetics asset API exposed to Lua.

Done when:

- A custom state can be built without hand-positioning every text button.
- Mouse, keyboard, and controller navigation all work.
- The cosmetics screen can use real widgets instead of one-off UI code.

### Phase 3: Expanded Colors And Effects

- Add custom color/effect registry.
- Apply render-time colors in the preview/custom renderer.
- Avoid writing animated cosmetic colors into player struct bytes that rollback currently snapshots.
- Keep original palette indices as fallback.
- Add enough presets that the color UI is worth using.

Done when:

- Players can choose many more colors.
- Animated effects render correctly.
- Gameplay state checks are unchanged.

### Phase 4: Customization Menu

- Add hanger icon to the main menu.
- Add `cosmetics_state`.
- Start with the existing Lua custom-state API if needed.
- Move to the upgraded UI widgets once available.
- Add character preview.
- Add category filters.
- Add item selection.
- Add `X` removal item per category.
- Add color/effect picker.
- Save profile edits.

Done when:

- A player can enter the screen, equip/remove cosmetics, change colors, preview the result, save, and return.

Current implementation status:

- Not implemented in the current tree after rollback to the UI API foundation work.

### Phase 5: Cosmetic Overlay Renderer

- Load cosmetic item sprites into a cosmetics atlas.
- Hook or extend atlas flushing so cosmetics atlas batches draw every frame.
- First prototype: draw cosmetics as top-level overlay for validation.
- Final path: add draw hooks around player rendering.
- Draw hats/masks/torso/pants/shoes at anchor positions.
- Add layer ordering.
- Add simple attachment motion for hats and similar items.

Done when:

- Equipped cosmetics appear in match and menu preview.
- They follow player facing and animation.
- The final renderer can layer items behind/in front of body parts instead of always drawing over everything.
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

1. Document and expose the missing UI helpers we already have, especially `begin_overlay` and `end_overlay`.
2. Add the smallest useful UI upgrades: `rect`, `icon_button`, `tabs`, `swatch_grid`, and `slider`.
3. Add `cosmetics_ext.c/h` with profile save/load.
4. Add a few hardcoded color/effect presets.
5. Add the hanger button and `cosmetics_state`.
6. Add a preview and item/category UI using placeholder cosmetics.
7. Add cosmetics asset loading into a separate atlas.
8. Add a top-overlay gameplay prototype for hats and masks.
9. Move hats/masks into native player-render hooks after the data/UI are proven.
10. Once overlays are stable, add torso/pants/shoes.
11. Then tackle strict per-player spritesheets.

This order gets visible customization working quickly while avoiding the riskiest part, which is replacing the player renderer before the cosmetic model, asset pipeline, and UI are proven.
