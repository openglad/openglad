-- core:door_open — hands the opened-door sprite to a fresh effect (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- Here is how doors work.  They start out as a FAMILY_DOOR
--  from ORDER_WEAPON under the weaplist.  When the door is
--  collided with, the obmap marks the door as dead, and spawns
--  the FAMILY_DOOR_OPEN on the weaplist (this object).  It
--  animates ANI_DOOR_OPEN, and when it is done, it dies and
--  spawns a FAMILY_DOOR_OPEN on the fxlist.  The amusing part
--  is that now that it is on the fxlist, it won't act anymore,
--  thus preventing it from continuously respawning itself.

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

  -- The original comment called this "the amusing part": the final copy goes
  -- on the non-acting FX list, so it cannot continuously respawn itself.
  local opened = og.add_fx_ob("fx", FX_DOOR_OPEN)
  if not opened then
    return true  -- handled (nothing to spawn)
  end
  opened:set_ani_type(C.ANI_WALK)
  opened:set_floor(self:floor())  -- opened door stays on its floor
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
