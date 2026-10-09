-- core:ghost — scare cloud; with New Specials also wail, siphon, possess and phase (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
local kg = og.use("kit_ghost")
local ai = og.use("ai")
local FX_GHOST_SCARE = assert(og.family_id("fx", "core:ghost_scare"))

local function do_special(self)
  -- The old "nifty scare thing" is a carrier; its on_death does the scare.
  local scare = og.summon(self, "fx", FX_GHOST_SCARE)
  if not scare then
    return false, "COULD NOT CREATE SCARE"
  end
  scare:set_ani_type(1)  -- ANI_SCARE
  -- center on self: sizes/coords are non-negative shorts, so // is the C /2
  scare:setxy(self:xpos() + self:sizex() // 2 - scare:sizex() // 2,
              self:ypos() + self:sizey() // 2 - scare:sizey() // 2)
  return true
end

og.family("living", {
  id = "core:ghost",
  wire_id = 12,
  name = "GHOST",
  short_name = og.NIL,
  stats  = { strength = 6, dexterity = 12, constitution = 18,
             intelligence = 10, armor = 15, level = 1 },
  combat = { hp = 50, melee_damage = 12, stepsize = 4,
             fire_delay = 7, fire_mp_cost = 0 },
  costs  = { hire = 600,
             train = { strength = 16, dexterity = 16, constitution = 16,
                       intelligence = 16, armor = 45, level = 200 } },
  specials = {
    { id = "scare",   name = "SCARE",   mp_cost = 30, alternate = { name = "WAIL", mp_cost = 50, new_kit = true }, cast = kg.scare_or_wail(do_special), ai = ai.foe_within(130) },
    { id = "siphon",  name = "SIPHON",  mp_cost = 30, new_kit = true, cast = kg.siphon,  ai = kg.ai_siphon },
    { id = "possess", name = "POSSESS", mp_cost = 50, new_kit = true, cast = kg.possess, ai = kg.ai_possess },
    { id = "phase",   name = "PHASE",   mp_cost = 60, new_kit = true, cast = kg.phase,   ai = kg.ai_phase },
  },
  default_weapon = "core:knife",
  flags = { "FLYING", "ANIMATE", "NO_RANGED", "ETHEREAL" },
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = false,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = true,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "GHOST VANISHED",
  sprite = "ghost.png",
  animation = "standard",
  ai_line_of_sight = 12,
  description = "Ghosts can pass through walls, trees, and anything else that gets in the way. Their chilling touch can bring death quickly at close range.\n\nSpecial: Scare or Wail, Siphon, Possess, Phase",
  names = { "Casper", "Slimer", "Reaper", "Ecto", "Pepper", "Boo", "Banshee",
            "Nyx" },
  playable = true,
  playable_order = 14,
  glyph = "g",
  glyph_ascii = "g",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- SIPHON: the touch reaches siphon_reach px and hits for
    -- siphon_damage_base + siphon_damage_per_level per ghost level (about
    -- one swing of the ghost's own); the ghost heals half of what it deals.
    -- A touch that finds a foe costs one attack's pause.
    siphon_reach = 24,
    siphon_damage_base = 10,
    siphon_damage_per_level = 2,
    -- POSSESS: the touch reaches possess_reach px. The game runs 12 ticks
    -- a second. A host of the ghost's own level is ridden possess_base
    -- ticks (10 s); every doubling of 1 + the levels the ghost has over it
    -- adds about possess_log_scale x 0.7 ticks (11.5 s), up to possess_cap
    -- (one minute, at 19 levels over). A host above the ghost's level is
    -- ridden possess_min ticks. Every ride ends.
    possess_reach = 24,
    possess_min = 120,
    possess_base = 120,
    possess_log_scale = 200,
    possess_cap = 720,
    -- PHASE: how many ticks the ghost stays spectral.
    phase_ticks = 48,
  },
})
