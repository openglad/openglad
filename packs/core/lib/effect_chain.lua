-- core:chain — homing chain-lightning bolt; forks on strike (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The frame poke `self->set_frame(self->ani[curdir()][0])` reads the walker's
-- animation table through og.ani_frame(entity, row, index); nil covers every
-- case where the C++ `if (self->ani)` guard would have failed, so the
-- set_frame is simply skipped then.

local C = og.C
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local FX_CHAIN = assert(og.family_id("fx", "core:chain"))

local hits = og.use("effect_common").hits

-- chain_on_act: the bolt homes on its leader; on contact it detonates and
-- forks into up to rand(owner level)+1 new bolts aimed at nearby foes.
local function on_act(self)
  local leader = self:leader()
  -- lost our leader, etc.? kill us ..
  -- rule: A chain-lightning bolt that has lost its target, flown out its life (lineofsight) or lost
  --   its caster fizzles at once without striking.
  -- tests: EffectChainAndDoor.effect_chain_early_exit_and_movement_branches
  -- tests: EffectMorePaths.effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan
  -- tests: SemLibsChain.a_bolt_that_has_lost_its_target_fizzles_without_striking
  if not leader
      or self:lineofsight() < 1
      or not self:owner() then
    self.dead = 1
    self:death()
    return true
  end
  -- Are we at our leader? If so, attack him :)
  -- rule: A chain bolt strikes when its box touches its target's box: it leaves an explosion with
  --   its own damage, team and level on its floor, gives the struck foe 3 rounds of immunity from
  --   further bolts (skip_exit +3), plays the explode sound and dies.
  -- tests: EffectChainAndDoor.effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx
  -- tests: EffectMorePaths.effect_chain_lightning_hits_leader_and_spawns_explosion
  -- tests: EffectChainAndDoor.effect_chain_early_exit_and_movement_branches
  if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
          leader:xpos(), leader:ypos(), leader:sizex(), leader:sizey()) then
    local blast = og.add_ob("fx", FX_EXPLOSION)
    -- rule: A chain bolt striking in a full world simply dies: no explosion, no immunity on the
    --   target, no forks.
    -- tests: EffectMorePaths.effect_chain_lightning_hits_leader_and_spawns_explosion
    -- tests: SemLibsChain.a_bolt_striking_in_a_full_world_dies_without_a_blast_or_forks
    if not blast then
      self.dead = 1
      self:death()
      return true
    end
    blast:set_owner(self:owner())
    blast.team = self.team
    blast.level = self.level
    blast.damage = self:damage()
    blast.ani_type = C.ANI_EXPLODE
    blast:set_floor(self:floor())  -- strike on the bolt's floor
    blast:center_on(self)
    -- can't hit us for 3 rounds ..
    leader:set_skip_exit(leader:skip_exit() + 3)
    og.emit_sound(C.SOUND_EXPLODE)
    -- Now make new objects to seek out foes ..
    local t = og.tuning(self)
    -- shim kept: damage is a C++ float, but master STORES the cut in a
    -- Sint32 (`generic = (damage)/2`, master effect.cpp:429), so the float
    -- product truncates toward zero before it gates the fork or arms the
    -- successor bolt. Keeping the fraction made third-generation bolts
    -- carry 27.5 where classic carries 27.
    local fork_damage = og.trunc(og.fmul(self:damage(), t.fork_damage_mult))
    local foes, foe_count
    -- rule: Forks look for foes within 240 px plus half the caster's Intelligence when the caster
    --   is a hero (has a guy), else 240 + 5 px per bolt level.
    -- tests: SemLibsChain.a_heros_forks_reach_240_plus_half_int_a_bots_240_plus_5_a_level
    if self:owner():has_guy() then
      -- Int/2 stays a formula: a guy-stat conversion, not a tuning knob.
      foes, foe_count = og.find_foes_in_range(
        "ob", t.fork_range_base + self:owner():g_intelligence() // 2, self)
    else
      foes, foe_count = og.find_foes_in_range(
        "ob", t.fork_range_base + self.level * t.fork_range_per_level, self)
    end
    -- The old "are our offspring powerful enough at 1/2 our power?" gate:
    -- weak descendants stop reproducing instead of extending the chain.
    -- rule: A struck bolt forks only when there is a foe in reach and its children would carry more
    --   than 20 damage (half its own, truncated): weak bolts end the chain. The fork budget draws
    --   one RNG only when forking.
    -- tests: EffectChainAndDoor.effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx
    -- tests: EffectMorePaths.effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan
    -- tests: SemLibsChain.a_strike_forks_only_with_a_foe_in_reach_and_children_over_20
    if foe_count ~= 0 and fork_damage > t.fork_min_damage then
      -- One draw: the fork budget rolls off the owner's level.
      local forks_left = og.rand0(self:owner().level) + 1
      -- plumbing: iterates the fork scan; rules at effect_chain.lua:70, :76, :77
      for i = 1, #foes do
        local w = foes[i]
        -- rule: The fork budget (1..owner level) is spent per same-floor foe considered in list
        --   order, INCLUDING the struck leader and any immune foe: those use up a fork without
        --   receiving a bolt.
        -- tests: EffectChainAndDoor.effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx
        if forks_left <= 0 then
          break
        end
        -- Chain lightning must not arc through solid floors: skip foes on
        -- other floors (all-floor-0 on legacy levels, so single-floor
        -- behavior is byte-identical).
        -- rule: Chain lightning never forks to a foe on another floor, and such foes do not use up
        --   the fork budget.
        -- tests: EffectChainAndDoor.effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx
        -- tests: SemLibsChain.chain_lightning_never_forks_to_another_floor_nor_spends_a_fork_there
        if w:floor() == self:floor() then
          -- rule: A fork never re-targets the foe just struck nor a foe struck in the last three
          --   rounds (skip_exit immunity).
          -- tests: EffectMorePaths.effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan
          -- tests: EffectChainAndDoor.effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx
          -- tests: EffectMorePaths.effect_chain_lightning_hits_leader_and_spawns_explosion
          -- tests: SemLibsChain.a_fork_skips_the_struck_and_the_immune_but_they_spend_the_budget
          if w ~= self:leader() and w:skip_exit() < 1 then
            local bolt = og.add_ob("fx", FX_CHAIN)
            -- rule: A fork that cannot be created (world full) abandons the rest of the forks; the
            --   bolt itself returns WITHOUT dying this tick.
            -- tests: EffectMorePaths.effect_batch4_chain_guard_ownerless_and_non_myguy_foe_scan
            -- unreachable: GameWorld::add_ob returns nullptr only when the entity factory fails,
            --   never in play (triage); a test could reach it only by swapping the factory as
            --   test_kit_ghost does, and the arm it would pin (the bolt left alive, Lua true vs the
            --   old game's return 0) is the triaged suspect | triage: Unreachable in play:
            --   GameWorld::add_ob returns nullptr only when the entity factory fails (no factory or
            --   unknown family), and the world has no object cap.
            if not bolt then
              return true
            end
            bolt:set_owner(self:owner())
            bolt:set_leader(w)
            bolt.level = self.level
            bolt:s_set_bit_flags(C.BIT_MAGICAL, 1)
            bolt.damage = fork_damage
            bolt.team = self.team
            bolt:set_floor(self:floor())  -- seek on our floor
            bolt:center_on(self)
          end
          forks_left = forks_left - 1
        end
      end
    end

    self.dead = 1
    self:death()
    return true
  end
  -- Move toward our leader ..
  self:set_lineofsight(self:lineofsight() - 1)
  local distance = self:distance_to_ob_center(leader)
  -- shim kept: stepsize is a C++ float: per-op float rounding.
  -- rule: A homing chain bolt steps toward its target's corner at up to stepsize per axis, burning
  --   one tick of its life; once within two steps of the target's centre it snaps onto the target
  --   (and strikes next tick).
  -- tests: EffectChainAndDoor.effect_chain_early_exit_and_movement_branches
  -- tests: EffectMorePaths.effect_batch3_chain_snap_to_leader_and_effect_death_guard
  if distance > og.fmul(self:stepsize(), 2) then
    local xd, yd = 0, 0
    -- rule: Each axis of a homing step moves at most stepsize toward the target and never past it;
    --   an aligned axis does not move; the bolt faces the direction it flies.
    -- tests: EffectMorePaths.effect_batch6_chain_small_delta_else_branches
    -- tests: EffectChainAndDoor.effect_chain_movement_axis_delta_branches
    -- tests: EffectMorePaths.effect_batch4_chain_movement_negative_delta_branch
    -- tests: SemLibsChain.a_homing_bolt_faces_the_way_it_flies
    if leader:xpos() > self:xpos() then
      xd = og.min(self:stepsize(), leader:xpos() - self:xpos())
    elseif leader:xpos() < self:xpos() then
      xd = og.max(leader:xpos() - self:xpos(), -self:stepsize())
    end
    -- rule: Part of the rule above that begins "Each axis of a homing step moves at most ..."
    -- tests: EffectMorePaths.effect_batch6_chain_small_delta_else_branches
    -- tests: EffectChainAndDoor.effect_chain_movement_axis_delta_branches
    -- tests: EffectMorePaths.effect_batch4_chain_movement_negative_delta_branch
    -- tests: SemLibsChain.a_homing_bolt_faces_the_way_it_flies
    if leader:ypos() > self:ypos() then
      yd = og.min(self:stepsize(), leader:ypos() - self:ypos())
    elseif leader:ypos() < self:ypos() then
      yd = og.max(leader:ypos() - self:ypos(), -self:stepsize())
    end
    -- Set our facing?
    -- shim kept: facing() takes ints; xd/yd are floats: C truncation.
    self:set_curdir(self:facing(og.trunc(xd), og.trunc(yd)))
    local frame = og.ani_frame(self, self:curdir(), 0)
    -- plumbing: og.ani_frame nil = the C++ `if (ani)` guard; skips set_frame on a family without an
    --   animation table
    if frame ~= nil then
      self:set_frame(frame)
    end
    -- shim kept: worldx/worldy are C++ floats: per-op float rounding.
    self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
  else
    self:center_on(leader)
  end
  return true  -- skip default animate/die
end

-- The declarations that reference this module (packs/core/families/):
--   core:chain  on_act = chain.on_act
return {
  on_act = on_act,
}
