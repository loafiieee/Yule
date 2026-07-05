local P = dofile("lib/path.lua")

-- build a grid from ascii rows ('#' solid, '.' open); 1-based cells
local function G(rows)
  local h = #rows
  local w = #rows[1]
  return {
    w = w, h = h,
    solid = function(c, r)
      if c < 1 or c > w or r < 1 or r > h then return false end
      return rows[r]:sub(c, c) == '#'
    end,
  }
end

-- flat floor: walk straight across
local flat = G({
  "........",
  "........",
  "########",
})
check(P.standable(flat, 1, 2), "floor cell standable")
check(not P.standable(flat, 1, 1), "air cell not standable")
local p1 = P.find(flat, 1, 2, 8, 2)
check(p1 and #p1 == 7, "flat path length 7 (" .. tostring(p1 and #p1) .. ")")
check(p1 and p1[1].kind == 'walk', "flat path walks")
check(p1 and p1[#p1].c == 8 and p1[#p1].r == 2, "flat path reaches goal")

-- step up: requires a jump edge
local step = G({
  "........",
  "....####",
  "########",
})
local p2 = P.find(step, 1, 2, 8, 1)
check(p2 ~= nil, "step path found")
local has_jump = false
for _, wp in ipairs(p2 or {}) do if wp.kind == 'jump' then has_jump = true end end
check(has_jump, "step path includes a jump")
check(p2 and p2[#p2].c == 8 and p2[#p2].r == 1, "step path reaches the ledge")

-- gap: jump across two open columns
local gapg = G({
  "........",
  "........",
  "###..###",
})
local p3 = P.find(gapg, 2, 2, 7, 2)
check(p3 ~= nil, "gap path found")
local gap_jump = false
for _, wp in ipairs(p3 or {}) do
  if wp.kind == 'jump' and wp.r == 2 then gap_jump = true end
end
check(gap_jump, "gap path jumps the hole")

-- platform above: jump up to it
local plat = G({
  "........",
  "...###..",
  "........",
  "########",
})
local p4 = P.find(plat, 1, 3, 5, 1)
check(p4 ~= nil, "platform path found")
check(p4 and p4[#p4].r == 1, "platform path ends on the platform")

-- drop down from a ledge (stand on the ledge at r1, floor at r3)
local ledge = G({
  "........",
  "##......",
  "........",
  "########",
})
local p5 = P.find(ledge, 1, 1, 8, 3)
check(p5 ~= nil, "drop path found")
local has_drop = false
for _, wp in ipairs(p5 or {}) do if wp.kind == 'drop' then has_drop = true end end
check(has_drop, "path walks off the ledge (drop edge)")

-- unreachable goal: best-effort path toward it
local walled = G({
  "....#...",
  "....#...",
  "####@###",   -- '@' is not '#' so open... make the wall full instead
})
walled = G({
  "....#...",
  "....#...",
  "####,###",
})
-- column 5 is solid from top to floor? rows: r1 c5 '#', r2 c5 '#', r3 c5 ','(open)
-- floor gap under the wall means the right side is reachable by walking under? no:
-- r3 c5 open with nothing below = pit hole; right side unreachable
local p6 = P.find(walled, 1, 2, 8, 2)
check(p6 ~= nil, "best-effort path exists")
check(p6 and p6[#p6].c <= 4, "best-effort stops before the wall (c=" .. tostring(p6 and p6[#p6].c) .. ")")

-- tall wall (3 rows: beyond a plain jump) topped by a ledge -> wall-jump climb
local wallg = G({
  "........",
  "....#...",
  "....#...",
  "....#...",
  "########",
})
local p7 = P.find(wallg, 4, 4, 5, 1)
check(p7 ~= nil, "climb path found")
local has_climb = false
for _, wp in ipairs(p7 or {}) do if wp.kind == 'climb' then has_climb = true end end
check(has_climb, "path climbs the wall (wall-jump chain)")
check(p7 and p7[#p7].c == 5 and p7[#p7].r == 1, "climb tops out on the ledge")

-- snap: airborne start snaps to the landing cell below
local sc, sr = P.snap(flat, 3, 1)
check(sc == 3 and sr == 2, "snap falls to the floor")

-- hazards: 'x' = lethal (spikes, never a node) / 'o' = soft (mine: passable)
local function GH(rows, hazrows, softrows)
  local h, w = #rows, #rows[1]
  return {
    w = w, h = h,
    solid = function(c, r)
      if c < 1 or c > w or r < 1 or r > h then return false end
      return rows[r]:sub(c, c) == '#'
    end,
    hazard = function(c, r)
      if not hazrows or c < 1 or c > w or r < 1 or r > h then return false end
      return hazrows[r]:sub(c, c) == 'x'
    end,
    softhazard = function(c, r)
      if not softrows or c < 1 or c > w or r < 1 or r > h then return false end
      return softrows[r]:sub(c, c) == 'o'
    end,
  }
end

-- spikes (lethal): the cell above is never a node, so the route jumps the gap
local spikec = GH({
  "........",
  "........",
  "########",
}, {
  "........",
  "........",
  "...xx...",
})
check(not P.standable(spikec, 4, 2), "cell above spikes is not standable")
local ps = P.find(spikec, 1, 2, 8, 2)
local spike_jump = false
for _, wp in ipairs(ps or {}) do if wp.kind == 'jump' then spike_jump = true end end
check(spike_jump, "route jumps over lethal spikes")

-- mines (timed): SAFE to step on briefly, so passable - the cell above a mine
-- IS standable and a route may cross it (at extra cost), not forced to jump
local minec = GH({
  "........",
  "........",
  "########",
}, nil, {
  "........",
  "........",
  "...oo...",
})
check(P.standable(minec, 4, 2), "can stand above a mine (safe to step on, timed)")
local pm = P.find(minec, 1, 2, 8, 2)
check(pm ~= nil and pm[#pm].c == 8, "route crosses the mine to the far side")
