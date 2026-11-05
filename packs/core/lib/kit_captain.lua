-- core lib: kit_captain — the orc captain's New Specials: howl, eat corpse, hook blade/knife fan, war banner/warband, and their bot gates (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local G = og.use("gates")
local lc = og.use("living_common")
local orc = og.use("orc_specials")

local FX_HOOK_BLADE = assert(og.family_id("fx", "core:hook_blade"))
local WEAPON_WAR_BANNER = assert(og.family_id("weapon", "core:war_banner"))
local LIVING_ORC = assert(og.family_id("living", "core:orc"))

-- The eight facings, indexed curdir + 1 (FACE_UP = 0, clockwise).
local DIR_X = { 0, 1, 1, 1, 0, -1, -1, -1 }
local DIR_Y = { -1, -1, 0, 1, 1, 1, 0, -1 }

local M = {}

-- ---------------------------------------------------------------- helpers

-- The facing as 0..7. A walker parked on the curdir -1 sentinel (soldier
-- whirlwind, archer volley) is read from the signs of its aim instead; an
-- aim of (0, 0) faces down, the standing pose.
local function facing_index(self)
  local dir = self:curdir()
  -- rule: The captain's knife fan flies along his facing; a captain parked on the curdir -1
  --   sentinel (after a whirlwind/volley) fans along the signs of his aim, and with no aim at all
  --   he fans downward.
  -- tests: KitCaptain.knife_fan_throws_three
  -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
  if dir >= 0 then
    return og.mod(dir, 8)
  end
  local sx = og.sign(self:lastx())
  local sy = og.sign(self:lasty())
  for i = 1, 8 do
    -- rule: The captain's knife fan flies along his facing.
    -- tests: KitCaptain.knife_fan_throws_three
    -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
    if DIR_X[i] == sx and DIR_Y[i] == sy then
      return i - 1
    end
  end
  return C.FACE_DOWN
end

-- One coordinate of a ring spot: the mover's top-left so that it stands just
-- beside the anchor on that side (2 px apart), or level with it.
local function ring_coord(anchor_pos, anchor_size, mover_size, side)
  -- rule: A spot in the ring round an anchor puts the mover 2 px beyond the anchor's edge on that
  --   side, or level with it on the middle axis.
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: KitCaptain.hook_reels_a_foe_in_from_the_west
  if side > 0 then
    return anchor_pos + anchor_size + 2
  end
  if side < 0 then
    return anchor_pos - mover_size - 2
  end
  return anchor_pos
end

-- The clear spot hugging `anchor` nearest the point (near_x, near_y) where
-- `mover` can stand, or nil. og.spawn_spot_clear is the eat-free probe, so
-- a probe never eats a drumstick or picks up a flag; ties keep the first
-- spot clockwise from straight up.
function M.ring_spot(mover, anchor, near_x, near_y)
  local best_x = nil
  local best_y = nil
  local best_d = 0
  -- rule: A hooked foe is set down on the clear ring spot beside the captain nearest the point it
  --   is reeled from (ties: first clockwise from straight up); a probe never eats or picks anything
  --   up; no clear spot -> nil (the reel stops).
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: KitCaptain.hook_reels_a_foe_in_from_the_west
  -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
  -- tests: SemGatesProbe.a_reeled_foe_takes_the_first_clockwise_of_two_equally_near_ring_spots
  for i = 1, 8 do
    local x = ring_coord(anchor:xpos(), anchor:sizex(), mover:sizex(), DIR_X[i])
    local y = ring_coord(anchor:ypos(), anchor:sizey(), mover:sizey(), DIR_Y[i])
    local d = math.abs(x - near_x) + math.abs(y - near_y)
    -- rule: A hooked foe is set down on the clear ring spot beside the captain nearest the point it
    --   is reeled from (ties. Here: `best_x == nil or d < best_d`.
    -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
    -- tests: KitCaptain.hook_reels_a_foe_in_from_the_west
    -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
    -- tests: SemGatesProbe.a_reeled_foe_takes_the_first_clockwise_of_two_equally_near_ring_spots
    local closer = best_x == nil or d < best_d
    -- rule: A hooked foe is set down on the clear ring spot beside the captain nearest the point it
    --   is reeled from (ties. Here: `closer and og.spawn_spot_clear`.
    -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
    -- tests: KitCaptain.hook_reels_a_foe_in_from_the_west
    -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
    -- tests: SemGatesProbe.a_reeled_foe_takes_the_first_clockwise_of_two_equally_near_ring_spots
    if closer and og.spawn_spot_clear(mover, x, y, anchor:floor()) then
      best_x = x
      best_y = y
      best_d = d
    end
  end
  return best_x, best_y
end

-- The captain has a live hook blade out. Summoned effects live on the
-- oblist (the bot gates' library scans it, order and family both).
local function live_blade(self)
  return G.owned(self, C.ORDER_FX, FX_HOOK_BLADE) > 0
end

-- The captain's live war banner, or nil. Weapons live on the weaplist.
local function live_banner(self)
  local weaps = og.weaplist()
  -- rule: The captain's standing banner is his own live war banner (a fallen one or another
  --   captain's does not count).
  -- tests: KitCaptain.second_banner_strikes_the_first_quietly
  -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
  -- tests: KitCaptain.ai_banner_plants_among_friends_and_calls_the_warband_when_pressed
  -- tests: SemCaptain.the_captains_banner_is_his_own_standing_one
  for i = 1, #weaps do
    local w = weaps[i]
    -- rule: The captain's standing banner is his own live war banner (a fallen one or another
    --   captain's does not count).
    -- tests: KitCaptain.second_banner_strikes_the_first_quietly
    -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
    -- tests: KitCaptain.ai_banner_plants_among_friends_and_calls_the_warband_when_pressed
    -- tests: SemCaptain.the_captains_banner_is_his_own_standing_one
    if w:family() == WEAPON_WAR_BANNER
        and w:dead() == 0
        and w:owner() == self then
      return w
    end
  end
  return nil
end

-- This captain's live warband grunts: orcs it summoned.
local function own_grunts(self)
  local obs = og.oblist()
  local grunts = {}
  -- rule: The captain's warband is the live orcs he summoned; other orcs and dead grunts do not
  --   count toward the cap or the 'no grunt left' gate.
  -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
  -- tests: KitCaptain.ai_banner_plants_among_friends_and_calls_the_warband_when_pressed
  -- tests: KitCaptain.grunts_turn_to_the_new_banner
  -- tests: SemCaptain.the_warband_counts_only_his_own_live_grunts
  for i = 1, #obs do
    local ob = obs[i]
    -- rule: The captain's warband is the live orcs he summoned; other orcs and dead grunts do not
    --   count toward the cap or the 'no grunt left' gate.
    -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
    -- tests: KitCaptain.ai_banner_plants_among_friends_and_calls_the_warband_when_pressed
    -- tests: KitCaptain.grunts_turn_to_the_new_banner
    -- tests: SemCaptain.the_warband_counts_only_his_own_live_grunts
    if ob:order() == C.ORDER_LIVING
        and ob:family() == LIVING_ORC
        and ob:dead() == 0
        and ob:owner() == self then
      grunts[#grunts + 1] = ob
    end
  end
  return grunts
end

-- How many warband grunts this captain has alive.
local function live_grunts(self)
  return #own_grunts(self)
end

-- A blood stain the captain stands on (within the squared reach), on its
-- floor, or nil (find_nearest_blood answers only the captain's floor and is
-- team-blind; the bot gates' library adds the reach).
local function corpse_underfoot(self, reach_sq)
  return G.corpse_underfoot(self, reach_sq)
end

-- ------------------------------------------- slots 1 and 2: HOWL, EAT CORPSE

-- The orc's own two specials, each on its own slot as the orc has them, so
-- an orc promoted at level 5 keeps both. HOWL rests between casts as the
-- orc's does (howl_rest on the captain's tuning block).
M.howl = orc.howl
M.eat_corpse = orc.eat_corpse

-- ------------------------------------------------ slot 3: HOOK BLADE / FAN

local function hook_blade(self)
  -- rule: A captain can have only one hook blade out: a second cast is refused BLADE ALREADY OUT,
  --   free.
  -- tests: KitCaptain.hook_spirals_out_and_back
  if live_blade(self) then
    return false, "BLADE ALREADY OUT"
  end
  local t = og.tuning(self)
  -- og.summon_configured applies these keys in its own fixed order:
  -- ani_type, lifetime, hp_add, max_hp_from_hp, damage_add
  local blade = og.summon_configured(self, "fx", FX_HOOK_BLADE, {
    ani_type = 1,
    lifetime = t.hook_ticks,
    hp_add = t.hook_hp,
    max_hp_from_hp = true,
    damage_add = self:damage(),
  })
  -- plumbing: summon_configured nil (world full): 'COULD NOT MAKE A BLADE'
  if not blade then
    return false, "COULD NOT MAKE A BLADE"
  end
  -- Not on the hook yet (lib/effect_hook_blade.lua reads 1 as reeling).
  blade:set_lineofsight(0)
  og.emit_positional_sound(self, C.SOUND_FWIP)
  return true
end

-- Three plain knives: the facing and one point either side of it.
local function knife_fan(self)
  -- rule: KNIFE FAN throws three plain knives (the facing and one point either side) for its own
  --   price only, and refuses a busy captain for free. A knife whose launch pad a live body blocks
  --   becomes a melee attack() on that body instead (walker::fire() melee arm; the return is
  --   unchecked), and the fan goes on.
  -- tests: KitCaptain.knife_fan_throws_three
  -- tests: SemLibsCaptain.a_knife_fan_point_blank_strikes_the_body_on_its_pad_and_throws_the_other_two
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local facing = facing_index(self)
  -- The aim is kept whole (a diagonal walk leaves it fractional) and put
  -- back exactly after the three throws.
  local saved_aim_x = self:lastx()
  local saved_aim_y = self:lasty()
  -- fire() charges the weapon cost per knife; the special's price covers it.
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, 3 * self:s_weapon_cost())
  for spread = -1, 1 do
    local dir = og.mod(facing + spread + 8, 8) + 1
    self:set_lastx(DIR_X[dir])
    self:set_lasty(DIR_Y[dir])
    -- each fire() draws its own waver from the gameplay stream
    self:fire()
  end
  self:set_lastx(saved_aim_x)
  self:set_lasty(saved_aim_y)
  return true
end

function M.hook_or_fan(self)
  -- rule: Slot 3 is HOOK BLADE unshifted and KNIFE FAN with Shift held.
  -- tests: KitCaptain.knife_fan_throws_three, KitCaptain.hook_spirals_out_and_back
  if self:shifter_down() ~= 0 then
    return knife_fan(self)
  end
  return hook_blade(self)
end

-- The blade has caught `foe`: cut it and, if it lives, hook it: the foe
-- becomes the blade's leader, the blade is marked as reeling (lineofsight
-- 1), the foe is frozen for the whole reel and hook_stun ticks more, and
-- the blade's lifetime becomes the reel's budget; lib/effect_hook_blade.lua
-- then reels the foe in, one step a tick. Answers true when the foe is on the hook,
-- false when the cut killed it (the blade then goes).
function M.snag(blade, owner, foe)
  local t = og.tuning(owner)
  -- attack() looks pure but draws from the gameplay stream.
  blade:attack(foe)
  -- rule: The hook blade cuts the foe it catches; a foe the cut kills is not reeled (the blade
  --   goes), a survivor is hooked: frozen for reel+stun ticks, made to target the captain, and
  --   reeled in.
  -- tests: KitCaptain.hook_kills_a_dying_foe_without_reeling_it
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: KitCaptain.a_late_snag_still_reels_home
  if foe:dead() ~= 0 then
    return false
  end
  blade:set_leader(foe)
  blade:set_lineofsight(1)
  blade.lifetime = t.hook_reel_max
  foe:add_frozen_stun(t.hook_reel_max + t.hook_stun)
  foe:set_foe(owner)
  og.emit_positional_sound(blade, C.SOUND_CLANG)
  return true
end

-- ------------------------------------------- slot 4: WAR BANNER / WARBAND

local function war_banner(self)
  local t = og.tuning(self)
  local corpse = corpse_underfoot(self, t.banner_plant_sq)
  -- rule: WAR BANNER needs a corpse under the captain (his floor, within reach) and uses it up;
  --   planting a second strikes his first quietly and turns his marching grunts to the new banner.
  -- tests: KitCaptain.banner_needs_a_corpse_and_consumes_it
  -- tests: KitCaptain.second_banner_strikes_the_first_quietly
  -- tests: KitCaptain.grunts_turn_to_the_new_banner
  -- tests: KitCaptain.refused_second_plant_keeps_the_first
  -- tests: SemLibsCaptain.planting_a_war_banner_roars_once_and_stands_it_on_the_corpse
  if not corpse then
    return false, "NO CORPSE TO PLANT ON"
  end
  -- A banner of his already standing comes down once the new one is up
  -- (found before the new one exists, so the scan cannot answer it).
  local old = live_banner(self)
  local banner = og.add_weap_ob("weapon", WEAPON_WAR_BANNER)
  -- plumbing: add_weap_ob nil (world full): 'COULD NOT RAISE BANNER'
  if not banner then
    return false, "COULD NOT RAISE BANNER"
  end
  banner:set_owner(self)
  banner.team = self.team
  banner.level = self.level
  banner:set_floor(corpse:floor())
  banner:center_on(corpse)
  banner.damage = 0
  banner.max_hp = t.banner_hp_base + self.level * t.banner_hp_per_level
  banner.hp = banner.max_hp
  banner.lifetime = t.banner_ticks
  banner:set_act_type(C.ACT_SIT)
  -- The fallen one's head goes on the pole: the corpse is used up.
  corpse.dead = 1
  corpse:death()
  -- rule: WAR BANNER needs a corpse under the captain (his floor, within reach) and uses it up.
  --   Here: `old`.
  -- tests: KitCaptain.banner_needs_a_corpse_and_consumes_it
  -- tests: KitCaptain.second_banner_strikes_the_first_quietly
  -- tests: KitCaptain.grunts_turn_to_the_new_banner
  -- tests: KitCaptain.refused_second_plant_keeps_the_first
  -- tests: SemLibsCaptain.planting_a_war_banner_roars_once_and_stands_it_on_the_corpse
  if old then
    -- Struck quietly: lib/weapon_banner.lua reads the leader mark and
    -- flashes without the fall's boom and notice.
    old:set_leader(self)
    old.dead = 1
    old:death()
    -- The captain moved his rally point: grunts marching to the old banner
    -- turn to the new one.
    local cx = banner:xpos() + og.div(banner:sizex(), 2)
    local cy = banner:ypos() + og.div(banner:sizey(), 2)
    local grunts = own_grunts(self)
    -- rule: WAR BANNER needs a corpse under the captain (his floor, within reach) and uses it up.
    --   Here: `for i = 1, #grunts`.
    -- tests: KitCaptain.banner_needs_a_corpse_and_consumes_it
    -- tests: KitCaptain.second_banner_strikes_the_first_quietly
    -- tests: KitCaptain.grunts_turn_to_the_new_banner
    -- tests: KitCaptain.refused_second_plant_keeps_the_first
    -- tests: SemLibsCaptain.planting_a_war_banner_roars_once_and_stands_it_on_the_corpse
    for i = 1, #grunts do
      grunts[i]:s_clear_command()
      grunts[i]:s_add_command(C.COMMAND_GOTO, 200, cx, cy)
    end
  end
  og.emit_positional_sound(self, C.SOUND_ROAR)
  og.emit_notification(og.entity_display_name(self, "Orc Captain")
    .. " raises a war banner!")
  return true
end

-- True when a captain-sized box at (sx, sy) overlaps the captain himself.
-- The clear-ground probe never counts the walker it measures with, and the
-- scan runs along the captain's own row or column, so without this a grunt
-- could be summoned inside him. Same edges as the probe: touching is clear.
local function on_the_captain(self, sx, sy)
  local x = self:xpos()
  local y = self:ypos()
  local w = self:sizex()
  local h = self:sizey()
  -- rule: A warband grunt is never summoned overlapping its captain (touching edges is clear).
  -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
  -- tests: SemLibsCaptain.a_warband_tile_touching_the_captains_edge_is_clear_ground
  if sx + w <= x then
    return false
  end
  if sx >= x + w then
    return false
  end
  if sy + h <= y then
    return false
  end
  return sy < y + h
end

-- Where the grunts come in: on the map edge nearest the captain, scanning
-- inward a tile at a time for clear ground. Answers up to `want` spots as a
-- flat x, y, x, y ... array (the captain is the size proxy: grunts are orcs,
-- 16x16 like it).
local function edge_spots(self, want, scan_tiles)
  local pixmaxx, pixmaxy = og.map_size()
  local x = self:xpos()
  local y = self:ypos()
  local gaps = { x, pixmaxx - x, y, pixmaxy - y }
  local side = 1
  -- rule: The warband comes in on the map edge nearest the captain (ties to west, east, north,
  --   south order), scanning inward a tile at a time along his row or column for clear ground, up
  --   to the count wanted.
  -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
  -- tests: SemLibsCaptain.a_warband_with_equal_edge_gaps_comes_in_west_east_north_south_order
  for i = 2, 4 do
    -- rule: The warband comes in on the map edge nearest the captain (ties to west, east, north,
    --   south order), scanning inward a tile at a time along his row or column for clear ground, up
    --   to the count wanted.
    -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
    -- tests: SemLibsCaptain.a_warband_with_equal_edge_gaps_comes_in_west_east_north_south_order
    if gaps[i] < gaps[side] then
      side = i
    end
  end
  local spots = {}
  for k = 0, scan_tiles - 1 do
    local step = k * C.GRID_SIZE
    local sx = x
    local sy = y
    -- rule: The warband comes in on the map edge nearest the captain (ties to west, east, north,
    --   south order), scanning inward a tile at a time along his row or column for clear ground, up
    --   to the count wanted.
    -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
    -- tests: SemLibsCaptain.a_warband_with_equal_edge_gaps_comes_in_west_east_north_south_order
    if side == 1 then
      sx = step
    elseif side == 2 then
      sx = pixmaxx - C.GRID_SIZE - step
    elseif side == 3 then
      sy = step
    else
      sy = pixmaxy - C.GRID_SIZE - step
    end
    local room = #spots < want * 2
    -- rule: The warband comes in on the map edge nearest the captain (ties to west, east, north,
    --   south order), scanning inward a tile at a time along his row or column for clear ground, up
    --   to the count wanted.
    -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
    -- tests: SemLibsCaptain.a_warband_with_equal_edge_gaps_comes_in_west_east_north_south_order
    local clear = room and not on_the_captain(self, sx, sy)
    -- rule: The warband comes in on the map edge nearest the captain (ties to west, east, north,
    --   south order), scanning inward a tile at a time along his row or column for clear ground, up
    --   to the count wanted.
    -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
    -- tests: SemLibsCaptain.a_warband_with_equal_edge_gaps_comes_in_west_east_north_south_order
    if clear and og.spawn_spot_clear(self, sx, sy) then
      spots[#spots + 1] = sx
      spots[#spots + 1] = sy
    end
  end
  return spots
end

local function warband(self)
  local t = og.tuning(self)
  local here = live_grunts(self)
  -- rule: WARBAND summons warband_base (+1 per 3 levels above 10) fearless grunts of a third the
  --   captain's level (min 1), never past the cap, who run to his standing banner's centre or else
  --   to him; a full warband or a walled edge is refused for free.
  -- tests: KitCaptain.warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner
  -- tests: KitCaptain.warband_has_its_own_price_and_a_short_pool_is_refused
  -- tests: SemLibsCaptain.a_warband_with_no_banner_runs_to_the_captains_spot
  if here >= t.warband_cap then
    return false, "WARBAND ALREADY HERE"
  end
  local count = t.warband_base
    + og.div(og.max(0, self.level - 10), t.warband_per_levels)
  count = og.min(count, t.warband_cap - here)
  local spots = edge_spots(self, count, t.warband_scan_tiles)
  if #spots == 0 then
    return false, "NO ROOM AT THE EDGE"
  end
  -- They run to the centre of a standing banner, else to the captain.
  local goal_x = self:xpos()
  local goal_y = self:ypos()
  local rally = live_banner(self)
  if rally then
    goal_x = rally:xpos() + og.div(rally:sizex(), 2)
    goal_y = rally:ypos() + og.div(rally:sizey(), 2)
  end
  for i = 1, #spots, 2 do
    -- Owned and timed like a summoned elemental: a grunt dies with its
    -- captain, so it is fearless exactly while the captain lives.
    local grunt = self:do_summon(LIVING_ORC, t.warband_lifetime)
    -- plumbing: do_summon nil (world full): that grunt is skipped
    if grunt then
      grunt.team = self.team
      grunt.level = og.max(1, og.div(self.level, 3))
      grunt:set_difficulty(grunt.level)
      grunt:setxy(spots[i], spots[i + 1])
      grunt:set_kit_state(C.KIT_FEARLESS)
      grunt:set_leader(self)
      grunt:s_add_command(C.COMMAND_GOTO, 200, goal_x, goal_y)
    end
  end
  og.emit_positional_sound(self, C.SOUND_ROAR)
  return true
end

function M.banner_or_warband(self)
  -- rule: Slot 4 is WAR BANNER unshifted and WARBAND with Shift held (separate prices 80 / 120).
  -- tests: KitCaptain.warband_has_its_own_price_and_a_short_pool_is_refused
  if self:shifter_down() ~= 0 then
    return warband(self)
  end
  return war_banner(self)
end

-- --------------------------------------------------------------- bot gates
-- Every gate starts with the setting: with New Specials off the captain has
-- no specials, and a gate that answered false would skip the engine's
-- following rng(3) draw (stats.cpp `check_special() && !rng(3)`), so it
-- answers true, exactly as no gate at all. Ranges are code constants (see
-- lib/ai.lua on per-call tuning reads in gates).

-- Howl when a body the yell can stun (a living foe or a generator: a
-- stunned tent stops spawning) is inside the yell, unless HOWL is resting.
-- The orc's classic foe search within 130 runs first for its draw and its
-- foe; the yell itself reaches 160 px plus 20 a level. HOWL has no
-- alternate, so the shift coin the engine drew is left alone.
M.ai_howl = orc.howl_gate(function(self)
  -- rule: A bot captain howls when a body the yell can stun (living foe or generator) is inside
  --   160+20/level px and he is not busy; the orc's classic within-130 foe search runs first for
  --   its draw; HOWL has no alternate so the coin is left alone. With New Specials off the gate
  --   answers true without drawing.
  -- tests: KitCaptain.ai_howl_fires_when_a_foe_is_near, KitCaptain.ai_off_answers_the_classic_gate
  -- tests: GameplayFixesBots.captain_howl_wants_a_target_inside_the_yell
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  if og.match_setting("new_specials") == 0 then
    return true
  end
  og.check_special_ai_distance(self, 130)
  -- The howl refuses a busy captain (orc_specials.yell); a kit gate has no
  -- classic answer to keep, so the guard holds whatever the Gameplay Fixes
  -- setting.
  if lc.is_busy(self) then
    return false
  end
  return G.targets_within(self, 1, og.combat.yell_radius(self.level))
end)

-- Eat when below 60 % health and standing on a corpse; otherwise hold. (The
-- orc's own bot tries its meal whenever a foe is within 130 and lets the
-- cast refuse; the captain keeps the stricter rule its shifted howl had.)
function M.ai_eat(self)
  -- rule: A bot captain eats only below 60 % health (whole-number compare; exactly 60 % is not
  --   hurt) and standing on a corpse (squared centre distance <= 24).
  -- tests: KitCaptain.ai_eat_fires_when_hurt_on_a_corpse
  -- tests: KitCaptain.ai_off_answers_the_classic_gate
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- Below 60 %, from whole numbers: hp and max_hp are C++ floats, one
  -- float multiply each side, and no decimal fraction to round.
  local hurt = og.fmul(self.hp, 5) < og.fmul(self.max_hp, 3)
  if not hurt then
    return false
  end
  return corpse_underfoot(self, 24) ~= nil
end

-- HOOK BLADE's reach (the blade spirals out to hook_reach, 60 px) and how
-- far the gate looks for the fan's foes (past any knife's flight).
local HOOK_GATE_RANGE = 60
local FAN_GATE_SCAN = 200
-- The knife's table step and range (core:knife), for the fan's flight.
local KNIFE_STEP = 5
local KNIFE_LOS = 7

-- The living foes on the fan's three lines inside the knife's flight. The
-- fan flies along facing_index (curdir first, as knife_fan throws it), not
-- along the fire heading G.fan_faces reads.
local function foes_in_fan(self)
  local foes = G.living_foes(self, FAN_GATE_SCAN, true)
  local facing = facing_index(self)
  local n = 0
  -- rule: The knife fan's foes are living foes on its floor on its three lines (along facing_index,
  --   curdir first) and inside a knife's flight.
  -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
  -- tests: SemLibsCaptainGates.a_bot_captain_counts_no_foe_past_the_knives_flight
  for i = 1, #foes do
    -- rule: The knife fan's foes are living foes on its floor on its three lines (along
    --   facing_index, curdir first) and inside a knife's flight.
    -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
    -- tests: SemLibsCaptainGates.a_bot_captain_counts_no_foe_past_the_knives_flight
    if G.fan_covers(self, facing, foes[i]) then
      -- rule: The knife fan's foes are living foes on its floor on its three lines (along
      --   facing_index, curdir first) and inside a knife's flight.
      -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
      -- tests: SemLibsCaptainGates.a_bot_captain_counts_no_foe_past_the_knives_flight
      if G.in_flight(self, foes[i], KNIFE_STEP, KNIFE_LOS) then
        n = n + 1
      end
    end
  end
  return n
end

-- HOOK BLADE: a living foe within the blade's reach, no blade out, and the
-- 40 it costs (the engine keeps the slot for the fan's 30 and does not
-- check again). KNIFE FAN: two living foes on its three lines within the
-- knives' flight.
function M.ai_hook(self)
  -- rule: A bot captain hooks (shift 0) a living foe within the blade's 60 px reach when no blade
  --   is out and he can pay 40; else fans (shift 1) when two living foes stand on the fan's lines
  --   within knife flight and he is not busy; else holds. Off: true, coin untouched.
  -- tests: KitCaptain.ai_hook_fires_when_a_foe_is_near_and_fans_into_a_crowd
  -- tests: GameplayFixesBots.captain_hook_wants_a_foe_at_the_blades_reach_and_the_price
  -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if G.foes_within(self, 1, HOOK_GATE_RANGE, true) then
    -- rule: A bot captain hooks (shift 0) a living foe within the blade's 60 px reach when no blade
    --   is out and he can pay 40. Here: `not live_blade`.
    -- tests: KitCaptain.ai_hook_fires_when_a_foe_is_near_and_fans_into_a_crowd
    -- tests: GameplayFixesBots.captain_hook_wants_a_foe_at_the_blades_reach_and_the_price
    -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    if not live_blade(self) then
      -- rule: A bot captain hooks (shift 0) a living foe within the blade's 60 px reach when no
      --   blade is out and he can pay 40. Here: `can_pay(3,false)`.
      -- tests: KitCaptain.ai_hook_fires_when_a_foe_is_near_and_fans_into_a_crowd
      -- tests: GameplayFixesBots.captain_hook_wants_a_foe_at_the_blades_reach_and_the_price
      -- tests: GameplayFixesBots.captain_fan_wants_two_foes_on_its_three_lines
      -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
      if G.can_pay(self, 3, false) then
        self:set_shifter_down(0)
        return true
      end
    end
  end
  -- The fan refuses a busy captain (knife_fan), whatever the Gameplay Fixes
  -- setting.
  if not lc.is_busy(self) and foes_in_fan(self) >= 2 then
    self:set_shifter_down(1)
    return true
  end
  return false
end

-- Plant a banner on a corpse with friends around, a foe within 120 to
-- fight and no banner standing; call the warband when foes press, no
-- grunt is left and he can pay.
function M.ai_banner(self)
  -- rule: A bot captain plants a banner (shift 0) on a corpse with two or more friends within 96, a
  --   living foe within 120 and no banner of his standing; else calls the warband (shift 1) when
  --   two living foes press within 120, no grunt of his lives, and he can pay WARBAND's 120; else
  --   holds.
  -- tests: KitCaptain.ai_banner_plants_among_friends_and_calls_the_warband_when_pressed
  -- tests: KitCaptain.ai_banner_holds_the_warband_it_cannot_pay
  -- tests: KitCaptain.bot_captain_calls_the_warband_only_when_it_can_pay
  -- tests: GameplayFixesBots.captain_banner_wants_a_foe_near_the_flag
  -- tests: SemLibsCaptainGates.a_bot_captain_counts_no_tent_among_the_friends_round_a_banner
  -- deferred: #475 triaged bug #475: the friend count has no floor filter; the tents arm
  --   (find_friends_in_range is Living-only: tents are no friends) is asserted
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local _, allies = og.find_friends_in_range("ob", 96, self)
  local plant = allies >= 2 and corpse_underfoot(self, 1024) ~= nil
  if plant then
    -- The flag heals, holds and frightens where a fight is.
    plant = G.foes_within(self, 1, 120, true)
  end
  if plant and not live_banner(self) then
    self:set_shifter_down(0)
    return true
  end
  local pressed = G.foes_within(self, 2, 120, true)
  if pressed and live_grunts(self) == 0
      and G.can_pay(self, 4, true) then
    self:set_shifter_down(1)
    return true
  end
  return false
end

-- The declarations that reference this module (packs/core/families/):
--   core:orc_captain  every specials entry's cast and ai
--   core:hook_blade   (lib/effect_hook_blade.lua calls M.snag and M.ring_spot)
return M
