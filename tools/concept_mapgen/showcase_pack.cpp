/* The Ninefold Court's embedded showcase pack — court.lua source + writer.
 *
 * See showcase_pack.h. The Lua text lives here as a raw string so the
 * campaign package regenerates content-reproducibly from this tool alone
 * (no source-tree path lookups at run time).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "showcase_pack.h"

#include <filesystem>
#include <fstream>

namespace conceptgen {

const char* showcase_pack_id()
{
    return "org.openglad.concept.showcase";
}

const char* showcase_court_lua()
{
    return R"LUA(-- The Ninefold Court (scen 605) — the Concept Playground's level-script
-- showcase, shipped INSIDE builtin/org.openglad.concept.glad as the
-- embedded pack org.openglad.concept.showcase. Campaign packs mount with
-- their campaign and unmount with it, so this fight logic exists exactly
-- when the court does.
--
-- The encounter is IMPOSSIBLE as pure level data:
--   * the Magistrate (the lone archmage) is invincible while any of the
--     four corner pillar generators stands (BIT_INVINCIBLE stamped in
--     on_load, dropped by on_tick when the last pillar dies),
--   * once the wards fail, the Court passes judgment every 300 ticks: a
--     ninefold ring of explosions around the arena center,
--   * every 3rd generator spawn is promoted to an "Adjutant" through the
--     generator customize_spawn hook.
--
-- Determinism cookbook (docs/lua-classpacks-design.md §3) applies to every
-- line: og.div/og.mod for integer / and %, one og.f* call per C float op,
-- og.rand as the only randomness, arrays only, and NO mutable sim state in
-- locals/upvalues (R6) — fight state is re-derived from the world on every
-- dispatch, with one world-resident ledger noted at on_load.

local C = og.C

local COURT_LEVEL = 605

-- Arena geometry in pixels (the court is 30x22 tiles, single floor).
-- Constants are the one kind of upvalue R6 permits.
local CENTER_X = 15 * C.GRID_SIZE
local CENTER_Y = 11 * C.GRID_SIZE
local RING_RADIUS = 4 * C.GRID_SIZE

-- Family bytes resolved once at chunk load.
local FAM_ARCHMAGE = og.family_id("living", "core:archmage")
local FAM_EXPLOSION = og.family_id("fx", "core:explosion")

-- The nine strikes of one judgment pulse: eight compass points on the ring
-- plus the center — the Court judges ninefold. Offsets are percent pairs
-- (cos/sin at 45-degree steps scaled by 100) so the radius math stays in
-- integers: dx = og.div(RING_RADIUS * pct, 100), per cookbook R1.
local JUDGMENT_STRIKES = {
  {100, 0}, {71, 71}, {0, 100}, {-71, 71},
  {-100, 0}, {-71, -71}, {0, -100}, {71, -71},
  {0, 0},
}

-- Live pillar census: hostile generators still standing, re-counted from
-- og.oblist() on every dispatch (R6: derive, don't remember).
local function live_pillars()
  local obs = og.oblist()
  local count = 0
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_GENERATOR
        and ob:team_num() ~= 0
        and ob:dead() == 0 then
      count = count + 1
    end
  end
  return count
end

-- The Magistrate: the highest-level living archmage on a hostile team.
local function find_boss()
  local obs = og.oblist()
  local boss = nil
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:family() == FAM_ARCHMAGE
        and ob:team_num() ~= 0
        and ob:dead() == 0 then
      if boss == nil or ob:s_level() > boss:s_level() then
        boss = ob
      end
    end
  end
  return boss
end

-- Per-entity death hook (og.set_entity_hooks; consumed when it fires): the
-- victory fanfare. The engine's own win logic is untouched — the level
-- still completes the standard way (every hostile dead, then walk out the
-- south-gate exit); this only decorates the moment the bench breaks.
local function on_boss_death(ent)
  og.emit_notification("The Magistrate falls!", 80)
  og.emit_notification("The Ninefold Court is broken.", 80)
  og.emit_sound(C.SOUND_SPARKLE)
  og.emit_sound(C.SOUND_MONEY)
end

-- One judgment pulse. Each strike is an EXPLOSION fx built like the core
-- bomb recipe in packs/core/scripts/effect_bomb.lua: zero hitpoints,
-- ANI_EXPLODE, stats level (level drives the blast radius), and damage. The
-- explosion animates for a few frames before its death deals the damage —
-- that animation IS the telegraph. Team 3 is unused in this level, so the
-- judgment burns every side alike (is_friendly holds for no one). og.rand
-- jitters each strike by up to half a tile — the only randomness (R4).
local function judgment_pulse(boss)
  og.emit_notification("The Court passes judgment!", 60)
  og.emit_sound(C.SOUND_BLAST)
  for i = 1, #JUDGMENT_STRIKES do
    local fx = og.add_ob("fx", FAM_EXPLOSION)
    if fx ~= nil then
      local dx = og.div(RING_RADIUS * JUDGMENT_STRIKES[i][1], 100)
      local dy = og.div(RING_RADIUS * JUDGMENT_STRIKES[i][2], 100)
      local jx = og.rand(C.GRID_SIZE) - og.div(C.GRID_SIZE, 2)
      local jy = og.rand(C.GRID_SIZE) - og.div(C.GRID_SIZE, 2)
      fx:set_team_num(3)
      fx:set_real_team_num(3)
      fx:s_set_hitpoints(0)
      fx:s_set_level(4)
      fx:set_ani_type(C.ANI_EXPLODE)
      fx:set_floor(0)                  -- set_floor before setxy (obmap rule)
      fx:setxy(CENTER_X + dx + jx, CENTER_Y + dy + jy)
      -- Mild damage, scaled gently by the bench's own rank: one C float
      -- add (R2).
      fx:set_damage(og.fadd(10.0, boss:s_level()))
    end
  end
end

-- on_load: fires on each peer's first tick of the level (fresh start or
-- mid-join alike), so everything here derives from the world and is
-- idempotent. Stamps the ward and hangs the victory hook.
local function on_load(level)
  local boss = find_boss()
  if boss == nil then
    return
  end
  -- The ward state is a pure function of the live pillar census, so a peer
  -- joining mid-fight arrives at the same answer without any remembered
  -- history (cookbook R6 — no state hidden in the script).
  if live_pillars() > 0 then
    boss:s_set_bit_flags(C.BIT_INVINCIBLE, 1)
    og.emit_notification("The Court's wards hold while its pillars stand.",
                         120)
  end
  og.set_entity_hooks(boss, { on_death = on_boss_death })
end

-- on_tick: once unwarded, the Court passes judgment on a fixed cadence.
local function on_tick(level, tick)
  local boss = find_boss()
  if boss == nil then
    return
  end
  -- Anchored to the absolute level tick — a "ticks since the wards fell"
  -- origin would be remembered state (R6).
  if live_pillars() == 0 and og.mod(tick, 300) == 0 then
    judgment_pulse(boss)
  end
end

-- Level-wide death hook. Generators dispatch it exactly like livings do,
-- so a pillar falling is an EVENT rather than something polled for — the
-- ward phase machine needs no census diffing and no ledger to keep in sync.
local function on_entity_death(ent)
  if ent:order() == C.ORDER_GENERATOR then
    if ent:team_num() == 0 then
      return -- not one of the Court's pillars
    end
    local boss = find_boss()
    if boss == nil then
      return -- the bench is already broken: court adjourned
    end
    -- The dying generator is already flagged dead, so it is out of this count.
    local remaining = live_pillars()
    if remaining > 0 then
      og.emit_notification("A pillar falls: " .. remaining .. " wards remain.",
                           60)
      og.emit_sound(C.SOUND_CLANG)
    else
      boss:s_set_bit_flags(C.BIT_INVINCIBLE, 0)
      og.emit_notification("The last pillar falls. The wards fail!", 80)
      og.emit_sound(C.SOUND_BOLT)
    end
    return
  end
  -- A body fell: strike promoted Adjutants from the rolls. Their stamped
  -- name is the marker — no bookkeeping.
  if ent:team_num() ~= 0 and ent:s_name() == "Adjutant" then
    og.emit_notification("An Adjutant is struck from the rolls.", 50)
  end
end

-- Generator customize_spawn, registered from THIS pack only — the core
-- pack keeps its stock generator behavior everywhere else, and the hook
-- additionally gates on the court's level id so future concept levels
-- with generators stay untouched. Every 3rd spawn becomes an Adjutant.
--
-- "Every 3rd" is derived, not counted: cookbook R6 forbids a mutable
-- spawn counter in a script local/upvalue for FAMILY hooks (it would
-- escape snapshots and desync rejoining peers). The spawn's sim entity id
-- is assigned deterministically and identically on every peer, so
-- og.mod(og.entity_id(spawn), 3) == 0 picks a stable ~third of all spawns
-- instead.
local function customize_spawn(generator, spawn)
  if og.level_id() ~= COURT_LEVEL then
    return
  end
  if og.mod(og.entity_id(spawn), 3) ~= 0 then
    return
  end
  spawn:s_set_level(spawn:s_level() + 3)
  spawn:s_set_name("Adjutant")
  -- A small speed bonus and a hardier writ of office: one C float op per
  -- line (R2). Heal to the new maximum so the promotion is visible.
  spawn:set_stepsize(og.fadd(spawn:stepsize(), 1.0))
  spawn:s_set_max_hitpoints(og.fmul(spawn:s_max_hitpoints(), 1.5))
  spawn:s_set_hitpoints(spawn:s_max_hitpoints())
  spawn:set_damage(og.fadd(spawn:damage(), 4.0))
end

og.register_level_hooks(COURT_LEVEL, {
  on_load = on_load,
  on_tick = on_tick,
  on_entity_death = on_entity_death,
})

-- The four colleges' pillars: tents raise skeletons, towers seat mages,
-- bones loose haunts, treehouses post wardens. One registration per
-- generator family; core registers no generator hooks, so nothing is
-- shadowed.
local PILLAR_FAMILIES = {
  "core:tent", "core:tower", "core:bones", "core:treehouse",
}
for i = 1, #PILLAR_FAMILIES do
  og.register_hooks("generator", PILLAR_FAMILIES[i],
                    { customize_spawn = customize_spawn })
end
)LUA";
}

bool write_showcase_pack(const std::string& staging_dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path scripts = fs::path(staging_dir) / "packs" /
                             showcase_pack_id() / "scripts";
    fs::create_directories(scripts, ec);
    if (ec)
        return false;
    std::ofstream out(scripts / "court.lua", std::ios::binary);
    out << showcase_court_lua();
    return static_cast<bool>(out);
}

} // namespace conceptgen
