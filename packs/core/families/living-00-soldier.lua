-- core:soldier — charge, boomerang, whirlwind, disarm (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local ai = og.use("ai")
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local FX_BOOMERANG = assert(og.family_id("fx", "core:boomerang"))

local function charge(self)
  -- rule: CHARGE refuses (free, 'PATH BLOCKED') when the square ahead along the soldier's heading
  --   is blocked by a wall, an ally or a foe; otherwise it queues one COMMAND_RUSH (3 acts, step
  --   lastx/stepsize, lasty/stepsize) and plays SOUND_CHARGE. Classic (setting off) asks the LOUD
  --   forward probe: probing a foe in front swings at it, eats a treasure or opens a door as a side
  --   effect even though the charge is refused; with Gameplay Fixes on the probe is quiet (no draw,
  --   no swing, no eat, no door).
  -- tests: WalkerSpecials.soldier_charge_drives_simulation, FamilyBehaviors.special_soldier_charge
  -- tests: GameplayFixesSoldier.reporter_charge_refused_by_a_foe_in_front_swings_only_with_the_setting_off
  -- tests: GameplayFixesSoldier.a_wall_in_front_was_always_free_and_an_ally_is_left_alone
  -- tests: GameplayFixesSoldier.the_probe_eats_nothing_and_opens_no_door_with_the_setting_on
  -- tests: parity:soldier_charge_front_foe_scen99
  -- tests: SemSoldier.a_clear_charge_queues_its_rush_and_plays_the_charge_sound
  if self:s_forward_blocked(gf.on()) then
    return false, "PATH BLOCKED"
  end
  -- lastx/lasty are C++ floats: one fdiv then one C trunc, per axis
  self:s_add_command(C.COMMAND_RUSH, 3,
    og.trunc(og.fdiv(self:lastx(), self:stepsize())),
    og.trunc(og.fdiv(self:lasty(), self:stepsize())))
  og.emit_sound(C.SOUND_CHARGE)
  return true
end

local function throw_boomerang(self)
  local t = og.tuning(self)
  -- og.summon_configured applies these keys in exactly the legacy order:
  -- ani_type, lifetime, hp_add, max_hp_from_hp, damage_add
  local boomerang = og.summon_configured(self, "fx", FX_BOOMERANG, {
    -- The old code used ani_type = 1 only as a dummy non-zero value.
    ani_type = 1,
    lifetime = t.boomerang_lifetime_base
      + self.level * t.boomerang_lifetime_per_level,
    hp_add = self.level * t.boomerang_hp_per_level,
    max_hp_from_hp = true,
    damage_add = self.level * t.boomerang_damage_per_level,
  })
  -- plumbing: og.summon_configured returned nil (object cap / allocation failure): 'COULD NOT MAKE
  --   BOOMERANG'.
  if not boomerang then
    return false, "COULD NOT MAKE BOOMERANG"
  end
  return true
end

local function whirlwind(self)
  -- can't do while attacking, etc.
  -- rule: WHIRLWIND is refused 'SPECIAL BUSY' (free) while the soldier is busy. Classic: busy ~= 0,
  --   so a hero whose busy rests a fraction below zero after a fractional-delay shot is refused
  --   forever; Gameplay Fixes (fix S): only busy > 0 refuses. Same rule as below
  --   (DISARM) and living-02-archer.lua/43.
  -- tests: GameplayFixesSpecialBusy.whirlwind_after_a_shot
  -- tests: FamilyBehaviors.soldier_batch3_special_ai_and_fire_callback_paths
  -- tests: SemSoldier.a_busy_soldier_is_refused_whirlwind_for_free
  -- tests: SemSoldier.a_whirlwind_after_a_shot_spins_with_the_setting_on
  if gf.special_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local foes = og.foes_in_range(
    self, t.whirlwind_range_base + self.level * t.whirlwind_range_per_level)
  -- rule: With Gameplay Fixes on a WHIRLWIND with no foe inside its ring (32+2*level px,
  --   og.foes_in_range) is refused free ('NO FOE IN RANGE'); classic spins anyway and pays 120 MP,
  --   8 busy and the 8-walk spin for nothing.
  -- tests: GameplayFixesSoldier.whirlwind_refuses_with_no_foe_when_on
  -- tests: WalkerSpecials.soldier_whirlwind_drives_simulation
  -- tests: GameplayFixesSoldier.whirlwind_with_a_foe_spins_on_both_values
  -- tests: parity:soldier_whirlwind_no_foe_scen99
  -- tests: SemSoldier.a_classic_whirlwind_with_no_foe_spins_through_the_eight_compass_points
  if gf.on() and #foes == 0 then
    -- Gameplay Fixes: a spin that would hit nobody costs nothing.
    return false, "NO FOE IN RANGE"
  end
  -- busy is a C++ float: per-op rounding
  self:set_busy(og.fadd(self:busy(), 8.0))
  self:set_curdir(-1)
  self:set_lastx(0)
  self:set_lasty(0)
  self:s_add_command(C.COMMAND_WALK, 1, 0, -1)
  self:s_add_command(C.COMMAND_WALK, 1, 1, -1)
  self:s_add_command(C.COMMAND_WALK, 1, 1, 0)
  self:s_add_command(C.COMMAND_WALK, 1, 1, 1)
  self:s_add_command(C.COMMAND_WALK, 1, 0, 1)
  self:s_add_command(C.COMMAND_WALK, 1, -1, 1)
  self:s_add_command(C.COMMAND_WALK, 1, -1, 0)
  self:s_add_command(C.COMMAND_WALK, 1, -1, -1)

  -- rule: Every foe inside the whirlwind ring takes one melee attack() and is then force-shoved 8
  --   walk steps directly away from the soldier (sign of the dx,dy from soldier to foe); the attack
  --   draws from the RNG before the shove. Ring radius 32 + 2*level px.
  -- tests: parity:soldier_whirlwind_ring_scen99
  -- tests: SemSoldier.whirlwind_strikes_and_shoves_every_foe_in_its_ring_and_nobody_outside
  for i = 1, #foes do
    local foe = foes[i]
    local dx = og.sign(foe:xpos() - self:xpos())
    local dy = og.sign(foe:ypos() - self:ypos())
    -- attack() draws from the RNG; the shove must stay after it
    self:attack(foe)
    foe:s_force_command(C.COMMAND_WALK, 8, dx, dy)
  end
  return true
end

local function disarm(self)
  -- rule: DISARM is refused 'SPECIAL BUSY' while busy; classic busy ~= 0 (a hero after a fractional
  --   shot is refused forever), fixed busy > 0. Same rule as above.
  -- tests: GameplayFixesSpecialBusy.disarm_after_a_shot
  -- tests: SemSoldier.a_busy_soldier_is_refused_disarm_for_free
  if gf.special_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- can't do this if no frontal enemy
  -- rule: DISARM needs something blocking the soldier's front (a foe at close range); with nothing
  --   ahead it is refused free 'FACE FOE AT CLOSE RANGE', even when a foe stands within reach
  --   beside or behind. Classic uses the loud probe (may swing/eat/open), fixed the quiet one.
  -- tests: FamilySoldier.disarm_refusal_explains_facing_with_a_foe_already_in_range
  -- tests: WalkerSpecials.soldier_disarm_busies_the_foe_it_disarms
  if not self:s_forward_blocked(gf.on()) then
    return false, "FACE FOE AT CLOSE RANGE"
  end

  local found = 0
  local disarmed = 0
  local foes = og.foes_in_range(self, og.tuning(self).disarm_range)
  -- plumbing: for each foe within disarm_range (28 px): see below (roll), below
  --   (penalty), and found=1 per foe in reach regardless of the roll (feeds below).
  for i = 1, #foes do
    local foe = foes[i]
    -- two draws in one comparison: parity adjudicated LEFT-first here
    -- (as at the thief and orc sites). og.rand, not rand0: living levels
    -- are >= 1, so n <= 0 would be a real bug worth the loud error.
    -- rule: Each foe in reach resists a disarm by an opposed level roll: the soldier wins when
    --   rand(soldier level) >= rand(foe level) (left draw first). A level-1 foe never resists
    --   (rand(1)==0).
    -- tests: WalkerSpecials.soldier_disarm_busies_the_foe_it_disarms
    -- tests: GameplayFixesSoldier.disarm_notice_needs_a_win
    -- tests: GameplayFixesSoldier.disarm_never_speeds_up_a_higher_foe
    if og.rand(self.level) >= og.rand(foe.level) then
      local penalty = 6 * (self.level - foe.level + 1)
      -- Gameplay Fixes: a foe above the fighter's level is not sped up by
      -- a stagger below zero, and only a real stagger counts.
      -- rule: Classic: a won roll against a foe ABOVE the soldier's level applies the negative
      --   penalty 6*(Ls-Lf+1), SPEEDING the foe up (busy decreases), and still counts as a disarm
      --   (notice + 5 busy + full price). Gameplay Fixes: a stagger <= 0 is not applied and does
      --   not count, so a higher foe is never sped up.
      -- tests: GameplayFixesSoldier.disarm_never_speeds_up_a_higher_foe
      -- tests: parity:soldier_disarm_higher_foe_scen99
      if not gf.on() or penalty > 0 then
        -- busy is a C++ float: per-op rounding
        foe:set_busy(og.fadd(foe:busy(), penalty))
        disarmed = disarmed + 1
      end
    end
    -- disarmed at least one guy
    found = 1
  end

  -- rule: With something blocking the front but no foe within the 28 px disarm reach (a wall, or a
  --   foe out of reach), DISARM is refused free; the message is 'NO FOE IN RANGE' classic, 'NO ONE
  --   TO DISARM' with Gameplay Fixes.
  -- tests: GameplayFixesSoldier.disarm_with_nobody_in_reach_is_free
  -- tests: GameplayFixesSoldier.disarm_never_speeds_up_a_higher_foe
  if found == 0 then
    -- rule: With something blocking the front but no foe within the 28 px disarm reach (a wall, or
    --   a foe out of reach), DISARM is refused free.
    -- tests: GameplayFixesSoldier.disarm_with_nobody_in_reach_is_free
    -- tests: GameplayFixesSoldier.disarm_never_speeds_up_a_higher_foe
    if gf.on() then
      -- Gameplay Fixes: nobody in reach to disarm, so nothing is spent and
      -- the message says so.
      return false, "NO ONE TO DISARM"
    end
    return false, "NO FOE IN RANGE"
  end
  -- rule: Gameplay Fixes: if every foe in reach resisted (or only negative staggers), the disarm
  --   fails 'DISARM RESISTED' but still costs half the slot's price (trunc(150/2)=75 MP) and the
  --   soldier's 5 busy. Classic: no such arm; a fully resisted disarm still succeeds (full price,
  --   notice, sound).
  -- tests: GameplayFixesSoldier.a_resisted_disarm_pays_half_the_slots_own_price
  -- tests: GameplayFixesSoldier.disarm_notice_needs_a_win
  -- tests: GameplayFixesSoldier.disarm_never_speeds_up_a_higher_foe
  -- tests: parity:soldier_disarm_resisted_price_scen99
  -- tests: SemSoldier.a_resisted_classic_disarm_still_pays_its_recovery_and_plays_the_charge_sound
  if gf.on() and disarmed == 0 then
    -- Gameplay Fixes: everyone in reach resisted. The try still costs
    -- half the slot's price and the fighter's usual recovery.
    -- magicpoints is a C++ float: per-op rounding
    self.magicpoints = og.fsub(self.magicpoints, og.div(self:s_special_cost(self:current_special()), 2))
    -- busy is a C++ float: per-op rounding
    self:set_busy(og.fadd(self:busy(), 5.0))
    return false, "DISARM RESISTED"
  end
  og.emit_sound(C.SOUND_CHARGE)
  -- rule: The 'Fighter Disarmed Enemy!' notice is shown only for a soldier on the player team (team
  --   0) or a hero (has a guy); an enemy bot's disarm is silent. Classic quirk: the notice needs
  --   only a foe in reach (found==1), so it is shown on a LOST roll too (no foe staggered); with
  --   Gameplay Fixes a cast that disarmed nobody returns 'DISARM RESISTED' before reaching this
  --   line (see above).
  -- tests: GameplayFixesSoldier.disarm_notice_needs_a_win
  -- tests: GameplayFixesSoldier.disarm_never_speeds_up_a_higher_foe
  -- tests: SemSoldier.only_the_player_team_or_a_hero_is_told_of_a_disarm
  if self.team == 0 or self:has_guy() then
    og.emit_notification("Fighter Disarmed Enemy!")
  end
  -- busy is a C++ float: per-op rounding
  self:set_busy(og.fadd(self:busy(), 5.0))
  return true
end

local function on_fire_weapon(self, weapon)
  -- rule: The thrown-blade limit applies only to a living soldier; any non-living soldier-family
  --   body fires freely (hook returns true without touching weapons_left).
  -- tests: FamilyBehaviors.soldier_weapons_left_limits_fire
  -- tests: SemSoldier.the_blade_limit_binds_only_a_living_soldier
  if self:order() ~= C.ORDER_LIVING then
    return true
  end
  -- Jonathan Dearborn's 2013 fix keeps melee available while the returning
  -- blade is away; only a ranged release consumes weapons_left.
  -- rule: A soldier throws at most weapons_left returning blades at once ((level+1)/2); with none
  --   left the ranged release is cancelled: its MP cost is refunded and the weapon is killed. Each
  --   throw decrements weapons_left (the returning blade gives it back, effect_knife_back). Melee
  --   stays available (Dearborn 2013).
  -- tests: FamilyBehaviors.soldier_weapons_left_limits_fire
  -- tests: FamilyBehaviors.soldier_batch3_special_ai_and_fire_callback_paths
  if self:weapons_left() <= 0 then
    -- Give back the magic it cost, since we didn't throw it.
    -- magicpoints is a C++ float: per-op rounding
    self.magicpoints = og.fadd(self.magicpoints, self:s_weapon_cost())
    weapon:set_dead(1)
    return false
  end
  self:set_weapons_left(self:weapons_left() - 1)
  return true
end

-- Fighters: limited weapons
local function on_create(self)
  -- rule: A newly created living soldier starts with (level+1)//2 throwable blades.
  -- tests: FamilyBehaviors.soldier_weapons_left_on_create
  -- tests: SemSoldier.on_create_arms_only_a_living_soldier_with_blades
  if self:order() == C.ORDER_LIVING then
    self:set_weapons_left((self.level + 1) // 2)
  end
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 13.0, 8.0, 5.0, 2.0)
  -- level is a hook argument with unproven range: og.div keeps C trunc
  self:set_weapons_left(og.div(level + 1, 2))
end

-- Gameplay Fixes: the classic window below (a foe one to three squares
-- off) was written for CHARGE and asked of every slot. It is still asked
-- first on every slot (it acquires the foe, and that draws); with the
-- setting on each slot then asks for its own payoff. CHARGE pays when the
-- rush lands: a living foe in the path it runs along the soldier's heading,
-- three acts of three steps, is struck and shoved (the window's foe or any
-- other; with no heading the rush walks nowhere). BOOMERANG wants a living foe inside the blade's widest orbit (17 +
-- 6 px a level) and no blade of the soldier's already out. WHIRLWIND
-- wants two bodies inside its ring (a tent counts: the spin strikes it);
-- one is what the free swing hits. DISARM wants a living foe blocking the
-- soldier's front, inside the stagger reach, not above the soldier's level.
-- COMMAND_RUSH: three acts (charge's command count) of three walksteps.
local RUSH_STEPS = 9

local function fixed(self, classic_answer)
  local slot = self:current_special()
  -- rule: Gameplay Fixes bot CHARGE: only inside the classic 20-75 px window AND only when the rush
  --   along the soldier's current heading (3 acts x 3 walksteps = 9 steps) meets a living foe that
  --   would be struck; a wall, ally, door or no heading in the path refuses.
  -- tests: GameplayFixesBots.soldier_charge_wants_a_foe_in_the_rush
  -- tests: GameplayFixesBots.soldier_charge_refuses_a_wall_in_front
  -- tests: parity:soldier_bot_charge_door_fixes_scen99
  -- tests: parity:soldier_bot_charge_unaimed_fixes_scen99
  if slot == 1 then
    -- rule: Gameplay Fixes bot CHARGE: only inside the classic 20-75 px window AND only when the
    --   rush along the soldier's current heading (3 acts x 3 walksteps = 9 steps) meets a living
    --   foe that would be struck.
    -- tests: GameplayFixesBots.soldier_charge_wants_a_foe_in_the_rush
    -- tests: GameplayFixesBots.soldier_charge_refuses_a_wall_in_front
    -- tests: parity:soldier_bot_charge_door_fixes_scen99
    -- tests: parity:soldier_bot_charge_unaimed_fixes_scen99
    if not classic_answer then
      return false
    end
    return G.rush_strikes(self, RUSH_STEPS) ~= nil
  end
  -- rule: Gameplay Fixes bot BOOMERANG: only with no boomerang of this soldier already out and a
  --   living foe inside the blade's widest orbit (17 + 6*level px).
  -- tests: GameplayFixesBots.soldier_boomerang_wants_a_foe_inside_the_orbit_and_no_blade_out
  -- tests: parity:soldier_bot_boomerang_far_fixes_scen99
  if slot == 2 then
    local out = G.owned(self, C.ORDER_FX, FX_BOOMERANG)
    -- rule: Gameplay Fixes bot BOOMERANG: only with no boomerang of this soldier already out and a
    --   living foe inside the blade's widest orbit (17 + 6*level px).
    -- tests: GameplayFixesBots.soldier_boomerang_wants_a_foe_inside_the_orbit_and_no_blade_out
    -- tests: parity:soldier_bot_boomerang_far_fixes_scen99
    return out == 0 and G.foes_within(self, 1, 17 + 6 * self.level, false)
  end
  -- rule: Gameplay Fixes bot WHIRLWIND wants two bodies (living foes or a foe tent/generator)
  --   inside its ring (32 + 2*level px) and not busy; one body is what a free swing would hit. A
  --   busy soldier on DISARM is refused through this arm too (busy refuses the cast).
  -- tests: GameplayFixesBots.soldier_whirlwind_wants_two_targets_inside_the_spin
  -- tests: GameplayFixesBots.soldier_whirlwind_waits_out_its_busy
  -- tests: GameplayFixesBots.soldier_disarm_waits_out_its_busy
  -- tests: parity:soldier_bot_whirlwind_one_foe_fixes_scen99
  if slot == 3 or gf.special_busy(self) then  -- a busy DISARM is refused here too
    -- rule: Gameplay Fixes bot WHIRLWIND wants two bodies (living foes or a foe tent/generator)
    --   inside its ring (32 + 2*level px) and not busy.
    -- tests: GameplayFixesBots.soldier_whirlwind_wants_two_targets_inside_the_spin
    -- tests: GameplayFixesBots.soldier_whirlwind_waits_out_its_busy
    -- tests: GameplayFixesBots.soldier_disarm_waits_out_its_busy
    -- tests: parity:soldier_bot_whirlwind_one_foe_fixes_scen99
    return not gf.special_busy(self) and G.targets_within(self, 2, 32 + 2 * self.level)
  end
  local f = G.ahead_blocked_by_foe(self, 28)
  -- rule: Gameplay Fixes bot DISARM wants a living foe blocking its front within 28 px whose level
  --   does not exceed the soldier's (a higher foe would only resist/not be staggered).
  -- tests: GameplayFixesBots.soldier_disarm_wants_a_foe_blocking_its_front
  -- tests: parity:soldier_bot_disarm_front_fixes_scen99
  return f ~= nil and f.level <= self.level
end

og.family("living", {
  id = "core:soldier",
  wire_id = 0,
  name = "SOLDIER",
  short_name = og.NIL,
  stats  = { strength = 12, dexterity = 6, constitution = 12,
             intelligence = 8, armor = 9, level = 1 },
  combat = { hp = 120, melee_damage = 20, stepsize = 4,
             fire_delay = 6, fire_mp_cost = 2 },
  costs  = { hire = 250,
             train = { strength = 6, dexterity = 10, constitution = 6,
                       intelligence = 25, armor = 50, level = 200 } },
  specials = {
    { id = "charge",    name = "CHARGE",    mp_cost = 25,  cast = charge, detail = "Charge causes you to run forward, damaging anything in your way.", icon = "icons/charge.png" },
    { id = "boomerang", name = "BOOMERANG", mp_cost = 100, cast = throw_boomerang, detail = "The boomerang flies out in a spiral, hurting nearby foes.", icon = "icons/boomerang.png" },
    { id = "whirlwind", name = "WHIRLWIND", mp_cost = 120, cast = whirlwind, detail = "The fighter whirls in a spiral, hurting or stunning melee foes.", icon = "icons/whirlwind.png" },
    { id = "disarm",    name = "DISARM",    mp_cost = 150, cast = disarm, detail = "Stagger the foe you face so it waits before its next swing. A stronger foe resists.", icon = "icons/disarm.png" },
  },
  default_weapon = "core:knife",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = true,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOLDIER SLAIN",
  sprite = "footman.png",
  animation = "standard",
  ai_line_of_sight = 7,
  description = "Your basic grunt, can absorb and deal damage and move moderately fast. A good all-around fighter. A soldier's normal weapon is a magical returning blade.",
  names = { "Lothar", "Arthur", "Uther", "Achilles", "Lu Bu", "Wallace",
            "Leonidas", "Attila", "Alexander", "Ajax", "Nestor", "Priam",
            "Hector", "Tom", "Bigfoot" },
  playable = true,
  playable_order = 0,
  glyph = "S",
  glyph_ascii = "S",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Returning-blade summon: lifetime/hp/damage formulas over level
    boomerang_lifetime_base = 30,
    boomerang_lifetime_per_level = 12,
    boomerang_hp_per_level = 12,
    boomerang_damage_per_level = 4,
    -- Whirlwind strike ring: base + per-level radius (px)
    whirlwind_range_base = 32,
    whirlwind_range_per_level = 2,
    -- Disarm reach (px)
    disarm_range = 28,
  },

  -- Historical charge window: about one to three grid squares.
  -- rule: Bot gate composition: the classic soldier bot casts ANY slot when its (current-or-found)
  --   foe is between 20 and 75 px away (the CHARGE window, asked of every slot); the window's foe
  --   search always runs first (it may assign self.foe and draws for cloaked candidates). Setting
  --   off its answer stands; setting on the per-slot payoff gate (fixed, above) decides.
  -- tests: FamilyBehaviors.soldier_batch3_special_ai_and_fire_callback_paths
  -- tests: GameplayFixesBots.soldier_charge_wants_a_foe_in_the_rush
  -- tests: GameplayFixesBots.soldier_boomerang_wants_a_foe_inside_the_orbit_and_no_blade_out
  -- tests: parity:soldier_bot_boomerang_far_scen99, parity:soldier_bot_whirlwind_one_foe_scen99
  -- tests: SemSoldier.the_soldier_bot_gate_acquires_its_foe_on_both_values
  check_special_ai = G.with_classic(ai.foe_in_window(20, 75), fixed),  -- fixed per-tick AI gate
  on_fire_weapon = on_fire_weapon,
  on_create = on_create,
  set_difficulty = set_difficulty,
})
