-- core:cloud — drifting, fading poison cloud (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The drift step is statistics::do_command() (src/gameplay/stats.cpp), reached
-- from `self->stats()->do_command()` whenever the cloud already has a queued
-- walk — which is every tick after the first. walker:s_do_command() is the
-- only binding that EXECUTES the queue (the other s_*_command calls just
-- manipulate it); its short return value is discarded here exactly as in C++.

local C = og.C
local gf = og.use("gameplay_fixes")
local hits = og.use("effect_common").hits

-- cloud_on_act: a poison cloud ages out, fades as it expires, damages any foe
-- it overlaps, and drifts one random step at a time.
local function on_act(self)
  -- rule: A poison cloud ages one tick per act and vanishes on the act after its lifetime reaches 0
  --   (without acting further that tick).
  -- tests: EffectAct.cloud_ages_and_queues_its_drift_walk, EffectAct.cloud_expired
  -- tests: SemLibsCloud.an_expired_cloud_vanishes_without_acting_that_tick
  if self:lifetime() > 0 then
    self:set_lifetime(self:lifetime() - 1)
  else
    self.dead = 1
    self:death()
    return true
  end
  -- rule: A cloud fades out over its last 8 ticks: each such tick adds 3 to its invisibility and
  --   every tick spends 1, so it thins by a net 2 a tick as it expires; a fresh cloud's starting
  --   invisibility drains 1 a tick.
  -- tests: SemLibsCloud.a_cloud_fades_out_over_its_last_eight_ticks
  if self:lifetime() < 8 then
    self:set_invisibility_left(self:invisibility_left() + 3)
  end
  if self:invisibility_left() > 0 then
    self:set_invisibility_left(self:invisibility_left() - 1)
  end

  -- Foes only: friendlies stand in the cloud unharmed (the C++ kept the
  -- friendly branch "for now", and parity keeps it forever).
  local foes = og.find_foes_in_range("ob", self:sizex(), self)
  -- rule: A cloud hurts every foe whose box overlaps its own, each tick; friends stand in it
  --   unharmed, and a foe merely near (in the finder's range) but outside the box is unharmed.
  -- tests: PackLuaCloud.a_cloud_damages_the_foe_it_covers_and_spares_its_own_team
  -- tests: PackLuaCloud.a_foe_in_range_but_outside_the_cloud_box_is_unharmed
  -- tests: parity:effect_cloud_poison_scen99
  for i = 1, #foes do
    local w = foes[i]
    -- rule: A cloud hurts every foe whose box overlaps its own, each tick.
    -- tests: PackLuaCloud.a_cloud_damages_the_foe_it_covers_and_spares_its_own_team
    -- tests: PackLuaCloud.a_foe_in_range_but_outside_the_cloud_box_is_unharmed
    -- tests: parity:effect_cloud_poison_scen99
    if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
            w:xpos(), w:ypos(), w:sizex(), w:sizey()) then
      self:attack(w)
    end
  end

  -- Are we performing some action?
  -- rule: A cloud drifts by queued random walks: with a walk queued it executes one step; with none
  --   it queues a new walk of rand(20) steps in a random non-zero direction (draw order xd, yd,
  --   count).
  -- tests: EffectAct.cloud_ages_and_queues_its_drift_walk
  -- tests: EffectMorePaths.effect_cloud_queues_walk_then_executes_the_drift_step
  if self:s_has_commands() then
    -- do_command hides walkstep's result (stats.cpp keeps result == 1), so
    -- blockage is read back from state: with the setting on, a tick that
    -- moved nothing AND kept the facing is the frozen wall-wedge (#233) — a
    -- deflected slide moves, a pure turn tick changes curdir, and neither
    -- draws here, so the RNG stream on every moving path is the old game's.
    local held_x = self:xpos()
    local held_y = self:ypos()
    local held_dir = self:curdir()
    self:s_do_command()
    -- rule: With Gameplay Fixes on, a cloud wedged against a wall (its step moved nothing and did not
    --   even turn it) drops its stuck walk and picks a fresh random direction, so it never freezes in
    --   place (#233); off (the old game, which only calls do_command), the wedged cloud keeps its
    --   stuck walk until it runs out. A cloud that slid, moved or turned keeps its walk and draws
    --   nothing on both values.
    -- tests: EffectMorePaths.effect_cloud_queues_walk_then_executes_the_drift_step
    -- tests: SemLibsCloud.a_cloud_that_moved_or_turned_keeps_its_walk_and_draws_nothing
    -- tests: SemLibsCloud.a_wedged_cloud_stays_put_with_the_fixes_off_and_redraws_its_walk_with_them_on
    -- tests: EffectAct.cloud_wedged_against_wall_keeps_moving
    if gf.on() and self:xpos() == held_x then
      -- rule: With Gameplay Fixes on, a cloud wedged against a wall (its step moved nothing and did
      --   not even turn it) drops its stuck walk and picks a fresh random direction (#233).
      -- tests: SemLibsCloud.a_wedged_cloud_stays_put_with_the_fixes_off_and_redraws_its_walk_with_them_on
      -- tests: SemLibsCloud.a_cloud_that_moved_or_turned_keeps_its_walk_and_draws_nothing
      if self:ypos() == held_y then
        -- rule: With Gameplay Fixes on, a cloud wedged against a wall (its step moved nothing and
        --   did not even turn it) drops its stuck walk and picks a fresh random direction (#233).
        -- tests: SemLibsCloud.a_wedged_cloud_stays_put_with_the_fixes_off_and_redraws_its_walk_with_them_on
        -- tests: SemLibsCloud.a_cloud_that_moved_or_turned_keeps_its_walk_and_draws_nothing
        if self:curdir() == held_dir then
          self:s_clear_command()
          local xd, yd = 0, 0
          -- rule: A cloud's drift direction is never the zero step: the two -1..1 rolls are redrawn
          --   together until at least one is non-zero.
          -- tests: EffectMorePaths.effect_cloud_queues_walk_then_executes_the_drift_step
          -- tests: SemLibsCloud.a_cloud_never_drifts_by_the_zero_step
          while xd == 0 and yd == 0 do
            xd = og.rand(3) - 1
            yd = og.rand(3) - 1
          end
          self:s_add_command(C.COMMAND_WALK, og.rand(20), xd, yd)
        end
      end
    end
  else
    -- Two separate C++ statements, so the draw order is defined: xd then yd,
    -- re-rolled together until the step is non-zero.
    local xd, yd = 0, 0
    -- rule: A cloud's drift direction is never the zero step: the two -1..1 rolls are redrawn
    --   together until at least one is non-zero.
    -- tests: EffectMorePaths.effect_cloud_queues_walk_then_executes_the_drift_step
    -- tests: SemLibsCloud.a_cloud_never_drifts_by_the_zero_step
    while xd == 0 and yd == 0 do
      xd = og.rand(3) - 1
      yd = og.rand(3) - 1
    end
    self:s_add_command(C.COMMAND_WALK, og.rand(20), xd, yd)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:cloud  on_act = cloud.on_act
return {
  on_act = on_act,
}
