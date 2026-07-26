-- core:wave and core:wave2 — the mage's ENERGY WAVE, transliterated from
-- src/gameplay/families/weapon_family_wave.cpp. Cookbook
-- (docs/lua-classpacks-design.md §3) applies: every value moved here is a
-- field copy, and hitpoints/max_hitpoints are C++ floats that round-trip
-- exactly through the s_ accessors, so no og.f* or narrowing helper is
-- needed.
--
-- The wave is a three-stage weapon: WAVE dying becomes WAVE2, WAVE2 dying
-- becomes WAVE3, and WAVE3 (no on_death) finally expires. Each stage
-- un-deads itself first so weap::death()'s caller sees a live walker, and
-- refills hitpoints so the new stage starts at full strength.

local WEAPON_WAVE2 = og.family_id("weapon", "core:wave2")
local WEAPON_WAVE3 = og.family_id("weapon", "core:wave3")

-- wave_on_death: promote to the second wave stage instead of dying.
local function wave_on_death(self)
  self:set_dead(0)
  self:transform_to("weapon", WEAPON_WAVE2)
  self:s_set_hitpoints(self:s_max_hitpoints())
  return true
end

-- wave2_on_death: promote to the third (and last) wave stage.
local function wave2_on_death(self)
  self:set_dead(0)
  self:transform_to("weapon", WEAPON_WAVE3)
  self:s_set_hitpoints(self:s_max_hitpoints())
  return true
end

og.register_hooks("weapon", "core:wave", {
  on_death = wave_on_death,
})

og.register_hooks("weapon", "core:wave2", {
  on_death = wave2_on_death,
})
