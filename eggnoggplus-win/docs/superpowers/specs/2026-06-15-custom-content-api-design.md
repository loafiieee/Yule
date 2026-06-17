# Custom Content API — Design (v1)

Status: draft for review
Date: 2026-06-15
Scope: EGGNOGG+ SDL2 mod framework

## 1. Vision and framing

Modders should be able to add and override **gameplay** — not just visuals/audio.
The end goal is a general "control anything" layer: spawn new objects, define new
weapons/abilities, make interactive map tiles, and eventually override base-game
behavior (e.g. new sword/melee moves). The framework already covers visuals
(`mod.ui`, `mod.texture`, `mod.font`, `mod.anim`), audio (`mod.audio`), custom
maps (`custom_maps`), and gameplay *reads* + input override + rollback blobs
(`mod.game`). What is missing is the ability to **write the live game world** and
**attach custom behavior** to it.

This is several subsystems (entities, weapons, interactive tiles, abilities,
native-behavior override) sharing one spine: a deterministic, tick-time
**World Control** API. We build that spine once, then add front-ends.

### v1 deliverable (this spec)

1. **World Control API** (`mod.game.world`) — the general read/write/effect layer.
2. **Custom tile behaviors** (`mod.game.register_tile`) — the first front-end.
3. **Ability POC** — a dash + double-jump mod built purely on World Control + input.
4. **Online safety gate** — custom behaviors are disabled (with a warning) during
   GGPO online matches.
5. **Example content** — a custom map with a bounce tile + a spike tile, and the
   dash/double-jump ability mod.
6. **Docs** — a new MODDING.md section.

### Explicitly deferred (NOT in v1)

- **Entities/weapons front-end** (spawn/define custom objects with `update`/`render`,
  custom sword/projectile/powerup/enemy). Next sub-project, same foundation.
- **Online / rollback integration** for custom content. v1 is local-first by
  decision; custom world-writes are not in the rollback blob, hence the safety gate.
- **Native-behavior override** (intercepting the native attack/state machine to add
  melee moves). The far end of the vision; the design must not preclude it, but v1
  does not implement it.

## 2. Goals / non-goals

**Goals**
- A small, general, deterministic API to read and write player/tile/world state
  from tick-time Lua, plus effect helpers (kill, knockback, teleport, bounce, give
  sword, set facing/state).
- A tile-behavior registry that fires `on_enter`/`on_stay`/`on_exit` as players
  overlap registered tile ids, integrated with custom maps.
- Two convincing, base-game-rooted examples (bounce/spike tiles; dash/double-jump).
- No silent online desyncs: custom behaviors are inert during online matches.

**Non-goals (v1)**
- Spawning brand-new simulated entities.
- Rollback/netcode safety for custom content.
- Replacing native player/attack logic.

## 3. Architecture

```
mod.on_tick (runs inside native game_update, deterministic in local play)
        │
        ▼
  World Control core (world_api.c)
   ├── player handles  ── read/write PLAYER_OFS_* on the two player structs
   ├── tile access     ── read/write tilemap cells (reuses room geometry)
   ├── effect helpers  ── kill / knockback / teleport / bounce / set_* 
   └── tile-behavior registry
         └── per-tick overlap scan: for each player, map world pos → tile cell,
             read cell id, dispatch on_enter/on_stay/on_exit for registered ids
        │
        ▼
  online gate: if ggpo online match active → skip dispatch + warn once
```

### 3.1 Layer 1 — World Control (`mod.game.world`)

A new module `world_api.c` / `world_api.h` holds the core read/write/effect
helpers (pure C over known offsets) so `lua_manager.c` stays focused on binding.
`lua_manager.c` registers the Lua bindings into the existing `mod.game` table
(under `mod.game.world`) because that is where the mod environment is built and
where the player/thing pointers already live.

Player handle (a light Lua table or userdata keyed by player index 0/1):

```lua
local p = mod.game.world.player(0)        -- handle for player 0
p.index                                    -- 0 or 1
-- reads (snapshot of current native fields)
p.x, p.y, p.vx, p.vy                       -- PLAYER_OFS_X/Y/VX/VY
p.facing                                   -- PLAYER_OFS_FACING_SIGN (-1/+1)
p.state_id, p.state_timer                  -- PLAYER_OFS_STATE_ID/STATE_TIMER
p.has_sword                                -- PLAYER_OFS_HAS_SWORD
p.grounded, p.ceiling, p.wall_left, p.wall_right  -- from PLAYER_OFS_COLLISION_FLAGS
p.cmd_bits, p.prev_cmd_bits                -- PLAYER_OFS_CMD_BITS / PREV_CMD_BITS
p.room                                      -- PLAYER_OFS_ROOM
-- writes (methods; each writes the native struct immediately)
p:set_pos(x, y)
p:set_velocity(vx, vy)        -- (nil keeps current component)
p:add_velocity(dvx, dvy)
p:set_facing(sign)
p:set_has_sword(bool)
p:teleport(x, y)              -- set_pos + zero prev_x/prev_y to avoid interp tearing
p:bounce(vy)                  -- convenience: set_velocity(_, vy)
p:knockback(dvx, dvy)
p:kill()                      -- trigger native death (see §6 native notes)
p:hurt()                      -- alias of kill in v1 (eggnogg is one-hit)
```

World/tile helpers:

```lua
mod.game.world.tile(col, row [,room]) -> { id, frame, arg, exists, x, y, ... }  -- read (wraps room_tile)
mod.game.world.set_tile(col, row, { id=, frame=, arg= } [,room]) -> bool         -- write a cell
mod.game.world.tile_at_world(world_x, world_y) -> col, row, room | nil           -- inverse mapping
mod.game.world.player_cell(player_index) -> col, row, room | nil                  -- player's occupied cell(s)
mod.game.world.online_active() -> bool                                           -- is a GGPO match running
```

All writes are plain memory writes to known offsets, executed synchronously.
Intended call site is `mod.on_tick` (deterministic, inside the native update);
calling from `mod.on_frame` is allowed but discouraged (renders between ticks).

### 3.2 Layer 2a — Custom tile behaviors (`mod.game.register_tile`)

```lua
mod.game.register_tile({
  id = 200,                       -- tilemap cell id this behavior is bound to
  on_enter = function(p, t) end,  -- player p first overlaps a cell of this id
  on_stay  = function(p, t) end,  -- each tick while overlapping
  on_exit  = function(p, t) end,  -- player leaves all cells of this id
})
-- p: a world player handle (see above)
-- t: { col, row, room, id, frame, arg } for the triggering cell
```

Per-tick dispatch (framework, inside the tick, after `on_tick` callbacks):
1. If an online match is active → skip entirely (see §3.3).
2. For each player still in-game:
   - Compute the set of tile cells the player's bounding box overlaps
     (`player_cell`, expanded to the player's box using tile_w/tile_h).
   - For each overlapped cell whose `id` has a registered behavior:
     - if it was not overlapped last tick → `on_enter`
     - else → `on_stay`
   - For ids overlapped last tick but not this tick → `on_exit`.
3. Store this tick's overlap set per player for next-tick edge detection.

The registry is keyed by tile id. Registration is per-mod; unloading a mod clears
its registrations. Multiple mods may register the same id (all fire; order =
load order). Callbacks run in a pcall; an erroring callback is logged and the
mod's error count incremented (consistent with existing callback dispatch).

Integration with custom maps: map authors place the registered tile id in a
custom map (existing `custom_maps` flow). No change to map format — behavior is
attached by id at runtime. The example ships a custom map using the ids.

### 3.3 Online safety gate

Custom world-writes and tile behaviors are **not** part of the GGPO rollback blob,
so they would desync online. Therefore, while an online match is active
(`ggpo_net_active()` / equivalent), the framework:
- skips the tile-behavior dispatch entirely, and
- logs a one-shot warning per mod that registered behaviors/abilities.

`mod.game.world.online_active()` lets ability mods self-gate their own logic the
same way. (Full online support is a later sub-project that moves custom state into
the rollback blob.)

### 3.4 Ability POC (example mod, no new engine code)

Dash and double-jump are implemented entirely in Lua on top of World Control +
existing input reads (`mod.game.poll_cmds`), proving the foundation extends the
player without any tile system:

- **Double jump:** on `on_tick`, detect a fresh JUMP press (`cmd_bits & CMD_JUMP`
  rising edge) while airborne (`not p.grounded`) and a per-player "jumps used"
  counter < 2; on trigger, `p:set_velocity(nil, jump_vy)` and increment. Reset the
  counter when grounded.
- **Dash:** detect a double-tap of LEFT/RIGHT (or a chosen button) and apply a
  horizontal velocity burst for N ticks via `p:set_velocity(dash_vx*p.facing, _)`.

Both self-gate via `mod.game.world.online_active()`.

## 4. API summary (Lua)

| Call | Purpose |
| --- | --- |
| `mod.game.world.player(i)` | player handle (reads + write methods) |
| `mod.game.world.tile(col,row[,room])` | read a tile cell |
| `mod.game.world.set_tile(col,row,t[,room])` | write a tile cell |
| `mod.game.world.tile_at_world(x,y)` | world pos → col,row,room |
| `mod.game.world.player_cell(i)` | player's occupied cell |
| `mod.game.world.online_active()` | true during GGPO match |
| `mod.game.register_tile{ id, on_enter, on_stay, on_exit }` | bind behavior to a tile id |

Player handle writes: `set_pos`, `set_velocity`, `add_velocity`, `set_facing`,
`set_has_sword`, `teleport`, `bounce`, `knockback`, `kill`/`hurt`.

## 5. Data flow & determinism

- All world-writes happen at tick time (`mod.on_tick`), inside the native
  `game_update`. In **local** play there is a single timeline, so behavior is
  deterministic for replay/score purposes.
- Tile dispatch runs once per tick, after mod `on_tick` callbacks, before the
  frame is presented.
- **Online:** dispatch and ability logic are gated off (see §3.3). No custom state
  enters the rollback blob in v1.

## 6. Native integration points (grounding)

Confirmed available (offsets/addresses already defined in `lua_manager.c`):
- Player fields: `PLAYER_OFS_X 0x24`, `Y 0x28`, `PREV_X 0x2C`, `PREV_Y 0x30`,
  `VX 0x34`, `VY 0x38`, `FACING_SIGN 0x98`, `STATE_ID 0x78`, `STATE_TIMER 0x8C`,
  `HAS_SWORD 0x11`, `CMD_BITS 0x9F`, `PREV_CMD_BITS 0x9E`, `COLLISION_FLAGS 0xAD`,
  `ROOM 0x9B`. Player struct stride = `THING_SIZE 0x15C`; players resolved as the
  framework already does for `snapshot`/`apply_snapshot`.
- Tile geometry: `tile_w`/`tile_h` natives, `room_w`, room info at `0x543700`.
  `room_tile(col,row)` already maps cell → bytes + world coords; v1 adds the
  inverse (world → cell) using the same geometry, plus a cell writer.
- Online state: existing `ggpo_net_active()` (hooks/ggpo).

To resolve during implementation (one item):
- **`kill()` trigger.** The clean primitives (`set_pos/velocity/facing/has_sword/
  teleport/bounce/knockback`) are immediate memory writes and fully feasible now.
  Killing a player needs the native death path: set the death `STATE_ID`, or set a
  death bit in `EVENT_FLAGS 0x70` / `PENDING_EVENT_FLAGS 0x84`, mirroring what a
  sword hit does. Implementation will confirm the exact trigger by inspecting the
  native sword-hit handler in ghidra. If a clean trigger proves costly, the spike
  example can fall back to a hard teleport-to-respawn; the **bounce** example does
  not depend on `kill` and is the primary proof.

## 7. Example content (ships with v1)

1. **`mods/tile_demo/`** — registers tile id(s) for a **bounce pad** (`on_stay`:
   `p:bounce(launch_vy)` when grounded on it) and a **spike** (`on_enter`:
   `p:kill()`), plus a small **custom map** placing them. Demonstrates write +
   effect primitives, rooted in the base game's tile/map system.
2. **`mods/abilities_demo/`** — dash + double-jump (§3.4), pure World Control +
   input. Demonstrates extending player behavior with no new engine subsystem.

Both demos call `mod.game.world.online_active()` and no-op during online matches.

## 8. Testing / verification

- Build clean (mingw32) and deploy (copy `build\SDL2_test.dll` → `SDL2.dll`,
  verified by hash; close the game if the DLL is locked).
- Bounce tile: standing on the tile launches the player; repeatable; feels stable.
- Spike tile: touching it kills/respawns the player.
- Dash + double-jump behave correctly and reset on landing.
- Online gate: while in a GGPO match, tile behaviors and abilities are inert and a
  one-shot warning is logged; no new desyncs in `mods/modframework.log`.
- Unloading the demo mods clears their tile registrations (no leftover behavior).

## 9. Files touched (anticipated)

- `world_api.c` / `world_api.h` — new: world read/write/effect helpers + tile
  registry + per-tick dispatch.
- `lua_manager.c` — register `mod.game.world.*` and `mod.game.register_tile`
  bindings; call the tile dispatch from the existing tick path; clear registry on
  mod unload.
- `compile.sh` + build command — add `world_api.c`.
- `MODDING.md` — new "Gameplay content: World Control + custom tiles" section.
- `mods/tile_demo/`, `mods/abilities_demo/` — example mods (+ a custom map).

## 10. Future (documented next steps, not v1)

- **Entities/weapons (Layer 2b):** `mod.game.define_entity{update,render,...}` +
  spawn/despawn; custom sword/projectile/powerup/enemy.
- **Online/rollback:** move custom content state into the rollback blob so behaviors
  survive replay; remove the online gate.
- **Native-behavior override:** intercept the native attack/state machine to add or
  replace melee moves — the most ambitious end of the "do anything" vision.

## 11. Implementation notes (v1 as built, 2026-06-15)

Deviations from the design above, decided during implementation:

- **Location:** built inside `lua_manager.c` (not a separate `world_api.c`). The
  player resolution (`game_get_player_ptr`), all `PLAYER_OFS_*`, tile helpers, the
  tick path, and the mod registry are static there; a separate module would have
  forced exposing or duplicating them. Bindings live under `mod.game.world.*` and
  `mod.game.register_tile`.
- **Tile detection** uses the engine's authoritative lookups rather than manual
  cell math: `map_coord_tile(x,y)` (`0x00434B30`, cdecl, returns the tile-id byte)
  and `is_pos_solid` for the solid test. Each tick the dispatch samples the tile id
  at the player point, just below the feet, and just above, and fires behaviors
  registered by **tile id** (0..255) or by **`solid=true`**. There is no cell-coord
  registry; instead `mod.game.world.player_tile(i)` is a discovery helper (the
  `tile_demo` inspector logs the id under the player).
- **`kill()`** calls the native `player_die(player_ptr)` (`0x00422830`, cdecl),
  confirmed via disassembly (checks `state_id != 9`, sets dying state 8, drops the
  sword, updates leader/loser/scoring).
- **No custom glyph map authored.** Custom maps use a fixed 33×12 glyph template
  set, so behaviors attach to tile ids that work on any map (vanilla or custom).
  The `tile_demo` ground-bounce uses `solid=true` and needs no specific id.
- **Player handle** read fields are a snapshot at `world.player(i)` call time;
  write methods (`p:set_velocity`, `p:bounce`, `p:kill`, ...) act on live memory.

Shipped: World Control API, `register_tile` + per-tick dispatch + online gate +
unload cleanup, and demos `mods/abilities_demo` (dash + double jump) and
`mods/tile_demo` (ground-bounce + configurable bounce/spike + tile inspector).
Built clean (`-Wall`), deployed, and the demo Lua byte-compiles under LuaJIT.
Not yet play-tested in-engine.
