-- core lib: gameplay_fixes — the Gameplay Fixes setting and the few helpers the fixed branches of the classic specials share (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C

local M = {}

-- The setting as this session plays it. A pure read: it draws nothing and
-- has no side effect, so a gate may ask it anywhere. 0 = the classic game,
-- bugs included (docs/gameplay-fixes.md).
function M.on()
  return og.match_setting("gameplay_fixes") ~= 0
end

-- TURN UNDEAD's reach: 4 px per level, and with the fixes on never under
-- turn_undead_range_min. The roll keeps its classic strength either way
-- (walker::turn_undead).
function M.turn_range(self, t)
  local reach = t.turn_undead_range_per_level * self.level
  -- rule: TURN UNDEAD reaches 4 px per cleric level; with Gameplay Fixes on it never reaches less
  --   than turn_undead_range_min (60 px). The roll strength is unchanged.
  -- tests: GameplayFixesLua.gf_turn_range_has_a_floor_only_when_on
  -- tests: GameplayFixesCleric.turn_undead_reaches_sixty_px_when_on
  if not M.on() then
    return reach
  end
  return og.max(reach, t.turn_undead_range_min)
end

-- A cast that pre-refunded weapon costs and then could not fire: the rocks
-- (or bolts) that never flew are not refunded after all. fire() already
-- charged the one swing it made. magicpoints is a C++ float: per-op
-- rounding.
function M.take_back(self, rocks)
  self.magicpoints = og.fsub(self.magicpoints, rocks * self:s_weapon_cost())
end

-- A foe a charm can take: never charmed and a living-order body (the
-- finder hands over only the living); with the setting on, also not a hero
-- a player is driving (Possess's "HERO RESISTS" rule, walker:seat_driven).
-- `fixes` is the caller's one read of the setting, so a loop asks it once.
function M.charmable(foe, fixes)
  -- rule: A charm can take a foe that is not already charmed and is a living body; with Gameplay
  --   Fixes on, never a hero a player is driving (HERO RESISTS).
  -- tests: GameplayFixesLua.gf_charmable_skips_a_seated_hero_only_with_the_fixes
  if foe:real_team_num() ~= 255 then
    return false
  end
  if foe:order() ~= C.ORDER_LIVING then
    return false
  end
  if not fixes then
    return true
  end
  return not foe:seat_driven()
end

-- The nearest charmable foe in `foes` by distance_to_ob (a tie goes to the
-- first in the list), or nil. max_level nil = any level; otherwise only a
-- foe at or below it (a higher level always resists). Draws nothing.
-- Only the fixed branches call it, so `charmable` runs with fixes = true.
function M.nearest_charmable(self, foes, max_level)
  local best = nil
  local best_dist = 0
  -- plumbing: scan for the nearest charmable foe; rules below
  for i = 1, #foes do
    local foe = foes[i]
    local eligible = M.charmable(foe, true)
    -- rule: With Gameplay Fixes on, the thief's and archmage's charms pick the nearest charmable
    --   foe (distance_to_ob), ties to the first in the list, and only one at or below the caster's
    --   level where a level cap applies (a higher level always resists). Draws nothing.
    -- tests: GameplayFixesLua.gf_nearest_charmable_takes_the_nearest_eligible_foe
    -- tests: parity:thief_charm_nearest_fixes_scen99
    if eligible and max_level ~= nil then
      eligible = foe.level <= max_level
    end
    if eligible then
      local dist = self:distance_to_ob(foe)
      -- rule: With Gameplay Fixes on, the thief's and archmage's charms pick the nearest charmable
      --   foe (distance_to_ob), ties to the first in the list, and only one at or below the
      --   caster's level where a level cap applies (a higher level always resists).
      -- tests: GameplayFixesLua.gf_nearest_charmable_takes_the_nearest_eligible_foe
      -- tests: parity:thief_charm_nearest_fixes_scen99
      if best == nil or dist < best_dist then
        best = foe
        best_dist = dist
      end
    end
  end
  return best
end

-- The soldier's WHIRLWIND and DISARM and the archer's BARRAGE and EXPLODING
-- BOLT refuse as SPECIAL BUSY while busy is not zero. A hero fires on a
-- fractional delay (the class's delay less dexterity / 47) and busy only
-- counts down while it is above zero, so after one shot it rests a fraction
-- below zero for good, and the classic gate refuses forever. With the
-- setting on, busy at or below zero is not busy, as for every other
-- special. Draws nothing.
function M.special_busy(self)
  -- rule: The soldier's WHIRLWIND/DISARM and the archer's BARRAGE/EXPLODING BOLT refuse SPECIAL
  --   BUSY while busy. Classic: any non-zero busy refuses, so a hero whose fractional fire delay
  --   left busy a hair below zero is refused forever. Fixed: busy at or below zero is not busy.
  -- tests: GameplayFixesSpecialBusy.whirlwind_after_a_shot
  -- tests: GameplayFixesSpecialBusy.disarm_after_a_shot
  -- tests: GameplayFixesSpecialBusy.archer_specials_after_a_shot
  -- tests: GameplayFixesSpecialBusy.positive_and_zero_busy_are_unchanged
  if M.on() then
    return self:busy() > 0
  end
  return self:busy() ~= 0
end

-- Gameplay Fixes AC: a cast whose weapon's pad the grid refuses (a wall
-- face, the map edge; a tree crown for a weapon that does not walk the
-- forest; never water, which every weapon crosses) would pay fire()'s shot
-- and its waver for nothing. The cast asks first and refuses before it pays.
-- `weapon` is the family the cast fires (nil: the current weapon). A body on
-- the pad keeps the paid blow (walker:fire_pad_answer, the same pad and rules
-- as fire(), asked quietly). Draws nothing.
function M.pad_blocked_by_terrain(self, weapon)
  -- rule: With Gameplay Fixes on (AC), a cast whose launch pad is refused by terrain (a wall face,
  --   the map edge, a tree crown for a non-forest weapon; never water) is refused before it pays:
  --   the elf's volleys, the druid's bolts, BARRAGE (fix AS), EXPLODING BOLT, both boulders and ENERGY WAVE. A body
  --   on the pad does not count (it keeps the paid blow). Classic: the cast still pays and draws.
  -- tests: GameplayFixesLua.gf_pad_blocked_by_terrain_is_the_grid_half_only_with_the_fixes
  -- tests: GameplayFixesElf.a_volley_at_a_wall_spends_nothing_with_the_setting_on
  -- tests: GameplayFixesDruid.a_cast_at_terrain_spends_nothing_with_the_setting_on, SemArcher.a_barrage_at_terrain_spends_nothing_with_the_setting_on
  return M.on() and self:fire_pad_answer(weapon) == "terrain"
end

-- Gameplay Fixes AX: a summon's difficulty scaling at its level
-- (walker:set_difficulty). The difficulty percent skips team 0, the
-- player's side, and a fresh summon is on team 0 until its caster's team
-- is written. The cleric's RAISE GHOST and the archmage's elemental and
-- phantom write it after the scaling in the classic game, so a foe's
-- keeps 100 % on Easy and Hard (#495). With the setting on the summon
-- takes its caster's team first. The caller still writes the team after,
-- on both values. Draws nothing.
function M.scale_summon(summon, caster)
  -- rule: With Gameplay Fixes on (AX), the cleric's RAISE GHOST ghost and the archmage's elemental
  --   and phantom take their caster's team before their difficulty scaling, so a foe's summon takes
  --   the difficulty percent as its caster does; a summon of the player's side (team 0) never does.
  --   Classic: scaled while still on team 0, so a foe's keeps 100 %.
  -- tests: DifficultyScaling.a_foes_raised_ghost_takes_the_difficulty_only_with_the_setting_on
  -- tests: DifficultyScaling.a_foes_elemental_takes_the_difficulty_only_with_the_setting_on
  -- tests: DifficultyScaling.a_foes_phantom_takes_the_difficulty_only_with_the_setting_on
  -- tests: DifficultyScaling.a_players_elemental_and_phantom_never_scale
  -- tests: DifficultyScaling.a_players_raised_skeleton_and_ghost_never_scale
  -- tests: parity:cleric_raise_ghost_scen99, parity:archmage_summon_elemental_scen99
  if M.on() then
    summon.team = caster.team
  end
  summon:set_difficulty(summon.level)
end

-- Gameplay Fixes AZ (#502, #533): with the setting on everything fights for
-- the colour it wears and nothing follows its maker. A thing one walker makes
-- with og.add_ob (a thief's bomb, ENERGY WAVE, a boulder, a blast, a returning
-- knife) takes `owner` as its owner, as always, and with the setting on the
-- team number of `from` (`owner` itself unless given: a blast takes its
-- bomb's, its mine's or its shot's, a returning knife its knife's), which it
-- keeps whatever happens to its maker afterwards. Classic, it stays on the
-- team it was made with (team 0) and fights for the head of its owner chain.
-- Draws nothing.
function M.made_by(made, owner, from)
  made:set_owner(owner)
  -- rule: Gameplay Fixes AZ (#502, #533): a thing made from another thing takes that thing's team
  --   number (a blast its bomb's, its mine's or its shot's; a returning knife its knife's), and
  --   any other thing its owner's (a thief's bomb, ENERGY WAVE, a boulder). The setting's gate is
  --   take_team's.
  -- tests: SemLibsBomb.a_bomb_and_its_blast_wear_the_thiefs_colour_only_with_the_setting_on
  -- tests: SemLibsKnife.a_returning_knife_wears_the_knifes_colour_only_with_the_setting_on
  -- tests: SemMage.an_energy_wave_wears_the_mages_colour_through_its_promotions_only_with_the_setting_on
  M.take_team(made, from or owner)
end

-- The team half of made_by, for a thing whose owner is set apart: a wail's
-- fork takes the bolt it forks from, a meteor the rain it falls from. (An
-- ember needs no call: IMMOLATE's fire rides its elemental, so an ember takes
-- the elemental's colour at the drop on both settings.) Draws nothing.
function M.take_team(made, from)
  -- rule: Gameplay Fixes AZ (#502, #533): with the setting on a thing takes, when it is made, the
  --   team number of the thing it comes from (a thief's bomb, a mage's ENERGY WAVE, a barbarian's
  --   boulder; a blast from its bomb, mine or exploding shot; a returning knife from its knife; a
  --   wail's fork from its bolt; a meteor from its rain) and keeps it. Classic: the team it was
  --   made with.
  -- tests: SemLibsBomb.a_bomb_and_its_blast_wear_the_thiefs_colour_only_with_the_setting_on
  -- tests: SemLibsProjectiles.an_exploding_shots_blast_wears_the_shots_colour_only_with_the_setting_on
  -- tests: SemLibsKnife.a_returning_knife_wears_the_knifes_colour_only_with_the_setting_on
  -- tests: SemMage.an_energy_wave_wears_the_mages_colour_through_its_promotions_only_with_the_setting_on
  -- tests: SemBarbarian.a_boulder_wears_the_barbarians_colour_only_with_the_setting_on
  -- tests: SemWail.a_wail_fork_wears_the_colour_of_the_bolt_it_forks_from_with_the_setting_on
  -- tests: KitElemental.a_meteor_wears_its_rains_colour_with_the_setting_on
  -- tests: KitThief.a_mines_blast_wears_the_mines_colour_only_with_the_setting_on
  -- tests: parity:thief_bomb_colour_score_fixes_scen99
  -- tests: parity:thief_bomb_dead_thrower_scen99, parity:thief_bomb_dead_thrower_fixes_scen99
  if M.on() then
    made.team = from.team
  end
end

-- Gameplay Fixes AZ (#502): marks a weapon or effect that rides the walker
-- who owns it (a circle of protection, a mace, a boomerang, a hook blade, a
-- scare, IMMOLATE's fire, a returning knife's probe). With the setting on it
-- is part of that walker and goes with him: while he lives it fights for
-- whatever side he is on now (walker::alliance_head answers him for it).
-- Unmarked, a thing keeps the colour it was made with. Answers `thing`, so it
-- wraps the call that makes it; nil (a full world) stays nil. Draws nothing.
function M.rides(thing)
  -- rule: Gameplay Fixes AZ (#502): with the setting on a thing that rides its owner is marked
  --   KIT_RIDER when it is made, so it goes with him: it fights for his side of the moment, never
  --   against him, and turns back with him when a charm ends. Classic: no mark (one owner chain
  --   gives it his head anyway).
  -- tests: GameplayFixesChain.a_ring_of_protection_goes_with_its_walker_and_turns_back_with_him
  -- tests: SemShield.a_boomerang_goes_with_its_charmed_soldier_and_spares_his_new_knives
  -- tests: SemShield.a_mace_goes_with_its_charmed_cleric
  -- tests: KitCaptain.a_hook_blade_goes_with_its_charmed_captain
  -- tests: SemLibsGhostScare.a_scare_frightens_its_ghosts_foes_of_the_moment
  -- tests: KitElemental.a_charmed_elementals_fire_burns_his_foes_of_the_moment
  -- tests: SemLibsKnife.a_returning_knife_wears_the_knifes_colour_only_with_the_setting_on
  if thing and M.on() then
    thing:set_kit_state(thing:kit_state() | C.KIT_RIDER)
  end
  return thing
end

-- Gameplay Fixes BG (#588): what the guard of a MAGIC SHIELD, a BOOMERANG
-- or a hook blade passes by instead of destroying it as a shot. On both
-- settings that is the solid scenery a pack declares (blocks_placement: a
-- bone wall, a war banner, #472). With the setting on it is also a door and
-- a grown tree: a guard destroys shots in flight, and a thing that stands
-- is cut down by blows. Classic, the guard erases a door of 5,000 hit
-- points or a tree at once, with no key and no blow, and pays their damage.
-- A door or a tree is the order and the family both, because a golem's
-- family number is a door's. Every other body the guard meets (a shot, a
-- glow, a boulder, a blood stain) is destroyed as before. Draws nothing.
local WEAPON_DOOR = assert(og.family_id("weapon", "core:door"))
local WEAPON_TREE = assert(og.family_id("weapon", "core:tree"))

function M.guard_passes_by(body)
  -- rule: A guard (MAGIC SHIELD, BOOMERANG, hook blade) passes by the solid scenery a pack declares
  --   (blocks_placement: a bone wall, a war banner) on both settings, and with Gameplay Fixes BG
  --   also a door or a grown tree (weapon order and that family; a living golem is not a door);
  --   it destroys every other body. Classic: a door and a grown tree are destroyed like shots.
  -- tests: GameplayFixesLua.gf_guard_passes_by_scenery_and_with_the_setting_on_a_door_or_a_grown_tree
  -- tests: SemShield.a_guard_passes_an_enemy_door_and_a_grown_tree_by_with_the_setting_on
  -- tests: KitCaptain.hook_guard_passes_an_enemy_door_and_a_grown_tree_by_with_the_setting_on
  -- tests: SemShield.a_guard_passes_a_foes_bone_wall_and_war_banner_by, KitCaptain.hook_guard_passes_scenery_by
  if body:blocks_placement() then
    return true
  end
  if not M.on() then
    return false
  end
  if body:order() ~= C.ORDER_WEAPON then
    return false
  end
  local family = body:family()
  return family == WEAPON_DOOR or family == WEAPON_TREE
end

return M
