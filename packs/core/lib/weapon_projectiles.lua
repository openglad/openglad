-- core:fire_arrow + core:boulder — explode on death when armed (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))

local function explode_on_death(self)
  if self:skip_exit() == 0 then
    return false  -- skip_exit means we're supposed to explode :)
  end
  local owner = self:owner()
  if not owner or owner:dead() ~= 0 then
    self:set_owner(self)
  end
  local explosion = og.add_ob("fx", FX_EXPLOSION)
  if not explosion then
    return false  -- failsafe
  end
  og.emit_sound(C.SOUND_EXPLODE)
  explosion:set_owner(self:owner())
  explosion.hp = 0
  explosion.level = self:owner().level
  explosion.ani_type = C.ANI_EXPLODE
  explosion:set_floor(self:floor())  -- explode on the projectile's floor
  explosion:center_on(self)
  -- damage is a C++ float: per-op rounding.
  explosion.damage = og.fmul(self:damage(), 2.0)
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:fire_arrow  on_death = projectiles.explode_on_death
--   core:boulder     on_death = projectiles.explode_on_death
return {
  explode_on_death = explode_on_death,
}
