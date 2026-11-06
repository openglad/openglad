-- core:#8/#9/#10 (slime trio) — split on death/ani-complete, grow specials (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- All three living descriptors share the registry name "SLIME", so the
-- numeric escapes are mandatory (api-reference: "core:#<id>").

local C = og.C

local LIVING_SLIME        = assert(og.family_id("living", "core:#8"))
local LIVING_SMALL_SLIME  = assert(og.family_id("living", "core:#9"))
local LIVING_MEDIUM_SLIME = assert(og.family_id("living", "core:#10"))

-- guy.cpp calculate_level()/calculate_exp() inlined (pure integer helpers;
-- no og.* binding exists). The C++ recomputes calculate_exp(result) from
-- scratch each loop pass as a uint32 sum over k = 2..result of
-- (8000 + 2000*(k-1) + 4000*(k-2)); accumulating incrementally below yields
-- the exact same uint32 partial sums (same additions, same order), with the
-- uint32 wrap reproduced via og.mod by 2^32. The per-term int arithmetic
-- cannot overflow in any state reachable here (experience arrives as
-- uint32-exp/2 <= 2^31-1, so the loop ends by result ~ 27000).
local function calculate_level(experience)
  local result = 1
  local acc = 0  -- calculate_exp(1) == 0
  -- rule: A slime character's level after a high-experience split is the highest level whose
  --   cumulative experience requirement (sum over k=2..L of 8000+2000(k-1)+4000(k-2), uint32) does
  --   not exceed the experience held; level 1 below 8000.
  -- tests: PackLuaSlime.a_veteran_split_halves_the_experience_across_both_bodies
  -- tests: SemSlime.a_veteran_split_levels_both_halves_on_the_experience_ladder
  while acc <= experience do
    result = result + 1
    -- og.mod by 2^32 = the C++ uint32 wrap (see above)
    acc = og.mod(acc + (8000 + 2000 * (result - 1) + 4000 * (result - 2)),
                 4294967296)
  end
  return result - 1
end

-- Big slime splits to two small slimes
local function slime_do_special(self)
  self:set_ani_type(C.ANI_SLIME_SPLIT)
  self:set_cycle(0)
  return true
end

local function slime_check_special_ai(self)
  -- living_count is the world's maintained head-count (og.living_count
  -- reads the same field the C++ read — an oblist recount would not be
  -- faithful; the counter can legitimately drift, e.g. editor map-resize).
  -- rule: A big slime bot only SPLITs while the world's head-count is under MAXOBS (room for the
  --   extra body); small and medium slimes have no AI gate (GROW is always rolled).
  -- tests: parity:ai_slime_split_scen99, FamilyBehaviors.check_special_slime_capacity
  -- tests: SemSlime.a_big_slime_never_splits_in_a_full_world
  return og.living_count() < C.MAXOBS
end

-- slime_on_death / medium_slime_on_death share this body in the C++ too:
-- the dying blob is replaced by one next-size-down offspring.
local function split_on_death(self, offspring_family)
  self:set_dead(1)
  local child = og.add_ob("living", offspring_family)
  -- plumbing: add_ob failure (object cap) on death split: the dying slime simply dies with no
  --   offspring; null guard.
  if not child then
    return true
  end
  child:set_floor(self:floor())  -- split child stays on our floor
  child.team = self.team
  child.level = self.level
  child:set_difficulty(self.level)
  child:set_foe(self:foe())
  child:set_leader(self:leader())
  -- rule: When a NAMED slime dies and splits, the dying slime takes the offspring's freshly drawn
  --   name (sic, classic direction); an unnamed slime keeps no name.
  -- tests: PackLuaSlime.a_split_carries_the_name_and_the_player_character_across
  -- tests: SemSlime.classic_a_dying_slime_ends_unnamed_named_or_not
  -- unreachable: no teeth possible: a fresh offspring is always unnamed in both games, so the
  --   unnamed arm's outcome (name stays empty) equals the copy's; the planted 'if true' stays
  --   green.
  if #self:s_name() > 0 then
    -- (sic: the DYING slime takes the offspring's fresh name — not the
    -- other way around)
    self:s_set_name(child:s_name())
  end
  -- rule: A dying slime that carries a player character hands the character to its offspring (the
  --   roster entry survives the split).
  -- tests: PackLuaSlime.a_split_carries_the_name_and_the_player_character_across
  -- tests: SemSlime.a_slime_without_a_character_leaves_an_offspring_without_one
  -- unreachable: no teeth possible: walker::move_myguy_to with no guy is a no-op, so dropping the
  --   has_guy gate changes nothing; the planted 'if true' stays green
  if self:has_guy() then
    self:move_myguy_to(child)
  end
  child:center_on(self)
  self.hp = self.max_hp
  return true
end

local function slime_on_death(self)
  return split_on_death(self, LIVING_MEDIUM_SLIME)
end

local function medium_slime_on_death(self)
  return split_on_death(self, LIVING_SMALL_SLIME)
end

local function slime_on_ani_complete(self)
  -- rule: A big slime fissions only when its SPLIT animation completes; any other animation wrap is
  --   ignored.
  -- tests: PackLuaSlime.the_split_hook_ignores_other_animations
  -- tests: PackLuaSlime.a_completed_split_animation_leaves_two_small_slimes
  -- tests: parity:special_slime_1_scen99
  if self:ani_type() ~= C.ANI_SLIME_SPLIT then
    return false
  end

  self:set_ani_type(C.ANI_WALK)
  self:set_cycle(0)
  -- First, shrink (and move) normal guy ..
  self:transform_to("living", LIVING_SMALL_SLIME)
  -- diagonal 'down left' of normal
  self:setxy(self:xpos() - 10, self:ypos() + 10)

  -- Create a new small slime ..
  local child = og.add_ob("living", LIVING_SMALL_SLIME)
  -- plumbing: add_ob failure on split: parent already transformed into a small slime, no sibling;
  --   object-cap guard.
  if not child then
    return true
  end
  child:set_floor(self:floor())  -- split child stays on our floor
  child:setxy(self:xpos() + 12, self:ypos() - 12)
  -- Transfer stats/etc. across to new guy ..
  self:transfer_stats(child)
  local exp_needed = 1000 * self.level  -- uint32 cast as above
  -- plumbing: exp_needed = 1000*level is negative only for level < 0, unreachable (level >= 1
  --   engine invariant); uint32-cast mirror.
  if exp_needed < 0 then
    exp_needed = exp_needed + 4294967296
  end
  -- rule: A hired slime that splits can only sustain a second character if it holds at least 1000 x
  --   level experience: below that the sibling is stripped to a plain monster named SLIME at level
  --   calculate_level(exp/2); at or above it both halves keep a character, each levelled to
  --   calculate_level(exp/2) holding exp/2.
  -- tests: PackLuaSlime.a_low_experience_split_strips_the_offspring_to_a_monster
  -- tests: PackLuaSlime.a_veteran_split_halves_the_experience_across_both_bodies
  -- tests: SemSlime.a_monster_slime_split_copies_its_level_and_makes_no_character
  -- tests: SemSlime.a_slime_split_at_exactly_one_thousand_per_level_keeps_both_characters
  if child:has_guy() and child:g_exp() < exp_needed then
    -- The second character cannot be "sustained" at this experience.
    child:clear_myguy()
    child:s_set_name("SLIME")
    child.level = calculate_level(self:g_exp() // 2)
  elseif child:has_guy() then
    -- High-exp split: both halves level to calculate_level(exp/2).
    -- g_upgrade_to_level re-dispatches the guy's family level_up hook
    -- internally (nested VM entry; budget arms outermost-only).
    local exp = self:g_exp() // 2
    -- og.i16: the C++ level destination is a short
    local new_level = og.i16(calculate_level(exp))
    self:g_upgrade_to_level(new_level)
    self:g_update_derived_stats(self)
    self:g_set_exp(exp)
    child:g_upgrade_to_level(new_level)
    child:g_update_derived_stats(child)
    child:g_set_exp(exp)
  end

  child.team = self.team
  child:set_foe(self:foe())
  child:set_leader(self:leader())
  return true
end

-- small_slime_do_special / medium_slime_do_special share this body in the
-- C++ too: grow into the next size up when there is room.
local function grow_into(self, grown_family)
  -- room to grow?
  -- rule: A small or medium slime GROWs into the next size only when all eight neighbouring probe
  --   boxes are free; otherwise the cast fails with NO ROOM TO GROW and the slime wanders 10 steps
  --   in a random direction (two og.rand(3) draws, y first).
  -- tests: parity:special_small_slime_1_scen99, parity:slime_grow_blocked_scen99
  if self:spaces_clear() > 7 then
    self:transform_to("living", grown_family)
  else
    -- FLAGGED RNG-order record: the C++ passes TWO rng_.next(3)
    -- draws as arguments of one set_command() call, so the draw order is
    -- unspecified. Parity adjudicated RIGHT-first at THIS site (the y-draw
    -- happens before the x-draw): left-first diverged
    -- special_slime_1_scen99 at ~tick 83 (post-split AI GROW attempts),
    -- right-first matches the golden — unlike every previously adjudicated
    -- two-rng site, which matched left-first. og.rand(3) has a constant
    -- n > 0, so no n<=0 guard.
    local dy = og.rand(3) - 1
    local dx = og.rand(3) - 1
    self:s_set_command(C.COMMAND_WALK, 10, dx, dy)
    return false, "NO ROOM TO GROW"
  end
  return true
end

local function small_slime_do_special(self)
  return grow_into(self, LIVING_MEDIUM_SLIME)
end

local function medium_slime_do_special(self)
  return grow_into(self, LIVING_SLIME)
end

og.family("living", {   -- SLIME
  id = "core:#8",
  wire_id = 8,
  name = "SLIME",
  short_name = og.NIL,
  stats  = { strength = 18, dexterity = 2, constitution = 18,
             intelligence = 7, armor = 6, level = 1 },
  combat = { hp = 150, melee_damage = 28, stepsize = 3,
             fire_delay = 11, fire_mp_cost = 0 },
  costs  = { hire = 700,
             train = { strength = 20, dexterity = 20, constitution = 8,
                       intelligence = 14, armor = 50, level = 200 } },
  specials = {
    { id = "split", name = "SPLIT", mp_cost = 30, detail = "Divide into two smaller slimes.", icon = "icons/split.png" },
    default_cast = slime_do_special,
  },
  default_weapon = "core:blob",
  flags = { "ANIMATE" },
  init_ani_type = 0,
  init_max_magicpoints = 50,
  leaves_bloodspot = true,
  magic_damage_modifier = 2,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SLIME DESTROYED",
  sprite = "amoeba3.png",
  animation = "slime",
  ai_line_of_sight = 4,
  description = "Slimes are patches of ooze which grow and split into two smaller slimes, overwhelming the enemy. Their nebulous nature makes them more susceptible to magic.",
  names = { "Grimer", "Goop", "Slurp", "Glopp", "Sludge", "Blob" },
  playable = false,
  playable_order = 999,
  glyph = "J",
  glyph_ascii = "J",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  check_special_ai = slime_check_special_ai,
  on_death = slime_on_death,
  on_ani_complete = slime_on_ani_complete,
})

og.family("living", {   -- SMALL_SLIME
  id = "core:#9",
  wire_id = 9,
  name = "SLIME",
  short_name = og.NIL,
  stats  = { strength = 18, dexterity = 2, constitution = 18,
             intelligence = 7, armor = 6, level = 1 },
  combat = { hp = 80, melee_damage = 12, stepsize = 2,
             fire_delay = 12, fire_mp_cost = 0 },
  costs  = { hire = 700,
             train = { strength = 20, dexterity = 20, constitution = 8,
                       intelligence = 14, armor = 50, level = 200 } },
  specials = {
    { id = "grow", name = "GROW", mp_cost = 30, detail = "Grow into a bigger slime.", icon = "icons/grow.png" },
    default_cast = small_slime_do_special,
  },
  default_weapon = "core:blob",
  flags = { "ANIMATE", "NO_RANGED" },
  init_ani_type = 0,
  init_max_magicpoints = 50,
  leaves_bloodspot = true,
  magic_damage_modifier = 2,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SLIME DESTROYED",
  sprite = "s_slime.png",
  animation = "small_slime",
  ai_line_of_sight = 2,
  description = "Slimes are patches of ooze which grow and split into two smaller slimes, overwhelming the enemy. Their nebulous nature makes them more susceptible to magic.",
  names = { "Grimer", "Goop", "Slurp", "Glopp", "Sludge", "Blob" },
  playable = true,
  playable_order = 12,
  glyph = "i",
  glyph_ascii = "i",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})

og.family("living", {   -- MEDIUM_SLIME
  id = "core:#10",
  wire_id = 10,
  name = "SLIME",
  short_name = og.NIL,
  stats  = { strength = 18, dexterity = 2, constitution = 18,
             intelligence = 7, armor = 6, level = 1 },
  combat = { hp = 110, melee_damage = 20, stepsize = 2,
             fire_delay = 10, fire_mp_cost = 0 },
  costs  = { hire = 700,
             train = { strength = 20, dexterity = 20, constitution = 8,
                       intelligence = 14, armor = 50, level = 200 } },
  specials = {
    { id = "grow", name = "GROW", mp_cost = 30, detail = "Grow into a bigger slime.", icon = "icons/grow.png" },
    default_cast = medium_slime_do_special,
  },
  default_weapon = "core:blob",
  flags = { "ANIMATE" },
  init_ani_type = 0,
  init_max_magicpoints = 50,
  leaves_bloodspot = true,
  magic_damage_modifier = 2,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SLIME DESTROYED",
  sprite = "m_slime.png",
  animation = "small_slime",
  ai_line_of_sight = 3,
  description = "Slimes are patches of ooze which grow and split into two smaller slimes, overwhelming the enemy. Their nebulous nature makes them more susceptible to magic.",
  names = { "Grimer", "Goop", "Slurp", "Glopp", "Sludge", "Blob" },
  playable = false,
  playable_order = 999,
  glyph = "j",
  glyph_ascii = "j",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = medium_slime_on_death,
})
