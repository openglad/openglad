-- core:chain — homing chain-lightning bolt; forks on strike (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The frame poke `self->set_frame(self->ani[curdir()][0])` reads the walker's
-- animation table through og.ani_frame(entity, row, index); nil covers every
-- case where the C++ `if (self->ani)` guard would have failed, so the
-- set_frame is simply skipped then.

local C = og.C
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local FX_CHAIN = assert(og.family_id("fx", "core:chain"))

local hits = og.use("effect_common").hits

-- chain_on_act: the bolt homes on its leader; on contact it detonates and
-- forks into up to rand(owner level)+1 new bolts aimed at nearby foes.
local function on_act(self)
  local leader = self:leader()
  -- lost our leader, etc.? kill us ..
  if not leader
      or self:lineofsight() < 1
      or not self:owner() then
    self.dead = 1
    self:death()
    return true
  end
  -- Are we at our leader? If so, attack him :)
  if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
          leader:xpos(), leader:ypos(), leader:sizex(), leader:sizey()) then
    local blast = og.add_ob("fx", FX_EXPLOSION)
    if not blast then
      self.dead = 1
      self:death()
      return true
    end
    blast:set_owner(self:owner())
    blast.team = self.team
    blast.level = self.level
    blast.damage = self:damage()
    blast.ani_type = C.ANI_EXPLODE
    blast:set_floor(self:floor())  -- strike on the bolt's floor
    blast:center_on(self)
    -- can't hit us for 3 rounds ..
    leader:set_skip_exit(leader:skip_exit() + 3)
    og.emit_sound(C.SOUND_EXPLODE)
    -- Now make new objects to seek out foes ..
    local t = og.tuning(self)
    -- shim kept: damage is a C++ float: the fork's damage cut (a YAML
    -- float, 0.5) rounds through float.
    local fork_damage = og.fmul(self:damage(), t.fork_damage_mult)
    local foes, foe_count
    if self:owner():has_guy() then
      -- Int/2 stays a formula: a guy-stat conversion, not a tuning knob.
      foes, foe_count = og.find_foes_in_range(
        "ob", t.fork_range_base + self:owner():g_intelligence() // 2, self)
    else
      foes, foe_count = og.find_foes_in_range(
        "ob", t.fork_range_base + self.level * t.fork_range_per_level, self)
    end
    -- The old "are our offspring powerful enough at 1/2 our power?" gate:
    -- weak descendants stop reproducing instead of extending the chain.
    if foe_count ~= 0 and fork_damage > t.fork_min_damage then
      -- One draw: the fork budget rolls off the owner's level.
      local forks_left = og.rand0(self:owner().level) + 1
      for i = 1, #foes do
        local w = foes[i]
        if forks_left <= 0 then
          break
        end
        -- Chain lightning must not arc through solid floors: skip foes on
        -- other floors (all-floor-0 on legacy levels, so single-floor
        -- behavior is byte-identical).
        if w:floor() == self:floor() then
          if w ~= self:leader() and w:skip_exit() < 1 then
            local bolt = og.add_ob("fx", FX_CHAIN)
            if not bolt then
              return true
            end
            bolt:set_owner(self:owner())
            bolt:set_leader(w)
            bolt.level = self.level
            bolt:s_set_bit_flags(C.BIT_MAGICAL, 1)
            bolt.damage = fork_damage
            bolt.team = self.team
            bolt:set_floor(self:floor())  -- seek on our floor
            bolt:center_on(self)
          end
          forks_left = forks_left - 1
        end
      end
    end

    self.dead = 1
    self:death()
    return true
  end
  -- Move toward our leader ..
  self:set_lineofsight(self:lineofsight() - 1)
  local distance = self:distance_to_ob_center(leader)
  -- shim kept: stepsize is a C++ float: per-op float rounding.
  if distance > og.fmul(self:stepsize(), 2) then
    local xd, yd = 0, 0
    if leader:xpos() > self:xpos() then
      xd = og.min(self:stepsize(), leader:xpos() - self:xpos())
    elseif leader:xpos() < self:xpos() then
      xd = og.max(leader:xpos() - self:xpos(), -self:stepsize())
    end
    if leader:ypos() > self:ypos() then
      yd = og.min(self:stepsize(), leader:ypos() - self:ypos())
    elseif leader:ypos() < self:ypos() then
      yd = og.max(leader:ypos() - self:ypos(), -self:stepsize())
    end
    -- Set our facing?
    -- shim kept: facing() takes ints; xd/yd are floats: C truncation.
    self:set_curdir(self:facing(og.trunc(xd), og.trunc(yd)))
    local frame = og.ani_frame(self, self:curdir(), 0)
    if frame ~= nil then
      self:set_frame(frame)
    end
    -- shim kept: worldx/worldy are C++ floats: per-op float rounding.
    self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
  else
    self:center_on(leader)
  end
  return true  -- skip default animate/die
end

-- The declarations that reference this module (packs/core/families/):
--   core:chain  on_act = chain.on_act
return {
  on_act = on_act,
}
