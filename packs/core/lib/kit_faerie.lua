-- core lib: kit_faerie — the faerie's New Specials: blink and swap, glimmer, hasten, wish (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local lc = og.use("living_common")
local FX_FLASH = assert(og.family_id("fx", "core:flash"))
local FX_HIT = assert(og.family_id("fx", "core:hit"))
local TREASURE_STAIN = assert(og.family_id("treasure", "core:stain"))

local M = {}

-- The teleporter pad's flash, centred on `at`. Returns the flash, or nil
-- when the world is full.
local function flash_at(at)
  local flash = og.add_ob("fx", FX_FLASH)
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(at:floor())
    flash:center_on(at)
  end
  return flash
end

-- The combat hit spark on `target` (the engine's own hit effect, no damage).
local function hit_on(target)
  local hit = og.add_ob("fx", FX_HIT)
  if hit then
    hit.ani_type = 1
    hit:set_damage(0)
    hit:set_owner(target)
    hit.team = target.team
    hit:set_floor(target:floor())
    hit:center_on(target)
  end
end

-- A walker SWAP may trade places with: another living with a clear line
-- between them (og.line_clear is false across floors, so it is on the
-- faerie's floor too). Hidden, dormant and dead walkers never reach here
-- (the range finder skips them).
local function swappable(self, ob)
  if ob == self then
    return false
  end
  if ob:order() ~= C.ORDER_LIVING then
    return false
  end
  return og.line_clear(self, ob)
end

-- SWAP's partner, ally or foe: the nearest swappable walker within `range`
-- (the first one in list order on a tie), or nil.
local function swap_partner(self, range)
  local near = og.find_in_range("ob", range, self)
  local best = nil
  local best_dist = range + 1
  for i = 1, #near do
    local ob = near[i]
    if swappable(self, ob) then
      local dist = self:distance_to_ob(ob)
      if dist < best_dist then
        best = ob
        best_dist = dist
      end
    end
  end
  return best
end

-- HASTEN's partner: the nearest living ally on the faerie's floor within
-- `reach` (never the faerie herself), or nil.
local function hasten_partner(self, reach)
  local friends = og.find_friends_in_range("ob", reach, self)
  local best = nil
  local best_dist = reach + 1
  for i = 1, #friends do
    local ally = friends[i]
    if ally:floor() == self:floor() then
      local dist = self:distance_to_ob(ally)
      if dist < best_dist then
        best = ally
        best_dist = dist
      end
    end
  end
  return best
end

-- WISH's corpse: the nearest friendly blood stain on the faerie's floor
-- closer than `range`, or nil. og.find_nearest_blood would answer any
-- team's stain on any floor, so the fx list is scanned here instead.
local function wish_stain(self, range)
  local fxs = og.fxlist()
  local best = nil
  local best_dist = range
  for i = 1, #fxs do
    local ob = fxs[i]
    if ob:order() == C.ORDER_TREASURE then
      if ob:family() == TREASURE_STAIN then
        if ob:floor() == self:floor() then
          if self:is_friendly(ob) then
            local dist = self:distance_to_ob(ob)
            if dist < best_dist then
              best = ob
              best_dist = dist
            end
          end
        end
      end
    end
  end
  return best
end

-- Who HASTEN speeds up: the faerie herself with the shift held, else her
-- nearest ally in reach (nil when there is none).
local function hasten_target(self, reach)
  if self:alternate_down() then
    return self
  end
  return hasten_partner(self, reach)
end

-- Every living ally on the faerie's floor within `radius` back to full
-- health, each with the healer's green number.
local function heal_party(self, radius)
  local friends = og.find_friends_in_range("ob", radius, self)
  for i = 1, #friends do
    local ally = friends[i]
    if ally:floor() == self:floor() then
      if ally.hp < ally.max_hp then
        -- hp is a C++ float: one float subtract, the number shown is its int truncation
        local amount = og.trunc(og.fsub(ally.max_hp, ally.hp))
        ally.hp = ally.max_hp
        self:do_heal_effects(self, ally, og.i16(amount))
      end
    end
  end
end

-- Living allies on the faerie's floor within `radius` below half health.
local function wounded_allies(self, radius)
  local friends = og.find_friends_in_range("ob", radius, self)
  local count = 0
  for i = 1, #friends do
    local ally = friends[i]
    if ally:floor() == self:floor() then
      -- max_hp is a C++ float: fdiv is a genuine float half
      if ally.hp < og.fdiv(ally.max_hp, 2.0) then
        count = count + 1
      end
    end
  end
  return count
end

-- BLINK: a short random hop on the same floor. The appear row that follows
-- keeps a held key from blinking again until it has played.
local function blink(self)
  if lc.mid_teleport(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local from_x = self:xpos()
  local from_y = self:ypos()
  if not self:teleport_ranged(t.blink_base + t.blink_per_level * self.level) then
    return false, "NOWHERE TO BLINK"
  end
  -- One flash where she left (moved back from her landing), one where she lands.
  local origin = flash_at(self)
  if origin then
    origin:setxy(origin:xpos() + from_x - self:xpos(), origin:ypos() + from_y - self:ypos())
  end
  flash_at(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  return true
end

-- SWAP: trade places with the nearest walker in sight, ally or foe.
local function swap(self)
  local partner = swap_partner(self, og.tuning(self).swap_range)
  if not partner then
    return false, "NO ONE IN SIGHT"
  end
  -- The faerie flies: a walker that cannot stand where she hovers (water,
  -- a wall's edge) is not dropped there.
  if not og.query_grid_passable(self:xpos(), self:ypos(), partner, self:floor()) then
    return false, "SWAP BLOCKED"
  end
  if not og.swap_places(self, partner) then
    return false, "SWAP BLOCKED"
  end
  flash_at(self)
  flash_at(partner)
  return true
end

-- Slot 1: BLINK, or SWAP with the shift held.
function M.blink_or_swap(self)
  if self:alternate_down() then
    return swap(self)
  end
  return blink(self)
end

-- GLIMMER: the faerie's freezing sprinkle in all eight directions.
function M.glimmer(self)
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- shim kept (both): the aim is parked in int temps: C truncation.
  local saved_aim_x = og.trunc(self:lastx())
  local saved_aim_y = og.trunc(self:lasty())
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
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
  -- The burst costs one attack's pause, so a held key cannot repeat it at
  -- once. busy is a C++ float: per-op rounding.
  self:set_busy(og.fadd(self:busy(), self:fire_frequency()))
  return true
end

-- Slot 3: HASTEN the nearest ally, or HASTE SELF with the shift held. The
-- speed potion's timer and units; a stronger bonus already running is kept.
function M.hasten(self)
  local t = og.tuning(self)
  local target = hasten_target(self, t.hasten_reach)
  if not target then
    return false, "NO ALLY IN REACH"
  end
  target:set_speed_bonus_left(target:speed_bonus_left() + t.hasten_ticks)
  target:set_speed_bonus(og.max(target:speed_bonus(), t.hasten_bonus))
  hit_on(target)
  og.emit_sound(C.SOUND_HEAL)
  return true
end

-- Slot 4: WISH. A fallen ally rises at full health where it fell (the
-- cleric's friendly resurrect), every living ally nearby is healed to full,
-- and the faerie pops.
function M.wish(self)
  local t = og.tuning(self)
  local blood = wish_stain(self, t.wish_range)
  if not blood then
    return false, "NO FALLEN ALLY NEARBY"
  end
  local alive = og.add_ob("living", blood:s_old_family())
  if not alive then
    return false, "COULD NOT RESURRECT"
  end
  -- restore our old values ..
  blood:transfer_stats(alive)
  alive.hp = alive.max_hp
  alive.team = blood.team
  alive:set_floor(blood:floor())
  alive:setxy(blood:xpos(), blood:ypos())
  blood:set_dead(1)
  self:do_heal_effects(self, alive, og.i16(og.trunc(alive.max_hp)))
  flash_at(alive)
  heal_party(self, t.wish_heal_radius)
  og.emit_sound(C.SOUND_HEAL)
  self.hp = 0
  self.dead = 1
  self:death()
  return true
end

-- The bot gates. Each starts with the setting check and answers true with
-- the setting off: the engine asks slot 1's gate of every hit faerie, and a
-- false there would skip the draw that follows it (the classic game had no
-- gate and always answered true).

-- Slot 1: SWAP a dying ally out when it is the walker SWAP would take (the
-- nearest in sight) and its attacker is on it; else BLINK away from a foe
-- that is close while the faerie is hurt.
function M.ai_blink(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local partner = swap_partner(self, og.tuning(self).swap_range)
  if partner then
    if self:is_friendly(partner) then
      -- max_hp is a C++ float: one float multiply
      if partner.hp < og.fmul(partner.max_hp, 0.3) then
        local attacker = partner:foe()
        if attacker then
          if attacker:dead() == 0 then
            if partner:distance_to_ob(attacker) <= 40 then
              self:set_shifter_down(1)
              return true
            end
          end
        end
      end
    end
  end
  local _, close_foes = og.find_foes_in_range("ob", 30, self)
  if close_foes > 0 then
    -- max_hp is a C++ float: fdiv is a genuine float half
    if self.hp < og.fdiv(self.max_hp, 2.0) then
      self:set_shifter_down(0)
      return true
    end
  end
  return false
end

-- Slot 2: GLIMMER when a foe is within 60.
function M.ai_glimmer(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local _, foes = og.find_foes_in_range("ob", 60, self)
  return foes > 0
end

-- Slot 3: HASTEN the ally HASTEN would take when it is fighting and not
-- already quick; else HASTE SELF when the faerie's own foe is within 60.
function M.ai_hasten(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local ally = hasten_partner(self, og.tuning(self).hasten_reach)
  if ally then
    if ally:speed_bonus_left() == 0 then
      local fight = ally:foe()
      if fight then
        if fight:dead() == 0 then
          self:set_shifter_down(0)
          return true
        end
      end
    end
  end
  local foe = self:foe()
  if foe then
    if foe:dead() == 0 then
      if self:distance_to_ob(foe) <= 60 then
        self:set_shifter_down(1)
        return true
      end
    end
  end
  return false
end

-- Slot 4: WISH only when there is someone to raise AND at least two living
-- allies nearby are below half health.
function M.ai_wish(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  local t = og.tuning(self)
  if not wish_stain(self, t.wish_range) then
    return false
  end
  return wounded_allies(self, t.wish_heal_radius) >= 2
end

-- The declarations that reference this module (packs/core/families/):
--   core:faerie  specials = blink_or_swap, glimmer, hasten, wish and their ai gates
return M
