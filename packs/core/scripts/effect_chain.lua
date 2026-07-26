-- core:chain — chain lightning, transliterated from
-- src/gameplay/families/effect_family_chain.cpp.
--
-- Cookbook (docs/lua-classpacks-design.md §3): stepsize/damage/worldx/worldy
-- are C++ floats (og.f*), the intelligence and level divisions are integer
-- (og.div), and the arc-count draw is the single RNG call in this family —
-- guarded because og.rand raises on a zero bound while C++ rng.next(0)
-- returns 0 without advancing the stream.
--
-- The frame poke `self->set_frame(self->ani[curdir()][0])` reads the walker's
-- animation table through og.ani_frame(entity, row, index); nil covers every
-- case where the C++ `if (self->ani)` guard would have failed, so the
-- set_frame is simply skipped then.

local C = og.C
local FX_EXPLOSION = og.family_id("fx", "core:explosion")
local FX_CHAIN = og.family_id("fx", "core:chain")

-- effect.cpp: hits() — axis-aligned box overlap on short coordinates. The
-- right/bottom edges are computed as shorts in C++, so og.i16 reproduces the
-- (unreachable in practice, but exact) wrap.
local function hits(x, y, xsize, ysize, x2, y2, xsize2, ysize2)
  local x2right = og.i16(x2 + xsize2)
  if x > x2right then return false end
  local xright = og.i16(x + xsize)
  if xright < x2 then return false end
  local y2down = og.i16(y2 + ysize2)
  if y > y2down then return false end
  local ydown = og.i16(y + ysize)
  if ydown < y2 then return false end
  return true
end

-- chain_on_act: the bolt homes on its leader; on contact it detonates and
-- forks into up to rand(owner level)+1 new bolts aimed at nearby foes.
local function on_act(self)
  local leader = self:leader()
  if not leader or self:lineofsight() < 1 or not self:owner() then
    self:set_dead(1)
    self:death()
    return true
  end
  -- Are we at our leader? If so, attack him :)
  if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
          leader:xpos(), leader:ypos(), leader:sizex(), leader:sizey()) then
    local newob = og.add_ob("fx", FX_EXPLOSION)
    if not newob then
      self:set_dead(1)
      self:death()
      return true
    end
    newob:set_owner(self:owner())
    newob:set_team_num(self:team_num())
    newob:s_set_level(self:s_level())
    newob:set_damage(self:damage())
    newob:set_ani_type(C.ANI_EXPLODE)
    newob:set_floor(self:floor())  -- strike on the bolt's floor (A8)
    newob:center_on(self)
    leader:set_skip_exit(leader:skip_exit() + 3)
    og.emit_sound(C.SOUND_EXPLODE)
    -- Now make new objects to seek out foes ..
    local generic = og.fmul(self:damage(), 0.5)
    local foelist, temp
    if self:owner():has_guy() then
      foelist, temp = og.find_foes_in_range(
        "ob", 240 + og.div(self:owner():g_intelligence(), 2), self)
    else
      foelist, temp = og.find_foes_in_range(
        "ob", 240 + self:s_level() * 5, self)
    end
    if temp ~= 0 and generic > 20 then
      local owner_level = self:owner():s_level()
      local roll = 0
      if owner_level > 0 then roll = og.rand(owner_level) end
      local numfoes = roll + 1
      for i = 1, #foelist do
        local w = foelist[i]
        if numfoes <= 0 then break end
        -- Chain lightning must not arc through solid floors: skip foes on
        -- other floors (A8; all-floor-0 on legacy levels so single-floor
        -- behavior is byte-identical).
        if w:floor() == self:floor() then
          if w ~= self:leader() and w:skip_exit() < 1 then
            newob = og.add_ob("fx", FX_CHAIN)
            if not newob then return true end
            newob:set_owner(self:owner())
            newob:set_leader(w)
            newob:s_set_level(self:s_level())
            newob:s_set_bit_flags(C.BIT_MAGICAL, 1)
            newob:set_damage(generic)
            newob:set_team_num(self:team_num())
            newob:set_floor(self:floor())  -- seek on our floor (A8)
            newob:center_on(self)
          end
          numfoes = numfoes - 1
        end
      end
    end

    self:set_dead(1)
    self:death()
    return true
  end
  -- Move toward our leader ..
  self:set_lineofsight(self:lineofsight() - 1)
  local distance = self:distance_to_ob_center(leader)
  if distance > og.fmul(self:stepsize(), 2) then
    local xd, yd = 0, 0
    if leader:xpos() > self:xpos() then
      if (leader:xpos() - self:xpos()) > self:stepsize() then
        xd = self:stepsize()
      else
        xd = leader:xpos() - self:xpos()
      end
    elseif leader:xpos() < self:xpos() then
      if (self:xpos() - leader:xpos()) > self:stepsize() then
        xd = -self:stepsize()
      else
        xd = leader:xpos() - self:xpos()
      end
    end
    if leader:ypos() > self:ypos() then
      if (leader:ypos() - self:ypos()) > self:stepsize() then
        yd = self:stepsize()
      else
        yd = leader:ypos() - self:ypos()
      end
    elseif leader:ypos() < self:ypos() then
      if (self:ypos() - leader:ypos()) > self:stepsize() then
        yd = -self:stepsize()
      else
        yd = leader:ypos() - self:ypos()
      end
    end
    self:set_curdir(self:facing(og.trunc(xd), og.trunc(yd)))
    local frame = og.ani_frame(self, self:curdir(), 0)
    if frame ~= nil then
      self:set_frame(frame)
    end
    self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
  else
    self:center_on(leader)
  end
  return true  -- skip default animate/die
end

og.register_hooks("fx", "core:chain", {
  on_act = on_act,
})
