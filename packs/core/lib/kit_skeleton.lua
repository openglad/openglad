-- core lib: kit_skeleton — the skeleton's New Specials: DIG IN, BONE WALL / BONE STORM, REASSEMBLE / LEGION, their bot gates and the LEGION kill hook (cookbook: docs/lua-classpacks-design.md §3).
--
-- Numbers a special shares with its body live in the skeleton's `tuning`
-- block (read through og.tuning on the skeleton). The bot gates' ranges are
-- code constants: a gate runs on every bot roll (lib/ai.lua says why).

local C = og.C
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
local STORM_FOES_RANGE = 80
local LEGION_FOES_RANGE = 100

-- FACE_UP .. FACE_UP_LEFT (0 .. 7) as unit steps.
local FACING_X = { 0, 1, 1, 1, 0, -1, -1, -1 }
local FACING_Y = { -1, -1, 0, 1, 1, 1, 0, -1 }

-- hp below num/den of max_hp. hp and max_hp are C++ floats: one float
-- multiply each side keeps the comparison free of decimal fractions.
local function hp_below(self, num, den)
  return og.fmul(self.hp, den) < og.fmul(self.max_hp, num)
end

local function foe_count_within(self, range)
  local _, count = og.find_foes_in_range("ob", range, self)
  return count
end

-- The skeleton's facing as a unit step: the 8-way table, else the signs of
-- the last heading (a sentinel curdir), else up.
local function facing_of(self)
  local dir = self:curdir()
  if dir >= 0 then
    if dir < 8 then
      return FACING_X[dir + 1], FACING_Y[dir + 1]
    end
  end
  local fx = og.sign(self:lastx())
  local fy = og.sign(self:lasty())
  if fx == 0 then
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
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(self:floor())
    flash:center_on(self)
  end
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
  if foe:order() ~= C.ORDER_LIVING then
    return false
  end
  if foe:floor() ~= owner:floor() then
    return false
  end
  if foe:s_query_bit_flags(C.BIT_FLYING) then
    return false
  end
  return foe:flight_left() == 0
end

local function foe_on_top(owner, reach)
  local foes = og.find_foes_in_range("ob", reach, owner)
  for i = 1, #foes do
    local foe = foes[i]
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
  if owner:ani_type() == C.ANI_TELE_IN then
    local step = km.age(marker)
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
  if not owner:hidden() then
    km.finish(marker)
  end
end

-- Buried: regenerate, and come up under the first foe that walks over, or
-- when the burrow's time is out.
local function buried(marker, owner)
  local t = og.tuning(owner)
  local left = marker:lifetime() - 1
  marker:set_lifetime(left)
  if og.mod(left, t.dig_regen_pulse) == 0 then
    owner:heal_clamped(t.dig_regen)
  end
  local foe = foe_on_top(owner, t.dig_trigger)
  if foe then
    pop_up(owner, marker, foe)
    return
  end
  if left <= 0 then
    pop_up(owner, marker, nil)
  end
end

local function burrow(marker)
  local owner = marker:owner()
  if not owner then
    km.finish(marker)
    return
  end
  -- While the burrow lives the special in hand stays DIG IN, so a press
  -- while sinking or buried always reaches it. A Switch Special pressed
  -- during the sink would otherwise leave TUNNEL in hand, and a free press
  -- while buried would start the tunnel from under the floor.
  owner:set_current_special(DIG_IN_SLOT)
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
-- is never voiced).
function M.dig_in(self)
  local t = og.tuning(self)
  local burrow_marker = km.find(self, km.BURROW)
  if burrow_marker then
    if km.age(burrow_marker) < t.kit_latch then
      return false, "DIG IN SETTLING"
    end
    pop_up(self, burrow_marker, nil)
    return true
  end
  if self:possess_link() ~= 0 then
    return false, "POSSESSED BODY"
  end
  if lc.mid_teleport(self) then
    return false, "SPECIAL BUSY"
  end
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
  if not seg then
    return nil
  end
  seg:set_floor(self:floor())
  local x = cx - og.div(seg:sizex(), 2)
  local y = cy - og.div(seg:sizey(), 2)
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

-- BONE WALL (30 MP): three segments across the facing, wall_gap ahead.
local function bone_wall(self)
  local t = og.tuning(self)
  local fx, fy = facing_of(self)
  local px = -fy
  local py = fx
  local cx = self:xpos() + og.div(self:sizex(), 2) + fx * t.wall_gap
  local cy = self:ypos() + og.div(self:sizey(), 2) + fy * t.wall_gap
  local placed = 0
  for k = -1, 1 do
    if wall_segment(self, t, cx + px * 16 * k, cy + py * 16 * k) then
      placed = placed + 1
    end
  end
  if placed == 0 then
    return false, "NO ROOM FOR A WALL"
  end
  og.emit_positional_sound(self, C.SOUND_CLANG)
  return true
end

-- Eight bones out of one standing wall, each owned by the skeleton.
local function wall_burst(self, wall, range)
  for i = -1, 1 do
    for j = -1, 1 do
      if i ~= 0 or j ~= 0 then
        local bone = og.add_ob("weapon", WEAP_BONE)
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

-- The skeleton's own eight-way burst (the elemental's starburst shape).
local function storm_from_skeleton(self)
  -- The aim is saved and restored as whole numbers, the way the
  -- elemental's starburst does it (its C++ original kept the aim in ints).
  local saved_aim_x = og.trunc(self:lastx())
  local saved_aim_y = og.trunc(self:lasty())
  -- magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, 8 * self:s_weapon_cost())
  for i = -1, 1 do
    for j = -1, 1 do
      if i ~= 0 or j ~= 0 then
        self:set_lastx(i)
        self:set_lasty(j)
        self:fire()
      end
    end
  end
  self:set_lastx(saved_aim_x)
  self:set_lasty(saved_aim_y)
end

-- BONE STORM (45 MP): bones from the skeleton, then every standing wall of
-- THIS skeleton shatters into its own burst, wherever it stands.
local function bone_storm(self)
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  storm_from_skeleton(self)
  local weapons = og.weaplist()
  for i = 1, #weapons do
    local wall = weapons[i]
    if wall:family() == WEAP_BONE_WALL then
      if wall:dead() == 0 then
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
  if digging(self) then
    return false, "SPECIAL BUSY"
  end
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
-- the next death (the skeleton gets up at a quarter health).
local function reassemble(self)
  if warded(self) then
    return false, "ALREADY WARDED"
  end
  self:set_kit_state(self:kit_state() | C.KIT_WARD)
  flash_at(self)
  return true
end

-- LEGION (60 MP): a window during which every foe this skeleton kills
-- rises on its side (the on_kill hook below).
local function legion(self)
  if km.find(self, km.LEGION) then
    return false, "LEGION ALREADY RISING"
  end
  if not km.spawn(self, km.LEGION, og.tuning(self).legion_ticks) then
    return false, "COULD NOT CALL LEGION"
  end
  flash_at(self)
  return true
end

local function legion_window(marker)
  local left = marker:lifetime() - 1
  marker:set_lifetime(left)
  if left <= 0 then
    km.finish(marker)
    return
  end
  if not marker:owner() then
    km.finish(marker)
  end
end

function M.reassemble_or_legion(self)
  if digging(self) then
    return false, "SPECIAL BUSY"
  end
  if self:shifter_down() ~= 0 then
    return legion(self)
  end
  return reassemble(self)
end

-- on_kill: runs after the victim's death() has left its corpse (and a
-- hero's life gem) behind, so the raise can consume them. The setting
-- guard comes first: the skeleton declares the hook whatever the setting.
function M.on_kill(self, victim)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if self:dead() ~= 0 then
    return true
  end
  if not km.find(self, km.LEGION) then
    return true
  end
  -- A team test, not is_friendly (which calls every dead walker a
  -- stranger): a possessed ally is attackable, and its death hands it back
  -- to this side before the hook runs; it is mourned, not raised.
  if victim.team == self.team then
    return true
  end
  if og.family_flag("living", victim:family(), "is_undead") then
    return true
  end
  local risen = og.add_ob("living", LIVING_SKELETON)
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

-- Dig in when hurt and outnumbered.
function M.ai_dig_in(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if self:possess_link() ~= 0 then
    return false
  end
  if not hp_below(self, 6, 10) then
    return false
  end
  return foe_count_within(self, DIG_FOES_RANGE) >= 2
end

local function own_wall_standing(self)
  local weapons = og.weaplist()
  for i = 1, #weapons do
    local wall = weapons[i]
    if wall:family() == WEAP_BONE_WALL then
      if wall:dead() == 0 then
        if wall:owner() == self then
          return true
        end
      end
    end
  end
  return false
end

-- A wall when hurt with a foe close; the storm once a wall stands and the
-- foes gather.
function M.ai_wall(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if own_wall_standing(self) then
    if self.magicpoints < M.STORM_COST then
      return false
    end
    if foe_count_within(self, STORM_FOES_RANGE) < 2 then
      return false
    end
    self:set_shifter_down(1)
    return true
  end
  if not hp_below(self, 7, 10) then
    return false
  end
  if foe_count_within(self, WALL_FOE_RANGE) < 1 then
    return false
  end
  self:set_shifter_down(0)
  return true
end

-- The ward when hurt and unwarded; the legion when healthy and crowded.
function M.ai_reassemble(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if hp_below(self, 1, 2) then
    if warded(self) then
      return false
    end
    self:set_shifter_down(0)
    return true
  end
  if km.find(self, km.LEGION) then
    return false
  end
  if foe_count_within(self, LEGION_FOES_RANGE) < 3 then
    return false
  end
  self:set_shifter_down(1)
  return true
end

km.register(km.BURROW, burrow)
km.register(km.LEGION, legion_window)

-- The declarations that reference this module (packs/core/families/):
--   core:skeleton  specials dig_in / bone_wall / reassemble, on_kill
return M
