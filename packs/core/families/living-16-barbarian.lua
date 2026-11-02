-- core:barbarian — boulder-toss special (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
local lc = og.use("living_common")
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local WEAP_BOULDER = assert(og.family_id("weapon", "core:boulder"))

-- The boulder is made, not fired, so its flight is not G.flight_px's: it
-- rolls the weapon table's 9 steps (gloader.cpp) of the speed do_special
-- sets below (a bot's 2 px a level, a hero's 1 + strength / 7, held to 1 ..
-- 15), with no level or straight-line growth, and its first walk only
-- turns it.
local BOULDER_LOS = 9

local function do_special(self)
  -- rule: A busy barbarian cannot throw a boulder.
  -- tests: WalkerSpecials.barbarian_hurl_boulder_drives_simulation
  -- tests: SemBarbarian.a_busy_barbarian_throws_no_boulder
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local shot = self:fire()
  -- failed somehow? !?!
  -- rule: A boulder rides on the barbarian's hammer shot: if the hammer cannot be fired (blocked
  --   pad, no MP) there is no boulder. With a body on the pad the classic fire() lands a melee blow
  --   instead.
  -- tests: SemBarbarian.a_boulder_is_made_only_from_a_hammer_that_flew
  if not shot then
    return false, "COULD NOT FIRE"
  end
  local boulder = og.add_ob("weapon", WEAP_BOULDER)
  -- plumbing: add_ob failure for the boulder: 'COULD NOT CREATE BOULDER'.
  if not boulder then
    return false, "COULD NOT CREATE BOULDER"
  end
  boulder:set_floor(shot:floor())  -- boulder rolls on the thrower's floor
  boulder:center_on(shot)
  boulder:set_owner(self)
  boulder.level = self.level
  boulder:set_lastx(shot:lastx())
  boulder:set_lasty(shot:lasty())
  -- Set our boulder's speed and extra damage ..
  -- rule: A hero's boulder rolls at 1 + strength/7 px a step and hits for strength/5 extra; a
  --   monster's rolls at 2 px per level and hits for its level extra.
  -- tests: SemBoulder.a_heros_boulder_goes_by_strength_and_a_monsters_by_level
  if self:has_guy() then
    -- guy strength is a short: strength / 7 is INTEGER division in the C++,
    -- then the int joins 1.0f in a single float add.
    boulder:set_stepsize(1 + self:g_strength() // 7)
    -- damage is a C++ float and strength/5 is FLOAT division: per-op
    -- rounding.
    boulder.damage = og.fadd(boulder:damage(),
                             og.fdiv(self:g_strength(), 5.0))
  else
    boulder:set_stepsize(self.level * 2)
    -- damage is a C++ float: per-op rounding.
    boulder.damage = og.fadd(boulder:damage(), self.level)
  end
  -- rule: A boulder's speed is held to 1..15 px a step.
  -- tests: PackLuaBarbarian.a_zero_step_summon_is_floored_to_one
  -- tests: SemBoulder.a_boulders_speed_is_held_to_fifteen
  if boulder:stepsize() < 1 then
    boulder:set_stepsize(1)
  end
  -- rule: Part of the rule above that begins "A boulder's speed is held to 1..15 px a ..."
  -- tests: PackLuaBarbarian.a_zero_step_summon_is_floored_to_one
  -- tests: SemBoulder.a_boulders_speed_is_held_to_fifteen
  if boulder:stepsize() > 15 then
    boulder:set_stepsize(15)
  end
  -- rule: The boulder leaves along the shot's heading at its own speed: each nonzero heading
  --   component becomes +/- stepsize, a zero component stays zero.
  -- tests: PackLuaBarbarian.a_zero_step_summon_is_floored_to_one
  -- tests: SemBoulder.a_boulder_keeps_the_sign_of_each_heading_component
  if boulder:lasty() > 0 then
    boulder:set_lasty(boulder:stepsize())
  elseif boulder:lasty() < 0 then
    boulder:set_lasty(-boulder:stepsize())
  end
  -- rule: Part of the rule above that begins "The boulder leaves along the shot's heading at its
  --   ..."
  -- tests: PackLuaBarbarian.a_zero_step_summon_is_floored_to_one
  -- tests: SemBoulder.a_boulder_keeps_the_sign_of_each_heading_component
  if boulder:lastx() > 0 then
    boulder:set_lastx(boulder:stepsize())
  elseif boulder:lastx() < 0 then
    boulder:set_lastx(-boulder:stepsize())
  end
  -- Legacy sentinel: 5000 means "explode this boulder on impact."
  -- rule: Slot 2 throws the EXPLODING boulder (legacy skip_exit 5000 = explode on impact); slot 1's
  --   boulder just stops where it lands. Busy += 1 + 5 x slot.
  -- tests: WalkerSpecials.barbarian_hurl_boulder_drives_simulation
  -- tests: WalkerSpecials.barbarian_exploding_boulder_drives_simulation
  -- tests: WalkerSpecials.npc_orc_eats_a_corpse_and_npc_barbarian_arms_its_boulder
  -- tests: parity:barbarian_boulder_impact_scen99
  if self:current_special() == 2 then
    boulder:set_skip_exit(5000)
  else
    boulder:set_skip_exit(0)
  end
  shot.dead = 1
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(og.fadd(self:busy(), 1.0),
                      self:current_special() * 5)
  return true
end

local function boulder_reach(self)
  local step
  -- rule: The bot gate measures a boulder's flight with the same speed rule the throw uses (hero by
  --   strength, monster by level), clamped 1..15, times 8 steps.
  -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
  -- tests: parity:barbarian_bot_boulder_far_fixes_scen99
  -- tests: FamilyBehaviors.check_special_default_families_with_new_specials_on
  -- tests: SemBarbarian.the_boulder_gate_measures_the_flight_with_the_throws_own_speed
  if self:has_guy() then
    step = 1 + self:g_strength() // 7
  else
    step = 2 * self.level
  end
  return (BOULDER_LOS - 1) * og.clamp(step, 1, 15)
end

-- The barbarian's nearest living foe on its floor, when the barbarian faces
-- it (the boulder leaves along its heading) and it is inside the
-- boulder's flight; else nil.
local function boulder_target(self)
  local reach = boulder_reach(self)
  local f = G.nearest_living_foe(self, 2 * reach + 64, true)
  -- rule: A boulder target is the nearest living foe on the barbarian's floor that it faces and
  --   whose larger edge gap is within the boulder's flight.
  -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
  -- tests: FamilyBehaviors.check_special_default_families_with_new_specials_on
  if not f or not G.faces(self, f) then
    return nil
  end
  local gap_x, gap_y = G.gap(self, f)
  -- rule: Part of the rule above that begins "A boulder target is the nearest living foe on ..."
  -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
  -- tests: FamilyBehaviors.check_special_default_families_with_new_specials_on
  if og.max(gap_x, gap_y) > reach then
    return nil
  end
  return f
end

-- Gameplay Fixes: the classic barbarian had no gate and threw at nothing.
-- With the setting on HURL BOULDER wants a faced foe inside the boulder's
-- flight; EXPLODING BOULDER wants that foe with a second living foe inside
-- the blast around it, and neither the barbarian nor an ally of its own in
-- the blast. Draw-free: finders and positions only.
local function check_special_ai(self)
  -- rule: Classic: the barbarian has no bot gate and throws at nothing.
  -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
  -- tests: GameplayFixesBots.barbarian_exploding_boulder_wants_a_cluster_with_no_ally_in_it
  -- tests: FamilyBehaviors.check_special_default_families, parity:barbarian_bot_boulder_far_scen99
  -- tests: parity:barbarian_bot_explode_lone_scen99
  if not gf.on() then
    return true
  end
  -- Both throws refuse a busy barbarian (do_special's guard) and a blocked hammer pad.
  -- rule: Gameplay Fixes: a busy barbarian, or one whose hammer pad is blocked (wall or body), does
  --   not roll a throw.
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  -- tests: GameplayFixesBots.barbarian_boulder_refuses_a_pad_on_a_wall
  -- tests: parity:barbarian_bot_boulder_pad_body_fixes_scen99
  if lc.is_busy(self) or not G.pad_clear(self) then
    return false
  end
  local slot = self:current_special()
  -- rule: Gameplay Fixes: HURL BOULDER wants a boulder target; EXPLODING BOULDER wants that target
  --   plus a second living foe inside the blast around it, and neither the barbarian nor an ally
  --   inside the blast.
  -- tests: GameplayFixesBots.barbarian_exploding_boulder_wants_a_cluster_with_no_ally_in_it
  -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
  -- tests: parity:barbarian_bot_explode_lone_fixes_scen99
  -- tests: SemBarbarian.an_exploding_boulder_gate_with_no_boulder_target_refuses
  if slot == 2 then
    local f = boulder_target(self)
    -- rule: Part of the rule above that begins "Gameplay Fixes: HURL BOULDER wants a boulder
    --   target; EXPLODING ..."
    -- tests: GameplayFixesBots.barbarian_exploding_boulder_wants_a_cluster_with_no_ally_in_it
    -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
    -- tests: parity:barbarian_bot_explode_lone_fixes_scen99
    -- tests: SemBarbarian.an_exploding_boulder_gate_with_no_boulder_target_refuses
    if not f then
      return false
    end
    local reach = G.blast_reach(self.level)
    local foes = G.living_foes(self, G.RING_SCAN, true)
    local near = 0
    -- plumbing: counts living foes near the boulder target; rule row living-16-barbarian.lua:117
    for i = 1, #foes do
      -- rule: Part of the rule above that begins "Gameplay Fixes: HURL BOULDER wants a boulder
      --   target; EXPLODING ..."
      -- tests: GameplayFixesBots.barbarian_exploding_boulder_wants_a_cluster_with_no_ally_in_it
      -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
      -- tests: parity:barbarian_bot_explode_lone_fixes_scen99
      -- tests: SemBarbarian.an_exploding_boulder_gate_with_no_boulder_target_refuses
      if foes[i] == f or f:distance_to_ob(foes[i]) <= reach then
        near = near + 1
      end
    end
    -- rule: Part of the rule above that begins "Gameplay Fixes: HURL BOULDER wants a boulder
    --   target; EXPLODING ..."
    -- tests: GameplayFixesBots.barbarian_exploding_boulder_wants_a_cluster_with_no_ally_in_it
    -- tests: GameplayFixesBots.barbarian_boulder_wants_a_faced_foe_inside_its_short_flight
    -- tests: parity:barbarian_bot_explode_lone_fixes_scen99
    -- tests: SemBarbarian.an_exploding_boulder_gate_with_no_boulder_target_refuses
    return near >= 2 and G.blast_clear(self, f, reach, true)
  end
  return boulder_target(self) ~= nil
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.family("living", {
  id = "core:barbarian",
  wire_id = 16,
  name = "BARBARIAN",
  short_name = "BARBAR.",
  stats  = { strength = 14, dexterity = 5, constitution = 14,
             intelligence = 8, armor = 8, level = 1 },
  combat = { hp = 150, melee_damage = 25, stepsize = 3,
             fire_delay = 5.5, fire_mp_cost = 2 },
  costs  = { hire = 350,
             train = { strength = 5, dexterity = 35, constitution = 5,
                       intelligence = 35, armor = 50, level = 200 } },
  specials = {
    { id = "hurl_boulder",      name = "HURL BOULDER",      mp_cost = 20, detail = "Throw a massive stone boulder at your enemies.", icon = "icons/hurl_boulder.png" },
    { id = "exploding_boulder", name = "EXPLODING BOULDER", mp_cost = 30, detail = "Hurl a boulder so hard that it explodes and hits foes all around.", icon = "icons/exploding_boulder.png" },
    default_cast = do_special,
  },
  default_weapon = "core:hammer",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 0.5,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "barby.png",
  animation = "standard",
  ai_line_of_sight = 12,
  description = "Barbarians are powerful and resist some magic damage, but have more will than skill. They are tough, tending to bash their way through trouble with heavy iron hammers.",
  names = { "Thor", "Conan", "Beowulf", "Cronus", "Pallas", "Atlas",
            "Prometheus", "Titan" },
  playable = true,
  playable_order = 1,
  glyph = "B",
  glyph_ascii = "B",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  check_special_ai = check_special_ai,
  level_up = level_up,
})
