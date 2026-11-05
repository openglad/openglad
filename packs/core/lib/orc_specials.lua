-- core lib: orc_specials — HOWL (the yell stun) and EAT CORPSE, cast by the orc and the orc captain (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- Both read og.tuning(self), so each family that casts them declares the
-- same five yell_*/corpse_* keys in its own tuning block.

local C = og.C
local lc = og.use("living_common")
local gf = og.use("gameplay_fixes")

local M = {}

function M.yell(self)
  -- yell and 'freeze' foes (special 1)
  -- rule: An orc (or captain) that is busy (> 0) cannot howl; the refusal is free.
  -- tests: KitCaptain.captain_howl_refuses_while_busy
  -- tests: KitCaptain.promoted_orc_keeps_howl_and_eat_corpse
  -- tests: FamilyOrc.r15_howl_freezes_foes_in_radius_and_spares_the_horde
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local foes = og.find_foes_in_range(
    "ob", og.combat.yell_radius(self.level), self)
  -- rule: Classic: HOWL with nobody in reach still costs its price, 2 busy and roars. Gameplay
  --   Fixes: a howl that would stun nobody is refused for free (NO FOE IN RANGE).
  -- tests: parity:orc_howl_no_foe_scen99, parity:orc_howl_no_foe_fixes_scen99
  -- tests: FamilyOrc.r15_howl_freezes_foes_in_radius_and_spares_the_horde
  -- tests: SemOrcHowl.a_howl_with_a_foe_in_reach_goes_off_with_fixes_on_or_off
  if gf.on() and #foes == 0 then
    -- Gameplay Fixes: a howl that would stun nobody costs nothing.
    return false, "NO FOE IN RANGE"
  end
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 2.0)

  -- plumbing: stun loop over foes in yell_radius; rule below
  for i = 1, #foes do
    local foe = foes[i]
    -- plumbing: nil foe guard inside the foe list
    if foe then
      local con
      -- rule: HOWL stuns every foe in 160+20*level px for max(0, yell_stun_base + rand(level*10) -
      --   rand(con*10)) ticks of frozen_delay, where con is a hero's constitution, or for a guyless
      --   foe its hp/30 (trunc); a zero con draws nothing.
      -- tests: FamilyOrc.r15_howl_freezes_foes_in_radius_and_spares_the_horde
      -- tests: SemOrcHowl.a_guyless_foe_resists_a_howl_by_a_thirtieth_of_its_hitpoints
      -- unreachable: add_frozen_stun's stun_total floors a negative add at 0
      --   (include/openglad/core/combat_math.h), so removing og.max(0, ...) changes no state;
      --   candidate for a defensive-guard note
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

  og.emit_positional_sound(self, C.SOUND_ROAR)
  return true
end

function M.eat_corpse(self)
  -- eat corpse for health (specials 2/3/4 and the default case)
  -- can't eat if we're 'full'
  -- rule: EAT CORPSE refuses at full health, with no bloodstain within the finder's reach on its
  --   floor, or with the nearest stain out of reach (24 squared px) - each refusal free.
  -- tests: KitCaptain.captain_eats_a_corpse_when_hurt, parity:orc_eat_corpse_scen99, SemOrc.an_orc_on_a_balcony_cannot_eat_the_corpse_below
  if self.hp >= self.max_hp then
    return false, "ALREADY AT FULL HEALTH"
  end
  local t = og.tuning(self)
  local corpse = og.find_nearest_blood(self)
  -- no blood, so do nothing
  if not corpse then
    return false, "NO CORPSE NEARBY"
  end
  local dist = self:distance_to_ob_center(corpse)
  if dist > t.corpse_eat_range then
    return false, "NO CORPSE IN RANGE"
  end
  -- hp is a C++ float: per-op rounding.
  self.hp = og.fadd(self.hp, corpse.level * t.corpse_heal_per_level)
  -- narrows to int16 (short) like the C++ destination.
  self:do_heal_effects(
    nil, self, og.i16(corpse.level * t.corpse_heal_per_level))
  -- rule: Eating a corpse heals corpse level * 5, pays a hero EXP_EAT_CORPSE experience, notifies
  --   "<name> ate a corpse." and consumes the stain.
  -- tests: KitCaptain.captain_eats_a_corpse_when_hurt
  -- tests: FamilyOrc.r15_eating_a_corpse_heals_by_its_level_and_consumes_it
  -- tests: KitCaptain.promoted_orc_keeps_howl_and_eat_corpse
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, corpse, "eat_corpse", 0))
  end
  -- Print the eating notice
  og.emit_notification(
    og.entity_display_name(self, "Orc") .. " ate a corpse.")
  -- Overheal clamps only AFTER the exp grant and the notice (C++ order),
  -- which is why this heal is not a self:heal_clamped call.
  -- rule: A corpse meal larger than the wound tops up to max hp, no further (clamped after exp and
  --   notice).
  -- tests: KitCaptain.captain_eats_a_corpse_when_hurt
  if self.hp > self.max_hp then
    self.hp = self.max_hp
  end
  corpse.dead = 1
  corpse:death()
  return true
end

-- HOWL as both families cast it: the yell, resting howl_rest ticks between
-- casts whatever the New Specials setting (lib/living_common.lua). HOWL is
-- slot 1 for the orc and the captain alike.
local HOWL_SLOT = 1
M.howl = lc.with_rest(M.yell, HOWL_SLOT, "howl_rest", "HOWL")

-- A bot gate that also holds HOWL while it rests.
function M.howl_gate(gate)
  return lc.gate_with_rest(gate, HOWL_SLOT)
end

-- The declarations that reference this module (packs/core/families/):
--   core:orc          HOWL cast = orc_specials.howl, default_cast = orc_specials.eat_corpse,
--                     check_special_ai = orc_specials.howl_gate(...)
--   core:orc_captain  through kit_captain.howl, kit_captain.ai_howl and kit_captain.eat_corpse
return M
