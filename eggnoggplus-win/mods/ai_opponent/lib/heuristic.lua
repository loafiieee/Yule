-- Scripted utility AI (macro navigation + fallback combat), adapted from the
-- decision logic of github.com/borbog/Eggnogg-AI. Pure given ctx: all game
-- reads happen in bot.lua. Call H.init{NN=} once; H.new_mem(seed) per fighter.
--
-- ctx = { snap, my_room, enemy_room, goal_dir, leader (1=me,-1=enemy,0=none),
--         nav = { [1] = {wall=bool, gap=bool}, [-1] = {wall=bool, gap=bool} } }
-- Returns: action_index (into actions.lua A.LIST), mode_string.
local H = { _NN = nil }

-- action indices (must match lib/actions.lua A.LIST order)
local IDLE, BACK, FWD, JUMP, JUMP_BACK, JUMP_FWD = 1, 2, 3, 4, 5, 6
local ATK, ATK_BACK, ATK_FWD, UP, DOWN, DOWN_JUMP = 7, 8, 9, 10, 11, 12
local SLIDE_FWD, SLIDE_BACK = 13, 14   -- down+jump while running: passes hitboxes

H.ENGAGE_X = 120   -- fight-context range (bot.lua uses H.is_fight for the NN arbiter)
H.ENGAGE_Y = 60

function H.init(deps) H._NN = deps.NN end

function H.new_mem(seed)
  return { rand = H._NN.rng_new(seed or 1), hold = nil, hold_t = 0, mode = 'idle',
           blocked_x = nil, blocked_t = 0, climb_t = 0 }
end

local function is_dying(p) local s = p.state_id or 0; return s == 8 or s == 9 end
-- ATTACK is bit 0x01 (decompile-verified; 0x02 is JUMP)
local function attacking(p) return (((p.cmd_bits or 0) % 2) == 1) end

-- translate a SCREEN direction (+1 = +x) into our goal-relative actions
local function toward(dir, g)
  if (dir >= 0) == (g >= 0) then return FWD else return BACK end
end
local function jump_toward(dir, g)
  if (dir >= 0) == (g >= 0) then return JUMP_FWD else return JUMP_BACK end
end
local function attack_toward(dir, g)
  if (dir >= 0) == (g >= 0) then return ATK_FWD else return ATK_BACK end
end
local function slide_toward(dir, g)
  if (dir >= 0) == (g >= 0) then return SLIDE_FWD else return SLIDE_BACK end
end

-- Fight context: same room, close, both alive, and I'm armed. One source of
-- truth for "should the combat brain (NN or scripted) be driving".
-- A RUNNER NEVER FIGHTS: with the go (leader) or an open lane (ctx.intent ==
-- 'run', decided by the bot with a stall fallback), reaching the goal is
-- worth more than any exchange - run mode slides/vaults past instead.
function H.is_fight(ctx)
  local s = ctx.snap
  if ctx.leader == 1 then return false end
  if ctx.intent == 'run' or ctx.intent == 'warp' then return false end
  if not s.player.has_sword then return false end
  if is_dying(s.player) or is_dying(s.enemy) then return false end
  if ctx.my_room ~= ctx.enemy_room then return false end
  local dx = s.enemy.x - s.player.x
  local adx = math.abs(dx)
  if adx >= H.ENGAGE_X or math.abs(s.enemy.y - s.player.y) >= H.ENGAGE_Y then
    return false
  end
  -- terrain gate: a wall/step between us and an out-of-reach enemy is a
  -- NAVIGATION problem (route over it), not a fight - fight mode has no
  -- terrain moves and would swing at the geometry forever
  if adx > 40 then
    local edir = (dx >= 0) and 1 or -1
    local nav = ctx.nav and ctx.nav[edir]
    if nav and nav.wall then return false end
  end
  return true
end

-- JUMP is edge-triggered: holding the button is NOT pressing it. After any
-- jump-bearing hold ends, a short release gap must pass before the next one,
-- or the second jump simply never happens (the walk-into-the-step bug).
local JUMP_DEGRADE = {
  [JUMP] = IDLE, [JUMP_BACK] = BACK, [JUMP_FWD] = FWD,
  [DOWN_JUMP] = DOWN, [SLIDE_FWD] = FWD, [SLIDE_BACK] = BACK,
}

local function hold(mem, action, ticks, mode)
  if JUMP_DEGRADE[action] and (mem.jump_cool or 0) > 0 then
    mem.mode = mode
    return JUMP_DEGRADE[action], mode   -- release gap: keep moving, no jump yet
  end
  mem.hold = action
  mem.hold_t = ticks
  mem.mode = mode
  return action, mode
end

-- Throwing the sword is a PHASED input: a direction (or jump) pressed first,
-- then ATTACK a tick later WITH the direction still held (overlap). Pressing
-- attack alone just swings. We queue the exact frames: [dir], [dir+attack]x2.
-- Costs us our sword, so callers use it sparingly as a ranged poke/finisher.
local function throw_seq(mem, dir, g, mode)
  mem.seq = { attack_toward(dir, g), attack_toward(dir, g) }  -- frames 2..3
  mem.seq_mode = mode
  mem.mode = mode
  return toward(dir, g), mode                                 -- frame 1: direction only
end

-- lateral movement with a blocked-jump reflex: if we keep pressing a direction
-- and x doesn't move for 12 ticks (probes can miss steps/ledges), commit to a
-- real held jump instead of grinding against the geometry
local function move_or_jump(ctx, mem, dir, g, mode_name)
  local x = ctx.snap.player.x
  if mem.blocked_x ~= nil and math.abs(x - mem.blocked_x) < 0.75 then
    mem.blocked_t = mem.blocked_t + 1
  else
    mem.blocked_x, mem.blocked_t = x, 0
  end
  if mem.blocked_t >= 12 and ctx.snap.player.grounded then
    mem.blocked_t = 0
    return hold(mem, jump_toward(dir, g), 10, mode_name)
  end
  mem.mode = mode_name
  return toward(dir, g), mode_name
end

-- scripted fight micro (also the NN's sparring baseline and fallback)
local function fight(ctx, mem)
  local s = ctx.snap
  local me, en = s.player, s.enemy
  local g = ctx.goal_dir
  local dx = en.x - me.x
  local adx = math.abs(dx)
  local ady = math.abs((en.y or 0) - (me.y or 0))
  local edir = (dx >= 0) and 1 or -1
  local r = mem.rand()

  -- anti-air: enemy jumping at me -> point up / swing
  if not en.grounded and adx < 64 then
    if r < 0.5 then return UP, 'fight' end
    return attack_toward(edir, g), 'fight'
  end

  if adx > 46 then
    -- geometry in the way? jump it, don't grind into it
    local nav = ctx.nav and ctx.nav[edir]
    if nav and nav.wall then
      return hold(mem, jump_toward(edir, g), 12, 'fight')
    end
    -- THROW: a ranged poke when they are out of stab reach but roughly level.
    -- It disarms us, so only sometimes - it is a commitment, not a jab.
    if me.has_sword and adx < 118 and ady < 34 and r < 0.18 then
      return throw_seq(mem, edir, g, 'throw')
    end
    -- slide in to close the gap and blow through a poke (sliding is safe)
    if r < 0.30 then
      return hold(mem, slide_toward(edir, g), 12, 'fight')
    end
    -- approach; jump-approach occasionally when they poke at range
    if attacking(en) and adx < 84 and r < 0.45 then
      return hold(mem, jump_toward(edir, g), 6, 'fight')
    end
    return toward(edir, g), 'fight'
  elseif adx >= 16 then
    -- KILL INTENT: mostly swing; evasion is the seasoning, not the meal
    if attacking(en) then
      if r < 0.25 then return hold(mem, jump_toward(edir, g), 6, 'fight') end  -- vault over
      if r < 0.45 then return DOWN, 'fight' end                                 -- duck under
      return attack_toward(edir, g), 'fight'                                    -- trade (55%)
    end
    if r < 0.10 then return UP, 'fight' end      -- light stance mixups
    if r < 0.20 then return DOWN, 'fight' end
    return attack_toward(edir, g), 'fight'       -- attack (80%)
  else
    -- point blank: mostly swing, sometimes step out, rare cross-up
    if r < 0.15 then return hold(mem, jump_toward(edir, g), 6, 'fight') end
    if r < 0.70 then return attack_toward(edir, g), 'fight' end
    return toward(-edir, g), 'fight'
  end
end

function H.decide(ctx, mem)
  -- queued input sequence (e.g. a throw: direction then direction+attack)
  if mem.seq and #mem.seq > 0 then
    local a = table.remove(mem.seq, 1)
    mem.mode = mem.seq_mode or mem.mode
    return a, mem.mode
  end
  -- DON'T LINGER ON A MINE: stepping on one is safe but starts a ~1s fuse, so
  -- get off it fast. Hop toward our goal (that also keeps making progress and
  -- clears a mine we are crossing); fall back to the other clear side if the
  -- goal side is blocked. Highest-priority reflex except a committed jump arc.
  if not (mem.hold and mem.hold_t > 0) and ctx.mine and ctx.mine.near
     and ctx.snap.player.grounded and not is_dying(ctx.snap.player) then
    local g = (ctx.goal_dir or 1) >= 0 and 1 or -1
    local navF, navB = ctx.nav and ctx.nav[g], ctx.nav and ctx.nav[-g]
    local away = g
    if navF and (navF.wall or navF.gap) and navB and not navB.wall and not navB.gap then
      away = -g
    end
    return hold(mem, jump_toward(away, g), 8, 'mine!')
  end
  -- multi-tick action holds (jump arcs need sustained input)
  if mem.hold and mem.hold_t > 0 then
    mem.hold_t = mem.hold_t - 1
    if mem.hold_t <= 0 then
      local a = mem.hold
      mem.hold = nil
      if JUMP_DEGRADE[a] then mem.jump_cool = 5 end   -- force a release gap
      return a, mem.mode
    end
    return mem.hold, mem.mode
  end
  if (mem.jump_cool or 0) > 0 then mem.jump_cool = mem.jump_cool - 1 end

  local s = ctx.snap
  local me, en = s.player, s.enemy
  local g = (ctx.goal_dir or 1) >= 0 and 1 or -1
  if is_dying(me) then mem.mode = 'dead'; return IDLE, 'dead' end

  local dx = en.x - me.x
  local adx = math.abs(dx)
  local ady = math.abs(en.y - me.y)
  local edir = (dx >= 0) and 1 or -1
  local same_room = ctx.my_room == ctx.enemy_room
  local en_alive = not is_dying(en)

  -- ------------------------------------------------------------- unarmed ---
  if not me.has_sword then
    local sx, sy = s.nearest_sword_x, s.nearest_sword_y
    -- Being armed dominates almost everything, so go for a loose sword whenever
    -- one is sensed and not absurdly far. You grab a sword by CROUCHING on it
    -- OR SLIDING onto it - the slide is more forgiving (it covers the last
    -- stretch and passes through the enemy), so prefer it once we are close.
    if sx and math.abs(sx - me.x) < 360 and math.abs((sy or me.y) - me.y) < 120 then
      local sdir = ((sx - me.x) >= 0) and 1 or -1
      local sdx = math.abs(sx - me.x)
      local level = math.abs((sy or me.y) - me.y) < 26
      if sdx < 12 and level then
        -- right on it: crouch (down+jump) grabs; pulse so the edge repeats
        mem.grab_t = (mem.grab_t or 0) + 1
        mem.mode = 'grab'
        if (mem.grab_t % 8) < 4 then return DOWN_JUMP, 'grab' end
        return DOWN, 'grab'
      end
      mem.grab_t = 0
      if sdx < 64 and level and me.grounded then
        -- close and level: slide onto it (also grabs, and blows past a guard)
        return hold(mem, slide_toward(sdir, g), 12, 'get_sword')
      end
      if ctx.route and ctx.route.kind == 'sword' and ctx.route.dir and ctx.route.dir ~= 0 then
        if ctx.route.jump then
          return hold(mem, jump_toward(ctx.route.dir, g), 14, 'get_sword')
        end
        return move_or_jump(ctx, mem, ctx.route.dir, g, 'get_sword')
      end
      local nav = ctx.nav and ctx.nav[sdir]
      if (sy and sy < me.y - 24) or (nav and (nav.wall or nav.gap)) then
        return hold(mem, jump_toward(sdir, g), 8, 'get_sword')
      end
      return move_or_jump(ctx, mem, sdir, g, 'get_sword')
    end
    if same_room and en_alive and en.has_sword and adx < 70 and ady < 50 then
      local r = mem.rand()
      if adx < 34 and r < 0.40 then
        return hold(mem, jump_toward(edir, g), 5, 'disarm')          -- jump at them
      elseif r < 0.60 then
        mem.mode = 'disarm'
        return attack_toward(edir, g), 'disarm'                      -- punch
      elseif r < 0.80 then
        return hold(mem, slide_toward(edir, g), 12, 'evade')         -- slide THROUGH them
      end
      return hold(mem, jump_toward(-edir, g), 6, 'evade')            -- hop away
    end
    if same_room and en_alive and en.has_sword and adx < 130 then
      mem.mode = 'evade'
      return toward(-edir, g), 'evade'                               -- keep distance
    end
    if same_room and en_alive and (not en.has_sword) and adx < 40 and ady < 50 then
      -- fistfight: nobody has a sword - punch them out instead of shoving
      if mem.rand() < 0.30 then
        return hold(mem, jump_toward(edir, g), 5, 'fists')           -- jump punch
      end
      mem.mode = 'fists'
      return attack_toward(edir, g), 'fists'
    end
    -- fall through to navigation (advance while they can't punish)
  end

  -- --------------------------------------------------------------- fight ---
  if H.is_fight(ctx) then
    local a, m = fight(ctx, mem)
    mem.mode = m
    return a, m
  end

  -- ------------------------------------------------------------ navigate ---
  -- RUNNING (the go, an open lane, or a dead enemy): the goal ABOVE ALL ELSE.
  -- Otherwise hunt the enemy down. (ctx.intent is decided by the bot, with a
  -- stall fallback so a blocked run turns back into a hunt.)
  local running = ctx.leader == 1 or ctx.intent == 'run' or ctx.intent == 'warp' or not en_alive
  local dir
  if running then
    dir = g
    -- blocker in my lane (SAME floor - an elevated enemy is a terrain
    -- problem for the route, not a body to swing at): SLIDE through their
    -- hitbox, swing through, or vault - but never stop running
    if en_alive and same_room and (dx * g) > 0 and adx < 60 and ady < 16 then
      -- slide through, kill them, or jump over - roughly even odds
      local r = mem.rand()
      if r < 0.35 then
        return hold(mem, slide_toward(g, g), 12, 'run')      -- slide through
      elseif me.has_sword and r < 0.70 then
        mem.mode = 'run'
        return attack_toward(edir, g), 'run'                 -- cut through
      end
      return hold(mem, jump_toward(g, g), 8, 'run')          -- vault past
    end
  else
    dir = edir
    -- vertical hunt fallback (only when no planned route exists)
    if not (ctx.route and ctx.route.dir and ctx.route.dir ~= 0) and adx < 24 and ady > 40 then
      if (en.y or 0) < (me.y or 0) then
        return hold(mem, jump_toward(edir, g), 8, 'navigate')   -- enemy above: jump
      end
      -- enemy below: head for a drop (prefer goal-side gap; mirror-safe)
      local first, second = g, -g
      if ctx.nav and ctx.nav[first] and ctx.nav[first].gap then
        dir = first
      elseif ctx.nav and ctx.nav[second] and ctx.nav[second].gap then
        dir = second
      end
    end
  end
  -- planned route (bot-side pathfinder) drives all long-distance movement:
  -- jumps, drops, and wall climbs are explicit plan steps
  if ctx.route and ctx.route.dir then
    local rt = ctx.route
    if rt.climb and rt.dir ~= 0 then
      -- wall-jump chain for HEIGHT: hold INTO the wall, and press JUMP only on
      -- actual wall contact while not already rising - that is the instant a
      -- wall-jump gains height. A blind press cadence wastes presses mid-air
      -- and tops out low; contact-timing gets the full chain. Jump is
      -- edge-triggered, so a short release gap must pass between presses.
      mem.mode = 'climb'
      if (mem.climb_cool or 0) > 0 then mem.climb_cool = mem.climb_cool - 1 end
      local wall_contact = (rt.dir > 0 and me.wall_right) or (rt.dir < 0 and me.wall_left)
      if wall_contact and (me.vy or 0) > -1.0 and (mem.climb_cool or 0) == 0 then
        mem.climb_cool = 4                       -- re-arm the jump edge
        return jump_toward(rt.dir, g), 'climb'
      end
      return toward(rt.dir, g), 'climb'          -- keep pressing into the wall
    end
    mem.climb_t, mem.climb_cool = 0, 0
    if rt.jump then
      local jd = (rt.dir ~= 0) and rt.dir or dir
      return hold(mem, jump_toward(jd, g), 14, 'navigate')
    end
    if rt.dir ~= 0 then
      return move_or_jump(ctx, mem, rt.dir, g, 'navigate')
    end
  end
  mem.climb_t = 0
  local nav = ctx.nav and ctx.nav[dir]
  if nav and (nav.wall or nav.gap) then
    return hold(mem, jump_toward(dir, g), 8, 'navigate')
  end
  return move_or_jump(ctx, mem, dir, g, 'navigate')
end

return H
