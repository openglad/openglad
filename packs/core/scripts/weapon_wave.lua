-- core:wave/wave2 — three-stage energy wave promotion (cookbook: docs/lua-classpacks-design.md §3).
--
-- WAVE dying becomes WAVE2, WAVE2 dying becomes WAVE3, and WAVE3 (no
-- on_death) finally expires. Each stage un-deads itself first so
-- weap::death()'s caller sees a live walker, and refills hitpoints so the
-- new stage starts at full strength.

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

og.register_hooks("weapon", "core:wave", {
  on_death = wave_on_death,
})

og.register_hooks("weapon", "core:wave2", {
  on_death = wave2_on_death,
})
