-- core:wail — the ghost's wail: a homing fright bolt that hops foe to foe like chain lightning (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local FX_WAIL = assert(og.family_id("fx", "core:wail"))

local hits = og.use("effect_common").hits

-- lightnin.png holds one frame per heading, in the chain bolt's order
-- (gloader.cpp's aniarrow rows): index curdir + 1 is the frame to show.
local FRAME_BY_DIR = { 1, 5, 2, 6, 0, 4, 3, 7 }

local M = {}

-- A fresh bolt leaving `from` (the ghost, or the bolt that just struck) and
-- homing on `foe`. It is owned by the ghost, harmless (damage 0) and carries
-- the hops it may still make in its lifetime field, which an effect whose
-- on_act always answers true never counts down. nil when the world is full.
function M.launch(owner, from, foe, hops)
  local bolt = og.add_ob("fx", FX_WAIL)
  if not bolt then
    return nil
  end
  local t = og.tuning(bolt)
  bolt:set_owner(owner)
  bolt.team = owner.team
  bolt.level = owner.level
  bolt.damage = 0
  bolt:set_floor(from:floor())
  bolt:center_on(from)
  bolt:set_leader(foe)
  bolt:set_lineofsight(t.wail_bolt_life)
  bolt:set_stepsize(t.wail_bolt_step)
  bolt.lifetime = hops
  return bolt
end

local function die(self)
  self.dead = 1
  self:death()
  return true
end

-- A foe the wail may hop to: a living on the struck bolt's floor, not the
-- one it just frightened, and not wailed at in the last three rounds.
local function is_fork_target(self, leader, foe)
  if foe == leader then
    return false
  end
  if foe:order() ~= C.ORDER_LIVING then
    return false
  end
  if foe:floor() ~= self:floor() then
    return false
  end
  return foe:skip_exit() < 1
end

-- The bolt reached its foe: frighten it away from the ghost, then fork.
local function strike(self, leader, owner)
  -- The scare's own fright and draw order (effect_ghost_scare.lua): the
  -- duration first, then the constitution resist, drawn only for a hero.
  local fright = og.scare_duration(owner.level)
  if leader:has_guy() then
    local con_roll = og.rand0(leader:g_constitution())
    fright = fright - con_roll
  end
  if fright > 0 then
    local flee_dx = og.sign(leader:xpos() - owner:xpos())
    local flee_dy = og.sign(leader:ypos() - owner:ypos())
    leader:s_force_fright(fright, flee_dx, flee_dy)
  end
  -- can't be wailed at again for 3 rounds (the chain bolt's guard)
  leader:set_skip_exit(leader:skip_exit() + 3)

  local hops = self:lifetime()
  if hops < 1 then
    return
  end
  local foes = og.find_foes_in_range("ob", og.tuning(self).wail_fork_px, self)
  -- One draw: the fork budget rolls off the ghost's level (the chain's
  -- shape), drawn even when no other foe is in reach.
  local forks_left = og.rand0(owner.level) + 1
  for i = 1, #foes do
    local foe = foes[i]
    if forks_left <= 0 then
      break
    end
    if is_fork_target(self, leader, foe) then
      M.launch(owner, self, foe, hops - 1)
      forks_left = forks_left - 1
    end
  end
end

-- One step toward the leader, turning the bolt to face where it flies
-- (effect_chain.lua's homing, float for float). Each axis steps at most the
-- distance left, so the bolt never overshoots and needs no final snap.
local function home_on(self, leader)
  local xd, yd = 0, 0
  if leader:xpos() > self:xpos() then
    xd = og.min(self:stepsize(), leader:xpos() - self:xpos())
  elseif leader:xpos() < self:xpos() then
    xd = og.max(leader:xpos() - self:xpos(), -self:stepsize())
  end
  if leader:ypos() > self:ypos() then
    yd = og.min(self:stepsize(), leader:ypos() - self:ypos())
  elseif leader:ypos() < self:ypos() then
    yd = og.max(leader:ypos() - self:ypos(), -self:stepsize())
  end
  -- shim kept: facing() takes ints; xd/yd are floats: C truncation.
  self:set_curdir(self:facing(og.trunc(xd), og.trunc(yd)))
  self:set_frame(FRAME_BY_DIR[self:curdir() + 1])
  -- shim kept: worldx/worldy are C++ floats: per-op float rounding.
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
end

-- core:wail on_act: the bolt homes on its leader; on contact it frightens
-- it and forks. A bolt whose foe or ghost is gone, or that flew out its
-- life, fades without a fright.
local function on_act(self)
  local leader = self:leader()
  local owner = self:owner()
  if not leader or not owner then
    return die(self)
  end
  if self:lineofsight() < 1 or leader:hidden() then
    return die(self)
  end
  if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
          leader:xpos(), leader:ypos(), leader:sizex(), leader:sizey()) then
    strike(self, leader, owner)
    return die(self)
  end
  self:set_lineofsight(self:lineofsight() - 1)
  home_on(self, leader)
  return true
end

M.on_act = on_act

-- The declarations that reference this module (packs/core/families/):
--   core:wail  on_act = effect_wail.on_act
return M
