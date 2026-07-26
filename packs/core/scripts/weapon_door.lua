-- core:door — weapon behavior hooks transliterated from
-- src/gameplay/families/weapon_family_door.cpp. Cookbook
-- (docs/lua-classpacks-design.md §3) applies: xpos/ypos are shorts, so the
-- tile conversion is og.div (C truncation), and every field move is an
-- integer copy that the setters narrow exactly like the C++ member types.

local C = og.C
local FX_DOOR_OPEN = og.family_id("fx", "core:door_open")

-- door_on_death: a broken door hands its spot to the DOOR_OPEN effect that
-- plays the opening animation.
local function on_death(self)
  local newob = og.add_weap_ob("fx", FX_DOOR_OPEN)
  if not newob then
    return false
  end
  newob:set_ani_type(C.ANI_DOOR_OPEN)
  newob:set_floor(self:floor())  -- opened door stays on its floor (A8)
  newob:setxy(self:xpos(), self:ypos())
  newob:s_set_level(self:s_level())
  newob:set_team_num(self:team_num())
  -- What way are we 'facing'?
  -- (og.query_genre reads the default-floor smoother, like the C++
  -- `current_game->world->mysmoother` this replaces.)
  if og.query_genre(og.div(self:xpos(), C.GRID_SIZE),
                    og.div(self:ypos(), C.GRID_SIZE) - 1) == C.TYPE_WALL then
    -- a wall above us?
    newob:set_curdir(C.FACE_RIGHT)
  else
    -- Kept verbatim: the original writes SELF's facing here, not newob's.
    self:set_curdir(C.FACE_UP)
  end
  return true
end

og.register_hooks("weapon", "core:door", {
  on_death = on_death,
})
