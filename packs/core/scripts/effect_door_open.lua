-- core:door_open — hands the opened-door sprite to a fresh effect (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local FX_DOOR_OPEN = assert(og.family_id("fx", "core:door_open"))

-- door_open_on_act: once the open animation has run out (ani_type back to
-- ANI_WALK), hand the opened-door sprite off to a fresh effect at the same
-- spot and retire this one. Anything mid-animation falls through to the
-- default effect::act animate path.
local function on_act(self)
  if self:ani_type() ~= C.ANI_WALK then
    return false  -- let default animate() handle it
  end

  local opened = og.add_fx_ob("fx", FX_DOOR_OPEN)
  if not opened then
    return true  -- handled (nothing to spawn)
  end
  opened:set_ani_type(C.ANI_WALK)
  opened:set_floor(self:floor())  -- opened door stays on its floor (A8)
  opened:setworldxy(self:worldx(), self:worldy())
  opened.level = self.level
  opened.team = self.team
  opened:set_ignore(1)
  opened:set_curdir(self:curdir())
  -- animate() here only sets the correct frame
  opened:animate()
  self:set_dead(1)
  self:death()
  return true
end

og.register_hooks("fx", "core:door_open", {
  on_act = on_act,
})
