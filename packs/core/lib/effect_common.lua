-- core lib: effect_common — shared effect geometry: the C++ hits() box-overlap test (cookbook: docs/lua-classpacks-design.md §3).

local M = {}

-- The free function hits() from src/gameplay/effect.cpp — an axis-aligned
-- box overlap test on shorts. Pure arithmetic, so it is transliterated
-- rather than bound; og.i16 reproduces the `static_cast<short>` on each
-- sum's (unreachable in practice, but exact) wrap.
function M.hits(x, y, xsize, ysize, x2, y2, xsize2, ysize2)
  -- shim kept (all four og.i16 sums): short edges, per the note above.
  local x2right = og.i16(x2 + xsize2)
  if x > x2right then
    return false
  end
  local xright = og.i16(x + xsize)
  if xright < x2 then
    return false
  end
  local y2down = og.i16(y2 + ysize2)
  if y > y2down then
    return false
  end
  local ydown = og.i16(y + ysize)
  if ydown < y2 then
    return false
  end
  return true
end

return M
