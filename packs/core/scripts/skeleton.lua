-- core:skeleton — behavior hooks transliterated from family_skeleton.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local C = og.C

local function handle_teleport(self)
  self:set_ani_type(C.ANI_TELE_IN)
  self:set_cycle(0)
  self:teleport_ranged(self:s_level() * 18)
  return true
end

local function check_special_ai(self)
  local _, howmany = og.find_foes_in_range("ob", 5 * 16, self) -- 5 * GRID_SIZE
  return howmany < 1
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 8, 12, 4, 4, 1)
end

local function do_special(self)
  if self:ani_type() == C.ANI_TELE_OUT or self:ani_type() == C.ANI_TELE_IN then
    return false
  end
  self:set_ani_type(C.ANI_TELE_OUT)
  self:set_cycle(0)
  return true
end

og.register_hooks("living", "core:skeleton", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  level_up = level_up,
  handle_teleport = handle_teleport,
})
