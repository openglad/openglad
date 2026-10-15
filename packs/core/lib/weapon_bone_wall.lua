-- core lib: weapon_bone_wall — a bone wall segment stands for its lifetime and shows its cracks below half health (cookbook: docs/lua-classpacks-design.md §3).

local M = {}

-- pix/bonewall.png: frame 0 is the whole wall, frame 1 the cracked one.
local FRAME_WHOLE = 0
local FRAME_CRACKED = 1

-- weap::act runs this every tick, because the segment is built with a
-- non-walk ani_type (the family's init_ani_type), so the engine never
-- reaches its sitting or wandering arms. The CALLER CONTRACT is
-- circle_protection's: returning false hands the death to weap::animate,
-- which runs death() on a segment this hook has already marked dead.
function M.on_animate(self)
  local left = self:lifetime() - 1
  self:set_lifetime(left)
  if left <= 0 then
    self:set_dead(1)
    return false
  end
  -- hp and max_hp are C++ floats: the comparison is exact, the halving is
  -- one float division.
  if self.hp < og.fdiv(self.max_hp, 2.0) then
    self:set_frame(FRAME_CRACKED)
  else
    self:set_frame(FRAME_WHOLE)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:bone_wall  on_animate = bone_wall.on_animate
return M
