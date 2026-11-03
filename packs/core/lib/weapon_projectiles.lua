-- core:fire_arrow + core:boulder — explode on death when armed (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))

local function explode_on_death(self)
  -- rule: A fire arrow or boulder explodes on death only when armed (skip_exit set by the firing
  --   special); an unarmed one just dies silently.
  -- tests: WeapBehavior.weap_death_fire_arrow_exploding
  -- tests: WeapBehavior.weap_death_fire_arrow_no_explode, WeapBehavior.weap_death_boulder_exploding
  if self:skip_exit() == 0 then
    return false  -- skip_exit means we're supposed to explode :)
  end
  local owner = self:owner()
  -- rule: An exploding projectile whose shooter is gone or dead credits the explosion to the
  --   projectile itself: the explosion's owner is the projectile and its level is the projectile's
  --   own level, not the dead shooter's.
  -- tests: WeapBehavior.weap_death_fire_arrow_exploding
  -- tests: SemLibsProjectiles.an_explosion_whose_shooter_is_dead_or_gone_is_credited_to_the_projectile
  if not owner or owner:dead() ~= 0 then
    self:set_owner(self)
  end
  local explosion = og.add_ob("fx", FX_EXPLOSION)
  -- plumbing: add_ob('fx', core:explosion) failure guard ('failsafe').
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
