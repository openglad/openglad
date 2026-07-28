-- core:thief — bomb, cloak, taunt/charm, poison cloud (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local FX_BOMB = og.family_id("fx", "core:bomb")
local FX_CLOUD = og.family_id("fx", "core:cloud")

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
      local _, foe_count = og.find_foes_in_range("ob", 110, self)
      return foe_count >= 3
    end
    return true -- fallthrough for foe case when distance is acceptable
  elseif self:current_special() == 3 then
    local range
    if self:shifter_down() == 0 then
      range = 80 + 4 * self.level
    else
      range = 16 + 4 * self.level
    end
    local _, foe_count = og.find_foes_in_range("ob", range, self)
    return foe_count >= 1
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
    local bomb = og.add_ob("fx", FX_BOMB)
    if not bomb then
      return false
    end
    bomb.ani_type = C.ANI_BOMB
    if self:has_guy() then
      self:g_set_total_shots(self:g_total_shots() + 1)
      self:g_set_scen_shots(self:g_scen_shots() + 1)
    end
    bomb.damage = og.combat.bomb_damage(self.level)
    bomb:set_floor(self:floor()) -- bomb armed on the thief's floor (A8)
    bomb:setxy(self:xpos() + self:sizex() // 2
                 - bomb:sizex() // 2,
               self:ypos() + self:sizey() // 2
                 - bomb:sizey() // 2)
    bomb:set_owner(self)
    -- An AI thief flees its own armed bomb.
    if self:user() == -1 then
      local flee_dx = og.rand(3) - 1
      local flee_dy = og.rand(3) - 1
      if flee_dx == 0 and flee_dy == 0 then
        flee_dx = 1
      end
      self:s_force_command(C.COMMAND_WALK, 20, flee_dx, flee_dy)
    end
  elseif sp == 2 then
    -- cloak
    local cur = self:invisibility_left()
    local gain = 20 + og.rand(20) * self.level
    self:set_invisibility_left(og.combat.cloak_total(cur, gain))
  elseif sp == 3 then
    if self:shifter_down() == 0 then
      -- normal taunt
      if self:busy() > 0 then
        return false
      end
      local foes = og.foes_in_range(self, 80 + 4 * self.level)
      for i = 1, #foes do
        local foe = foes[i]
        -- Two RNG draws in one C++ expression; drawn left (self) first.
        -- (Bounds lean on the engine invariant level >= 1; the bare og.rand
        -- is the loud tripwire if that ever breaks.)
        local my_roll = og.rand(self.level)
        local foe_roll = og.rand(foe.level)
        if my_roll >= foe_roll then
          foe:set_foe(self)
          foe:set_leader(self)
          if foe:act_type() ~= C.ACT_CONTROL then
            foe:s_force_command(C.COMMAND_FOLLOW,
                                10 + og.rand(self.level), 0, 0)
          end
        end
      end
      og.emit_notification(og.entity_display_name(self, "THIEF")
                             .. ": 'Nyah Nyah!'")
      -- shim kept: busy is a C++ float: per-op float rounding.
      self:set_busy(og.fadd(self:busy(), 2.0))
    else
      -- charm opponent
      if self:busy() > 0 then
        return false
      end
      local foes, foe_count =
        og.find_foes_in_range("ob", 16 + 4 * self.level, self)
      if foe_count < 1 then
        return false
      end
      local handled = 0
      local resisted = 0
      for i = 1, #foes do
        if handled ~= 0 then
          break
        end
        local target = foes[i]
        if target:real_team_num() == 255
            and target:order() == C.ORDER_LIVING then
          local level_diff = self.level - target.level
          if level_diff < 0 or og.rand(20) == 0 then
            target:set_foe(self)
            target:attack(self)
            resisted = 1
          else
            target:set_real_team_num(target.team)
            target.team = self.team
            if self:foe() == target then
              target:set_foe(nil)
            else
              target:set_foe(self:foe())
            end
            -- og::combat::soften(75 + 25*diff, kThiefCharmKnee=375,
            -- kThiefCharmCeiling=490); §2.9: identity through diff 12
            target:set_charm_left(og.soften(75 + level_diff * 25, 375, 490))
            resisted = 0
          end
          handled = handled + 1
        end
      end
      if handled == 0 then
        return false
      end
      local message
      if resisted ~= 0 then
        message = og.entity_display_name(self, "Thief")
                    .. " failed to charm!"
      else
        message = og.entity_display_name(self, "Thief")
                    .. " charmed an opponent!"
      end
      og.emit_notification(message)
      -- shim kept: busy is a C++ float: per-op float rounding.
      self:set_busy(og.fadd(self:busy(), 10.0))
    end
  else
    -- poison cloud (case 4 and default)
    if self:busy() > 0 then
      return false
    end
    local cloud = og.summon(self, "fx", FX_CLOUD)
    if not cloud then
      return false
    end
    -- shim kept: busy is a C++ float: per-op float rounding.
    self:set_busy(og.fadd(self:busy(), 5.0))
    cloud:set_ignore(1)
    cloud.lifetime = 40 + 3 * self.level
    cloud:set_invisibility_left(10)
    cloud.ani_type = C.ANI_SPIN
    cloud.damage = self.level
  end
  return true
end

og.register_hooks("living", "core:thief", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  level_up = level_up,
})
