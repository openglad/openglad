-- core:ghost — scare-cloud special (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local ai = og.use("ai")
local FX_GHOST_SCARE = assert(og.family_id("fx", "core:ghost_scare"))

local function do_special(self)
  -- The old "nifty scare thing" is a carrier; its on_death does the scare.
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
  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI gate
})
