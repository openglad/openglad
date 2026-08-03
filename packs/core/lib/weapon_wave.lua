-- core:wave/wave2 — three-stage energy wave promotion (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- WAVE dying becomes WAVE2, WAVE2 dying becomes WAVE3, and WAVE3 (no
-- on_death) finally expires. This is the old death-hook promise that dead is
-- set first "so that we can easily reverse the decision :)": each stage
-- un-deads itself, then refills hitpoints so the new stage starts at full
-- strength.

local WEAPON_WAVE2 = assert(og.family_id("weapon", "core:wave2"))
local WEAPON_WAVE3 = assert(og.family_id("weapon", "core:wave3"))

-- wave_on_death: promote to the second wave stage instead of dying.
local function wave_on_death(self)
  self.dead = 0
  self:transform_to("weapon", WEAPON_WAVE2)
  self.hp = self.max_hp
  return true
end

-- wave2_on_death: promote to the third (and last) wave stage.
local function wave2_on_death(self)
  self.dead = 0
  self:transform_to("weapon", WEAPON_WAVE3)
  self.hp = self.max_hp
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:wave   on_death = wave.wave_on_death
--   core:wave2  on_death = wave.wave2_on_death
return {
  wave_on_death = wave_on_death,
  wave2_on_death = wave2_on_death,
}
