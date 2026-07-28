-- core:ghost — scare-cloud special (cookbook: docs/lua-classpacks-design.md §3).

local FX_GHOST_SCARE = og.family_id("fx", "core:ghost_scare")

local function check_special_ai(self)
  return og.check_special_ai_distance(self, 130)
end

local function do_special(self)
  local scare = og.summon(self, "fx", FX_GHOST_SCARE)
  if not scare then
    return false
  end
  scare:set_ani_type(1)  -- ANI_SCARE
  -- center on self: sizes/coords are non-negative shorts, so // is the C /2
  scare:setxy(self:xpos() + self:sizex() // 2 - scare:sizex() // 2,
              self:ypos() + self:sizey() // 2 - scare:sizey() // 2)
  return true
end

og.register_hooks("living", "core:ghost", {
  do_special = do_special,
  check_special_ai = check_special_ai,
})
