-- core:cloud — drifting, fading poison cloud (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The drift step is statistics::do_command() (src/gameplay/stats.cpp), reached
-- from `self->stats()->do_command()` whenever the cloud already has a queued
-- walk — which is every tick after the first. walker:s_do_command() is the
-- only binding that EXECUTES the queue (the other s_*_command calls just
-- manipulate it); its short return value is discarded here exactly as in C++.

local C = og.C

local hits = og.use("effect_common").hits

-- cloud_on_act: a poison cloud ages out, fades as it expires, damages any foe
-- it overlaps, and drifts one random step at a time.
local function on_act(self)
  if self:lifetime() > 0 then
    self:set_lifetime(self:lifetime() - 1)
  else
    self.dead = 1
    self:death()
    return true
  end
  if self:lifetime() < 8 then
    self:set_invisibility_left(self:invisibility_left() + 3)
  end
  if self:invisibility_left() > 0 then
    self:set_invisibility_left(self:invisibility_left() - 1)
  end

  -- Foes only: friendlies stand in the cloud unharmed (the C++ kept the
  -- friendly branch "for now", and parity keeps it forever).
  local foes = og.find_foes_in_range("ob", self:sizex(), self)
  for i = 1, #foes do
    local w = foes[i]
    if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
            w:xpos(), w:ypos(), w:sizex(), w:sizey()) then
      self:attack(w)
    end
  end

  if self:s_has_commands() then
    self:s_do_command()
  else
    -- Two separate C++ statements, so the draw order is defined: xd then yd,
    -- re-rolled together until the step is non-zero.
    local xd, yd = 0, 0
    while xd == 0 and yd == 0 do
      xd = og.rand(3) - 1
      yd = og.rand(3) - 1
    end
    self:s_add_command(C.COMMAND_WALK, og.rand(20), xd, yd)
  end
  return true
end

og.register_hooks("fx", "core:cloud", {
  on_act = on_act,
})
