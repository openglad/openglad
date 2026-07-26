-- core treasure valuables — on_eat hooks transliterated from
-- src/gameplay/families/treasure_family_valuables.cpp (gold bar, silver bar,
-- life gem, key). Cookbook (docs/lua-classpacks-design.md §3) applies.
--
-- The three scoring families bank through og.award_score(team, points) — the
-- binding for the file-local award_score() helper, which adds to
-- world->m_score[team] and emits EventKind::ScoreChange. Teams outside the
-- score table are dropped inside the binding, exactly like the C++
-- is_valid_score_team() guard, so callers pass the raw team number.

local C = og.C
local FX_FLASH = og.family_id("fx", "core:flash")

-- gold_bar_on_eat: banks 200*level for the eater's team. Only team 0 or a
-- roster character (myguy) can cash a bar in.
local function gold_bar_on_eat(self, eater)
  if eater:team_num() == 0 or eater:has_guy() then
    og.award_score(eater:team_num(), 200 * self:s_level())
    self:set_dead(1)
    og.emit_sound(C.SOUND_MONEY)
  end
  return true
end

-- silver_bar_on_eat: same shape, 50*level.
local function silver_bar_on_eat(self, eater)
  if eater:team_num() == 0 or eater:has_guy() then
    og.award_score(eater:team_num(), 50 * self:s_level())
    self:set_dead(1)
    og.emit_sound(C.SOUND_MONEY)
  end
  return true
end

-- life_gem_on_eat: a dead character's banked hitpoints, claimable only by
-- the gem's own team. The flash pops where the gem sat (its floor, A8).
local function life_gem_on_eat(self, eater)
  if eater:team_num() ~= self:team_num() then
    return true  -- only our team can get these
  end
  local hp = self:s_hitpoints()  -- C++ std::max(0.0f, hitpoints())
  if hp < 0.0 then
    hp = 0.0
  end
  og.award_score(eater:team_num(), og.trunc(hp))
  local flash = og.add_ob("fx", FX_FLASH)
  if flash then
    flash:set_ani_type(C.ANI_EXPAND_8)
    flash:set_floor(self:floor())  -- flash where the gem sat (A8)
    flash:center_on(self)
  end
  self:set_dead(1)
  self:death()
  return true
end

-- key_on_eat: sets one bit of the eater's key mask. Re-touching a key the
-- eater already holds is silent.
local function key_on_eat(self, eater)
  local level = self:s_level()
  -- key_level_mask(): 1u << clamp(level, 0, 30) (key_mask.h).
  local shift = level
  if shift < 0 then
    shift = 0
  elseif shift > 30 then
    shift = 30
  end
  local key_mask = 1 << shift
  if (eater:keys() & key_mask) == 0 then  -- just got it?
    eater:set_keys(eater:keys() | key_mask)  -- ie, 2, 4, 8, 16...
    local who
    if eater:has_guy() then
      who = eater:g_name()
    else
      who = eater:s_name()
    end
    local message = string.format("%s picks up key %d", who, level)
    if eater:team_num() == 0 then  -- only show players picking up keys
      og.emit_notification(message)
      og.emit_sound(C.SOUND_MONEY)
    end
  end
  return true
end

og.register_hooks("treasure", "core:gold_bar", {
  on_eat = gold_bar_on_eat,
})

og.register_hooks("treasure", "core:silver_bar", {
  on_eat = silver_bar_on_eat,
})

og.register_hooks("treasure", "core:life_gem", {
  on_eat = life_gem_on_eat,
})

og.register_hooks("treasure", "core:key", {
  on_eat = key_on_eat,
})
