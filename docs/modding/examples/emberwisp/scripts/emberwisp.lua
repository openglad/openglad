-- example:emberwisp — behavior for the Ember Wisp example pack.
--
-- The family id string resolves against the descriptor's `name:` field
-- (lowercased, spaces to underscores); the namespace before the colon is
-- convention only. The determinism cookbook
-- (docs/lua-classpacks-design.md §3) applies to every line here: no
-- pairs/next, no float division in Lua, RNG only through og.rand, and no
-- mutable state outside the entity.

local C = og.C

-- A wisp with less ember than this in reserve cannot spit fire.
local FLARE_COST = 6.0

og.register_hooks("living", "example:emberwisp", {
  -- Wisps burn hot and unevenly: each one starts with a random slice of
  -- its magic pool already spent. Exactly one og.rand call, unconditional,
  -- so the RNG stream advances identically on every peer.
  on_create = function(self)
    local spent = og.rand(16)
    self:s_set_magicpoints(og.fsub(self:s_max_magicpoints(), spent))
    self:set_ani_type(C.ANI_WALK)
  end,

  -- Returning false here blocks the shot; anything else lets it through.
  -- A wisp that is nearly out of ember simply cannot fire, and one that
  -- can flares into ani_type 1 — the attack half of this pack's own
  -- animation table (rows 8..15 of `emberwisp_motion`).
  on_fire_weapon = function(self)
    if self:s_magicpoints() < FLARE_COST then
      return false
    end
    self:set_ani_type(C.ANI_ATTACK)
    self:set_cycle(0)
    return true
  end,
})
