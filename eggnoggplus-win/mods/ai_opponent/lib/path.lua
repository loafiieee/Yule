-- Grid pathfinding for a jumping fencer. Pure: operates on a grid object
-- { w, h, solid = function(col, row) -> bool } with 1-based cells; out-of-range
-- cells count as open (room edges connect rooms; below the map is the pit).
--
-- Nodes are STANDABLE cells (open, with solid support directly below).
-- Edges (all cost 1, BFS):
--   walk  : adjacent standable cell, same row
--   jump  : up 1..MAX_JUMP_UP rows (same column or 1 aside, clear headroom),
--           or across a 1-2 column gap at the same row
--   drop  : walk off an edge and land on the first standable cell below
-- The follower turns "next waypoint is higher / gap-jump" into a held jump -
-- jumping stops being a probe heuristic and becomes an explicit plan step.
local P = {}

P.MAX_JUMP_UP = 3
P.MAX_DROP = 8
P.MAX_GAP = 2

local function open(g, c, r)
  if c < 1 or c > g.w or r < 1 or r > g.h then return true end
  return not g.solid(c, r)
end

function P.standable(g, c, r)
  if c < 1 or c > g.w or r < 1 or r > g.h then return false end
  if not open(g, c, r) then return false end
  if r + 1 > g.h then return false end           -- bottom row: the pit
  return g.solid(c, r + 1) and true or false
end

-- nearest standable cell to (c,r): same column downward first (where a falling
-- body would land), then a small ring search
function P.snap(g, c, r)
  if c < 1 then c = 1 elseif c > g.w then c = g.w end
  if r < 1 then r = 1 elseif r > g.h then r = g.h end
  for rr = r, math.min(g.h, r + P.MAX_DROP) do
    if P.standable(g, c, rr) then return c, rr end
  end
  for radius = 1, 4 do
    for dc = -radius, radius do
      for dr = -radius, radius do
        if P.standable(g, c + dc, r + dr) then return c + dc, r + dr end
      end
    end
  end
  return nil
end

local function neighbors(g, c, r, out)
  local n = 0
  -- walk
  for _, dc in ipairs({ -1, 1 }) do
    if P.standable(g, c + dc, r) then
      n = n + 1; out[n] = { c = c + dc, r = r, kind = 'walk' }
    end
  end
  -- jump up (needs headroom in our column)
  local clear = true
  for dr = 1, P.MAX_JUMP_UP do
    if not open(g, c, r - dr) then clear = false end
    if not clear then break end
    for _, dc in ipairs({ 0, -1, 1 }) do
      if P.standable(g, c + dc, r - dr) and open(g, c + dc, r - dr) then
        n = n + 1; out[n] = { c = c + dc, r = r - dr, kind = 'jump' }
      end
    end
  end
  -- gap jump (same row across 1..MAX_GAP open columns)
  for _, dir in ipairs({ -1, 1 }) do
    for gap = 1, P.MAX_GAP do
      local blocked = false
      for k = 1, gap do
        if not open(g, c + dir * k, r) or P.standable(g, c + dir * k, r) then blocked = true break end
      end
      if blocked then break end
      if P.standable(g, c + dir * (gap + 1), r) then
        n = n + 1; out[n] = { c = c + dir * (gap + 1), r = r, kind = 'jump' }
        break
      end
    end
  end
  -- drop (walk off an adjacent open cell, land below)
  for _, dc in ipairs({ -1, 1 }) do
    if open(g, c + dc, r) and not P.standable(g, c + dc, r) then
      for dr = 1, P.MAX_DROP do
        if P.standable(g, c + dc, r + dr) then
          n = n + 1; out[n] = { c = c + dc, r = r + dr, kind = 'drop' }
          break
        end
        if not open(g, c + dc, r + dr) then break end
      end
    end
  end
  return n
end

-- BFS from (sc,sr) toward (gc,gr); returns waypoint list (start exclusive) to
-- the goal, or to the REACHABLE cell nearest the goal (best effort - the
-- target may stand somewhere we can't path to).
function P.find(g, sc, sr, gc, gr)
  sc, sr = P.snap(g, sc, sr)
  if not sc then return nil end
  local snap_gc, snap_gr = P.snap(g, gc, gr)
  if snap_gc then gc, gr = snap_gc, snap_gr end

  local key = function(c, r) return r * 1024 + c end
  local came = {}
  local kind = {}
  local queue = { { c = sc, r = sr } }
  local seen = { [key(sc, sr)] = true }
  local head = 1
  local best, best_d = { c = sc, r = sr }, math.abs(sc - gc) + math.abs(sr - gr)
  local scratch = {}

  while queue[head] do
    local cur = queue[head]; head = head + 1
    local dist = math.abs(cur.c - gc) + math.abs(cur.r - gr)
    if dist < best_d then best, best_d = cur, dist end
    if cur.c == gc and cur.r == gr then best = cur break end
    local n = neighbors(g, cur.c, cur.r, scratch)
    for i = 1, n do
      local nb = scratch[i]
      local k = key(nb.c, nb.r)
      if not seen[k] then
        seen[k] = true
        came[k] = cur
        kind[k] = nb.kind
        queue[#queue + 1] = { c = nb.c, r = nb.r }
      end
    end
  end

  -- reconstruct
  local path = {}
  local node = best
  while node and not (node.c == sc and node.r == sr) do
    local k = key(node.c, node.r)
    table.insert(path, 1, { c = node.c, r = node.r, kind = kind[k] or 'walk' })
    node = came[k]
  end
  if #path == 0 then return nil end
  return path
end

return P
