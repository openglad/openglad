-- core:elemental — starburst, parting shot on death, owner drain (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- (Descriptor name "ELEMENTAL" is unique in the living registry.)
local ke = og.use("kit_elemental")
local ai = og.use("ai")
local G = og.use("gates")

local function do_special(self)
  -- "Lots o' fireballs": fire the default weapon in all eight directions.
  -- shim kept (both): the C++ parks the float aim in int temps: C truncation.
  local saved_aim_x = og.trunc(self:lastx())
  local saved_aim_y = og.trunc(self:lasty())
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, 8 * self:s_weapon_cost())
  -- rule: STARBURST fires one meteor along each of the 8 compass headings (weapon costs
  --   pre-refunded, 8 x wc) and restores the elemental's facing.
  -- tests: WalkerSpecials.fire_elemental_starburst_drives_simulation
  -- tests: KitElemental.dying_elemental_always_fires_slot_one
  -- tests: parity:fireelemental_starburst_ring_scen99
  -- tests: SemElemental.starburst_fires_all_eight_headings_and_restores_the_facing
  for i = -1, 1 do
    -- rule: STARBURST fires one meteor along each of the 8 compass headings (weapon costs
    --   pre-refunded, 8 x wc) and restores the elemental's facing.
    -- tests: WalkerSpecials.fire_elemental_starburst_drives_simulation
    -- tests: KitElemental.dying_elemental_always_fires_slot_one
    -- tests: parity:fireelemental_starburst_ring_scen99
    -- tests: SemElemental.starburst_fires_all_eight_headings_and_restores_the_facing
    for j = -1, 1 do
      -- rule: STARBURST fires one meteor along each of the 8 compass headings (weapon costs
      --   pre-refunded, 8 x wc) and restores the elemental's facing.
      -- tests: WalkerSpecials.fire_elemental_starburst_drives_simulation
      -- tests: KitElemental.dying_elemental_always_fires_slot_one
      -- tests: parity:fireelemental_starburst_ring_scen99
      -- tests: SemElemental.starburst_fires_all_eight_headings_and_restores_the_facing
      if i ~= 0 or j ~= 0 then
        self:set_lastx(i)
        self:set_lasty(j)
        self:fire()
      end
    end
  end
  self:set_lastx(saved_aim_x)
  self:set_lasty(saved_aim_y)
  return true
end

-- Gameplay Fixes: STARBURST, a meteor along each of the eight headings,
-- pays where one meteor would not: two living foes inside the ring's
-- flight, or one the elemental does not face. The classic foe search runs
-- first either way (G.with_classic). The meteor is step 7, range 9.
local function fixed_starburst(self)
  -- rule: Classic bot elemental casts STARBURST with an acquired foe within 130 px. Gameplay Fixes:
  --   the classic search still runs, then the ring must pay: two living foes inside the meteor
  --   flight (7 x 9) or one the elemental does not face.
  -- tests: GameplayFixesBots.elemental_starburst_wants_a_crowd_or_a_foe_behind
  -- tests: KitElemental.ai_off_answers_the_classic_gate
  return G.ring_pays(self, 7, 9)
end

-- rule: A dying elemental ALWAYS fires a free parting STARBURST (slot 1 unshifted) whatever slot or
--   Shift it was parked on: the slot-1 cost is refunded first so net MP is unchanged, a burning
--   IMMOLATE channel is put out, the body is un-killed for the cast and re-killed, and
--   current_special/shifter_down are restored so the body dies as it was.
-- tests: KitElemental.dying_elemental_always_fires_slot_one
-- tests: KitElemental.dying_while_immolating_still_fires_the_parting_starburst
-- tests: SemElemental.an_elemental_dying_on_slot_one_fires_a_free_starburst_and_stays_dead
local function on_death(self)
  -- Free parting starburst: un-kill, refund the special cost, fire, re-kill.
  -- The parting shot is ALWAYS slot 1 unshifted: a dying elemental parked
  -- on another slot must not cast that slot (the New Specials kits made
  -- this reachable); both fields are restored so the body dies as it was.
  local slot = self:current_special()
  local shift = self:shifter_down()
  self:set_current_special(1)
  self:set_shifter_down(0)
  ke.end_channel(self)  -- a burning IMMOLATE goes out with the body
  self.dead = 0
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, self:s_special_cost(1))
  self:special()
  self.dead = 1
  self:set_current_special(slot)
  self:set_shifter_down(shift)
  return true
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 6, 4, 8, 1)
end

local function on_act_living(self)
  -- A summoned elemental (finite lifetime) takes the old "toll from our
  -- mage": 1 HP + 3 MP buys 1 HP of healing; if the owner cannot pay both,
  -- the elemental burns lifetime instead.
  -- rule: Only a summoned elemental (finite lifetime) with a living owner and missing health takes
  --   the toll from its summoner; a natural (lifetime 0) or full-health elemental, or one whose
  --   owner is gone/dead, takes nothing and burns no lifetime.
  -- tests: KitElemental.classic_level_up_and_summoned_toll_are_unchanged
  -- tests: SemElemental.only_a_hurt_summoned_elemental_with_a_living_owner_takes_the_toll
  if self:lifetime() ~= 0 then
    local owner = self:owner()
    -- rule: Only a summoned elemental (finite lifetime) with a living owner and missing health
    --   takes the toll from its summoner.
    -- tests: KitElemental.classic_level_up_and_summoned_toll_are_unchanged
    -- tests: SemElemental.only_a_hurt_summoned_elemental_with_a_living_owner_takes_the_toll
    if owner and owner:dead() == 0 then
      -- rule: Only a summoned elemental (finite lifetime) with a living owner and missing health
      --   takes the toll from its summoner.
      -- tests: KitElemental.classic_level_up_and_summoned_toll_are_unchanged
      -- tests: SemElemental.only_a_hurt_summoned_elemental_with_a_living_owner_takes_the_toll
      if self.hp < self.max_hp then
        local paid = 0
        -- shim kept: max_hp is a C++ float: per-op float rounding.
        -- rule: A hurt summoned elemental takes 1 HP (if its owner is at or above a third of max
        --   health) and then 3 MP from its owner to heal 1 HP per tick; if the owner cannot pay
        --   both, the elemental loses one EXTRA tick of lifetime (on top of the engine's per-act
        --   lifetime tick) instead -- and an owner who paid the HP but not the MP still loses that
        --   HP (classic quirk).
        -- tests: KitElemental.classic_level_up_and_summoned_toll_are_unchanged
        -- tests: SemElemental.classic_an_owner_who_cannot_pay_both_costs_the_elemental_a_tick
        if owner.hp >= og.fdiv(owner.max_hp, 3.0) then
          paid = 1
          -- shim kept: hp is a C++ float: per-op float rounding.
          owner.hp = og.fsub(owner.hp, 1.0)
        end
        if paid ~= 0 and owner.magicpoints >= 3 then
          paid = paid + 1
          -- shim kept: magicpoints is a C++ float: per-op float rounding.
          owner.magicpoints = og.fsub(owner.magicpoints, 3.0)
        end
        if paid == 2 then
          -- shim kept: hp is a C++ float: per-op float rounding.
          self.hp = og.fadd(self.hp, 1.0)
        else
          self:set_lifetime(self:lifetime() - 1)
        end
      end
    end
  end
end

og.family("living", {
  id = "core:elemental",
  wire_id = 6,
  name = "ELEMENTAL",
  short_name = "ELEMENT.",
  stats  = { strength = 14, dexterity = 10, constitution = 14,
             intelligence = 14, armor = 9, level = 1 },
  combat = { hp = 100, melee_damage = 28, stepsize = 4,
             fire_delay = 5, fire_mp_cost = 1 },
  costs  = { hire = 600,
             train = { strength = 7, dexterity = 10, constitution = 14,
                       intelligence = 12, armor = 50, level = 200 } },
  specials = {
    { id = "starburst", name = "STARBURST",   mp_cost = 50, ai = ke.ai_starburst(G.with_classic(ai.foe_within(130), fixed_starburst)), detail = "Expel flaming meteors in all directions.", icon = "icons/starburst.png" },
    { id = "immolate",  name = "IMMOLATE",    mp_cost = 30, new_kit = true, cast = ke.immolate,    ai = ke.ai_immolate, detail = "Burn whatever touches you and leave burning footprints, while your magic lasts.", icon = "icons/immolate.png" },
    { id = "meteors",   name = "METEOR RAIN", mp_cost = 70, new_kit = true, cast = ke.meteor_rain, ai = ke.ai_meteor, detail = "Call meteors down on a distant area for a few seconds.", icon = "icons/meteor_rain.png" },
    { id = "rekindle",  name = "REKINDLE",    mp_cost = 40, new_kit = true, alternate = { name = "SUPERNOVA", mp_cost = 10, detail = "Die in one huge blast.", icon = "icons/supernova.png" }, cast = ke.rekindle_or_supernova, ai = ke.ai_rekindle, detail = "Burn magic into health.", icon = "icons/rekindle.png" },
    default_cast = ke.starburst(do_special),
  },
  default_weapon = "core:meteor",
  flags = { "ANIMATE" },
  init_ani_type = 0,
  init_max_magicpoints = 150,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "FIRE ELEMENTAL EXTINGUISHED",
  sprite = "firelem.png",
  animation = "standard",
  ai_line_of_sight = 10,
  description = "Strong and quick, fire elementals can expel flaming meteors in all directions to decimate enemies.",
  names = { "Furnace", "Molten", "Burns", "Fire Eli", "Fireball", "Sunny",
            "Lava", "Heatwave", "Torch", "Scorch" },
  playable = true,
  playable_order = 11,
  glyph = "E",
  glyph_ascii = "E",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
  tuning = {
    -- A toggle ignores its own second press for kit_latch ticks.
    kit_latch = 10,
    -- IMMOLATE: mana burnt per tick, longest burn (ticks), contact burn
    -- (plus level, doubled on the melee attacker), an ember every
    -- ember_step ticks lasting ember_ticks and burning ember_damage
    -- (plus half the level).
    immolate_drain = 2,
    immolate_max = 200,
    immolate_contact = 6,
    ember_step = 4,
    ember_ticks = 40,
    ember_damage = 4,
    -- METEOR RAIN: reach (px), length (ticks), a strike every
    -- rain_cadence ticks within rain_spread px of the spot, damage base
    -- (plus twice the level).
    rain_range = 160,
    rain_ticks = 60,
    rain_cadence = 6,
    rain_spread = 40,
    rain_damage_base = 8,
    -- REKINDLE heals rekindle_hp plus 4 per level; SUPERNOVA's blast is
    -- one and a half times the health it burns plus nova_per_level per
    -- level, and eight blasts of half that go off in a ring nova_ring_px
    -- out from it.
    rekindle_hp = 40,
    nova_per_level = 15,
    nova_ring_px = 40,
  },

  level_up = level_up,
  on_death = on_death,
  on_act_living = on_act_living,
})
