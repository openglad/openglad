-- core:rock — bouncing rock reflects off whichever axis is open (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- rock_on_death: a bouncing rock (do_bounce set by the elf special) that
-- died against a barrier un-deads itself and reflects off whichever axis
-- is open, instead of dying.
local function on_death(self)
  -- rule: Only an elf's bouncing rock that dies by striking terrain bounces; a non-bouncing rock, a
  --   rock that has run out of range (lineofsight 0) or a rock that hit a walker dies normally.
  -- tests: WeapBehavior.weapon_family_rock_death_bounce_matrix
  -- tests: SemLibsRock.a_bouncing_rock_out_of_range_dies_without_bouncing
  -- tests: SemLibsRock.a_bouncing_rock_that_hit_a_walker_dies_without_bouncing
  if self:do_bounce() == 0 or self:lineofsight() == 0
      or self:collide_ob() ~= nil then  -- died of natural causes
    return false
  end
  self:set_dead(0)  -- first, un-dead us so we can collide ..
  -- Every probe point below sums short xpos/ypos with float lastx/lasty in
  -- C float (og.fadd/og.fsub), and each taken bounce narrows the same sums
  -- back to short through setxy — exactly the C++ call shapes.
  -- Did we hit a barrier?
  -- rule: A bouncing rock that died against a barrier reflects: if its forward cell is actually
  --   open it dies normally; otherwise it tries flipping X, then flipping Y, then flipping both,
  --   takes the first open cell (moving there and reversing that velocity component) and lives on;
  --   if all are blocked it dies.
  -- tests: WeapBehavior.weapon_family_rock_death_bounce_matrix
  -- tests: SemLibsRock.a_rock_bouncing_off_x_moves_to_the_x_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_y_moves_to_the_y_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_both_axes_moves_to_the_corner_cell_and_lives
  if og.query_grid_passable(og.fadd(self:xpos(), self:lastx()),
                            og.fadd(self:ypos(), self:lasty()), self) then
    self:set_dead(1)
    return false  -- if not, die like normal
  end
  -- shim kept (all three bounce probes): xpos/ypos +- lastx/lasty are C++
  -- float sums: per-op float rounding.
  -- rule: Part of the rule above that begins "A bouncing rock that died against a barrier reflects:
  --   ..."
  -- tests: WeapBehavior.weapon_family_rock_death_bounce_matrix
  -- tests: SemLibsRock.a_rock_bouncing_off_x_moves_to_the_x_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_y_moves_to_the_y_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_both_axes_moves_to_the_corner_cell_and_lives
  if og.query_grid_passable(og.fsub(self:xpos(), self:lastx()),
                            og.fadd(self:ypos(), self:lasty()), self) then
    -- bounce 'down-left'
    self:setxy(og.fsub(self:xpos(), self:lastx()),
               og.fadd(self:ypos(), self:lasty()))
    self:set_lastx(-self:lastx())
    self:set_death_called(0)
    return true
  end
  -- rule: Part of the rule above that begins "A bouncing rock that died against a barrier reflects:
  --   ..."
  -- tests: WeapBehavior.weapon_family_rock_death_bounce_matrix
  -- tests: SemLibsRock.a_rock_bouncing_off_x_moves_to_the_x_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_y_moves_to_the_y_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_both_axes_moves_to_the_corner_cell_and_lives
  if og.query_grid_passable(og.fadd(self:xpos(), self:lastx()),
                            og.fsub(self:ypos(), self:lasty()), self) then
    -- bounce 'up-right'
    self:setxy(og.fadd(self:xpos(), self:lastx()),
               og.fsub(self:ypos(), self:lasty()))
    self:set_lasty(-self:lasty())
    self:set_death_called(0)
    return true
  end
  -- rule: Part of the rule above that begins "A bouncing rock that died against a barrier reflects:
  --   ..."
  -- tests: WeapBehavior.weapon_family_rock_death_bounce_matrix
  -- tests: SemLibsRock.a_rock_bouncing_off_x_moves_to_the_x_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_y_moves_to_the_y_flip_cell_and_lives
  -- tests: SemLibsRock.a_rock_bouncing_off_both_axes_moves_to_the_corner_cell_and_lives
  if og.query_grid_passable(og.fsub(self:xpos(), self:lastx()),
                            og.fsub(self:ypos(), self:lasty()), self) then
    -- bounce off both axes
    self:setxy(og.fsub(self:xpos(), self:lastx()),
               og.fsub(self:ypos(), self:lasty()))
    self:set_lastx(-self:lastx())
    self:set_lasty(-self:lasty())
    self:set_death_called(0)
    return true
  end
  -- Else we're really stuck, so die :)
  self:set_dead(1)
  return false
end

-- The declarations that reference this module (packs/core/families/):
--   core:rock  on_death = rock.on_death
return {
  on_death = on_death,
}
