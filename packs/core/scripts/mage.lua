-- core:mage — behavior hooks transliterated from family_mage.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order (mage draws none directly).

local C = og.C
local FX_MARKER = og.family_id("fx", "core:marker")
local FX_EXPLOSION = og.family_id("fx", "core:explosion")
local WEAPON_WAVE = og.family_id("weapon", "core:wave")

-- og::combat::kStarburstAddCap (include/openglad/core/combat_math.h),
-- pure constant with no og.C binding: per-fireball damage add cap.
local STARBURST_ADD_CAP = 40

local function handle_teleport(self)
  self:set_ani_type(C.ANI_TELE_IN)
  self:set_cycle(0)
  self:teleport()
  return true
end

local function check_special_ai(self)
  -- count_foes_in_range(self, 110) == the howmany out-param of the same
  -- world->find_foes_in_range(oblist, ...) call the binding performs.
  local _, howmany = og.find_foes_in_range("ob", 110, self)
  return howmany < 1 or howmany > 3
end

-- The trampoline passes stats->controller() (the stats' owner walker) as
-- self, so the C++ body's `controller` IS self and `stats->` maps to s_*.
local function hit_response(self, foe)
  local threshold
  if self:has_guy() then
    threshold = og.fdiv(og.fmul(3.0, self:s_max_hitpoints()), 5.0)
  else
    threshold = og.fdiv(og.fmul(3.0, self:s_max_hitpoints()), 8.0)
  end

  local possible = {}
  for i = 0, og.div(self:s_level() + 2, 3) do
    if i < C.NUM_SPECIALS
        and self:s_magicpoints() >= self:s_special_cost(i) then
      possible[i] = 1
    end
  end

  if self:s_hitpoints() < threshold and possible[1] then
    self:set_current_special(1)
    self:set_shifter_down(0)
    self:set_busy(0)
    self:special()
  else
    if self:foe() ~= foe then
      self:set_foe(foe)
      foe:set_foe(self)
      self:s_set_current_distance(15000)
      self:s_set_last_distance(15000)
    end
  end
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 7.0, 14.0, 3.0, 0.5)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 6, 4, 16, 1)
end

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- teleport
    if self:ani_type() == C.ANI_TELE_OUT
        or self:ani_type() == C.ANI_TELE_IN then
      return false
    end
    if self:shifter_down() ~= 0 then
      -- leave/remove a marker
      if self:busy() > 0 then
        return false
      end
      if self:has_guy() and self:g_intelligence() < 75 then
        if self:user() ~= -1 then
          og.emit_notification("Need 75 Int for Marker!")
        end
        return false
      end
      -- Remove a marker, if present
      local obs = og.oblist()
      for i = 1, #obs do
        local ob = obs[i]
        if ob
            and ob:order() == C.ORDER_FX
            and ob:family() == FX_MARKER
            and ob:owner() == self
            and ob:dead() == 0 then
          ob:set_dead(1)
          ob:death()
          if (self:team_num() == 0 or self:has_guy())
              and self:user() ~= -1 then
            og.emit_notification("(Old Marker Removed)")
          end
          self:set_busy(og.fadd(self:busy(), 8.0))
          break
        end
      end
      -- The C++ resets generic = 0 after the removal loop ("force new
      -- placement, for now"), so a marker is always placed here.
      local newob = og.add_ob("fx", FX_MARKER)
      if not newob then
        return false
      end
      newob:set_owner(self)
      newob:set_floor(self:floor())  -- marker on the caster's floor (A8)
      newob:center_on(self)
      if self:has_guy() then
        newob:set_lifetime(og.div(self:g_intelligence(), 33))
      else
        newob:set_lifetime(og.div(self:s_level(), 4) + 1)
      end
      newob:set_ani_type(C.ANI_SPIN)
      if (self:team_num() == 0 or self:has_guy())
          and self:user() ~= -1 then
        og.emit_notification("Teleport Marker Placed")
        og.emit_notification(string.format("(%d Uses)", newob:lifetime()))
      end
      self:set_busy(og.fadd(self:busy(), 8.0))
      local generic = og.trunc(og.fsub(
        self:s_magicpoints(),
        self:s_special_cost(self:current_special())))
      generic = og.div(generic, 2)
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
    else
      og.emit_positional_sound(self, C.SOUND_TELEPORT)
      self:set_ani_type(C.ANI_TELE_OUT)
      self:set_cycle(0)
    end
  elseif sp == 2 then
    -- starburst
    local tempx = og.trunc(self:lastx())
    local tempy = og.trunc(self:lasty())
    local generic = og.trunc(og.fsub(
      self:s_magicpoints(),
      self:s_special_cost(self:current_special())))
    if generic > 0 then
      -- §2.12: per-fireball add binds only above 660 MP; lineofsight
      -- add/3 inherits the bound.
      local g15 = og.div(generic, 15)
      if g15 < STARBURST_ADD_CAP then
        generic = g15
      else
        generic = STARBURST_ADD_CAP
      end
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
    else
      generic = 0
    end
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   og.fmul(8.0, self:s_weapon_cost())))
    for i = -1, 1 do
      for j = -1, 1 do
        if i ~= 0 or j ~= 0 then
          self:set_lastx(i)
          self:set_lasty(j)
          local newob = self:fire()
          if newob then
            newob:set_damage(og.fadd(newob:damage(), generic))
            newob:set_lineofsight(newob:lineofsight() + og.div(generic, 3))
            if newob:lastx() ~= 0.0 then
              newob:set_lastx(og.fdiv(newob:lastx(),
                                      math.abs(newob:lastx())))
            end
            if newob:lasty() ~= 0.0 then
              newob:set_lasty(og.fdiv(newob:lasty(),
                                      math.abs(newob:lasty())))
            end
          end
        end
      end
    end
    self:set_lastx(tempx)
    self:set_lasty(tempy)
  elseif sp == 3 then
    -- freeze time. enemy_freeze is relative to world.my_team: a Mage on
    -- the player's team banks a global time-stop; anyone else grants
    -- bonus_rounds to its own side instead.
    if self:team_num() == og.u8(og.my_team()) then
      og.set_enemy_freeze(og.enemy_freeze() + 20 + 11 * self:s_level())
      og.set_palette(1)
      og.emit_event(C.EVENT_SET_PALETTE, 1)
    else
      local generic = 5 + 2 * self:s_level()
      if generic > 50 then
        generic = 50
      end
      og.emit_notification(
        string.format("TIME IS FROZEN! (%d rounds)", generic), 2)
      og.emit_event(C.EVENT_REQUEST_REDRAW)
      local newlist = og.find_friends_in_range("ob", 30000, self)
      for i = 1, #newlist do
        local w = newlist[i]
        if w then
          w:set_bonus_rounds(w:bonus_rounds() + generic)
        end
      end
    end
  elseif sp == 4 then
    -- energy wave
    local newob = self:fire()
    if not newob then
      return false
    end
    local alive = og.add_ob("weapon", WEAPON_WAVE)
    if not alive then
      return false
    end
    alive:set_floor(newob:floor())  -- wave rides the caster's floor (A8)
    alive:center_on(newob)
    alive:set_owner(self)
    alive:s_set_level(self:s_level())
    alive:set_lastx(newob:lastx())
    alive:set_lasty(newob:lasty())
    newob:set_dead(1)
  else
    -- heartburst (case 5 and the default case)
    local newlist, howmany = og.find_foes_in_range(
      "ob", 80 + 2 * self:s_level(), self)
    if howmany == 0 then
      return false
    end
    local generic = og.trunc(og.fsub(self:s_magicpoints(),
                                     self:s_special_cost(5)))
    -- §2.12: heartburst pool binds only above ~1300 MP.
    local g2 = og.div(generic, 2)
    if g2 < C.MP_POOL_DAMAGE_CAP then
      generic = g2
    else
      generic = C.MP_POOL_DAMAGE_CAP
    end
    generic = og.div(generic, howmany)
    if self:has_guy() then
      self:g_set_total_shots(self:g_total_shots() + howmany)
      self:g_set_scen_shots(self:g_scen_shots() + howmany)
    end
    self:set_busy(og.fadd(self:busy(), 5.0))
    for i = 1, #newlist do
      local ob = newlist[i]
      local newob = og.summon(self, "fx", FX_EXPLOSION)
      if not newob then
        return false
      end
      newob:set_damage(generic)
      -- Heartburst bursts materialize ON each acquired foe, so they take
      -- that target's floor (A8); the blast itself only damages same-floor
      -- walkers (explosion_on_death floor filter).
      newob:set_floor(ob:floor())
      newob:center_on(ob)
      og.emit_sound(C.SOUND_EXPLODE)
      newob:set_ani_type(C.ANI_EXPLODE)
      newob:s_set_bit_flags(C.BIT_MAGICAL, 1)
      newob:set_skip_exit(100)
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
    end
  end
  return true
end

-- promotion_new_level (mage_promotion_level) is descriptor data, not one of
-- the registrable living hooks — it stays on the C++ descriptor.

og.register_hooks("living", "core:mage", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  hit_response = hit_response,
  set_difficulty = set_difficulty,
  level_up = level_up,
  handle_teleport = handle_teleport,
})
