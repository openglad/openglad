-- core:druid — plant tree, summon faerie, reveal items, protection circle (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local lc = og.use("living_common")
local WEAP_TREE = assert(og.family_id("weapon", "core:tree"))
local LIVING_FAERIE = assert(og.family_id("living", "core:faerie"))
local WEAP_CIRCLE_PROTECTION = assert(og.family_id("weapon", "core:circle_protection"))

local function plant_tree(self)
  if lc.is_busy(self) then  -- do not start the fire-and-replace sequence
    return false
  end
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, self:s_weapon_cost())
  local bolt = self:fire()
  if not bolt then
    return false
  end
  -- shim kept: busy and fire_frequency are C++ floats: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 2.0)))
  local tree = og.summon(self, "weapon", WEAP_TREE)
  if not tree then
    return false
  end
  tree:setxy(bolt:xpos(), bolt:ypos())
  tree.ani_type = C.ANI_GROW
  bolt.dead = 1
  return true
end

local function summon_faerie(self)
  if lc.is_busy(self) then
    return false
  end
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, self:s_weapon_cost())
  local bolt = self:fire()
  if not bolt then
    return false
  end
  local faerie = og.add_ob("living", LIVING_FAERIE)
  if not faerie then
    return false
  end
  faerie:set_owner(self)
  faerie.team = self.team
  faerie:set_floor(bolt:floor())  -- faerie stays on the caster's floor
  faerie:setxy(bolt:xpos(), bolt:ypos())
  faerie.lifetime = og.combat.druid_faerie_lifetime(self.level)
  bolt.dead = 1
  if not og.query_passable(faerie:xpos(), faerie:ypos(), faerie) then
    faerie.dead = 1
    return false
  end
  -- shim kept: busy and fire_frequency are C++ floats: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 3.0)))
  return true
end

local function reveal_items(self)
  if lc.is_busy(self) then
    return false
  end
  self:set_view_all(self:view_all() + self.level * 10)
  -- shim kept: busy and fire_frequency are C++ floats: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 4.0)))
  return true
end

local function protection_circle(self)
  if lc.is_busy(self) then
    return false
  end
  local friends, friend_count = og.find_friends_in_range("ob", 60, self)
  if friend_count <= 1 then
    return false
  end
  local protected_count = 0
  for i = 1, #friends do
    local friend = friends[i]
    if friend ~= self then
      -- Historic slow path, now looking where circles actually live. The
      -- C++ walked oblist (openglad-master/src/walker.cpp:3813), but
      -- add_ob routes ORDER_WEAPON to add_weap_ob and the circle lands in
      -- weaplist, so the scan never matched once between the 2002 import
      -- and 2026 and every recast minted a second circle. Range 100 is a
      -- loose bound on a ring that circle_protection_on_animate re-centres
      -- on its owner every tick, so the true distance is ~0; the owner
      -- filter, not the radius, is what selects the friend's own circle.
      local existing = nil
      local circles = og.find_in_range("weap", 100, friend)
      for j = 1, #circles do
        local ob = circles[j]
        if ob:owner() == friend
            and ob:family() == WEAP_CIRCLE_PROTECTION then
          existing = ob
          break
        end
      end
      if not existing then
        local circle = og.summon(friend, "weapon", WEAP_CIRCLE_PROTECTION)
        if not circle then
          return false
        end
        protected_count = protected_count + 1
      else
        -- A fresh circle is minted only to read a full charge off it;
        -- its hitpoints top up the existing circle and it dies unused.
        local fresh = og.add_ob("weapon", WEAP_CIRCLE_PROTECTION)
        if not fresh then
          return false
        end
        -- shim kept: hitpoints is a C++ float: per-op float rounding.
        existing.hp = og.fadd(existing.hp, fresh.hp)
        fresh.dead = 1
        protected_count = protected_count + 1
        -- TODO: Should we show healing numbers here?
      end
      -- Get experience either way
      if self:has_guy() then
        self:g_set_exp(self:g_exp() +
                       og.exp_from_action(self, friend, "protection", 0))
      end
    end
  end
  if protected_count == 0 then
    -- Everyone was okay; don't charge us.
    return false
  end
  local message
  if protected_count == 1 then
    message = "Druid protected 1 man!"
  else
    message = "Druid protected " .. protected_count .. " men!"
  end
  if self.team == 0 or self:has_guy() then
    og.emit_notification(message)
  end
  og.emit_sound(C.SOUND_HEAL)
  return true
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 9.0, 12.0, 4.0, 0.5)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 8, 3, 8, 12, 1)
end

og.family("living", {
  id = "core:druid",
  wire_id = 13,
  name = "DRUID",
  short_name = og.NIL,
  stats  = { strength = 7, dexterity = 8, constitution = 14,
             intelligence = 12, armor = 7, level = 1 },
  combat = { hp = 110, melee_damage = 10, stepsize = 3,
             fire_delay = 9, fire_mp_cost = 4 },
  costs  = { hire = 350,
             train = { strength = 15, dexterity = 15, constitution = 7,
                       intelligence = 6, armor = 50, level = 200 } },
  specials = {
    { id = "grow_tree",     name = "GROW TREE",     mp_cost = 15,  cast = plant_tree },
    { id = "summon_faerie", name = "SUMMON FAERIE", mp_cost = 80,  cast = summon_faerie },
    { id = "reveal",        name = "REVEAL",        mp_cost = 150, cast = reveal_items },
    { id = "protection",    name = "PROTECTION",    mp_cost = 200 },
    default_cast = protection_circle,  -- case 4 and every unmapped slot
  },
  default_weapon = "core:lightning",
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
  death_message = "DRUID VANQUISHED",
  sprite = "druid.png",
  animation = "standard",
  ai_line_of_sight = 10,
  description = "Druids are the magicians  \nof nature, and have power \nover natural events. They \nthrow lightning bolts at  \ntheir foes; the fast bolts\nhave long range.          \n\nSpecial: Plant Tree",
  names = { "Roland", "Merlin", "Hippy", "Green Thumb", "Treefall", "Rain" },
  playable = true,
  playable_order = 8,
  glyph = "d",
  glyph_ascii = "d",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  set_difficulty = set_difficulty,
  level_up = level_up,
})
