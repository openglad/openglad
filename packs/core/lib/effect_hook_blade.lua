-- core lib: effect_hook_blade — the orc captain's chained blade: it spirals out from the captain and back like the boomerang, eats incoming shots, and reels the first foe it touches to the captain's feet (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local kc = og.use("kit_captain")
local hits = og.use("effect_common").hits

-- This lib's own copy of the 16-step orbit circle (radius 24) that
-- lib/effect_shield.lua turns the shield and the boomerang on; the blade
-- scales it to its radius of the moment.
local ORBIT_X = { 0, -9, -17, -22, -24, -22, -17, -9,
                  0,   9,  17,  22,  24,  22,  17,  9 }
local ORBIT_Y = { -24, -22, -17, -9, 0, 9, 17, 22,
                   24,  22,  17,  9, 0, -9, -17, -22 }

-- The spiral's radius at this age (ticks): out from hook_start, hook_growth
-- px a tick, to hook_reach, then back in at the same pace. With the core
-- numbers that is 12 px to 60 px in 24 ticks and back to 12 at 48.
local function hook_radius(t, age)
  local turn = og.div(t.hook_reach - t.hook_start, t.hook_growth)
  -- rule: The captain's HOOK BLADE spirals out 2 px a tick from 12 px to 60 px over 24 ticks, then
  --   back in at the same pace to 12 px at tick 48.
  -- tests: KitCaptain.hook_spirals_out_and_back
  if age <= turn then
    return t.hook_start + t.hook_growth * age
  end
  return t.hook_reach - t.hook_growth * (age - turn)
end

-- One step of the circle per tick, the shield's angular speed: 16 ticks a
-- turn, so the spiral grows 32 px a turn and sweeps nearly every spot
-- inside its reach.
local function hook_orbit_offset(drawcycle, radius)
  local idx = og.mod(drawcycle, 16) + 1
  local xd = og.fdiv(og.fmul(ORBIT_X[idx], radius), 24)
  local yd = og.fdiv(og.fmul(ORBIT_Y[idx], radius), 24)
  return xd, yd
end

local function retire(self)
  self.dead = 1
  self:death()
end

-- The first living foe on the blade's floor whose box the blade touches.
-- The blade's box is grown 4 px each side, so at full reach (where one
-- step of the circle is about 24 px) a foe cannot slip between two steps.
local function touched_foe(self)
  local foes = og.find_foes_in_range("ob", 64, self)
  -- plumbing: scan of foes within 64; rule at effect_hook_blade.lua:50
  for i = 1, #foes do
    local w = foes[i]
    local living = w:order() == C.ORDER_LIVING
    local same_floor = w:floor() == self:floor()
    -- rule: The blade snags the first living foe on its floor whose box touches the blade's box
    --   grown 4 px each side (so a foe cannot slip between two orbit steps); generators and
    --   other-floor foes are never snagged.
    -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
    -- tests: KitCaptain.a_late_snag_still_reels_home, KitCaptain.hook_spirals_out_and_back
    -- tests: SemHookBlade.the_blade_snags_living_foes_on_its_floor_never_a_generator_or_another_floor
    if living and same_floor then
      -- rule: Part of the rule above that begins "The blade snags the first living foe on its ..."
      -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
      -- tests: KitCaptain.a_late_snag_still_reels_home, KitCaptain.hook_spirals_out_and_back
      -- tests: SemHookBlade.the_blade_snags_living_foes_on_its_floor_never_a_generator_or_another_floor
      if hits(self:xpos() - 4, self:ypos() - 4, self:sizex() + 8, self:sizey() + 8,
              w:xpos(), w:ypos(), w:sizex(), w:sizey()) then
        return w
      end
    end
  end
  return nil
end

-- One axis of a reel step: at most `step` px from `from` toward `to`.
local function reel_axis(from, to, step)
  -- rule: A reel step moves the snagged foe at most hook_reel_step px per axis toward the spot
  --   centred on the captain, never past it.
  -- tests: KitCaptain.hook_reels_a_foe_in_from_the_west
  -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  if to > from then
    return from + og.min(step, to - from)
  end
  return from - og.min(step, from - to)
end

-- One tick of the reel. The snagged foe (the blade's leader) slides up to
-- hook_reel_step px a tick (each axis) toward the captain's centre, the
-- blade riding it in. When the step would bring it to the captain it takes
-- the clear spot beside him (if that is no further than a step away) and
-- the blade goes. A foe that died (effect::act lets go of a dead leader), a
-- captain on another floor, or a step that is not clear (a wall, another
-- walker) ends the reel where the foe stands.
local function reel(self, owner, t)
  local foe = self:leader()
  -- rule: A reel ends where the foe stands when the snagged foe has died (effect::act let go of it)
  --   or the captain is on another floor.
  -- tests: KitCaptain.reel_ends_when_the_foe_falls_or_the_captain_leaves_the_floor
  if not foe or foe:floor() ~= owner:floor() then
    retire(self)
    return true
  end
  local step = t.hook_reel_step
  local fx = foe:xpos()
  local fy = foe:ypos()
  local tx = owner:xpos() + og.div(owner:sizex() - foe:sizex(), 2)
  local ty = owner:ypos() + og.div(owner:sizey() - foe:sizey(), 2)
  local nx = reel_axis(fx, tx, step)
  local ny = reel_axis(fy, ty, step)
  -- The captain's box grown by the 2-px gap ring spots keep.
  local at_feet = hits(nx, ny, foe:sizex(), foe:sizey(),
                       owner:xpos() - 2, owner:ypos() - 2,
                       owner:sizex() + 4, owner:sizey() + 4)
  -- rule: When the reel step would bring the foe against the captain (his box grown 2 px) the foe
  --   is set down on the clear ring spot beside him, but only if that spot is no further than one
  --   reel step from where the foe is; otherwise (or with no ring spot) the foe stays put; either
  --   way the reel ends.
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
  -- tests: SemHookBlade.the_last_reel_step_sets_the_foe_down_beside_the_captain_with_the_blade_on_it
  if at_feet then
    local rx, ry = kc.ring_spot(foe, owner, fx, fy)
    -- rule: Part of the rule above that begins "When the reel step would bring the foe against ..."
    -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
    -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
    -- tests: SemHookBlade.the_last_reel_step_sets_the_foe_down_beside_the_captain_with_the_blade_on_it
    if rx ~= nil then
      local dx = math.abs(rx - fx)
      local dy = math.abs(ry - fy)
      -- rule: Part of the rule above that begins "When the reel step would bring the foe against
      --   ..."
      -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
      -- tests: KitCaptain.reel_never_hauls_a_foe_past_the_captain
      -- tests: SemHookBlade.the_last_reel_step_sets_the_foe_down_beside_the_captain_with_the_blade_on_it
      if og.max(dx, dy) <= step then
        foe:setxy(rx, ry)
        self:center_on(foe)
      end
    end
    retire(self)
    return true
  end
  -- rule: A reel step that would put the foe into a wall or another walker ends the reel where the
  --   foe stands.
  -- tests: KitCaptain.reel_stops_at_a_wall, KitCaptain.hook_reels_the_foe_to_the_captains_feet
  if not og.spawn_spot_clear(foe, nx, ny) then
    retire(self)
    return true
  end
  foe:setxy(nx, ny)
  self:center_on(foe)
  return true
end

local M = {}

-- core:hook_blade on_act.
function M.on_act(self)
  local owner = self:owner()
  -- rule: The hook blade dies with its captain.
  -- tests: KitCaptain.hook_blade_dies_with_its_captain, KitCaptain.hook_spirals_out_and_back
  if not owner or owner:dead() ~= 0 then
    retire(self)
    return true
  end
  -- The lifetime counts the spiral down; once a foe is snagged it counts
  -- the reel instead (kit_captain.snag resets it), so a late snag is never
  -- cut short by the spiral's clock. lineofsight 1 marks the blade as on
  -- the hook (kit_captain.snag sets it; the blade is thrown with 0).
  local lifetime = self:lifetime()
  -- rule: The blade's lifetime counts the spiral (48 ticks) and, once a foe is snagged, the reel
  --   instead (reset by kit_captain.snag), so a late snag is never cut short; at 0 the blade
  --   retires.
  -- tests: KitCaptain.hook_spirals_out_and_back, KitCaptain.a_late_snag_still_reels_home
  if lifetime < 1 then
    retire(self)
    return true
  end
  self.lifetime = lifetime - 1

  local t = og.tuning(owner)
  -- rule: A blade with a foe on the hook (lineofsight 1, set by snag) reels instead of spinning: it
  --   no longer guards against shots nor snags another foe.
  -- tests: KitCaptain.hook_spirals_out_and_back, KitCaptain.hook_eats_incoming_projectiles
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: SemHookBlade.a_blade_with_a_foe_on_the_hook_neither_guards_nor_snags_another
  if self:lineofsight() > 0 then
    return reel(self, owner, t)
  end

  local radius = hook_radius(t, self:drawcycle())
  local xd, yd = hook_orbit_offset(self:drawcycle(), radius)
  self:center_on(owner)  -- each step starts at the captain; offsets do not accumulate
  -- worldx/worldy are C++ floats: per-op rounding.
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
  self:set_frame(og.mod(self:drawcycle(), 8))

  -- While it spins it is a shield: foe shots that close in are cut down,
  -- each costing the blade its damage in hitpoints (the boomerang's guard).
  -- Solid scenery (a bone wall, a war banner) is neither a shot nor a
  -- shield: the blade passes it by, and foes must chop it down.
  local weapons = og.find_foe_weapons_in_range("weap", self:sizex() * 2, self)
  -- plumbing: iterates foe weapons in range; rule at effect_hook_blade.lua:153
  for i = 1, #weapons do
    local weapon = weapons[i]
    -- rule: While it spins, the hook blade cuts down foe shots within twice its size, each costing
    --   it the shot's damage in hp; solid scenery weapons (bone wall, war banner: blocks_placement)
    --   are passed by, never destroyed.
    -- tests: KitCaptain.hook_eats_incoming_projectiles, KitCaptain.hook_guard_passes_scenery_by
    if not weapon:blocks_placement() then
      -- hp is a C++ float: per-op rounding.
      self.hp = og.fsub(self.hp, weapon:damage())
      weapon.dead = 1
      weapon:death()
    end
  end
  -- rule: A hook blade that has eaten shots worth its 30 hp breaks and snags nothing that tick.
  -- tests: KitCaptain.hook_eats_incoming_projectiles
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: SemHookBlade.a_blade_broken_by_a_shot_snags_nothing_that_tick
  if self.hp <= 0 then
    retire(self)
    return true
  end

  local foe = touched_foe(self)
  -- rule: A spinning blade that touches a foe snags it (kit_captain.snag: lineofsight 1, leader,
  --   reel lifetime); a snag the kit refuses (e.g. the blow killed the foe) retires the blade
  --   without reeling.
  -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
  -- tests: KitCaptain.hook_kills_a_dying_foe_without_reeling_it
  -- tests: KitCaptain.hook_spirals_out_and_back
  if foe then
    -- rule: Part of the rule above that begins "A spinning blade that touches a foe snags it ..."
    -- tests: KitCaptain.hook_reels_the_foe_to_the_captains_feet
    -- tests: KitCaptain.hook_kills_a_dying_foe_without_reeling_it
    -- tests: KitCaptain.hook_spirals_out_and_back
    if not kc.snag(self, owner, foe) then
      retire(self)
    end
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:hook_blade  on_act = effect_hook_blade.on_act
return M
