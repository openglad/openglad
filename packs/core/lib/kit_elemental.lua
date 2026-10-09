-- core lib: kit_elemental — the fire elemental's New Specials: IMMOLATE, METEOR RAIN, REKINDLE / SUPERNOVA (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local km = og.use("kit_marker")
local foes_touching = og.use("effect_ember").foes_touching
local FX_EMBER = assert(og.family_id("fx", "core:ember"))
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local FX_HIT = assert(og.family_id("fx", "core:hit"))

-- While IMMOLATE burns the elemental wears KIT_CHANNEL, and the engine
-- prices EVERY slot at 0 for a walker wearing it (so the quenching press is
-- free once the drain has eaten the mana). The other specials therefore
-- refuse while the fire burns, or they would cast for nothing.
local BURNING = "QUENCH IMMOLATE FIRST"

-- Bot-gate ranges are code constants: the gates run on every AI roll and
-- every hit, and a per-call og.tuning read there costs instructions
-- (lib/ai.lua says why).
local IMMOLATE_GATE_RANGE = 40
local IMMOLATE_GATE_MANA = 60
local RAIN_GATE_RANGE = 160
local RAIN_GATE_CLUSTER = 60
local NOVA_GATE_RANGE = 48

local M = {}

local function burning(self)
  return (self:kit_state() & C.KIT_CHANNEL) ~= 0
end

-- Ends a burning IMMOLATE's channel on the elemental (no-op otherwise). The
-- dying elemental calls it too, so its parting starburst is never refused.
function M.end_channel(self)
  if burning(self) then
    self:set_kit_state(self:kit_state() & ~C.KIT_CHANNEL)
  end
end

-- The quench: the channel ends on the owner, then the marker dies.
local function quench(owner, marker)
  M.end_channel(owner)
  km.finish(marker)
end

-- A flame-lick flash on the elemental (the first of the hit rows; no draw).
local function hit_flash(self)
  local flash = og.add_ob("fx", FX_HIT)
  if flash then
    flash:set_owner(self)
    flash.team = self.team
    flash:set_floor(self:floor())
    flash:center_on(self)
    flash.ani_type = C.ANI_ATTACK
  end
end

-- Living foes on the owner's floor whose box touches the owner's box grown
-- by 4 px burn. The one that last struck the elemental, standing in the
-- fire, is a melee attacker and burns double (hit_response cannot do this:
-- it never fires for a player-driven victim).
local function burn_contacts(marker, owner, t)
  local foes = foes_touching(owner, 4)
  local striker = owner:last_attacker_id()
  for i = 1, #foes do
    local foe = foes[i]
    local burn = t.immolate_contact + owner.level
    if og.entity_id(foe) == striker then
      burn = burn * 2
    end
    marker.damage = burn
    marker:attack(foe)
  end
end

-- True when one of the owner's live embers already lies on the spot an
-- ember of its size would drop on now (bottom centre of the owner's box).
local function ember_underfoot(owner)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:family() == FX_EMBER
        and ob:order() == C.ORDER_FX
        and ob:dead() == 0
        and ob:owner() == owner then
      local x = owner:xpos() + og.div(owner:sizex() - ob:sizex(), 2)
      local y = owner:ypos() + owner:sizey() - ob:sizey()
      local on_spot = ob:xpos() == x and ob:ypos() == y
      if on_spot and ob:floor() == owner:floor() then
        return true
      end
    end
  end
  return false
end

-- A burning footprint at the owner's feet (bottom centre of its box), unless
-- one still burns there: a standing elemental keeps one ember under it
-- instead of piling them up.
local function drop_ember(owner, t)
  if ember_underfoot(owner) then
    return
  end
  local ember = og.add_ob("fx", FX_EMBER)
  if ember then
    ember:set_owner(owner)
    ember.team = owner.team
    ember:set_floor(owner:floor())
    ember.lifetime = t.ember_ticks
    ember.damage = t.ember_damage + og.div(owner.level, 2)
    ember:set_frame(0)
    ember:setxy(owner:xpos() + og.div(owner:sizex() - ember:sizex(), 2),
                owner:ypos() + owner:sizey() - ember:sizey())
  end
end

-- IMMOLATION marker: drains the owner's mana each tick, burns what touches
-- it, drops an ember every ember_step ticks; goes out when the owner dies,
-- the mana runs dry or immolate_max ticks pass. After each drain its clock
-- is cut to what the mana left can pay for (one more tick for every
-- immolate_drain, and the tick that finds the pool dry), so the countdown
-- a player sees is the tick the fire really goes out.
local function immolation_act(marker)
  local owner = marker:owner()
  if not owner then
    km.finish(marker)
    return
  end
  local t = og.tuning(owner)
  if owner.magicpoints < t.immolate_drain then
    quench(owner, marker)
    return
  end
  marker.lifetime = marker:lifetime() - 1
  if marker:lifetime() <= 0 then
    quench(owner, marker)
    return
  end
  -- magicpoints is a C++ float: per-op rounding.
  owner.magicpoints = og.fsub(owner.magicpoints, t.immolate_drain)
  local paid = og.div(og.trunc(owner.magicpoints), t.immolate_drain) + 1
  marker.lifetime = og.min(marker:lifetime(), paid)
  marker:center_on(owner)
  burn_contacts(marker, owner, t)
  if og.mod(marker:lifetime(), t.ember_step) == 0 then
    drop_ember(owner, t)
  end
end

-- One meteor: a fire explosion somewhere in the rain's square, x then y.
-- skip_exit makes it the archmage heartburst's magical blast: the 16 px
-- range, and the caster is never caught in its own rain.
local function strike(marker, owner, t)
  local blast = og.add_ob("fx", FX_EXPLOSION)
  if blast then
    local dx = og.rand(2 * t.rain_spread + 1) - t.rain_spread
    local dy = og.rand(2 * t.rain_spread + 1) - t.rain_spread
    blast:set_owner(owner)
    blast.team = owner.team
    blast.level = marker.level
    blast.damage = marker:damage()
    blast:set_floor(marker:floor())
    blast:center_on(marker)
    blast:setxy(blast:xpos() + dx, blast:ypos() + dy)
    blast.ani_type = C.ANI_EXPLODE
    blast:s_set_bit_flags(C.BIT_FIRE, 1)
    blast:set_skip_exit(100)
    og.emit_sound(C.SOUND_EXPLODE)
  end
end

-- METEOR_RAIN marker: sits on the target spot and strikes every
-- rain_cadence ticks until its lifetime runs out or its caster dies.
local function meteor_rain_act(marker)
  local owner = marker:owner()
  if not owner then
    km.finish(marker)
    return
  end
  local t = og.tuning(owner)
  marker.lifetime = marker:lifetime() - 1
  if og.mod(marker:lifetime(), t.rain_cadence) == 0 then
    strike(marker, owner, t)
  end
  if marker:lifetime() <= 0 then
    km.finish(marker)
  end
end

km.register(km.IMMOLATION, immolation_act)
km.register(km.METEOR_RAIN, meteor_rain_act)

-- STARBURST (slot 1) is the classic cast, refused only while IMMOLATE burns.
function M.starburst(classic)
  return function(self)
    if burning(self) then
      return false, BURNING
    end
    return classic(self)
  end
end

-- IMMOLATE (slot 2): lights the fire, or (a second press after the latch,
-- free because the channel prices it at 0) puts it out.
function M.immolate(self)
  local t = og.tuning(self)
  local marker = km.find(self, km.IMMOLATION)
  if marker then
    if km.age(marker) < t.kit_latch then
      return false, "IMMOLATE SETTLING"
    end
    quench(self, marker)
    return true
  end
  marker = km.spawn(self, km.IMMOLATION, t.immolate_max)
  if not marker then
    return false, "COULD NOT IGNITE"
  end
  self:set_kit_state(self:kit_state() | C.KIT_CHANNEL)
  return true
end

-- The foe METEOR RAIN falls on: alive, on our floor and within rain_range.
local function rain_target(self, foe, t)
  if not foe then
    return nil
  end
  if foe:dead() ~= 0 then
    return nil
  end
  if foe:floor() ~= self:floor() then
    return nil
  end
  if self:distance_to_ob(foe) > t.rain_range then
    return nil
  end
  return foe
end

-- METEOR RAIN (slot 3): our foe, or the nearest one, gets a rain marker.
function M.meteor_rain(self)
  if burning(self) then
    return false, BURNING
  end
  local t = og.tuning(self)
  local target = rain_target(self, self:foe(), t)
  if not target then
    target = rain_target(self, og.find_near_foe(self), t)
  end
  if not target then
    return false, "NO TARGET IN RANGE"
  end
  local marker = km.spawn(self, km.METEOR_RAIN, t.rain_ticks)
  if not marker then
    return false, "COULD NOT CALL METEORS"
  end
  marker:set_floor(target:floor())
  marker:center_on(target)
  marker.level = self.level
  marker.damage = t.rain_damage_base + self.level * 2
  return true
end

local function rekindle(self, t)
  if self.hp >= self.max_hp then
    return false, "ALREADY AT FULL HEALTH"
  end
  self:heal_clamped(t.rekindle_hp + self.level * 4)
  hit_flash(self)
  og.emit_sound(C.SOUND_HEAL)
  return true
end

-- One fire blast of SUPERNOVA's, centred on `at` and moved by (dx, dy).
local function nova_blast(self, at, dx, dy)
  local blast = og.add_ob("fx", FX_EXPLOSION)
  if blast then
    blast:set_owner(self)
    blast.team = self.team
    blast:set_floor(self:floor())
    blast:center_on(at)
    blast:setxy(blast:xpos() + dx, blast:ypos() + dy)
    blast.ani_type = C.ANI_EXPLODE
    blast:s_set_bit_flags(C.BIT_FIRE, 1)
  end
  return blast
end

-- SUPERNOVA: every hit point goes into one blast (its own level, so its
-- range, doubles the elemental's), and eight blasts of half its damage go
-- off in a ring nova_ring_px out, so the nova reaches past one blast's
-- range. Each ring blast carries the main blast's level: the elemental is
-- dead when they go off, so each blast reads its own level for its range.
-- Then the elemental dies and on_death fires the parting starburst on top.
local function supernova(self, t)
  local blast = nova_blast(self, self, 0, 0)
  if blast then
    blast.level = og.min(self.level * 2, 24)
    -- hp is a C++ float: per-op rounding.
    blast.damage = og.fadd(og.fdiv(og.fmul(self.hp, 3), 2), self.level * t.nova_per_level)
    local ring_damage = og.div(og.trunc(blast:damage()), 2)
    for i = -1, 1 do
      for j = -1, 1 do
        if i ~= 0 or j ~= 0 then
          local ring = nova_blast(self, blast, i * t.nova_ring_px, j * t.nova_ring_px)
          if ring then
            ring.level = blast.level
            ring.damage = ring_damage
          end
        end
      end
    end
  end
  og.emit_sound(C.SOUND_EXPLODE)
  self.hp = 0
  self.dead = 1
  self:death()
  return true
end

-- REKINDLE / SUPERNOVA (slot 4). The whole slot is new, so a plain shift
-- test is enough: with the setting off neither arm is in play.
function M.rekindle_or_supernova(self)
  if burning(self) then
    return false, BURNING
  end
  local t = og.tuning(self)
  if self:shifter_down() ~= 0 then
    return supernova(self, t)
  end
  return rekindle(self, t)
end

-- Bot gates. The setting guard is first and answers true, so with the
-- setting off a gate draws and changes nothing (check_special() && !rng(3)
-- would skip a draw on false).
function M.ai_immolate(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if burning(self) then
    return false
  end
  if self.magicpoints <= IMMOLATE_GATE_MANA then
    return false
  end
  return og.check_special_ai_distance(self, IMMOLATE_GATE_RANGE)
end

-- Our foe within 160 with at least one more of our foes within 60 of it.
function M.ai_meteor(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if not og.check_special_ai_distance(self, RAIN_GATE_RANGE) then
    return false
  end
  local target = self:foe()
  local foes = og.find_foes_in_range("ob", RAIN_GATE_RANGE + RAIN_GATE_CLUSTER, self)
  local clustered = 0
  for i = 1, #foes do
    if foes[i]:distance_to_ob(target) <= RAIN_GATE_CLUSTER then
      clustered = clustered + 1
    end
  end
  return clustered >= 2
end

-- SUPERNOVA below a fifth of our health with two foes close; otherwise
-- REKINDLE below half.
function M.ai_rekindle(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- max_hp is a C++ float: per-op rounding.
  if self.hp < og.fdiv(self.max_hp, 5.0) then
    local _, close = og.find_foes_in_range("ob", NOVA_GATE_RANGE, self)
    if close >= 2 then
      self:set_shifter_down(1)
      return true
    end
  end
  self:set_shifter_down(0)
  -- max_hp is a C++ float: per-op rounding.
  return self.hp < og.fdiv(self.max_hp, 2.0)
end

-- The declarations that reference this module (packs/core/families/):
--   core:elemental  default_cast = ke.starburst(do_special); cast = ke.immolate,
--                   ke.meteor_rain, ke.rekindle_or_supernova; ai = ke.ai_immolate,
--                   ke.ai_meteor, ke.ai_rekindle; its on_death calls ke.end_channel
return M
