-- core lib: weapon_banner — the orc captain's war banner: allies around it regenerate and never flee, weaker foes near it are frightened, and it stands until chopped down or worn out (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C

local FX_FLASH = assert(og.family_id("fx", "core:flash"))

local M = {}

-- One pulse of the aura. "Never flees" is not here: og::sim::fearless reads
-- the family's rally_radius every time a walker would run.
local function pulse(self, t)
  local friends = og.find_friends_in_range("ob", t.banner_radius, self)
  for i = 1, #friends do
    local w = friends[i]
    if w:floor() == self:floor() then
      w:heal_clamped(t.banner_regen)
    end
  end
  local foes = og.find_foes_in_range("ob", t.banner_radius, self)
  for i = 1, #foes do
    local w = foes[i]
    local living = w:order() == C.ORDER_LIVING
    local weaker = w.level < self.level
    if living and weaker then
      -- s_force_fright skips a fearless walker by itself.
      local dx = og.sign(w:xpos() - self:xpos())
      local dy = og.sign(w:ypos() - self:ypos())
      w:s_force_fright(t.banner_fright, dx, dy)
    end
  end
end

-- core:war_banner on_animate: runs every tick (the banner is planted with a
-- non-walk ani_type). A banner outlives its captain: weap::act hands a
-- dead owner's banner to itself, and its team stays its own.
function M.on_animate(self)
  local t = og.tuning(self)
  self:set_frame(og.mod(og.div(og.world_tick(), 4), 4))
  if og.mod(og.world_tick(), t.banner_pulse) == 0 then
    pulse(self, t)
  end
  local lifetime = self:lifetime()
  if lifetime < 1 then
    self.dead = 1
    self:death()
    return true
  end
  self.lifetime = lifetime - 1
  return true
end

-- core:war_banner on_death: chopped down or worn out.
function M.on_death(self)
  local flash = og.add_ob("fx", FX_FLASH)
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(self:floor())
    flash:center_on(self)
  end
  og.emit_sound(C.SOUND_EXPLODE)
  og.emit_notification("The banner falls.")
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:war_banner  on_animate = weapon_banner.on_animate, on_death = weapon_banner.on_death
return M
