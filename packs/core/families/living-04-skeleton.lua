-- core:skeleton — ranged self-teleport special (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local lc = og.use("living_common")

local function handle_teleport(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  -- With no nearby foe, TUNNEL can recast at every MP-regeneration interval;
  -- keep its fixed 18 px/level range out of a per-cast tuning lookup.
  self:teleport_ranged(self.level * 18)
  return true
end

local function check_special_ai(self)
  -- Historical rule: tunnel only when "away from anybody."
  -- This per-tick gate keeps its fixed five-grid range in code.
  local _, foe_count = og.find_foes_in_range("ob", 5 * 16, self)
  return foe_count < 1
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 8, 12, 4, 4, 1)
end

local function do_special(self)
  if lc.mid_teleport(self) then
    return false
  end
  self.ani_type = C.ANI_TELE_OUT
  self:set_cycle(0)
  return true
end

og.family("living", {
  id = "core:skeleton",
  wire_id = 4,
  name = "SKELETON",
  short_name = "SKELTON",
  stats  = { strength = 9, dexterity = 14, constitution = 9,
             intelligence = 6, armor = 6, level = 1 },
  combat = { hp = 60, melee_damage = 4, stepsize = 6,
             fire_delay = 4.5, fire_mp_cost = 0 },
  costs  = { hire = 300,
             train = { strength = 15, dexterity = 6, constitution = 16,
                       intelligence = 25, armor = 50, level = 200 } },
  specials = {
    { id = "tunnel", name = "TUNNEL", mp_cost = 10 },
    default_cast = do_special,
  },
  default_weapon = "core:bone",
  flags = {},
  init_ani_type = 3,
  init_max_magicpoints = 0,
  leaves_bloodspot = false,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = true,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SKELETON CRUMBLED",
  sprite = "skeleton.png",
  animation = "skeleton",
  ai_line_of_sight = 7,
  description = "Skeletons are the pathetic remains of those who once were among the living. They are not particularly dangerous, but they move with blinding speed.\n\nSpecial: Tunnel",
  names = { "Drybones", "Blackbeard", "Boney", "Femur", "Patella", "Humerus",
            "Scapula" },
  playable = true,
  playable_order = 10,
  glyph = "k",
  glyph_ascii = "k",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  check_special_ai = check_special_ai,
  level_up = level_up,
  handle_teleport = handle_teleport,
})
