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
  -- damage is a C++ float: per-op rounding (the multiplier is a YAML
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

og.register_hooks("living", "core:archer", {
  specials = {
    [1] = fire_arrows,
    [2] = flurry,
    default = exploding_shot,  -- cases 3, 4, and every unmapped slot
  },
  -- about 6 squares
  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI gate
  hit_response = hit_response,
  set_difficulty = set_difficulty,
  level_up = level_up,
})
