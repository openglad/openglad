-- core:door — broken door hands its spot to the opening effect (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local FX_DOOR_OPEN = assert(og.family_id("fx", "core:door_open"))

-- door_on_death: a broken door hands its spot to the DOOR_OPEN effect that
-- plays the opening animation.
local function on_death(self)
  local opened = og.add_weap_ob("fx", FX_DOOR_OPEN)
  if not opened then
    return false
  end
  opened:set_ani_type(C.ANI_DOOR_OPEN)
  opened:set_floor(self:floor())  -- opened door stays on its floor
  opened:setxy(self:xpos(), self:ypos())
  opened.level = self.level
  opened.team = self.team
  -- What way are we 'facing'? A wall above us picks FACE_RIGHT.
  -- (og.query_genre reads the default-floor smoother, like the C++
  -- `current_game->world->mysmoother` this replaces; the tile conversion
  -- is og.div — C trunc.)
  if og.query_genre(og.div(self:xpos(), C.GRID_SIZE),
                    og.div(self:ypos(), C.GRID_SIZE) - 1) == C.TYPE_WALL then
    opened:set_curdir(C.FACE_RIGHT)
  else
    -- Kept verbatim: the original writes SELF's facing here, not the new
    -- effect's.
    self:set_curdir(C.FACE_UP)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:door  on_death = door.on_death
return {
  on_death = on_death,
}
