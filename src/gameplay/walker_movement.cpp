/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <cmath>
#include <cstdint>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

short walker::move(short x, short y)
{
    return setxy(static_cast<short>(xpos() + x), static_cast<short>(ypos() + y));
}

void walker::worldmove(float x, float y)
{
    return setworldxy(worldx() + x, worldy() + y);
}

bool walker::setxy(short x, short y)
{
    set_worldx(static_cast<float>(x));
    set_worldy(static_cast<float>(y));

    // rule: Only tangible walkers are in the collision map: repositioning a normal walker
    //   re-buckets it at the new spot; repositioning an ignored (blood, walk-over), dormant
    //   (delayed spawn) or hidden (burrowed) walker removes it instead, so it blocks nothing and is
    //   found by nothing until it becomes tangible again (set_dormant(false)/set_hidden(false)
    //   re-register).
    // tests: WalkerMovement.walker_setxy_moves_obmap_registration
    // tests: WalkerMovementUnit.walker_movement_r11_move_worldmove_setxy_and_setworldxy
    // tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
    // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
    // tests: SemWalker4.setxy_without_a_world_map_only_repositions
    obmap* map = (current_game && current_game->world)
        ? current_game->world->myobmap.get()
        : nullptr;
    if (map != nullptr)
    {
        // Dormant (delayed-spawn) walkers hold the "never in the obmap"
        // invariant: repositioning one must not re-register it. Waking
        // (walker::set_dormant(false)) is the only re-entry point.
        // rule: Only tangible walkers are in the collision map.
        // tests: WalkerMovement.walker_setxy_moves_obmap_registration
        // tests: WalkerMovementUnit.walker_movement_r11_move_worldmove_setxy_and_setworldxy
        // tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: SemWalker4.setxy_without_a_world_map_only_repositions
        if (!ignore() && !dormant() && !hidden())
            map->move(this, x, y);
        else // just remove us, in case :)
            map->remove(this);
    }

    set_xpos(x);
    set_ypos(y);
    return true;
}

void walker::setworldxy(float x, float y)
{
    set_worldx(x);
    set_worldy(y);

    // rule: Same tangibility rule as above for sub-pixel moves: the collision map
    //   uses the truncated pixel position.
    // tests: WalkerMovementUnit.walker_movement_r11_move_worldmove_setxy_and_setworldxy
    // tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
    // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
    // tests: SemWalker4.setworldxy_rebuckets_a_tangible_walker_at_the_truncated_pixel
    obmap* map = (current_game && current_game->world)
        ? current_game->world->myobmap.get()
        : nullptr;
    if (map != nullptr)
    {
        // Same dormancy rule as setxy: never re-register a delayed spawn.
        // rule: Same tangibility rule as above for sub-pixel moves: the collision
        //   map uses the truncated pixel position.
        // tests: WalkerMovementUnit.walker_movement_r11_move_worldmove_setxy_and_setworldxy
        // tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: SemWalker4.setworldxy_rebuckets_a_tangible_walker_at_the_truncated_pixel
        if (!ignore() && !dormant() && !hidden())
            map->move(this, static_cast<short>(x), static_cast<short>(y));
        else // just remove us, in case :)
            map->remove(this);
    }

    set_xpos(static_cast<short>(x));
    set_ypos(static_cast<short>(y));
}

// WALK -- This function allows us to change facing when we walk.
// This includes an automatic frame change. It also redraws the background
// at the coords it used to occupy.
// It calls the lower level function MOVE.
bool walker::walk()
{
    return walker::walk(lastx(), lasty());
}

short walker::facing(short x, short y)
{
    std::int32_t bigy = y*1000;
    std::int32_t slope;

    // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
    //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope 1000*y/x;
    //   a pure vertical vector faces down if y>0 else up, so (0,0) faces UP.
    // tests: WalkerMovement.walker_facing_all_16_vectors
    // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
    // tests: WalkerMovementUnit.walker_movement_facing_thresholds
    if (!x)
    {
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (y>0)
            return FACE_DOWN;
        else
            return FACE_UP;
    }

    slope = bigy / x;

    // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
    //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope 1000*y/x.
    // tests: WalkerMovement.walker_facing_all_16_vectors
    // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
    // tests: WalkerMovementUnit.walker_movement_facing_thresholds
    if (x>0)
    {
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > 2414)
            return FACE_DOWN;
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > 414)
            return FACE_DOWN_RIGHT;
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > -414)
            return FACE_RIGHT;
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > -2414)
            return FACE_UP_RIGHT;
        return FACE_UP;
    }
    else
    {
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > 2414)
            return FACE_UP;
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > 414)
            return FACE_UP_LEFT;
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > -414)
            return FACE_LEFT;
        // rule: A movement or aim vector maps to one of eight facings in 45-degree sectors whose
        //   borders sit at 22.5 degrees (slope 0.414) and 67.5 degrees (2.414), integer slope
        //   1000*y/x.
        // tests: WalkerMovement.walker_facing_all_16_vectors
        // tests: WalkerMovement.walker_facing_threshold_boundaries_round6
        // tests: WalkerMovementUnit.walker_movement_facing_thresholds
        if (slope > -2414)
            return FACE_DOWN_LEFT;
        return FACE_DOWN;
    }
}

short walker::shove(walker  *target, short x, short y)
{

    // this code has been moved to living, we should only shove livings

    // rule: Only livings can be shoved: shoving anything else refuses (-1) and logs the misuse.
    // tests: WalkerSpecials.no_magic
    // tests: WalkerMovementUnit.walker_movement_r12_walkstep_npc_and_user_slide_paths
    if (x || y || target)
        Log("Shoving a non-living. ORDER: {} FAMILY: {}\n",
            static_cast<int>(order()), static_cast<int>(family()));
    return -1;

}

bool walker::walkstep(float x, float y)
{
    short returnvalue;
    short ret1 = 0, ret2 = 0;
    short oldcurdir = curdir();
    float step = stepsize();
    float halfstep;
    std::int32_t i;
    //walker *control1 = myscreen->viewob[0]->control;
    //walker *control2;
    short mycycle;

    // Repeat last walk.
    set_lastx(x * stepsize());
    set_lasty(y * stepsize());

    // A stationary living is walker::stationary(), the one test that walk() and turn() below
    // and the kept turn of Gameplay Fixes BE also ask. The old game wrote it here as "order
    // LIVING and family TOWER1"; the family's descriptor (is_stationary) says it now, and only a
    // living is asked because a weapon can carry a living's family number (the bone wall has the
    // tower's). A tower's stepsize is 0, so the heading just written is 0,0, which faces UP: the
    // arm below writes the unscaled step instead, the heading that the tower's line check and
    // shot are aimed along (set_weapon_heading).
    // rule: A stationary living (tower) never moves: a step only turns it to face the step
    //   direction (curdir and enddir), records the unscaled step as its heading, and reports
    //   success.
    // tests: WalkerMovement.stationary_family_walk_and_turn_branches
    // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
    if (stationary())
    {
        set_curdir(static_cast<signed char>(facing(x, y)));
        set_enddir(static_cast<char>(curdir()));
        set_lastx(x);
        set_lasty(y);
        return 1;
    }
    returnvalue = walk(x * stepsize(), y * stepsize());
    halfstep = 1;

    // rule: A blocked step is retried as a one-unit baby step (x,y as given, e.g. 1px) before any
    //   fallback; only if both fail does the walker try its NPC sidestep or player wall-slide.
    // tests: WalkerMovement.walker_walkstep_cardinals
    // tests: WalkerMovement.user_blocked_step_tries_full_then_baby_step_before_sliding
    // tests: WalkerMovement.round6_scripted_walkstep_switch_coverage
    // tests: SemWalker4.a_blocked_full_step_is_retried_as_a_baby_step
    if (!returnvalue) // couldn't walk this direction ..
    {
        returnvalue = walk(x*halfstep, y*halfstep); // Now try a baby step
        // rule: A blocked step is retried as a one-unit baby step (x,y as given, e.g. 1px) before
        //   any fallback.
        // tests: WalkerMovement.walker_walkstep_cardinals
        // tests: WalkerMovement.user_blocked_step_tries_full_then_baby_step_before_sliding
        // tests: WalkerMovement.round6_scripted_walkstep_switch_coverage
        // tests: SemWalker4.a_blocked_full_step_is_retried_as_a_baby_step
        if (!returnvalue) // if we still fail
        {
            // rule: A blocked NPC sidesteps: on a cardinal it turns 90 degrees (UP->LEFT,
            //   RIGHT->UP, DOWN->RIGHT, LEFT->DOWN) and walks one full stepsize that way; on a
            //   diagonal it walks the vertical component then the horizontal one separately; it
            //   reports success if any component moved and keeps its original facing.
            // tests: WalkerMovement.npc_blocked_step_fallback_arms_and_the_user_slide
            // tests: WalkerMovement.round6_scripted_walkstep_switch_coverage
            // tests: WalkerMovementUnit.walker_movement_r14_npc_fallback_denials_and_one_success
            // tests: SemWalker4.an_npc_with_an_invalid_facing_makes_no_fallback_move
            if (user() == -1) // means we are an npc
            {
                // rule: A blocked NPC sidesteps: on a cardinal it turns 90 degrees (UP->LEFT,
                //   RIGHT->UP, DOWN->RIGHT, LEFT->DOWN) and walks one full stepsize that way.
                // tests: WalkerMovement.npc_blocked_step_fallback_arms_and_the_user_slide
                // tests: WalkerMovement.round6_scripted_walkstep_switch_coverage
                // tests: WalkerMovementUnit.walker_movement_r14_npc_fallback_denials_and_one_success
                // tests: SemWalker4.an_npc_with_an_invalid_facing_makes_no_fallback_move
                switch (facing(x, y))
                {
                    case FACE_UP:    // For cardinal directions, fail if
                        set_curdir(static_cast<signed char>(FACE_LEFT));
                        ret1 = walk(-step, 0);
                        break;
                    case FACE_RIGHT: // we can't walk this direction
                        set_curdir(static_cast<signed char>(FACE_UP));
                        ret1 = walk(0, -step);
                        break;
                    case FACE_DOWN:
                        set_curdir(static_cast<signed char>(FACE_RIGHT));
                        ret1 = walk(step, 0);
                        break;
                    case FACE_LEFT:
                        set_curdir(static_cast<signed char>(FACE_DOWN));
                        ret1 = walk(0, step);
                        break;
                        //return returnvalue;
                    case FACE_UP_RIGHT:
                        set_curdir(static_cast<signed char>(FACE_UP));
                        ret1 = walk(0, y*step);
                        set_curdir(static_cast<signed char>(FACE_RIGHT));
                        ret2 = walk(x*step, 0);
                        break;
                    case FACE_DOWN_RIGHT:
                        set_curdir(static_cast<signed char>(FACE_DOWN));
                        ret1 = walk(0, y*step);
                        set_curdir(static_cast<signed char>(FACE_RIGHT));
                        ret2 = walk(x*step, 0);
                        break;
                    case FACE_DOWN_LEFT:
                        set_curdir(static_cast<signed char>(FACE_DOWN));
                        ret1 = walk(0, y*step);
                        set_curdir(static_cast<signed char>(FACE_LEFT));
                        ret2 = walk(x*step, 0);
                        break;
                    case FACE_UP_LEFT:
                        set_curdir(static_cast<signed char>(FACE_UP));
                        ret1 = walk(0, y*step);
                        set_curdir(static_cast<signed char>(FACE_LEFT));
                        ret2 = walk(x*step, 0);
                        break;
                    default:
                        ret1 = 0;
                        ret2 = 0;
                        break;
                }
            } // end of npc switch
            else // we're a user // rule: A blocked player slides along walls only on diagonals: for
                //   each started unit of stepsize (the old game's float compare: ceil) it tries one
                //   pixel along the vertical axis and one along the horizontal axis independently,
                //   keeping whichever moves are free; a blocked cardinal does not slide. The slide
                //   always reports failure.
                // tests: WalkerMovement.walker_walkstep_user_slide_cardinal_break_path_round5, WalkerMovement.walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs
                // tests: WalkerMovement.walker_walkstep_user_slide_diagonal_switch_cases_round6
                // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default, SemWalker4.a_wall_slide_moves_one_pixel_per_started_unit_of_stepsize
            {
                // We can't move where we want to.  Can we slide against the wall?
                
                // Store our cycle
                mycycle = cycle();
                short myfacing = facing(x, y);
                bool gotup = false, gotover = false;
                short dx = 0, dy = 0;
                
                // rule: A blocked player slides along walls only on diagonals.
                // tests: WalkerMovement.walker_walkstep_user_slide_cardinal_break_path_round5
                // tests: WalkerMovement.walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs
                // tests: WalkerMovement.walker_walkstep_user_slide_diagonal_switch_cases_round6
                // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default
                switch (myfacing)
                {
                    case FACE_UP:    // For cardinal directions, fail if
                    case FACE_RIGHT: // we can't walk this direction
                    case FACE_DOWN:
                    case FACE_LEFT:
                        break;
                    case FACE_UP_RIGHT:
                        dx = 1;
                        dy = -1;
                        break;
                    case FACE_DOWN_RIGHT:
                        dx = 1;
                        dy = 1;
                        break;
                    case FACE_DOWN_LEFT:
                        dx = -1;
                        dy = 1;
                        break;
                    case FACE_UP_LEFT:
                        dx = -1;
                        dy = -1;
                        break;
                    default:
                        ret1 = 0;
                        ret2 = 0;
                        break;
                }
                
                // rule: A blocked player slides along walls only on diagonals.
                // tests: WalkerMovement.walker_walkstep_user_slide_cardinal_break_path_round5
                // tests: WalkerMovement.walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs
                // tests: WalkerMovement.walker_walkstep_user_slide_diagonal_switch_cases_round6
                // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default
                if(dx != 0 || dy != 0)
                {
                    // The old game compares the int counter with the float stepsize: one pixel per started unit.
                    // rule: A blocked player slides along walls only on diagonals, one pixel per
                    //   started unit of stepsize (ceil; none when the stepsize is 0 or below).
                    // tests: WalkerMovement.walker_walkstep_user_slide_cardinal_break_path_round5
                    // tests: WalkerMovement.walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs, WalkerMovement.walker_walkstep_user_slide_diagonal_switch_cases_round6
                    // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default, SemWalker4.a_wall_slide_moves_one_pixel_per_started_unit_of_stepsize
                    for (i = 0; static_cast<float>(i) < step; i++)
                    {
                        // rule: A blocked player slides along walls only on diagonals.
                        // tests: WalkerMovement.walker_walkstep_user_slide_cardinal_break_path_round5
                        // tests: WalkerMovement.walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs
                        // tests: WalkerMovement.walker_walkstep_user_slide_diagonal_switch_cases_round6
                        // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default
                        if (stats()->slide_half_clear(0, dy))
                        {
                            worldmove(0, dy);  // walk without turning ..
                            gotup = true;
                        }
                        // rule: A blocked player slides along walls only on diagonals.
                        // tests: WalkerMovement.walker_walkstep_user_slide_cardinal_break_path_round5
                        // tests: WalkerMovement.walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs
                        // tests: WalkerMovement.walker_walkstep_user_slide_diagonal_switch_cases_round6
                        // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default
                        if (stats()->slide_half_clear(dx, 0))
                        {
                            worldmove(dx, 0);
                            gotover = true;
                        }
                        // rule: While sliding, the player's walk frame advances one cycle per pixel
                        //   step and is drawn facing the axis actually moved (right/left or
                        //   up/down) when only one axis moved; facing itself is restored to the
                        //   pre-step direction afterwards.
                        // tests: SemWalker4.a_player_slide_draws_the_moved_axis_one_cycle_on_classic
                        if (!gotup && gotover)  // moved horizontally
                        {
                            // rule: While sliding, the player's walk frame advances one cycle per
                            //   pixel step and is drawn facing the axis actually moved (right/left
                            //   or up/down) when only one axis moved.
                            // tests: SemWalker4.a_player_slide_draws_the_moved_axis_one_cycle_on_classic
                            if(dx > 0)
                                set_curdir(static_cast<signed char>(FACE_RIGHT));
                            else
                                set_curdir(static_cast<signed char>(FACE_LEFT));
                        }
                        else if (gotup && !gotover) // moved vertically
                        {
                            // rule: While sliding, the player's walk frame advances one cycle per
                            //   pixel step and is drawn facing the axis actually moved (right/left
                            //   or up/down) when only one axis moved.
                            // tests: SemWalker4.a_player_slide_draws_the_moved_axis_one_cycle_on_classic
                            if(dy < 0)
                                set_curdir(static_cast<signed char>(FACE_UP));
                            else
                                set_curdir(static_cast<signed char>(FACE_DOWN));
                        }
                        if (gotup || gotover) // we moved somewhere?
	                        {
	                            set_cycle(static_cast<signed char>(mycycle));
	                            set_cycle(static_cast<signed char>(cycle() + 1));
	                            set_frame_from_current_walk_animation();
	                        }  // end of cycled us a frame
                    }
                }
            }

            set_curdir(static_cast<signed char>(oldcurdir));
            return ( ret1 || ret2 );
        }
    }
    return returnvalue;
}

bool walker::walk(float x, float y)
{
    short dir;

    dir = facing(x, y);

    // living::walk overrides this walk, so a living reaches it only through walk() above, which
    // names this one (the repeat of its last step: a seated hero's bonus rounds), and walkstep()
    // answers a tower before it would call either. The old game wrote this test as "order
    // LIVING and family TOWER1" too; walker::stationary() asks the family's descriptor, and only
    // for a living. A tower's heading is the unscaled step that its last walkstep() or snap-face
    // wrote (its stepsize is 0), so the repeated step turns it to face where it aims and moves
    // nothing, as the old game's tower did.
    // rule: A stationary living's walk only sets its facing (same rule as above)
    //   and succeeds.
    // tests: SemWalker4.a_stationary_living_walk_only_sets_its_facing
    if (stationary())
    {
        set_curdir(static_cast<signed char>(dir));
        return 1;
    }

    // rule: A zero step succeeds without moving or turning.
    // tests: WalkerMovement.round9_user_cardinal_slide_break_and_offmap_guards
    // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
    if (x == 0.0f && y == 0.0f)
    {
        //Log("walker %d:%d walking 0,0\n",order, family);
        //this happens sometimes, and shouldn't, but it is non-fatal
        return 1;
    }
    // rule: A non-living walker (weapon/effect) moving in a new direction first turns to it (curdir
    //   snaps, cycle 0, frame reset) without moving that call and reports success; only a step in
    //   the direction it already faces moves it.
    // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
    // tests: WalkerMovementUnit.walker_movement_walk_and_walkstep_edge_paths
    if (curdir() == dir)  // if continue direction
    {
        // check if off map
        // rule: Nothing walks off the map: a step whose top-left would leave the grid fails without
        //   moving.
        // tests: WalkerMovementUnit.walker_movement_walk_and_walkstep_edge_paths
        if (x + xpos() < 0 ||
                x + xpos() >= current_game->world->grid.w * GRID_SIZE ||
                y + ypos() < 0 ||
                y + ypos() >= current_game->world->grid.h * GRID_SIZE)
        {
            return 0;
        }

        // Here we check if the move is valid
        // Normally we would check if the object at this grid point
        //    is passable (I cheated for now)
        // rule: A passable step moves the walker (sub-pixel kept) and advances its walk frame; a
        //   blocked step does not move, and advances the frame only for walkers flagged to animate
        //   regardless.
        // tests: WalkerMovementUnit.walker_movement_walk_and_walkstep_edge_paths
        // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
        // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
        if (current_game->world->query_passable(xpos() + x, ypos() + y, this))
        {
            // Control object does complete redraw anyway
            worldmove(x,y);
            set_cycle(static_cast<signed char>(cycle() + 1));
            //if (!ani || (curdir*cycle > sizeof(ani)) )
            //  Log("WALKER::WALK: Bad ani!\n");
            set_frame_from_current_walk_animation();
            return 1;
        }
        else //Invalid move?
        {
            //we're not alive

            // rule: A passable step moves the walker (sub-pixel kept) and advances its walk frame.
            // tests: WalkerMovementUnit.walker_movement_walk_and_walkstep_edge_paths
            // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
            // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
            if (stats_->query_bit_flags(BIT_ANIMATE) )  // animate regardless..
            {
                set_cycle(static_cast<signed char>(cycle() + 1));
                set_frame_from_current_walk_animation();
            }
            return 0;
        }
    }
    else  // changed direction
    {
        set_curdir(static_cast<signed char>(dir));
        set_cycle(0);
        set_frame_from_current_walk_animation();
        worldmove(0,0);
    }
    return 1;
}

float walker::get_current_angle()
{
    // rule: A walker's facing maps to a screen angle (UP -pi/2, clockwise in 45-degree steps, RIGHT
    //   0); an invalid facing reads as 0. Note UP_LEFT is 5pi/4, not -3pi/4.
    // tests: WalkerMovement.walker_get_current_angle_all_direction_cases
    // tests: WalkerMovement.round9_blocked_animate_angle_and_turn_default_paths
    // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
    switch (curdir())
    {
        case FACE_UP:
            return -static_cast<float>(M_PI_2);
        case FACE_UP_RIGHT:
            return -static_cast<float>(M_PI_4);
        case FACE_RIGHT:
            return 0.0f;
        case FACE_DOWN_RIGHT:
            return static_cast<float>(M_PI_4);
        case FACE_DOWN:
            return static_cast<float>(M_PI_2);
        case FACE_DOWN_LEFT:
            return static_cast<float>(3 * M_PI_4);
        case FACE_LEFT:
            return static_cast<float>(M_PI);
        case FACE_UP_LEFT:
            return static_cast<float>(5 * M_PI_4);
        default:
            return 0.0f;
    }
}

bool walker::turn(short targetdir)
{
    short distance;
    short currentdir = static_cast<short>(curdir());

    // rule: Gameplay Fixes AT (#446): with the setting on, a walker asked to face the way it
    //   already faces does nothing (its facing, heading and walk frame stay as they are) and the
    //   turn reports done; with it off the classic step below turns it one notch counter-clockwise.
    // tests: SemWalker4.a_walker_asked_to_face_the_way_it_faces_stays_put_with_the_setting_on
    // tests: SemWalker4.classic_a_walker_asked_to_face_the_way_it_faces_turns_a_notch_counter_clockwise
    // tests: parity:orc_bot_faces_foe_scen99, parity:orc_bot_faces_foe_fixes_scen99, parity:mage_bot_far_foe_scen99
    if (og::sim::session_gameplay_fixes() != 0 && currentdir == targetdir)
        return true;

    //   We use a clock-ordered
    //   of directions to numbers) to a clock-ordered
    //   mapping of directions so we can calculate what
    //   our next facing should be based on our current one.

    // Find how  we have to turn.
    distance = static_cast<short>(currentdir - targetdir);

    // Figure out if we should turn clockwise or counterclockwise
    // rule: Turning rotates one 45-degree step per call the short way round; a half-turn (4 steps
    //   either way) goes clockwise (+1); asked for the facing it already has, it steps one notch
    //   counter-clockwise (+7; the classic game, as Gameplay Fixes AT returns above).
    // tests: WalkerMovement.walker_turn_to_all_targets, WalkerMovement.walker_turn_from_all_starts
    // tests: SemWalker4.classic_a_walker_asked_to_face_the_way_it_faces_turns_a_notch_counter_clockwise
    if ( ( (distance >= -4) && (distance < 0) ) || (distance >= 4) )
        currentdir = static_cast<short>((currentdir + 1) % 8);
    else
        currentdir = static_cast<short>((currentdir + 7) % 8);

    set_curdir(static_cast<signed char>(currentdir));

    // Now set our lastx and lasty (facing) variables correctly
    // A stationary living (walker::stationary(): a tower) keeps the heading its last step or
    // snap-face gave it: its stepsize is 0, so the arms below would write 0,0, which faces UP,
    // and its line check and its shot are aimed along that heading. With Gameplay Fixes BE the
    // kept turn snap-faces a tower (face_delta) instead of turning it here.
    // rule: After a turn a mobile walker's heading (lastx/lasty, used to repeat walks and aim
    //   weapons) is one stepsize along the new facing; a stationary living keeps its old heading;
    //   an invalid facing heads up.
    // tests: WalkerMovementUnit.walker_movement_turn_stationary_and_normal
    // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
    // tests: WalkerMovement.round9_blocked_animate_angle_and_turn_default_paths
    // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default
    // tests: SemWalker4.a_turn_heads_a_mobile_walker_one_stepsize_along_each_facing
    if (!stationary())
    {
        // rule: After a turn a mobile walker's heading (lastx/lasty, used to repeat walks and aim
        //   weapons) is one stepsize along the new facing.
        // tests: WalkerMovementUnit.walker_movement_turn_stationary_and_normal
        // tests: WalkerMovementUnit.walker_movement_r11_walk_turn_and_angles
        // tests: WalkerMovement.round9_blocked_animate_angle_and_turn_default_paths
        // tests: WalkerMovementUnit.walker_movement_r14_user_slide_axes_and_turn_default
        // tests: SemWalker4.a_turn_heads_a_mobile_walker_one_stepsize_along_each_facing
        switch (curdir())
        {
            case FACE_UP:
                set_lastx(0.0f);
                set_lasty(-stepsize());
                break;
            case FACE_UP_RIGHT:
                set_lastx(stepsize());
                set_lasty(-stepsize());
                break;
            case FACE_RIGHT:
                set_lastx(stepsize());
                set_lasty(0.0f);
                break;
            case FACE_DOWN_RIGHT:
                set_lastx(stepsize());
                set_lasty(stepsize());
                break;
            case FACE_DOWN:
                set_lastx(0.0f);
                set_lasty(stepsize());
                break;
            case FACE_DOWN_LEFT:
                set_lastx(-stepsize());
                set_lasty(stepsize());
                break;
            case FACE_LEFT:
                set_lastx(-stepsize());
                set_lasty(0.0f);
                break;
            case FACE_UP_LEFT:
                set_lastx(-stepsize());
                set_lasty(-stepsize());
                break;
            default :
                set_lastx(0.0f);
                set_lasty(-stepsize());
        }
    }
    set_cycle(0);
    set_frame_from_current_walk_animation();
    worldmove(0,0);
    return true;
}

// Guard-standoff melee deadlock fix (2026-07-07, see
// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): snap-face the direction of a
// (foe) delta in one act. Unlike turn() this doesn't rotate one 45-degree
// step per tick — the caller has already decided the walker must point at
// its adjacent foe NOW. Sets all three facing channels a swing depends on:
//   - curdir: the fire_check() facing gate compares against it,
//   - enddir: otherwise living::act()'s pre-command turn would rotate us
//     right back off the foe next tick,
//   - lastx/lasty: set_weapon_heading() derives the weapon spawn cell and
//     flight vector from these, not from curdir.
// Consumes no RNG and never moves the walker.
void walker::face_delta(short xdelta, short ydelta)
{
    const short dir = facing(xdelta, ydelta);
    set_curdir(static_cast<signed char>(dir));
    set_enddir(static_cast<char>(dir));
    // #350: a stationary (stepsize 0) shooter keeps a usable heading toward
    // the foe after a snap-face: magnitude 1 instead of 0 (which read FACE_UP).
    // rule: Snap-facing a foe (guard-standoff fix) points curdir and enddir at it and sets the
    //   firing heading to the foe delta scaled by stepsize; a stationary shooter (stepsize 0) gets
    //   the unscaled delta so its heading still points at the foe instead of reading as UP (#350).
    // tests: WalkerCoreMore.face_delta_stepsize_zero_keeps_a_unit_heading_toward_the_foe
    // tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
    // tests: parity:tower_snap_face_heading_scen99
    const float scale = (stepsize() == 0.0f) ? 1.0f : stepsize();
    set_lastx(static_cast<float>(xdelta) * scale);
    set_lasty(static_cast<float>(ydelta) * scale);
}

// One step of a weapon in flight (walker.h): what stops it and what it strikes
// are written here once. Appended at the end so the parity pins above never shift.
walker::FlightStep walker::flight_step()
{
    const bool walked = walk();
    // rule: A projectile that cannot take its step (it met a body or a wall), or a NO_COLLIDE
    //   projectile on every step, has struck the live body that step met; a wall, the map's edge
    //   or a corpse is no strike. A step that was taken and struck nothing flew.
    // tests: WalkerMore.walker_act_fire_collision_arm_attacks_the_blocking_body
    // tests: SemWalker2.a_shot_stopped_by_a_wall_or_a_corpse_strikes_nothing
    // tests: SemLineCheck.one_flight_step_tells_a_strike_from_a_stop_and_a_free_step
    if ((!walked || stats_->query_bit_flags(BIT_NO_COLLIDE)) && collide_ob() != nullptr &&
        !collide_ob()->dead())
        return FlightStep::Struck;
    return walked ? FlightStep::Flew : FlightStep::Stopped;
}

// Gameplay Fixes BD (#538): fire_check's ray, flown the way the shot flies (walker.h); the classic
// loop's spots, 0, 1, 3, 6 ... steps out, step over bodies and one-tile walls. Gameplay Fixes BF:
// an enemy door on the shot's path refuses the check, as a wall does, unless it is the shooter's
// foe. The test weapon meets that door without the clash roll. Appended below the parity pins.
std::optional<bool> walker::line_check_flight(walker* weapon, walker* ignored_foe,
                                              FireCheckDenial* denial)
{
    // rule: Gameplay Fixes BD (#538): classic (the setting off) nothing is asked here and
    //   fire_check's own loop tests 0, 1, 3, 6 ... steps out.
    // tests: SemLineCheck.classic_the_check_steps_over_a_foe_between_two_of_its_spots
    // tests: SemLineCheck.a_tower_shoots_a_still_foe_between_two_classic_spots_only_with_the_setting_on, parity:tower_foe_between_spots_scen99, parity:tower_foe_between_spots_fixes_scen99
    if (og::sim::session_gameplay_fixes() == 0)
        return std::nullopt;
    GameWorld& world = *current_game->world;
    // From the pad on the check takes an arrow slit as open and draws nothing for it: the shot
    // itself takes the slit's gamble (GameWorld::slit_open_for, read where the gamble is rolled).
    // The same mark has ob_pass_check meet a live enemy door without the clash roll (fix BF).
    world.slit_open_for = weapon;
    const auto answer = [&](bool lands, FireCheckDenial why) {
        world.slit_open_for = nullptr;
        weapon->set_dead(1);
        // plumbing: the denial is written only for a caller that asked for it
        if (denial != nullptr)
            *denial = why;
        return std::optional<bool>(lands);
    };
    const auto on_the_ball = [&] {
        return ignored_foe != nullptr &&
               ::collide(weapon->xpos(), weapon->ypos(), weapon->sizex(), weapon->sizey(),
                         ignored_foe->xpos(), ignored_foe->ypos(), ignored_foe->sizex(),
                         ignored_foe->sizey());
    };
    // Gameplay Fixes BF: what stopped the test weapon (its collide_ob, which the door arm of
    // ob_pass_check records) is a live enemy door that is not the shooter's own foe. A door of
    // the shooter's side never stops it: ob_pass_check lets a weapon pass its own side.
    const auto stopped_by_a_door = [&] {
        const walker* met = weapon->collide_ob();
        return met != nullptr && !met->dead() && met->query_order() == Order::Weapon &&
               met->family() == FAMILY_DOOR && met != foe();
    };
    // rule: Gameplay Fixes BD (#538): the pad first, as fire() asks it: terrain there refuses
    //   (the shot would be wasted), the mode ball there passes, and so does a live body that is
    //   not a door (the blow lands; a door is Gameplay Fixes BF's, below).
    // tests: SemLineCheck.the_pad_passes_on_a_body_and_refuses_on_a_wall_on_both_settings
    // tests: SemLineCheck.a_mode_ball_on_the_line_passes_the_flown_check
    // tests: SemLineCheck.a_door_on_the_pad_refuses_the_check_with_the_setting_on
    if (!world.query_grid_passable(weapon->xpos(), weapon->ypos(), weapon))
        return answer(false, FireCheckDenial::WallBlocked);
    if (on_the_ball())
        return answer(true, FireCheckDenial::None);
    weapon->set_collide_ob(nullptr);  // as fire_check did: the pad reads this question's answer
    if (!world.query_object_passable(weapon->xpos(), weapon->ypos(), weapon))
    {
        // rule: Gameplay Fixes BF: a pad held by a live enemy door that is not the shooter's
        //   foe refuses (WallBlocked: to a shot a shut door is a wall); a door that is the foe,
        //   and every other body, passes.
        // tests: SemLineCheck.a_door_on_the_pad_refuses_the_check_with_the_setting_on
        // tests: SemLineCheck.a_door_that_is_the_shooters_foe_passes_the_check
        if (stopped_by_a_door())
            return answer(false, FireCheckDenial::WallBlocked);
        return answer(true, FireCheckDenial::None);
    }
    // fire() gives a fresh shot a facing its first act only turns to, so the shot moves
    // lineofsight - 1 steps; the probe is pointed along its heading so walk() steps at once.
    weapon->set_curdir(static_cast<signed char>(weapon->facing(weapon->lastx(), weapon->lasty())));
    // rule: Gameplay Fixes BD (#538): then the steps a fresh shot moves, each the shot's own
    //   (flight_step): a step that strikes a live body that is not a door passes, a step stopped
    //   by terrain or the map's edge refuses (WallBlocked), and so does a step struck by an enemy
    //   door that is not the foe (Gameplay Fixes BF), a taken step onto the mode ball passes, and
    //   when the steps run out the check refuses (RayMiss). The bound is the steps a fresh shot
    //   moves, no more and no fewer (the check's side: the_check_flies_as_far_as_a_fresh_shot...;
    //   the shot's side, that it moves lineofsight - 1 steps: a_fresh_shot_moves_one_step_fewer...).
    // tests: SemLineCheck.the_check_passes_exactly_where_the_shot_lands_with_the_setting_on
    // tests: SemLineCheck.the_check_flies_as_far_as_a_fresh_shot_and_no_further_with_the_setting_on
    // tests: SemLineCheck.a_wall_the_shot_stops_on_refuses_with_the_setting_on
    // tests: SemLineCheck.the_map_edge_refuses_only_when_the_shot_would_leave_the_map
    // tests: SemLineCheck.an_enemy_weapon_on_the_line_is_met_on_its_clash_roll_with_the_setting_on
    // tests: SemLineCheck.the_check_draws_its_waver_and_nothing_else_in_open_ground
    // tests: SemLineCheck.a_fresh_shot_moves_one_step_fewer_than_its_line_of_sight
    // tests: SemLineCheck.a_door_on_the_shots_path_refuses_the_check_with_the_setting_on
    // tests: SemLineCheck.a_door_behind_the_foe_changes_nothing
    for (std::int32_t step = 1; step < weapon->lineofsight(); ++step)
    {
        weapon->set_collide_ob(nullptr);  // weap::act clears it before every act
        const FlightStep met = weapon->flight_step();
        // rule: Gameplay Fixes BD (#538): a flight step that strikes a live body that is not a
        //   door passes (the shot lands, also on an enemy weapon in flight or a grown tree that
        //   won its clash roll against the probe), a step stopped by terrain or the map's edge
        //   refuses (WallBlocked), and a taken step onto the mode ball passes.
        // tests: SemLineCheck.the_check_passes_exactly_where_the_shot_lands_with_the_setting_on
        // tests: SemLineCheck.a_wall_the_shot_stops_on_refuses_with_the_setting_on
        // tests: SemLineCheck.the_map_edge_refuses_only_when_the_shot_would_leave_the_map
        // tests: SemLineCheck.an_enemy_weapon_on_the_line_is_met_on_its_clash_roll_with_the_setting_on
        // tests: SemLineCheck.a_mode_ball_on_the_line_passes_the_flown_check
        // tests: SemLineCheck.a_door_behind_the_foe_changes_nothing
        // tests: SemLineCheck.an_own_team_door_is_flown_through
        if (met == FlightStep::Struck)
        {
            // rule: Gameplay Fixes BF: a step struck by a live enemy door that is not the
            //   shooter's foe refuses (WallBlocked), as a step a wall stops does; a door that is
            //   the foe passes, as every foe does.
            // tests: SemLineCheck.a_door_on_the_shots_path_refuses_the_check_with_the_setting_on
            // tests: SemLineCheck.a_door_that_is_the_shooters_foe_passes_the_check
            // tests: SemLineCheck.a_bot_whose_foe_is_behind_a_door_does_not_shoot_with_the_setting_on
            // tests: SemLineCheck.a_refused_check_at_a_door_tells_no_one_a_key_is_needed
            if (stopped_by_a_door())
                return answer(false, FireCheckDenial::WallBlocked);
            return answer(true, FireCheckDenial::None);
        }
        if (met == FlightStep::Stopped)
            return answer(false, FireCheckDenial::WallBlocked);
        if (on_the_ball())
            return answer(true, FireCheckDenial::None);
    }
    return answer(false, FireCheckDenial::RayMiss);
}
