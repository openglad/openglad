-- core:mage — teleport/marker, starburst, freeze time, wave, heartburst (cookbook: docs/lua-classpacks-design.md §3).
-- rng: the mage's own hooks draw nothing directly.

local C = og.C
local FX_MARKER = og.family_id("fx", "core:marker")
local FX_EXPLOSION = og.family_id("fx", "core:explosion")
local WEAPON_WAVE = og.family_id("weapon", "core:wave")

-- og::combat::kStarburstAddCap (include/openglad/core/combat_math.h),
-- pure constant with no og.C binding: per-fireball damage add cap.
local STARBURST_ADD_CAP = 40

local function handle_teleport(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  self:teleport()
  return true
end

local function check_special_ai(self)
  -- count_foes_in_range(self, 110) == the howmany out-param of the same
  -- world->find_foes_in_range(oblist, ...) call the binding performs.
  local _, foe_count = og.find_foes_in_range("ob", 110, self)
  return foe_count < 1 or foe_count > 3
end

-- The trampoline passes stats->controller() (the stats' owner walker) as
-- self, so the C++ body's `controller` IS self and `stats->` maps to s_*.
local function hit_response(self, foe)
  local threshold
  if self:has_guy() then
    -- shim kept: max_hp is a C++ float: per-op float rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 5.0)
  else
    -- shim kept: max_hp is a C++ float: per-op float rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 8.0)
  end

  local possible = {}
  for i = 0, (self.level + 2) // 3 do
    if i < C.NUM_SPECIALS
        and self.magicpoints >= self:s_special_cost(i) then
      possible[i] = 1
    end
  end

  if self.hp < threshold and possible[1] then
    self.current_special = 1
    self:set_shifter_down(0)
    self.busy = 0
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
          ob.dead = 1
          ob:death()
          if (self.team == 0 or self:has_guy())
              and self:user() ~= -1 then
            og.emit_notification("(Old Marker Removed)")
          end
          -- shim kept: busy is a C++ float: per-op float rounding.
          self:set_busy(og.fadd(self:busy(), 8.0))
          break
        end
      end
      -- The C++ resets its generic to 0 after the removal loop ("force new
      -- placement, for now"), so a marker is always placed here.
      local marker = og.add_ob("fx", FX_MARKER)
      if not marker then
        return false
      end
      marker:set_owner(self)
      marker:set_floor(self:floor())  -- marker on the caster's floor (A8)
      marker:center_on(self)
      if self:has_guy() then
        marker.lifetime = self:g_intelligence() // 33
      else
        marker.lifetime = self.level // 4 + 1
      end
      marker.ani_type = C.ANI_SPIN
      if (self.team == 0 or self:has_guy())
          and self:user() ~= -1 then
        og.emit_notification("Teleport Marker Placed")
        og.emit_notification(string.format("(%d Uses)", marker:lifetime()))
      end
      -- shim kept: busy is a C++ float: per-op float rounding.
      self:set_busy(og.fadd(self:busy(), 8.0))
      -- Marker surcharge: half the MP left after the cast's own cost.
      -- shim kept: magicpoints is a C++ float the C++ banks into an int (trunc).
      local surcharge = og.trunc(og.fsub(
        self.magicpoints,
        self:s_special_cost(self:current_special())))
      -- shim kept: the leftover can be negative: C trunc, not Lua floor.
      surcharge = og.div(surcharge, 2)
      -- shim kept: magicpoints is a C++ float: per-op float rounding.
      self.magicpoints = og.fsub(self.magicpoints, surcharge)
    else
      og.emit_positional_sound(self, C.SOUND_TELEPORT)
      self.ani_type = C.ANI_TELE_OUT
      self:set_cycle(0)
    end
  elseif sp == 2 then
    -- starburst
    -- shim kept (both): the C++ parks the float aim in int temps: C truncation.
    local saved_aim_x = og.trunc(self:lastx())
    local saved_aim_y = og.trunc(self:lasty())
    -- shim kept: magicpoints is a C++ float the C++ banks into an int (trunc).
    local dmg_bonus = og.trunc(og.fsub(
      self.magicpoints,
      self:s_special_cost(self:current_special())))
    if dmg_bonus > 0 then
      -- §2.12: per-fireball add binds only above 660 MP; lineofsight
      -- add/3 inherits the bound.
      -- shim kept: positive here, but the audit's proof is site-local: C trunc.
      dmg_bonus = og.min(og.div(dmg_bonus, 15), STARBURST_ADD_CAP)
      -- shim kept: magicpoints is a C++ float: per-op float rounding.
      self.magicpoints = og.fsub(self.magicpoints, dmg_bonus)
    else
      dmg_bonus = 0
    end
    -- shim kept: magicpoints is a C++ float: per-op float rounding.
    self.magicpoints = og.fadd(self.magicpoints, 8 * self:s_weapon_cost())
    for i = -1, 1 do
      for j = -1, 1 do
        if i ~= 0 or j ~= 0 then
          self:set_lastx(i)
          self:set_lasty(j)
          local bolt = self:fire()
          if bolt then
            -- shim kept: damage is a C++ float: per-op float rounding.
            bolt:set_damage(og.fadd(bolt:damage(), dmg_bonus))
            -- shim kept: bonus/3 stays C trunc (site-local audit proof gap).
            bolt:set_lineofsight(bolt:lineofsight() + og.div(dmg_bonus, 3))
            if bolt:lastx() ~= 0.0 then
              -- shim kept: lastx is a C++ float: per-op float rounding.
              bolt:set_lastx(og.fdiv(bolt:lastx(), math.abs(bolt:lastx())))
            end
            if bolt:lasty() ~= 0.0 then
              -- shim kept: lasty is a C++ float: per-op float rounding.
              bolt:set_lasty(og.fdiv(bolt:lasty(), math.abs(bolt:lasty())))
            end
          end
        end
      end
    end
    self:set_lastx(saved_aim_x)
    self:set_lasty(saved_aim_y)
  elseif sp == 3 then
    -- freeze time. enemy_freeze is relative to world.my_team: a Mage on
    -- the player's team banks a global time-stop; anyone else grants
    -- bonus_rounds to its own side instead.
    if self.team == og.u8(og.my_team()) then
      og.set_enemy_freeze(og.enemy_freeze() + 20 + 11 * self.level)
      og.set_palette(1)
      og.emit_event(C.EVENT_SET_PALETTE, 1)
    else
      local rounds = og.min(5 + 2 * self.level, 50)
      og.emit_notification(
        string.format("TIME IS FROZEN! (%d rounds)", rounds), 2)
      og.emit_event(C.EVENT_REQUEST_REDRAW)
      local friends = og.find_friends_in_range("ob", 30000, self)
      for i = 1, #friends do
        local w = friends[i]
        if w then
          w:set_bonus_rounds(w:bonus_rounds() + rounds)
        end
      end
    end
  elseif sp == 4 then
    -- energy wave
    local bolt = self:fire()
    if not bolt then
      return false
    end
    local wave = og.add_ob("weapon", WEAPON_WAVE)
    if not wave then
      return false
    end
    wave:set_floor(bolt:floor())  -- wave rides the caster's floor (A8)
    wave:center_on(bolt)
    wave:set_owner(self)
    wave.level = self.level
    wave:set_lastx(bolt:lastx())
    wave:set_lasty(bolt:lasty())
    bolt.dead = 1
  else
    -- heartburst (case 5 and the default case)
    local foes, foe_count = og.find_foes_in_range(
      "ob", 80 + 2 * self.level, self)
    if foe_count == 0 then
      return false
    end
    -- shim kept: magicpoints is a C++ float the C++ banks into an int (trunc).
    local pool = og.trunc(og.fsub(self.magicpoints,
                                  self:s_special_cost(5)))
    -- §2.12: heartburst pool binds only above ~1300 MP.
    -- shim kept: the pool can be negative: C trunc, not Lua floor.
    local share = og.min(og.div(pool, 2), C.MP_POOL_DAMAGE_CAP)
    -- shim kept: share can be negative: C trunc, not Lua floor.
    local damage = og.div(share, foe_count)
    if self:has_guy() then
      self:g_set_total_shots(self:g_total_shots() + foe_count)
      self:g_set_scen_shots(self:g_scen_shots() + foe_count)
    end
    -- shim kept: busy is a C++ float: per-op float rounding.
    self:set_busy(og.fadd(self:busy(), 5.0))
    for i = 1, #foes do
      local foe = foes[i]
      local burst = og.summon(self, "fx", FX_EXPLOSION)
      if not burst then
        return false
      end
      burst.damage = damage
      -- Heartburst bursts materialize ON each acquired foe, so they take
      -- that target's floor (A8); the blast itself only damages same-floor
      -- walkers (explosion_on_death floor filter).
      burst:set_floor(foe:floor())
      burst:center_on(foe)
      og.emit_sound(C.SOUND_EXPLODE)
      burst.ani_type = C.ANI_EXPLODE
      burst:s_set_bit_flags(C.BIT_MAGICAL, 1)
      burst:set_skip_exit(100)
      -- shim kept: magicpoints is a C++ float: per-op float rounding.
      self.magicpoints = og.fsub(self.magicpoints, damage)
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
