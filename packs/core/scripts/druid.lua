-- core:druid — behavior hooks transliterated from family_druid.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local C = og.C
local WEAP_TREE = og.family_id("weapon", "core:tree")
local LIVING_FAERIE = og.family_id("living", "core:faerie")
local WEAP_CIRCLE_PROTECTION = og.family_id("weapon", "core:circle_protection")

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- plant tree
    if self:busy() > 0 then
      return false
    end
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   self:s_weapon_cost()))
    local newob = self:fire()
    if not newob then
      return false
    end
    self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 2.0)))
    local alive = og.summon(self, "weapon", WEAP_TREE)
    if not alive then
      return false
    end
    alive:setxy(newob:xpos(), newob:ypos())
    alive:set_ani_type(C.ANI_GROW)
    newob:set_dead(1)
  elseif sp == 2 then
    -- summon faerie
    if self:busy() > 0 then
      return false
    end
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   self:s_weapon_cost()))
    local newob = self:fire()
    if not newob then
      return false
    end
    local alive = og.add_ob("living", LIVING_FAERIE)
    if not alive then
      return false
    end
    alive:set_owner(self)
    alive:set_team_num(self:team_num())
    alive:set_floor(newob:floor())  -- faerie on the caster's floor (A8)
    alive:setxy(newob:xpos(), newob:ypos())
    -- og::combat::druid_faerie_lifetime (include/openglad/core/combat_math.h):
    -- soften(50 + 40*L, kFaerieLifeKnee=570, kFaerieLifeCeiling=800). No
    -- direct og.* binding exists; composed from the bound og.soften.
    alive:set_lifetime(og.soften(50 + 40 * self:s_level(), 570, 800))
    newob:set_dead(1)
    if not og.query_passable(alive:xpos(), alive:ypos(), alive) then
      alive:set_dead(1)
      return false
    end
    self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 3.0)))
  elseif sp == 3 then
    -- reveal items
    if self:busy() > 0 then
      return false
    end
    self:set_view_all(self:view_all() + self:s_level() * 10)
    self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 4.0)))
  else
    -- circle of protection (special 4 and the default case)
    if self:busy() > 0 then
      return false
    end
    local newlist, howmany = og.find_friends_in_range("ob", 60, self)
    local didheal = 0
    if howmany > 1 then
      for i = 1, #newlist do
        local newob = newlist[i]
        if newob ~= self then
          -- Scan the world oblist for an existing circle owned by this
          -- friend (same list and order the C++ walked).
          local tempwalk = nil
          local obs = og.oblist()
          for j = 1, #obs do
            local ob = obs[j]
            if ob:owner() == newob
                and ob:order() == C.ORDER_WEAPON
                and ob:family() == WEAP_CIRCLE_PROTECTION then
              tempwalk = ob
              break
            end
          end
          local alive
          if not tempwalk then
            alive = og.summon(newob, "weapon", WEAP_CIRCLE_PROTECTION)
            if not alive then
              return false
            end
            didheal = didheal + 1
          else
            alive = og.add_ob("weapon", WEAP_CIRCLE_PROTECTION)
            if not alive then
              return false
            end
            tempwalk:s_set_hitpoints(og.fadd(tempwalk:s_hitpoints(),
                                             alive:s_hitpoints()))
            alive:set_dead(1)
            didheal = didheal + 1
          end
          if self:has_guy() then
            self:g_set_exp(self:g_exp() +
                           og.exp_from_action(self, newob, "protection", 0))
          end
        end
      end
      if didheal == 0 then
        return false
      else
        local message
        if didheal == 1 then
          message = "Druid protected 1 man!"
        else
          message = "Druid protected " .. didheal .. " men!"
        end
        if self:team_num() == 0 or self:has_guy() then
          og.emit_notification(message)
        end
        og.emit_sound(C.SOUND_HEAL)
      end
    else
      return false
    end
  end
  return true
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 9.0, 12.0, 4.0, 0.5)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 8, 3, 8, 12, 1)
end

og.register_hooks("living", "core:druid", {
  do_special = do_special,
  set_difficulty = set_difficulty,
  level_up = level_up,
})
