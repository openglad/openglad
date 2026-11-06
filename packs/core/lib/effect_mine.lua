-- core:mine — the thief's buried mine: waits unseen, blows under a foe, sets off its neighbours (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local bomb = og.use("effect_bomb")
local hits = og.use("effect_common").hits
local FX_MINE = assert(og.family_id("fx", "core:mine"))
local FX_CLOUD = assert(og.family_id("fx", "core:cloud"))

-- No living is more than 64 px on a side (the giant skeleton), so any living
-- whose box can overlap a mine has its corner within 128 px (Manhattan, which
-- is what distance_to_ob measures) of the mine's corner. The box test in
-- trips() decides; this range only keeps the scan short.
local TRIP_SCAN = 128

-- A foe walks onto the mine: alive, on the mine's floor, on its feet (a
-- flier floats over), and overlapping the mine's box.
local function trips(self, foe)
  -- rule: A thief's mine is set off only by a foe on its floor who is on its feet: a flying family
  --   or a walker under a flight potion floats over it.
  -- tests: KitThief.flying_foe_does_not_trip_a_mine
  -- tests: KitThief.mine_detonates_under_a_foe_not_an_ally
  if foe:floor() ~= self:floor() then
    return false
  end
  if foe:s_query_bit_flags(C.BIT_FLYING) then
    return false
  end
  if foe:flight_left() > 0 then
    return false
  end
  return hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
              foe:xpos(), foe:ypos(), foe:sizex(), foe:sizey())
end

local function detonate(self)
  self.dead = 1
  self:death()
end

-- on_act: a mine set off by a neighbour (ani_type no longer ANI_WALK) blows
-- now; otherwise it ages, expires quietly when its lifetime runs out, blows
-- when a foe steps on it, and blinks its light.
local function on_act(self)
  -- rule: A mine lit by a neighbour's blast blows on its own next act (no recursion inside the
  --   first death).
  -- tests: KitThief.mines_chain_on_their_own_next_tick, KitThief.mine_is_laid_invisible_and_waits
  if self:ani_type() ~= C.ANI_WALK then
    detonate(self)
    return true
  end
  self.lifetime = self:lifetime() - 1
  -- rule: A mine that nobody steps on for its lifetime (600 + 30 per level ticks) expires quietly:
  --   hp is set negative so its death makes no blast.
  -- tests: KitThief.mine_expires_quietly
  -- tests: SemMine.an_expired_mine_goes_with_hp_minus_one_and_no_blast
  if self:lifetime() <= 0 then
    -- A quiet expiry: on_death sees the negative hp and makes no blast.
    self.hp = -1
    detonate(self)
    return true
  end
  local foes = og.find_foes_in_range("ob", TRIP_SCAN, self)
  -- plumbing: scan of foes within 128; rule below
  for i = 1, #foes do
    -- rule: A mine blows under the first foe that trips it; allies walk over their own mines;
    --   otherwise it blinks (frame = (world_tick/8) % 2).
    -- tests: KitThief.mine_detonates_under_a_foe_not_an_ally
    -- tests: KitThief.mine_is_laid_invisible_and_waits
    if trips(self, foes[i]) then
      detonate(self)
      return true
    end
  end
  -- world_tick is never negative; og.div/og.mod kept for the cookbook.
  self:set_frame(og.mod(og.div(og.world_tick(), 8), 2))
  return true
end

-- At a high enough level a mine leaves a short poison puff where it blew.
local function leave_puff(self, t)
  local puff = og.add_ob("fx", FX_CLOUD)
  -- rule: A high-level mine's poison puff is skipped in a full world (the blast still happens).
  -- tests: KitThief.high_level_mine_leaves_a_puff
  -- tests: SemMine.a_full_world_skips_the_mines_puff_but_not_its_blast
  if not puff then
    return
  end
  puff:set_owner(self:owner())
  puff.team = self.team
  puff:set_floor(self:floor())
  puff:center_on(self)
  puff:set_ignore(1)
  puff.lifetime = t.mine_puff_ticks
  puff:set_invisibility_left(10)
  puff.ani_type = C.ANI_SPIN
  puff.damage = og.div(self.level, 2)
end

-- Every other live mine of the same team within the chain reach is lit: it
-- blows on its own next act (no recursion inside this death). Team, not
-- owner, so the mines a dead thief left behind still set each other off.
local function light_neighbours(self, t)
  local obs = og.oblist()
  -- plumbing: walks the whole oblist; rule below
  for i = 1, #obs do
    local ob = obs[i]
    -- rule: A mine's blast lights every other live mine of the same TEAM (not owner, so a dead
    --   thief's mines still chain) within 24 px; each blows on its own next act. Foe mines and far
    --   mines are not lit.
    -- tests: KitThief.mines_chain_on_their_own_next_tick
    -- tests: SemMine.a_blast_lights_only_live_mines_of_its_team_nearby
    if ob ~= self
        and ob:order() == C.ORDER_FX
        and ob:family() == FX_MINE
        and ob:dead() == 0
        and ob.team == self.team
        and self:distance_to_ob(ob) <= t.mine_chain_px then
      ob.ani_type = C.ANI_EXPLODE
    end
  end
end

-- on_death: the bomb's own blast (owner, level, damage, floor, centre), then
-- the chain and the puff. A mine that ran out of time (hp < 0) just goes.
local function on_death(self)
  -- rule: An expired mine (hp < 0) leaves without a blast, chain or puff.
  -- tests: KitThief.mine_expires_quietly, KitThief.mines_chain_on_their_own_next_tick
  -- tests: KitThief.high_level_mine_leaves_a_puff, SemMine.an_expired_mine_lights_no_neighbour
  if self.hp < 0 then
    return true
  end
  local t = og.tuning(self)
  bomb.bomb_on_death(self)
  light_neighbours(self, t)
  -- rule: From level 7 a mine also leaves a short poison puff where it blew.
  -- tests: KitThief.high_level_mine_leaves_a_puff
  if self.level >= t.mine_puff_level then
    leave_puff(self, t)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:mine  on_act = mine.on_act, on_death = mine.on_death
return {
  on_act = on_act,
  on_death = on_death,
}
