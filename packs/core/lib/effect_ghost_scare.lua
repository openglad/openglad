-- core:ghost_scare — expiring scare cloud frights nearby foes (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- on_death applies the fright through walker:s_force_fright(iterations,
-- dx, dy) — statistics::force_fright, the scare-MERGE variant of
-- force_command that inspects the command queue front (forced +
-- COMMAND_WALK → refresh count/direction instead of prepending).
-- s_force_command is NOT a substitute: it stacks overlapping scares
-- end-to-end instead of refreshing the active flee.

local C = og.C

-- ghost_scare_on_act: the scare cloud rides its caster for its whole life.
local function on_act(self)
  local owner = self:owner()
  -- rule: A ghost's scare cloud rides its caster for its whole life (re-centred each tick); without
  --   an owner it stays where it is; either way it then animates and expires by the default effect
  --   path.
  -- tests: EffectAct.ghost_scare_rides_its_caster_then_expires
  -- tests: SemLibsGhostScare.a_scare_without_its_ghost_stays_where_it_is
  if owner then
    self:center_on(owner)
  end
  return false  -- delegate to effect::act default animate/die path
end

-- ghost_scare_on_death: when the cloud expires, every living foe inside the
-- caster's scare radius is pushed into a forced flee-walk away from the cloud.
local function on_death(self)
  local owner = self:owner()
  -- dead() hands back the C++ int, and 0 is TRUTHY in Lua: the comparison
  -- against 0 is what makes this the `!self->owner()->dead()` test.
  -- rule: A scare whose ghost has died (or is gone) expires harmlessly: nobody is frightened.
  -- tests: EffectAct.effect_death_ghost_scare_frights_only_foes_in_radius
  -- tests: EffectDeathMore.effect_death_ghost_scare_forces_walk_commands_on_foes
  -- tests: SemLibsGhostScare.a_scare_whose_ghost_is_gone_frightens_nobody
  if not owner or owner:dead() ~= 0 then
    return false
  end

  -- The radius caps at 250 px before legacy 50 + 10*L grows off-screen;
  -- the cap begins at L21 and consumes no draw. og.scare_radius binds
  -- og::combat::scare_radius (core/combat_math.h).
  local foes, foe_count =
    og.find_foes_in_range("ob", og.scare_radius(owner.level), owner)
  -- rule: A scare with no foe within the caster's scare radius (50 + 10 per level, capped at 250
  --   px) does nothing and draws nothing.
  -- tests: EffectAct.effect_death_ghost_scare_frights_only_foes_in_radius
  -- tests: SemLibsGhostScare.a_scare_with_no_foe_in_its_radius_does_nothing_and_draws_nothing
  if foe_count < 1 then
    return false
  end

  -- plumbing: iterates the scare's foe list; rules below
  for i = 1, #foes do
    local foe = foes[i]
    -- rule: A scare frightens living foes only (never a generator or a weapon), each fleeing
    --   directly away from the cloud (sign of dx, dy).
    -- tests: EffectDeathMore.effect_death_ghost_scare_forces_walk_commands_on_foes
    -- tests: parity:effect_ghost_scare_emission_scen99
    -- tests: SemGhostScare.a_scare_frightens_living_foes_never_a_generator
    if foe and foe:order() == C.ORDER_LIVING then
      local flee_dx = og.sign(foe:xpos() - self:xpos())
      local flee_dy = og.sign(foe:ypos() - self:ypos())
      -- Duration soft-caps legacy 25*L at a 325 knee and 375 ceiling BEFORE
      -- the myguy-only constitution-resist draw. Keep that order so the draw
      -- count and stream position do not change; values through L13 are exact.
      local fright = og.scare_duration(owner.level)
      -- rule: A hero resists a scare by a constitution roll (rand0(con) ticks off the fright, one
      --   draw per hero, none for monsters); a hero who resists it entirely (fright <= 0) does not
      --   flee at all.
      -- tests: EffectDeathMore.effect_death_ghost_scare_forces_walk_commands_on_foes
      -- tests: SemGhostScare.a_hero_resists_a_scare_by_a_constitution_roll_a_monster_never_rolls
      if foe:has_guy() then
        local con_roll = og.rand0(foe:g_constitution())
        fright = fright - con_roll
      end
      if fright > 0 then
        foe:s_force_fright(fright, flee_dx, flee_dy)
      end
    end
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:ghost_scare  on_act = ghost_scare.on_act
--   core:ghost_scare  on_death = ghost_scare.on_death
return {
  on_act = on_act,
  on_death = on_death,
}
