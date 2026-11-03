-- core lib: corpses — where the cleric's raises stand the body with the Gameplay Fixes setting on, and the stain they raise (cookbook: docs/lua-classpacks-design.md §3).
--
-- The classic raises (living-05-cleric.lua, nearby_corpse with the setting
-- off) take the nearest stain alone and probe the STAIN's own 16 x 16 box
-- with og.query_passable, at its corner, before the range compare: probe,
-- then range, because the probe can eat a drumstick and draw (a stain's probe
-- never rolls the obmap miss: that roll is for two weapons; it draws only
-- through the drumstick its box eats). The stain box is wider than most
-- victims and the caster's own body blocks it, so a corpse against a wall,
-- in a one-tile corridor or under the cleric is refused CORPSE IS BLOCKED.
-- With the setting on (docs/gameplay-fixes.md, T) the body that will rise is
-- probed instead, nudged up to NUDGE px from the corner, over every stain the
-- finder would answer, nearest first.

local M = {}

-- The body never stands further than this from the corpse's corner (px).
M.NUDGE = 16

local LIVING_GHOST = assert(og.family_id("living", "core:ghost"))

-- The family the cast will raise on `blood`: the one given, or for RESURRECT
-- (body nil) the corpse's own family when the stain is friendly and a ghost
-- otherwise, the branch resurrect itself takes.
local function body_for(self, blood, body)
  -- rule: With Gameplay Fixes on, the room a raise needs is measured for the body that will
  --   actually rise: RAISE SKELETON / RAISE GHOST probe their own family; RESURRECT probes the
  --   corpse's old family when the stain is friendly and a ghost when it is a foe's.
  -- tests: GameplayFixesClericRaise.reporter_elf_against_a_wall_is_raised_with_the_setting_on
  -- tests: GameplayFixesClericRaise.a_blocked_near_stain_does_not_hide_a_usable_far_one
  -- tests: SemLibsCorpses.resurrect_measures_a_foe_stain_for_the_ghost_it_will_raise
  if body ~= nil then
    return body
  end
  if self:is_friendly(blood) then
    return blood:s_old_family()
  end
  return LIVING_GHOST
end

-- The stain the cast raises and the spot its body stands on, or nil and the
-- refusal. The stains find_nearest_blood would answer (squared centre
-- distance under 800, the finder's own cap) on the caster's floor, nearest
-- first; the first inside `range` (Manhattan, the cast's compare) with room
-- for the body within NUDGE px wins. Draw-free and eat-free: og.body_spot_near
-- probes a detached body with its own box and flags, and the caster blocks
-- it like any body. The refusal names what the player can act on: a blocked
-- corpse before a far one before none.
function M.raise_spot(self, range, body)
  local stains = og.blood_in_reach(self)
  -- rule: With Gameplay Fixes on, a raise with no stain within the finder's own cap (squared centre
  --   distance < 800 on the caster's floor) refuses NO CORPSE NEARBY and spends nothing.
  -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
  -- tests: GameplayFixesClericRaise.a_blocked_near_stain_does_not_hide_a_usable_far_one
  if #stains == 0 then
    return nil, "NO CORPSE NEARBY"
  end
  local blocked = false
  -- plumbing: iterates stains nearest first; the rule is below
  for i = 1, #stains do
    local blood = stains[i]
    -- rule: With Gameplay Fixes on, a raise takes the NEAREST stain inside the cast's Manhattan
    --   range that has room for the risen body within 16 px of the corpse corner; a blocked near
    --   stain never hides a usable far one; when none qualifies the refusal names a blocked corpse
    --   before a far one (CORPSE IS BLOCKED before CORPSE TOO FAR).
    -- tests: GameplayFixesClericRaise.a_blocked_near_stain_does_not_hide_a_usable_far_one
    -- tests: GameplayFixesClericRaise.reporter_elf_against_a_wall_is_raised_with_the_setting_on
    -- tests: GameplayFixesClericRaise.reporter_one_tile_corridor
    -- tests: GameplayFixesClericRaise.caster_standing_on_the_corpse
    -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
    if self:distance_to_ob(blood) < range then
      local fam = body_for(self, blood, body)
      local sx, sy = og.body_spot_near("living", fam, blood:xpos(), blood:ypos(), blood:floor(), M.NUDGE)
      -- rule: With Gameplay Fixes on, a raise takes the NEAREST stain inside the cast's Manhattan
      --   range that has room for the risen body within 16 px of the corpse corner.
      -- tests: GameplayFixesClericRaise.a_blocked_near_stain_does_not_hide_a_usable_far_one
      -- tests: GameplayFixesClericRaise.reporter_elf_against_a_wall_is_raised_with_the_setting_on
      -- tests: GameplayFixesClericRaise.reporter_one_tile_corridor
      -- tests: GameplayFixesClericRaise.caster_standing_on_the_corpse
      -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
      if sx ~= nil then
        return blood, nil, sx, sy
      end
      blocked = true
    end
  end
  -- rule: With Gameplay Fixes on, a raise takes the NEAREST stain inside the cast's Manhattan range
  --   that has room for the risen body within 16 px of the corpse corner.
  -- tests: GameplayFixesClericRaise.a_blocked_near_stain_does_not_hide_a_usable_far_one
  -- tests: GameplayFixesClericRaise.reporter_elf_against_a_wall_is_raised_with_the_setting_on
  -- tests: GameplayFixesClericRaise.reporter_one_tile_corridor
  -- tests: GameplayFixesClericRaise.caster_standing_on_the_corpse
  -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
  if blocked then
    return nil, "CORPSE IS BLOCKED"
  end
  return nil, "CORPSE TOO FAR"
end

return M
