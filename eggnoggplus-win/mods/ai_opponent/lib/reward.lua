-- Potential-based reward shaping: the explicit statement of WHAT WE WANT.
--
-- Phi(state) measures "how close this fighter is to winning an eggnogg match".
-- The per-tick reward is Phi(now) - Phi(before): fighters are continuously
-- REWARDED for making progress toward winning, not just punished at the rare
-- kill/score events. Potential-based shaping cannot be farmed by oscillating
-- (advance+retreat nets zero) and camping yields exactly zero while any
-- opponent that progresses pulls ahead - so evolution always has a gradient.
--
-- What "progress toward winning" means, explicitly:
--   territory  : every pixel advanced toward your goal end       (+0.05/px)
--   armament   : holding a sword                                 (+15)
--   dominance  : the enemy being disarmed                        (+10)
--   right of way: being the leader (you may advance)             (+25)
--   the hunt   : when NOT leader you need a kill - being closer
--                to the enemy is progress                        (up to +6)
--
-- Big sparse events (kills +-, scores +-) are paid separately by the trainers.
local RW = {}

RW.W = {
  X = 0.05,
  X_LEAD = 0.10,     -- EXTRA territory rate while leading: with the go,
                     -- advancing is worth 3x - the goal above all else
  SWORD = 15,
  DISARM = 10,
  LEAD = 25,
  HUNT = 0.05,       -- without the go, closing on the enemy IS the job
  HUNT_RANGE = 200,
  DELTA_CLAMP = 8,   -- per-tick clamp: teleports/respawns can't spike rewards
}

-- o = { x, goal (+1/-1), has_sword, enemy_has_sword, is_leader, dist }
function RW.potential(o)
  local W = RW.W
  local p = W.X * (o.x * o.goal)
  if o.has_sword then p = p + W.SWORD end
  if not o.enemy_has_sword then p = p + W.DISARM end
  if o.is_leader then
    p = p + W.LEAD + W.X_LEAD * (o.x * o.goal)
  else
    local dist = o.dist or W.HUNT_RANGE
    if dist > W.HUNT_RANGE then dist = W.HUNT_RANGE end
    p = p + W.HUNT * (W.HUNT_RANGE - dist)
  end
  return p
end

-- prev may be nil (baseline tick / after respawn or map hop) -> no reward
function RW.delta(prev, cur)
  if not prev then return 0 end
  local dv = RW.potential(cur) - RW.potential(prev)
  local c = RW.W.DELTA_CLAMP
  if dv > c then dv = c elseif dv < -c then dv = -c end
  return dv
end

return RW
