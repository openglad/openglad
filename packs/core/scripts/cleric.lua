-- core:cleric — behavior hooks transliterated from family_cleric.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local C = og.C
local FX_MAGIC_SHIELD = og.family_id("fx", "core:magic_shield")
local LIVING_SKELETON = og.family_id("living", "core:skeleton")
local LIVING_GHOST = og.family_id("living", "core:ghost")

-- og::combat::glow_bonus (include/openglad/core/combat_math.h), trivially
-- inlined pure-integer helper (no og.* binding exists): legacy 110*L with a
-- FLAT cap at kGlowBonusCap = 2200 (not a soft knee — identity through L20
-- is forced by the L20 glow golden).
local function glow_bonus(level)
  local raw = 110 * level
  if raw < 2200 then
    return raw
  end
  return 2200
end

-- og::combat::skeleton_lifetime / ghost_raise_lifetime: og.soften (the bound
-- C++ soft-knee) over the legacy linear formulas; knee/ceiling literals are
-- kSkeletonLife{Knee,Ceiling} / kGhostRaiseLife{Knee,Ceiling} from
-- combat_math.h.
local function skeleton_lifetime(level)
  return og.soften(125 + 40 * level, 645, 900)
end

local function ghost_raise_lifetime(level)
  return og.soften(150 + 40 * level, 670, 925)
end

-- og::combat::kMaceLifeCap (mystic mace lifetime bound; binds above 738 MP).
local MACE_LIFE_CAP = 468

-- rng_.next(level): og.rand errors on n <= 0 while the C++ rng returns 0
-- WITHOUT advancing state (irandom.h); level is never negative here so the
-- C++ uint32 cast is value-preserving.
local function rand_level(self)
  local n = self:s_level()
  if n > 0 then
    return og.rand(n)
  end
  return 0
end

-- Shared turn-undead block: the case-2 and case-3 shifter_down bodies in
-- family_cleric.cpp are token-identical. Returns false where the C++
-- returned false out of do_special; true = fall through to `return true`.
local function do_turn_undead(self)
  if self:busy() > 0 then
    return false
  end
  if self:has_guy() and self:g_intelligence() < 60 then
    if self:team_num() == 0 or self:has_guy() then
      og.emit_notification("You need 60 Int to Turn Undead")
    end
    self:set_busy(og.fadd(self:busy(), 5.0))
    return false
  end
  local generic = self:turn_undead(4 * self:s_level(), self:s_level())
  if generic == -1 then
    return false
  end
  if self:has_guy() and generic ~= 0 then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, nil, "turn_undead", generic))
    if self:team_num() == 0 or self:has_guy() then
      og.emit_notification(
        string.format("%s turned %d undead.", self:g_name(), generic))
    end
  end
  og.emit_sound(C.SOUND_HEAL)
  return true
end

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    if self:shifter_down() == 0 then
      -- normal heal
      local newlist, howmany = og.find_friends_in_range("ob", 60, self)
      local didheal = 0
      if howmany > 1 then
        for i = 1, #newlist do
          local newob = newlist[i]
          if newob:s_hitpoints() < newob:s_max_hitpoints() and
              newob ~= self then
            -- og.heal_amount draws the same rng compute_heal_amount drew.
            local generic, cost =
              og.heal_amount(og.trunc(self:s_magicpoints()), self:s_level())
            if self:s_magicpoints() < cost then
              -- two identical (int32) casts of the unchanged mp in the C++
              local mp_i = og.trunc(self:s_magicpoints())
              generic = generic - mp_i
              cost = cost - mp_i
            end
            if generic <= 0 or cost <= 0 then
              break
            end
            newob:s_set_hitpoints(og.fadd(newob:s_hitpoints(), generic))
            self:s_set_magicpoints(og.fsub(self:s_magicpoints(), cost))
            if self:has_guy() then
              self:g_set_exp(self:g_exp() +
                             og.exp_from_action(self, newob, "heal", generic))
            end
            didheal = didheal + 1
            self:do_heal_effects(self, newob, generic)
          end
        end
        if didheal == 0 then
          return false
        else
          local message
          if didheal == 1 then
            message = "Cleric healed 1 man!"
          else
            message = string.format("Cleric healed %d men!", didheal)
          end
          if self:team_num() == 0 or self:has_guy() then
            og.emit_notification(message)
          end
          og.emit_sound(C.SOUND_HEAL)
        end
      else
        return false
      end
    else
      -- mystic mace
      if self:busy() > 0 then
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
      local newob = og.summon(self, "fx", FX_MAGIC_SHIELD)
      if not newob then
        return false
      end
      newob:set_ani_type(1)
      local generic = og.trunc(og.fsub(
        self:s_magicpoints(), self:s_special_cost(self:current_special())))
      generic = og.div(generic, 2)
      local life = 100 + generic
      if life >= MACE_LIFE_CAP then
        life = MACE_LIFE_CAP
      end
      newob:set_lifetime(life)
      newob:s_set_hitpoints(og.fadd(newob:s_hitpoints(), og.div(generic, 2)))
      newob:set_damage(og.fadd(newob:damage(), og.fdiv(generic, 4.0)))
      self:s_set_magicpoints(og.fsub(self:s_magicpoints(), generic))
      self:set_busy(og.fadd(self:busy(), 5.0))
    end
  elseif sp == 2 then
    if self:shifter_down() ~= 0 then
      -- turn undead
      if not do_turn_undead(self) then
        return false
      end
    else
      -- raise skeleton at the nearest bloodstain
      local newob = og.find_nearest_blood(self)
      if newob then
        local targetx = newob:xpos()
        local targety = newob:ypos()
        local distance = self:distance_to_ob(newob)
        if og.query_passable(targetx, targety, newob) and distance < 60 then
          local alive =
            self:do_summon(LIVING_SKELETON, skeleton_lifetime(self:s_level()))
          if not alive then
            return false
          end
          alive:set_team_num(self:team_num())
          alive:s_set_level(rand_level(self) + 1)
          alive:set_difficulty(alive:s_level())
          alive:set_floor(newob:floor())  -- rise at the bloodstain's floor (A8)
          alive:setxy(newob:xpos(), newob:ypos())
          alive:set_owner(self)
          newob:set_dead(1)
          if self:has_guy() then
            self:g_set_exp(self:g_exp() +
                           og.exp_from_action(self, alive, "raise_skeleton", 0))
          end
        else
          return false
        end
      else
        return false
      end
    end
  elseif sp == 3 then
    if self:shifter_down() ~= 0 then
      -- turn undead (identical block to special 2)
      if not do_turn_undead(self) then
        return false
      end
    else
      -- raise ghost at the nearest bloodstain
      local newob = og.find_nearest_blood(self)
      if newob then
        local targetx = newob:xpos()
        local targety = newob:ypos()
        local distance = self:distance_to_ob(newob)
        if og.query_passable(targetx, targety, newob) and distance < 30 then
          local alive =
            self:do_summon(LIVING_GHOST, ghost_raise_lifetime(self:s_level()))
          if not alive then
            return false
          end
          alive:s_set_level(rand_level(self) + 1)
          alive:set_difficulty(alive:s_level())
          alive:set_team_num(self:team_num())
          alive:set_floor(newob:floor())  -- rise at the bloodstain's floor (A8)
          alive:setxy(newob:xpos(), newob:ypos())
          alive:set_owner(self)
          newob:set_dead(1)
          if self:has_guy() then
            self:g_set_exp(self:g_exp() +
                           og.exp_from_action(self, alive, "raise_ghost", 0))
          end
        else
          return false
        end
      else
        return false
      end
    end
  else
    -- resurrect (case 4 and the default case)
    local newob = og.find_nearest_blood(self)
    if newob then
      local targetx = newob:xpos()
      local targety = newob:ypos()
      local distance = self:distance_to_ob(newob)
      if og.query_passable(targetx, targety, newob) and distance < 30 then
        local alive
        if self:is_friendly(newob) then
          -- Friendly resurrect: restore the corpse's pre-bloodstain family
          -- at half health.
          alive = og.add_ob("living", newob:s_old_family())
          if not alive then
            return false
          end
          newob:transfer_stats(alive)
          alive:s_set_hitpoints(og.fdiv(alive:s_max_hitpoints(), 2.0))
          self:do_heal_effects(self, alive,
                               og.i16(og.trunc(og.fdiv(
                                 alive:s_max_hitpoints(), 2.0))))
          alive:set_team_num(newob:team_num())
          if self:has_guy() then
            -- C++ stores the short return into an unsigned short (modular).
            local exp_loss =
              og.exp_from_action(self, newob, "resurrect_penalty", 0)
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
          alive:set_team_num(self:team_num())
          alive:s_set_level(rand_level(self) + 1)
          alive:set_difficulty(alive:s_level())
          alive:set_owner(self)
        end
        alive:set_floor(newob:floor())  -- return at the bloodstain's floor (A8)
        alive:setxy(newob:xpos(), newob:ypos())
        newob:set_dead(1)
        if self:has_guy() then
          self:g_set_exp(self:g_exp() +
                         og.exp_from_action(self, alive, "resurrect", 0))
        end
      else
        return false
      end
    else
      return false
    end
  end
  return true
end

local function check_special_ai(self)
  if self:current_special() == 1 then -- healing
    local _, howmany = og.find_friends_in_range("ob", 60, self)
    if howmany > 1 then
      self:set_shifter_down(0) -- we're HEALING
      return true
    elseif self:s_magicpoints() >= og.fdiv(self:s_max_magicpoints(), 2) then
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
  weapon:set_lifetime(weapon:lifetime() + glow_bonus(self:s_level()))
end

og.register_hooks("living", "core:cleric", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  set_difficulty = set_difficulty,
  on_shoved = on_shoved,
  customize_weapon = customize_weapon,
})
