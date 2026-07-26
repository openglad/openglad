-- core:rock — weapon behavior hooks transliterated from
-- weapon_family_rock.cpp. Cookbook (docs/lua-classpacks-design.md §3)
-- applies: xpos/ypos are shorts and lastx/lasty floats, so every C++
-- coordinate sum is one float op (og.fadd/og.fsub); setxy narrows its
-- float arguments to short exactly like the C++ call site.

-- rock_on_death: a bouncing rock (do_bounce set by the elf special) that
-- died against a barrier un-deads itself and reflects off whichever axis
-- is open, instead of dying.
local function on_death(self)
  if self:do_bounce() == 0 or self:lineofsight() == 0
      or self:collide_ob() ~= nil then  -- died of natural causes
    return false
  end
  self:set_dead(0)  -- first, un-dead us so we can collide ..
  -- Did we hit a barrier?
  if og.query_grid_passable(og.fadd(self:xpos(), self:lastx()),
                            og.fadd(self:ypos(), self:lasty()), self) then
    self:set_dead(1)
    return false  -- if not, die like normal
  end
  if og.query_grid_passable(og.fsub(self:xpos(), self:lastx()),
                            og.fadd(self:ypos(), self:lasty()), self) then
    -- bounce 'down-left'
    self:setxy(og.fsub(self:xpos(), self:lastx()),
               og.fadd(self:ypos(), self:lasty()))
    self:set_lastx(-self:lastx())
    self:set_death_called(0)
    return true
  end
  if og.query_grid_passable(og.fadd(self:xpos(), self:lastx()),
                            og.fsub(self:ypos(), self:lasty()), self) then
    -- bounce 'up-right'
    self:setxy(og.fadd(self:xpos(), self:lastx()),
               og.fsub(self:ypos(), self:lasty()))
    self:set_lasty(-self:lasty())
    self:set_death_called(0)
    return true
  end
  if og.query_grid_passable(og.fsub(self:xpos(), self:lastx()),
                            og.fsub(self:ypos(), self:lasty()), self) then
    self:setxy(og.fsub(self:xpos(), self:lastx()),
               og.fsub(self:ypos(), self:lasty()))
    self:set_lastx(-self:lastx())
    self:set_lasty(-self:lasty())
    self:set_death_called(0)
    return true
  end
  -- Else we're really stuck, so die :)
  self:set_dead(1)
  return false
end

og.register_hooks("weapon", "core:rock", {
  on_death = on_death,
})
