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

og.family("living", {
  id = "core:orc",
  wire_id = 14,
  name = "ORC",
  short_name = og.NIL,
  stats  = { strength = 18, dexterity = 8, constitution = 16,
             intelligence = 5, armor = 11, level = 1 },
  combat = { hp = 140, melee_damage = 23, stepsize = 3,
             fire_delay = 7, fire_mp_cost = 2 },
  costs  = { hire = 300,
             train = { strength = 6, dexterity = 15, constitution = 5,
                       intelligence = 40, armor = 50, level = 200 } },
  specials = {
    { id = "howl",       name = "HOWL",       mp_cost = 25, cast = yell },
    { id = "eat_corpse", name = "EAT CORPSE", mp_cost = 20 },
    default_cast = eat_corpse,
  },
  default_weapon = "core:rock",
  flags = { "NO_RANGED" },
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = "core:orc_captain",
  promotion_level_req = 5,
  death_message = "ORC DIED",
  sprite = "orc.png",
  animation = "standard",
  ai_line_of_sight = 20,
  description = "Orcs are a basic 'grunt'; strong and hard to hurt, they don't do much more than inflict pain. Orcs can't attack at range.\n\nSpecial: Howl",
  names = { "Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog",
            "Krosh" },
  playable = true,
  playable_order = 9,
  glyph = "o",
  glyph_ascii = "o",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Yell stun: og.max(0, stun_base + rand0(level*level_roll_mult)
    --                       - rand0(con*con_roll_mult))
    yell_stun_base = 10,
    yell_level_roll_mult = 10,
    yell_con_roll_mult = 10,
    -- Corpse eating: reach gate (px) and heal per corpse level
    corpse_eat_range = 24,
    corpse_heal_per_level = 5,
  },

  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI range
  set_difficulty = set_difficulty,
  level_up = level_up,
})
