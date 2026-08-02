-- core:archmage — teleport/marker, heartburst/chain, summons, mind control (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- rng order: hit_response rand(3)/rand(2); illusion tier rand(3/5/7/9);
-- mind-control rand(20)/rand(8) + charm_duration draw.
-- Unlike the Mage twin, marker notifications here are NOT gated on
-- user() ~= -1 — an intentional divergence between the twins.

local C = og.C
local lc = og.use("living_common")
local FX_MARKER = assert(og.family_id("fx", "core:marker"))
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local FX_CHAIN = assert(og.family_id("fx", "core:chain"))
local LIVING_ELF = assert(og.family_id("living", "core:elf"))
local LIVING_SOLDIER = assert(og.family_id("living", "core:soldier"))
local LIVING_ARCHER = assert(og.family_id("living", "core:archer"))
local LIVING_ORC = assert(og.family_id("living", "core:orc"))
local LIVING_SKELETON = assert(og.family_id("living", "core:skeleton"))
local LIVING_DRUID = assert(og.family_id("living", "core:druid"))
local LIVING_CLERIC = assert(og.family_id("living", "core:cleric"))
local LIVING_ELEMENTAL = assert(og.family_id("living", "core:elemental"))
local LIVING_ORC_CAPTAIN = assert(og.family_id("living", "core:orc_captain"))

local function on_fire_weapon(self, weapon)
  -- ArchMage gets 1/20th of 'extra' magic for more damage.
  -- The drain cap binds only above 1000 MP; /20 is one float division.
  local extra = og.fdiv(self.magicpoints, 20)
  extra = og.min(extra, C.SHOT_DRAIN_CAP)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, extra)
  -- damage is a C++ float: per-op rounding.
  weapon.damage = og.fadd(weapon:damage(), extra)
  return true
end

-- Fires every tick per archmage: bonus viewing periodically based on level.
local function on_act_living(self)
  local period
  if self.level >= 40 then
    period = 1
  else
    period = 40 - self.level
  end
  -- Keep C remainder semantics explicit for drawcycle.
  if og.mod(self:drawcycle(), period) == 0 then
    self:set_view_all(self:view_all() + 1)
  end
end

-- The trampoline passes stats->controller() (the stats' owner walker) as
-- self, so the C++ body's `controller` IS self and `stats->` maps to s_*.
local function hit_response(self, foe)
  self.busy = 0 -- yes, this is a cheat

  local possible = {}
  for i = 0, (self.level + 2) // 3 do
    if i < C.NUM_SPECIALS
        and self.magicpoints >= self:s_special_cost(i) then
      possible[i] = 1
    end
  end

  local threshold
  if self:has_guy() then
    -- Player characters flee at 60% health.
    -- max_hp is a C++ float: per-op rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 5.0)
  else
    -- Enemies are braver :> — they wait until 3/8 health.
    -- max_hp is a C++ float: per-op rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 8.0)
  end

  -- C++ &&-chain short-circuit preserved: rand(3) is drawn only when the
  -- hitpoint and possible[1] conditions already hold.
  if self.hp < threshold and possible[1]
      and og.rand(3) ~= 0 then
    self.current_special = 1
    self:set_shifter_down(0)
    self.busy = 0
    self:special()
  else
    if self:foe() ~= foe then
      self.foe = foe
      foe.foe = self
      self:s_set_current_distance(15000)
      self:s_set_last_distance(15000)
    end
    local _, foe_count = og.find_foes_in_range("ob", 200, self)
    if foe_count ~= 0 then
      -- can we summon illusion?
      if possible[3] then
        self.current_special = 3
        if self:special() then
          return
        end
      end
      -- heartburst, chain lightning, etc.
      if possible[2] then
        -- The old source called the follow-up "then leave! :)", but never
        -- switched to teleport: it repeats slot 2 as a normal heartburst.
        if og.rand(2) ~= 0 then
          self:set_shifter_down(1)
          self.current_special = 2
          if self:special() then
            self:set_shifter_down(0)
            if self.magicpoints >= self:s_special_cost(1) then
              self.busy = 0
              self:special()
            end
            return
          end
        end
        self:set_shifter_down(0)
        self.current_special = 2
        if self:special() then
          if self.magicpoints >= self:s_special_cost(1) then
            self.busy = 0
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

local function teleport(self)
  if lc.mid_teleport(self) then
    return false
  end
  if self:shifter_down() == 0 then
    og.emit_positional_sound(self, C.SOUND_TELEPORT)
    self.ani_type = C.ANI_TELE_OUT
    self:set_cycle(0)
    return true
  end
  -- leave/remove a marker
  if lc.is_busy(self) then
    return false
  end
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
      ob.dead = 1
      ob:death()
      if self.team == 0 or self:has_guy() then
        og.emit_notification("(Old Marker Removed)")
      end
      -- busy is a C++ float: per-op rounding.
      self.busy = og.fadd(self:busy(), 8.0)
      break
    end
  end
  local marker = og.add_ob("fx", FX_MARKER)
  if not marker then
    return false
  end
  marker:set_owner(self)
  marker:set_floor(self:floor())  -- marker stays on the caster's floor
  marker:center_on(self)
  if self:has_guy() then
    marker.lifetime = self:g_intelligence() // 33
  else
    marker.lifetime = self.level // 4 + 1
  end
  marker.ani_type = 2  -- raw 2 in the C++ (not ANI_SPIN, which is 1)
  if self.team == 0 or self:has_guy() then
    og.emit_notification("Teleport Marker Placed")
    og.emit_notification(string.format("(%d Uses)", marker:lifetime()))
  end
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 8.0)
  -- Marker surcharge: half the MP left after the cast's own cost.
  lc.halve_mp_surcharge(self, self:current_special())
  return true
end

local function burst_or_chain(self)
  -- heartburst / chain lightning
  if lc.is_busy(self) then
    return false
  end
  local t = og.tuning(self)
  local radius
  if self:shifter_down() ~= 0 then
    if self:has_guy() then
      -- Int/2 stays a formula: a guy-stat conversion, not a tuning knob.
      radius = t.chain_radius_base + self:g_intelligence() // 2
    else
      radius = t.chain_radius_base + self.level * t.chain_radius_per_level
    end
  else
    radius = t.burst_radius_base
  end
  local foes, foe_count = og.find_foes_in_range(
    "ob", radius + t.radius_bonus_per_level * self.level, self)
  if foe_count == 0 then
    return false
  end
  if self:shifter_down() == 0 then
    -- normal heartburst
    local pool = lc.mp_pool_damage(self, 2)
    -- Preserve C truncation if a malformed state produces a negative pool.
    pool = og.div(pool, foe_count)
    if self:has_guy() then
      self:g_set_total_shots(self:g_total_shots() + foe_count)
      self:g_set_scen_shots(self:g_scen_shots() + foe_count)
    end
    -- busy is a C++ float: per-op rounding.
    self.busy = og.fadd(self:busy(), 5.0)
    for i = 1, #foes do
      local foe = foes[i]
      local burst = og.summon(self, "fx", FX_EXPLOSION)
      if not burst then
        return false
      end
      burst:s_set_bit_flags(C.BIT_MAGICAL, 1)
      burst.damage = pool
      -- Burst materializes on the acquired foe; taking its floor keeps
      -- blast damage on that floor.
      burst:set_floor(foe:floor())
      burst:center_on(foe)
      og.emit_sound(C.SOUND_EXPLODE)
      burst.ani_type = C.ANI_EXPLODE
      burst:s_set_bit_flags(C.BIT_MAGICAL, 1)  -- (set twice in the C++)
      burst:set_skip_exit(100)
      -- magicpoints is a C++ float: per-op rounding.
      self.magicpoints = og.fsub(self.magicpoints, pool)
    end
    return true
  end
  -- chain lightning
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 5.0)
  if self:has_guy() then
    -- One cast is one shot even when the chain hits several foes, so the
    -- old accuracy counter can get above 100% :)
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  local bolt = og.summon(self, "fx", FX_CHAIN)
  if not bolt then
    return false
  end
  -- The initial bolt and its MP charge cap only above roughly 1280 MP.
  local pool = lc.mp_pool_damage(self, 2)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, pool)
  bolt.damage = pool
  local best_dist = 30000
  for i = 1, #foes do
    local foe = foes[i]
    -- Chain lightning cannot arc through solid floors: only same-floor
    -- foes are valid first strikes.
    if foe:floor() == self:floor() then
      local dist = self:distance_to_ob_center(foe)
      if best_dist > dist then
        best_dist = dist
        bolt:set_leader(foe)
      end
    end
  end
  return true
end

local function summon_image(self)
  -- summon image / elemental
  if lc.is_busy(self) then
    return false
  end
  if self:shifter_down() ~= 0 then
    -- true summoning
    local t = og.tuning(self)
    if self:has_guy() and self:g_intelligence() < t.summon_int_req then
      if self:user() ~= -1 then
        og.emit_notification(string.format(
          "%d Int required to Summon!", t.summon_int_req))
      end
      return false
    end
    lc.halve_mp_surcharge(self, 3)
    -- First make the guy we'd summon, at least physically
    local elemental = og.add_ob("living", LIVING_ELEMENTAL)
    if not elemental then
      return false
    end
    -- Set the caster's floor before placement probes and setxy so both use
    -- the right floor.
    elemental:set_floor(self:floor())
    elemental:set_summoned(true)  -- ammunition: never a SAVE_ALL loss
    -- Placement scan: i outer, j inner, skip (0,0), and skip the rest of
    -- the grid once placed (the C++ `generic` latch) — query_passable is
    -- never probed again after a success.
    local placed = false
    for i = -1, 1 do
      for j = -1, 1 do
        if not ((i == 0 and j == 0)
                or placed) then
          local testx = self:xpos() + (elemental:sizex() + 1) * i
          local testy = self:ypos() + (elemental:sizey() + 1) * j
          if og.query_passable(testx, testy, elemental) then
            placed = true
            elemental:setxy(testx, testy)
            elemental.level = (self.level + 1) // 2
            elemental:set_difficulty(elemental.level)
            elemental.team = self.team
            elemental:set_owner(self)
            elemental.lifetime = og.elemental_lifetime(self.level)
          end
        end
      end
    end
    if not placed then
      elemental.dead = 1
      return false
    end
    -- Summoning takes lots of time :)
    -- busy is a C++ float: per-op rounding.
    self.busy = og.fadd(self:busy(), 15.0)
    return true
  end
  -- illusion summoning: tier by post-cost MP; draw counts per tier are
  -- exact (0/1/1/1/1 draws for the 5 tiers).
  local mp_pool = lc.spare_mp(self, 3)
  local person
  if mp_pool < 100 then
    person = LIVING_ELF
  elseif mp_pool < 250 then
    local tier_roll = og.rand(3)
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    else  -- C++ default (unreachable)
      person = LIVING_SOLDIER
    end
  elseif mp_pool < 500 then
    local tier_roll = og.rand(5)
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    elseif tier_roll == 3 then
      person = LIVING_ORC
    elseif tier_roll == 4 then
      person = LIVING_SKELETON
    else  -- C++ default (unreachable)
      person = LIVING_ARCHER
    end
  elseif mp_pool < 1000 then
    local tier_roll = og.rand(7)
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    elseif tier_roll == 3 then
      person = LIVING_ORC
    elseif tier_roll == 4 then
      person = LIVING_SKELETON
    elseif tier_roll == 5 then
      person = LIVING_DRUID
    elseif tier_roll == 6 then
      person = LIVING_CLERIC
    else  -- C++ default (unreachable)
      person = LIVING_ARCHER
    end
  -- our maximum possible, insert before if needed
  else
    local tier_roll = og.rand(9)
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    elseif tier_roll == 3 then
      person = LIVING_ORC
    elseif tier_roll == 4 then
      person = LIVING_SKELETON
    elseif tier_roll == 5 then
      person = LIVING_DRUID
    elseif tier_roll == 6 then
      person = LIVING_CLERIC
    elseif tier_roll == 7 then
      person = LIVING_ELEMENTAL
    elseif tier_roll == 8 then
      person = LIVING_ORC_CAPTAIN
    else  -- C++ default (unreachable)
      person = LIVING_ARCHER
    end
  end
  -- Now make the guy we'd summon, at least physically
  local phantom = og.add_ob("living", person)
  if not phantom then
    return false
  end
  -- Place the illusion beside the caster on the caster's floor.
  phantom:set_floor(self:floor())
  -- Named "Phantom" below, but conjured ammunition must never fail a
  -- SAVE_ALL mission when it expires.
  phantom:set_summoned(true)
  local placed = false
  for i = -1, 1 do
    for j = -1, 1 do
      if not ((i == 0 and j == 0)
              or placed) then
        local testx = self:xpos() + (phantom:sizex() + 1) * i
        local testy = self:ypos() + (phantom:sizey() + 1) * j
        if og.query_passable(testx, testy, phantom) then
          placed = true
          phantom:setxy(testx, testy)
          phantom.level = (self.level + 2) // 3
          phantom:set_difficulty(phantom.level)
          phantom.team = self.team
          phantom:set_owner(self)
          phantom.lifetime = og.image_lifetime(self.level)
          phantom.max_hp = 1
          phantom.hp = 0
          phantom:s_set_armor(0)
          -- just to help out ..
          phantom.foe = self:foe()
          phantom:s_set_bit_flags(C.BIT_MAGICAL, 1)
          phantom:s_set_name("Phantom")
        end
      end
    end
  end
  if not placed then
    phantom.dead = 1
    return false
  end
  -- Summoning takes lots of time :)
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 15.0)
  return true
end

local function mind_control(self)
  if lc.is_busy(self) then
    return false
  end
  local t = og.tuning(self)
  local mp_after_base_cost = lc.spare_mp(self, self:current_special())
  local foes, foe_count = og.find_foes_in_range(
    "ob",
    t.mind_control_range_base + t.mind_control_range_per_level * self.level,
    self)
  if foe_count < 1 then
    return false
  end
  local controlled = 0
  local budget = mp_after_base_cost + 10
  for i = 1, #foes do
    if budget < 10 then
      break
    end
    local foe = foes[i]
    -- never been charmed
    if foe:real_team_num() == 255
        and foe:order() == C.ORDER_LIVING
        and foe:charm_left() <= 10 then
      budget = budget - 10
      local level_edge = self.level - foe.level
      -- C++ `generic < 0 || !rng.next(20)` (generic = the level edge) —
      -- short-circuit preserved: rand(20) is drawn only when the edge
      -- is >= 0.
      local confused
      if level_edge < 0 then
        confused = true
      else
        confused = (og.rand(20) == 0)
      end
      if confused then
        -- Resisted proper control: berserk onto a random team.
        foe:set_real_team_num(foe.team)
        foe.team = og.rand(8)
        foe:set_charm_left(og.charm_duration(level_edge))
      else
        foe:set_real_team_num(foe.team)
        foe.team = self.team
        -- allow choice of new foe
        foe.foe = nil
        foe:set_charm_left(og.charm_duration(level_edge))
      end
      controlled = controlled + 1
    end
  end
  if controlled == 0 then
    return false
  end
  og.emit_notification(string.format(
    "%s has controlled %d men",
    og.entity_display_name(self, "ArchMage"), controlled))
  -- Target budget starts at mp_after_base_cost + 10, so the first
  -- control is covered by base special cost and extras cost 10 MP each.
  if mp_after_base_cost > 0 and controlled > 1 then
    local spend = (controlled - 1) * 10
    spend = og.min(spend, mp_after_base_cost)
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fsub(self.magicpoints, spend)
  end
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 10.0)
  return true
end

og.register_hooks("living", "core:archmage", {
  specials = {
    [1] = teleport,
    [2] = burst_or_chain,
    [3] = summon_image,
    [4] = mind_control,
  },
  hit_response = hit_response,
  level_up = level_up,
  on_act_living = on_act_living,
  on_fire_weapon = on_fire_weapon,
  handle_teleport = lc.handle_teleport,
})
