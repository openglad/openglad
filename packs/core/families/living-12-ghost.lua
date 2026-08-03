-- core:ghost — scare-cloud special (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local ai = og.use("ai")
local FX_GHOST_SCARE = assert(og.family_id("fx", "core:ghost_scare"))

local function do_special(self)
  -- The old "nifty scare thing" is a carrier; its on_death does the scare.
  local scare = og.summon(self, "fx", FX_GHOST_SCARE)
  if not scare then
    return false
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
    { id = "scare", name = "SCARE", mp_cost = 30 },
    default_cast = do_special,
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
  description = "Ghosts can pass through   \nwalls, trees, and anything\nelse that gets in the way.\nTheir chilling touch can  \nbring death quickly at    \nclose range.              \n\nSpecial: Scare",
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

  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI gate
})
