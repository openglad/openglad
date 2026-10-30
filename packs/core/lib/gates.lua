-- core lib: gates — the predicates every bot gate is built from (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- DRAW-ORDER DISCIPLINE (docs/gameplay-fixes.md, "Bots"): with the Gameplay
-- Fixes setting on, every draw the classic bot makes is still made, in the
-- same order. So:
--   1. Nothing here draws: no og.rand, no og.find_near_foe /
--      og.find_nearest_foe / og.check_special_ai_distance (find_near_foe
--      rolls rng(invisibility_left / 20) per cloaked candidate), no loud
--      og.query_passable or s_forward_blocked() (eat, latch, open, swing);
--      s_forward_blocked(true), og.quiet_passable and fire_pad_clear are ours.
--   2. A family whose classic gate acquires a foe (ai.foe_within,
--      ai.foe_in_window) calls it FIRST through G.with_classic, on both
--      values of the setting, and tests after.
--   3. The engine draws the Shift coin before the hook (living.cpp:584); a
--      gate that chooses an arm writes it AFTER, with set_shifter_down, and
--      only an arm the bot can pay (G.can_pay): the engine does not re-check.
--   4. No og.tuning here: a gate runs on every fifth act tick and on every
--      hit, and a tuning read there costs instructions (lib/ai.lua). The
--      families pass numbers in.
-- Nothing here writes a walker field.

local C = og.C
local gf = og.use("gameplay_fixes")

local M = {}

-- A melee blow lands across a gap of at most 5 px on each axis (the 6 px
-- weapon box sits 1 px outside the attacker and collide() moves the other
-- body's near edge in by 1 px: walker.cpp, obmap.cpp). The same at every level.
M.BLOW_GAP = 5
-- A scan radius that holds every pair at blows: the widest body is the
-- 25 x 29 tower, 29 + 5 = 34 per axis, 68 Manhattan.
M.BLOW_SCAN = 80
-- A scan radius above the longest flight a ring throws (a level-16 hero's
-- fire arrow goes about 200 px straight).
M.RING_SCAN = 400
-- "Everywhere": the mage's freeze cue scans the whole map.
M.FAR = 30000
-- A bolt whose lastx / lasty the cast sets to +-1 after the fire (WARP
-- SPACE's ring, mage.lua) moves one pixel a tick on every heading: its step
-- is 1 and a straight heading does not scale it. No fired weapon of the
-- table has a step of 1 (gloader.cpp).
M.UNIT_STEP = 1

function M.living(w)
  return w:order() == C.ORDER_LIVING
end

-- The living foes within range px (Manhattan between top-left corners, the
-- finder's own compare; it already skips the dead, the dormant and the
-- hidden), on the caster's floor when floor_only. A generator is never one.
function M.living_foes(self, range, floor_only)
  local found = og.find_foes_in_range("ob", range, self)
  local out = {}
  local floor = self:floor()
  for i = 1, #found do
    local w = found[i]
    if w:order() == C.ORDER_LIVING then
      if not floor_only or w:floor() == floor then
        out[#out + 1] = w
      end
    end
  end
  return out
end

-- "At least n living foes within range": the one check most gates are.
function M.foes_within(self, n, range, floor_only)
  return #M.living_foes(self, range, floor_only) >= n
end

-- "At least n bodies the finder counts as foes", generators included: for a
-- cast that acts on a generator (WHIRLWIND strikes it, HOWL stuns it; fix K).
function M.targets_within(self, n, range)
  local _, count = og.find_foes_in_range("ob", range, self)
  return count >= n
end

-- The nearest living foe within range, a tie to list order; never assigns
-- self.foe (that is the classic acquisition's job, and it draws).
function M.nearest_living_foe(self, range, floor_only)
  local foes = M.living_foes(self, range, floor_only)
  local best = nil
  local best_dist = 0
  for i = 1, #foes do
    local dist = self:distance_to_ob(foes[i])
    if best == nil or dist < best_dist then
      best = foes[i]
      best_dist = dist
    end
  end
  return best
end

-- The living friends within range; the finder skips the caller itself.
function M.allies(self, range)
  local found = og.find_friends_in_range("ob", range, self)
  local out = {}
  for i = 1, #found do
    if found[i]:order() == C.ORDER_LIVING then
      out[#out + 1] = found[i]
    end
  end
  return out
end

function M.allies_within(self, n, range)
  return #M.allies(self, range) >= n
end

-- The edge-to-edge gap between the bodies of a and b on each axis (negative
-- where the boxes overlap). Positions and sizes are METHOD calls: `a.xpos`
-- is the method itself, never a number.
function M.gap(a, b)
  local gap_x = og.max(b:xpos() - (a:xpos() + a:sizex()),
                       a:xpos() - (b:xpos() + b:sizex()))
  local gap_y = og.max(b:ypos() - (a:ypos() + a:sizey()),
                       a:ypos() - (b:ypos() + b:sizey()))
  return gap_x, gap_y
end

-- Within a blow: both gaps at most BLOW_GAP.
function M.at_blows(a, b)
  local gap_x, gap_y = M.gap(a, b)
  return gap_x <= M.BLOW_GAP and gap_y <= M.BLOW_GAP
end

-- The living foes at blows with the caster.
function M.foes_at_blows(self)
  local foes = M.living_foes(self, M.BLOW_SCAN, false)
  local out = {}
  for i = 1, #foes do
    if M.at_blows(self, foes[i]) then
      out[#out + 1] = foes[i]
    end
  end
  return out
end

-- The allies' fight: how many living foes are at blows with an ally other
-- than the caster (foe-major: a foe at blows with two allies counts once),
-- and whether such an ally is under a quarter of its health. FREEZE TIME's
-- cue; the body of the former mage.lua freeze_wanted.
function M.allies_fight(self)
  local allies = M.allies(self, M.FAR)
  local foes = M.living_foes(self, M.FAR, false)
  local engaged = 0
  local hurt = false
  for i = 1, #foes do
    local near = false
    for j = 1, #allies do
      local ally = allies[j]
      if M.at_blows(ally, foes[i]) then
        near = true
        if ally.hp * 4 < ally.max_hp then
          hurt = true
        end
      end
    end
    if near then
      engaged = engaged + 1
    end
  end
  return engaged, hurt
end

-- An ally within range that has a living foe at blows with it: the ally
-- and that foe, or nil.
function M.ally_at_blows(self, range)
  local allies = M.allies(self, range)
  local foes = M.living_foes(self, range + M.BLOW_SCAN, false)
  for i = 1, #allies do
    for j = 1, #foes do
      if M.at_blows(allies[i], foes[j]) then
        return allies[i], foes[j]
      end
    end
  end
  return nil
end

-- The living foes within range that are hunting the caster (their foe is
-- self); the first of them, or nil, from hunted_by.
function M.hunters(self, range)
  local foes = M.living_foes(self, range, false)
  local out = {}
  for i = 1, #foes do
    if foes[i]:foe() == self then
      out[#out + 1] = foes[i]
    end
  end
  return out
end

function M.hunted_by(self, range)
  return M.hunters(self, range)[1]
end

-- An ally within range below its maximum health.
function M.hurt_ally(self, range)
  local allies = M.allies(self, range)
  for i = 1, #allies do
    if allies[i].hp < allies[i].max_hp then
      return allies[i]
    end
  end
  return nil
end

-- hp below num/den of max_hp. hp and max_hp are C++ floats: one float
-- multiply each side keeps the comparison free of decimal fractions.
function M.hp_below(w, num, den)
  return og.fmul(w.hp, den) < og.fmul(w.max_hp, num)
end

function M.centre(w)
  return w:xpos() + w:sizex() // 2, w:ypos() + w:sizey() // 2
end

-- The engine facing from the caster's centre to the target's.
function M.heading_to(self, target)
  local sx, sy = M.centre(self)
  local tx, ty = M.centre(target)
  return self:facing(tx - sx, ty - sy)
end

-- Where a fired weapon leaves: facing(lastx, lasty), NOT curdir (walker.cpp
-- fire; at spawn lastx = lasty = 0 and facing(0, 0) is FACE_UP). lastx and
-- lasty are C++ floats; the facing binding takes integers and the C++
-- narrows a float to short by truncation.
function M.fire_heading(self)
  return self:facing(og.trunc(self:lastx()), og.trunc(self:lasty()))
end

-- The caster has a heading at all: walk(0, 0) and a rush along (0, 0) move
-- nothing.
function M.has_heading(self)
  return self:lastx() ~= 0.0 or self:lasty() ~= 0.0
end

-- The caster's fired weapon would leave toward the target.
function M.faces(self, target)
  return M.heading_to(self, target) == M.fire_heading(self)
end

-- The caster has a heading and it is toward the target (CHARGE's rush).
function M.aimed_at(self, target)
  return M.has_heading(self) and M.faces(self, target)
end

-- The heading to the target is `facing` (0 .. 7) or one of its two
-- neighbours: the three lines a fan of knives flies along or a bone wall
-- rises across. The kit passes the facing its own cast uses.
function M.fan_covers(self, facing, target)
  local h = M.heading_to(self, target)
  return h == facing
      or h == og.mod(facing + 1, 8)
      or h == og.mod(facing + 7, 8)
end

-- The target is on the fire heading or one of its two neighbours (a fan).
function M.fan_faces(self, target)
  return M.fan_covers(self, M.fire_heading(self), target)
end

local function straight(heading)
  return heading == C.FACE_UP
      or heading == C.FACE_RIGHT
      or heading == C.FACE_DOWN
      or heading == C.FACE_LEFT
end

-- How far a fired weapon (table step / los) goes PER AXIS from this caster
-- along heading, as walker::configure_weapon_profile_base shapes it: a hero
-- adds strength / 23 + dexterity / 31 to los, every caster adds level / 3,
-- and on the four straight headings los is scaled by 309 / 256 (integer)
-- and the step by 362 / 256 (float; never a UNIT_STEP). A volley's los multiplier (los_num /
-- los_den) and an additive los bonus (los_add) are applied after, as the
-- casts apply them to the fired bolt. The bolt's first walk only turns it,
-- so los - 1 steps move. The product is a C++ float: truncated.
function M.flight_px(self, step, los, heading, los_num, los_den, los_add)
  local steps = los
  if self:has_guy() then
    steps = steps + self:g_strength() // 23 + self:g_dexterity() // 31
  end
  steps = steps + self.level // 3
  local px = step
  if straight(heading) then
    steps = steps * 309 // 256
    if step ~= M.UNIT_STEP then
      px = og.fdiv(og.fmul(step, 362), 256)
    end
  end
  if los_num then
    steps = steps * los_num // los_den
  end
  if los_add then
    steps = steps + los_add
  end
  return og.trunc(og.fmul(steps - 1, px))
end

-- The target is within the weapon's flight along the heading to it: the
-- edge gap on the dominant axis is at most the reach (the bolt starts just
-- outside the caster's body and stops at the target's edge).
function M.in_flight(self, target, step, los, los_num, los_den, los_add)
  local heading = M.heading_to(self, target)
  local reach = M.flight_px(self, step, los, heading, los_num, los_den, los_add)
  local gap_x, gap_y = M.gap(self, target)
  return og.max(gap_x, gap_y) <= reach
end

-- A ring (the same bolt along all eight headings) pays where one bolt would
-- not: at least two living foes in its flight (each along its own heading),
-- or exactly one the caster does not face.
function M.ring_pays(self, step, los, los_add)
  local foes = M.living_foes(self, M.RING_SCAN, false)
  local in_ring, unfaced = 0, false
  for i = 1, #foes do
    if M.in_flight(self, foes[i], step, los, nil, nil, los_add) then
      in_ring = in_ring + 1
      if not M.faces(self, foes[i]) then
        unfaced = true
      end
    end
  end
  if in_ring >= 2 then
    return true
  end
  return in_ring == 1 and unfaced
end

-- The explosion's reach: find_in_range(15 + clamp(4 * level, 16, 96)).
function M.blast_reach(level)
  return 15 + og.clamp(level * 4, 16, 96)
end

-- No ally of the CASTER within reach of the blast's centre (Manhattan, as
-- the blast measures), and, when check_self, the caster not in it either.
-- find_friends_in_range answers friends of the body it is given, so the
-- caster's allies are scanned around the caster and sifted by their
-- distance to the centre; the centre may be a foe.
function M.blast_clear(self, centre, reach, check_self)
  if check_self and centre ~= self
      and self:distance_to_ob(centre) <= reach then
    return false
  end
  local scan = reach
  if centre ~= self then
    scan = scan + self:distance_to_ob(centre)
  end
  local allies = M.allies(self, scan)
  for i = 1, #allies do
    if centre:distance_to_ob(allies[i]) <= reach then
      return false
    end
  end
  return true
end

-- The live objects of `order` / `family` the caster owns: count, and the
-- first one. An FX or living order scans og.oblist() (effects and summoned
-- livings live there); the weapon order scans the weapon list (the druid's
-- circle). The order is tested because family numbers collide across
-- orders (boomerang 7 is the faerie's number, cloud 8 the slime's).
function M.owned(self, order, family)
  local obs
  if order == C.ORDER_WEAPON then
    obs = og.find_in_range("weap", M.FAR, self)
  else
    obs = og.oblist()
  end
  local count = 0
  local first = nil
  for i = 1, #obs do
    local ob = obs[i]
    if ob and ob:order() == order
        and ob:family() == family
        and ob:dead() == 0
        and ob:owner() == self then
      count = count + 1
      first = first or ob
    end
  end
  return count, first
end

-- A phantom of the caster's: a summoned living with max_hp 1.
function M.owned_phantom(self)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob and ob:order() == C.ORDER_LIVING
        and ob:dead() == 0
        and ob:owner() == self
        and ob.max_hp == 1 then
      return ob
    end
  end
  return nil
end

-- The stain find_nearest_blood returns, when it is on the caster's floor
-- and within range (Manhattan, the raises' own compare). The finder itself
-- never answers beyond about 28 px of centre (its 800 start on a squared
-- distance), so a range above that is a promise the finder cannot keep.
function M.corpse_near(self, range)
  local blood = og.find_nearest_blood(self)
  if not blood or blood:floor() ~= self:floor() then
    return nil
  end
  if self:distance_to_ob(blood) < range then
    return blood
  end
  return nil
end

-- The same stain by the meal's compare: squared centre distance.
function M.corpse_underfoot(self, reach_sq)
  local blood = og.find_nearest_blood(self)
  if not blood or blood:floor() ~= self:floor() then
    return nil
  end
  if self:distance_to_ob_center(blood) <= reach_sq then
    return blood
  end
  return nil
end

-- FACE_UP .. FACE_UP_LEFT as unit steps.
local STEP_X = { [0] = 0, 1, 1, 1, 0, -1, -1, -1 }
local STEP_Y = { [0] = -1, -1, 0, 1, 1, 1, 0, -1 }

-- The first of foes whose box, shrunk 1 px on every side as collide()
-- shrinks it, meets the box at (ax, ay) of size aw x ah (already shrunk);
-- collide() compares inclusively.
local function foe_meeting(foes, ax, ay, aw, ah)
  for i = 1, #foes do
    local f = foes[i]
    local fx, fy = f:xpos() + 1, f:ypos() + 1
    local fw, fh = f:sizex() - 2, f:sizey() - 2
    if ax <= fx + fw
        and ax + aw >= fx
        and ay <= fy + fh
        and ay + ah >= fy then
      return f
    end
  end
  return nil
end

-- A living foe within reach whose box meets the caster's box moved one
-- pixel along curdir (DISARM's forward_blocked steps CHECK_STEP_SIZE = 1
-- along curdir and asks query_passable, whose collide() shrinks both boxes
-- by 1 px on every side and compares inclusively). From positions alone.
function M.ahead_blocked_by_foe(self, reach)
  local d = self:curdir()
  if d < 0 or d > 7 then
    return nil
  end
  local ax = self:xpos() + STEP_X[d] + 1
  local ay = self:ypos() + STEP_Y[d] + 1
  local aw, ah = self:sizex() - 2, self:sizey() - 2
  return foe_meeting(M.living_foes(self, reach, true), ax, ay, aw, ah)
end

-- The living foe a rush along the caster's heading runs into. CHARGE
-- queues COMMAND_RUSH for three acts, and each act walks three steps of
-- stepsize px along trunc(lastx / stepsize), trunc(lasty / stepsize) (the
-- step soldier.lua's charge passes) and strikes and shoves the body it ran
-- into (stats.cpp). The first living foe whose box meets the caster's box
-- moved k such steps, k = 1 .. steps (collide()'s shrink), or nil; nil with
-- no heading, where the rush walks nowhere. A wall, an ally, a door or a
-- generator before the foe refuses, and so does a front the cast itself
-- finds blocked.
function M.rush_strikes(self, steps)
  -- The cast refuses PATH BLOCKED when the one-pixel step along curdir is
  -- blocked (a wall, any body, a door): the same question, asked quietly
  -- (Gameplay Fixes U); a foe already pressed against the front is a blow,
  -- not a charge.
  if self:s_forward_blocked(true) then
    return nil
  end
  local step = self:stepsize()
  local ux = og.trunc(og.fdiv(self:lastx(), step))
  local uy = og.trunc(og.fdiv(self:lasty(), step))
  if ux == 0 and uy == 0 then
    return nil
  end
  local span = og.max(og.max(ux, -ux), og.max(uy, -uy))
  local run = og.trunc(og.fmul(steps * span, step))
  local foes = M.living_foes(self, 2 * run + M.BLOW_SCAN, true)
  local aw, ah = self:sizex() - 2, self:sizey() - 2
  for k = 1, steps do
    local ax = self:xpos() + og.trunc(og.fmul(ux * k, step)) + 1
    local ay = self:ypos() + og.trunc(og.fmul(uy * k, step)) + 1
    local f = foe_meeting(foes, ax, ay, aw, ah)
    if f then
      return f
    end
    -- Nothing struck here: the rush must be able to stand here on its way.
    -- A wall, an ally, a door or a generator at this step stops it before
    -- any later foe (COMMAND_RUSH strikes whatever its walksteps bump into).
    if not og.quiet_passable(ax - 1, ay - 1, self) then
      return nil
    end
  end
  return nil
end

-- The pad fire() would place the caster's weapon on is clear: the weapon
-- would fly instead of meeting a wall, a door, the map edge or a foe's body
-- there (walker:fire_pad_clear, the same spot and the same weapon rules,
-- asked quietly). A body of the caster's own team passes, as the weapon
-- passes it.
function M.pad_clear(self)
  return self:fire_pad_clear()
end

-- The bot can pay the slot's price, or the alternate's when shifted. An
-- alternate with no price of its own answers 0 from alternate_cost and
-- costs the slot's price.
function M.can_pay(self, slot, shifted)
  local price = 0
  if shifted then
    price = self:alternate_cost(slot)
  end
  if price == 0 then
    price = self:s_special_cost(slot)
  end
  return self.magicpoints >= price
end

-- The slot's rest is running (living_common gate_with_rest reads the same).
function M.resting(self, slot)
  return self:special_rest_slot() == slot and self:special_rest_left() > 0
end

-- TELEPORT MARKER pays for a bot when it will come back here: no marker of
-- its own stands yet, a hero has the INT the cast asks, and half of what
-- is left after the slot's price (the marker's surcharge) still leaves the
-- price of next_slot, so marking does not cost the bot its fighting special.
function M.marker_pays(self, fx_marker, next_slot, int_req)
  if self:has_guy() and self:g_intelligence() < int_req then
    return false
  end
  local count = M.owned(self, C.ORDER_FX, fx_marker)
  if count > 0 then
    return false
  end
  local spare = og.trunc(og.fsub(self.magicpoints,
                                 self:s_special_cost(self:current_special())))
  return og.div(spare, 2) >= self:s_special_cost(next_slot)
end

-- The combinator every family with a classic foe-acquiring gate uses:
-- `classic` runs first on both values (its draws and its foe assignment
-- are the classic bot's); with the setting off its answer stands; with it
-- on `fixed(self, classic_answer)` decides.
function M.with_classic(classic, fixed)
  return function(self)
    local classic_answer = classic(self)
    if not gf.on() then
      return classic_answer
    end
    return fixed(self, classic_answer)
  end
end

return M
