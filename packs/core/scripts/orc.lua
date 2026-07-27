-- core:orc — behavior hooks transliterated from family_orc.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local C = og.C

-- og::combat::yell_radius (include/openglad/core/combat_math.h), trivially
-- inlined pure-integer helper (no og.* binding exists): legacy 160 + 20*L
-- with a flat cap at kYellRadiusCap = 420.
local function yell_radius(level)
  local raw = 160 + 20 * level
  if raw < 420 then
    return raw
  end
  return 420
end

-- og::combat::stun_total (include/openglad/core/combat_math.h), trivially
-- inlined pure-integer helper (no og.* binding exists): cur_raw < 0 is the
-- thaw-immunity phase (the add is DISCARDED and the raw value returned
-- unchanged); otherwise negative adds count as 0 and the sum caps
-- monotonically at kFrozenStunStackCap = 150.
local function stun_total(cur_raw, add)
  if cur_raw < 0 then
    return cur_raw
  end
  -- Spelled out rather than as `(add > 0) and add or 0`: a ternary is one
  -- statement on one line, so neither arm is separately measurable and an
  -- untested one costs nothing. Identical semantics — every value here is a
  -- number, and numbers are never falsy in Lua.
  local gain = 0
  if add > 0 then
    gain = add
  end
  local summed = cur_raw + gain
  local capped = 150
  if summed < 150 then
    capped = summed
  end
  if capped > cur_raw then
    return capped
  end
  return cur_raw
end

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- yell and 'freeze' foes
    if self:busy() > 0 then
      return false
    end
    self:set_busy(og.fadd(self:busy(), 2.0))

    local newlist = og.find_foes_in_range(
      "ob", yell_radius(self:s_level()), self)
    for i = 1, #newlist do
      local ob = newlist[i]
      if ob then
        local tempx
        if ob:has_guy() then
          tempx = ob:g_constitution()
        else
          -- (int32)(hitpoints / 30.0f): one float division, then trunc.
          tempx = og.trunc(og.fdiv(ob:s_hitpoints(), 30.0))
        end
        local tempx_clamped = 0
        if tempx > 0 then
          tempx_clamped = tempx
        end
        -- FLAGGED (two rng calls in one C++ expression, operand order
        -- unspecified): tempy = 10 + rng(level*10) - rng(con*10) is
        -- written LEFT-FIRST here. rng_.next(0) returns 0 WITHOUT
        -- advancing the rng state (irandom.h) while og.rand errors on
        -- n <= 0, hence the guards; level/tempx_clamped are never
        -- negative here so the C++ uint32 casts are value-preserving.
        local r1n = self:s_level() * 10
        local r1 = 0
        if r1n > 0 then
          r1 = og.rand(r1n)
        end
        local r2n = tempx_clamped * 10
        local r2 = 0
        if r2n > 0 then
          r2 = og.rand(r2n)
        end
        local tempy = 10 + r1 - r2
        if tempy < 0 then
          tempy = 0
        end
        -- (the C++ also TRACEs "orc yell stun add discarded: thaw
        -- immunity" when raw < 0 — TESTING-only diagnostics, no sim effect)
        local raw = ob:s_frozen_delay_raw()
        ob:s_set_frozen_delay(stun_total(raw, tempy))
      end
    end

    og.emit_sound(C.SOUND_ROAR)
  else
    -- eat corpse for health (specials 2/3/4 and the default case)
    if self:s_hitpoints() >= self:s_max_hitpoints() then
      return false
    end
    local newob = og.find_nearest_blood(self)
    if not newob then
      return false
    end
    local distance = og.trunc(self:distance_to_ob_center(newob))
    if distance > 24 then
      return false
    end
    self:s_set_hitpoints(og.fadd(self:s_hitpoints(),
                                 og.fmul(newob:s_level(), 5.0)))
    self:do_heal_effects(nil, self, og.i16(newob:s_level() * 5))
    if self:has_guy() then
      self:g_set_exp(self:g_exp() +
                     og.exp_from_action(self, newob, "eat_corpse", 0))
    end
    -- Print the eating notice
    og.emit_notification(
      og.entity_display_name(self, "Orc") .. " ate a corpse.")
    if self:s_hitpoints() > self:s_max_hitpoints() then
      self:s_set_hitpoints(self:s_max_hitpoints())
    end
    newob:set_dead(1)
    newob:death()
  end
  return true
end

local function check_special_ai(self)
  return og.check_special_ai_distance(self, 130)
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 14.0, 7.0, 6.0, 3.0)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

-- promotion_new_level (orc_promotion_level) is descriptor data, not one of
-- the registrable living hooks — it stays on the C++ descriptor.

og.register_hooks("living", "core:orc", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  set_difficulty = set_difficulty,
  level_up = level_up,
})
