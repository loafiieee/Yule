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

H.ENGAGE_X = 120   -- fight-context range (bot.lua uses H.is_fight for the NN arbiter)
H.ENGAGE_Y = 60

function H.init(deps) H._NN = deps.NN end

function H.new_mem(seed)
  return { rand = H._NN.rng_new(seed or 1), hold = nil, hold_t = 0, mode = 'idle' }
end

local function is_dying(p) local s = p.state_id or 0; return s == 8 or s == 9 end
local function attacking(p) return (((p.cmd_bits or 0) % 4) >= 2) end

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

-- Fight context: same room, close, both alive, and I'm armed. One source of
-- truth for "should the combat brain (NN or scripted) be driving".
-- Exception: when I'm the leader and the enemy is BEHIND my run, keep running
-- unless they're right on top of me - turning around throws away the lead.
function H.is_fight(ctx)
  local s = ctx.snap
  if not s.player.has_sword then return false end
  if is_dying(s.player) or is_dying(s.enemy) then return false end
  if ctx.my_room ~= ctx.enemy_room then return false end
  local dx = s.enemy.x - s.player.x
  local adx = math.abs(dx)
  if adx >= H.ENGAGE_X or math.abs(s.enemy.y - s.player.y) >= H.ENGAGE_Y then return false end
  local g = (ctx.goal_dir or 1) >= 0 and 1 or -1
  if ctx.leader == 1 and (dx * g) < 0 and adx > 40 then return false end
  return true
end

local function hold(mem, action, ticks, mode)
  mem.hold = action
  mem.hold_t = ticks
  mem.mode = mode
  return action, mode
end

-- scripted fight micro (also the NN's sparring baseline and fallback)
local function fight(ctx, mem)
  local s = ctx.snap
  local me, en = s.player, s.enemy
  local g = ctx.goal_dir
  local dx = en.x - me.x
  local adx = math.abs(dx)
  local edir = (dx >= 0) and 1 or -1
  local r = mem.rand()

  -- anti-air: enemy jumping at me -> point up / swing
  if not en.grounded and adx < 64 then
    if r < 0.5 then return UP, 'fight' end
    return attack_toward(edir, g), 'fight'
  end

  if adx > 46 then
    -- approach; jump-approach sometimes when they poke at range
    if attacking(en) and adx < 84 and r < 0.35 then
      return hold(mem, jump_toward(edir, g), 6, 'fight')
    end
    return toward(edir, g), 'fight'
  elseif adx >= 16 then
    if attacking(en) then
      if r < 0.45 then return hold(mem, jump_toward(edir, g), 6, 'fight') end  -- vault over
      if r < 0.70 then return DOWN, 'fight' end                                 -- duck under
      return attack_toward(edir, g), 'fight'                                    -- trade
    end
    if r < 0.18 then return UP, 'fight' end      -- stance mixups
    if r < 0.36 then return DOWN, 'fight' end
    return attack_toward(edir, g), 'fight'
  else
    -- point blank: cross-up, swing, or step out
    if r < 0.30 then return hold(mem, jump_toward(edir, g), 6, 'fight') end
    if r < 0.60 then return attack_toward(edir, g), 'fight' end
    return toward(-edir, g), 'fight'
  end
end

function H.decide(ctx, mem)
  -- multi-tick action holds (jump arcs need sustained input)
  if mem.hold and mem.hold_t > 0 then
    mem.hold_t = mem.hold_t - 1
    if mem.hold_t <= 0 then local a = mem.hold; mem.hold = nil; return a, mem.mode end
    return mem.hold, mem.mode
  end

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
    if sx and math.abs(sx - me.x) < 240 and math.abs((sy or me.y) - me.y) < 90 then
      local sdir = ((sx - me.x) >= 0) and 1 or -1
      local sdx = math.abs(sx - me.x)
      if sdx < 10 then mem.mode = 'grab'; return DOWN, 'grab' end   -- crouch onto it
      local nav = ctx.nav and ctx.nav[sdir]
      if (sy and sy < me.y - 24) or (nav and (nav.wall or nav.gap)) then
        return hold(mem, jump_toward(sdir, g), 8, 'get_sword')
      end
      mem.mode = 'get_sword'
      return toward(sdir, g), 'get_sword'
    end
    if same_room and en_alive and en.has_sword and adx < 70 and ady < 50 then
      local r = mem.rand()
      if adx < 34 and r < 0.45 then
        return hold(mem, jump_toward(edir, g), 5, 'disarm')          -- jump at them
      elseif r < 0.70 then
        mem.mode = 'disarm'
        return attack_toward(edir, g), 'disarm'                      -- punch
      end
      return hold(mem, jump_toward(-edir, g), 6, 'evade')            -- hop away
    end
    if same_room and en_alive and en.has_sword and adx < 130 then
      mem.mode = 'evade'
      return toward(-edir, g), 'evade'                               -- keep distance
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
  -- advance toward the goal when it's my run (leader / enemy dead / enemy
  -- already behind me); otherwise chase the enemy down
  local dir
  if ctx.leader == 1 or not en_alive or (dx * g) < 0 then
    dir = g
  else
    dir = edir
  end
  local nav = ctx.nav and ctx.nav[dir]
  if nav and (nav.wall or nav.gap) then
    return hold(mem, jump_toward(dir, g), 8, 'navigate')
  end
  mem.mode = 'navigate'
  return toward(dir, g), 'navigate'
end

return H
