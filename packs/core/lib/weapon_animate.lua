-- core:tree/blood/circle_protection/glow/sprinkle — on_animate; freeze hit (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- CALLER CONTRACT (weap::animate): a hook returning FALSE makes the caller
-- run death(). tree/blood and glow always return true — glow calls death()
-- itself — while circle_protection returns false to hand the death off,
-- and must NOT call death() on its own.

local C = og.C

-- constants/geometry only, no sim state (R6)
local NUM_FACINGS = C.NUM_FACINGS
local SPRINKLE_REFRESH_OWNER_LEVEL = 21   -- combat_math.h kSprinkleRefreshOwnerLevel
local SPRINKLE_REFRESH_FLOOR = 10         -- combat_math.h kSprinkleRefreshFloor

-- weapon_animate_step: the file-local helper of the same name. Advances the
-- weapon's animation by one frame with full bounds checking and reports
-- whether the advanced cycle reached the sequence's -1 sentinel.
--
-- curdir, ani_type and cycle may originate from an untrusted snapshot, so
-- the facing and the animation type are bounded before the row is addressed
-- and the cycle is bounded against the row length. The C++ guards that
-- remain -- `if (!self->ani)`, the ani_count bound, a null row, and a row
-- with no sentinel inside 128 frames -- are ALL the cases where
-- og.ani_row() returns nil, and an empty table is its `seq_len <= 0` stop;
-- every one of them means "no such sequence", i.e. stop (return true).
local function weapon_animate_step(self)
  local dir_index = og.u8(self:curdir())  -- C++ (int)(unsigned char)curdir()
  -- plumbing: weapon_animate_step facing bound: a snapshot-borne curdir outside 0..7 animates row 0
  --   instead of indexing out of range.
  if dir_index < 0 or dir_index >= NUM_FACINGS then
    dir_index = 0
  end
  local type_index = self:ani_type()
  type_index = og.max(type_index, 0)
  local ani_index = dir_index + type_index * NUM_FACINGS

  local seq = og.ani_row(self, ani_index)
  -- plumbing: weapon_animate_step: og.ani_row nil (no table, past ani_count, null row, no sentinel)
  --   or an empty row means 'no such sequence' -> report ended (true), so tree/glow reset to their
  --   rest state.
  if not seq then
    return true
  end
  local seq_len = #seq
  if seq_len <= 0 then
    return true
  end

  local c = self:cycle()
  -- plumbing: weapon_animate_step cycle bound: an out-of-range cycle restarts the row at frame 0.
  if c < 0 or c >= seq_len then
    c = 0
  end
  self:set_frame(seq[c + 1])  -- Lua arrays are 1-based; the C++ index is c
  c = c + 1                   -- advance into [1, seq_len]
  -- `seq[c] == -1` in C++: the sentinel sits at index seq_len and nowhere
  -- earlier (that is what seq_len means), so it is reached exactly when the
  -- advanced cycle equals the trimmed row length.
  local ended = (c == seq_len)
  -- og.i8: the C++ cycle field is a signed char
  self:set_cycle(og.i8(c))
  return ended
end

-- --- TREE and BLOOD: simple animation cycling ---

local function tree_blood_on_animate(self)
  -- rule: A tree or blood splat plays its current animation (type 0 = still, type 1 = sway/splash)
  --   through once frame by frame; any other type is treated as still, and when the row ends it
  --   returns to the still animation at frame 0.
  -- tests: WeapBehavior.weapon_family_animate_callbacks_and_sprinkle_hit_paths
  -- tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
  -- tests: SemWeaponAnimate.a_tree_or_blood_plays_its_sway_once_then_stands_still
  if self:ani_type() > 1 then
    self:set_ani_type(0)
  end
  if weapon_animate_step(self) then
    self:set_ani_type(0)
    self:set_cycle(0)
  end
  return true
end

-- --- CIRCLE_PROTECTION: orbit owner ---

local function circle_protection_on_animate(self)
  local owner = self:owner()
  -- rule: A druid's protection ring follows its owner every tick and vanishes the moment its owner
  --   is gone or dead or the ring's own hitpoints are spent.
  -- tests: WeapBehavior.weapon_family_animate_callbacks_and_sprinkle_hit_paths
  -- tests: parity:weapon_circle_protection_follow_scen99
  -- tests: SemWeaponAnimate.a_protection_ring_dies_with_its_owner_or_its_hitpoints
  if not owner
      or owner:dead() ~= 0
      or self.hp <= 0 then
    self:set_dead(1)
    return false  -- let the caller's default death handling proceed
  end
  self:center_on(owner)
  return true
end

-- --- GLOW: pulse animation with lifetime ---

local function glow_on_animate(self)
  -- rule: A glow (the cleric/mage halo effect) loops its pulse animation (type 2) forever; an
  --   illegal type above 2 is forced to the pulse, and every row end restarts the pulse at frame 0.
  -- tests: WeapBehavior.weapon_family_animate_callbacks_and_sprinkle_hit_paths
  -- tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
  -- tests: SemWeaponAnimate.a_glow_pulses_frame_by_frame_and_restarts_at_the_row_end
  if self:ani_type() > 2 then  -- illegal case
    self:set_ani_type(2)       -- pulse case
  end
  if weapon_animate_step(self) then
    self:set_ani_type(2)       -- pulse
    self:set_cycle(0)
  end
  local lifetime = self:lifetime()
  self:set_lifetime(lifetime - 1)
  -- rule: A glow burns one lifetime tick per animate and dies on the animate that finds its
  --   lifetime already below 1 (a glow with lifetime N shows for N+1 animates).
  -- tests: WeapBehavior.weapon_family_animate_callbacks_and_sprinkle_hit_paths
  -- tests: SemWeaponAnimate.a_glow_with_lifetime_n_shows_for_n_plus_one_animates
  if lifetime < 1 then
    self:set_dead(1)
    self:death()
  end
  return true
end

-- --- SPRINKLE: freeze foes on hit ---

-- The C++ dereferences `owner` unconditionally, so a sprinkle with no owner
-- was already undefined behavior there; here it is a clean script error.
local function sprinkle_on_hit_target(self, target, owner)
  -- rule: A faerie's sprinkle freezes only living creatures; hitting a generator, weapon or
  --   treasure freezes nothing.
  -- tests: WeapBehavior.weapon_family_animate_callbacks_and_sprinkle_hit_paths
  if target:order() ~= C.ORDER_LIVING then
    return true
  end
  local con = 0
  -- rule: A hero's constitution shortens a sprinkle freeze (freeze_duration bound shrinks by
  --   con/21); a guy-less creature resists with constitution 0.
  -- tests: WeapBehavior.weapon_family_animate_callbacks_and_sprinkle_hit_paths
  -- tests: SemWeaponAnimate.a_heros_constitution_shortens_a_sprinkle_freeze
  if target:has_guy() then
    con = target:g_constitution()
  end
  -- The roll is ALWAYS drawn; only the assignment below is gated, so refresh
  -- policy cannot move the RNG stream. og.i16 matches the short result.
  -- rule: A GLIMMER sprinkle (marked with the quarter-freeze kit bit) freezes for a quarter of the
  --   rolled time; a plain sprinkle freezes the full roll. The roll is drawn either way.
  -- tests: KitFaerie.glimmer_sprinkle_freezes_a_quarter
  -- tests: KitFaerie.glimmer_fires_eight_sprinkles_and_prepays
  local roll = og.i16(og.div(og.freeze_duration(owner.level, con), 1 + 3 * og.div(self:kit_state() & C.KIT_QUARTER_FREEZE, C.KIT_QUARTER_FREEZE)))
  -- At owner level 21+, do not replace a freeze still above 10 ticks. This
  -- mirrors the charm refresh floor while leaving lower-level thaw cadence
  -- unchanged.
  -- rule: A sprinkle hit re-sets the victim's freeze to the new roll, except (a) a level-21+ faerie
  --   never replaces a freeze still above 10 ticks (so it cannot chain a victim forever), and (b)
  --   nobody can be re-frozen during the 12-tick thaw-immunity window after a controlled hero
  --   thaws. The RNG roll is drawn on every living hit regardless.
  -- tests: SillinessBattery.sprinkle_refresh_gate_level_boundary_and_immunity
  -- tests: SillinessBattery.sprinkle_ai_victim_gate_window_and_escape
  -- tests: SemWeaponAnimate.a_high_level_sprinkle_refreshes_a_freeze_at_the_ten_tick_floor
  local refresh_gated = owner.level >= SPRINKLE_REFRESH_OWNER_LEVEL
      and target:s_frozen_delay() > SPRINKLE_REFRESH_FLOOR
  -- Never re-freeze inside the negative thaw-immunity phase.
  local thaw_immune = target:s_frozen_delay_raw() < 0
  if not refresh_gated and not thaw_immune then
    target:s_set_frozen_delay(roll)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:tree               on_animate = animate.tree_blood_on_animate
--   core:blood              on_animate = animate.tree_blood_on_animate
--   core:circle_protection  on_animate = animate.circle_protection_on_animate
--   core:glow               on_animate = animate.glow_on_animate
--   core:sprinkle           on_hit_target = animate.sprinkle_on_hit_target
return {
  tree_blood_on_animate = tree_blood_on_animate,
  circle_protection_on_animate = circle_protection_on_animate,
  glow_on_animate = glow_on_animate,
  sprinkle_on_hit_target = sprinkle_on_hit_target,
}
