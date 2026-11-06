-- core:druid — plant tree, summon faerie, reveal items, protection circle (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local lc = og.use("living_common")
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local WEAP_TREE = assert(og.family_id("weapon", "core:tree"))
local LIVING_FAERIE = assert(og.family_id("living", "core:faerie"))
local WEAP_CIRCLE_PROTECTION = assert(og.family_id("weapon", "core:circle_protection"))

local function plant_tree(self)
  -- rule: A busy druid cannot cast GROW TREE (the other three druid casts
  --   share the rule, each in its own function).
  -- tests: GameplayFixesDruid.an_open_tree_costs_its_price_on_both_values
  -- tests: WalkerSpecials.druid_grow_tree_drives_simulation
  -- tests: SemDruid.a_busy_druid_casts_none_of_its_four_specials
  if lc.is_busy(self) then  -- do not start the fire-and-replace sequence
    return false, "SPECIAL BUSY"
  end
  -- rule: Gameplay Fixes: a druid aiming its bolt into terrain (wall/map edge on the fire pad) is
  --   refused before anything is paid (the same rule as in summon_faerie below). Classic: the
  --   cast goes on to fire() and fails there.
  -- tests: GameplayFixesDruid.a_cast_at_terrain_spends_nothing_with_the_setting_on
  -- tests: GameplayFixesDruid.a_cast_at_the_map_edge_spends_nothing
  -- tests: GameplayFixesDruid.an_open_tree_costs_its_price_on_both_values
  -- tests: SemDruid.a_classic_cast_into_a_wall_attempts_the_bolt_and_fails_there
  if gf.pad_blocked_by_terrain(self) then
    return false, "COULD NOT FIRE"
  end
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, self:s_weapon_cost())
  local bolt = self:fire()
  -- rule: The druid pre-refunds its bolt's cost before fire(); classic keeps that refund when the
  --   bolt never flies (a blocked cast gives back one bolt's MP), Gameplay Fixes takes it back so a
  --   failed bolt costs exactly nothing extra (same rule in plant_tree and summon_faerie).
  -- tests: GameplayFixesDruid.a_blocked_tree_costs_the_swing, parity:druid_tree_blocked_scen99
  -- tests: parity:druid_tree_blocked_fixes_scen99
  -- tests: GameplayFixesDruid.an_open_tree_costs_its_price_on_both_values
  if not bolt then
    -- rule: The druid pre-refunds its bolt's cost before fire().
    -- tests: GameplayFixesDruid.a_blocked_tree_costs_the_swing, parity:druid_tree_blocked_scen99
    -- tests: parity:druid_tree_blocked_fixes_scen99
    -- tests: GameplayFixesDruid.an_open_tree_costs_its_price_on_both_values
    if gf.on() then
      -- Gameplay Fixes: the refund paid for a bolt that never flew.
      gf.take_back(self, 1)
    end
    return false, "COULD NOT FIRE"
  end
  -- shim kept: busy and fire_frequency are C++ floats: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 2.0)))
  local tree = og.summon(self, "weapon", WEAP_TREE)
  -- plumbing: og.summon failure for the tree (object cap): 'COULD NOT CREATE TREE' (bolt already
  --   flew).
  if not tree then
    return false, "COULD NOT CREATE TREE"
  end
  tree:setxy(bolt:xpos(), bolt:ypos())
  tree.ani_type = C.ANI_GROW
  bolt.dead = 1
  return true
end

local function summon_faerie(self)
  -- rule: A busy druid cannot cast SUMMON FAERIE (the other three druid casts
  --   share the rule, each in its own function).
  -- tests: WalkerSpecials.druid_summon_faerie_drives_simulation
  -- tests: SemDruid.a_busy_druid_casts_none_of_its_four_specials
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: Gameplay Fixes: a druid aiming its bolt into terrain (wall/map edge on the fire pad) is
  --   refused before anything is paid (the same rule as in plant_tree above). Classic: the cast
  --   goes on to fire() and fails there.
  -- tests: GameplayFixesDruid.a_cast_at_terrain_spends_nothing_with_the_setting_on
  -- tests: GameplayFixesDruid.a_cast_at_the_map_edge_spends_nothing
  -- tests: WalkerSpecials.druid_summon_faerie_drives_simulation
  -- tests: SemDruid.a_classic_cast_into_a_wall_attempts_the_bolt_and_fails_there
  if gf.pad_blocked_by_terrain(self) then
    return false, "COULD NOT FIRE"
  end
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, self:s_weapon_cost())
  local bolt = self:fire()
  -- rule: The druid pre-refunds its bolt's cost before fire(); classic keeps that refund when the
  --   bolt never flies (a blocked cast gives back one bolt's MP), Gameplay Fixes takes it back so a
  --   failed bolt costs exactly nothing extra (same rule in plant_tree and summon_faerie).
  -- tests: GameplayFixesDruid.a_blocked_faerie_costs_the_swing
  -- tests: WalkerSpecials.druid_summon_faerie_drives_simulation
  if not bolt then
    -- rule: The druid pre-refunds its bolt's cost before fire().
    -- tests: GameplayFixesDruid.a_blocked_faerie_costs_the_swing
    -- tests: WalkerSpecials.druid_summon_faerie_drives_simulation
    if gf.on() then
      -- Gameplay Fixes: the refund paid for a bolt that never flew.
      gf.take_back(self, 1)
    end
    return false, "COULD NOT FIRE"
  end
  local faerie = og.add_ob("living", LIVING_FAERIE)
  -- plumbing: add_ob failure for the faerie (object cap): 'COULD NOT CREATE FAERIE'.
  if not faerie then
    return false, "COULD NOT CREATE FAERIE"
  end
  faerie:set_owner(self)
  faerie.team = self.team
  faerie:set_floor(bolt:floor())  -- faerie stays on the caster's floor
  faerie:setxy(bolt:xpos(), bolt:ypos())
  faerie.lifetime = og.combat.druid_faerie_lifetime(self.level)
  bolt.dead = 1
  -- rule: A faerie summoned where its body cannot stand (the bolt's spot is blocked) is cancelled:
  --   the faerie dies and the cast fails NO ROOM TO SUMMON.
  -- tests: SemDruid.a_faerie_summoned_on_open_ground_lives_for_its_lifetime
  -- tests: SemDruid.a_faerie_summoned_onto_a_body_is_cancelled
  if not og.query_passable(faerie:xpos(), faerie:ypos(), faerie) then
    faerie.dead = 1
    return false, "NO ROOM TO SUMMON"
  end
  -- shim kept: busy and fire_frequency are C++ floats: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 3.0)))
  return true
end

local function reveal_items(self)
  -- rule: A busy druid cannot cast REVEAL (the other three druid casts
  --   share the rule, each in its own function).
  -- tests: FamilyBehaviors.druid_batch3_special_branches
  -- tests: WalkerSpecials.druid_reveal_drives_simulation
  -- tests: SemDruid.a_busy_druid_casts_none_of_its_four_specials
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  self:set_view_all(self:view_all() + self.level * 10)
  -- shim kept: busy and fire_frequency are C++ floats: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), og.fmul(self:fire_frequency(), 4.0)))
  return true
end

local function protection_circle(self)
  -- rule: A busy druid cannot cast PROTECTION (the other three druid casts
  --   share the rule, each in its own function).
  -- tests: FamilyBehaviors.druid_batch3_special_branches
  -- tests: PackLuaDruid.one_ally_receives_one_circle_and_caster_receives_none
  -- tests: SemDruid.a_busy_druid_casts_none_of_its_four_specials
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local friends, friend_count = og.find_friends_in_range("ob", 60, self)
  -- rule: PROTECTION needs at least one ally within 60 px (the druid itself is not protected); with
  --   none the cast is free.
  -- tests: PackLuaDruid.a_lone_druid_cannot_cast_circle_of_protection
  -- tests: PackLuaDruid.one_ally_receives_one_circle_and_caster_receives_none
  if friend_count == 0 then
    -- Everyone was okay; don't charge us.
    return false, "NO ALLY IN RANGE"
  end
  local protected_count = 0
  -- plumbing: per-friend loop and per-friend circle scan; rule rows below,
  --   below
  for i = 1, #friends do
    local friend = friends[i]
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
    -- plumbing: per-friend loop and per-friend circle scan; rule rows below,
    --   below
    for j = 1, #circles do
      local ob = circles[j]
      -- rule: A friend's existing ring is recognised only by owner AND family among weapons within
      --   100 px (another friend's ring nearby is not this friend's).
      -- tests: WalkerSpecials.druid_protection_tops_up_an_existing_circle
      -- tests: PackLuaDruid.each_circle_of_protection_cast_shields_every_friend_in_range
      -- tests: parity:druid_protection_refresh_scen99
      -- tests: SemDruid.a_friends_own_knife_is_not_mistaken_for_its_ring
      if ob:owner() == friend
          and ob:family() == WEAP_CIRCLE_PROTECTION then
        existing = ob
        break
      end
    end
    -- rule: An unringed friend gets a new circle of protection; an already-ringed friend's circle
    --   is topped up by a fresh circle's full charge instead of stacking a second ring.
    -- tests: PackLuaDruid.one_ally_receives_one_circle_and_caster_receives_none
    -- tests: WalkerSpecials.druid_protection_tops_up_an_existing_circle
    -- tests: parity:druid_protection_refresh_scen99, FamilyBehaviors.druid_batch3_special_branches
    if not existing then
      local circle = og.summon(friend, "weapon", WEAP_CIRCLE_PROTECTION)
      -- plumbing: og.summon / add_ob failure for the circle: 'COULD NOT PROTECT' (returns mid-loop;
      --   earlier friends keep their circles and exp, cast reported failed).
      if not circle then
        return false, "COULD NOT PROTECT"
      end
      protected_count = protected_count + 1
    else
      -- A fresh circle is minted only to read a full charge off it;
      -- its hitpoints top up the existing circle and it dies unused.
      local fresh = og.add_ob("weapon", WEAP_CIRCLE_PROTECTION)
      -- plumbing: og.summon / add_ob failure for the circle: 'COULD NOT PROTECT' (returns mid-loop;
      --   earlier friends keep their circles and exp, cast reported failed).
      if not fresh then
        return false, "COULD NOT PROTECT"
      end
      -- shim kept: hitpoints is a C++ float: per-op float rounding.
      existing.hp = og.fadd(existing.hp, fresh.hp)
      fresh.dead = 1
      protected_count = protected_count + 1
      -- TODO: Should we show healing numbers here?
    end
    -- Get experience either way
    -- rule: A hero druid earns experience for every friend it protects, whether the friend got a
    --   new ring or a top-up; a monster druid earns nothing.
    -- tests: SemDruid.a_hero_druid_earns_experience_per_friend_protected
    if self:has_guy() then
      self:g_set_exp(self:g_exp() +
                     og.exp_from_action(self, friend, "protection", 0))
    end
  end
  local message
  -- rule: The PROTECTION message counts the friends protected, singular for one.
  -- tests: SemDruid.the_protection_message_counts_the_friends_singular_for_one
  if protected_count == 1 then
    message = "Druid protected 1 man!"
  else
    message = "Druid protected " .. protected_count .. " men!"
  end
  -- rule: Only the player's side (team 0) or a hero druid announces PROTECTION; an enemy druid
  --   casts silently (the heal sound plays either way).
  -- tests: SemDruid.only_the_home_team_or_a_hero_druid_announces_protection
  if self.team == 0 or self:has_guy() then
    og.emit_notification(message)
  end
  og.emit_positional_sound(self, C.SOUND_HEAL)
  return true
end

-- The bot gate. The classic druid had none: every special it rolled was
-- cast. With the Gameplay Fixes setting on, each asks for its payoff.
local function check_special_ai(self)
  -- rule: Classic: the druid has no bot gate; every special it rolls is cast.
  -- tests: GameplayFixesBots.druid_tree_wants_a_faced_foe_closing_but_not_touching
  -- tests: GameplayFixesBots.druid_faerie_wants_a_foe_near_and_no_faerie_out
  -- tests: GameplayFixesBots.druid_reveal_is_never_cast_by_a_bot
  -- tests: GameplayFixesBots.druid_protection_wants_an_ally_under_blows_without_a_fresh_circle
  -- tests: parity:druid_bot_tree_behind_scen99, parity:druid_bot_faerie_alone_scen99
  -- tests: parity:druid_bot_protection_idle_scen99
  if not gf.on() then
    return true -- the classic druid casts every roll
  end
  -- All four casts refuse a busy druid (their own guard).
  -- rule: Gameplay Fixes: a busy druid bot rolls no special.
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  if lc.is_busy(self) then
    return false
  end
  local slot = self:current_special()
  -- rule: Gameplay Fixes: GROW TREE pays only between the druid and a living foe closing on it: the
  --   nearest living foe within 60 px, faced, not yet at blows, with the bolt's pad clear.
  -- tests: GameplayFixesBots.druid_tree_wants_a_faced_foe_closing_but_not_touching
  -- tests: GameplayFixesBots.druid_tree_refuses_a_pad_on_a_wall
  -- tests: parity:druid_bot_tree_behind_fixes_scen99, parity:druid_bot_tree_pad_body_fixes_scen99
  if slot == 1 then
    -- GROW TREE: a tree between the druid and a foe closing on it: the
    -- nearest living foe faced, within 60 px, not yet at blows (a foe
    -- touching blocks the bolt the tree grows from), the bolt's pad clear.
    local f = G.nearest_living_foe(self, 60, true)
    -- rule: Gameplay Fixes: GROW TREE pays only between the druid and a living foe closing on it.
    -- tests: GameplayFixesBots.druid_tree_wants_a_faced_foe_closing_but_not_touching
    -- tests: GameplayFixesBots.druid_tree_refuses_a_pad_on_a_wall
    -- tests: parity:druid_bot_tree_behind_fixes_scen99, parity:druid_bot_tree_pad_body_fixes_scen99
    if f == nil or not G.pad_clear(self) then
      return false
    end
    -- rule: Gameplay Fixes: GROW TREE pays only between the druid and a living foe closing on it.
    -- tests: GameplayFixesBots.druid_tree_wants_a_faced_foe_closing_but_not_touching
    -- tests: GameplayFixesBots.druid_tree_refuses_a_pad_on_a_wall
    -- tests: parity:druid_bot_tree_behind_fixes_scen99, parity:druid_bot_tree_pad_body_fixes_scen99
    return G.faces(self, f) and not G.at_blows(self, f)
  elseif slot == 2 then  -- rule: Gameplay Fixes: SUMMON FAERIE pays with a living foe within 130
    --   px, no faerie of the druid's out, and the bolt's pad clear.
    -- tests: GameplayFixesBots.druid_faerie_wants_a_foe_near_and_no_faerie_out
    -- tests: GameplayFixesBots.druid_faerie_refuses_a_pad_on_a_foe
    -- tests: parity:druid_bot_faerie_alone_fixes_scen99
    -- SUMMON FAERIE: a living foe within 130 px, no faerie out, the bolt's pad clear.
    -- rule: Gameplay Fixes: SUMMON FAERIE pays with a living foe within 130 px, no faerie of the
    --   druid's out, and the bolt's pad clear.
    -- tests: GameplayFixesBots.druid_faerie_wants_a_foe_near_and_no_faerie_out
    -- tests: GameplayFixesBots.druid_faerie_refuses_a_pad_on_a_foe
    -- tests: parity:druid_bot_faerie_alone_fixes_scen99
    if G.owned(self, C.ORDER_LIVING, LIVING_FAERIE) > 0 then
      return false
    end
    -- rule: Gameplay Fixes: SUMMON FAERIE pays with a living foe within 130 px, no faerie of the
    --   druid's out, and the bolt's pad clear.
    -- tests: GameplayFixesBots.druid_faerie_wants_a_foe_near_and_no_faerie_out
    -- tests: GameplayFixesBots.druid_faerie_refuses_a_pad_on_a_foe
    -- tests: parity:druid_bot_faerie_alone_fixes_scen99
    return G.foes_within(self, 1, 130, false) and G.pad_clear(self)
  elseif slot == 3 then  -- rule: Gameplay Fixes: a druid bot never casts REVEAL (it only changes a
    --   viewscreen's radar).
    -- tests: GameplayFixesBots.druid_reveal_is_never_cast_by_a_bot
    -- REVEAL changes only a viewscreen's radar: a bot never casts it.
    return false
  end
  -- PROTECTION: an ally within 60 px with a living foe at blows with it,
  -- and no circle on it or one under 50 hp.
  local ally = G.ally_at_blows(self, 60)
  -- rule: Gameplay Fixes: PROTECTION pays for an ally within 60 px that a living foe is at blows
  --   with, and only if that ally has no circle or one under 50 hp.
  -- tests: GameplayFixesBots.druid_protection_wants_an_ally_under_blows_without_a_fresh_circle
  -- tests: parity:druid_bot_protection_idle_fixes_scen99
  if not ally then
    return false
  end
  local _, circle = G.owned(ally, C.ORDER_WEAPON, WEAP_CIRCLE_PROTECTION)
  return circle == nil or circle.hp < 50
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
    { id = "grow_tree",     name = "GROW TREE",     mp_cost = 15,  cast = plant_tree, detail = "These magical trees will resist the enemy, while allowing friends to pass.", icon = "icons/grow_tree.png" },
    { id = "summon_faerie", name = "SUMMON FAERIE", mp_cost = 80,  cast = summon_faerie, detail = "This spell brings to you a small flying faerie to stun foes.", icon = "icons/summon_faerie.png" },
    { id = "reveal",        name = "REVEAL",        mp_cost = 150, cast = reveal_items, detail = "Gives you a magical view to see treasure, potions, outposts, and invisible enemies.", icon = "icons/reveal.png" },
    { id = "protection",    name = "PROTECTION",    mp_cost = 200, detail = "Calls the winds to aid your nearby friends by circling them with a shield of moving air.", icon = "icons/protection.png" },
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
  description = "Druids are the magicians of nature, and have power over natural events. They throw lightning bolts at their foes; the fast bolts have long range.",
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

  check_special_ai = check_special_ai,
  set_difficulty = set_difficulty,
  level_up = level_up,
})
