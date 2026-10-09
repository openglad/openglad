-- core:wail — the ghost's wail: a homing puff of the scare's sparkles that hops foe to foe like chain lightning (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local FX_WAIL = assert(og.family_id("fx", "core:wail"))

local hits = og.use("effect_common").hits

-- expand8.png is the scare's cloud, a dot (frame 0) growing to a ring
-- (frame 7). The puff loops the small clusters: index + 1 is the frame.
local SPARKLE_FRAMES = { 1, 2, 3, 2 }

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

-- A foe the wail may hop to: a living on the struck bolt's floor within
-- wail_fork_px of the one it just frightened, not that one, and not wailed
-- at in the last three rounds.
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
  if leader:distance_to_ob(foe) > og.tuning(self).wail_fork_px then
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
  -- The reach is measured from the struck foe, not from the puff, whose
  -- corner sits well off its centre: every foe within wail_fork_px of the
  -- struck one is within that plus the puff's own distance to it.
  local reach = og.tuning(self).wail_fork_px + self:distance_to_ob(leader)
  local foes = og.find_foes_in_range("ob", reach, self)
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
-- (effect_chain.lua's homing, float for float). The puff is far bigger than
-- its foe, so it steers its centre onto the foe's centre: the target is the
-- foe's corner moved by half the difference in size (og.div truncates as C
-- does). Each axis steps at most the distance left, so the bolt never
-- overshoots and needs no final snap.
local function home_on(self, leader)
  local tx = leader:xpos() + og.div(leader:sizex() - self:sizex(), 2)
  local ty = leader:ypos() + og.div(leader:sizey() - self:sizey(), 2)
  local xd, yd = 0, 0
  if tx > self:xpos() then
    xd = og.min(self:stepsize(), tx - self:xpos())
  elseif tx < self:xpos() then
    xd = og.max(tx - self:xpos(), -self:stepsize())
  end
  if ty > self:ypos() then
    yd = og.min(self:stepsize(), ty - self:ypos())
  elseif ty < self:ypos() then
    yd = og.max(ty - self:ypos(), -self:stepsize())
  end
  -- shim kept: facing() takes ints; xd/yd are floats: C truncation.
  self:set_curdir(self:facing(og.trunc(xd), og.trunc(yd)))
  -- shim kept: worldx/worldy are C++ floats: per-op float rounding.
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
end

-- core:wail on_act: the bolt homes on its leader; on contact it frightens
-- it and forks. A bolt whose foe or ghost is gone, or that flew out its
-- life, fades without a fright. Only the puff's 8x8 core strikes: its
-- sparkles show smaller than its 48x40 frame, so the frame would strike
-- before the puff is seen to touch.
local function on_act(self)
  local leader = self:leader()
  local owner = self:owner()
  if not leader or not owner then
    return die(self)
  end
  if self:lineofsight() < 1 or leader:hidden() then
    return die(self)
  end
  local cx = self:xpos() + og.div(self:sizex(), 2)
  local cy = self:ypos() + og.div(self:sizey(), 2)
  if hits(cx - 4, cy - 4, 8, 8,
          leader:xpos(), leader:ypos(), leader:sizex(), leader:sizey()) then
    strike(self, leader, owner)
    return die(self)
  end
  self:set_lineofsight(self:lineofsight() - 1)
  -- The lineofsight count is the puff's clock: one frame per
  -- wail_frame_ticks ticks, round the small sparkles.
  local step = og.div(self:lineofsight(), og.tuning(self).wail_frame_ticks)
  self:set_frame(SPARKLE_FRAMES[og.mod(step, 4) + 1])
  home_on(self, leader)
  return true
end

M.on_act = on_act

-- The declarations that reference this module (packs/core/families/):
--   core:wail  on_act = effect_wail.on_act
return M
