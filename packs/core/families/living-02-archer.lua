-- core:archer — arrow volleys, flurry, exploding shot; melee backpedal (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local ai = og.use("ai")
local G, gf = og.use("gates"), og.use("gameplay_fixes")
local WEAPON_FIRE_ARROW = assert(og.family_id("weapon", "core:fire_arrow"))

local function fire_arrows(self)
  self:set_curdir(-1)
  self:set_lastx(0)
  self:set_lasty(0)
  -- magicpoints is a C++ float: per-op rounding
  self.magicpoints = og.fadd(self.magicpoints, 8 * self:s_weapon_cost())
  self:s_add_command(C.COMMAND_SET_WEAPON, 1, WEAPON_FIRE_ARROW, 0)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, 0, -1)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, 1, -1)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, 1, 0)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, 1, 1)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, 0, 1)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, -1, 1)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, -1, 0)
  self:s_add_command(C.COMMAND_QUICK_FIRE, 1, -1, -1)
  self:s_add_command(C.COMMAND_RESET_WEAPON, 1, 0, 0)
  return true
end

local function flurry(self)
  -- rule: BARRAGE is refused 'SPECIAL BUSY' while busy (classic busy ~= 0, fixed busy > 0; fix S).
  --   On success it refunds 3 weapon costs, fires three arrows along the heading and adds 2 x
  --   fire_frequency busy. Same rule as living-00-soldier.lua.
  -- tests: GameplayFixesSpecialBusy.archer_specials_after_a_shot
  -- tests: WalkerSpecials.archer_barrage_drives_simulation
  -- tests: SemArcher.a_barrage_costs_exactly_the_slots_price
  if gf.special_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- magicpoints is a C++ float: per-op rounding
  self.magicpoints = og.fadd(self.magicpoints, 3 * self:s_weapon_cost())
  self:fire()
  self:fire()
  self:fire()
  -- busy and fire_frequency are C++ floats: per-op rounding
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 2.0)))
  return true
end

local function exploding_shot(self)
  -- rule: EXPLODING BOLT refused 'SPECIAL BUSY' while busy; classic ~= 0, fixed > 0 (fix S). Same
  --   rule as above.
  -- tests: GameplayFixesSpecialBusy.archer_specials_after_a_shot
  -- tests: GameplayFixesSpecialBusy.positive_and_zero_busy_are_unchanged
  if gf.special_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: Gameplay Fixes AC (#476): an EXPLODING BOLT whose fire arrow's pad is refused by terrain
  --   (a wall face, the map edge, a crown the fire arrow cannot cross; never water) refuses 'COULD
  --   NOT FIRE' before fire(): no 1 MP shot, no waver, the bow untouched. Classic: fire() pays and
  --   draws, then refuses. A body on the pad keeps the paid blow.
  -- tests: SemArcher.an_exploding_bolt_at_terrain_spends_nothing_with_the_setting_on
  -- tests: parity:archer_bolt_edge_pad_fixes_scen99
  if gf.pad_blocked_by_terrain(self, WEAPON_FIRE_ARROW) then
    return false, "COULD NOT FIRE"
  end
  local t = og.tuning(self)
  local old_weapon = self:current_weapon()
  self:set_current_weapon(WEAPON_FIRE_ARROW)
  local arrow = self:fire()
  self:set_current_weapon(old_weapon)
  -- rule: The exploding bolt is one fire arrow with skip_exit=5000 (the 'explodes on contact'
  --   mark), hp 500 and damage x2; if the fire arrow cannot be fired the cast fails 'COULD NOT
  --   FIRE' and the archer keeps its ordinary weapon either way.
  -- tests: WalkerSpecials.archer_exploding_bolt_drives_simulation
  -- tests: GameplayFixesBots.archer_exploding_bolt_asks_the_fire_arrows_own_pad
  -- tests: SemArcher.an_exploding_bolt_is_a_buffed_fire_arrow_and_the_archer_keeps_its_bow
  if not arrow then
    return false, "COULD NOT FIRE"
  end
  -- used as a dummy variable to
  -- signify exploding .. :(
  arrow:set_skip_exit(5000)
  -- Keep the old "buffed arrow" hitpoint pool.
  arrow.hp = t.exploding_shot_hp
  -- damage is a C++ float: per-op rounding (the multiplier is a tuning
  -- float, 2.0)
  arrow:set_damage(og.fmul(arrow:damage(), t.exploding_shot_damage_mult))
  return true
end

-- self is the stats' owner (stats->controller() in the C++ signature).
local function hit_response(self, foe)
  -- rule: An archer struck by a new attacker (or with no foe) switches its foe to the attacker,
  --   clears its command queue and resets its pursuit distances to 15000; a repeat hit from its
  --   current foe keeps its orders.
  -- tests: StatsHitResponseMore.statistics_hit_response_archer_runs_away_and_queues_walk
  -- tests: SemArcher.a_new_attacker_clears_the_archers_orders_and_a_repeat_hit_keeps_them
  if not self:foe() or self:foe() ~= foe then
    self:set_foe(foe)
    self:s_clear_command()
    self:s_set_current_distance(15000)
    self:s_set_last_distance(15000)
  end
  local distance = self:distance_to_ob(foe)
  -- rule: An archer hit from inside 64 px backpedals: a forced 8-step WALK directly away from the
  --   attacker (sign of archer-attacker per axis, int16-narrowed).
  -- tests: StatsHitResponseMore.statistics_hit_response_archer_runs_away_and_queues_walk
  -- tests: parity:archer_hit_response_backpedal_scen99
  if distance < og.tuning(self).melee_backpedal_range then
    -- og.i16: the C++ stores each delta into a short before the sign divide
    local dx = og.sign(og.i16(self:xpos() - foe:xpos()))
    local dy = og.sign(og.i16(self:ypos() - foe:ypos()))
    self:s_force_command(C.COMMAND_WALK, 8, dx, dy)
  end
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 11.0, 12.0, 4.0, 1.0)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 9, 8, 8, 1)
end

-- Gameplay Fixes: the classic gate (a foe within 130, about 6 squares) was
-- asked of every slot. It is still asked first (it acquires the foe, and
-- that draws); with the setting on each slot then asks for its own payoff,
-- measured by the fire arrow's flight (step 8, range 12 in the weapon
-- table) along the archer's heading. FIRE ARROWS' ring pays for two foes
-- in its flight, or one the archer does not face (one faced foe is what an
-- ordinary arrow hits). BARRAGE's three arrows fly along one line, so they
-- want the archer's foe alive, faced and in flight. EXPLODING BOLT wants the
-- same, no archer or ally of its own in the blast, and the fire arrow's pad clear.
local function fixed(self)
  local slot = self:current_special()
  -- rule: Gameplay Fixes bot FIRE ARROWS (the 8-way ring) pays for two living foes inside the fire
  --   arrow's flight (step 8 x range 12) or one the archer does not face (one faced foe is what an
  --   ordinary arrow would hit). The classic gate (foe within 130) still runs first for its draws
  --   and foe assignment, but its answer is ignored.
  -- tests: GameplayFixesBots.archer_fire_arrows_want_a_crowd_or_a_foe_behind
  -- tests: parity:archer_bot_ring_one_foe_fixes_scen99
  -- tests: SemArcher.the_archer_ring_gate_with_no_foe_never_fires
  if slot == 1 then
    return G.ring_pays(self, 8, 12)
  end
  -- BARRAGE and EXPLODING BOLT refuse a busy archer (the casts' guard).
  -- rule: Gameplay Fixes bot archer never picks BARRAGE or EXPLODING BOLT while busy (the casts
  --   would refuse SPECIAL BUSY).
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  if gf.special_busy(self) then
    return false
  end
  local f = self:foe()
  -- rule: Gameplay Fixes bot BARRAGE and EXPLODING BOLT want the archer's own current foe alive, a
  --   living-order body, faced along the heading, and inside the fire arrow's flight (8 x 12).
  -- tests: GameplayFixesBots.archer_barrage_wants_its_foe_faced_in_flight
  -- tests: GameplayFixesBots.archer_exploding_bolt_wants_a_foe_past_its_own_blast_with_no_ally_in_it
  -- tests: SemArcher.the_archer_line_gates_want_a_living_current_foe
  if not f or f:dead() ~= 0 then
    return false
  end
  if not G.living(f) or not G.faces(self, f) then
    return false
  end
  if not G.in_flight(self, f, 8, 12) then
    return false
  end
  -- rule: BARRAGE needs nothing beyond the faced foe in flight.
  -- tests: GameplayFixesBots.archer_barrage_wants_its_foe_faced_in_flight
  -- tests: GameplayFixesBots.archer_exploding_bolt_wants_a_foe_past_its_own_blast_with_no_ally_in_it
  if slot == 2 then
    return true
  end
  -- rule: Gameplay Fixes bot EXPLODING BOLT also wants no archer (check_self) and no ally inside
  --   the blast around its foe, and the FIRE ARROW's own launch pad clear.
  -- tests: GameplayFixesBots.archer_exploding_bolt_wants_a_foe_past_its_own_blast_with_no_ally_in_it
  -- tests: GameplayFixesBots.archer_exploding_bolt_asks_the_fire_arrows_own_pad
  -- tests: parity:archer_bot_bolt_at_blows_fixes_scen99
  return G.blast_clear(self, f, G.blast_reach(self.level), true) and G.pad_clear(self, WEAPON_FIRE_ARROW)
end

og.family("living", {
  id = "core:archer",
  wire_id = 2,
  name = "ARCHER",
  short_name = og.NIL,
  stats  = { strength = 6, dexterity = 12, constitution = 6,
             intelligence = 10, armor = 5, level = 1 },
  combat = { hp = 90, melee_damage = 8, stepsize = 4,
             fire_delay = 5, fire_mp_cost = 1 },
  costs  = { hire = 350,
             train = { strength = 15, dexterity = 6, constitution = 9,
                       intelligence = 10, armor = 50, level = 200 } },
  specials = {
    { id = "fire_arrows",    name = "FIRE ARROWS",    mp_cost = 20, cast = fire_arrows, detail = "An archer can spin in a circle, firing off a ring of flaming bolts.", icon = "icons/fire_arrows.png" },
    { id = "barrage",        name = "BARRAGE",        mp_cost = 60, cast = flurry, detail = "Rather than a single bolt, the archer sends 3 deadly bolts ahead.", icon = "icons/barrage.png" },
    { id = "exploding_bolt", name = "EXPLODING BOLT", mp_cost = 70, detail = "This fatal bolt will explode on contact, dealing death to all.", icon = "icons/exploding_bolt.png" },
    default_cast = exploding_shot,  -- slot 3 and every unmapped slot (slot 4 is undeclared)
  },
  default_weapon = "core:arrow",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "ARCHER DIED",
  sprite = "archer.png",
  animation = "standard",
  ai_line_of_sight = 12,
  description = "Archers are fleet of foot, and their arrows have a long range. Although they're not as strong as other fighters, they can be a good squad backbone.",
  names = { "Robin", "Green Arrow", "Legolas", "Yeoman", "Strider",
            "Longshot", "Bowyer", "Hunter", "Archy" },
  playable = true,
  playable_order = 3,
  glyph = "a",
  glyph_ascii = "a",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Exploding shot: arrow durability and damage multiplier
    exploding_shot_hp = 500,
    exploding_shot_damage_mult = 2.0,
    -- Melee backpedal trigger distance (px)
    melee_backpedal_range = 64,
  },

  -- about 6 squares
  -- rule: Classic archer bot casts any slot when its acquired/found foe is within 130 px (about 6
  --   squares). With the setting on the classic search still runs (draws, foe assignment) but
  --   fixed() decides and ignores its answer.
  -- tests: GameplayFixesBots.archer_fire_arrows_want_a_crowd_or_a_foe_behind
  -- tests: GameplayFixesBots.archer_exploding_bolt_wants_a_foe_past_its_own_blast_with_no_ally_in_it
  -- tests: parity:archer_bot_ring_one_foe_scen99, parity:archer_bot_bolt_at_blows_scen99
  check_special_ai = G.with_classic(ai.foe_within(130), fixed),  -- fixed per-tick AI gate
  hit_response = hit_response,
  set_difficulty = set_difficulty,
  level_up = level_up,
})
