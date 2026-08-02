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
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
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

    obmap* map = (current_game && current_game->world)
        ? current_game->world->myobmap.get()
        : nullptr;
    if (map != nullptr)
    {
        // Dormant (delayed-spawn) walkers hold the "never in the obmap"
        // invariant: repositioning one must not re-register it. Waking
        // (walker::set_dormant(false)) is the only re-entry point.
        if (!ignore() && !dormant())
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

    obmap* map = (current_game && current_game->world)
        ? current_game->world->myobmap.get()
        : nullptr;
    if (map != nullptr)
    {
        // Same dormancy rule as setxy: never re-register a delayed spawn.
        if (!ignore() && !dormant())
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

    if (!x)
    {
        if (y>0)
            return FACE_DOWN;
        else
            return FACE_UP;
    }

    slope = bigy / x;

    if (x>0)
    {
        if (slope > 2414)
            return FACE_DOWN;
        if (slope > 414)
            return FACE_DOWN_RIGHT;
        if (slope > -414)
            return FACE_RIGHT;
        if (slope > -2414)
            return FACE_UP_RIGHT;
        return FACE_UP;
    }
    else
    {
        if (slope > 2414)
            return FACE_UP;
        if (slope > 414)
            return FACE_UP_LEFT;
        if (slope > -414)
            return FACE_LEFT;
        if (slope > -2414)
            return FACE_DOWN_LEFT;
        return FACE_DOWN;
    }
}

short walker::shove(walker  *target, short x, short y)
{

    // this code has been moved to living, we should only shove livings

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

    if (order() == Order::Living)
    {
        const auto* fd = get_family_descriptor(family());
        if (fd && fd->is_stationary)
        {
            set_curdir(static_cast<signed char>(facing(x, y)));
            set_enddir(static_cast<char>(curdir()));
            set_lastx(x);
            set_lasty(y);
            return 1;
        }
    }
    returnvalue = walk(x * stepsize(), y * stepsize());
    halfstep = 1;

    if (!returnvalue) // couldn't walk this direction ..
    {
        returnvalue = walk(x*halfstep, y*halfstep); // Now try a baby step
        if (!returnvalue) // if we still fail
        {
            if (user() == -1) // means we are an npc
            {
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
            else // we're a user
            {
                // We can't move where we want to.  Can we slide against the wall?
                
                // Store our cycle
                mycycle = cycle();
                short myfacing = facing(x, y);
                bool gotup = false, gotover = false;
                short dx = 0, dy = 0;
                
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
                
                if(dx != 0 || dy != 0)
                {
                    const std::int32_t step_i = static_cast<std::int32_t>(step);
                    for (i = 0; i < step_i; i++)
                    {
                        if (current_game->world->query_passable(xpos(), ypos() + dy, this))
                        {
                            worldmove(0, dy);  // walk without turning ..
                            gotup = true;
                        }
                        if (current_game->world->query_passable(xpos() + dx, ypos(), this))
                        {
                            worldmove(dx, 0);
                            gotover = true;
                        }
                        if (!gotup && gotover)  // moved horizontally
                        {
                            if(dx > 0)
                                set_curdir(static_cast<signed char>(FACE_RIGHT));
                            else
                                set_curdir(static_cast<signed char>(FACE_LEFT));
                        }
                        else if (gotup && !gotover) // moved vertically
                        {
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

    if (order() == Order::Living)
    {
        const auto* fd = get_family_descriptor(family());
        if (fd && fd->is_stationary)
        {
            set_curdir(static_cast<signed char>(dir));
            return 1;
        }
    }

    if ( !x && !y)
    {
        //Log("walker %d:%d walking 0,0\n",order, family);
        //this happens sometimes, and shouldn't, but it is non-fatal
        return 1;
    }
    if (curdir() == dir)  // if continue direction
    {
        // check if off map
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

    //   We use a clock-ordered
    //   of directions to numbers) to a clock-ordered
    //   mapping of directions so we can calculate what
    //   our next facing should be based on our current one.

    // Find how  we have to turn.
    distance = static_cast<short>(currentdir - targetdir);

    // Figure out if we should turn clockwise or counterclockwise
    if ( ( (distance >= -4) && (distance < 0) ) || (distance >= 4) )
        currentdir = static_cast<short>((currentdir + 1) % 8);
    else
        currentdir = static_cast<short>((currentdir + 7) % 8);

    set_curdir(static_cast<signed char>(currentdir));

    // Now set our lastx and lasty (facing) variables correctly
    const bool stationary = (order() == Order::Living) && [&]{
        const auto* fd = get_family_descriptor(family());
        return fd && fd->is_stationary;
    }();
    if (!stationary)
    {
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
    set_lastx(static_cast<float>(xdelta) * stepsize());
    set_lasty(static_cast<float>(ydelta) * stepsize());
}
