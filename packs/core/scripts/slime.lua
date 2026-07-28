-- core:#8/#9/#10 (slime trio) — split on death/ani-complete, grow specials (cookbook: docs/lua-classpacks-design.md §3).
-- All three living descriptors share the registry name "SLIME", so the
-- numeric escapes are mandatory (api-reference: "core:#<id>").

local C = og.C

local LIVING_SLIME        = assert(og.family_id("living", "core:#8"))
local LIVING_SMALL_SLIME  = assert(og.family_id("living", "core:#9"))
local LIVING_MEDIUM_SLIME = assert(og.family_id("living", "core:#10"))

-- guy.cpp calculate_level()/calculate_exp() inlined (pure integer helpers;
-- no og.* binding exists). The C++ recomputes calculate_exp(result) from
-- scratch each loop pass as a uint32 sum over k = 2..result of
-- (8000 + 2000*(k-1) + 4000*(k-2)); accumulating incrementally below yields
-- the exact same uint32 partial sums (same additions, same order), with the
-- uint32 wrap reproduced via og.mod by 2^32. The per-term int arithmetic
-- cannot overflow in any state reachable here (experience arrives as
-- uint32-exp/2 <= 2^31-1, so the loop ends by result ~ 27000).
local function calculate_level(experience)
  local result = 1
  local acc = 0  -- calculate_exp(1) == 0
  while acc <= experience do
    result = result + 1
    -- og.mod by 2^32 = the C++ uint32 wrap (see above)
    acc = og.mod(acc + (8000 + 2000 * (result - 1) + 4000 * (result - 2)),
                 4294967296)
  end
  return result - 1
end

local function slime_do_special(self)
  self:set_ani_type(C.ANI_SLIME_SPLIT)
  self:set_cycle(0)
  return true
end

local function slime_check_special_ai(self)
  -- living_count is the world's maintained head-count (og.living_count
  -- reads the same field the C++ read — an oblist recount would not be
  -- faithful; the counter can legitimately drift, e.g. editor map-resize).
  return og.living_count() < C.MAXOBS
end

-- slime_on_death / medium_slime_on_death share this body in the C++ too:
-- the dying blob is replaced by one next-size-down offspring.
local function split_on_death(self, offspring_family)
  self:set_dead(1)
  local child = og.add_ob("living", offspring_family)
  if not child then
    return true
  end
  child:set_floor(self:floor())  -- split child stays on our floor (A8)
  child.team = self.team
  child.level = self.level
  child:set_difficulty(self.level)
  child:set_foe(self:foe())
  child:set_leader(self:leader())
  if #self:s_name() > 0 then
    -- (sic: the DYING slime takes the offspring's fresh name — not the
    -- other way around)
    self:s_set_name(child:s_name())
  end
  if self:has_guy() then
    self:move_myguy_to(child)
  end
  child:center_on(self)
  self.hp = self.max_hp
  return true
end

local function slime_on_death(self)
  return split_on_death(self, LIVING_MEDIUM_SLIME)
end

local function medium_slime_on_death(self)
  return split_on_death(self, LIVING_SMALL_SLIME)
end

local function slime_on_ani_complete(self)
  if self:ani_type() ~= C.ANI_SLIME_SPLIT then
    return false
  end

  self:set_ani_type(C.ANI_WALK)
  self:set_cycle(0)
  self:transform_to("living", LIVING_SMALL_SLIME)
  self:setxy(self:xpos() - 10, self:ypos() + 10)

  local child = og.add_ob("living", LIVING_SMALL_SLIME)
  if not child then
    return true
  end
  child:set_floor(self:floor())  -- split child stays on our floor (A8)
  child:setxy(self:xpos() + 12, self:ypos() - 12)
  self:transfer_stats(child)
  local exp_needed = 1000 * self.level  -- uint32 cast as above
  if exp_needed < 0 then
    exp_needed = exp_needed + 4294967296
  end
  if child:has_guy() and child:g_exp() < exp_needed then
    child:clear_myguy()
    child:s_set_name("SLIME")
    child.level = calculate_level(self:g_exp() // 2)
  elseif child:has_guy() then
    -- High-exp split: both halves level to calculate_level(exp/2).
    -- g_upgrade_to_level re-dispatches the guy's family level_up hook
    -- internally (nested VM entry; budget arms outermost-only).
    local exp = self:g_exp() // 2
    -- og.i16: the C++ level destination is a short
    local new_level = og.i16(calculate_level(exp))
    self:g_upgrade_to_level(new_level)
    self:g_update_derived_stats(self)
    self:g_set_exp(exp)
    child:g_upgrade_to_level(new_level)
    child:g_update_derived_stats(child)
    child:g_set_exp(exp)
  end

  child.team = self.team
  child:set_foe(self:foe())
  child:set_leader(self:leader())
  return true
end

-- small_slime_do_special / medium_slime_do_special share this body in the
-- C++ too: grow into the next size up when there is room.
local function grow_into(self, grown_family)
  if self:spaces_clear() > 7 then
    self:transform_to("living", grown_family)
  else
    -- FLAGGED (cookbook checklist #4): the C++ passes TWO rng_.next(3)
    -- draws as arguments of one set_command() call, so the draw order is
    -- unspecified. Parity adjudicated RIGHT-first at THIS site (the y-draw
    -- happens before the x-draw): left-first diverged
    -- special_slime_1_scen99 at ~tick 83 (post-split AI GROW attempts),
    -- right-first matches the golden — unlike every previously adjudicated
    -- two-rng site, which matched left-first. og.rand(3) has a constant
    -- n > 0, so no n<=0 guard.
    local dy = og.rand(3) - 1
    local dx = og.rand(3) - 1
    self:s_set_command(C.COMMAND_WALK, 10, dx, dy)
    return false
  end
  return true
end

local function small_slime_do_special(self)
  return grow_into(self, LIVING_MEDIUM_SLIME)
end

local function medium_slime_do_special(self)
  return grow_into(self, LIVING_SLIME)
end

og.register_hooks("living", "core:#8", {   -- SLIME
  do_special = slime_do_special,
  check_special_ai = slime_check_special_ai,
  on_death = slime_on_death,
  on_ani_complete = slime_on_ani_complete,
})

og.register_hooks("living", "core:#9", {   -- SMALL_SLIME
  do_special = small_slime_do_special,
})

og.register_hooks("living", "core:#10", {  -- MEDIUM_SLIME
  do_special = medium_slime_do_special,
  on_death = medium_slime_on_death,
})
