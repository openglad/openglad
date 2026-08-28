-- modes lib: mode_caps — the D5 live-spawn cap toolkit shared by the soccer/onslaught directors: spawn marking, manifest cap banking, fire_frequency pause/resume (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- Generator spawns carry stats bit 32768 (free beyond the BIT_* range;
-- test_mode_bindings pins its round-trip) so the cap census still counts
-- them after a clear_owner family (tower, treehouse) severs the owner
-- link. Marking happens in the shared customize_spawn the onslaught
-- script registers for the core generator families.
local SPAWN_MARK_BIT = 32768

-- Added onto the authored fire_frequency to pause a generator: busy gains
-- fire_frequency on every fire, so a paused generator re-arms 16384 ticks
-- (~23 min) out — never inside a match. The authored value rides inside
-- the paused one, so no per-generator storage is needed (R6).
local PAUSE_FIRE_FREQUENCY = 16384

-- The bot-squad provenance mark (stats bit 65536) lives in the shared core
-- pack now, beside the one squad seam that stamps it (docs/lineup-design.md
-- C1); this module re-exports the pair so the modes keep their name for it.
local lineup = og.use("core:lineup")

local function mark_spawn(spawn)
  spawn:s_set_bit_flags(SPAWN_MARK_BIT, 1)
end

local function is_marked_spawn(w)
  return w:s_query_bit_flags(SPAWN_MARK_BIT)
end

-- Bank a manifest row's spawn_caps into 8 mode vars (team byte keyed,
-- holes = -1 = uncapped) so the per-tick reader never touches the row.
local function bank_caps(row, slot_base)
  for team = 0, 7 do
    local cap = -1
    if row.spawn_caps ~= nil then
      if row.spawn_caps[team] ~= nil then
        cap = row.spawn_caps[team]
      end
    end
    og.mode_set(slot_base + team, cap)
  end
end

-- fire_frequency is an integer-valued C++ float, so the +/- below are
-- exact (well inside 2^24) and need no og.f* shims.
local function pause_generator(gen)
  local freq = gen:fire_frequency()
  if freq < PAUSE_FIRE_FREQUENCY then
    gen:set_fire_frequency(freq + PAUSE_FIRE_FREQUENCY)
  end
end

-- Resuming clamps any pause-sized busy residue a mid-pause fire banked,
-- so the generator re-arms on its authored cadence.
local function resume_generator(gen)
  local freq = gen:fire_frequency()
  if freq >= PAUSE_FIRE_FREQUENCY then
    freq = freq - PAUSE_FIRE_FREQUENCY
    gen:set_fire_frequency(freq)
    if gen:busy() > freq then
      gen.busy = freq
    end
  end
end

-- The D5 cap: at director cadence every generator pauses while its team's
-- live marked spawns sit at the cap, and resumes below it. One fire can
-- slip through between cadences (the busy pause lands after it) — the cap
-- is a ceiling with one-spawn slop by design.
local function apply_caps(gens, spawn_count, slot_base)
  for k = 1, #gens do
    local gen = gens[k]
    local team = gen:team_num()
    local cap = -1
    if team < 8 then
      cap = og.mode_get(slot_base + team)
    end
    if cap >= 0 and spawn_count[team + 1] >= cap then
      pause_generator(gen)
    else
      resume_generator(gen)
    end
  end
end

return {
  SPAWN_MARK_BIT = SPAWN_MARK_BIT,
  BOT_MARK_BIT = lineup.BOT_MARK_BIT,
  PAUSE_FIRE_FREQUENCY = PAUSE_FIRE_FREQUENCY,
  mark_spawn = mark_spawn,
  is_marked_spawn = is_marked_spawn,
  mark_bot = lineup.mark_bot,
  bank_caps = bank_caps,
  pause_generator = pause_generator,
  resume_generator = resume_generator,
  apply_caps = apply_caps,
}
