-- core lib: effect_ember — IMMOLATE's burning footprint: smoulders for its lifetime and burns the foes standing on it (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local hits = og.use("effect_common").hits

-- The finder measures from top-left corner to top-left corner, so a radius
-- of the burning box's own size misses a foe that overlaps it from the far
-- side (the ember under a foe's middle, a diagonal touch). The search
-- therefore reaches far enough for any foe box up to LIVING_BOX on a side
-- (the golem and the giant skeleton are the biggest core livings; a test
-- pins that they fit), and the box test decides who really touches.
local LIVING_BOX = 64

-- The living foes on ent's floor whose box overlaps ent's box grown by
-- grow px on every side, in the finder's order. Shared with IMMOLATE's
-- contact burn (lib/kit_elemental.lua).
local function foes_touching(ent, grow)
  local reach = ent:sizex() + ent:sizey() + 2 * LIVING_BOX + 2 * grow
  local foes = og.find_foes_in_range("ob", reach, ent)
  local touching = {}
  for i = 1, #foes do
    local w = foes[i]
    if w:order() == C.ORDER_LIVING
        and w:floor() == ent:floor()
        and hits(ent:xpos() - grow, ent:ypos() - grow,
                 ent:sizex() + 2 * grow, ent:sizey() + 2 * grow,
                 w:xpos(), w:ypos(), w:sizex(), w:sizey()) then
      touching[#touching + 1] = w
    end
  end
  return touching
end

-- ember_on_act: count down, show the dying frame near the end, and every
-- ember_pulse ticks burn each living foe on this floor whose box overlaps
-- the ember. The owner may be gone (an ember outlives its elemental): the
-- ember keeps its own team and damage.
local function on_act(self)
  local t = og.tuning(self)
  self.lifetime = self:lifetime() - 1
  if self:lifetime() <= 0 then
    self.dead = 1
    self:death()
    return true
  end
  if self:lifetime() < t.ember_dying then
    self:set_frame(1)
  end
  if og.mod(self:lifetime(), t.ember_pulse) == 0 then
    local foes = foes_touching(self, 0)
    for i = 1, #foes do
      self:attack(foes[i])
    end
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:ember  on_act = ember.on_act
-- lib/kit_elemental.lua uses foes_touching for IMMOLATE's contact burn.
return {
  on_act = on_act,
  foes_touching = foes_touching,
}
