-- core:archer — arrow volleys, flurry, exploding shot; melee backpedal (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local ai = og.use("ai")
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
  if self:busy() ~= 0 then
    return false
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
  if self:busy() ~= 0 then
    return false
  end
  local t = og.tuning(self)
  local old_weapon = self:current_weapon()
  self:set_current_weapon(WEAPON_FIRE_ARROW)
  local arrow = self:fire()
  self:set_current_weapon(old_weapon)
  if not arrow then
    return false
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
  if not self:foe() or self:foe() ~= foe then
    self:set_foe(foe)
    self:s_clear_command()
    self:s_set_current_distance(15000)
    self:s_set_last_distance(15000)
  end
  local distance = self:distance_to_ob(foe)
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
    { id = "fire_arrows",    name = "FIRE ARROWS",    mp_cost = 20, cast = fire_arrows },
    { id = "barrage",        name = "BARRAGE",        mp_cost = 60, cast = flurry },
    { id = "exploding_bolt", name = "EXPLODING BOLT", mp_cost = 70 },
    default_cast = exploding_shot,  -- cases 3, 4, and every unmapped slot
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
  description = "Archers are fleet of foot,\nand their arrows have a   \nlong range. Although      \nthey're not as strong as  \nother fighters, they can  \nbe a good squad backbone. \n\nSpecial: Fire Arrows",
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
  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI gate
  hit_response = hit_response,
  set_difficulty = set_difficulty,
  level_up = level_up,
})
