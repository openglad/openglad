-- core:tree, core:blood, core:circle_protection, core:glow and
-- core:sprinkle — weapon behavior hooks transliterated from
-- src/gameplay/families/weapon_family_animate.cpp. Cookbook
-- (docs/lua-classpacks-design.md §3) applies: og.div/og.mod are not needed
-- here (the only arithmetic is exact integer addition and multiplication),
-- og.i8 reproduces the `static_cast<signed char>` on the cycle write, og.u8
-- the `static_cast<unsigned char>` on the facing read, and og.i16 the
-- `static_cast<short>` on the freeze roll.
--
-- CALLER CONTRACT (weap::animate): a hook returning FALSE makes the caller
-- run death(). tree/blood and glow always return true — glow calls death()
-- itself, exactly as the C++ did — while circle_protection returns false to
-- hand the death off, and must NOT call death() on its own.

local C = og.C

-- constants/geometry only, no sim state (R6)
local NUM_FACINGS = C.NUM_FACINGS
local SPRINKLE_REFRESH_OWNER_LEVEL = 21   -- combat_math.h kSprinkleRefreshOwnerLevel
local SPRINKLE_REFRESH_FLOOR = 10         -- combat_math.h kSprinkleRefreshFloor

-- weapon_animate_step: the file-local helper of the same name. Advances the
-- weapon's animation by one frame with full bounds checking and reports
-- whether the advanced cycle reached the sequence's -1 sentinel.
--
-- curdir, ani_type and cycle may originate from an untrusted snapshot, so
-- the facing and the animation type are bounded before the row is addressed
-- and the cycle is bounded against the row length. The C++ guards that
-- remain -- `if (!self->ani)`, the ani_count bound, a null row, and a row
-- with no sentinel inside 128 frames -- are ALL the cases where
-- og.ani_row() returns nil, and an empty table is its `seq_len <= 0` stop;
-- every one of them means "no such sequence", i.e. stop (return true).
local function weapon_animate_step(self)
  local dir_index = og.u8(self:curdir())  -- C++ (int)(unsigned char)curdir()
  if dir_index < 0 or dir_index >= NUM_FACINGS then
    dir_index = 0
  end
  local type_index = self:ani_type()
  if type_index < 0 then
    type_index = 0
  end
  local ani_index = dir_index + type_index * NUM_FACINGS

  local seq = og.ani_row(self, ani_index)
  if not seq then
    return true
  end
  local seq_len = #seq
  if seq_len <= 0 then
    return true
  end

  local c = self:cycle()
  if c < 0 or c >= seq_len then
    c = 0
  end
  self:set_frame(seq[c + 1])  -- Lua arrays are 1-based; the C++ index is c
  c = c + 1                   -- advance into [1, seq_len]
  -- `seq[c] == -1` in C++: the sentinel sits at index seq_len and nowhere
  -- earlier (that is what seq_len means), so it is reached exactly when the
  -- advanced cycle equals the trimmed row length.
  local ended = (c == seq_len)
  self:set_cycle(og.i8(c))
  return ended
end

-- --- TREE and BLOOD: simple animation cycling ---

local function tree_blood_on_animate(self)
  if self:ani_type() > 1 then
    self:set_ani_type(0)
  end
  if weapon_animate_step(self) then
    self:set_ani_type(0)
    self:set_cycle(0)
  end
  return true
end

-- --- CIRCLE_PROTECTION: orbit owner ---

local function circle_protection_on_animate(self)
  local owner = self:owner()
  if not owner
      or owner:dead() ~= 0
      or self:s_hitpoints() <= 0 then
    self:set_dead(1)
    return false  -- let the caller's default death handling proceed
  end
  self:center_on(owner)
  return true
end

-- --- GLOW: pulse animation with lifetime ---

local function glow_on_animate(self)
  if self:ani_type() > 2 then  -- illegal case
    self:set_ani_type(2)       -- pulse case
  end
  if weapon_animate_step(self) then
    self:set_ani_type(2)       -- pulse
    self:set_cycle(0)
  end
  local lifetime = self:lifetime()
  self:set_lifetime(lifetime - 1)
  if lifetime < 1 then
    self:set_dead(1)
    self:death()
  end
  return true
end

-- --- SPRINKLE: freeze foes on hit ---

-- The C++ dereferences `owner` unconditionally, so a sprinkle with no owner
-- was already undefined behavior there; here it is a clean script error.
local function sprinkle_on_hit_target(self, target, owner)
  if target:order() ~= C.ORDER_LIVING then
    return true
  end
  local con = 0
  if target:has_guy() then
    con = target:g_constitution()
  end
  -- Runaway-specials §2.6: the roll is ALWAYS drawn (RNG stream identical
  -- to master at every level); only the SET below is gated.
  local roll = og.i16(og.freeze_duration(owner:s_level(), con))
  -- §2.6b refresh gate: a high-level faerie may not re-SET a freeze that is
  -- still running (mirrors the archmage charm_left<=10 gate). Level-gated
  -- >= 21 because the L20 weapon_sprinkle_emission golden's soldier thaw
  -- schedule must stay byte-identical.
  local refresh_gated = owner:s_level() >= SPRINKLE_REFRESH_OWNER_LEVEL
      and target:s_frozen_delay() > SPRINKLE_REFRESH_FLOOR
  -- §3.3 thaw immunity: never re-freeze inside the negative immunity phase
  -- (only the player-side drain writes it).
  local thaw_immune = target:s_frozen_delay_raw() < 0
  if not refresh_gated and not thaw_immune then
    target:s_set_frozen_delay(roll)
  end
  return true
end

og.register_hooks("weapon", "core:tree", {
  on_animate = tree_blood_on_animate,
})

og.register_hooks("weapon", "core:blood", {
  on_animate = tree_blood_on_animate,
})

og.register_hooks("weapon", "core:circle_protection", {
  on_animate = circle_protection_on_animate,
})

og.register_hooks("weapon", "core:glow", {
  on_animate = glow_on_animate,
})

og.register_hooks("weapon", "core:sprinkle", {
  on_hit_target = sprinkle_on_hit_target,
})
