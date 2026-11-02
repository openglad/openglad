-- core lib: kit_skeleton — the skeleton's New Specials: DIG IN, BONE WALL / BONE STORM, REASSEMBLE / LEGION, their bot gates and the LEGION kill hook (cookbook: docs/lua-classpacks-design.md §3).
--
-- Numbers a special shares with its body live in the skeleton's `tuning`
-- block (read through og.tuning on the skeleton). The bot gates' ranges are
-- code constants: a gate runs on every bot roll (lib/ai.lua says why).

local C = og.C
local G = og.use("gates")
local lc = og.use("living_common")
local km = og.use("kit_marker")

local LIVING_SKELETON = assert(og.family_id("living", "core:skeleton"))
local WEAP_BONE = assert(og.family_id("weapon", "core:bone"))
local WEAP_BONE_WALL = assert(og.family_id("weapon", "core:bone_wall"))
local FX_FLASH = assert(og.family_id("fx", "core:flash"))

local M = {}

-- BONE STORM's own price (the declaration's `alternate.mp_cost` reads it
-- too, so the bot gate and the cast gate cannot disagree).
M.STORM_COST = 45

-- The sink shows the skeleton's going-under frames 24, 25, 26, 27, one per
-- tick, set by the burrow itself; it goes under on the tick after 27. The
-- frames are not played as the tele-out row: that row's end is TUNNEL, and a
-- seated hero's animation is advanced twice a tick (once with its input,
-- once when it acts), which reached the end before the burrow looked and
-- hopped the hero away. The skeleton is held on the grow row instead (a
-- non-walk row keeps a bot from walking or fighting while it sinks, and the
-- grow row's end is only "back to walking"); the burrow rewinds it every
-- tick, so it never finishes, and if anything else takes over the
-- animation the skeleton goes under at once.
local SINK_FRAMES = { 24, 25, 26, 27 }
local SINK_TICKS = 4
local SUNK_FRAME = 27

-- DIG IN's slot in the skeleton's specials table.
local DIG_IN_SLOT = 2

-- Bot gate ranges (code constants, see the header).
local DIG_FOES_RANGE = 80
local WALL_FOE_RANGE = 60
-- BONE STORM: foes within STORM_WALL_REACH px of a standing segment count;
-- the scan around the skeleton reaches any wall it can hold.
local STORM_WALL_REACH = 60
local STORM_FOE_SCAN = 400
local REASSEMBLE_FOE_RANGE = 60
local LEGION_FOES_RANGE = 100

-- FACE_UP .. FACE_UP_LEFT (0 .. 7) as unit steps.
local FACING_X = { 0, 1, 1, 1, 0, -1, -1, -1 }
local FACING_Y = { -1, -1, 0, 1, 1, 1, 0, -1 }

-- The skeleton's facing as a unit step: the 8-way table, else the signs of
-- the last heading (a sentinel curdir), else up.
local function facing_of(self)
  local dir = self:curdir()
  -- rule: BONE WALL rises across the skeleton's facing: its 8-way curdir when it has one, else the
  --   signs of its last heading, else straight up.
  -- tests: KitSkeleton.bone_wall_follows_the_facing
  if dir >= 0 then
    -- rule: Part of the rule above that begins "BONE WALL rises across the skeleton's facing: its
    --   8-way ..."
    -- tests: KitSkeleton.bone_wall_follows_the_facing
    if dir < 8 then
      return FACING_X[dir + 1], FACING_Y[dir + 1]
    end
  end
  local fx = og.sign(self:lastx())
  local fy = og.sign(self:lasty())
  -- rule: Part of the rule above that begins "BONE WALL rises across the skeleton's facing: its
  --   8-way ..."
  -- tests: KitSkeleton.bone_wall_follows_the_facing
  if fx == 0 then
    -- rule: Part of the rule above that begins "BONE WALL rises across the skeleton's facing: its
    --   8-way ..."
    -- tests: KitSkeleton.bone_wall_follows_the_facing
    if fy == 0 then
      return 0, -1
    end
  end
  return fx, fy
end

-- A flash at the walker's spot and floor: the visible tell of a cast that
-- leaves no object behind.
local function flash_at(self)
  local flash = og.add_ob("fx", FX_FLASH)
  -- plumbing: flash_at: add_ob nil guard (world full) - the flash is cosmetic
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(self:floor())
    flash:center_on(self)
  end
end

-- The ticks a drain that takes `drain` mana every `pulse` ticks can still
-- pay for: a pulse for every `drain` left in the pool, plus the ticks to
-- the next pulse (the full pulse right after one). DIG IN and REASSEMBLE
-- clamp their marker's clock to this, so the countdown a player sees is
-- the tick the effect really ends, whether the time or the mana runs out
-- first.
local function ticks_mana_buys(owner, left, drain, pulse)
  local phase = og.mod(left, pulse)
  -- rule: A mana-drained marker's clock (DIG IN burrow, REASSEMBLE ward) is clamped to the tick the
  --   mana actually runs out, so the countdown shown is the real end: pulses the pool can still pay
  --   plus the ticks to the next pulse (a full pulse right after one).
  -- tests: KitSkeleton.burrow_surfaces_when_the_mana_runs_dry
  -- tests: KitSkeleton.ward_fades_when_the_mana_runs_dry
  if phase == 0 then
    phase = pulse
  end
  return og.div(og.trunc(owner.magicpoints), drain) * pulse + phase
end

-- ---------------------------------------------------------------------------
-- DIG IN
-- ---------------------------------------------------------------------------

-- The sink is channelled (KIT_CHANNEL): a press while the skeleton goes
-- down is free, so the held key that started the dig meets the latch, not
-- a missing-mana cue. Every way out of the dig clears the mark.
local function stop_channel(owner)
  owner:set_kit_state(owner:kit_state() & ~C.KIT_CHANNEL)
end

-- A foe that steps on a buried skeleton: a living on its floor that walks
-- (a flyer passes over the burrow).
local function walks_over(owner, foe)
  -- rule: Only a foe that walks over the burrow wakes a buried skeleton: a living on the same
  --   floor, not a natural flyer and not under a flight potion; generators and foes on other floors
  --   pass over unnoticed.
  -- tests: KitSkeleton.dig_in_ignores_flyers_and_generators_over_the_burrow
  -- tests: KitSkeleton.dig_in_on_floor_one_stays_on_floor_one
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  if foe:order() ~= C.ORDER_LIVING then
    return false
  end
  -- rule: Part of the rule above that begins "Only a foe that walks over the burrow wakes ..."
  -- tests: KitSkeleton.dig_in_ignores_flyers_and_generators_over_the_burrow
  -- tests: KitSkeleton.dig_in_on_floor_one_stays_on_floor_one
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  if foe:floor() ~= owner:floor() then
    return false
  end
  -- rule: Part of the rule above that begins "Only a foe that walks over the burrow wakes ..."
  -- tests: KitSkeleton.dig_in_ignores_flyers_and_generators_over_the_burrow
  -- tests: KitSkeleton.dig_in_on_floor_one_stays_on_floor_one
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  if foe:s_query_bit_flags(C.BIT_FLYING) then
    return false
  end
  return foe:flight_left() == 0
end

local function foe_on_top(owner, reach)
  local foes = og.find_foes_in_range("ob", reach, owner)
  -- plumbing: foe_on_top iteration over find_foes_in_range; rule in kit_skeleton.lua:112
  for i = 1, #foes do
    local foe = foes[i]
    -- rule: Part of the rule above that begins "Only a foe that walks over the burrow wakes ..."
    -- tests: KitSkeleton.dig_in_ignores_flyers_and_generators_over_the_burrow
    -- tests: KitSkeleton.dig_in_on_floor_one_stays_on_floor_one
    -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
    if walks_over(owner, foe) then
      return foe
    end
  end
  return nil
end

-- Up out of the ground (the grow row), swinging at `foe` when there is one:
-- the weapon leaves toward the foe at once, the way a melee swing does.
local function pop_up(owner, marker, foe)
  km.finish(marker)
  stop_channel(owner)
  owner:set_hidden(0)
  owner.ani_type = C.ANI_TELE_IN
  owner:set_cycle(0)
  -- rule: A skeleton that surfaces under a foe swings at it at once (the weapon leaves toward the
  --   foe); surfacing on a press, on time out or on dry mana throws nothing and keeps no foe.
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  -- tests: KitSkeleton.burrow_surfaces_when_the_mana_runs_dry
  -- tests: KitSkeleton.dig_in_expires_on_its_own
  if foe then
    owner:set_foe(foe)
    owner:set_lastx(og.sign(foe:xpos() - owner:xpos()))
    owner:set_lasty(og.sign(foe:ypos() - owner:ypos()))
    owner:fire()
  end
end

-- Sinking: the skeleton is still visible (and can still be hit) while the
-- burrow shows the sink frames; after the last one, or as soon as anything
-- else took over its animation, it goes under, parked on walk.
local function sink(marker, owner)
  -- rule: DIG IN sinks over four ticks showing frames 24..27 while still visible and hittable, then
  --   goes under (hidden, parked on walk, channel cleared); if anything else takes over the
  --   skeleton's animation it goes under at once.
  -- tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
  -- tests: SemLibsSkeleton.a_sinking_skeleton_whose_animation_is_taken_over_goes_under_at_once
  if owner:ani_type() == C.ANI_TELE_IN then
    local step = km.age(marker)
    -- rule: Part of the rule above that begins "DIG IN sinks over four ticks showing frames 24..27
    --   ..."
    -- tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
    -- tests: SemLibsSkeleton.a_sinking_skeleton_whose_animation_is_taken_over_goes_under_at_once
    if step < SINK_TICKS then
      owner:set_frame(SINK_FRAMES[step + 1])
      owner:set_cycle(0)
      return
    end
  end
  owner:set_frame(SUNK_FRAME)
  owner.ani_type = C.ANI_WALK
  owner:set_cycle(0)
  stop_channel(owner)
  owner:set_hidden(1)
  -- A ghost that took the body during the sink keeps it visible: the
  -- burrow has nothing left to do.
  -- rule: A body a ghost possessed during the sink cannot be hidden: it stays up and the burrow
  --   ends, leaving no channel.
  -- tests: KitSkeleton.a_body_possessed_mid_sink_stays_up
  -- tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
  if not owner:hidden() then
    km.finish(marker)
  end
end

-- Buried: pay dig_drain mana every dig_drain_pulse ticks to stay down,
-- regenerate, and come up under the first foe that walks over, when the
-- burrow's time is out, or (plainly, nobody to swing at) when the mana
-- runs dry. A buried skeleton regains no mana: the pool sets the stay.
local function buried(marker, owner)
  local t = og.tuning(owner)
  local left = marker:lifetime() - 1
  marker:set_lifetime(left)
  -- rule: Staying buried costs dig_drain MP every dig_drain_pulse ticks; a pulse the pool cannot
  --   pay surfaces the skeleton plainly (no swing), never driving mana below zero.
  -- tests: KitSkeleton.burrow_surfaces_when_the_mana_runs_dry
  -- tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  if og.mod(left, t.dig_drain_pulse) == 0 then
    -- rule: Part of the rule above that begins "Staying buried costs dig_drain MP every
    --   dig_drain_pulse ticks; a ..."
    -- tests: KitSkeleton.burrow_surfaces_when_the_mana_runs_dry
    -- tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
    -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
    if owner.magicpoints < t.dig_drain then
      pop_up(owner, marker, nil)
      return
    end
    -- magicpoints is a C++ float: per-op rounding.
    owner.magicpoints = og.fsub(owner.magicpoints, t.dig_drain)
  end
  left = og.min(left, ticks_mana_buys(owner, left, t.dig_drain, t.dig_drain_pulse))
  marker:set_lifetime(left)
  -- rule: A buried skeleton regenerates dig_regen HP every dig_regen_pulse ticks (clamped to max).
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  -- tests: SemLibsSkeleton.a_buried_skeletons_regen_pulse_never_heals_past_its_maximum
  if og.mod(left, t.dig_regen_pulse) == 0 then
    owner:heal_clamped(t.dig_regen)
  end
  local foe = foe_on_top(owner, t.dig_trigger)
  -- rule: A foe stepping within dig_trigger of a buried skeleton makes it pop up under that foe
  --   (see kit_skeleton.lua:112 for who counts).
  -- tests: KitSkeleton.dig_in_hides_regens_and_pops_when_a_foe_steps_close
  if foe then
    pop_up(owner, marker, foe)
    return
  end
  -- rule: A burrow lasts at most dig_max ticks; when the time runs out the skeleton surfaces
  --   without a target.
  -- tests: KitSkeleton.dig_in_expires_on_its_own
  if left <= 0 then
    pop_up(owner, marker, nil)
  end
end

local function burrow(marker)
  local owner = marker:owner()
  -- plumbing: burrow marker with no owner (owner gone) finishes itself
  if not owner then
    km.finish(marker)
    return
  end
  -- While the burrow lives the special in hand stays DIG IN, so a press
  -- while sinking or buried always reaches it. A Switch Special pressed
  -- during the sink would otherwise leave TUNNEL in hand, and a free press
  -- while buried would start the tunnel from under the floor.
  owner:set_current_special(DIG_IN_SLOT)
  -- rule: While a burrow lives the special in hand is pinned to DIG IN (a Switch Special during
  --   sink or burial cannot leave TUNNEL in hand); a hidden skeleton runs the buried phase, a
  --   visible one the sink.
  -- tests: KitSkeleton.switching_special_while_sinking_keeps_dig_in_in_hand
  if owner:hidden() then
    buried(marker, owner)
    return
  end
  sink(marker, owner)
end

-- DIG IN (15 MP down, free up). The second press is free because a buried
-- skeleton is HIDDEN (the engine charges a hidden walker nothing), and it
-- is latched: a held key re-casts every tick, so the burrow ignores presses
-- for its first kit_latch ticks (silently: a Lua refusal under a held key
-- is never voiced). Staying down costs mana, so the dig refuses unless at
-- least dig_min_pool is left after the price: a burrow that would surface
-- at once is no burrow.
function M.dig_in(self)
  local t = og.tuning(self)
  local burrow_marker = km.find(self, km.BURROW)
  -- rule: A second DIG IN press while dug in surfaces the skeleton for free, but presses in the
  --   first kit_latch ticks are silently refused (DIG IN SETTLING) so a held key does not pop the
  --   burrow.
  -- tests: KitSkeleton.held_special_does_not_pop_the_burrow
  -- tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
  if burrow_marker then
    -- rule: Part of the rule above that begins "A second DIG IN press while dug in surfaces ..."
    -- tests: KitSkeleton.held_special_does_not_pop_the_burrow
    -- tests: KitSkeleton.second_press_pops_up_free_and_costs_nothing
    if km.age(burrow_marker) < t.kit_latch then
      return false, "DIG IN SETTLING"
    end
    pop_up(self, burrow_marker, nil)
    return true
  end
  -- rule: A possessed skeleton body cannot dig in.
  -- tests: KitSkeleton.possessed_body_cannot_dig_in
  -- tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
  if self:possess_link() ~= 0 then
    return false, "POSSESSED BODY"
  end
  -- rule: A skeleton mid-teleport (TUNNEL in progress) cannot dig in (same rule as
  --   living_common.lua:22).
  -- tests: KitSkeleton.possessed_body_cannot_dig_in
  -- tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
  if lc.mid_teleport(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: DIG IN refuses unless at least dig_min_pool MP remain after its price (a burrow that
  --   would surface at once is no burrow); the refusal is free.
  -- tests: KitSkeleton.dig_in_refuses_without_a_pool_to_stay_down
  if lc.spare_mp(self, DIG_IN_SLOT) < t.dig_min_pool then
    return false, "NEED MANA TO STAY DOWN"
  end
  -- plumbing: km.spawn failed (world full): 'COULD NOT DIG IN'; no test reaches it
  if not km.spawn(self, km.BURROW, t.dig_max) then
    return false, "COULD NOT DIG IN"
  end
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  self:set_frame(SINK_FRAMES[1])
  self:set_kit_state(self:kit_state() | C.KIT_CHANNEL)
  return true
end

-- ---------------------------------------------------------------------------
-- BONE WALL / BONE STORM
-- ---------------------------------------------------------------------------

-- One segment centred on (cx, cy), or nil when its spot is not clear (a
-- door, a tree, a body, another wall). The segment is made first so the
-- probe measures its own size.
local function wall_segment(self, t, cx, cy)
  local seg = og.add_weap_ob("weapon", WEAP_BONE_WALL)
  -- plumbing: add_weap_ob nil guard (world full)
  if not seg then
    return nil
  end
  seg:set_floor(self:floor())
  local x = cx - og.div(seg:sizex(), 2)
  local y = cy - og.div(seg:sizey(), 2)
  -- rule: Each of a bone wall's three segments rises only on a clear spot (no door, tree, body or
  --   other wall); a blocked segment is simply not placed.
  -- tests: KitSkeleton.bone_wall_refuses_on_a_door
  -- tests: KitSkeleton.bone_wall_stands_three_segments_blocks_foes_passes_allies_and_landings
  if not og.spawn_spot_clear(seg, x, y) then
    seg.dead = 1
    return nil
  end
  seg:setxy(x, y)
  seg.team = self.team
  seg:set_owner(self)
  seg:set_act_type(C.ACT_SIT)
  seg.hp = t.wall_hp_base + self.level * 4
  seg.max_hp = seg.hp
  seg:set_lifetime(t.wall_ticks)
  seg:set_frame(0)
  return seg
end

-- How many walls a skeleton keeps standing at once: 8 x level / (level + 3),
-- rounded half up (wall_cap_ceiling 8, wall_cap_knee 3). Level 1 keeps 2,
-- level 2 keeps 3, level 3 keeps 4, level 5 keeps 5, level 9 keeps 6,
-- level 13 keeps 7, and from level 45 on it keeps 8, never more.
function M.walls_cap(self)
  local t = og.tuning(self)
  local level = og.max(1, self.level)
  return og.div(2 * t.wall_cap_ceiling * level + level + t.wall_cap_knee,
                2 * (level + t.wall_cap_knee))
end

-- After a new wall goes up, the skeleton's oldest segments fall quietly
-- (no bones, no storm, nothing told) until no more than three per kept
-- wall stand. A wall is three segments that share one lifetime, and the
-- oldest has the least left; the new segments are never taken. The count
-- is of segments, so a wall a foe has chipped holds less room.
local function fell_oldest_walls(self, new_segments)
  local cap = 3 * M.walls_cap(self)
  local function is_new(wall)
    -- rule: A skeleton keeps at most 3*walls_cap(level) of its own bone-wall segments standing (2
    --   walls at L1 .. 7 at L13, 8 max); after a new wall the oldest own segments (least lifetime)
    --   fall quietly, one at a time, until under the cap - never the new ones, never another
    --   skeleton's, no bones, no notification. A chopped wall frees room.
    -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
    -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
    -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
    -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
    for k = 1, #new_segments do
      -- rule: Part of the rule above that begins "A skeleton keeps at most 3*walls_cap(level) of
      --   its own ..."
      -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
      -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
      -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
      -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
      if new_segments[k] == wall then
        return true
      end
    end
    return false
  end
  -- plumbing: fell_oldest_walls outer repeat and weaplist scan; rule in kit_skeleton.lua:309
  while true do
    local standing = 0
    local oldest = nil
    local weapons = og.weaplist()
    -- plumbing: fell_oldest_walls outer repeat and weaplist scan; rule in kit_skeleton.lua:309
    for i = 1, #weapons do
      local wall = weapons[i]
      -- rule: Part of the rule above that begins "A skeleton keeps at most 3*walls_cap(level) of
      --   its own ..."
      -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
      -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
      -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
      -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
      if wall:family() == WEAP_BONE_WALL then
        -- rule: Part of the rule above that begins "A skeleton keeps at most 3*walls_cap(level) of
        --   its own ..."
        -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
        -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
        -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
        -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
        if wall:dead() == 0 then
          -- rule: Part of the rule above that begins "A skeleton keeps at most 3*walls_cap(level)
          --   of its own ..."
          -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
          -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
          -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
          -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
          if wall:owner() == self then
            standing = standing + 1
            -- rule: Part of the rule above that begins "A skeleton keeps at most 3*walls_cap(level)
            --   of its own ..."
            -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
            -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
            -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
            -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
            if not is_new(wall) then
              -- rule: Part of the rule above that begins "A skeleton keeps at most
              --   3*walls_cap(level) of its own ..."
              -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
              -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
              -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
              -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
              if oldest == nil or wall:lifetime() < oldest:lifetime() then
                oldest = wall
              end
            end
          end
        end
      end
    end
    -- rule: Part of the rule above that begins "A skeleton keeps at most 3*walls_cap(level) of its
    --   own ..."
    -- tests: KitSkeleton.bone_walls_kept_follow_the_level_curve
    -- tests: KitSkeleton.level_one_skeleton_keeps_two_walls_and_the_oldest_falls_quietly
    -- tests: KitSkeleton.level_thirteen_skeleton_keeps_seven_walls
    -- tests: KitSkeleton.a_chopped_wall_leaves_room_and_only_the_oldest_needed_fall
    if standing <= cap or oldest == nil then
      return
    end
    oldest.dead = 1
    oldest:death()
  end
end

-- BONE WALL (30 MP): three segments across the facing, wall_gap ahead.
local function bone_wall(self)
  local t = og.tuning(self)
  local fx, fy = facing_of(self)
  local px = -fy
  local py = fx
  local cx = self:xpos() + og.div(self:sizex(), 2) + fx * t.wall_gap
  local cy = self:ypos() + og.div(self:sizey(), 2) + fy * t.wall_gap
  local placed = {}
  -- plumbing: three-segment placement loop; rules in kit_skeleton.lua:275 and :354
  for k = -1, 1 do
    local seg = wall_segment(self, t, cx + px * 16 * k, cy + py * 16 * k)
    -- rule: A bone wall with no segment that fits is refused for free (NO ROOM FOR A WALL); a wall
    --   with any segment placed is cast, fells old walls, and clangs.
    -- tests: KitSkeleton.bone_wall_stands_three_segments_blocks_foes_passes_allies_and_landings
    -- tests: KitSkeleton.bone_wall_refuses_on_a_door
    -- tests: SemLibsSkeleton.a_bone_wall_clangs_once_when_it_stands_and_never_when_refused
    if seg then
      placed[#placed + 1] = seg
    end
  end
  -- rule: Part of the rule above that begins "A bone wall with no segment that fits is ..."
  -- tests: KitSkeleton.bone_wall_stands_three_segments_blocks_foes_passes_allies_and_landings
  -- tests: KitSkeleton.bone_wall_refuses_on_a_door
  -- tests: SemLibsSkeleton.a_bone_wall_clangs_once_when_it_stands_and_never_when_refused
  if #placed == 0 then
    return false, "NO ROOM FOR A WALL"
  end
  fell_oldest_walls(self, placed)
  og.emit_positional_sound(self, C.SOUND_CLANG)
  return true
end

-- Eight bones out of one standing wall, each owned by the skeleton.
local function wall_burst(self, wall, range)
  -- plumbing: wall_burst 3x3 direction loops; rule in kit_skeleton.lua:370
  for i = -1, 1 do
    -- plumbing: wall_burst 3x3 direction loops; rule in kit_skeleton.lua:370
    for j = -1, 1 do
      -- rule: BONE STORM bursts eight bones out of each wall segment, one in each of the eight
      --   directions (none straight down at the centre), each owned by the skeleton on its team
      --   with its damage and storm_range.
      -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
      if i ~= 0 or j ~= 0 then
        local bone = og.add_ob("weapon", WEAP_BONE)
        -- plumbing: add_ob bone nil guard
        if bone then
          bone:set_floor(wall:floor())
          bone:center_on(wall)
          bone.team = self.team
          bone:set_owner(self)
          bone:set_lastx(i * bone:stepsize())
          bone:set_lasty(j * bone:stepsize())
          bone:set_curdir(wall:facing(i, j))
          bone:set_damage(self:damage())
          bone:set_lineofsight(range)
        end
      end
    end
  end
end

local function own_wall_standing(self)
  local weapons = og.weaplist()
  -- plumbing: own_wall_standing weaplist scan; rule in kit_skeleton.lua:392
  for i = 1, #weapons do
    local wall = weapons[i]
    -- rule: BONE STORM needs a standing wall of THIS skeleton; with none (even if another
    --   skeleton's walls stand) it is refused for free.
    -- tests: KitSkeleton.bone_storm_refuses_with_no_wall
    -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
    if wall:family() == WEAP_BONE_WALL then
      -- rule: Part of the rule above that begins "BONE STORM needs a standing wall of THIS
      --   skeleton; ..."
      -- tests: KitSkeleton.bone_storm_refuses_with_no_wall
      -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
      if wall:dead() == 0 then
        -- rule: Part of the rule above that begins "BONE STORM needs a standing wall of THIS
        --   skeleton; ..."
        -- tests: KitSkeleton.bone_storm_refuses_with_no_wall
        -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
        if wall:owner() == self then
          return true
        end
      end
    end
  end
  return false
end

-- BONE STORM (45 MP): every standing wall of THIS skeleton shatters into
-- its own eight-way burst, wherever it stands. Nothing leaves the skeleton
-- itself, and with no wall standing there is nothing to shatter: refused,
-- and a refusal spends nothing.
local function bone_storm(self)
  -- rule: Part of the rule above that begins "BONE STORM needs a standing wall of THIS skeleton;
  --   ..."
  -- tests: KitSkeleton.bone_storm_refuses_with_no_wall
  -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
  if not own_wall_standing(self) then
    return false, "NO WALL STANDING"
  end
  local t = og.tuning(self)
  local weapons = og.weaplist()
  -- plumbing: bone_storm weaplist scan; rule in kit_skeleton.lua:415
  for i = 1, #weapons do
    local wall = weapons[i]
    -- rule: BONE STORM shatters every standing wall of this skeleton (wherever it is) and only
    --   those; nothing leaves the skeleton itself.
    -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
    if wall:family() == WEAP_BONE_WALL then
      -- rule: Part of the rule above that begins "BONE STORM shatters every standing wall of this
      --   skeleton ..."
      -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
      if wall:dead() == 0 then
        -- rule: Part of the rule above that begins "BONE STORM shatters every standing wall of this
        --   skeleton ..."
        -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
        if wall:owner() == self then
          wall_burst(self, wall, t.storm_range)
          wall.dead = 1
          wall:death()
        end
      end
    end
  end
  return true
end

-- While a dig is in progress (the free, channelled sink) no other special
-- of the skeleton's may be cast: the free press belongs to the dig.
local function digging(self)
  return km.find(self, km.BURROW) ~= nil
end

function M.wall_or_storm(self)
  -- rule: While DIG IN is in progress no other skeleton special may be cast (same rule as
  --   kit_skeleton.lua:544).
  -- tests: KitSkeleton.other_specials_wait_for_the_dig
  -- tests: KitSkeleton.bone_wall_stands_three_segments_blocks_foes_passes_allies_and_landings
  if digging(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: Slot 3 is BONE WALL plainly and BONE STORM with the shifter held.
  -- tests: KitSkeleton.bone_storm_bursts_from_every_wall_only
  -- tests: KitSkeleton.bone_wall_stands_three_segments_blocks_foes_passes_allies_and_landings
  if self:shifter_down() ~= 0 then
    return bone_storm(self)
  end
  return bone_wall(self)
end

-- ---------------------------------------------------------------------------
-- REASSEMBLE / LEGION
-- ---------------------------------------------------------------------------

local function warded(self)
  return self:kit_state() & C.KIT_WARD ~= 0
end

-- REASSEMBLE (60 MP): the ward is a bit the engine spends at the top of
-- the next death (the skeleton gets up at a quarter health). A WARD marker
-- keeps the time: the ward lasts ward_ticks, costs ward_drain mana every
-- ward_drain_pulse ticks while it is armed, and fades when either runs out.
local function reassemble(self)
  -- rule: REASSEMBLE cannot be bought twice: an armed ward refuses the cast for free.
  -- tests: KitSkeleton.warded_skeleton_stands_back_up_at_a_quarter
  if warded(self) then
    return false, "ALREADY WARDED"
  end
  -- A ward a death spent this tick can still have its marker: retire it, or
  -- its old clock would end the new ward.
  local stale = km.find(self, km.WARD)
  -- rule: A ward a death spent this tick may still have its marker; re-warding retires the stale
  --   marker first so the new ward keeps its own full clock.
  -- tests: KitSkeleton.a_ward_bought_again_at_once_keeps_its_own_clock
  -- tests: KitSkeleton.warded_skeleton_stands_back_up_at_a_quarter
  if stale then
    km.finish(stale)
  end
  self:set_kit_state(self:kit_state() | C.KIT_WARD)
  -- rule: A ward that cannot get its timekeeping marker (world full) is not armed and costs
  --   nothing.
  -- tests: KitSkeleton.a_full_world_cannot_ward
  if not km.spawn(self, km.WARD, og.tuning(self).ward_ticks) then
    self:set_kit_state(self:kit_state() & ~C.KIT_WARD)
    return false, "COULD NOT WARD"
  end
  flash_at(self)
  return true
end

-- The ward runs out (time or mana): the bit goes, the marker goes, and the
-- fade is told to the skeleton's seat. A bot skeleton has no seat, so its
-- fade is told to everyone, the way the engine tells everyone a skeleton
-- "reassembles!".
local function ward_fades(owner, marker)
  owner:set_kit_state(owner:kit_state() & ~C.KIT_WARD)
  km.finish(marker)
  og.emit_notification(og.entity_display_name(owner, "Skeleton")
    .. "'s ward fades", nil, owner)
end

-- WARD marker. A death that spent the ward cleared the bit: the marker
-- just goes, silently (the skeleton already stood up). Otherwise it counts
-- down, drains, and keeps its clock to what the mana can still buy.
local function ward_window(marker)
  local owner = marker:owner()
  -- plumbing: ward marker with no owner finishes
  if not owner then
    km.finish(marker)
    return
  end
  -- rule: A ward spent by a death clears silently: the marker just goes with no 'ward fades'
  --   notice.
  -- tests: KitSkeleton.a_spent_ward_retires_its_marker
  if not warded(owner) then
    km.finish(marker)
    return
  end
  local t = og.tuning(owner)
  local left = marker:lifetime() - 1
  marker:set_lifetime(left)
  -- rule: An unused ward fades after ward_ticks (360), telling the skeleton's seat (or everyone for
  --   a bot) "<name>'s ward fades".
  -- tests: KitSkeleton.ward_fades_after_its_time
  if left <= 0 then
    ward_fades(owner, marker)
    return
  end
  -- rule: An armed ward costs ward_drain MP every ward_drain_pulse ticks and fades early when a
  --   pulse cannot be paid.
  -- tests: KitSkeleton.ward_fades_when_the_mana_runs_dry
  -- tests: SemLibsSkeleton.an_armed_ward_fades_on_a_pulse_it_cannot_pay
  if og.mod(left, t.ward_drain_pulse) == 0 then
    -- rule: Part of the rule above that begins "An armed ward costs ward_drain MP every
    --   ward_drain_pulse ticks ..."
    -- tests: KitSkeleton.ward_fades_when_the_mana_runs_dry
    -- tests: SemLibsSkeleton.an_armed_ward_fades_on_a_pulse_it_cannot_pay
    if owner.magicpoints < t.ward_drain then
      ward_fades(owner, marker)
      return
    end
    -- magicpoints is a C++ float: per-op rounding.
    owner.magicpoints = og.fsub(owner.magicpoints, t.ward_drain)
  end
  left = og.min(left, ticks_mana_buys(owner, left, t.ward_drain, t.ward_drain_pulse))
  marker:set_lifetime(left)
end

-- LEGION (60 MP): a window during which every foe this skeleton kills
-- rises on its side (the on_kill hook below).
local function legion(self)
  -- rule: LEGION cannot be cast while its window is open; the refusal is free.
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  if km.find(self, km.LEGION) then
    return false, "LEGION ALREADY RISING"
  end
  -- plumbing: LEGION marker spawn failed (world full): 'COULD NOT CALL LEGION'; untested
  if not km.spawn(self, km.LEGION, og.tuning(self).legion_ticks) then
    return false, "COULD NOT CALL LEGION"
  end
  flash_at(self)
  return true
end

local function legion_window(marker)
  local left = marker:lifetime() - 1
  marker:set_lifetime(left)
  -- rule: The LEGION window lasts legion_ticks (300) and closes early when its skeleton is gone.
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  if left <= 0 then
    km.finish(marker)
    return
  end
  -- rule: Part of the rule above that begins "The LEGION window lasts legion_ticks (300) and closes
  --   early ..."
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  if not marker:owner() then
    km.finish(marker)
  end
end

function M.reassemble_or_legion(self)
  -- rule: Same rule as kit_skeleton.lua:435: no REASSEMBLE/LEGION while DIG IN is in progress.
  -- tests: KitSkeleton.other_specials_wait_for_the_dig
  if digging(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: Slot 4 is REASSEMBLE plainly and LEGION with the shifter held.
  -- tests: KitSkeleton.warded_skeleton_stands_back_up_at_a_quarter
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  if self:shifter_down() ~= 0 then
    return legion(self)
  end
  return reassemble(self)
end

-- on_kill: runs after the victim's death() has left its corpse (and a
-- hero's life gem) behind, so the raise can consume them. The setting
-- guard comes first: the skeleton declares the hook whatever the setting.
function M.on_kill(self, victim)
  -- rule: During LEGION, a foe this living skeleton kills (melee, its bone, its explosion) rises on
  --   its side as a skeleton at half the victim's level (min 1), at the victim's spot in the grow
  --   row, consuming the corpse and life gem - but only with New Specials on, never for a dead
  --   killer, never after the window, never for a victim on the killer's own team (a released
  --   possessed ally), never for the undead, and only on a coin of heads (tails leaves corpse and
  --   gem). The coin is drawn only for a kill that could rise.
  -- tests: KitSkeleton.legion_hook_is_inert_when_off
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_leaves_half_its_kills_down
  -- tests: SemLibsSkeleton.a_legion_kill_of_a_level_1_foe_rises_at_level_1
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- rule: Part of the rule above that begins "During LEGION, a foe this living skeleton kills
  --   (melee, ..."
  -- tests: KitSkeleton.legion_hook_is_inert_when_off
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_leaves_half_its_kills_down
  -- tests: SemLibsSkeleton.a_legion_kill_of_a_level_1_foe_rises_at_level_1
  if self:dead() ~= 0 then
    return true
  end
  -- rule: Part of the rule above that begins "During LEGION, a foe this living skeleton kills
  --   (melee, ..."
  -- tests: KitSkeleton.legion_hook_is_inert_when_off
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_leaves_half_its_kills_down
  -- tests: SemLibsSkeleton.a_legion_kill_of_a_level_1_foe_rises_at_level_1
  if not km.find(self, km.LEGION) then
    return true
  end
  -- A team test, not is_friendly (which calls every dead walker a
  -- stranger): a possessed ally is attackable, and its death hands it back
  -- to this side before the hook runs; it is mourned, not raised.
  -- rule: Part of the rule above that begins "During LEGION, a foe this living skeleton kills
  --   (melee, ..."
  -- tests: KitSkeleton.legion_hook_is_inert_when_off
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_leaves_half_its_kills_down
  -- tests: SemLibsSkeleton.a_legion_kill_of_a_level_1_foe_rises_at_level_1
  if victim.team == self.team then
    return true
  end
  -- rule: Part of the rule above that begins "During LEGION, a foe this living skeleton kills
  --   (melee, ..."
  -- tests: KitSkeleton.legion_hook_is_inert_when_off
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_leaves_half_its_kills_down
  -- tests: SemLibsSkeleton.a_legion_kill_of_a_level_1_foe_rises_at_level_1
  if og.family_flag("living", victim:family(), "is_undead") then
    return true
  end
  -- Half the kills rise: a coin, drawn only for a kill that could rise.
  -- Tails leaves the corpse (and a hero's life gem) where it fell.
  -- rule: Part of the rule above that begins "During LEGION, a foe this living skeleton kills
  --   (melee, ..."
  -- tests: KitSkeleton.legion_hook_is_inert_when_off
  -- tests: KitSkeleton.legion_needs_a_living_skeleton_and_a_foe
  -- tests: KitSkeleton.legion_raises_melee_bone_and_explosion_kills_within_the_window
  -- tests: KitSkeleton.legion_leaves_half_its_kills_down
  -- tests: SemLibsSkeleton.a_legion_kill_of_a_level_1_foe_rises_at_level_1
  if og.rand(2) == 0 then
    return true
  end
  local risen = og.add_ob("living", LIVING_SKELETON)
  -- plumbing: add_ob risen skeleton nil (world full)
  if not risen then
    return true
  end
  risen.team = self.team
  risen.level = og.max(1, og.div(victim.level, 2))
  risen:set_difficulty(risen.level)
  risen:set_summoned(true)
  risen:set_floor(victim:floor())
  risen:setxy(victim:xpos(), victim:ypos())
  risen.ani_type = C.ANI_TELE_IN
  risen:set_cycle(0)
  og.scrub_corpse_stain(victim:xpos(), victim:ypos(), victim:floor())
  return true
end

-- ---------------------------------------------------------------------------
-- Bot gates (each slot's `ai =`). The setting guard is every gate's first
-- statement and answers TRUE: check_special() && !rng(3) short-circuits,
-- so a false would skip a draw the setting-off game makes.
-- ---------------------------------------------------------------------------

-- The foe is within STORM_WALL_REACH px of one of this skeleton's
-- standing wall segments (`weapons` is the weapon list).
local function at_own_wall(self, foe, weapons)
  -- plumbing: at_own_wall weapon scan and foes_at_walls foe scan; rule in kit_skeleton.lua:676
  for i = 1, #weapons do
    local wall = weapons[i]
    -- rule: Once a wall of its own stands, a bot skeleton never raises another; it storms (shift 1)
    --   only with 45 MP and at least two living foes (each counted once, scanned within 400) within
    --   60 px of one of its own standing segments.
    -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
    if wall:family() == WEAP_BONE_WALL then
      -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
      -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
      if wall:dead() == 0 then
        -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
        -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
        if wall:owner() == self then
          -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
          -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
          if wall:distance_to_ob(foe) <= STORM_WALL_REACH then
            return true
          end
        end
      end
    end
  end
  return false
end

-- How many living foes stand at this skeleton's walls (each counted once):
-- the bones burst from the walls, not from the skeleton.
local function foes_at_walls(self)
  local foes = G.living_foes(self, STORM_FOE_SCAN, true)
  local weapons = og.weaplist()
  local n = 0
  -- plumbing: at_own_wall weapon scan and foes_at_walls foe scan; rule in kit_skeleton.lua:676
  for i = 1, #foes do
    -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
    -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
    if at_own_wall(self, foes[i], weapons) then
      n = n + 1
    end
  end
  return n
end

-- How many living foes within `range` could rise under LEGION: the undead
-- never do (M.on_kill).
local function foes_that_can_rise(self, range)
  local foes = G.living_foes(self, range, false)
  local n = 0
  -- plumbing: foes_that_can_rise scan; rule in kit_skeleton.lua:719
  for i = 1, #foes do
    -- rule: A bot skeleton at or above half health calls LEGION (shift 1) when no window is open
    --   and at least three living foes that could rise (not undead) stand within 100.
    -- tests: KitSkeleton.ai_reassemble_fires_when_hurt_and_legion_when_crowded
    -- tests: GameplayFixesBots.skeleton_legion_counts_only_foes_that_can_rise
    if not og.family_flag("living", foes[i]:family(), "is_undead") then
      n = n + 1
    end
  end
  return n
end

-- Dig in when hurt and outnumbered (living foes: a tent cannot press).
function M.ai_dig_in(self)
  -- rule: With New Specials off DIG IN's bot gate answers true at once with no draw, no shift write
  --   and no foe acquired, so the classic special roll is untouched (same rule as
  --   kit_skeleton.lua:673, :706).
  -- tests: KitSkeleton.ai_off_answers_the_classic_gate
  -- tests: KitSkeleton.ai_dig_in_fires_when_hurt_and_outnumbered
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- rule: A bot skeleton digs in only when below 60% hp, not possessed, able to stay down
  --   (dig_min_pool after the price), with two living foes within 80.
  -- tests: KitSkeleton.ai_dig_in_fires_when_hurt_and_outnumbered
  -- tests: KitSkeleton.bot_skeleton_digs_in_when_outnumbered
  if self:possess_link() ~= 0 then
    return false
  end
  -- The dig refuses a skeleton in the middle of a teleport (M.dig_in),
  -- whatever the Gameplay Fixes setting.
  -- rule: A bot skeleton mid-teleport (tunnelling) never picks DIG IN (the cast would refuse).
  -- tests: KitSkeleton.ai_dig_in_fires_when_hurt_and_outnumbered
  -- tests: SemLibsSkeleton.a_bot_skeleton_mid_teleport_never_picks_dig_in
  if lc.mid_teleport(self) then
    return false
  end
  -- rule: Part of the rule above that begins "A bot skeleton digs in only when below 60% ..."
  -- tests: KitSkeleton.ai_dig_in_fires_when_hurt_and_outnumbered
  -- tests: KitSkeleton.bot_skeleton_digs_in_when_outnumbered
  if not G.hp_below(self, 6, 10) then
    return false
  end
  -- A bot that cannot stay down does not dig.
  -- rule: Part of the rule above that begins "A bot skeleton digs in only when below 60% ..."
  -- tests: KitSkeleton.ai_dig_in_fires_when_hurt_and_outnumbered
  -- tests: KitSkeleton.bot_skeleton_digs_in_when_outnumbered
  if lc.spare_mp(self, DIG_IN_SLOT) < og.tuning(self).dig_min_pool then
    return false
  end
  return G.foes_within(self, 2, DIG_FOES_RANGE, false)
end

-- A wall when hurt with the nearest foe close and in front, where the wall
-- rises; the storm once a wall stands and the foes gather at the walls.
function M.ai_wall(self)
  -- rule: Same rule as kit_skeleton.lua:649 for BONE WALL's gate.
  -- tests: KitSkeleton.ai_off_answers_the_classic_gate
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
  -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
  if own_wall_standing(self) then
    -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
    -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
    if self.magicpoints < M.STORM_COST then
      return false
    end
    -- rule: Part of the rule above that begins "Once a wall of its own stands, a bot ..."
    -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
    if foes_at_walls(self) < 2 then
      return false
    end
    self:set_shifter_down(1)
    return true
  end
  -- rule: With no wall standing a bot skeleton raises one (shift 0) only when below 70% hp with the
  --   nearest living foe within 60 px on its floor and inside the three-way fan of its facing
  --   (where the wall rises).
  -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
  -- tests: GameplayFixesBots.skeleton_wall_rises_across_the_foes_line
  if not G.hp_below(self, 7, 10) then
    return false
  end
  local foe = G.nearest_living_foe(self, WALL_FOE_RANGE, true)
  -- rule: Part of the rule above that begins "With no wall standing a bot skeleton raises one ..."
  -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
  -- tests: GameplayFixesBots.skeleton_wall_rises_across_the_foes_line
  if not foe then
    return false
  end
  -- The wall rises across facing_of (curdir first, as bone_wall places
  -- it), not along the fire heading G.fan_faces reads.
  local fx, fy = facing_of(self)
  -- rule: Part of the rule above that begins "With no wall standing a bot skeleton raises one ..."
  -- tests: KitSkeleton.ai_wall_fires_when_hurt_with_a_foe_close_and_storms_a_crowd
  -- tests: GameplayFixesBots.skeleton_wall_rises_across_the_foes_line
  if not G.fan_covers(self, self:facing(fx, fy), foe) then
    return false
  end
  self:set_shifter_down(0)
  return true
end

-- The ward when hurt, unwarded and fighting (a living foe within 60); the
-- legion when healthy and crowded by foes that can rise.
function M.ai_reassemble(self)
  -- rule: Same rule as kit_skeleton.lua:649 for REASSEMBLE's gate.
  -- tests: KitSkeleton.ai_off_answers_the_classic_gate
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- rule: A bot skeleton below half health picks the ward (shift 0) only when unwarded and fighting
  --   (a living foe within 60 on its floor); a hurt skeleton never considers LEGION.
  -- tests: KitSkeleton.ai_reassemble_fires_when_hurt_and_legion_when_crowded
  if G.hp_below(self, 1, 2) then
    -- rule: Part of the rule above that begins "A bot skeleton below half health picks the ward
    --   ..."
    -- tests: KitSkeleton.ai_reassemble_fires_when_hurt_and_legion_when_crowded
    if warded(self) then
      return false
    end
    -- rule: Part of the rule above that begins "A bot skeleton below half health picks the ward
    --   ..."
    -- tests: KitSkeleton.ai_reassemble_fires_when_hurt_and_legion_when_crowded
    if not G.foes_within(self, 1, REASSEMBLE_FOE_RANGE, true) then
      return false
    end
    self:set_shifter_down(0)
    return true
  end
  -- rule: Part of the rule above that begins "A bot skeleton at or above half health calls ..."
  -- tests: KitSkeleton.ai_reassemble_fires_when_hurt_and_legion_when_crowded
  -- tests: GameplayFixesBots.skeleton_legion_counts_only_foes_that_can_rise
  if km.find(self, km.LEGION) then
    return false
  end
  -- rule: Part of the rule above that begins "A bot skeleton at or above half health calls ..."
  -- tests: KitSkeleton.ai_reassemble_fires_when_hurt_and_legion_when_crowded
  -- tests: GameplayFixesBots.skeleton_legion_counts_only_foes_that_can_rise
  if foes_that_can_rise(self, LEGION_FOES_RANGE) < 3 then
    return false
  end
  self:set_shifter_down(1)
  return true
end

km.register(km.BURROW, burrow)
km.register(km.LEGION, legion_window)
km.register(km.WARD, ward_window)

-- The declarations that reference this module (packs/core/families/):
--   core:skeleton  specials dig_in / bone_wall / reassemble, on_kill
return M
