-- core:archmage — behavior hooks transliterated from family_archmage.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order (hit_response rand(3)/rand(2), illusion
-- tier rand(3/5/7/9), mind-control rand(20)/rand(8) + charm_duration draw).
-- Unlike the Mage twin, the marker notifications here are NOT gated on
-- user() ~= -1 — the C++ bodies differ; each is transliterated exactly.

local C = og.C
local FX_MARKER = og.family_id("fx", "core:marker")
local FX_EXPLOSION = og.family_id("fx", "core:explosion")
local FX_CHAIN = og.family_id("fx", "core:chain")
local LIVING_ELF = og.family_id("living", "core:elf")
local LIVING_SOLDIER = og.family_id("living", "core:soldier")
local LIVING_ARCHER = og.family_id("living", "core:archer")
local LIVING_ORC = og.family_id("living", "core:orc")
local LIVING_SKELETON = og.family_id("living", "core:skeleton")
local LIVING_DRUID = og.family_id("living", "core:druid")
local LIVING_CLERIC = og.family_id("living", "core:cleric")
local LIVING_ELEMENTAL = og.family_id("living", "core:elemental")
local LIVING_ORC_CAPTAIN = og.family_id("living", "core:orc_captain")

local function handle_teleport(self)
  self:set_ani_type(C.ANI_TELE_IN)
  self:set_cycle(0)
  self:teleport()
  return true
end

local function on_fire_weapon(self, weapon)
  -- ArchMage gets 1/20th of 'extra' magic for more damage.
  -- std::min(mp / 20.0f, (float)kShotDrainCap) — §2.12: binds only above
  -- 1000 MP. The /20 is a single float division.
  local extra = og.fdiv(self:s_magicpoints(), 20)
  if C.SHOT_DRAIN_CAP < extra then
    extra = C.SHOT_DRAIN_CAP
  end
  self:s_set_magicpoints(og.fsub(self:s_magicpoints(), extra))
  weapon:set_damage(og.fadd(weapon:damage(), extra))
  return true
end

-- Fires every tick per archmage: bonus viewing periodically based on level.
local function on_act_living(self)
  local lvl = self:s_level()
  local temp
  if lvl >= 40 then
    temp = 1
  else
    temp = 40 - lvl
  end
  if og.mod(self:drawcycle(), temp) == 0 then
    self:set_view_all(self:view_all() + 1)
  end
end

-- The trampoline passes stats->controller() (the stats' owner walker) as
-- self, so the C++ body's `controller` IS self and `stats->` maps to s_*.
local function hit_response(self, foe)
  self:set_busy(0) -- yes, this is a cheat

  local possible = {}
  for i = 0, og.div(self:s_level() + 2, 3) do
    if i < C.NUM_SPECIALS
        and self:s_magicpoints() >= self:s_special_cost(i) then
      possible[i] = 1
    end
  end

  local threshold
  if self:has_guy() then
    threshold = og.fdiv(og.fmul(3.0, self:s_max_hitpoints()), 5.0)
  else
    threshold = og.fdiv(og.fmul(3.0, self:s_max_hitpoints()), 8.0)
  end

  -- C++ &&-chain short-circuit preserved: rand(3) is drawn only when the
  -- hitpoint and possible[1] conditions already hold.
  if self:s_hitpoints() < threshold and possible[1]
      and og.rand(3) ~= 0 then
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
    local _, howmany = og.find_foes_in_range("ob", 200, self)
    if howmany ~= 0 then
      if possible[3] then
        self:set_current_special(3)
        if self:special() then
          return
        end
      end
      if possible[2] then
        if og.rand(2) ~= 0 then
          self:set_shifter_down(1)
          self:set_current_special(2)
          if self:special() then
            self:set_shifter_down(0)
            if self:s_magicpoints() >= self:s_special_cost(1) then
              self:set_busy(0)
              self:special()
            end
            return
          end
        end
        self:set_shifter_down(0)
        self:set_current_special(2)
        if self:special() then
          if self:s_magicpoints() >= self:s_special_cost(1) then
            self:set_busy(0)
            self:special()
          end
          return
        end
      end
    end
  end
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
      if self:busy() > 0 then return false end
      if self:has_guy() and self:g_intelligence() < 75 then
        og.emit_notification("Need 75 Int for Marker!")
        return false
      end
      -- Remove this caster's old marker, if present. (The C++ sets
      -- generic = 1 here but never tests it: a new marker is always
      -- placed below.)
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
          if self:team_num() == 0 or self:has_guy() then
            og.emit_notification("(Old Marker Removed)")
          end
          self:set_busy(og.fadd(self:busy(), 8.0))
          break
        end
      end
      local newob = og.add_ob("fx", FX_MARKER)
      if not newob then return false end
      newob:set_owner(self)
      newob:set_floor(self:floor())  -- marker on the caster's floor (A8)
      newob:center_on(self)
      if self:has_guy() then
        newob:set_lifetime(og.div(self:g_intelligence(), 33))
      else
        newob:set_lifetime(og.div(self:s_level(), 4) + 1)
      end
      newob:set_ani_type(2)  -- raw 2 in the C++ (not ANI_SPIN, which is 1)
      if self:team_num() == 0 or self:has_guy() then
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
    -- heartburst / chain lightning
    if self:busy() > 0 then return false end
    local generic
    if self:shifter_down() ~= 0 then
      if self:has_guy() then
        generic = 200 + og.div(self:g_intelligence(), 2)
      else
        generic = 200 + self:s_level() * 5
      end
    else
      generic = 80
    end
    local newlist, howmany = og.find_foes_in_range(
      "ob", generic + 2 * self:s_level(), self)
    if howmany == 0 then return false end
    if self:shifter_down() == 0 then
      -- normal heartburst
      generic = og.trunc(og.fsub(self:s_magicpoints(),
                                 self:s_special_cost(2)))
      -- §2.12: heartburst pool binds only above ~1280 MP.
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
        if not newob then return false end
        newob:s_set_bit_flags(C.BIT_MAGICAL, 1)
        newob:set_damage(generic)
        -- Burst materializes ON the acquired foe: take its floor (A8);
        -- blast damage stays same-floor.
        newob:set_floor(ob:floor())
        newob:center_on(ob)
        og.emit_sound(C.SOUND_EXPLODE)
        newob:set_ani_type(C.ANI_EXPLODE)
        newob:s_set_bit_flags(C.BIT_MAGICAL, 1)  -- (set twice in the C++)
        newob:set_skip_exit(100)
        self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
      end
    else
      -- chain lightning
      self:set_busy(og.fadd(self:busy(), 5.0))
      if self:has_guy() then
        self:g_set_total_shots(self:g_total_shots() + 1)
        self:g_set_scen_shots(self:g_scen_shots() + 1)
      end
      local newob = og.summon(self, "fx", FX_CHAIN)
      if not newob then return false end
      generic = og.trunc(og.fsub(self:s_magicpoints(),
                                 self:s_special_cost(2)))
      -- §2.12: initial bolt (and its MP charge) bind only above ~1280 MP.
      local g2 = og.div(generic, 2)
      if g2 < C.MP_POOL_DAMAGE_CAP then
        generic = g2
      else
        generic = C.MP_POOL_DAMAGE_CAP
      end
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
      newob:set_damage(generic)
      generic = 30000
      for i = 1, #newlist do
        local w = newlist[i]
        -- Chain lightning cannot arc through solid floors: only same-floor
        -- foes are valid first strikes (A8; byte-identical on single-floor
        -- levels).
        if w:floor() == self:floor() then
          local dist = self:distance_to_ob_center(w)
          if generic > dist then
            generic = dist
            newob:set_leader(w)
          end
        end
      end
    end
  elseif sp == 3 then
    -- summon image / elemental
    if self:busy() > 0 then return false end
    if self:shifter_down() ~= 0 then
      -- true summoning
      if self:has_guy() and self:g_intelligence() < 150 then
        if self:user() ~= -1 then
          og.emit_notification("150 Int required to Summon!")
        end
        return false
      end
      local generic = og.trunc(og.fsub(self:s_magicpoints(),
                                       self:s_special_cost(3)))
      generic = og.div(generic, 2)
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
      local newob = og.add_ob("living", LIVING_ELEMENTAL)
      if not newob then return false end
      -- Summon appears beside the caster, on the caster's floor (A8); must
      -- precede the query_passable probes and setxy so both use the right
      -- floor.
      newob:set_floor(self:floor())
      newob:set_summoned(true)  -- ammunition: never a SAVE_ALL loss
      -- Placement scan: i outer, j inner, skip (0,0), and skip the rest of
      -- the grid once placed (the C++ `generic` latch) — query_passable is
      -- never probed again after a success.
      local placed = false
      for i = -1, 1 do
        for j = -1, 1 do
          if not ((i == 0 and j == 0) or placed) then
            local testx = self:xpos() + (newob:sizex() + 1) * i
            local testy = self:ypos() + (newob:sizey() + 1) * j
            if og.query_passable(testx, testy, newob) then
              placed = true
              newob:setxy(testx, testy)
              newob:s_set_level(og.div(self:s_level() + 1, 2))
              newob:set_difficulty(newob:s_level())
              newob:set_team_num(self:team_num())
              newob:set_owner(self)
              newob:set_lifetime(og.elemental_lifetime(self:s_level()))
            end
          end
        end
      end
      if not placed then
        newob:set_dead(1)
        return false
      end
      self:set_busy(og.fadd(self:busy(), 15.0))
    else
      -- illusion summoning: tier by post-cost MP; draw counts per tier are
      -- exact (0/1/1/1/1 draws for the 5 tiers).
      local generic = og.trunc(og.fsub(self:s_magicpoints(),
                                       self:s_special_cost(3)))
      local person
      if generic < 100 then
        person = LIVING_ELF
      elseif generic < 250 then
        local r = og.rand(3)
        if r == 0 then person = LIVING_ELF
        elseif r == 1 then person = LIVING_SOLDIER
        elseif r == 2 then person = LIVING_ARCHER
        else person = LIVING_SOLDIER end  -- C++ default (unreachable)
      elseif generic < 500 then
        local r = og.rand(5)
        if r == 0 then person = LIVING_ELF
        elseif r == 1 then person = LIVING_SOLDIER
        elseif r == 2 then person = LIVING_ARCHER
        elseif r == 3 then person = LIVING_ORC
        elseif r == 4 then person = LIVING_SKELETON
        else person = LIVING_ARCHER end  -- C++ default (unreachable)
      elseif generic < 1000 then
        local r = og.rand(7)
        if r == 0 then person = LIVING_ELF
        elseif r == 1 then person = LIVING_SOLDIER
        elseif r == 2 then person = LIVING_ARCHER
        elseif r == 3 then person = LIVING_ORC
        elseif r == 4 then person = LIVING_SKELETON
        elseif r == 5 then person = LIVING_DRUID
        elseif r == 6 then person = LIVING_CLERIC
        else person = LIVING_ARCHER end  -- C++ default (unreachable)
      else
        local r = og.rand(9)
        if r == 0 then person = LIVING_ELF
        elseif r == 1 then person = LIVING_SOLDIER
        elseif r == 2 then person = LIVING_ARCHER
        elseif r == 3 then person = LIVING_ORC
        elseif r == 4 then person = LIVING_SKELETON
        elseif r == 5 then person = LIVING_DRUID
        elseif r == 6 then person = LIVING_CLERIC
        elseif r == 7 then person = LIVING_ELEMENTAL
        elseif r == 8 then person = LIVING_ORC_CAPTAIN
        else person = LIVING_ARCHER end  -- C++ default (unreachable)
      end
      local newob = og.add_ob("living", person)
      if not newob then return false end
      -- Illusion appears beside the caster, on the caster's floor (A8).
      newob:set_floor(self:floor())
      -- Named "Phantom" below, but conjured ammunition must never fail a
      -- SAVE_ALL mission when it expires (Wave F2).
      newob:set_summoned(true)
      local placed = false
      for i = -1, 1 do
        for j = -1, 1 do
          if not ((i == 0 and j == 0) or placed) then
            local testx = self:xpos() + (newob:sizex() + 1) * i
            local testy = self:ypos() + (newob:sizey() + 1) * j
            if og.query_passable(testx, testy, newob) then
              placed = true
              newob:setxy(testx, testy)
              newob:s_set_level(og.div(self:s_level() + 2, 3))
              newob:set_difficulty(newob:s_level())
              newob:set_team_num(self:team_num())
              newob:set_owner(self)
              newob:set_lifetime(og.image_lifetime(self:s_level()))
              newob:s_set_max_hitpoints(1)
              newob:s_set_hitpoints(0)
              newob:s_set_armor(0)
              newob:set_foe(self:foe())
              newob:s_set_bit_flags(C.BIT_MAGICAL, 1)
              newob:s_set_name("Phantom")
            end
          end
        end
      end
      if not placed then
        newob:set_dead(1)
        return false
      end
      self:set_busy(og.fadd(self:busy(), 15.0))
    end
  elseif sp == 4 then
    -- mind control
    if self:busy() > 0 then return false end
    local special_cost = self:s_special_cost(self:current_special())
    local mp_after_base_cost = og.trunc(og.fsub(self:s_magicpoints(),
                                                special_cost))
    local newlist, howmany = og.find_foes_in_range(
      "ob", 80 + 4 * self:s_level(), self)
    if howmany < 1 then return false end
    local didheal = 0
    local controlled_targets = 0
    local generic2 = mp_after_base_cost + 10
    for i = 1, #newlist do
      if generic2 < 10 then break end
      local ob = newlist[i]
      if ob:real_team_num() == 255
          and ob:order() == C.ORDER_LIVING
          and ob:charm_left() <= 10 then
        generic2 = generic2 - 10
        local generic = self:s_level() - ob:s_level()
        -- C++ `generic < 0 || !rng.next(20)` — short-circuit preserved:
        -- rand(20) is drawn only when generic >= 0.
        local confused
        if generic < 0 then
          confused = true
        else
          confused = (og.rand(20) == 0)
        end
        if confused then
          -- Resisted proper control: berserk onto a random team.
          ob:set_real_team_num(ob:team_num())
          ob:set_team_num(og.rand(8))
          ob:set_charm_left(og.charm_duration(generic))
        else
          ob:set_real_team_num(ob:team_num())
          ob:set_team_num(self:team_num())
          ob:set_foe(nil)
          ob:set_charm_left(og.charm_duration(generic))
        end
        didheal = didheal + 1
        controlled_targets = controlled_targets + 1
      end
    end
    if didheal == 0 then return false end
    og.emit_notification(string.format(
      "%s has controlled %d men",
      og.entity_display_name(self, "ArchMage"), didheal))
    -- Target budget starts at mp_after_base_cost + 10, so the first
    -- control is covered by base special cost and extras cost 10 MP each.
    if mp_after_base_cost > 0 and controlled_targets > 1 then
      local spend = (controlled_targets - 1) * 10
      if mp_after_base_cost < spend then
        spend = mp_after_base_cost
      end
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), spend))
    end
    self:set_busy(og.fadd(self:busy(), 10.0))
  end
  -- default: no-op (the C++ switch default just breaks; do_special still
  -- returns true)
  return true
end

og.register_hooks("living", "core:archmage", {
  do_special = do_special,
  hit_response = hit_response,
  level_up = level_up,
  on_act_living = on_act_living,
  on_fire_weapon = on_fire_weapon,
  handle_teleport = handle_teleport,
})
