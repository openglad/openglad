-- mode_mutant — registers the Mutant impl (lib/mode_mutant_impl.lua) for every manifest row with mode == "mutant", plus the pack-scoped teleport-range clamp on the core blink families (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local match = og.use("mode_match")
local levels = og.use("mode_levels")
local mutant = og.use("mode_mutant_impl")

core.register_mode(match.rows_for(levels), "mutant", {
  on_mode_init = mutant.on_mode_init,
  on_mode_tick = mutant.on_mode_tick,
  on_damage = mutant.on_damage,
  on_entity_death = mutant.on_entity_death,
  on_respawn = mutant.on_respawn,
})

-- ---------------------------------------------------------------------------
-- Teleport-range clamp (issue #170: "teleport range must be limited for
-- this to make sense" — a full-map blink by ANYONE breaks the hunt).
--
-- handle_teleport has no engine default (returning false wastes the
-- special outright), so these overrides must carry the core behavior on
-- every other level of this campaign and clamp only where the manifest
-- says the level is a Mutant arena. They unmount with the campaign.
-- Clamp = min(family range, 160): the skeleton keeps its shorter
-- level-scaled tunnel; the mage/archmage full-map blink becomes ranged.
-- The mage's placed-marker recall rides walker::teleport, so on Mutant
-- levels a marker is deliberately ignored — a full-map recall is exactly
-- the escape the clamp exists to forbid.
-- ---------------------------------------------------------------------------

local function mutant_level()
  local row = levels.levels[og.level_id()]
  if row == nil then
    return false
  end
  return row.mode == "mutant"
end

-- core:mage / core:archmage — the living_common blink, range-clamped on
-- Mutant levels.
local function blink_handle_teleport(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  if mutant_level() then
    self:teleport_ranged(mutant.T.teleport_range)
  else
    self:teleport()
  end
  return true
end

-- core:skeleton — the fixed 18 px/level tunnel, capped on Mutant levels.
local function skeleton_handle_teleport(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  local range = self.level * 18
  if mutant_level() then
    range = og.min(range, mutant.T.teleport_range)
  end
  self:teleport_ranged(range)
  return true
end

og.register_hooks("living", "core:mage", {
  handle_teleport = blink_handle_teleport,
})
og.register_hooks("living", "core:archmage", {
  handle_teleport = blink_handle_teleport,
})
og.register_hooks("living", "core:skeleton", {
  handle_teleport = skeleton_handle_teleport,
})
