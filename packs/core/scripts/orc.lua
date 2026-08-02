-- core:orc — yell stun, corpse eating (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The shareware-era source labelled the Orc its "registered monster."

local C = og.C
local lc = og.use("living_common")
local ai = og.use("ai")

local function yell(self)
  -- yell and 'freeze' foes (special 1)
  if lc.is_busy(self) then
    return false
  end
  local t = og.tuning(self)
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 2.0)

  local foes = og.find_foes_in_range(
    "ob", og.combat.yell_radius(self.level), self)
  for i = 1, #foes do
    local foe = foes[i]
    if foe then
      local con
      if foe:has_guy() then
        con = foe:g_constitution()
      else
        -- (int32)(hitpoints / 30.0f): one float division, then trunc.
        con = og.trunc(og.fdiv(foe.hp, 30.0))
      end
      con = og.max(con, 0)
      -- rng order (FLAGGED adjudication): 10 + rng(level*10) - rng(con*10)
      -- is two draws in one C++ expression, operand order unspecified;
      -- parity chose LEFT-FIRST — level roll, then constitution roll.
      -- level/con are never negative, so the C++ uint32 casts are
      -- value-preserving.
      local level_roll = og.rand0(self.level * t.yell_level_roll_mult)
      local con_roll = og.rand0(con * t.yell_con_roll_mult)
      local stun = og.max(0, t.yell_stun_base + level_roll - con_roll)
      -- (the C++ also TRACEs "orc yell stun add discarded: thaw
      -- immunity" when raw < 0 — TESTING-only diagnostics, no sim effect)
      foe:add_frozen_stun(stun)
    end
  end

  og.emit_sound(C.SOUND_ROAR)
  return true
end

local function eat_corpse(self)
  -- eat corpse for health (specials 2/3/4 and the default case)
  -- can't eat if we're 'full'
  if self.hp >= self.max_hp then
    return false
  end
  local t = og.tuning(self)
  local corpse = og.find_nearest_blood(self)
  -- no blood, so do nothing
  if not corpse then
    return false
  end
  local dist = self:distance_to_ob_center(corpse)
  if dist > t.corpse_eat_range then
    return false
  end
  -- hp is a C++ float: per-op rounding.
  self.hp = og.fadd(self.hp, corpse.level * t.corpse_heal_per_level)
  -- narrows to int16 (short) like the C++ destination.
  self:do_heal_effects(
    nil, self, og.i16(corpse.level * t.corpse_heal_per_level))
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, corpse, "eat_corpse", 0))
  end
  -- Print the eating notice
  og.emit_notification(
    og.entity_display_name(self, "Orc") .. " ate a corpse.")
  -- Overheal clamps only AFTER the exp grant and the notice (C++ order),
  -- which is why this heal is not a self:heal_clamped call.
  if self.hp > self.max_hp then
    self.hp = self.max_hp
  end
  corpse.dead = 1
  corpse:death()
  return true
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 14.0, 7.0, 6.0, 3.0)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

-- Orc promotion is descriptor data, not a registrable living hook.

og.register_hooks("living", "core:orc", {
  specials = {
    [1] = yell,
    default = eat_corpse,
  },
  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI range
  set_difficulty = set_difficulty,
  level_up = level_up,
})
