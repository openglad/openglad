-- core:ghost — behavior hooks transliterated from family_ghost.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types.

local FX_GHOST_SCARE = og.family_id("fx", "core:ghost_scare")

local function check_special_ai(self)
  return og.check_special_ai_distance(self, 130)
end

local function do_special(self)
  local newob = og.summon(self, "fx", FX_GHOST_SCARE)
  if not newob then
    return false
  end
  newob:set_ani_type(1)  -- ANI_SCARE
  -- xpos/ypos/sizex/sizey are shorts: /2 is C integer division.
  newob:setxy(self:xpos() + og.div(self:sizex(), 2) - og.div(newob:sizex(), 2),
              self:ypos() + og.div(self:sizey(), 2) - og.div(newob:sizey(), 2))
  return true
end

og.register_hooks("living", "core:ghost", {
  do_special = do_special,
  check_special_ai = check_special_ai,
})
