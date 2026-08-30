-- Ball surfaces — water contact and L1 damping (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C

-- The mode banks ball CENTER coordinates in signed x256 fixed point. Query
-- every tile overlapped by its inclusive pixel footprint; returning early
-- would make the helper's terrain-query shape depend on which corner is wet.
local function touches_water(ball, px, py)
  -- Fixed-point positions may be signed: C truncation, not Lua floor.
  local cx = og.div(px, 256)
  local cy = og.div(py, 256)
  local left = cx - og.div(ball:sizex(), 2)
  local top = cy - og.div(ball:sizey(), 2)
  local right = left + ball:sizex() - 1
  local bottom = top + ball:sizey() - 1
  local tile_left = og.div(left, C.GRID_SIZE)
  local tile_top = og.div(top, C.GRID_SIZE)
  local tile_right = og.div(right, C.GRID_SIZE)
  local tile_bottom = og.div(bottom, C.GRID_SIZE)
  local wet = false
  for ty = tile_top, tile_bottom do
    for tx = tile_left, tile_right do
      if og.query_genre(tx, ty, ball:floor()) == C.TYPE_WATER then
        wet = true
      end
    end
  end
  return wet
end

-- L1 direction is preserved while the surface removes energy. Dry keep=256
-- is exactly the modes' old linear -64 friction; wet keep=64 makes one water
-- contact retain one quarter before the same loss. Components may be signed:
-- C truncation keeps the fixed-point result deterministic across Lua hosts.
local function damp(vx, vy, wet)
  local speed = math.abs(vx) + math.abs(vy)
  if speed == 0 then
    return 0, 0
  end
  local keep = 256
  if wet then
    keep = 64
  end
  local slowed = og.max(0, og.div(speed * keep, 256) - 64)
  return og.div(vx * slowed, speed), og.div(vy * slowed, speed)
end

return {
  touches_water = touches_water,
  damp = damp,
}
