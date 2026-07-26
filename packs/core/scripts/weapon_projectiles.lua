-- core:fire_arrow + core:boulder — weapon behavior hooks transliterated
-- from weapon_family_projectiles.cpp. Both families share one on_death:
-- a projectile whose skip_exit is set explodes into an FX explosion.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: damage*2 is one
-- float multiply (og.fmul); setters narrow like the C++ field types.

local C = og.C
local FX_EXPLOSION = og.family_id("fx", "core:explosion")

local function explode_on_death(self)
  if self:skip_exit() == 0 then
    return false  -- skip_exit means we're supposed to explode :)
  end
  local owner = self:owner()
  if not owner or owner:dead() ~= 0 then
    self:set_owner(self)
  end
  local newob = og.add_ob("fx", FX_EXPLOSION)
  if not newob then
    return false  -- failsafe
  end
  og.emit_sound(C.SOUND_EXPLODE)
  newob:set_owner(self:owner())
  newob:s_set_hitpoints(0)
  newob:s_set_level(self:owner():s_level())
  newob:set_ani_type(C.ANI_EXPLODE)
  newob:set_floor(self:floor())  -- explode on the projectile's floor (A8)
  newob:center_on(self)
  newob:set_damage(og.fmul(self:damage(), 2.0))
  return true
end

og.register_hooks("weapon", "core:fire_arrow", {
  on_death = explode_on_death,
})

og.register_hooks("weapon", "core:boulder", {
  on_death = explode_on_death,
})
