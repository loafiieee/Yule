# AI Opponent / Singleplayer Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Singleplayer EGGNOGG+ via a learned (neuroevolution-trained) AI opponent, entered through a mode-cycling PLAY button on the main menu.

**Architecture:** A thin C bridge in the SDL2-proxy framework (menu mode button + `ai_match` flag exposed to Lua) and a Lua mod `mods/ai_opponent/` containing all ML: a pure-Lua MLP policy, feature extraction, a scripted recovery scaffold, and an in-engine self-play neuroevolution trainer using `set_input` + `simulate_ticks` + state blobs.

**Tech Stack:** C (mingw32, 32-bit proxy DLL), LuaJIT 5.1 (embedded + standalone `C:\msys64\mingw32\bin\luajit.exe` for tests).

**Spec:** `docs/superpowers/specs/2026-07-01-ai-opponent-singleplayer-design.md`

## Global Constraints

- Never touch native gameplay/netcode/sync-sensitive code paths — purely additive changes only (post-revert constraint of 2026-06-18).
- The AI flag must NEVER be armed while `ggpo_net_active()` or `g_online_pending_match.active`.
- VS AI / TRAIN menu modes only appear when `lua_manager_has_bot_provider()` is true.
- All game addresses/symbols referenced below were verified in-code 2026-07-01: `MAIN_START_ACTION_PTR 0x432440`, `BTN_OFS_CENTER_X 0x10 / CENTER_Y 0x14 / WIDTH 0x20 / HEIGHT 0x24 / LABEL_PTR 0xC8 / LINK_PTR 0xE0 / ACTION_PTR 0xE4 / NOLINK_FLAG 0xBD`, button filter-tag at offset +4 (`0x11`=P1 selector, `0x12`=P2 selector).
- Build command (PowerShell, from `eggnoggplus-win/`):
  ```powershell
  $env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH
  $src = @('dllmain.c','stubs.c','hooks.c','custom_maps.c','lua_manager.c','ggpo_ext.c','ggpo_loopback.c','ggpo_local.c','ggpo_net.c','font_ext.c','texture_ext.c','log.c','net_ext.c')
  $libs = @('-lkernel32','-luser32','-lopengl32','-l:libluajit-5.1.dll.a','-lws2_32','-lwinhttp','-lcomdlg32','-lshell32','-lole32','-IC:\msys64\mingw32\include','-LC:\msys64\mingw32\lib')
  & C:\msys64\mingw32\bin\gcc.exe -m32 -shared -Wall -o build\SDL2_test.dll @src @libs
  Copy-Item build\SDL2_test.dll SDL2.dll -Force   # fails if game is running — ask user to close it
  ```
  (`fwsettings.c` no longer exists; do not add it.)
- Lua tests run standalone: `cd mods/ai_opponent; C:\msys64\mingw32\bin\luajit.exe tests/run_tests.lua` — pure-Lua libs must not reference `mod.*` at file scope; they are loaded with `dofile` (tests) and `mod.dofile` (in game) and must `return` their table.
- Lua command bits: JUMP 0x01, ATTACK 0x02, RIGHT 0x04, LEFT 0x08, UP 0x10, DOWN 0x20.
- In-game verification requires the user to launch the game; batch in-game checks to minimize round-trips, and always hash-compare `SDL2.dll` vs `build\SDL2_test.dll` before blaming code for stale-DLL ghosts.

## File Structure

```
eggnoggplus-win/
  hooks.c                 (modify: menu mode button, arrows, ai_match flag, overlay)
  hooks.h                 (modify: new externs)
  lua_manager.c           (modify: LoadedMod.bot_provider, 3 new bindings, native_state fields, has_bot_provider)
  lua_manager.h           (modify: lua_manager_has_bot_provider decl)
  mods/ai_opponent/
    mod.json
    config.cfg            (difficulty, train speed, overlay)
    main.lua              (glue: provider registration, on_tick dispatch, overlay)
    lib/nn.lua            (pure: MLP, serialize, mutate/crossover, rng)
    lib/actions.lua       (pure: discrete action table -> cmd masks)
    lib/features.lua      (pure: context table -> input vector)
    lib/policy.lua        (pure: humanizer wrapper: delay + epsilon)
    lib/evo.lua           (pure: population, selection, next_gen)
    lib/codec.lua         (pure-ish: chunked string <-> storage helpers)
    bot.lua               (game-facing: build context, probes, drive one player)
    trainer.lua           (game-facing: episodes, fitness, generations, TRAIN overlay)
    data/weights_easy.lua / weights_normal.lua / weights_hard.lua  (shipped checkpoints, `return "<serialized>"`)
    tests/run_tests.lua   (assert harness; runs all pure-lib tests)
    tests/test_nn.lua, test_evo.lua, test_actions.lua, test_features.lua, test_policy.lua, test_codec.lua
```

Interface contracts used across tasks (fixed now):

- `NN.new(sizes, seed) -> net`, `NN.forward(net, x) -> vector`, `NN.argmax(v) -> index`,
  `NN.copy(net)`, `NN.mutate(net, rate, scale, rand) -> new net`, `NN.crossover(a, b, rand) -> new net`,
  `NN.serialize(net) -> string`, `NN.deserialize(s) -> net|nil,err`, `NN.rng_new(seed) -> fn() -> [0,1)`,
  `NN.param_count(sizes) -> int`.
- `A.LIST` (array of `{name, mask}`), `A.COUNT`, `A.mask(i) -> int`.
- `F.N_INPUTS` (= 28), `F.extract(ctx) -> array[28]` where ctx =
  `{ snap, my_room, enemy_room, goal_dir, leader, probes = {ahead_near, ahead_far, hazard_ahead, ground_front, gap_below, above} }`.
- `Policy.new(net, {react_delay, epsilon, seed}) -> p`, `Policy.decide(p, feats) -> action_index`, `Policy.reset(p)`.
- `EVO.new(n, sizes, seed) -> pop` (`pop.nets`, `pop.gen`), `EVO.next_gen(pop, fitnesses, cfg, rand) -> pop`.
- `Codec.store(storage, key, str)`, `Codec.load(storage, key) -> string|nil`.
- C: `hooks_arm_ai_match(int ai_player, int training)`, `hooks_clear_ai_match(void)`,
  `hooks_get_ai_match(int* active, int* ai_player, int* training)`, `int lua_manager_has_bot_provider(void)`.
- Lua: `mod.game.register_bot_provider()`, `mod.game.ai_match() -> nil | {active=true, ai_player=0|1, training=bool}`,
  `mod.game.native_state()` gains `score_p0, score_p1, score_target, leader` (leader: 0|1|-1).

---

### Task 1: Pure-Lua NN library + test harness

**Files:**
- Create: `mods/ai_opponent/lib/nn.lua`
- Create: `mods/ai_opponent/tests/run_tests.lua`
- Create: `mods/ai_opponent/tests/test_nn.lua`

**Interfaces:** Produces the `NN` contract above. No dependencies.

- [ ] **Step 1: Write the test harness and failing NN tests**

`tests/run_tests.lua`:
```lua
-- Standalone test runner: luajit tests/run_tests.lua  (cwd = mods/ai_opponent)
local files = { "tests/test_nn.lua", "tests/test_actions.lua", "tests/test_features.lua",
                "tests/test_policy.lua", "tests/test_evo.lua", "tests/test_codec.lua" }
local failed, ran = 0, 0
_G.check = function(cond, msg)
  ran = ran + 1
  if not cond then failed = failed + 1; print("FAIL: " .. (msg or "?")) end
end
for _, f in ipairs(files) do
  local fh = io.open(f, "r")
  if fh then fh:close(); print("== " .. f); dofile(f) end
end
print(string.format("%d checks, %d failed", ran, failed))
os.exit(failed == 0 and 0 or 1)
```

`tests/test_nn.lua`:
```lua
local NN = dofile("lib/nn.lua")

check(NN.param_count({3, 4, 2}) == 3*4+4 + 4*2+2, "param_count")

local net = NN.new({4, 8, 3}, 123)
check(#net.w == NN.param_count({4, 8, 3}), "new allocates all params")

local out = NN.forward(net, {0.1, -0.5, 1.0, 0.0})
check(#out == 3, "forward output width")
local out2 = NN.forward(net, {0.1, -0.5, 1.0, 0.0})
for i = 1, 3 do check(out[i] == out2[i], "forward deterministic " .. i) end

check(NN.argmax({0.1, 5.0, -2.0}) == 2, "argmax")

local s = NN.serialize(net)
local net2, err = NN.deserialize(s)
check(net2 ~= nil, "deserialize ok: " .. tostring(err))
local o1, o3 = NN.forward(net, {1,1,1,1}), NN.forward(net2, {1,1,1,1})
for i = 1, 3 do check(math.abs(o1[i]-o3[i]) < 1e-6, "roundtrip forward " .. i) end
check(NN.deserialize("garbage") == nil, "deserialize rejects garbage")

local rand = NN.rng_new(7)
local mut = NN.mutate(net, 1.0, 0.1, rand)
check(mut ~= net and mut.w ~= net.w, "mutate returns copy")
local diff = 0
for i = 1, #net.w do if mut.w[i] ~= net.w[i] then diff = diff + 1 end end
check(diff > #net.w * 0.9, "mutate rate=1 changes ~all weights")

local a, b = NN.new({2,2}, 1), NN.new({2,2}, 2)
local c = NN.crossover(a, b, NN.rng_new(3))
local from_a, from_b = 0, 0
for i = 1, #c.w do
  if c.w[i] == a.w[i] then from_a = from_a + 1 end
  if c.w[i] == b.w[i] then from_b = from_b + 1 end
end
check(from_a > 0 and from_b > 0, "crossover mixes parents")
```

- [ ] **Step 2: Run to verify failure**

Run: `cd mods/ai_opponent; C:\msys64\mingw32\bin\luajit.exe tests/run_tests.lua`
Expected: error / FAIL (lib/nn.lua missing).

- [ ] **Step 3: Implement `lib/nn.lua`**

```lua
-- Feedforward MLP on a flat weight array. Pure Lua 5.1. No globals, no mod.* refs.
local NN = {}

function NN.rng_new(seed)
  local s = (seed or 42) % 2147483648
  if s <= 0 then s = s + 2147483647 end
  return function()
    s = (1103515245 * s + 12345) % 2147483648
    return s / 2147483648
  end
end

function NN.param_count(sizes)
  local n = 0
  for i = 2, #sizes do n = n + sizes[i-1] * sizes[i] + sizes[i] end
  return n
end

function NN.new(sizes, seed)
  local net = { sizes = {}, w = {} }
  for i = 1, #sizes do net.sizes[i] = sizes[i] end
  local rand = NN.rng_new(seed)
  for i = 1, NN.param_count(sizes) do net.w[i] = (rand() * 2 - 1) * 0.5 end
  return net
end

function NN.forward(net, x)
  local sizes, w = net.sizes, net.w
  local a = x
  local k = 0
  for layer = 2, #sizes do
    local nin, nout = sizes[layer-1], sizes[layer]
    local out = {}
    for j = 1, nout do
      local sum = 0
      local base = k + (j - 1) * nin
      for i = 1, nin do sum = sum + w[base + i] * a[i] end
      out[j] = sum
    end
    k = k + nin * nout
    for j = 1, nout do out[j] = out[j] + w[k + j] end
    k = k + nout
    if layer < #sizes then
      for j = 1, nout do
        local t = out[j]
        out[j] = t > 0 and t or 0.01 * t   -- leaky ReLU
      end
    end
    a = out
  end
  return a
end

function NN.argmax(v)
  local bi, bv = 1, v[1]
  for i = 2, #v do if v[i] > bv then bv, bi = v[i], i end end
  return bi
end

function NN.copy(net)
  local c = { sizes = {}, w = {} }
  for i = 1, #net.sizes do c.sizes[i] = net.sizes[i] end
  for i = 1, #net.w do c.w[i] = net.w[i] end
  return c
end

function NN.mutate(net, rate, scale, rand)
  local m = NN.copy(net)
  for i = 1, #m.w do
    if rand() < rate then m.w[i] = m.w[i] + (rand() * 2 - 1) * scale end
  end
  return m
end

function NN.crossover(a, b, rand)
  local c = NN.copy(a)
  for i = 1, #c.w do
    if rand() < 0.5 then c.w[i] = b.w[i] end
  end
  return c
end

function NN.serialize(net)
  local parts = { "nn1", table.concat(net.sizes, ",") }
  local ws = {}
  for i = 1, #net.w do ws[i] = string.format("%.9g", net.w[i]) end
  parts[3] = table.concat(ws, " ")
  return table.concat(parts, "|")
end

function NN.deserialize(s)
  if type(s) ~= "string" then return nil, "not a string" end
  local tag, sizes_s, w_s = s:match("^(nn1)|([%d,]+)|(.+)$")
  if not tag then return nil, "bad format" end
  local net = { sizes = {}, w = {} }
  for num in sizes_s:gmatch("%d+") do net.sizes[#net.sizes + 1] = tonumber(num) end
  if #net.sizes < 2 then return nil, "bad sizes" end
  for num in w_s:gmatch("%S+") do
    local v = tonumber(num)
    if not v then return nil, "bad weight" end
    net.w[#net.w + 1] = v
  end
  if #net.w ~= NN.param_count(net.sizes) then return nil, "weight count mismatch" end
  return net
end

return NN
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cd mods/ai_opponent; C:\msys64\mingw32\bin\luajit.exe tests/run_tests.lua`
Expected: `N checks, 0 failed` (only nn tests exist yet; harness skips missing files).

- [ ] **Step 5: Commit**

```bash
git add mods/ai_opponent/lib/nn.lua mods/ai_opponent/tests/
git commit -m "feat(ai_opponent): pure-Lua MLP with serialization and evolution ops"
```

---

### Task 2: Actions + features libraries

**Files:**
- Create: `mods/ai_opponent/lib/actions.lua`, `mods/ai_opponent/lib/features.lua`
- Create: `mods/ai_opponent/tests/test_actions.lua`, `mods/ai_opponent/tests/test_features.lua`

**Interfaces:**
- Consumes: nothing.
- Produces: `A.LIST/A.COUNT/A.mask(i)`; `F.N_INPUTS = 28`, `F.extract(ctx)`.

- [ ] **Step 1: Write failing tests**

`tests/test_actions.lua`:
```lua
local A = dofile("lib/actions.lua")
check(A.COUNT == 12, "12 actions")
check(A.mask(1) == 0, "idle mask 0")
local seen = {}
for i = 1, A.COUNT do
  local m = A.mask(i)
  check(m ~= nil, "mask defined " .. i)
  check(not (seen[m] and m ~= 0), "masks unique " .. i)
  seen[m] = true
  check(m % 64 == m, "mask uses only low 6 bits " .. i)  -- no MENU bit
end
check(A.mask(99) == 0, "out of range -> 0")
```

`tests/test_features.lua`:
```lua
local F = dofile("lib/features.lua")

local function fake_ctx()
  return {
    snap = {
      player = { x=100, y=50, vx=1, vy=0, facing=1, has_sword=true, grounded=true,
                 wall_left=false, wall_right=false, state_id=0, cmd_bits=0 },
      enemy  = { x=180, y=50, vx=-1, vy=0, facing=-1, has_sword=true, grounded=true,
                 wall_left=false, wall_right=false, state_id=0, cmd_bits=2 },
      nearest_sword_x = 140, nearest_sword_y = 50,
    },
    my_room = 2, enemy_room = 2, goal_dir = 1, leader = -1,
    probes = { ahead_near=0, ahead_far=1, hazard_ahead=0, ground_front=1, gap_below=0, above=0 },
  }
end

local v = F.extract(fake_ctx())
check(#v == F.N_INPUTS, "vector width == N_INPUTS")
for i = 1, #v do
  check(type(v[i]) == "number", "numeric " .. i)
  check(v[i] >= -1.001 and v[i] <= 1.001, "bounded [-1,1] at " .. i .. " = " .. tostring(v[i]))
end

-- direction sensitivity: enemy to the right -> dx positive; flip -> negative
local ctx = fake_ctx()
ctx.snap.enemy.x = 20
local v2 = F.extract(ctx)
check(v[1] > 0 and v2[1] < 0, "dx sign follows enemy side")

-- missing sword entity handled
local ctx3 = fake_ctx()
ctx3.snap.nearest_sword_x, ctx3.snap.nearest_sword_y = nil, nil
local v3 = F.extract(ctx3)
check(#v3 == F.N_INPUTS, "handles missing sword")

-- extreme values stay clamped
local ctx4 = fake_ctx()
ctx4.snap.enemy.x = 1e6; ctx4.snap.player.vx = 1e6
local v4 = F.extract(ctx4)
for i = 1, #v4 do check(v4[i] >= -1.001 and v4[i] <= 1.001, "clamped " .. i) end
```

- [ ] **Step 2: Run to verify failure** — same runner command; expect FAILs/errors.

- [ ] **Step 3: Implement**

`lib/actions.lua`:
```lua
-- Discrete action set -> native command bitmasks.
local A = {}
local J, AT, R, L, U, D = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
A.LIST = {
  { name = "idle",         mask = 0 },
  { name = "left",         mask = L },
  { name = "right",        mask = R },
  { name = "jump",         mask = J },
  { name = "jump_left",    mask = J + L },
  { name = "jump_right",   mask = J + R },
  { name = "attack",       mask = AT },
  { name = "attack_left",  mask = AT + L },
  { name = "attack_right", mask = AT + R },
  { name = "up",           mask = U },
  { name = "down",         mask = D },
  { name = "down_jump",    mask = D + J },
}
A.COUNT = #A.LIST
function A.mask(i)
  local e = A.LIST[i]
  return e and e.mask or 0
end
return A
```

`lib/features.lua`:
```lua
-- Snapshot context -> normalized feature vector. Pure; all game reads happen in bot.lua.
local F = {}
F.N_INPUTS = 28

local function clamp(v, lo, hi)
  if v ~= v or v == nil then return 0 end -- NaN/nil guard
  if v < lo then return lo elseif v > hi then return hi end
  return v
end
local function b(x) return x and 1 or 0 end

function F.extract(ctx)
  local p, e = ctx.snap.player, ctx.snap.enemy
  local o = {}
  -- relative opponent (1-4)
  o[1] = clamp((e.x - p.x) / 200, -1, 1)
  o[2] = clamp((e.y - p.y) / 100, -1, 1)
  o[3] = clamp((e.vx or 0) / 6, -1, 1)
  o[4] = clamp((e.vy or 0) / 6, -1, 1)
  -- self motion (5-6)
  o[5] = clamp((p.vx or 0) / 6, -1, 1)
  o[6] = clamp((p.vy or 0) / 6, -1, 1)
  -- facing / swords (7-10)
  o[7]  = clamp(p.facing or 1, -1, 1)
  o[8]  = clamp(e.facing or 1, -1, 1)
  o[9]  = b(p.has_sword)
  o[10] = b(e.has_sword)
  -- contact flags (11-14)
  o[11] = b(p.grounded)
  o[12] = b(e.grounded)
  o[13] = b(p.wall_left)
  o[14] = b(p.wall_right)
  -- state (15-18): normalized ids + death flags (dying=8, dead-ish=9)
  local ps, es = p.state_id or 0, e.state_id or 0
  o[15] = clamp(ps / 16, 0, 1)
  o[16] = clamp(es / 16, 0, 1)
  o[17] = b(ps == 8 or ps == 9)
  o[18] = b(es == 8 or es == 9)
  -- enemy attack button (19)
  o[19] = b(((e.cmd_bits or 0) % 4) >= 2)   -- ATTACK bit 0x02 without bit ops
  -- strategy (20-22)
  o[20] = clamp(ctx.goal_dir or 1, -1, 1)
  o[21] = clamp(ctx.leader or 0, -1, 1)
  o[22] = clamp(((ctx.my_room or 0) - (ctx.enemy_room or 0)) / 3, -1, 1)
  -- nearest loose sword (23-25)
  local sx, sy = ctx.snap.nearest_sword_x, ctx.snap.nearest_sword_y
  if sx and sy then
    o[23] = 1
    o[24] = clamp((sx - p.x) / 200, -1, 1)
    o[25] = clamp((sy - p.y) / 100, -1, 1)
  else
    o[23], o[24], o[25] = 0, 0, 0
  end
  -- tile probes (26-28 packed from 6 booleans: near/far solids, hazard; ground/gap/above)
  local pr = ctx.probes or {}
  o[26] = clamp((pr.ahead_near or 0) + 0.5 * (pr.ahead_far or 0) - 0.001, -1, 1)
  o[27] = clamp((pr.hazard_ahead or 0) + 0.5 * (pr.gap_below or 0), -1, 1)
  o[28] = clamp((pr.ground_front or 0) - (pr.above or 0), -1, 1)
  return o
end

return F
```

- [ ] **Step 4: Run tests to verify pass** — expect 0 failed.
- [ ] **Step 5: Commit** — `git add mods/ai_opponent; git commit -m "feat(ai_opponent): action table and feature extraction"`

---

### Task 3: Policy humanizer + evolution + codec

**Files:**
- Create: `mods/ai_opponent/lib/policy.lua`, `lib/evo.lua`, `lib/codec.lua`
- Create: `tests/test_policy.lua`, `tests/test_evo.lua`, `tests/test_codec.lua`

**Interfaces:**
- Consumes: `NN` (Task 1). Policy takes `NN`-shaped nets; evo produces populations of them.
- Produces: `Policy.new/decide/reset`, `EVO.new/next_gen`, `Codec.store/load`.

- [ ] **Step 1: Write failing tests**

`tests/test_policy.lua`:
```lua
local NN = dofile("lib/nn.lua")
local Policy = dofile("lib/policy.lua")

local net = NN.new({4, 8, 5}, 11)
local p = Policy.new(net, { react_delay = 0, epsilon = 0, seed = 1 })
local a1 = Policy.decide(p, {1, 0, 0, 0})
check(a1 >= 1 and a1 <= 5, "action in range")
check(Policy.decide(p, {1, 0, 0, 0}) == a1, "deterministic with eps=0")

-- reaction delay: with delay=3, first 3 decisions use the OLDEST queued features
local pd = Policy.new(net, { react_delay = 3, epsilon = 0, seed = 1 })
local first = Policy.decide(pd, {1, 0, 0, 0})
local second = Policy.decide(pd, {0, 1, 0, 0})  -- still sees {1,0,0,0}
check(second == first, "delayed observations")

-- epsilon=1 gives random actions across full range eventually
local pr = Policy.new(net, { react_delay = 0, epsilon = 1, seed = 2 })
local seen = {}
for i = 1, 200 do seen[Policy.decide(pr, {0, 0, 0, 0})] = true end
local n = 0; for _ in pairs(seen) do n = n + 1 end
check(n >= 3, "epsilon explores")

Policy.reset(pd)
check(#pd.fifo == 0, "reset clears fifo")
```

`tests/test_evo.lua`:
```lua
local NN = dofile("lib/nn.lua")
local EVO = dofile("lib/evo.lua")

local pop = EVO.new(8, {4, 6, 3}, 5)
check(#pop.nets == 8 and pop.gen == 0, "population init")

-- fitness = -index so net 1 is best; elites must survive verbatim
local fit = {}
for i = 1, 8 do fit[i] = -i end
local best_serial = NN.serialize(pop.nets[1])
local next_pop = EVO.next_gen(pop, fit, { elites = 2, mut_rate = 0.1, mut_scale = 0.2 }, NN.rng_new(9))
check(next_pop.gen == 1, "generation increments")
check(#next_pop.nets == 8, "size preserved")
check(NN.serialize(next_pop.nets[1]) == best_serial, "elite #1 preserved")

-- toy convergence: evolve weights toward output[1] high on fixed input
local pop2 = EVO.new(16, {2, 4, 2}, 1)
local function score(net) local o = NN.forward(net, {1, -1}); return o[1] - o[2] end
local best0
for g = 1, 30 do
  local f = {}
  for i = 1, 16 do f[i] = score(pop2.nets[i]) end
  if g == 1 then best0 = math.max(unpack(f)) end
  pop2 = EVO.next_gen(pop2, f, { elites = 2, mut_rate = 0.3, mut_scale = 0.3 }, NN.rng_new(g))
end
local fin = {}
for i = 1, 16 do fin[i] = score(pop2.nets[i]) end
check(math.max(unpack(fin)) > best0, "fitness improves over 30 generations")
```

`tests/test_codec.lua`:
```lua
local Codec = dofile("lib/codec.lua")

-- fake storage backed by a table (same get/set/delete contract as mod storage)
local kv = {}
local storage = {
  get = function(k, d) local v = kv[k]; if v == nil then return d end; return v end,
  set = function(k, v) kv[k] = v; return true end,
  delete = function(k) kv[k] = nil; return true end,
}

local long = string.rep("abcdefghij", 1000) .. "END"   -- 10,003 chars, forces chunking
check(Codec.store(storage, "ck", long), "store ok")
check(Codec.load(storage, "ck") == long, "roundtrip long")
check(Codec.load(storage, "missing") == nil, "missing -> nil")
check(Codec.store(storage, "ck", "short"), "overwrite shrinks")
check(Codec.load(storage, "ck") == "short", "roundtrip short after shrink")
```

- [ ] **Step 2: Run to verify failure** — expect FAILs.

- [ ] **Step 3: Implement**

`lib/policy.lua`:
```lua
-- Inference wrapper: reaction latency (feature FIFO) + epsilon action noise.
local NN = dofile("lib/nn.lua")
local Policy = {}

function Policy.new(net, opts)
  opts = opts or {}
  return {
    net = net,
    delay = opts.react_delay or 0,
    eps = opts.epsilon or 0,
    rand = NN.rng_new(opts.seed or 1),
    fifo = {},
    n_actions = net.sizes[#net.sizes],
  }
end

function Policy.reset(p)
  p.fifo = {}
end

function Policy.decide(p, feats)
  p.fifo[#p.fifo + 1] = feats
  while #p.fifo > p.delay + 1 do table.remove(p.fifo, 1) end
  local use = p.fifo[1]
  if p.eps > 0 and p.rand() < p.eps then
    return 1 + math.floor(p.rand() * p.n_actions)
  end
  return NN.argmax(NN.forward(p.net, use))
end

return Policy
```

Note: `dofile("lib/nn.lua")` works standalone. In-game, `bot.lua`/`main.lua` load libs via
`mod.dofile` and pass tables around — so `policy.lua` must ALSO work there. To keep one
loader path, in-game code sets `_G.dofile` alias? **No.** Instead: in-game `main.lua` does
`package_stub = mod.dofile` — but sandbox may not expose `dofile` at all. **Resolution
(binding decision for all game-facing files):** `policy.lua` receives `NN` via parameter
instead of loading it. Change signature: `Policy.init(NN_table)` must be called once before
`Policy.new`. Concretely:

```lua
-- lib/policy.lua (final form)
local Policy = { _NN = nil }
function Policy.init(NN) Policy._NN = NN end
function Policy.new(net, opts)
  opts = opts or {}
  return {
    net = net,
    delay = opts.react_delay or 0,
    eps = opts.epsilon or 0,
    rand = Policy._NN.rng_new(opts.seed or 1),
    fifo = {},
    n_actions = net.sizes[#net.sizes],
  }
end
function Policy.reset(p) p.fifo = {} end
function Policy.decide(p, feats)
  local NN = Policy._NN
  p.fifo[#p.fifo + 1] = feats
  while #p.fifo > p.delay + 1 do table.remove(p.fifo, 1) end
  local use = p.fifo[1]
  if p.eps > 0 and p.rand() < p.eps then
    return 1 + math.floor(p.rand() * p.n_actions)
  end
  return NN.argmax(NN.forward(p.net, use))
end
return Policy
```
Tests call `Policy.init(NN)` first (update test accordingly). The same
init-injection pattern applies to `evo.lua` (`EVO.init(NN)`).

`lib/evo.lua`:
```lua
-- (mu+lambda)-style GA with elitism and tournament selection.
local EVO = { _NN = nil }
function EVO.init(NN) EVO._NN = NN end

function EVO.new(n, sizes, seed)
  local pop = { nets = {}, gen = 0, sizes = sizes }
  for i = 1, n do pop.nets[i] = EVO._NN.new(sizes, seed * 7919 + i) end
  return pop
end

local function ranked_indices(fit)
  local idx = {}
  for i = 1, #fit do idx[i] = i end
  table.sort(idx, function(a, b) return fit[a] > fit[b] end)
  return idx
end

local function tournament(fit, rand, k)
  local best = nil
  for _ = 1, k do
    local c = 1 + math.floor(rand() * #fit)
    if not best or fit[c] > fit[best] then best = c end
  end
  return best
end

function EVO.next_gen(pop, fit, cfg, rand)
  local NN = EVO._NN
  local n = #pop.nets
  local elites = cfg.elites or 2
  local order = ranked_indices(fit)
  local out = { nets = {}, gen = pop.gen + 1, sizes = pop.sizes }
  for i = 1, math.min(elites, n) do
    out.nets[i] = NN.copy(pop.nets[order[i]])
  end
  while #out.nets < n do
    local pa = pop.nets[tournament(fit, rand, 3)]
    local pb = pop.nets[tournament(fit, rand, 3)]
    local child = NN.crossover(pa, pb, rand)
    out.nets[#out.nets + 1] = NN.mutate(child, cfg.mut_rate or 0.1, cfg.mut_scale or 0.2, rand)
  end
  return out
end

return EVO
```

`lib/codec.lua`:
```lua
-- Chunked string storage: values kept under CHUNK chars to stay friendly to the
-- line-based storage backend. Layout: key.."#n" = chunk count, key.."#i" = chunk i.
local Codec = {}
local CHUNK = 2000

function Codec.store(storage, key, str)
  local old_n = tonumber(storage.get(key .. "#n", 0)) or 0
  local n = math.ceil(#str / CHUNK)
  if n == 0 then n = 1 end
  for i = 1, n do
    local part = str:sub((i - 1) * CHUNK + 1, i * CHUNK)
    if not storage.set(key .. "#" .. i, part) then return false end
  end
  for i = n + 1, old_n do storage.delete(key .. "#" .. i) end
  return storage.set(key .. "#n", tostring(n))
end

function Codec.load(storage, key)
  local n = tonumber(storage.get(key .. "#n"))
  if not n or n < 1 then return nil end
  local parts = {}
  for i = 1, n do
    local part = storage.get(key .. "#" .. i)
    if type(part) ~= "string" then return nil end
    parts[i] = part
  end
  return table.concat(parts)
end

return Codec
```

- [ ] **Step 4: Run tests to verify pass** — `luajit tests/run_tests.lua`, expect 0 failed.
- [ ] **Step 5: Commit** — `git commit -m "feat(ai_opponent): policy humanizer, GA evolution, chunked codec"`

---

### Task 4: C bridge — AI match flag + Lua bindings

**Files:**
- Modify: `hooks.c` (flag storage + accessors + clear-on-menu)
- Modify: `hooks.h` (declarations)
- Modify: `lua_manager.c` (`LoadedMod.bot_provider`, `register_bot_provider`, `ai_match`, `native_state` score/leader fields, `lua_manager_has_bot_provider`)
- Modify: `lua_manager.h` (declaration)

**Interfaces:**
- Produces (C): `hooks_arm_ai_match(int ai_player, int training)`, `hooks_clear_ai_match(void)`, `hooks_get_ai_match(int*, int*, int*)`, `int lua_manager_has_bot_provider(void)`.
- Produces (Lua): `mod.game.register_bot_provider()`, `mod.game.ai_match()`, `native_state().score_p0/.score_p1/.score_target/.leader`.

- [ ] **Step 1: hooks.c — add the flag near the other input globals (next to `g_tick_input_mask`)**

```c
/* --- AI match flag (armed by the main-menu mode button, consumed by Lua bots) --- */
static volatile int g_ai_match_active = 0;
static volatile int g_ai_match_player = 1;
static volatile int g_ai_match_training = 0;

void hooks_arm_ai_match(int ai_player, int training) {
    if (ggpo_net_active()) return;               /* never during online play */
    g_ai_match_player = ai_player & 1;
    g_ai_match_training = training ? 1 : 0;
    g_ai_match_active = 1;
    LOG_INFO("ai_match: armed (ai_player=%d training=%d)", g_ai_match_player, g_ai_match_training);
}

void hooks_clear_ai_match(void) {
    if (g_ai_match_active) LOG_INFO("ai_match: cleared");
    g_ai_match_active = 0;
    g_ai_match_training = 0;
}

void hooks_get_ai_match(int* out_active, int* out_ai_player, int* out_training) {
    if (out_active) *out_active = g_ai_match_active;
    if (out_ai_player) *out_ai_player = g_ai_match_player;
    if (out_training) *out_training = g_ai_match_training;
}
```

Also guard against online arming at the call site later (Task 5) with `g_online_pending_match.active` (that struct is file-local to hooks.c, so the check lives in the proxy, not here).

- [ ] **Step 2: hooks.c — clear the flag whenever the main menu is current**

In `hooked_main_update_with_buttons`, at the existing site:
```c
            if (after_update == (void*)(uintptr_t)ADDR_MAIN_STATE ||
                after_update == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) {
                add_online_button_to_main();
                hooks_clear_ai_match();          /* <- add */
            }
```

- [ ] **Step 3: hooks.h — declare the three functions** (append near other `hooks_` input functions):
```c
void hooks_arm_ai_match(int ai_player, int training);
void hooks_clear_ai_match(void);
void hooks_get_ai_match(int* out_active, int* out_ai_player, int* out_training);
```

- [ ] **Step 4: lua_manager.c — bot provider flag + bindings**

(a) Add to `struct LoadedMod` (after `int trace_events;`): `int bot_provider;`
(`load` path memsets the struct; verify with a grep for `memset` near `g_mods[g_mod_count++]` — if not memset, explicitly zero it where `id`/`enabled` are initialized.)

(b) New Lua functions (place next to `lua_game_poll_cmds`):
```c
static int lua_game_register_bot_provider(lua_State* Ls) {
    LoadedMod* mod = (LoadedMod*)lua_touserdata(Ls, lua_upvalueindex(1));
    if (mod) mod->bot_provider = 1;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_ai_match(lua_State* Ls) {
    int active = 0, ai_player = 1, training = 0;
    hooks_get_ai_match(&active, &ai_player, &training);
    if (!active) { lua_pushnil(Ls); return 1; }
    lua_newtable(Ls);
    lua_push_field_bool(Ls, "active", 1);
    lua_push_field_int(Ls, "ai_player", ai_player);
    lua_push_field_bool(Ls, "training", training);
    return 1;
}
```

(c) Public query (bottom of file with other `lua_manager_` exports):
```c
int lua_manager_has_bot_provider(void) {
    for (int mi = 0; mi < g_mod_count; mi++) {
        if (g_mods[mi].enabled && g_mods[mi].bot_provider) return 1;
    }
    return 0;
}
```

(d) Register in the `mod.game` table build (next to the `poll_cmds` registration lines):
```c
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_register_bot_provider, 1); lua_setfield(Ls, -2, "register_bot_provider");
    lua_pushcfunction(Ls, lua_game_ai_match);                                                lua_setfield(Ls, -2, "ai_match");
```

(e) Extend `lua_game_native_state` before its `return 1;`:
```c
    if (ptr_readable((const void*)p_score_p0, sizeof(int)))
        lua_push_field_int(Ls, "score_p0", *p_score_p0);
    if (ptr_readable((const void*)p_score_p1, sizeof(int)))
        lua_push_field_int(Ls, "score_p1", *p_score_p1);
    if (ptr_readable((const void*)p_score_target, sizeof(int)))
        lua_push_field_int(Ls, "score_target", *p_score_target);
    {
        int leader = -1;
        if (ptr_readable((const void*)p_game_leader, sizeof(uintptr_t))) {
            uintptr_t lead = *p_game_leader;
            if (lead && lead == game_get_player_ptr(0)) leader = 0;
            else if (lead && lead == game_get_player_ptr(1)) leader = 1;
        }
        lua_push_field_int(Ls, "leader", leader);
    }
```
(`game_get_player_ptr` is defined later in the file at ~5440 — add a forward declaration
`static uintptr_t game_get_player_ptr(int player_index);` near the top statics if needed.)

- [ ] **Step 5: lua_manager.h — declare** `int lua_manager_has_bot_provider(void);`

- [ ] **Step 6: Build**

Run the Global Constraints build command. Expected: `build\SDL2_test.dll` produced, no new warnings referencing the touched functions.

- [ ] **Step 7: In-game smoke check (batch with Task 5's checks if convenient)**

Deploy (`Copy-Item build\SDL2_test.dll SDL2.dll -Force`, game closed). In the dev console:
- `return mod.game.ai_match()` → `nil`
- `return mod.game.native_state().score_p0` → `0` (on main menu) — proves fields exist.
- `return mod.game.register_bot_provider()` → `true`

- [ ] **Step 8: Commit** — `git commit -m "feat(framework): ai_match flag, bot provider registry, score/leader in native_state"`

---

### Task 5: C bridge — mode-cycling PLAY button with arrows

**Files:**
- Modify: `hooks.c` only (all in the online/menu section near `add_online_button_to_main`).

**Interfaces:**
- Consumes: `hooks_arm_ai_match` (Task 4), `lua_manager_has_bot_provider()` (Task 4).
- Produces: user-visible menu behavior; `g_menu_mode` persisted as `main_menu_mode=` in `mods/modframework.cfg`.

**Design notes for the implementer (all verified in-code):**
- The native START button's action is `MAIN_START_ACTION_PTR (0x432440)`; buttons store filter/action fn at `+0xE4`, link state at `+0xE0`, label `const char*` at `+0xC8`, center x/y at `+0x10/+0x14` **in screen pixels**, size at `+0x20/+0x24`.
- The activation pattern (who pressed it) is the tag dance on offset `+4`: set `0x11`, call `p_btn_player_filter(btn, 3)` → nonzero means P1 activated; else try `0x12` for P2. This is exactly `online_hub_player_filter_proxy`'s logic — copy its structure.
- To run the native start flow after arming, mirror `online_activate_native_button`: call the original action fn with event 3, then `p_state_switch(original_link)` if `link && !nolink`.
- The overlay (mode color accent + triangles over the arrow buttons) draws in `hooks_online_on_pre_swap` following the toast pattern (`mods_restore_render_state()` + `p_main_sprite_batches_draw()` around custom GL draws).

- [ ] **Step 1: Add mode state + persistence helpers (model on log.c's cfg read/write)**

```c
enum { MENU_MODE_PLAY = 0, MENU_MODE_ONLINE = 1, MENU_MODE_VSAI = 2, MENU_MODE_TRAIN = 3, MENU_MODE_COUNT = 4 };
static int g_menu_mode = MENU_MODE_PLAY;
static int g_menu_mode_loaded = 0;

static const char* menu_mode_label(int mode) {
    switch (mode) {
        case MENU_MODE_ONLINE: return "ONLINE";
        case MENU_MODE_VSAI:   return "VS AI";
        case MENU_MODE_TRAIN:  return "TRAIN AI";
        default:               return "PLAY";
    }
}

static int menu_mode_available(int mode) {
    if (mode == MENU_MODE_VSAI || mode == MENU_MODE_TRAIN) {
        return lua_manager_has_bot_provider();
    }
    return 1;
}

static void menu_mode_load(void) {
    FILE* f;
    char line[256];
    if (g_menu_mode_loaded) return;
    g_menu_mode_loaded = 1;
    f = fopen("mods/modframework.cfg", "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "main_menu_mode", 14) != 0) continue;
        p += 14;
        while (*p == ' ' || *p == '\t' || *p == '=' || *p == ':') p++;
        g_menu_mode = atoi(p);
        if (g_menu_mode < 0 || g_menu_mode >= MENU_MODE_COUNT) g_menu_mode = MENU_MODE_PLAY;
        break;
    }
    fclose(f);
    if (!menu_mode_available(g_menu_mode)) g_menu_mode = MENU_MODE_PLAY;
}

static void menu_mode_save(void) {
    char lines[64][256];
    int count = 0, replaced = 0;
    FILE* f = fopen("mods/modframework.cfg", "r");
    if (f) {
        while (count < 64 && fgets(lines[count], sizeof(lines[count]), f)) {
            char* p = lines[count];
            while (*p == ' ' || *p == '\t') p++;
            if (_strnicmp(p, "main_menu_mode", 14) == 0) {
                snprintf(lines[count], sizeof(lines[count]), "main_menu_mode=%d\n", g_menu_mode);
                replaced = 1;
            }
            count++;
        }
        fclose(f);
    }
    CreateDirectoryA("mods", NULL);
    f = fopen("mods/modframework.cfg", "w");
    if (!f) return;
    for (int i = 0; i < count; i++) fputs(lines[i], f);
    if (!replaced) fprintf(f, "main_menu_mode=%d\n", g_menu_mode);
    fclose(f);
}

static void menu_mode_cycle(int dir) {
    for (int step = 0; step < MENU_MODE_COUNT; step++) {
        g_menu_mode = (g_menu_mode + dir + MENU_MODE_COUNT) % MENU_MODE_COUNT;
        if (menu_mode_available(g_menu_mode)) break;
    }
    menu_mode_save();
    LOG_INFO("menu: mode -> %s", menu_mode_label(g_menu_mode));
}
```

- [ ] **Step 2: Add the three filter proxies**

```c
/* captured from the native START button the first time we see it */
static void* g_main_start_orig_action = NULL;
static void* g_main_start_orig_link = NULL;

static int menu_mode_tag_dance_activated(void* btn) {
    /* returns -1 (not activated), 0 (P1), 1 (P2). Mirrors online_hub_player_filter_proxy. */
    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;
    int who = -1;
    *tag_ptr = 0x11;
    if (p_btn_player_filter(btn, 3)) who = 0;
    if (who < 0) {
        *tag_ptr = 0x12;
        if (p_btn_player_filter(btn, 3)) who = 1;
    }
    *tag_ptr = old_tag;
    return who;
}

static int menu_mode_forward_nav(void* btn, int event_code) {
    /* non-activation events: same dual-selector pass-through the other proxies use */
    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;
    *tag_ptr = 0x11;
    if (p_btn_player_filter(btn, event_code)) { *tag_ptr = old_tag; return 1; }
    *tag_ptr = 0x12;
    if (p_btn_player_filter(btn, event_code)) { *tag_ptr = old_tag; return 1; }
    *tag_ptr = old_tag;
    return 0;
}

static int __cdecl menu_mode_arrow_up_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;
    if (event_code == 3) {
        if (menu_mode_tag_dance_activated(btn) >= 0) menu_mode_cycle(-1);
        return 0;  /* consume; no state switch */
    }
    return menu_mode_forward_nav(btn, event_code);
}

static int __cdecl menu_mode_arrow_down_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;
    if (event_code == 3) {
        if (menu_mode_tag_dance_activated(btn) >= 0) menu_mode_cycle(1);
        return 0;
    }
    return menu_mode_forward_nav(btn, event_code);
}

static int __cdecl menu_mode_main_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;
    if (event_code == 3) {
        int who = menu_mode_tag_dance_activated(btn);
        if (who < 0) return 0;
        if (g_menu_mode == MENU_MODE_ONLINE) {
            online_hub_open();
            return 0;
        }
        if ((g_menu_mode == MENU_MODE_VSAI || g_menu_mode == MENU_MODE_TRAIN) &&
            !ggpo_net_active() && !g_online_pending_match.active) {
            hooks_arm_ai_match(who ^ 1, g_menu_mode == MENU_MODE_TRAIN);
        }
        /* PLAY / VS AI / TRAIN: run the original native START activation */
        if (g_main_start_orig_action) {
            fn_btn_player_filter_t orig = (fn_btn_player_filter_t)g_main_start_orig_action;
            if (orig(btn, 3)) {
                int nolink = 0;
                if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_NOLINK_FLAG, (SIZE_T)sizeof(unsigned char)))
                    nolink = (*(unsigned char*)((uint8_t*)btn + BTN_OFS_NOLINK_FLAG) != 0);
                if (g_main_start_orig_link && !nolink && p_state_switch)
                    p_state_switch(g_main_start_orig_link);
            }
        }
        return 0;
    }
    return menu_mode_forward_nav(btn, event_code);
}
```

- [ ] **Step 3: Replace `add_online_button_to_main` with `apply_main_menu_mode_button`**

Rename the function and rewrite the body; update the single call site in
`hooked_main_update_with_buttons` (`add_online_button_to_main();` → `apply_main_menu_mode_button();`).

```c
static void apply_main_menu_mode_button(void) {
    void* start_btn;
    void* up_btn;
    void* down_btn;
    float sx, sy, sw, sh;
    menu_mode_load();
    if (!p_button_ex) return;

    start_btn = online_find_button_by_action(MAIN_START_ACTION_PTR);
    if (start_btn) {
        /* first sight of the native button: capture originals, take it over */
        g_main_start_orig_action = *(void**)((uint8_t*)start_btn + BTN_OFS_ACTION_PTR);
        g_main_start_orig_link   = *(void**)((uint8_t*)start_btn + BTN_OFS_LINK_PTR);
        *(void**)((uint8_t*)start_btn + BTN_OFS_ACTION_PTR) = (void*)&menu_mode_main_filter_proxy;
    } else {
        start_btn = online_find_button_by_action((uintptr_t)&menu_mode_main_filter_proxy);
    }
    if (!start_btn) return;

    /* per-mode label; keep native link so PLAY behaves natively when activated */
    *(const char**)((uint8_t*)start_btn + BTN_OFS_LABEL_PTR) = menu_mode_label(g_menu_mode);

    sx = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_X);
    sy = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_Y);
    sw = *(float*)((uint8_t*)start_btn + BTN_OFS_WIDTH);
    sh = *(float*)((uint8_t*)start_btn + BTN_OFS_HEIGHT);

    up_btn = online_find_button_by_action((uintptr_t)&menu_mode_arrow_up_filter_proxy);
    if (!up_btn) {
        if (p_button_set_layout) p_button_set_layout(3.0f, 6.0f);
        up_btn = p_button_ex(1.0f, 4.0f, 0u, "^", (int)(intptr_t)&menu_mode_arrow_up_filter_proxy);
        if (up_btn) *(void**)((uint8_t*)up_btn + BTN_OFS_ACTION_PTR) = (void*)&menu_mode_arrow_up_filter_proxy;
    }
    down_btn = online_find_button_by_action((uintptr_t)&menu_mode_arrow_down_filter_proxy);
    if (!down_btn) {
        if (p_button_set_layout) p_button_set_layout(3.0f, 6.0f);
        down_btn = p_button_ex(1.0f, 5.0f, 0u, "v", (int)(intptr_t)&menu_mode_arrow_down_filter_proxy);
        if (down_btn) *(void**)((uint8_t*)down_btn + BTN_OFS_ACTION_PTR) = (void*)&menu_mode_arrow_down_filter_proxy;
    }

    /* snug the arrows above/below the main button, narrow hit boxes */
    if (up_btn && !IsBadWritePtr((uint8_t*)up_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float))) {
        *(float*)((uint8_t*)up_btn + BTN_OFS_CENTER_X) = sx;
        *(float*)((uint8_t*)up_btn + BTN_OFS_CENTER_Y) = sy - sh * 0.85f;
        *(float*)((uint8_t*)up_btn + BTN_OFS_WIDTH)  = sw * 0.28f;
        *(float*)((uint8_t*)up_btn + BTN_OFS_HEIGHT) = sh * 0.55f;
    }
    if (down_btn && !IsBadWritePtr((uint8_t*)down_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float))) {
        *(float*)((uint8_t*)down_btn + BTN_OFS_CENTER_X) = sx;
        *(float*)((uint8_t*)down_btn + BTN_OFS_CENTER_Y) = sy + sh * 0.85f;
        *(float*)((uint8_t*)down_btn + BTN_OFS_WIDTH)  = sw * 0.28f;
        *(float*)((uint8_t*)down_btn + BTN_OFS_HEIGHT) = sh * 0.55f;
    }
    /* NOTE: the old ONLINE-button START-reposition dance is gone; START stays at its
       native position and ONLINE is reached via mode cycling. Do not create the old
       ONLINE button anymore. */
}
```
Delete/stop calling nothing else — `online_hub_open`, `g_online_hub_state`, and the hub itself stay untouched (they're reached through `menu_mode_main_filter_proxy` now). If any other code besides the old button creation references `online_hub_player_filter_proxy`, leave that function in place (it still backs older paths like the post-match "return to hub" flows).

- [ ] **Step 4: Mode overlay (accent + triangles) in `hooks_online_on_pre_swap`**

Insert after the toast blocks, before the `if (!ggpo_net_active() ...) return;` line:

```c
    if (state_ptr == (void*)(uintptr_t)ADDR_MAIN_STATE ||
        state_ptr == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) {
        menu_mode_render_overlay();
        mods_restore_render_state();
        if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
        mods_restore_render_state();
    }
```

And the renderer (place above `hooks_online_on_pre_swap`):
```c
static void menu_mode_accent(float* r, float* g, float* b) {
    switch (g_menu_mode) {
        case MENU_MODE_ONLINE: *r = 0.40f; *g = 0.75f; *b = 1.00f; break;   /* blue   */
        case MENU_MODE_VSAI:   *r = 1.00f; *g = 0.55f; *b = 0.30f; break;   /* orange */
        case MENU_MODE_TRAIN:  *r = 0.70f; *g = 0.45f; *b = 1.00f; break;   /* purple */
        default:               *r = 0.55f; *g = 1.00f; *b = 0.60f; break;   /* green  */
    }
}

static void menu_mode_draw_triangle(float cx, float cy, float half_w, float rows, int up,
                                    float r, float g, float b, float a) {
    /* pixel-art triangle from stacked rects (fits the game's aesthetic) */
    for (int i = 0; i < (int)rows; i++) {
        float t = (float)i / rows;
        float w = half_w * (up ? (1.0f - t) : t) * 2.0f;
        float y = cy + (up ? (rows - i) : i) - rows * 0.5f;
        if (w < 1.0f) continue;
        hooks_ui_fill_rect(cx - w * 0.5f, y, w, 1.0f, r, g, b, a);
    }
}

static void menu_mode_render_overlay(void) {
    void* start_btn = online_find_button_by_action((uintptr_t)&menu_mode_main_filter_proxy);
    void* up_btn = online_find_button_by_action((uintptr_t)&menu_mode_arrow_up_filter_proxy);
    void* down_btn = online_find_button_by_action((uintptr_t)&menu_mode_arrow_down_filter_proxy);
    float r, g, b;
    if (!start_btn) return;
    menu_mode_accent(&r, &g, &b);
    {
        float sx = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_X);
        float sy = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_Y);
        float sw = *(float*)((uint8_t*)start_btn + BTN_OFS_WIDTH);
        float sh = *(float*)((uint8_t*)start_btn + BTN_OFS_HEIGHT);
        /* accent underline below the label */
        hooks_ui_fill_rect(sx - sw * 0.30f, sy + sh * 0.42f, sw * 0.60f, 2.0f, r, g, b, 0.95f);
        if (up_btn) {
            float ux = *(float*)((uint8_t*)up_btn + BTN_OFS_CENTER_X);
            float uy = *(float*)((uint8_t*)up_btn + BTN_OFS_CENTER_Y);
            menu_mode_draw_triangle(ux, uy, 7.0f, 6.0f, 1, r, g, b, 0.9f);
        }
        if (down_btn) {
            float dx = *(float*)((uint8_t*)down_btn + BTN_OFS_CENTER_X);
            float dy = *(float*)((uint8_t*)down_btn + BTN_OFS_CENTER_Y);
            menu_mode_draw_triangle(dx, dy, 7.0f, 6.0f, 0, r, g, b, 0.9f);
        }
    }
}
```
Also change the arrow buttons' native labels from `"^"`/`"v"` to `" "` (single space) AFTER
confirming triangles render (Step 6) — the triangles replace them visually; keep text labels
until then so the buttons are findable on screen.

- [ ] **Step 5: Build** — same command; expect clean build.

- [ ] **Step 6: In-game verification checklist (single session with the user, log open)**

1. Main menu shows the mode button (label PLAY) with `^`/`v` around it; other native items intact.
2. Keyboard: navigate to `^`/`v`, activate → label cycles PLAY→ONLINE (VS AI/TRAIN hidden — no mod yet); underline color changes.
3. Mouse: click arrows → cycles; click main button in PLAY → native map select opens; in ONLINE → hub opens.
4. `mods/modframework.cfg` gains `main_menu_mode=` and survives restart.
5. Resize window on menu: button/arrows/overlay stay aligned.
6. `modframework.log` shows `menu: mode -> ...` lines, no errors.
7. If navigation between arrows/button misbehaves (native nav is creation-order/position based — flagged risk), tune arrow `CENTER_Y` gaps first; if still broken, fall back to left/right placement beside the button (same proxies, x offsets instead of y).

- [ ] **Step 7: Commit** — `git commit -m "feat(framework): mode-cycling main menu button (PLAY/ONLINE/VS AI/TRAIN) with selector arrows"`

---

### Task 6: Mod skeleton + play-mode bot glue

**Files:**
- Create: `mods/ai_opponent/mod.json`, `config.cfg`, `main.lua`, `bot.lua`
- Create: `mods/ai_opponent/data/weights_normal.lua` (temporary random-net placeholder, replaced in Task 8)

**Interfaces:**
- Consumes: all lib contracts; `mod.game.ai_match/set_input/input_clear/snapshot/native_state/world.player/world.tile_at_world/poll_cmds`; `mod.game.register_bot_provider`.
- Produces: `Bot.drive(ai_player, policy)` used by trainer too: computes ctx, applies scaffold, sets input, returns `ctx` (trainer reuses it for fitness).

- [ ] **Step 1: `mod.json`**
```json
{
  "id": "ai_opponent",
  "name": "AI Opponent",
  "version": "0.1.0",
  "author": "modframework",
  "description": "Singleplayer: a neuroevolution-trained AI opponent (VS AI / TRAIN AI on the main menu).",
  "api_version": 1,
  "entry": "main.lua",
  "config": "config.cfg",
  "storage": "storage.cfg"
}
```
(Verify current framework `api_version` by checking another working mod, e.g. `mods/options_demo/mod.json`, and match it.)

- [ ] **Step 2: `config.cfg`**
```
# AI Opponent config (v1)
difficulty: options[easy, normal, hard], normal
train_ticks_per_frame: int, 120
show_debug_overlay: bool, false
```
(Verify exact line syntax against `mods/options_demo/config.cfg` — `int`/`bool` type names
must match what the framework parses; adjust to the supported syntax if different.)

- [ ] **Step 3: `bot.lua`** — game-facing context/probe builder + driver
```lua
-- Game-facing bot runtime. Loaded via mod.dofile("bot.lua") from main.lua.
-- Returns a table; deps injected via init().
local Bot = { _d = nil }

function Bot.init(deps)  -- deps = { NN=, A=, F=, Policy= }
  Bot._d = deps
end

local HAZARD_IDS = { [5] = true, [10] = true }   -- spikes, lava (glyph map)

local function probe(x, y)
  local t = mod.game.world.tile_at_world(x, y)
  if not t then return 0, 0 end
  return (t.solid and 1 or 0), (HAZARD_IDS[t.id] and 1 or 0)
end

function Bot.build_ctx(ai_player)
  local snap = mod.game.snapshot(ai_player)
  if not snap or not snap.player or not snap.enemy then return nil end
  local me = mod.game.world.player(ai_player)
  local other = mod.game.world.player(1 - ai_player)
  if not me or not me.valid then return nil end
  local ns = mod.game.native_state()
  local dirx = (snap.player.facing or 1) >= 0 and 1 or -1
  local px, py = snap.player.x, snap.player.y
  local s_near, h_near = probe(px + dirx * 12, py)
  local s_far, h_far = probe(px + dirx * 28, py)
  local g_front = probe(px + dirx * 12, py + 14)
  local _, gap = 0, 0
  local below_solid = probe(px + dirx * 12, py + 30)
  local above = probe(px, py - 16)
  local leader = -0  -- resolved below
  local lead = ns.leader or -1
  if lead == ai_player then leader = 1 elseif lead == (1 - ai_player) then leader = -1 else leader = 0 end
  return {
    snap = snap,
    my_room = me.room or 0,
    enemy_room = (other and other.room) or 0,
    goal_dir = (ai_player == 0) and 1 or -1,   -- VERIFY in-game: P0 pushes right
    leader = leader,
    probes = {
      ahead_near = s_near, ahead_far = s_far,
      hazard_ahead = (h_near ~= 0 or h_far ~= 0) and 1 or 0,
      ground_front = g_front, gap_below = (g_front == 0 and below_solid == 0) and 1 or 0,
      above = above,
    },
    tick = mod.game.tick_count(),
  }
end

-- stuck scaffold: if x hasn't moved >2px in 90 ticks while trying to move, force a jump
local stuck = { [0] = { x = 0, t = 0 }, [1] = { x = 0, t = 0 } }

function Bot.scaffold(ai_player, ctx, mask)
  local s = stuck[ai_player]
  local x = ctx.snap.player.x
  local moving = (mask % 16) >= 4   -- LEFT or RIGHT bit set
  if moving and math.abs(x - s.x) < 2 then
    s.t = s.t + 1
  else
    s.x, s.t = x, 0
  end
  if s.t > 90 then
    s.t = 0
    return mask + ((mask % 2 == 0) and 1 or 0)   -- OR in JUMP if not set
  end
  return mask
end

function Bot.drive(ai_player, policy)
  local d = Bot._d
  local ctx = Bot.build_ctx(ai_player)
  if not ctx then return nil end
  local feats = d.F.extract(ctx)
  local action = d.Policy.decide(policy, feats)
  local mask = d.A.mask(action)
  mask = Bot.scaffold(ai_player, ctx, mask)
  mod.game.set_input(ai_player, mask, 1, true)
  return ctx
end

return Bot
```
(Verify `mod.game.world.tile_at_world` and the `world.player().room/valid` field names
against MODDING.md's world section / grep of `lua_manager.c` before first run; adjust
`probe()` if the actual return shape differs.)

- [ ] **Step 4: placeholder `data/weights_normal.lua`**
```lua
-- Placeholder untrained net (replaced by Task 8 training output).
-- Regenerate with: luajit -e "local NN=dofile('lib/nn.lua'); print(NN.serialize(NN.new({28,32,16,12}, 42)))"
return nil
```
(Returning `nil` makes main.lua fall back to generating a fresh random net.)

- [ ] **Step 5: `main.lua`** — glue
```lua
-- AI Opponent: singleplayer bot + in-engine trainer.
local NN = mod.dofile("lib/nn.lua")
local A = mod.dofile("lib/actions.lua")
local F = mod.dofile("lib/features.lua")
local Policy = mod.dofile("lib/policy.lua")
local EVO = mod.dofile("lib/evo.lua")
local Codec = mod.dofile("lib/codec.lua")
local Bot = mod.dofile("bot.lua")

Policy.init(NN)
EVO.init(NN)
Bot.init({ NN = NN, A = A, F = F, Policy = Policy })

local SIZES = { F.N_INPUTS, 32, 16, A.COUNT }

local DIFF = {
  easy   = { react_delay = 10, epsilon = 0.10, key = "ckpt_easy" },
  normal = { react_delay = 4,  epsilon = 0.03, key = "ckpt_normal" },
  hard   = { react_delay = 1,  epsilon = 0.00, key = "ckpt_hard" },
}

local play_policy = nil
local active_player = nil

local function load_net_for(diff_name)
  local d = DIFF[diff_name] or DIFF.normal
  local s = Codec.load(storage, d.key)
  if s then
    local net = NN.deserialize(s)
    if net then return net, d end
  end
  local shipped = mod.dofile("data/weights_" .. (DIFF[diff_name] and diff_name or "normal") .. ".lua")
  if type(shipped) == "string" then
    local net = NN.deserialize(shipped)
    if net then return net, d end
  end
  mod.log("no checkpoint for " .. diff_name .. "; using fresh random net")
  return NN.new(SIZES, 1337), d
end

local function config_difficulty()
  local v = mod.config and mod.config.get and mod.config.get("difficulty")
  if v ~= "easy" and v ~= "normal" and v ~= "hard" then v = "normal" end
  return v
end

mod.game.register_bot_provider()

local Trainer = mod.dofile("trainer.lua")  -- Task 7 (stub returns {tick=function() end,...} until then)
Trainer.init({ NN = NN, A = A, F = F, Policy = Policy, EVO = EVO, Codec = Codec, Bot = Bot, SIZES = SIZES })

mod.on_tick(function()
  local m = mod.game.ai_match()
  if not m then
    if active_player then
      mod.game.input_clear(active_player)
      active_player, play_policy = nil, nil
      Policy.reset_all_ok = true
    end
    return
  end
  if not mod.ui.is_state("game") then return end
  if m.training then
    Trainer.tick()
    return
  end
  if active_player ~= m.ai_player or not play_policy then
    local net, d = load_net_for(config_difficulty())
    play_policy = Policy.new(net, { react_delay = d.react_delay, epsilon = d.epsilon, seed = 99 })
    active_player = m.ai_player
    mod.log("VS AI: driving player " .. active_player .. " (" .. config_difficulty() .. ")")
  end
  Bot.drive(active_player, play_policy)
end)

mod.on_frame(function()
  local m = mod.game.ai_match()
  if m and m.training then Trainer.overlay() end
end)

mod.on_unload(function()
  if active_player then mod.game.input_clear(active_player) end
  mod.game.input_clear(0); mod.game.input_clear(1)
  Trainer.shutdown()
end)
```
Create `trainer.lua` as a stub for now so the mod loads:
```lua
local Trainer = {}
function Trainer.init(deps) Trainer._d = deps end
function Trainer.tick() end
function Trainer.overlay() end
function Trainer.shutdown() end
return Trainer
```
(Verify the exact config-read API (`mod.config.get`) and logging fn (`mod.log`) names in
MODDING.md; adjust to real names. Remove the stray `Policy.reset_all_ok` line — it's a
placeholder-free plan: DELETE that line when writing the file.)

- [ ] **Step 6: In-game verification (with user)**

1. Mod loads (`modframework.log`: ai_opponent loaded, no errors).
2. Main menu now offers VS AI and TRAIN AI in the cycle (bot provider registered).
3. Start VS AI with P1 controls → map select → in match, P2 moves on its own (random-net flailing is EXPECTED and fine); human inputs on P2's keys do nothing.
4. Start VS AI by activating with P2 controls → AI drives P1 instead.
5. Quit to menu → `ai_match` cleared (console: `return mod.game.ai_match()` → nil), P2 human control restored in a normal PLAY match.
6. Difficulty dropdown appears under Options → Mods → AI Opponent.

- [ ] **Step 7: Commit** — `git commit -m "feat(ai_opponent): mod skeleton, play-mode bot driving, difficulty config"`

---

### Task 7: Trainer — self-play episodes, fitness, generations, TRAIN overlay

**Files:**
- Create (replace stub): `mods/ai_opponent/trainer.lua`

**Interfaces:**
- Consumes: `EVO`, `Policy`, `Bot.build_ctx`, `A.mask`, `Codec`, `mod.game.full_state_blob/apply_full_state_blob/simulate_ticks/set_input/native_state`, `mod.ui.*`.
- Produces: `Trainer.init(deps)`, `Trainer.tick()`, `Trainer.overlay()`, `Trainer.shutdown()`; storage keys `ckpt_easy/ckpt_normal/ckpt_hard`, `trainer_pop` (chunked), `trainer_gen`.

**Fitness (per episode, from the AI-side player's perspective; both players scored symmetrically):**
- +100 opponent enters dying state (state_id 8) while it wasn't already dying (edge)
- −100 self dying edge
- +0.05/tick while leader (native_state().leader)
- +0.5 per pixel of NET progress toward goal (max over episode of goal-direction displacement, evaluated at episode end)
- +30 per room advanced toward goal (room delta at episode end)
- −0.01/tick flat time pressure

**Episode:** 1800 sim ticks max; early end on either death edge (after applying its fitness) or room change ≥ 2. Reset via base blob.

**Schedule per `Trainer.tick()` call:** run up to `train_ticks_per_frame` (config, default 120) sim ticks, spread across episodes; pattern per sim tick:
```
mask0 = policy_a decision from Bot-built ctx for player 0
mask1 = policy_b decision for player 1
mod.game.set_input(0, mask0, 1, true)
mod.game.set_input(1, mask1, 1, true)
mod.game.simulate_ticks(1)
accumulate fitness deltas from fresh snapshots/native_state
```

- [ ] **Step 1: Implement `trainer.lua`**

```lua
-- Self-play neuroevolution trainer. All heavy loops budgeted per frame.
local Trainer = {}
local d = nil

local POP_N = 32
local EPISODE_TICKS = 1800
local ELITES = 4
local MUT = { elites = ELITES, mut_rate = 0.15, mut_scale = 0.25 }

local st = nil  -- training session state

function Trainer.init(deps) d = deps end

local function fresh_session()
  local pop
  local saved = d.Codec.load(storage, "trainer_pop")
  local gen = tonumber(storage.get("trainer_gen", 0)) or 0
  if saved then
    pop = { nets = {}, gen = gen, sizes = d.SIZES }
    for chunk in saved:gmatch("([^\n]+)") do
      local net = d.NN.deserialize(chunk)
      if net then pop.nets[#pop.nets + 1] = net end
    end
  end
  if not pop or #pop.nets < 2 then
    pop = d.EVO.new(POP_N, d.SIZES, os.time() % 100000)
    pop.gen = 0
  end
  return {
    pop = pop,
    rand = d.NN.rng_new(os.time() % 100000 + 17),
    base_blob = nil,
    fitness = {},
    pair_i = 1,          -- genome being evaluated (plays as player 0)
    episode_tick = 0,
    ep = nil,            -- per-episode accumulators
    paused = false,
    best_fit = nil, mean_fit = nil,
    ticks_done = 0,
  }
end

local function pick_opponent(s)
  -- opponent for genome i: random OTHER genome (self-play within population)
  local j = 1 + math.floor(s.rand() * #s.pop.nets)
  if j == s.pair_i then j = (j % #s.pop.nets) + 1 end
  return j
end

local function begin_episode(s)
  local blob = s.base_blob
  if blob then mod.game.apply_full_state_blob(blob) end
  local oi = pick_opponent(s)
  s.ep = {
    a = d.Policy.new(s.pop.nets[s.pair_i], { react_delay = 0, epsilon = 0.05, seed = s.pair_i * 31 + s.pop.gen }),
    b = d.Policy.new(s.pop.nets[oi], { react_delay = 0, epsilon = 0.05, seed = oi * 37 + s.pop.gen }),
    fit_a = 0,
    tick = 0,
    was_dying = { [0] = false, [1] = false },
    start_x = nil, best_prog = 0, start_room = nil,
  }
  s.episode_tick = 0
end

local function dying(state_id) return state_id == 8 or state_id == 9 end

local function episode_step(s)
  local e = s.ep
  local ctx0 = d.Bot.build_ctx(0)
  local ctx1 = d.Bot.build_ctx(1)
  if not ctx0 or not ctx1 then return true end  -- lost the game state; end episode
  if not e.start_x then
    e.start_x = ctx0.snap.player.x
    e.start_room = ctx0.my_room
  end
  local a0 = d.Policy.decide(e.a, d.F.extract(ctx0))
  local a1 = d.Policy.decide(e.b, d.F.extract(ctx1))
  mod.game.set_input(0, d.A.mask(a0), 1, true)
  mod.game.set_input(1, d.A.mask(a1), 1, true)
  mod.game.simulate_ticks(1)
  e.tick = e.tick + 1

  local s0 = mod.game.snapshot(0)
  if not s0 or not s0.player or not s0.enemy then return true end
  local ns = mod.game.native_state()
  local p_dying = dying(s0.player.state_id or 0)
  local o_dying = dying(s0.enemy.state_id or 0)
  local ended = false
  if o_dying and not e.was_dying[1] then e.fit_a = e.fit_a + 100; ended = true end
  if p_dying and not e.was_dying[0] then e.fit_a = e.fit_a - 100; ended = true end
  e.was_dying[0], e.was_dying[1] = p_dying, o_dying
  if (ns.leader or -1) == 0 then e.fit_a = e.fit_a + 0.05 end
  e.fit_a = e.fit_a - 0.01
  local prog = (s0.player.x - e.start_x) * 1   -- goal_dir for player 0 assumed +1; VERIFY
  if prog > e.best_prog then e.best_prog = prog end
  local room_now = ctx0.my_room
  if math.abs(room_now - (e.start_room or room_now)) >= 2 then ended = true end
  if e.tick >= EPISODE_TICKS then ended = true end
  if ended then
    e.fit_a = e.fit_a + 0.5 * e.best_prog + 30 * (room_now - (e.start_room or room_now))
  end
  return ended
end

local function finish_episode(s)
  s.fitness[s.pair_i] = (s.fitness[s.pair_i] or 0) + s.ep.fit_a
  s.pair_i = s.pair_i + 1
  if s.pair_i > #s.pop.nets then
    -- generation done
    local best, sum = -1e18, 0
    for i = 1, #s.pop.nets do
      local f = s.fitness[i] or 0
      if f > best then best = f end
      sum = sum + f
    end
    s.best_fit, s.mean_fit = best, sum / #s.pop.nets
    Trainer.save_checkpoints(s)
    s.pop = d.EVO.next_gen(s.pop, s.fitness, MUT, s.rand)
    storage.set("trainer_gen", tostring(s.pop.gen))
    s.fitness = {}
    s.pair_i = 1
  end
  begin_episode(s)
end

function Trainer.save_checkpoints(s)
  -- population dump (newline-joined) + best-of-gen into rolling ckpt slots
  local order, fit = {}, s.fitness
  for i = 1, #s.pop.nets do order[i] = i end
  table.sort(order, function(x, y) return (fit[x] or 0) > (fit[y] or 0) end)
  local best = s.pop.nets[order[1]]
  d.Codec.store(storage, "ckpt_hard", d.NN.serialize(best))
  if s.pop.gen == 10 then d.Codec.store(storage, "ckpt_easy", d.NN.serialize(best)) end
  if s.pop.gen == 40 then d.Codec.store(storage, "ckpt_normal", d.NN.serialize(best)) end
  local dump = {}
  for i = 1, #s.pop.nets do dump[i] = d.NN.serialize(s.pop.nets[i]) end
  d.Codec.store(storage, "trainer_pop", table.concat(dump, "\n"))
  storage.save()
end

function Trainer.tick()
  if not st then st = fresh_session() end
  if st.paused then return end
  if not st.base_blob then
    st.base_blob = mod.game.full_state_blob()
    if not st.base_blob then return end
    begin_episode(st)
  end
  local budget = tonumber(mod.config.get("train_ticks_per_frame")) or 120
  for _ = 1, budget do
    if episode_step(st) then finish_episode(st) end
    st.ticks_done = st.ticks_done + 1
  end
end

function Trainer.overlay()
  if not st then return end
  mod.ui.begin_overlay()
  mod.ui.rect(20, 20, 300, 120, { color = { 0.03, 0.04, 0.06, 0.85 } })
  mod.ui.text_at("TRAIN AI  gen " .. st.pop.gen, 32, 34, 1.0, 0.8, 0.6, 1.0)
  mod.ui.text_at(string.format("genome %d/%d  ep tick %d", st.pair_i, #st.pop.nets, st.ep and st.ep.tick or 0),
                 32, 56, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at(string.format("best %.1f  mean %.1f", st.best_fit or 0, st.mean_fit or 0),
                 32, 74, 0.9, 0.9, 0.9, 0.9)
  mod.ui.text_at("sim ticks: " .. st.ticks_done, 32, 92, 0.9, 0.7, 0.7, 0.7)
  if mod.ui.button_at("train_pause", st.paused and "RESUME" or "PAUSE", 32, 108, 80, 24) then
    st.paused = not st.paused
  end
  if mod.ui.button_at("train_save", "SAVE", 120, 108, 60, 24) then
    if st.fitness and next(st.fitness) then Trainer.save_checkpoints(st) end
  end
  mod.ui.end_overlay()
end

function Trainer.shutdown()
  if st and st.fitness and next(st.fitness) then Trainer.save_checkpoints(st) end
  st = nil
end

return Trainer
```
Notes for implementer:
- `storage` is the mod-global storage table (same one main.lua uses) — it is in the mod env, accessible from `mod.dofile`-loaded files.
- The `prog` line assumes player 0's goal is +x. FIRST in-game training check: print `native_state().leader` + both players' x after one kill; if goal directions are inverted, flip the sign and the `goal_dir` in `bot.lua` together.
- `mod.ui.button_at` exists per MODDING.md; overlay interaction only needs mouse.

- [ ] **Step 2: In-game training smoke test (with user, TRAIN mode)**

1. Cycle to TRAIN AI → map select → match starts and immediately fast-forwards (players blur around); overlay shows gen/genome/tick counters increasing.
2. `train_ticks_per_frame=120` keeps UI at ~60fps (tune down if frame time spikes).
3. Let it run ≥ 3 generations: `best`/`mean` values change; `storage.cfg` grows (`trainer_pop#n` keys present).
4. PAUSE/RESUME buttons work; quitting the match to menu stops training; `ai_match` is nil at menu; re-entering TRAIN resumes from saved population (gen persists).
5. Fitness sanity: mean fitness at gen 3+ is above the gen-0 mean (log values; strict improvement not required every gen).

- [ ] **Step 3: Commit** — `git commit -m "feat(ai_opponent): in-engine self-play neuroevolution trainer with TRAIN overlay"`

---

### Task 8: Train real checkpoints, ship difficulty weights, final verification

**Files:**
- Modify: `mods/ai_opponent/data/weights_easy.lua`, `weights_normal.lua`, `weights_hard.lua` (real trained weights)
- Modify: `main.lua` only if checkpoint-loading bugs surface.

- [ ] **Step 1: Long training run** — user leaves TRAIN mode running (target: 60+ generations; at
~120x realtime and ~57k ticks/gen that's roughly 8–10s per generation → under 15 minutes for
60 generations; run longer if quality is poor). Claude monitors `storage.cfg` growth between runs.

- [ ] **Step 2: Export checkpoints to shipped data files** — Claude reads
`mods/ai_opponent/storage.cfg` from disk, extracts `ckpt_easy/ckpt_normal/ckpt_hard` chunked
values (reassemble per Codec layout: `key#n` count + `key#i` chunks), and writes each as
`data/weights_<level>.lua` containing `return "<serialized>"`. Verify each deserializes:
`luajit -e "local NN=dofile('lib/nn.lua'); assert(NN.deserialize(dofile('data/weights_hard.lua')))"`.

- [ ] **Step 3: Behavioral acceptance (with user, one session per difficulty)**

1. VS AI normal: AI moves toward the player, attacks in range, picks up a sword after disarm at least once, crosses rooms, no soft-locks over a full match; someone reaches map end.
2. VS AI easy vs hard: hard is noticeably harder (faster reactions, fewer random actions).
3. Human as P2 (activate VS AI with P2 controls): AI correctly drives P1 and pushes the correct direction.
4. Online play regression: start an online-hub match — bot never injects (log clean), `ai_match` nil.
5. Hot reload mid-VS-AI-match: bot resumes, no stuck input (`input_status` clean after unload).

- [ ] **Step 4: Update MODDING.md** — document `mod.game.register_bot_provider`, `mod.game.ai_match`, and the new `native_state` fields (3 short entries in the Game API section).

- [ ] **Step 5: Final commit** — `git commit -m "feat(ai_opponent): trained difficulty checkpoints + docs"`

---

## Self-Review Notes

- **Spec coverage:** menu button+arrows (T5), player-side detection (T5 proxy), flag lifecycle (T4), gating on bot provider (T4/T5), bot runtime+scaffold+difficulty (T6), humanizer (T3/T6), neuroevolution+accelerated self-play+overlay+persistence (T7), shipped checkpoints+human-side verification (T8), MODDING.md (T8). "Learning from human matches" (spec §3, explicitly modest) is deliberately deferred — implement only if the user asks after v1; noted as the one intentional scope cut.
- **Known-unverified points called out inline:** exact `mod.config.get`/`mod.log` names, `tile_at_world` return shape, `api_version` value, config type syntax, goal direction sign, native nav order around arrow buttons. Each has a verify instruction at first use.
- **Type consistency:** `Policy.init/EVO.init` injection pattern consistent across T3/T6/T7; `Bot.build_ctx` ctx shape matches `F.extract(ctx)` (T2); `Codec.store/load(storage, key, str)` consistent in T3/T7/T8.
