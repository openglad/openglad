-- The Raspberry Isle (scen 1) — the Imaginations dream-log's level
-- script, shipped INSIDE builtin/imaginations.glad as the embedded pack
-- imaginations.dreams. Campaign packs mount with their campaign and
-- unmount with it, so this dream exists exactly when the isle does.
--
-- The isle is a dream being told, and the script is what pure level data
-- cannot say:
--   * the level NARRATES its own creation — the submitted argument about
--     where the sea goes plays back as dream-log lines in the opening
--     minute,
--   * the Sea Wizard is invincible while either of his bailey
--     watchtowers stands (BIT_INVINCIBLE stamped in on_load, dropped by
--     on_entity_death when the second tower falls),
--   * dreams turn enemies into friends: a stable share of the colleges'
--     spawns wake up already on the dreamer's side, renamed "Dreamkin".
--
-- Determinism cookbook (docs/lua-classpacks-design.md §3) applies to
-- every line: og.div/og.mod for integer math, arrays only, and NO
-- mutable sim state in locals/upvalues (R6) — everything re-derives from
-- the world on every dispatch.

local C = og.C

local ISLE_LEVEL = 1

-- Family bytes resolved once at chunk load. core:#20 is the immobile
-- arrow turret (tower1) — the wizard's watchtowers.
local FAM_MAGE = og.family_id("living", "core:mage")
local FAM_WATCHTOWER = og.family_id("living", "core:#20")

-- Live watchtower census: hostile tower1 livings still standing,
-- re-counted from og.oblist() on every dispatch (R6: derive, don't
-- remember).
local function live_watchtowers()
  local obs = og.oblist()
  local count = 0
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:family() == FAM_WATCHTOWER
        and ob:team_num() ~= 0
        and ob:dead() == 0 then
      count = count + 1
    end
  end
  return count
end

-- The Sea Wizard: the highest-level hostile mage still dreaming.
local function find_wizard()
  local obs = og.oblist()
  local wizard = nil
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:family() == FAM_MAGE
        and ob:team_num() ~= 0
        and ob:dead() == 0 then
      if wizard == nil or ob:s_level() > wizard:s_level() then
        wizard = ob
      end
    end
  end
  return wizard
end

-- Per-entity death hook (consumed when it fires): the dream is won. The
-- engine's own win logic is untouched — the level still completes the
-- standard way (every hostile dead, then walk out at the landing).
local function on_wizard_death(ent)
  og.emit_notification("The Sea Wizard wakes up!", 80)
  og.emit_notification("The sun comes back to the isle.", 80)
  og.emit_notification("Dream won. Sail home!", 80)
  og.emit_sound(C.SOUND_SPARKLE)
  og.emit_sound(C.SOUND_MONEY)
end

-- on_load: fires on each peer's first tick (fresh start or mid-join), so
-- everything derives from the world and is idempotent. The ward state is
-- a pure function of the live watchtower census.
local function on_load(level)
  local wizard = find_wizard()
  if wizard == nil then
    return
  end
  if live_watchtowers() > 0 then
    wizard:s_set_bit_flags(C.BIT_INVINCIBLE, 1)
    og.emit_notification("The wizard cannot be touched while", 120)
    og.emit_notification("his watchtowers watch. Break them!", 120)
  end
  og.set_entity_hooks(wizard, { on_death = on_wizard_death })
end

-- on_tick: the dream-log reads the submitted argument back over the
-- landing — the level narrating its own creation. Anchored to absolute
-- level ticks (an origin of "since landing" would be remembered state).
local function on_tick(level, tick)
  if tick == 80 then
    og.emit_notification("The dream-log says: the sea is", 90)
    og.emit_notification("all around us. We land on the", 90)
    og.emit_notification("shores and run at the middle!", 90)
  end
  if tick == 260 then
    og.emit_notification("...is the sea in the middle, or", 90)
    og.emit_notification("around? BOTH. It is my dream.", 90)
  end
end

-- Level-wide death hook: watchtowers falling are events, not polls.
local function on_entity_death(ent)
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  if ent:family() ~= FAM_WATCHTOWER then
    return
  end
  if ent:team_num() == 0 then
    return
  end
  local wizard = find_wizard()
  if wizard == nil then
    return
  end
  -- The dying tower is already flagged dead, so it is out of this count.
  local remaining = live_watchtowers()
  if remaining > 0 then
    og.emit_notification("A watchtower falls! One still", 80)
    og.emit_notification("watches over the wizard.", 80)
    og.emit_sound(C.SOUND_CLANG)
  else
    wizard:s_set_bit_flags(C.BIT_INVINCIBLE, 0)
    og.emit_notification("Both watchtowers are down —", 80)
    og.emit_notification("the Sea Wizard can be hurt now!", 80)
    og.emit_sound(C.SOUND_BOLT)
  end
end

-- Generator customize_spawn, gated on the isle's level id so future
-- dream-log levels with colleges stay untouched. In a dream, some
-- enemies were friends all along: a stable fifth of every college's
-- spawns defect to the dreamer's team at birth. "Every 5th" is derived,
-- not counted (R6): the spawn's sim entity id is assigned identically on
-- every peer, so og.mod picks the same stable share everywhere.
local function customize_spawn(generator, spawn)
  if og.level_id() ~= ISLE_LEVEL then
    return
  end
  if og.mod(og.entity_id(spawn), 5) ~= 0 then
    return
  end
  spawn:set_team_num(0)
  spawn:set_real_team_num(0)
  spawn:s_set_name("Dreamkin")
end

og.register_level_hooks(ISLE_LEVEL, {
  on_load = on_load,
  on_tick = on_tick,
  on_entity_death = on_entity_death,
})

-- The three colleges of the isle: the mage tower, the bone tent, the elf
-- treehouse. One registration per generator family; core registers no
-- generator hooks, so nothing is shadowed.
local COLLEGE_FAMILIES = {
  "core:tent", "core:tower", "core:treehouse",
}
for i = 1, #COLLEGE_FAMILIES do
  og.register_hooks("generator", COLLEGE_FAMILIES[i],
                    { customize_spawn = customize_spawn })
end
