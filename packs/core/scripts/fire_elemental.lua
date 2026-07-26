-- core:elemental (fire elemental) — behavior hooks transliterated from
-- family_fire_elemental.cpp. The descriptor name is "ELEMENTAL" (unique in
-- the living registry, so no core:#6 escape needed).
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local function do_special(self)
  -- starburst: fire the default weapon in all eight directions
  local tempx = og.trunc(self:lastx())
  local tempy = og.trunc(self:lasty())
  self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                 og.fmul(8.0, self:s_weapon_cost())))
  for i = -1, 1 do
    for j = -1, 1 do
      if i ~= 0 or j ~= 0 then
        self:set_lastx(i)
        self:set_lasty(j)
        self:fire()
      end
    end
  end
  self:set_lastx(tempx)
  self:set_lasty(tempy)
  return true
end

local function check_special_ai(self)
  return og.check_special_ai_distance(self, 130)
end

local function on_death(self)
  -- Free parting starburst: un-kill, refund the special cost, fire, re-kill.
  self:set_dead(0)
  self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                 self:s_special_cost(1)))
  self:special()
  self:set_dead(1)
  return true
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 6, 4, 8, 1)
end

local function on_act_living(self)
  -- Summoned fire elemental drains owner HP/MP to heal itself
  if self:lifetime() ~= 0 then
    local owner = self:owner()
    if owner and owner:dead() == 0 then
      if self:s_hitpoints() < self:s_max_hitpoints() then
        local temp = 0
        if owner:s_hitpoints() >= og.fdiv(owner:s_max_hitpoints(), 3.0) then
          temp = 1
          owner:s_set_hitpoints(og.fsub(owner:s_hitpoints(), 1.0))
        end
        if temp ~= 0 and owner:s_magicpoints() >= 3 then
          temp = temp + 1
          owner:s_set_magicpoints(og.fsub(owner:s_magicpoints(), 3.0))
        end
        if temp == 2 then
          self:s_set_hitpoints(og.fadd(self:s_hitpoints(), 1.0))
        else
          self:set_lifetime(self:lifetime() - 1)
        end
      end
    end
  end
end

og.register_hooks("living", "core:elemental", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  level_up = level_up,
  on_death = on_death,
  on_act_living = on_act_living,
})
