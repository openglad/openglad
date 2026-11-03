-- core:orc — yell stun, corpse eating (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The shareware-era source labelled the Orc its "registered monster."
local lc = og.use("living_common")
local ai = og.use("ai")
local G = og.use("gates")
-- HOWL and EAT CORPSE live in lib/orc_specials.lua: the orc captain casts them too.
local orc = og.use("orc_specials")

-- The meal's reach: tuning.corpse_eat_range below, the squared centre
-- distance orc_specials.eat_corpse compares (a gate reads no tuning).
local CORPSE_EAT_REACH_SQ = 24

-- Gameplay Fixes: the classic gate (a foe within 130 px, acquired first on
-- both values, so its draws stand) was the same for both specials. With
-- the setting on HOWL wants a body the yell can stun (a living foe or a
-- generator: a frozen tent makes nothing) inside the yell's own radius, and
-- EAT CORPSE wants the orc below 60 percent of its health with a stain under
-- it, whatever the foes are doing (the classic gate let the meal refuse).
local function fixed_gate(self, classic_answer)
  local slot = self:current_special()
  -- rule: Classic: an orc bot HOWLs (and tries EAT CORPSE) whenever a foe is within 130 px.
  --   Gameplay Fixes: HOWL wants the orc not busy and a body the yell can stun (a living foe or a
  --   generator) inside the yell's own radius; the classic foe search still runs first for its
  --   draws.
  -- tests: GameplayFixesBots.orc_howl_wants_a_target_inside_the_yell
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  -- tests: SemOrc.the_fixed_gate_leaves_any_other_slot_to_the_classic_answer
  if slot == 1 then
    -- rule: Classic: an orc bot HOWLs (and tries EAT CORPSE) whenever a foe is within 130 px.
    -- tests: GameplayFixesBots.orc_howl_wants_a_target_inside_the_yell
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    -- tests: SemOrc.the_fixed_gate_leaves_any_other_slot_to_the_classic_answer
    return not lc.is_busy(self) and G.targets_within(self, 1, og.combat.yell_radius(self.level))
  end
  -- rule: Gameplay Fixes: an orc bot EATs a corpse when it is below 60% health with a stain under
  --   it (within the meal's squared reach 24), whatever the foes are doing; classic asked only for
  --   a foe within 130 and let the meal itself refuse.
  -- tests: GameplayFixesBots.orc_eats_when_hurt_on_a_corpse_whatever_the_foe
  if slot == 2 then
    -- rule: Gameplay Fixes: an orc bot EATs a corpse when it is below 60% health with a stain under
    --   it (within the meal's squared reach 24), whatever the foes are doing.
    -- tests: GameplayFixesBots.orc_eats_when_hurt_on_a_corpse_whatever_the_foe
    return G.hp_below(self, 3, 5) and G.corpse_underfoot(self, CORPSE_EAT_REACH_SQ) ~= nil
  end
  return classic_answer
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
    { id = "howl",       name = "HOWL",       mp_cost = 25, cast = orc.howl, detail = "Howl in rage, stunning nearby enemies in their tracks. Rests 10 s.", icon = "icons/howl.png" },
    { id = "eat_corpse", name = "EAT CORPSE", mp_cost = 20, detail = "Regain health by devouring the corpses of your foes.", icon = "icons/eat_corpse.png" },
    default_cast = orc.eat_corpse,
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
  description = "Orcs are a basic 'grunt'; strong and hard to hurt, they don't do much more than inflict pain. Orcs can't attack at range.",
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
    -- HOWL rests this many ticks between casts, whatever the New Specials setting
    howl_rest = 120,
  },

  check_special_ai = orc.howl_gate(G.with_classic(ai.foe_within(130), fixed_gate)),  -- fixed per-tick AI range
  set_difficulty = set_difficulty,
  level_up = level_up,
})
