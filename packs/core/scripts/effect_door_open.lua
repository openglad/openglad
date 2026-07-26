-- core:door_open — effect behavior hooks transliterated from
-- effect_family_door_open.cpp. Cookbook (docs/lua-classpacks-design.md §3)
-- applies; every value here is an integer field copy, so no float or
-- narrowing helpers are needed (the setters narrow exactly like the C++
-- member types).

local C = og.C
local FX_DOOR_OPEN = og.family_id("fx", "core:door_open")

-- door_open_on_act: once the open animation has run out (ani_type back to
-- ANI_WALK), hand the opened-door sprite off to a fresh effect at the same
-- spot and retire this one. Anything mid-animation falls through to the
-- default effect::act animate path.
local function on_act(self)
  if self:ani_type() ~= C.ANI_WALK then
    return false  -- let default animate() handle it
  end

  local newob = og.add_fx_ob("fx", FX_DOOR_OPEN)
  if not newob then
    return true  -- handled (nothing to spawn)
  end
  newob:set_ani_type(C.ANI_WALK)
  newob:set_floor(self:floor())  -- opened door stays on its floor (A8)
  newob:setworldxy(self:worldx(), self:worldy())
  newob:s_set_level(self:s_level())
  newob:set_team_num(self:team_num())
  newob:set_ignore(1)
  newob:set_curdir(self:curdir())
  -- set correct frame
  newob:animate()
  self:set_dead(1)
  self:death()
  return true
end

og.register_hooks("fx", "core:door_open", {
  on_act = on_act,
})
