-- core gold/silver bar, life gem, key — on_eat hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The three scoring families bank through og.award_score(team, points) — the
-- binding for the file-local award_score() helper, which adds to
-- world->m_score[team] and emits EventKind::ScoreChange. Teams outside the
-- score table are dropped inside the binding, exactly like the C++
-- is_valid_score_team() guard, so callers pass the raw team number.

local C = og.C
local FX_FLASH = assert(og.family_id("fx", "core:flash"))
local gf = og.use("gameplay_fixes")
-- gold_bar_on_eat: banks score_per_level*level for the eater's team. Only
-- team 0 or a roster character (myguy) can cash a bar in.
local function gold_bar_on_eat(self, eater)
  -- rule: Only team 0 or a roster hero can cash a gold bar: it banks score_per_level*level for the
  --   eater's team and is consumed with SOUND_MONEY; a guyless monster leaves it.
  -- tests: parity:treasure_gold_bar_team_reject_scen99, parity:treasure_gold_bar_pickup_scen99
  -- tests: TreasureEat.gold_bar, SemValuables.a_hero_of_any_team_cashes_a_gold_bar
  if eater.team == 0 or eater:has_guy() then
    og.award_score(eater.team,
                   og.tuning(self).score_per_level * self.level)
    self.dead = 1
    og.emit_positional_sound(self, C.SOUND_MONEY)
  end
  return true
end

-- silver_bar_on_eat: same shape, its own (smaller) score_per_level.
local function silver_bar_on_eat(self, eater)
  -- rule: Same rule as above for the silver bar (its own smaller
  --   score_per_level).
  -- tests: TreasureEat.silver_bar
  -- tests: SemValuables.a_hero_cashes_a_silver_bar_a_guyless_monster_leaves_it
  if eater.team == 0 or eater:has_guy() then
    og.award_score(eater.team,
                   og.tuning(self).score_per_level * self.level)
    self.dead = 1
    og.emit_positional_sound(self, C.SOUND_MONEY)
  end
  return true
end

-- life_gem_on_eat: what the original called "get back some of lost man's
-- xp" is a dead character's banked hitpoints, claimable only by the gem's
-- own team. The flash pops where the gem sat, on the same floor.
local function life_gem_on_eat(self, eater)
  -- rule: A life gem (a fallen character's banked hp) can be claimed only by its own team: it banks
  --   trunc(max(hp,0)) score, flashes and dies; another team walks over it.
  -- tests: parity:treasure_life_gem_enemy_reject_scen99, parity:treasure_life_gem_pickup_scen99
  -- tests: TreasureEat.life_gem
  if eater.team ~= self.team then
    return true  -- only our team can get these
  end
  local hp = og.max(self.hp, 0.0)
  -- shim kept: hp is a C++ float; the score credit is its int truncation.
  og.award_score(eater.team, og.trunc(hp))
  local flash = og.add_ob("fx", FX_FLASH)
  -- plumbing: add_ob flash nil guard
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(self:floor())  -- flash where the gem sat
    flash:center_on(self)
  end
  self.dead = 1
  self:death()
  return true
end

-- key_on_eat: sets one bit of the eater's key mask. Re-touching a key the
-- eater already holds is silent.
local function key_on_eat(self, eater)
  local level = self.level
  -- key_level_mask(): 1u << clamp(level, 0, 30) (key_mask.h).
  local shift = og.clamp(level, 0, 30)
  local key_mask = 1 << shift
  -- rule: A key sets bit 1<<clamp(level,0,30) on the eater's own key mask; touching a key it
  --   already holds is silent (no message, mask unchanged).
  -- tests: PackLuaTreasure.a_key_already_held_is_a_silent_no_op, parity:treasure_key_pickup_scen99
  if (eater:keys() & key_mask) == 0 then  -- just got it?
    eater:set_keys(eater:keys() | key_mask)  -- ie, 2, 4, 8, 16...
    local who
    -- rule: The key pickup line names a hero by its character name and anyone else by its stats
    --   name.
    -- tests: SemValuables.the_key_line_names_a_hero_by_its_guy_and_others_by_their_stats_name
    if eater:has_guy() then
      who = eater:g_name()
    else
      who = eater:s_name()
    end
    local message = string.format("%s picks up key %d", who, level)
    -- rule: Only team-0 key pickups are announced (the line below, then SOUND_MONEY); other teams
    --   pick keys up silently.
    -- tests: parity:treasure_key_team1_silent_scen99
    -- tests: PackLuaTreasure.a_key_already_held_is_a_silent_no_op
    if eater.team == 0 then  -- only show players picking up keys
      -- With the setting on the line goes to every view: keys are per-walker, so "Gorik picks up
      -- key 3" is the only way the rest of the party learns who can open that door.
      -- rule: With Gameplay Fixes on the key pickup line goes to every view; off (the old game's
      --   do_notify(message, eater)), to the eater's seat, or every view when no seat drives it.
      -- tests: SemValuables.the_key_line_goes_to_the_eaters_seat_with_the_fixes_off_and_to_everyone_with_them_on
      -- tests: PackLuaTreasure.a_key_already_held_is_a_silent_no_op, parity:treasure_key_pickup_scen99
      if gf.on() then
        og.emit_notification(message)
      else
        og.emit_notification(message, 0, eater)
      end
      og.emit_positional_sound(eater, C.SOUND_MONEY)
    end
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:gold_bar    on_eat = valuables.gold_bar_on_eat
--   core:silver_bar  on_eat = valuables.silver_bar_on_eat
--   core:life_gem    on_eat = valuables.life_gem_on_eat
--   core:key         on_eat = valuables.key_on_eat
return {
  gold_bar_on_eat = gold_bar_on_eat,
  silver_bar_on_eat = silver_bar_on_eat,
  life_gem_on_eat = life_gem_on_eat,
  key_on_eat = key_on_eat,
}
