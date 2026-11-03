-- core:bomb + core:explosion — the thief's bomb and its blast (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))

-- Ported from the former effect.cpp compute_explosion_range(level, skip_exit); this is the only copy now.
local function compute_explosion_range(level, skip_exit)
  -- shim kept: narrows to int32 like the C++ destination.
  local range = og.i32(level * 4)
  -- rule: A blast's reach is 4 px per owner level clamped to 16..96 px; a 'magical' blast
  --   (skip_exit > 0 on the explosion, e.g. a mage-family blast) collapses to the 16-px minimum
  --   whatever the level. The finder then searches 15 + range.
  -- tests: SemLibsBomb.a_blast_reaches_four_px_a_level_capped_at_96_and_a_magical_one_16
  -- deferred: #483 divergence: the current game floors a non-magical blast at 16 px for owner
  --   levels 1-3 (og.clamp(range, 16, 96), from the C++ modernization dc1a2a89); the old game has
  --   no floor (4*level, so level 2 reaches 15+8); levels 4+, the 96 cap and skip_exit -> 16 are
  --   asserted
  if skip_exit > 0 then
    -- The original called this "magical, ie mage": magical blasts collapse
    -- to the 16-pixel minimum instead of keeping the caster's level range.
    range = 0
  end
  -- set max range to about 6 tiles
  range = og.clamp(range, 16, 96)
  return range
end

-- bomb_on_death: the bomb itself carries no blast; dying it spawns the
-- EXPLOSION effect centred on itself, inheriting owner/level/damage.
local function bomb_on_death(self)
  -- rule: A bomb (or blast) whose thrower is gone or dead becomes its own owner: it still blows,
  --   with its own level for the blast's reach and shove, and no one counts as the owner's ally
  --   (its own team gets full damage).
  -- tests: EffectMorePaths.effect_batch3_bomb_owner_dead_fallback_path
  -- tests: KitElemental.an_unmarked_ownerless_blast_hits_its_own_colour_in_full
  -- tests: parity:effect_explosion_ally_tier_scen99, parity:effect_explosion_range_scen99
  if not self:owner() or self:owner():dead() ~= 0 then
    self:set_owner(self)
  end
  og.emit_sound(C.SOUND_EXPLODE)
  local blast = og.add_ob("fx", FX_EXPLOSION)
  -- rule: A bomb that goes off in a full world still makes its explosion sound and dies, but no
  --   blast hurts anyone.
  -- tests: EffectMorePaths.effect_batch3_bomb_owner_dead_fallback_path
  -- unreachable: divergence: with no explosion entity the current game still sounds and returns
  --   true; the old game's FAMILY_BOMB death dereferences add_ob's result unchecked (no defined
  --   behaviour); reachable only through a failing entity factory | triage: Unreachable in play:
  --   GameWorld::add_ob returns nullptr only when the entity factory fails (no factory or unknown
  --   family), and the world has no object cap.
  if not blast then
    return true
  end
  blast:set_owner(self:owner())
  blast.hp = 0
  blast.level = self:owner().level
  blast.ani_type = C.ANI_EXPLODE
  blast:set_floor(self:floor())  -- detonate on the bomb's floor
  blast:center_on(self)
  blast.damage = self:damage()
  return true
end

-- explosion_on_death: the blast proper. Everything in range on our floor is
-- shoved away from the centre and struck — the owner at quarter damage,
-- allies at half, everyone else in full.
local function explosion_on_death(self)
  -- rule: A bomb (or blast) whose thrower is gone or dead becomes its own owner.
  -- tests: EffectMorePaths.effect_batch3_bomb_owner_dead_fallback_path
  -- tests: KitElemental.an_unmarked_ownerless_blast_hits_its_own_colour_in_full
  -- tests: parity:effect_explosion_ally_tier_scen99, parity:effect_explosion_range_scen99
  if not self:owner() or self:owner():dead() ~= 0 then
    self:set_owner(self)
  end
  local range = compute_explosion_range(self:owner().level,
                                        self:skip_exit())
  local nearby, nearby_count = og.find_in_range("ob", 15 + range, self)

  -- DamageTile goes out as an event (the sim never touches the screen).
  og.emit_event(C.EVENT_DAMAGE_TILE,
                self:xpos() + self:sizex() // 2,
                self:ypos() + self:sizey() // 2)
  -- rule: A blast always scorches the tile under its centre (EVENT_DAMAGE_TILE) even when no object
  --   is in reach; with nothing in reach it hurts nothing and answers 'not handled'.
  -- tests: EffectMorePaths.effect_death_explosion_shoves_nearby_targets
  -- tests: EffectAct.effect_death_explosion_blasts_only_inside_its_radius
  -- tests: SemLibsBomb.a_blast_with_nobody_in_reach_scorches_its_centre_and_answers_false
  if nearby_count < 1 then
    return false
  end

  -- plumbing: iterates the floor-blind finder's result; rules below
  for i = 1, #nearby do
    local w = nearby[i]
    -- An explosion never blasts through solid floors: only same-floor
    -- walkers take damage. find_in_range is deliberately floor-blind,
    -- so the damage loop filters instead.
    -- rule: A blast shoves and strikes only live walkers, weapons and generators on its own floor:
    --   treasure and effects are never blown about, the dead are left alone, and nothing on another
    --   floor is touched. A 'magical' blast (skip_exit > 0) never touches its own owner at all: no
    --   shove, no quarter damage.
    -- tests: EffectMorePaths.effect_death_explosion_shoves_nearby_targets
    -- tests: PackLuaExplosion.a_blast_throws_even_an_invulnerable_bystander_clear
    -- tests: ZAxisFxFloor.explosion_damage_never_crosses_floors
    -- tests: SemLibsBomb.a_blast_leaves_other_floors_treasure_effects_and_the_dead_alone
    -- tests: PackLuaExplosion.a_skip_exit_blast_leaves_its_owner_alone_entirely
    -- tests: PackLuaExplosion.a_blast_softens_but_does_not_spare_its_own_side
    -- tests: parity:effect_bomb_bystander_scen99
    if w and w:dead() == 0
        and w:floor() == self:floor()
        and w:order() ~= C.ORDER_TREASURE
        and w:order() ~= C.ORDER_FX
        and (self:skip_exit() == 0
             or w ~= self:owner()) then
      --shove the target
      local dx = og.sign(w:xpos() - self:xpos())
      local dy = og.sign(w:ypos() - self:ypos())
      -- Set the distance to 'shove' by explosion
      local shove = og.min(2 + self:owner().level // 15, 8)
      w:s_force_command(C.COMMAND_WALK, shove, dx, dy)
      -- Damage (attack) the object
      -- rule: A blast strikes its owner at a quarter of its damage, the owner's living allies at
      --   half, and everyone else in full; a SUPERNOVA blast (New Specials, caster already dead)
      --   spares its own team colour half instead.
      -- tests: PackLuaExplosion.a_blast_softens_but_does_not_spare_its_own_side
      -- tests: KitElemental.an_unmarked_ownerless_blast_hits_its_own_colour_in_full
      -- tests: KitElemental.supernova_spends_all_health_blasts_a_ring_and_the_starburst_follows
      -- tests: parity:effect_explosion_ally_tier_scen99
      if w == self:owner() then
        local full_damage = self:damage()
        -- shim kept: damage is a C++ float: the quarter cut rounds through float.
        self.damage = og.fdiv(full_damage, 4.0)
        self:attack(w)
        self.damage = full_damage
      elseif (self:owner():dead() == 0 and self:owner():is_friendly(w))
          or ((self:kit_state() & C.KIT_NOVA) ~= 0
              and w:team_num() == self:team_num()) then
        -- A SUPERNOVA blast's caster is gone when it goes off, so the blast
        -- spares its own team half by team colour (New Specials).
        local full_damage = self:damage()
        -- shim kept: damage is a C++ float: the half cut rounds through float.
        self.damage = og.fdiv(full_damage, 2.0)
        self:attack(w)
        self.damage = full_damage
      else
        self:attack(w)
      end
    end
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:bomb       on_death = bomb.bomb_on_death
--   core:explosion  on_death = bomb.explosion_on_death
return {
  bomb_on_death = bomb_on_death,
  explosion_on_death = explosion_on_death,
}
