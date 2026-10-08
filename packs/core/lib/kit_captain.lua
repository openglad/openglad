-- core lib: kit_captain — the orc captain's New Specials: howl/eat corpse, hook blade/knife fan, hurl orc/shove, war banner/warband, and their bot gates (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local lc = og.use("living_common")
local orc = og.use("orc_specials")

local FX_HOOK_BLADE = assert(og.family_id("fx", "core:hook_blade"))
local FX_FLASH = assert(og.family_id("fx", "core:flash"))
local WEAPON_WAR_BANNER = assert(og.family_id("weapon", "core:war_banner"))
local LIVING_ORC = assert(og.family_id("living", "core:orc"))
local LIVING_ORC_CAPTAIN = assert(og.family_id("living", "core:orc_captain"))

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
  if dir >= 0 then
    return og.mod(dir, 8)
  end
  local sx = og.sign(self:lastx())
  local sy = og.sign(self:lasty())
  for i = 1, 8 do
    if DIR_X[i] == sx and DIR_Y[i] == sy then
      return i - 1
    end
  end
  return C.FACE_DOWN
end

local function is_orc(w)
  local fam = w:family()
  return fam == LIVING_ORC or fam == LIVING_ORC_CAPTAIN
end

-- A living foe on the walker's own floor (the finders are floor-blind and
-- also answer generators).
local function living_on_floor(w, floor)
  if w:order() ~= C.ORDER_LIVING then
    return false
  end
  return w:floor() == floor
end

local function count_living_foes(self, range)
  local foes = og.find_foes_in_range("ob", range, self)
  local n = 0
  for i = 1, #foes do
    if living_on_floor(foes[i], self:floor()) then
      n = n + 1
    end
  end
  return n
end

-- One coordinate of a ring spot: the mover's top-left so that it stands just
-- beside the anchor on that side (2 px apart), or level with it.
local function ring_coord(anchor_pos, anchor_size, mover_size, side)
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
  for i = 1, 8 do
    local x = ring_coord(anchor:xpos(), anchor:sizex(), mover:sizex(), DIR_X[i])
    local y = ring_coord(anchor:ypos(), anchor:sizey(), mover:sizey(), DIR_Y[i])
    local d = math.abs(x - near_x) + math.abs(y - near_y)
    local closer = best_x == nil or d < best_d
    if closer and og.spawn_spot_clear(mover, x, y, anchor:floor()) then
      best_x = x
      best_y = y
      best_d = d
    end
  end
  return best_x, best_y
end

-- The captain's live hook blade, or nil. Summoned effects live on the oblist.
local function live_blade(self)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_FX
        and ob:family() == FX_HOOK_BLADE
        and ob:dead() == 0
        and ob:owner() == self then
      return ob
    end
  end
  return nil
end

-- The captain's live war banner, or nil. Weapons live on the weaplist.
local function live_banner(self)
  local weaps = og.weaplist()
  for i = 1, #weaps do
    local w = weaps[i]
    if w:family() == WEAPON_WAR_BANNER
        and w:dead() == 0
        and w:owner() == self then
      return w
    end
  end
  return nil
end

-- How many warband grunts this captain has alive: orcs it summoned.
local function live_grunts(self)
  local obs = og.oblist()
  local n = 0
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:family() == LIVING_ORC
        and ob:dead() == 0
        and ob:owner() == self then
      n = n + 1
    end
  end
  return n
end

-- A blood stain the captain stands on (within the squared reach), on its
-- floor, or nil. find_nearest_blood is floor-blind and team-blind.
local function corpse_underfoot(self, reach_sq)
  local corpse = og.find_nearest_blood(self)
  if not corpse then
    return nil
  end
  if corpse:floor() ~= self:floor() then
    return nil
  end
  if self:distance_to_ob_center(corpse) > reach_sq then
    return nil
  end
  return corpse
end

local function flash_at(where)
  local flash = og.add_ob("fx", FX_FLASH)
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(where:floor())
    flash:center_on(where)
  end
end

-- ------------------------------------------------- slot 1: HOWL / EAT CORPSE

-- The orc's own two specials, carried over onto one slot, so an orc promoted
-- at level 5 keeps both. The whole slot is new-kit, so with the setting off
-- neither is in play and a plain shifter_down is the right test.
function M.howl_or_eat(self)
  if self:shifter_down() ~= 0 then
    return orc.eat_corpse(self)
  end
  return orc.yell(self)
end

-- ------------------------------------------------ slot 2: HOOK BLADE / FAN

local function hook_blade(self)
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
  if not blade then
    return false, "COULD NOT MAKE A BLADE"
  end
  og.emit_sound(C.SOUND_FWIP)
  return true
end

-- Three plain knives: the facing and one point either side of it.
local function knife_fan(self)
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
  if self:shifter_down() ~= 0 then
    return knife_fan(self)
  end
  return hook_blade(self)
end

-- The blade has caught `foe`: cut it, drag it to the captain's feet and
-- stun it. Called by lib/effect_hook_blade.lua, which then retires the blade.
function M.snag(blade, owner, foe)
  local t = og.tuning(owner)
  -- attack() looks pure but draws from the gameplay stream.
  blade:attack(foe)
  if foe:dead() ~= 0 then
    return
  end
  local x, y = M.ring_spot(foe, owner, foe:xpos(), foe:ypos())
  if x ~= nil and y ~= nil then
    foe:set_floor(owner:floor())
    foe:setxy(x, y)
  else
    -- Nowhere to land beside the captain: the chain hauls it a few steps.
    local dx = og.sign(owner:xpos() - foe:xpos())
    local dy = og.sign(owner:ypos() - foe:ypos())
    foe:s_force_command(C.COMMAND_WALK, t.hook_drag_ticks, dx, dy)
  end
  foe:add_frozen_stun(t.hook_stun)
  foe:set_foe(owner)
  og.emit_sound(C.SOUND_CLANG)
end

-- -------------------------------------------------- slot 3: HURL ORC / SHOVE

-- The nearest allied orc (orc or captain) beside the captain that no seat
-- drives, or nil.
local function orc_beside(self, reach)
  local friends = og.find_friends_in_range("ob", reach, self)
  local best = nil
  local best_d = 0
  for i = 1, #friends do
    local w = friends[i]
    local d = self:distance_to_ob(w)
    local closer = best == nil or d < best_d
    local unseated = w:user() == -1
    local candidate = is_orc(w) and unseated
    local same_floor = w:floor() == self:floor()
    if closer and candidate then
      if same_floor then
        best = w
        best_d = d
      end
    end
  end
  return best
end

-- A foe worth throwing at: alive, on the captain's floor, in range and in
-- plain sight.
local function hurl_target_ok(self, w, range)
  if not w or w:dead() ~= 0 then
    return false
  end
  if self:is_friendly(w) or w:floor() ~= self:floor() then
    return false
  end
  if self:distance_to_ob(w) > range then
    return false
  end
  return og.line_clear(self, w)
end

local function hurl_orc(self)
  local t = og.tuning(self)
  local thrown = orc_beside(self, t.hurl_adjacent)
  if not thrown then
    return false, "NO ORC BESIDE YOU"
  end
  local target = self:foe()
  if not hurl_target_ok(self, target, t.hurl_range) then
    target = og.find_near_foe(self)
  end
  if not target then
    return false, "NO TARGET"
  end
  if not hurl_target_ok(self, target, t.hurl_range) then
    return false, "NO TARGET"
  end
  local x, y = M.ring_spot(thrown, target, self:xpos(), self:ypos())
  if x == nil or y == nil then
    return false, "NO LANDING SPOT"
  end
  thrown:set_floor(target:floor())
  thrown:setxy(x, y)
  -- The impact: every living foe around the landing takes the orc's weight.
  local saved_damage = thrown:damage()
  thrown.damage = thrown.level * t.hurl_damage_per_level
  local foes = og.find_foes_in_range("ob", t.hurl_radius, thrown)
  for i = 1, #foes do
    local w = foes[i]
    if living_on_floor(w, thrown:floor()) then
      -- attack() draws from the gameplay stream, once per foe
      thrown:attack(w)
      w:add_frozen_stun(t.hurl_stun)
    end
  end
  thrown.damage = saved_damage
  -- ... and the orc fights where it lands.
  thrown:set_foe(target)
  thrown.ani_type = C.ANI_ATTACK
  thrown:set_cycle(0)
  flash_at(thrown)
  og.emit_sound(C.SOUND_CLANG)
  return true
end

-- Push one foe up to `tiles` tiles along (dx, dy), a tile at a time; the
-- first blocked tile stops it.
local function push_back(foe, dx, dy, tiles)
  for _ = 1, tiles do
    local x = foe:xpos() + dx * C.GRID_SIZE
    local y = foe:ypos() + dy * C.GRID_SIZE
    if not og.spawn_spot_clear(foe, x, y) then
      return
    end
    foe:setxy(x, y)
  end
end

local function shove(self)
  local t = og.tuning(self)
  local facing = facing_index(self) + 1
  local dx = DIR_X[facing]
  local dy = DIR_Y[facing]
  local foes = og.find_foes_in_range("ob", t.shove_reach, self)
  local shoved = 0
  for i = 1, #foes do
    local w = foes[i]
    local ahead = (w:xpos() - self:xpos()) * dx + (w:ypos() - self:ypos()) * dy
    if ahead > 0 and living_on_floor(w, self:floor()) then
      push_back(w, dx, dy, t.shove_tiles)
      w:add_frozen_stun(t.shove_stun)
      shoved = shoved + 1
    end
  end
  if shoved == 0 then
    return false, "NO ONE IN FRONT"
  end
  og.emit_sound(C.SOUND_CHARGE)
  return true
end

function M.hurl_or_shove(self)
  if self:shifter_down() ~= 0 then
    return shove(self)
  end
  return hurl_orc(self)
end

-- ------------------------------------------- slot 4: WAR BANNER / WARBAND

local function war_banner(self)
  local t = og.tuning(self)
  local corpse = corpse_underfoot(self, t.banner_plant_sq)
  if not corpse then
    return false, "NO CORPSE TO PLANT ON"
  end
  local banner = og.add_weap_ob("weapon", WEAPON_WAR_BANNER)
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
  og.emit_sound(C.SOUND_ROAR)
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
  for i = 2, 4 do
    if gaps[i] < gaps[side] then
      side = i
    end
  end
  local spots = {}
  for k = 0, scan_tiles - 1 do
    local step = k * C.GRID_SIZE
    local sx = x
    local sy = y
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
    local clear = room and not on_the_captain(self, sx, sy)
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
  -- They run to a standing banner, else to the captain.
  local rally = live_banner(self) or self
  local goal_x = rally:xpos()
  local goal_y = rally:ypos()
  for i = 1, #spots, 2 do
    -- Owned and timed like a summoned elemental: a grunt dies with its
    -- captain, so it is fearless exactly while the captain lives.
    local grunt = self:do_summon(LIVING_ORC, t.warband_lifetime)
    if grunt then
      grunt.team = self.team
      grunt.level = og.max(1, og.div(self.level, 2))
      grunt:set_difficulty(grunt.level)
      grunt:setxy(spots[i], spots[i + 1])
      grunt:set_kit_state(C.KIT_FEARLESS)
      grunt:set_leader(self)
      grunt:s_add_command(C.COMMAND_GOTO, 200, goal_x, goal_y)
    end
  end
  og.emit_sound(C.SOUND_ROAR)
  return true
end

function M.banner_or_warband(self)
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

-- Howl at a foe within 130 (the orc's own gate); eat when hurt and standing
-- on a corpse.
function M.ai_howl(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local hurt = self.hp < og.fmul(self.max_hp, 0.6)
  if hurt and corpse_underfoot(self, 24) then
    self:set_shifter_down(1)
    return true
  end
  self:set_shifter_down(0)
  return og.check_special_ai_distance(self, 130)
end

-- Hook a foe within 90 while no blade is out; fan knives into a crowd.
function M.ai_hook(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local near = count_living_foes(self, 90)
  if near >= 1 and not live_blade(self) then
    self:set_shifter_down(0)
    return true
  end
  if near >= 2 then
    self:set_shifter_down(1)
    return true
  end
  return false
end

-- Throw an orc standing beside the captain at a foe within 120; shove a
-- crowd at arm's length.
function M.ai_hurl(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local thrower = orc_beside(self, 20) ~= nil
  if thrower and count_living_foes(self, 120) >= 1 then
    self:set_shifter_down(0)
    return true
  end
  if count_living_foes(self, 40) >= 2 then
    self:set_shifter_down(1)
    return true
  end
  return false
end

-- Plant a banner on a corpse with friends around and none standing; call
-- the warband when foes press and no grunt is left.
function M.ai_banner(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local _, allies = og.find_friends_in_range("ob", 96, self)
  local plant = allies >= 2 and corpse_underfoot(self, 1024) ~= nil
  if plant and not live_banner(self) then
    self:set_shifter_down(0)
    return true
  end
  local pressed = count_living_foes(self, 120) >= 2
  if pressed and live_grunts(self) == 0 then
    self:set_shifter_down(1)
    return true
  end
  return false
end

-- The declarations that reference this module (packs/core/families/):
--   core:orc_captain  every specials entry's cast and ai
--   core:hook_blade   (lib/effect_hook_blade.lua calls M.snag)
return M
