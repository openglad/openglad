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
  if self:ani_type() ~= C.ANI_WALK then
    detonate(self)
    return true
  end
  self.lifetime = self:lifetime() - 1
  if self:lifetime() <= 0 then
    -- A quiet expiry: on_death sees the negative hp and makes no blast.
    self.hp = -1
    detonate(self)
    return true
  end
  local foes = og.find_foes_in_range("ob", TRIP_SCAN, self)
  for i = 1, #foes do
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
  for i = 1, #obs do
    local ob = obs[i]
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
  if self.hp < 0 then
    return true
  end
  local t = og.tuning(self)
  bomb.bomb_on_death(self)
  light_neighbours(self, t)
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
