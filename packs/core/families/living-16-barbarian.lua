-- core:barbarian — boulder-toss special (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local WEAP_BOULDER = assert(og.family_id("weapon", "core:boulder"))

-- The boulder is made, not fired, so its flight is not G.flight_px's: it
-- rolls the weapon table's 9 steps (gloader.cpp) of the speed do_special
-- sets below (a bot's 2 px a level, a hero's 1 + strength / 7, held to 1 ..
-- 15), with no level or straight-line growth, and its first walk only
-- turns it.
local BOULDER_LOS = 9

local function do_special(self)
  if self:busy() > 0 then
    return false, "SPECIAL BUSY"
  end
  local shot = self:fire()
  -- failed somehow? !?!
  if not shot then
    return false, "COULD NOT FIRE"
  end
  local boulder = og.add_ob("weapon", WEAP_BOULDER)
  if not boulder then
    return false, "COULD NOT CREATE BOULDER"
  end
  boulder:set_floor(shot:floor())  -- boulder rolls on the thrower's floor
  boulder:center_on(shot)
  boulder:set_owner(self)
  boulder.level = self.level
  boulder:set_lastx(shot:lastx())
  boulder:set_lasty(shot:lasty())
  -- Set our boulder's speed and extra damage ..
  if self:has_guy() then
    -- guy strength is a short: strength / 7 is INTEGER division in the C++,
    -- then the int joins 1.0f in a single float add.
    boulder:set_stepsize(1 + self:g_strength() // 7)
    -- damage is a C++ float and strength/5 is FLOAT division: per-op
    -- rounding.
    boulder.damage = og.fadd(boulder:damage(),
                             og.fdiv(self:g_strength(), 5.0))
  else
    boulder:set_stepsize(self.level * 2)
    -- damage is a C++ float: per-op rounding.
    boulder.damage = og.fadd(boulder:damage(), self.level)
  end
  if boulder:stepsize() < 1 then
    boulder:set_stepsize(1)
  end
  if boulder:stepsize() > 15 then
    boulder:set_stepsize(15)
  end
  if boulder:lasty() > 0 then
    boulder:set_lasty(boulder:stepsize())
  elseif boulder:lasty() < 0 then
    boulder:set_lasty(-boulder:stepsize())
  end
  if boulder:lastx() > 0 then
    boulder:set_lastx(boulder:stepsize())
  elseif boulder:lastx() < 0 then
    boulder:set_lastx(-boulder:stepsize())
  end
  -- Legacy sentinel: 5000 means "explode this boulder on impact."
  if self:current_special() == 2 then
    boulder:set_skip_exit(5000)
  else
    boulder:set_skip_exit(0)
  end
  shot.dead = 1
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(og.fadd(self:busy(), 1.0),
                      self:current_special() * 5)
  return true
end

local function boulder_reach(self)
  local step
  if self:has_guy() then
    step = 1 + self:g_strength() // 7
  else
    step = 2 * self.level
  end
  return (BOULDER_LOS - 1) * og.clamp(step, 1, 15)
end

-- The barbarian's nearest living foe on its floor, when the barbarian faces
-- it (the boulder leaves along its heading) and it is inside the
-- boulder's flight; else nil.
local function boulder_target(self)
  local reach = boulder_reach(self)
  local f = G.nearest_living_foe(self, 2 * reach + 64, true)
  if not f or not G.faces(self, f) then
    return nil
  end
  local gap_x, gap_y = G.gap(self, f)
  if og.max(gap_x, gap_y) > reach then
    return nil
  end
  return f
end

-- Gameplay Fixes: the classic barbarian had no gate and threw at nothing.
-- With the setting on HURL BOULDER wants a faced foe inside the boulder's
-- flight; EXPLODING BOULDER wants that foe with a second living foe inside
-- the blast around it, and neither the barbarian nor an ally of its own in
-- the blast. Draw-free: finders and positions only.
local function check_special_ai(self)
  if not gf.on() then
    return true
  end
  local slot = self:current_special()
  if slot == 2 then
    local f = boulder_target(self)
    if not f then
      return false
    end
    local reach = G.blast_reach(self.level)
    local foes = G.living_foes(self, G.RING_SCAN, true)
    local near = 0
    for i = 1, #foes do
      if foes[i] == f or f:distance_to_ob(foes[i]) <= reach then
        near = near + 1
      end
    end
    return near >= 2 and G.blast_clear(self, f, reach, true)
  end
  return boulder_target(self) ~= nil
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.family("living", {
  id = "core:barbarian",
  wire_id = 16,
  name = "BARBARIAN",
  short_name = "BARBAR.",
  stats  = { strength = 14, dexterity = 5, constitution = 14,
             intelligence = 8, armor = 8, level = 1 },
  combat = { hp = 150, melee_damage = 25, stepsize = 3,
             fire_delay = 5.5, fire_mp_cost = 2 },
  costs  = { hire = 350,
             train = { strength = 5, dexterity = 35, constitution = 5,
                       intelligence = 35, armor = 50, level = 200 } },
  specials = {
    { id = "hurl_boulder",      name = "HURL BOULDER",      mp_cost = 20, detail = "Throw a massive stone boulder at your enemies.", icon = "icons/hurl_boulder.png" },
    { id = "exploding_boulder", name = "EXPLODING BOULDER", mp_cost = 30, detail = "Hurl a boulder so hard that it explodes and hits foes all around.", icon = "icons/exploding_boulder.png" },
    default_cast = do_special,
  },
  default_weapon = "core:hammer",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 0.5,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "barby.png",
  animation = "standard",
  ai_line_of_sight = 12,
  description = "Barbarians are powerful and resist some magic damage, but have more will than skill. They are tough, tending to bash their way through trouble with heavy iron hammers.",
  names = { "Thor", "Conan", "Beowulf", "Cronus", "Pallas", "Atlas",
            "Prometheus", "Titan" },
  playable = true,
  playable_order = 1,
  glyph = "B",
  glyph_ascii = "B",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  check_special_ai = check_special_ai,
  level_up = level_up,
})
