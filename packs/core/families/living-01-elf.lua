-- core:elf — rock-spread specials (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local gf = og.use("gameplay_fixes")
local G = og.use("gates")

-- Jonathan Dearborn added this spread in 2013 so stacked rocks separate
-- visibly in flight.
-- The original jitter came from libc rand, separate from gameplay RNG.
-- og.cosmetic_rand selects that compatibility stream when installed and
-- otherwise uses the deterministic gameplay stream.
local function next_spread_multiplier()
  local spread_roll = og.cosmetic_rand(101)
  -- 0.8 + 0.4*roll/100 is a float chain: per-op rounding.
  return og.fadd(0.8, og.fdiv(og.fmul(0.4, spread_roll), 100.0))
end

local function some_rocks(self)
  -- rule: Gameplay Fixes AC: when the rock's launch pad is refused by terrain (wall face, map edge;
  --   not a tree crown, not water) the volley is refused 'COULD NOT FIRE' before any refund or
  --   swing: it costs nothing. Classic: the cast refunds 2 weapon costs, fire() fails and the one
  --   swing is still paid. A body on the pad is not 'terrain' and keeps the paid swing (fixes H/I).
  --   Same rule as below.
  -- tests: GameplayFixesElf.a_volley_at_a_wall_spends_nothing_with_the_setting_on
  -- tests: GameplayFixesElf.a_volley_at_the_map_edge_spends_nothing
  -- tests: GameplayFixesElf.a_tree_crown_on_the_pad_lets_the_rocks_fly
  -- tests: parity:elf_rocks_blocked_scen99
  -- tests: SemElf.a_volley_at_a_wall_leaves_the_elf_rested_with_the_setting_on
  if gf.pad_blocked_by_terrain(self) then
    return false, "COULD NOT FIRE"
  end
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fadd(self.magicpoints, 2 * self:s_weapon_cost())
  local rock = self:fire()
  -- rule: ROCKS pre-refunds two weapon costs so its two rocks are free beyond the slot price; if
  --   the first rock cannot be fired the cast fails 'COULD NOT FIRE'. Classic keeps the 2-rock
  --   refund even though none flew (net MP gain over the swing); Gameplay Fixes takes both back.
  -- tests: GameplayFixesElf.a_blocked_volley_costs_one_swing
  -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired, parity:elf_rocks_pair_scen99
  if not rock then
    -- rule: ROCKS pre-refunds two weapon costs so its two rocks are free beyond the slot price; if
    --   the first rock cannot be fired the cast fails 'COULD NOT FIRE'.
    -- tests: GameplayFixesElf.a_blocked_volley_costs_one_swing
    -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired, parity:elf_rocks_pair_scen99
    if gf.on() then
      -- Gameplay Fixes: the two rocks that never flew are not refunded.
      gf.take_back(self, 2)
    end
    return false, "COULD NOT FIRE"
  end
  -- lastx/lasty are C++ floats: per-op rounding.
  rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
  rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
  rock = self:fire()
  -- rule: If the first rock flew but the second cannot, ROCKS reports 'COULD NOT FIRE' (one rock in
  --   the air). Classic keeps the refund for the second rock; Gameplay Fixes takes one weapon cost
  --   back.
  -- tests: GameplayFixesElf.rocks_take_back_the_second_rock_that_never_flew
  -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired, parity:elf_rocks_pair_scen99
  if not rock then
    -- rule: If the first rock flew but the second cannot, ROCKS reports 'COULD NOT FIRE' (one rock
    --   in the air).
    -- tests: GameplayFixesElf.rocks_take_back_the_second_rock_that_never_flew
    -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired, parity:elf_rocks_pair_scen99
    if gf.on() then
      -- Gameplay Fixes: the second rock never flew.
      gf.take_back(self, 1)
    end
    return false, "COULD NOT FIRE"
  end
  -- lastx/lasty are C++ floats: per-op rounding.
  rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
  rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
  return true
end

-- The three bouncing volleys share one immutable shape. los_num spells each
-- old line-of-sight multiplier as `* los_num // 2`; case 3's `* 2` is
-- therefore `* 4 // 2`.
local function bounce_volley(cost_mult, count, los_num)
  return function(self)
    -- rule: Same rule as above for the bouncing volleys: a terrain-blocked pad refuses free with the
    --   setting on.
    -- tests: GameplayFixesElf.a_volley_at_a_wall_spends_nothing_with_the_setting_on
    -- tests: GameplayFixesElf.a_volley_at_the_map_edge_spends_nothing
    -- tests: parity:elf_rocks_blocked_fixes_scen99
    if gf.pad_blocked_by_terrain(self) then
      return false, "COULD NOT FIRE"
    end
    local refund = cost_mult
    -- rule: Classic bouncing volleys refund cost_mult weapon costs (3/4/5) while firing only count
    --   rocks (2/3/4): each volley over-refunds one rock (net MP gain). Gameplay Fixes refunds
    --   exactly the rocks that fly.
    -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired
    -- tests: parity:elf_volley_refund_scen99
    if gf.on() then
      -- Gameplay Fixes: refund the rocks that will fly, not one more.
      refund = count
    end
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fadd(self.magicpoints,
                               refund * self:s_weapon_cost())
    -- rule: A bouncing volley releases `count` rocks (2/3/4 for slots 2/3/4 and any unmapped slot),
    --   each with do_bounce set and line of sight multiplied by los_num/2 (x1.5, x2, x2.5), each
    --   with an independent 0.8-1.2 cosmetic speed spread.
    -- tests: WalkerSpecials.elf_bouncing_rocks_drives_simulation
    -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired
    -- tests: SemElf.bouncing_volleys_throw_bouncing_rocks_with_the_slots_longer_sight
    for i = 1, count do
      local rock = self:fire()
      -- rule: When a volley's i-th rock cannot be fired the cast stops 'COULD NOT FIRE' with the
      --   earlier rocks still flying. Classic keeps the whole refund; Gameplay Fixes takes back the
      --   count-i+1 rocks that never flew.
      -- tests: GameplayFixesElf.a_blocked_volley_costs_one_swing
      -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired
      -- tests: SemElf.a_volley_that_runs_dry_takes_back_only_the_rocks_that_never_flew
      if not rock then
        -- rule: When a volley's i-th rock cannot be fired the cast stops 'COULD NOT FIRE' with the
        --   earlier rocks still flying.
        -- tests: GameplayFixesElf.a_blocked_volley_costs_one_swing
        -- tests: GameplayFixesElf.volleys_refund_exactly_the_rocks_fired
        -- tests: SemElf.a_volley_that_runs_dry_takes_back_only_the_rocks_that_never_flew
        if gf.on() then
          -- Gameplay Fixes: the rocks that never flew are not refunded.
          gf.take_back(self, count - i + 1)
        end
        return false, "COULD NOT FIRE"
      end
      rock:set_lineofsight(rock:lineofsight() * los_num // 2)
      rock:set_do_bounce(1)
      -- lastx/lasty are C++ floats: per-op rounding.
      rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
      rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
    end
    return true
  end
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 6, 9, 6, 8, 1)
end

-- Gameplay Fixes: the classic elf had no gate and threw at nothing. With
-- the setting on a volley wants the elf's nearest living foe faced (the
-- rocks leave along the elf's heading) and inside the rocks' flight (the
-- rock is step 5, range 8 in the weapon table; the bouncing volleys
-- multiply the range by los_num / 2, as bounce_volley does), and the two
-- bigger volleys want a second living foe in flight anywhere around.
-- Draw-free: finders and positions only.
local function volley(self, count_needed, los_num)
  local foe = G.nearest_living_foe(self, 320, false)
  -- rule: Gameplay Fixes bot elf: a volley needs its nearest living foe (within 320 px) faced along
  --   the elf's heading and inside the rock's flight (step 5 x range 8, multiplied by los_num/2 for
  --   bouncing volleys).
  -- tests: GameplayFixesBots.elf_rocks_want_the_nearest_foe_faced_inside_the_flight
  -- tests: GameplayFixesBots.elf_bouncing_rocks_reach_half_again
  -- tests: parity:elf_bot_rocks_far_fixes_scen99
  -- tests: SemElf.the_elf_bot_gate_with_no_living_foe_never_throws
  if not foe or not G.faces(self, foe)
      or not G.in_flight(self, foe, 5, 8, los_num, 2) then
    return false
  end
  -- rule: ROCKS and BOUNCING ROCKS need only the one faced foe; LOTS OF ROCKS and MEGA ROCKS also
  --   need a second living foe.
  -- tests: GameplayFixesBots.elf_rocks_want_the_nearest_foe_faced_inside_the_flight
  -- tests: GameplayFixesBots.elf_lots_of_rocks_wants_two_foes_in_flight
  if count_needed < 2 then
    return true
  end
  local foes = G.living_foes(self, 320, false)
  local n = 0
  -- rule: LOTS OF ROCKS / MEGA ROCKS (bot, fixes on) want at least two living foes inside the
  --   volley's flight from the elf, in any direction (only the nearest must be faced).
  -- tests: GameplayFixesBots.elf_lots_of_rocks_wants_two_foes_in_flight
  -- tests: GameplayFixesBots.elf_mega_rocks_wants_two_foes_in_flight
  for i = 1, #foes do
    -- rule: LOTS OF ROCKS / MEGA ROCKS (bot, fixes on) want at least two living foes inside the
    --   volley's flight from the elf, in any direction (only the nearest must be faced).
    -- tests: GameplayFixesBots.elf_lots_of_rocks_wants_two_foes_in_flight
    -- tests: GameplayFixesBots.elf_mega_rocks_wants_two_foes_in_flight
    if G.in_flight(self, foes[i], 5, 8, los_num, 2) then
      n = n + 1
    end
  end
  return n >= count_needed
end

-- slot: { foes needed, los_num }; slot 4 is also every unmapped slot, as
-- default_cast is.
local VOLLEYS = { { 1, 2 }, { 1, 3 }, { 2, 4 }, { 2, 5 } }

local function check_special_ai(self)
  -- rule: Classic elf bot has no special gate: it casts whichever slot it rolled at any time, even
  --   with no foe (throws at nothing). Gameplay Fixes adds the pad and volley gates.
  -- tests: GameplayFixesBots.elf_rocks_want_the_nearest_foe_faced_inside_the_flight
  -- tests: parity:elf_bot_rocks_far_scen99
  if not gf.on() then
    return true
  end
  -- rule: Each elf slot maps to its volley shape (foes needed, range multiplier): 1:(1,x1)
  --   2:(1,x1.5) 3:(2,x2) 4 and any unmapped slot:(2,x2.5), matching default_cast.
  -- tests: GameplayFixesBots.elf_bouncing_rocks_reach_half_again
  -- tests: GameplayFixesBots.elf_mega_rocks_wants_two_foes_in_flight
  -- tests: SemElf.an_unmapped_elf_slot_aims_like_mega_rocks
  local v = VOLLEYS[self:current_special()] or VOLLEYS[4]
  -- rule: Gameplay Fixes bot elf never casts when its rock's launch pad is blocked (wall, edge, or
  --   a foe body on the pad) even with a foe in flight; an ally on the pad does not block.
  -- tests: GameplayFixesBots.elf_rocks_refuse_a_pad_on_a_wall
  -- tests: GameplayFixesBots.elf_rocks_refuse_a_pad_off_the_map
  -- tests: GameplayFixesBots.elf_rocks_refuse_a_foe_on_the_pad
  -- tests: GameplayFixesBots.elf_rocks_an_ally_on_the_pad_is_not_in_the_way
  -- tests: parity:elf_bot_rocks_blocked_fixes_scen99
  return G.pad_clear(self) and volley(self, v[1], v[2])
end

og.family("living", {
  id = "core:elf",
  wire_id = 1,
  name = "ELF",
  short_name = og.NIL,
  stats  = { strength = 5, dexterity = 14, constitution = 5,
             intelligence = 12, armor = 8, level = 1 },
  combat = { hp = 75, melee_damage = 12, stepsize = 4,
             fire_delay = 5, fire_mp_cost = 1 },
  costs  = { hire = 150,
             train = { strength = 25, dexterity = 6, constitution = 12,
                       intelligence = 8, armor = 50, level = 200 } },
  specials = {
    -- some rocks (normal)
    { id = "rocks",          name = "ROCKS",          mp_cost = 10, cast = some_rocks, detail = "Rocks hurls a few rocks at the enemy. Forestwalk, dexterity-based, lets you move in trees.", icon = "icons/rocks.png" },
    -- more rocks, and bouncing
    -- we get 50% longer, too!
    { id = "bouncing_rocks", name = "BOUNCING ROCKS", mp_cost = 20, cast = bounce_volley(3, 2, 3), detail = "Like #1, but these rocks bounce off walls and other barricades.", icon = "icons/bouncing_rocks.png" },
    -- get double distance
    { id = "lots_of_rocks",  name = "LOTS OF ROCKS",  mp_cost = 30, cast = bounce_volley(4, 3, 4), detail = "Like #2, but more rocks, with a longer thrown range.", icon = "icons/lots_of_rocks.png" },
    { id = "mega_rocks",     name = "MEGA ROCKS",     mp_cost = 40, detail = "This giant handful of rocks bounces far away and packs a big punch.", icon = "icons/mega_rocks.png" },
    -- we get 150% longer, too!
    default_cast = bounce_volley(5, 4, 5),  -- case 4 and every unmapped slot
  },
  default_weapon = "core:rock",
  flags = { "FORESTWALK" },
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "ELF KILLED",
  sprite = "elf.png",
  animation = "standard",
  ai_line_of_sight = 8,
  description = "Elves are small and weak, but are harder to hit than most classes. Alone of all the classes, elves possess the 'ForestWalk' ability.",
  names = { "Legolas", "Took", "Elrond", "Tanis", "Acorn", "Lightfoot",
            "Treewee" },
  playable = true,
  playable_order = 2,
  glyph = "e",
  glyph_ascii = "e",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  check_special_ai = check_special_ai,
  level_up = level_up,
})
