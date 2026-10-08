-- core lib: kit_ghost — the ghost's New Specials: WAIL, SIPHON, POSSESS and PHASE, with their bot gates (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local km = og.use("kit_marker")
local wail_bolt = og.use("effect_wail")
local FX_GHOST_SCARE = assert(og.family_id("fx", "core:ghost_scare"))

local M = {}

-- The nearest living foe on the ghost's floor within `reach` px, or nil.
-- The finder already skips the dead, the dormant and the hidden; a tie goes
-- to the foe met first in the object list.
local function nearest_foe(self, reach)
  local foes = og.find_foes_in_range("ob", reach, self)
  local best = nil
  local best_dist = 0
  for i = 1, #foes do
    local foe = foes[i]
    if foe:order() == C.ORDER_LIVING and foe:floor() == self:floor() then
      local dist = self:distance_to_ob(foe)
      if best == nil or dist < best_dist then
        best = foe
        best_dist = dist
      end
    end
  end
  return best
end

-- A phased ghost cannot touch anyone: SIPHON and POSSESS refuse.
local function phased(self)
  return self:s_query_bit_flags(C.BIT_PHANTOM)
end

-- ---------------------------------------------------------------------
-- SCARE / WAIL (slot 1). Shift casts WAIL when the setting has it in play.
-- ---------------------------------------------------------------------

local function wail(self)
  local foe = nearest_foe(self, og.scare_radius(self.level))
  if not foe then
    return false, "NO FOE TO WAIL AT"
  end
  local bolt = wail_bolt.launch(self, self, foe, 0)
  if not bolt then
    return false, "COULD NOT CREATE WAIL"
  end
  bolt.lifetime = og.tuning(bolt).wail_hops
  og.emit_sound(C.SOUND_FWIP)
  return true
end

-- The slot-1 cast: the classic SCARE unless the shifted alternate is in play.
function M.scare_or_wail(classic)
  return function(self)
    if self:alternate_down() then
      return wail(self)
    end
    return classic(self)
  end
end

-- ---------------------------------------------------------------------
-- SIPHON (slot 2): a touch that heals the ghost by half the damage dealt.
-- A resisted or empty touch still spends the mana (it answers true).
-- ---------------------------------------------------------------------

function M.siphon(self)
  if phased(self) then
    return false, "PHASED"
  end
  local t = og.tuning(self)
  local foe = nearest_foe(self, t.siphon_reach)
  if not foe then
    og.emit_notification(
      og.entity_display_name(self, "Ghost") .. "'s touch finds nothing", nil,
      self)
    return true
  end
  local before = foe.hp
  -- The touch hits with its own damage, swapped in around the attack (the
  -- explosion's shape in effect_bomb.lua); attack() draws as a melee hit.
  local full_damage = self:damage()
  self.damage = t.siphon_damage_base + t.siphon_damage_per_level * self.level
  self:attack(foe)
  self.damage = full_damage
  -- hp is a C++ float: per-op rounding; the heal binding takes a whole number.
  local dealt = og.fsub(before, foe.hp)
  local heal = og.trunc(og.fdiv(dealt, 2.0))
  if heal > 0 then
    self:heal_clamped(heal)
  end
  return true
end

function M.ai_siphon(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if phased(self) then
    return false
  end
  if self.hp >= og.fmul(self.max_hp, 0.8) then
    return false
  end
  return nearest_foe(self, 24) ~= nil
end

-- ---------------------------------------------------------------------
-- POSSESS (slot 3): touch a foe and ride it. The engine (og.possess) moves
-- the seat, hides the ghost and counts the host's ticks down; the refusals
-- below come first so a cast that cannot land spends neither the mana nor
-- the resist roll.
-- ---------------------------------------------------------------------

-- Why a foe cannot be taken at all, or nil when it can.
local function host_refusal(foe)
  if og.family_flag("living", foe:family(), "is_undead") then
    return "UNDEAD RESIST"
  end
  if foe:user() ~= -1 or foe:act_type() == C.ACT_CONTROL then
    return "HERO RESISTS"
  end
  if foe:possess_link() ~= 0 then
    return "ALREADY POSSESSED"
  end
  if foe:real_team_num() ~= 255 then
    return "HOST IS CHARMED"
  end
  return nil
end

-- How long the ride lasts: longer the more the ghost out-levels its host,
-- never shorter than possess_min, and for good (0) at a large gap.
local function possess_ticks(self, foe, t)
  local gap = self.level - foe.level
  if gap >= t.possess_permanent_gap then
    return 0
  end
  return og.max(t.possess_min, t.possess_base + t.possess_per_gap * gap)
end

function M.possess(self)
  if phased(self) then
    return false, "PHASED"
  end
  -- A charmed ghost's true team waits on its real_team_num; riding a host
  -- would overwrite it, so the engine refuses, and so do we, for free.
  if self:real_team_num() ~= 255 then
    return false, "CANNOT POSSESS"
  end
  local t = og.tuning(self)
  local foe = nearest_foe(self, t.possess_reach)
  if not foe then
    return false, "NO HOST IN REACH"
  end
  local refusal = host_refusal(foe)
  if refusal then
    return false, refusal
  end
  local con
  if foe:has_guy() then
    con = foe:g_constitution()
  else
    -- (int32)(hitpoints / 30.0f): one float division, then trunc.
    con = og.trunc(og.fdiv(foe.hp, 30.0))
  end
  con = og.max(con, 0)
  -- The orc howl's resist roll, in its order: level roll, then
  -- constitution roll (LEFT-FIRST, living-14-orc.lua).
  local level_roll = og.rand0(self.level * 10)
  local con_roll = og.rand0(con * 10)
  if con_roll > level_roll then
    -- Resisted: the mana is spent and the host strikes back at once. A
    -- struck bot answers with a special of its own (the hit response in
    -- stats.cpp), and this cast is paid for only after it returns, so the
    -- ghost's mana is held aside for the blow: no second cast can start
    -- inside this one.
    foe:set_foe(self)
    local mana = self.magicpoints
    self.magicpoints = 0
    foe:attack(self)
    self.magicpoints = mana
    og.emit_notification(og.entity_display_name(foe, "Foe") .. " resisted!",
                         nil, self)
    return true
  end
  local ok, why = og.possess(self, foe, possess_ticks(self, foe, t))
  if not ok then
    return false, why
  end
  -- The entrance, as the release's exit: an ownerless scare burst is a
  -- picture only (the scare frightens foes for a live owner alone).
  local burst = og.add_ob("fx", FX_GHOST_SCARE)
  if burst then
    burst.ani_type = C.ANI_SCARE
    burst.team = foe.team
    burst:set_floor(foe:floor())
    burst:center_on(foe)
  end
  og.emit_sound(C.SOUND_FWIP)
  return true
end

function M.ai_possess(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if phased(self) or self:real_team_num() ~= 255 then
    return false
  end
  local foe = nearest_foe(self, 24)
  if not foe then
    return false
  end
  if host_refusal(foe) then
    return false
  end
  return self.level >= foe.level
end

-- ---------------------------------------------------------------------
-- PHASE (slot 4): fully spectral for phase_ticks — invulnerable, out of
-- the collision table, unable to attack, twice as fast. The PHASE_VEIL
-- marker holds the window and puts the ghost back when it runs out.
-- ---------------------------------------------------------------------

-- Undo everything the cast did, in reverse, and rejoin the collision table.
local function unphase(owner)
  owner:s_set_bit_flags(C.BIT_PHANTOM, 0)
  owner:s_set_bit_flags(C.BIT_NO_COLLIDE, 0)
  owner:set_ignore(0)
  og.rejoin_obmap(owner)
  owner:set_invulnerable_left(0)
  owner:set_speed_bonus(0)
  owner:set_speed_bonus_left(0)
end

-- PHASE_VEIL role handler: ride the ghost, keep it too busy to attack
-- (2, because the ghost's own act takes one off before it may swing),
-- count down, and end the phase at zero.
local function veil(marker)
  local owner = marker:owner()
  if not owner then
    marker.dead = 1
    marker:death()
    return
  end
  marker:center_on(owner)
  -- busy is a C++ float: per-op rounding.
  owner.busy = og.max(owner:busy(), 2.0)
  marker.lifetime = marker:lifetime() - 1
  if marker:lifetime() <= 0 then
    unphase(owner)
    marker.dead = 1
    marker:death()
  end
end

km.register(km.PHASE_VEIL, veil)

function M.phase(self)
  if km.find(self, km.PHASE_VEIL) then
    return false, "ALREADY PHASED"
  end
  local t = og.tuning(self)
  local marker = km.spawn(self, km.PHASE_VEIL, t.phase_ticks)
  if not marker then
    return false, "COULD NOT PHASE"
  end
  self:set_invulnerable_left(t.phase_ticks)
  -- Double speed: the speed potion's timer, one tick longer than the veil
  -- because living::act spends a tick of it only while more than one is left.
  self:set_speed_bonus(self:stepsize())
  self:set_speed_bonus_left(t.phase_ticks + 1)
  self:s_set_bit_flags(C.BIT_PHANTOM, 1)
  self:s_set_bit_flags(C.BIT_NO_COLLIDE, 1)
  -- An ignored walker leaves the collision table on its next move; move it
  -- onto its own spot so it leaves now.
  self:set_ignore(1)
  self:setxy(self:xpos(), self:ypos())
  return true
end

function M.ai_phase(self)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if self.hp >= og.fmul(self.max_hp, 0.3) then
    return false
  end
  return nearest_foe(self, 48) ~= nil
end

-- The declarations that reference this module (packs/core/families/):
--   core:ghost  specials: scare_or_wail, siphon, possess, phase and the
--               ai gates ai_siphon, ai_possess, ai_phase
return M
