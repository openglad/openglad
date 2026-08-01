-- core:elemental — starburst, parting shot on death, owner drain (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- (Descriptor name "ELEMENTAL" is unique in the living registry.)

local ai = og.use("ai")

local function do_special(self)
  -- "Lots o' fireballs": fire the default weapon in all eight directions.
  -- shim kept (both): the C++ parks the float aim in int temps: C truncation.
  local saved_aim_x = og.trunc(self:lastx())
  local saved_aim_y = og.trunc(self:lasty())
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, 8 * self:s_weapon_cost())
  for i = -1, 1 do
    for j = -1, 1 do
      if i ~= 0 or j ~= 0 then
        self:set_lastx(i)
        self:set_lasty(j)
        self:fire()
      end
    end
  end
  self:set_lastx(saved_aim_x)
  self:set_lasty(saved_aim_y)
  return true
end

local function on_death(self)
  -- Free parting starburst: un-kill, refund the special cost, fire, re-kill.
  self.dead = 0
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, self:s_special_cost(1))
  self:special()
  self.dead = 1
  return true
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 6, 4, 8, 1)
end

local function on_act_living(self)
  -- A summoned elemental (finite lifetime) takes the old "toll from our
  -- mage": 1 HP + 3 MP buys 1 HP of healing; if the owner cannot pay both,
  -- the elemental burns lifetime instead.
  if self:lifetime() ~= 0 then
    local owner = self:owner()
    if owner and owner:dead() == 0 then
      if self.hp < self.max_hp then
        local paid = 0
        -- shim kept: max_hp is a C++ float: per-op float rounding.
        if owner.hp >= og.fdiv(owner.max_hp, 3.0) then
          paid = 1
          -- shim kept: hp is a C++ float: per-op float rounding.
          owner.hp = og.fsub(owner.hp, 1.0)
        end
        if paid ~= 0 and owner.magicpoints >= 3 then
          paid = paid + 1
          -- shim kept: magicpoints is a C++ float: per-op float rounding.
          owner.magicpoints = og.fsub(owner.magicpoints, 3.0)
        end
        if paid == 2 then
          -- shim kept: hp is a C++ float: per-op float rounding.
          self.hp = og.fadd(self.hp, 1.0)
        else
          self:set_lifetime(self:lifetime() - 1)
        end
      end
    end
  end
end

og.register_hooks("living", "core:elemental", {
  do_special = do_special,
  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI gate
  level_up = level_up,
  on_death = on_death,
  on_act_living = on_act_living,
})
