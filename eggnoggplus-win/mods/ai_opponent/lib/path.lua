-- Grid pathfinding for a jumping fencer. Pure: operates on a grid object
-- { w, h, solid = fn(col,row)->bool [, hazard = fn(col,row)->bool] } with
-- 1-based cells; out-of-range cells count as open (room edges connect rooms;
-- below the map is the pit).
--
-- Nodes are STANDABLE cells (open, supported, and NOT hazardous - landing on
-- spikes kills you, so spike cells are simply not part of the graph).
-- Weighted edges (bucketed Dijkstra, so the route prefers safe easy paths and
-- takes risky/awkward moves only when they genuinely win):
--   walk  (cost 1) : adjacent standable cell, same row
--   jump  (cost 3) : up 1..MAX_JUMP_UP rows or across a 1..MAX_GAP gap
--   drop  (cost 2) : walk off an edge, land on the first standable cell below
--   climb (cost 6) : wall-jump chain up a wall face to its ledge
-- plus +4 on any move that lands NEXT TO a hazard (danger margin).
local P = {}

P.MAX_JUMP_UP = 2   -- rows a real jump reliably gains
P.MAX_DROP = 8
P.MAX_GAP = 3       -- columns a running jump clears
P.MAX_CLIMB = 6     -- rows a wall-jump chain can gain (up to 3 timed jumps)

P.COST = { walk = 1, jump = 3, drop = 2, climb = 6, danger = 4 }

local function open(g, c, r)
  if c < 1 or c > g.w or r < 1 or r > g.h then return true end
  return not g.solid(c, r)
end

local function hazardous(g, c, r)
  if not g.hazard then return false end
  if c < 1 or c > g.w or r < 1 or r > g.h then return false end
  return g.hazard(c, r) and true or false
end

function P.standable(g, c, r)
  if c < 1 or c > g.w or r < 1 or r > g.h then return false end
  if not open(g, c, r) then return false end
  if r + 1 > g.h then return false end           -- bottom row: the pit
  if not g.solid(c, r + 1) then return false end
  -- landing in a spike cell (or on spike support) kills: not a node at all
  if hazardous(g, c, r) or hazardous(g, c, r + 1) then return false end
  return true
end

-- danger margin: standing beside a hazard is worth avoiding when a clean
-- alternative exists
local function near_hazard(g, c, r)
  return hazardous(g, c - 1, r) or hazardous(g, c + 1, r) or
         hazardous(g, c, r - 1) or hazardous(g, c - 1, r + 1) or
         hazardous(g, c + 1, r + 1)
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
  -- wall climb (wall-jump chain: hold into an adjacent solid wall, timed jump
  -- presses gain height; top out on the wall's ledge)
  for _, s in ipairs({ -1, 1 }) do
    if not open(g, c + s, r) then                -- wall face beside us
      local wall_ok = true
      for dr = 1, P.MAX_CLIMB do
        if not open(g, c, r - dr) then break end -- need clear air in our column
        if open(g, c + s, r - dr) then
          -- the wall ends here: its top surface is (c+s, r-dr) if standable
          if wall_ok and P.standable(g, c + s, r - dr) and dr > P.MAX_JUMP_UP then
            n = n + 1; out[n] = { c = c + s, r = r - dr, kind = 'climb' }
          end
          break
        end
      end
    end
  end
  return n
end

-- Bucketed Dijkstra from (sc,sr) toward (gc,gr); returns the cheapest waypoint
-- list (start exclusive) to the goal, or to the REACHABLE cell nearest the
-- goal (best effort - the target may stand somewhere we can't path to).
function P.find(g, sc, sr, gc, gr)
  sc, sr = P.snap(g, sc, sr)
  if not sc then return nil end
  local snap_gc, snap_gr = P.snap(g, gc, gr)
  if snap_gc then gc, gr = snap_gc, snap_gr end

  local key = function(c, r) return r * 1024 + c end
  local came, kind, dist = {}, {}, {}
  local buckets = { [0] = { { c = sc, r = sr } } }
  local skey = key(sc, sr)
  dist[skey] = 0
  local best, best_d = { c = sc, r = sr }, math.abs(sc - gc) + math.abs(sr - gr)
  local scratch = {}
  local cost_cap = (g.w + g.h) * (P.COST.jump + P.COST.danger)
  local goal_key = key(gc, gr)
  local found = nil

  local cur_cost = 0
  while cur_cost <= cost_cap and not found do
    local bucket = buckets[cur_cost]
    if bucket then
      for bi = 1, #bucket do
        local cur = bucket[bi]
        local ck = key(cur.c, cur.r)
        if dist[ck] == cur_cost then          -- not superseded by a cheaper visit
          local d2goal = math.abs(cur.c - gc) + math.abs(cur.r - gr)
          if d2goal < best_d then best, best_d = cur, d2goal end
          if ck == goal_key then found = cur break end
          local n = neighbors(g, cur.c, cur.r, scratch)
          for i = 1, n do
            local nb = scratch[i]
            local k = key(nb.c, nb.r)
            local step = P.COST[nb.kind] or 1
            if near_hazard(g, nb.c, nb.r) then step = step + P.COST.danger end
            local nd = cur_cost + step
            if dist[k] == nil or nd < dist[k] then
              dist[k] = nd
              came[k] = cur
              kind[k] = nb.kind
              local b = buckets[nd]
              if not b then b = {}; buckets[nd] = b end
              b[#b + 1] = { c = nb.c, r = nb.r }
            end
          end
        end
      end
      buckets[cur_cost] = nil
    end
    cur_cost = cur_cost + 1
  end

  -- reconstruct (to the goal if reached, else best effort)
  local path = {}
  local node = found or best
  while node and not (node.c == sc and node.r == sr) do
    local k = key(node.c, node.r)
    table.insert(path, 1, { c = node.c, r = node.r, kind = kind[k] or 'walk' })
    node = came[k]
  end
  if #path == 0 then return nil end
  return path
end

return P
