-- core lib: effect_common — shared effect geometry: the C++ hits() box-overlap test (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local M = {}

-- The free function hits() from src/gameplay/effect.cpp — an axis-aligned
-- box overlap test on shorts. Pure arithmetic lives here without a binding;
-- og.i16 reproduces the `static_cast<short>` on each sum's (unreachable in
-- practice, but exact) wrap.
function M.hits(x, y, xsize, ysize, x2, y2, xsize2, ysize2)
  -- shim kept (all four og.i16 sums): short edges, per the note above.
  local x2right = og.i16(x2 + xsize2)
  -- rule: Effects touch when their axis-aligned boxes overlap or merely share an edge (inclusive
  --   bounds): a box exactly one pixel past the other's edge does not touch.
  -- tests: PackLuaCloud.a_foe_in_range_but_outside_the_cloud_box_is_unharmed
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  -- tests: PackLuaCloud.a_cloud_damages_the_foe_it_covers_and_spares_its_own_team
  -- tests: SemLibsEffectCommon.a_box_that_only_shares_an_edge_still_touches
  if x > x2right then
    return false
  end
  local xright = og.i16(x + xsize)
  -- rule: Part of the rule above that begins "Effects touch when their axis-aligned boxes overlap
  --   or merely ..."
  -- tests: PackLuaCloud.a_foe_in_range_but_outside_the_cloud_box_is_unharmed
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  -- tests: PackLuaCloud.a_cloud_damages_the_foe_it_covers_and_spares_its_own_team
  -- tests: SemLibsEffectCommon.a_box_that_only_shares_an_edge_still_touches
  if xright < x2 then
    return false
  end
  local y2down = og.i16(y2 + ysize2)
  -- rule: Part of the rule above that begins "Effects touch when their axis-aligned boxes overlap
  --   or merely ..."
  -- tests: PackLuaCloud.a_foe_in_range_but_outside_the_cloud_box_is_unharmed
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  -- tests: PackLuaCloud.a_cloud_damages_the_foe_it_covers_and_spares_its_own_team
  -- tests: SemLibsEffectCommon.a_box_that_only_shares_an_edge_still_touches
  if y > y2down then
    return false
  end
  local ydown = og.i16(y + ysize)
  -- rule: Part of the rule above that begins "Effects touch when their axis-aligned boxes overlap
  --   or merely ..."
  -- tests: PackLuaCloud.a_foe_in_range_but_outside_the_cloud_box_is_unharmed
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  -- tests: PackLuaCloud.a_cloud_damages_the_foe_it_covers_and_spares_its_own_team
  -- tests: SemLibsEffectCommon.a_box_that_only_shares_an_edge_still_touches
  if ydown < y2 then
    return false
  end
  return true
end

return M
