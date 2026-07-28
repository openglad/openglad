-- core:skeleton — ranged self-teleport special (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local lc = og.use("living_common")

local function handle_teleport(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  -- 18 px/level. Cast-cadence-hot: with no foe in range TUNNEL recasts at
  -- MP-regen cadence for a whole run, so in the generator scenarios a
  -- per-cast og.tuning read here regressed the tent instruction budget to
  -- +11.1% vs the Stage-3 baseline. R-KEEP-4 code constant, exactly like
  -- the ai.lua gate ranges (Stage 4 measured the same class: +23.8% on
  -- bones).
  self:teleport_ranged(self.level * 18)
  return true
end

local function check_special_ai(self)
  -- per-tick gate: the range stays code (R-KEEP-4); 5 * GRID_SIZE
  local _, foe_count = og.find_foes_in_range("ob", 5 * 16, self)
  return foe_count < 1
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 8, 12, 4, 4, 1)
end

local function do_special(self)
  if lc.mid_teleport(self) then
    return false
  end
  self.ani_type = C.ANI_TELE_OUT
  self:set_cycle(0)
  return true
end

og.register_hooks("living", "core:skeleton", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  level_up = level_up,
  handle_teleport = handle_teleport,
})
