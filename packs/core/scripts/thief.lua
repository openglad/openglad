-- core:thief — behavior hooks transliterated from family_thief.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local C = og.C
local FX_BOMB = og.family_id("fx", "core:bomb")
local FX_CLOUD = og.family_id("fx", "core:cloud")
-- Not present in og.C; values from core/constants.h (both equal ANI_ATTACK).
local ANI_BOMB = 1
local ANI_SPIN = 1

local function check_special_ai(self)
  if self:current_special() == 1 then
    -- drop bomb
    local foe = self:foe()
    if foe then
      local distance = self:distance_to_ob(foe)
      if distance < 130 and distance > 35 then
        return false
      end
    else
      local _, howmany = og.find_foes_in_range("ob", 110, self)
      return howmany >= 3
    end
    return true -- fallthrough for foe case when distance is acceptable
  elseif self:current_special() == 3 then
    local myrange
    if self:shifter_down() == 0 then
      myrange = 80 + 4 * self:s_level()
    else
      myrange = 16 + 4 * self:s_level()
    end
    local _, howmany = og.find_foes_in_range("ob", myrange, self)
    return howmany >= 1
  end
  return true -- default: go for it
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 12, 4, 8, 1)
end

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- drop bomb
    local newob = og.add_ob("fx", FX_BOMB)
    if not newob then
      return false
    end
    newob:set_ani_type(ANI_BOMB)
    if self:has_guy() then
      self:g_set_total_shots(self:g_total_shots() + 1)
      self:g_set_scen_shots(self:g_scen_shots() + 1)
    end
    -- og::combat::bomb_damage(L): §2.7 legacy 15*(L+1) below knee 210
    -- (= L13); ceiling 300
    newob:set_damage(og.soften(15 * (self:s_level() + 1), 210, 300))
    newob:set_floor(self:floor()) -- bomb armed on the thief's floor (A8)
    newob:setxy(self:xpos() + og.div(self:sizex(), 2)
                  - og.div(newob:sizex(), 2),
                self:ypos() + og.div(self:sizey(), 2)
                  - og.div(newob:sizey(), 2))
    newob:set_owner(self)
    -- Run away if we're AI
    if self:user() == -1 then
      local tempx = og.rand(3) - 1
      local tempy = og.rand(3) - 1
      if tempx == 0 and tempy == 0 then
        tempx = 1
      end
      self:s_force_command(C.COMMAND_WALK, 20, tempx, tempy)
    end
  elseif sp == 2 then
    -- cloak: og::combat::cloak_total(cur, gain) inlined —
    -- new = max(cur, min(cur + gain, kInvisibilityCloakCap=350))
    local cur = self:invisibility_left()
    local gain = 20 + og.rand(20) * self:s_level()
    local summed = cur + gain
    local capped = math.min(summed, 350)
    self:set_invisibility_left(math.max(capped, cur))
  elseif sp == 3 then
    if self:shifter_down() == 0 then
      -- normal taunt
      if self:busy() > 0 then
        return false
      end
      local foes = og.foes_in_range(self, 80 + 4 * self:s_level())
      for i = 1, #foes do
        local ob = foes[i]
        -- Two RNG draws in one C++ expression; drawn left (self) first.
        local myroll = og.rand(self:s_level())
        local obroll = og.rand(ob:s_level())
        if myroll >= obroll then
          ob:set_foe(self)
          ob:set_leader(self)
          if ob:act_type() ~= C.ACT_CONTROL then
            ob:s_force_command(C.COMMAND_FOLLOW,
                               10 + og.rand(self:s_level()), 0, 0)
          end
        end
      end
      og.emit_notification(og.entity_display_name(self, "THIEF")
                             .. ": 'Nyah Nyah!'")
      self:set_busy(og.fadd(self:busy(), 2.0))
    else
      -- charm opponent
      if self:busy() > 0 then
        return false
      end
      local newlist, howmany =
        og.find_foes_in_range("ob", 16 + 4 * self:s_level(), self)
      if howmany < 1 then
        return false
      end
      local didheal = 0
      local generic2 = 0
      for i = 1, #newlist do
        if didheal ~= 0 then
          break
        end
        local ob = newlist[i]
        if ob:real_team_num() == 255 and ob:order() == C.ORDER_LIVING then
          local generic = self:s_level() - ob:s_level()
          if generic < 0 or og.rand(20) == 0 then
            ob:set_foe(self)
            ob:attack(self)
            generic2 = 1
          else
            ob:set_real_team_num(ob:team_num())
            ob:set_team_num(self:team_num())
            if self:foe() == ob then
              ob:set_foe(nil)
            else
              ob:set_foe(self:foe())
            end
            -- og::combat::soften(75 + 25*diff, kThiefCharmKnee=375,
            -- kThiefCharmCeiling=490); §2.9: identity through diff 12
            ob:set_charm_left(og.soften(75 + generic * 25, 375, 490))
            generic2 = 0
          end
          didheal = didheal + 1
        end
      end
      if didheal == 0 then
        return false
      end
      local tempstr
      if generic2 ~= 0 then
        tempstr = og.entity_display_name(self, "Thief")
                    .. " failed to charm!"
      else
        tempstr = og.entity_display_name(self, "Thief")
                    .. " charmed an opponent!"
      end
      og.emit_notification(tempstr)
      self:set_busy(og.fadd(self:busy(), 10.0))
    end
  else
    -- poison cloud (case 4 and default)
    if self:busy() > 0 then
      return false
    end
    local newob = og.summon(self, "fx", FX_CLOUD)
    if not newob then
      return false
    end
    self:set_busy(og.fadd(self:busy(), 5.0))
    newob:set_ignore(1)
    newob:set_lifetime(40 + 3 * self:s_level())
    newob:set_invisibility_left(10)
    newob:set_ani_type(ANI_SPIN)
    newob:set_damage(self:s_level())
  end
  return true
end

og.register_hooks("living", "core:thief", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  level_up = level_up,
})
