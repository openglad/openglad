-- example:emberwisp — ember pool, flare gate, stun-burst special 1 (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C

-- Each wisp starts with a random slice of its ember already spent.
-- Exactly one draw, unconditional, so the RNG stream advances identically
-- on every peer (R4). The bound is a positive literal, so plain og.rand
-- is right — its error on n <= 0 is a tripwire worth keeping; og.rand0 is
-- for bounds that can legitimately reach zero (see flare_burst).
local function on_create(self)
  local spent = og.rand(16)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.max_magicpoints, spent)
  self.ani_type = C.ANI_WALK
end

-- Returning false blocks the shot: a wisp below the flare cost cannot
-- fire. One that can flares into ani_type 1 — rows 8..15 of this pack's
-- own animation table.
local function on_fire_weapon(self)
  if self.magicpoints < og.tuning(self).flare_cost then
    return false
  end
  self.ani_type = C.ANI_ATTACK
  self:set_cycle(0)
  return true
end

-- Special 1, FLARE BURST: vent every point of ember above the burn floor
-- as a stunning flash. Answering false means "did not fire" — the engine
-- then skips the special's descriptor MP cost.
local function flare_burst(self)
  local t = og.tuning(self)
  local ember = og.trunc(self.magicpoints) - t.burn_floor
  if ember <= 0 then
    return false
  end
  -- Tuning is modder data: clamp it into a sane sim window before use.
  local range = og.clamp(t.burst_range, C.GRID_SIZE, 320)
  local foes = og.foes_in_range(self, range)
  for i = 1, #foes do
    -- One stun roll per foe, in list order (R4). The bound is tuning-
    -- driven and may be zero: og.rand0 answers 0 then WITHOUT advancing
    -- the stream (IRandom::next(0) semantics).
    local roll = og.rand0(self.level * t.stun_per_level)
    -- add_frozen_stun is combat_math.stun_total fused with the setter;
    -- the thaw-immunity discard and the 150 cap are its policy, not ours.
    foes[i]:add_frozen_stun(t.stun_base + roll)
  end
  -- magicpoints and busy are C++ floats: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, ember)
  self.busy = og.fadd(self:busy(), 4.0)
  og.emit_positional_sound(self, C.SOUND_EXPLODE)
  return true
end

og.register_hooks("living", "example:emberwisp", {
  on_create = on_create,
  on_fire_weapon = on_fire_weapon,
  -- The specials table replaces a hand-written current_special() ladder:
  -- [n] runs for special n, `default` (absent here) catches the rest, and
  -- a table with neither entry makes that cast a successful no-op.
  specials = {
    [1] = flare_burst,
  },
})
