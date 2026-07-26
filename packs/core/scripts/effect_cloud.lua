-- core:cloud — effect behavior hooks transliterated from
-- effect_family_cloud.cpp. Cookbook (docs/lua-classpacks-design.md §3)
-- applies: og.rand preserves draw order/count, og.i16 reproduces the short
-- narrowing inside the hits() overlap test, and the drift deltas stay
-- integers (the C++ floats only ever hold -1/0/1, which cast exactly).
--
-- MISSING BINDING: the drift step is statistics::do_command()
-- (src/gameplay/stats.cpp:362), reached from
-- `self->stats()->do_command()` at effect_family_cloud.cpp:47 whenever the
-- cloud already has a queued walk — which is every tick after the first, so
-- the hook is unusable without it. Nothing in the og.* surface runs a queued
-- command (s_add_command / s_force_command / s_set_command / s_clear_command
-- / s_has_commands only manipulate the queue). The branch sits AFTER the
-- lifetime decrement, the invisibility update and the attack sweep, so it
-- cannot be raised in place without half-applying the tick; per the
-- conversion rule the probe is raised at function entry instead, before any
-- mutation, so dispatch falls back to the still-registered C++ cloud_on_act
-- (R9) byte-identically. When walker:s_do_command() lands, delete that one
-- line and the body below goes live as-is.

local C = og.C

-- The free function hits() from src/gameplay/effect.cpp:189 — an axis-aligned
-- box overlap test on shorts. Pure arithmetic, so it is transliterated rather
-- than bound; og.i16 reproduces the `static_cast<short>` on each sum.
local function hits(x, y, xsize, ysize, x2, y2, xsize2, ysize2)
  local x2right = og.i16(x2 + xsize2)
  if x > x2right then
    return false
  end
  local xright = og.i16(x + xsize)
  if xright < x2 then
    return false
  end
  local y2down = og.i16(y2 + ysize2)
  if y > y2down then
    return false
  end
  local ydown = og.i16(y + ysize)
  if ydown < y2 then
    return false
  end
  return true
end

-- cloud_on_act: a poison cloud ages out, fades as it expires, damages any foe
-- it overlaps, and drifts one random step at a time.
local function on_act(self)
  -- MISSING BINDING (see header): raised before any state mutation so the
  -- hook is treated as absent (R9) and the C++ callback runs untouched.
  og.MISSING_s_do_command()

  if self:lifetime() > 0 then
    self:set_lifetime(self:lifetime() - 1)
  else
    self:set_dead(1)
    self:death()
    return true
  end
  if self:lifetime() < 8 then
    self:set_invisibility_left(self:invisibility_left() + 3)
  end
  if self:invisibility_left() > 0 then
    self:set_invisibility_left(self:invisibility_left() - 1)
  end

  -- Hit any nearby foes (not friends, for now)
  local foelist = og.find_foes_in_range("ob", self:sizex(), self)
  for i = 1, #foelist do
    local w = foelist[i]
    if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
            w:xpos(), w:ypos(), w:sizex(), w:sizey()) then
      self:attack(w)
    end
  end

  -- Are we performing some action?
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
