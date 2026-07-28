-- core:cleric — heal/mace, raise skeleton/ghost, turn undead, resurrect (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local lc = og.use("living_common")
local FX_MAGIC_SHIELD = og.family_id("fx", "core:magic_shield")
local LIVING_SKELETON = og.family_id("living", "core:skeleton")
local LIVING_GHOST = og.family_id("living", "core:ghost")

-- og::combat::kMaceLifeCap (mystic mace lifetime bound; binds above 738 MP).
local MACE_LIFE_CAP = 468

-- Shared turn-undead block: the case-2 and case-3 shifter_down bodies
-- were token-identical. Returns false where the legacy code returned
-- false out of do_special; true = the ladder's `return true`.
local function do_turn_undead(self)
  if lc.is_busy(self) then
    return false
  end
  if self:has_guy() and self:g_intelligence() < 60 then
    if self.team == 0 or self:has_guy() then
      og.emit_notification("You need 60 Int to Turn Undead")
    end
    -- busy is a C++ float: per-op rounding
    self:set_busy(og.fadd(self:busy(), 5.0))
    return false
  end
  local turned = self:turn_undead(4 * self.level, self.level)
  if turned == -1 then
    return false
  end
  if self:has_guy() and turned ~= 0 then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, nil, "turn_undead", turned))
    if self.team == 0 or self:has_guy() then
      og.emit_notification(
        string.format("%s turned %d undead.", self:g_name(), turned))
    end
  end
  og.emit_sound(C.SOUND_HEAL)
  return true
end

-- Corpse gate shared by the three raise specials: the nearest bloodstain,
-- accepted only when its tile is passable and it sits within `range`.
-- query_passable rolls (obmap miss): the probe stays first, the range
-- compare second, exactly the ladder's `passable and distance` order.
local function nearby_corpse(self, range)
  local blood = og.find_nearest_blood(self)
  if not blood then
    return nil
  end
  local passable = og.query_passable(blood:xpos(), blood:ypos(), blood)
  if passable and self:distance_to_ob(blood) < range then
    return blood
  end
  return nil
end

local function heal_or_mace(self)
  if self:shifter_down() == 0 then
    -- normal heal
    local friends, friend_count = og.find_friends_in_range("ob", 60, self)
    if friend_count <= 1 then
      return false
    end
    local healed = 0
    for i = 1, #friends do
      local ally = friends[i]
      if ally.hp < ally.max_hp and ally ~= self then
        -- og.heal_amount draws the same rng compute_heal_amount drew;
        -- its mp argument is the C++ (int) cast of the float pool.
        local amount, cost =
          og.heal_amount(og.trunc(self.magicpoints), self.level)
        if self.magicpoints < cost then
          -- the C++ (int32)-casts the unchanged mp twice; one value serves
          local mp_i = og.trunc(self.magicpoints)
          amount = amount - mp_i
          cost = cost - mp_i
        end
        if amount <= 0 or cost <= 0 then
          break
        end
        -- hp and magicpoints are C++ floats: per-op rounding
        ally.hp = og.fadd(ally.hp, amount)
        self.magicpoints = og.fsub(self.magicpoints, cost)
        if self:has_guy() then
          self:g_set_exp(self:g_exp() +
                         og.exp_from_action(self, ally, "heal", amount))
        end
        healed = healed + 1
        self:do_heal_effects(self, ally, amount)
      end
    end
    if healed == 0 then
      return false
    end
    local message
    if healed == 1 then
      message = "Cleric healed 1 man!"
    else
      message = string.format("Cleric healed %d men!", healed)
    end
    if self.team == 0 or self:has_guy() then
      og.emit_notification(message)
    end
    og.emit_sound(C.SOUND_HEAL)
    return true
  end
  -- mystic mace
  if lc.is_busy(self) then
    return false
  end
  if self:has_guy() and self:g_intelligence() < 50 then
    if self:user() ~= -1 then
      og.emit_notification("50 Int required for Mystic Mace!")
    end
    return false
  end
  if self:has_guy() then
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  local mace = og.summon(self, "fx", FX_MAGIC_SHIELD)
  if not mace then
    return false
  end
  mace:set_ani_type(1)
  -- spare MP funds the mace; the spare can be negative: og.div is C
  -- trunc, not Lua floor
  local spare_mp = og.div(lc.spare_mp(self, self:current_special()), 2)
  local life = og.min(100 + spare_mp, MACE_LIFE_CAP)
  mace:set_lifetime(life)
  -- hp is a C++ float: per-op rounding; og.div halves as C trunc again
  mace.hp = og.fadd(mace.hp, og.div(spare_mp, 2))
  -- damage is a C++ float: the quarter is a genuine float division
  mace:set_damage(og.fadd(mace:damage(), og.fdiv(spare_mp, 4.0)))
  -- magicpoints and busy are C++ floats: per-op rounding
  self.magicpoints = og.fsub(self.magicpoints, spare_mp)
  self:set_busy(og.fadd(self:busy(), 5.0))
  return true
end

local function raise_skeleton(self)
  if self:shifter_down() ~= 0 then
    return do_turn_undead(self)
  end
  -- raise skeleton at the nearest bloodstain
  local blood = nearby_corpse(self, 60)
  if not blood then
    return false
  end
  local life = og.combat.skeleton_lifetime(self.level)
  local alive = self:do_summon(LIVING_SKELETON, life)
  if not alive then
    return false
  end
  alive.team = self.team
  alive.level = og.rand0(self.level) + 1
  alive:set_difficulty(alive.level)
  alive:set_floor(blood:floor())  -- rise at the bloodstain's floor (A8)
  alive:setxy(blood:xpos(), blood:ypos())
  alive:set_owner(self)
  blood:set_dead(1)
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, alive, "raise_skeleton", 0))
  end
  return true
end

local function raise_ghost(self)
  if self:shifter_down() ~= 0 then
    -- turn undead (identical block to special 2)
    return do_turn_undead(self)
  end
  -- raise ghost at the nearest bloodstain
  local blood = nearby_corpse(self, 30)
  if not blood then
    return false
  end
  local life = og.combat.ghost_raise_lifetime(self.level)
  local alive = self:do_summon(LIVING_GHOST, life)
  if not alive then
    return false
  end
  alive.level = og.rand0(self.level) + 1
  alive:set_difficulty(alive.level)
  alive.team = self.team
  alive:set_floor(blood:floor())  -- rise at the bloodstain's floor (A8)
  alive:setxy(blood:xpos(), blood:ypos())
  alive:set_owner(self)
  blood:set_dead(1)
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, alive, "raise_ghost", 0))
  end
  return true
end

local function resurrect(self)
  local blood = nearby_corpse(self, 30)
  if not blood then
    return false
  end
  local alive
  if self:is_friendly(blood) then
    -- Friendly resurrect: restore the corpse's pre-bloodstain family
    -- at half health.
    alive = og.add_ob("living", blood:s_old_family())
    if not alive then
      return false
    end
    blood:transfer_stats(alive)
    -- max_hp is a C++ float: fdiv is a genuine float half
    alive.hp = og.fdiv(alive.max_hp, 2.0)
    -- the heal marker carries the same half, int16-narrowed (C++ short)
    local half = og.i16(og.trunc(og.fdiv(alive.max_hp, 2.0)))
    self:do_heal_effects(self, alive, half)
    alive.team = blood.team
    if self:has_guy() then
      -- C++ stores the short return into an unsigned short (modular).
      local exp_loss =
        og.exp_from_action(self, blood, "resurrect_penalty", 0)
      if exp_loss < 0 then
        exp_loss = exp_loss + 65536
      end
      if self:g_exp() >= exp_loss then
        self:g_set_exp(self:g_exp() - exp_loss)
      else
        self:g_set_exp(0)
      end
    end
  else
    alive = self:do_summon(LIVING_GHOST, 200)
    if not alive then
      return false
    end
    alive.team = self.team
    alive.level = og.rand0(self.level) + 1
    alive:set_difficulty(alive.level)
    alive:set_owner(self)
  end
  alive:set_floor(blood:floor())  -- return at the bloodstain's floor (A8)
  alive:setxy(blood:xpos(), blood:ypos())
  blood:set_dead(1)
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, alive, "resurrect", 0))
  end
  return true
end

local function check_special_ai(self)
  if self:current_special() == 1 then -- healing
    local _, friend_count = og.find_friends_in_range("ob", 60, self)
    if friend_count > 1 then
      self:set_shifter_down(0) -- we're HEALING
      return true
    -- max_magicpoints is a C++ float: fdiv is a genuine float half
    elseif self.magicpoints >= og.fdiv(self.max_magicpoints, 2) then
      self:set_shifter_down(1) -- do mace
      return true
    end
    return false
  end
  return true
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 9.0, 12.0, 4.0, 0.5)
end

local function on_shoved(self)
  self:set_current_special(1) -- healing
  self:special()
end

local function customize_weapon(self, weapon)
  weapon:set_ani_type(C.ANI_GLOWGROW)
  weapon:set_lifetime(weapon:lifetime() + og.combat.glow_bonus(self.level))
end

og.register_hooks("living", "core:cleric", {
  specials = {
    [1] = heal_or_mace,
    [2] = raise_skeleton,
    [3] = raise_ghost,
    default = resurrect,  -- case 4 and every unmapped slot
  },
  check_special_ai = check_special_ai,
  set_difficulty = set_difficulty,
  on_shoved = on_shoved,
  customize_weapon = customize_weapon,
})
