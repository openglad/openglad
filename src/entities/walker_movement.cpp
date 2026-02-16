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

#include <openglad/core/stats.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>

short walker::move(short x, short y)
{
    return setxy(static_cast<short>(xpos+x), static_cast<short>(ypos+y));
}

void walker::worldmove(float x, float y)
{
    return setworldxy(worldx_+x, worldy_+y);
}

bool walker::setxy(short x, short y)
{
    worldx_ = x;
    worldy_ = y;
    
    if (myobmap != nullptr)
    {
        if (!ignore)
            myobmap->move(this, x, y);
        else // just remove us, in case :)
            myobmap->remove(this);
    }

    xpos = x;
    ypos = y;
    return true;
}

void walker::setworldxy(float x, float y)
{
    worldx_ = x;
    worldy_ = y;

    if (myobmap != nullptr)
    {
        if (!ignore)
            myobmap->move(this, static_cast<short>(x), static_cast<short>(y));
        else // just remove us, in case :)
            myobmap->remove(this);
    }

    xpos = static_cast<short>(x);
    ypos = static_cast<short>(y);
}

// WALK -- This function allows us to change facing when we walk.
// This includes an automatic frame change. It also redraws the background
// at the coords it used to occupy.
// at the coords it used to occupy.
// It calls the lower level function MOVE.
bool walker::walk()
{
    return walker::walk(lastx, lasty);
}

short walker::facing(short x, short y)
{
    Sint32 bigy = y*1000;
    Sint32 slope;

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
        Log("Shoving a non-living. ORDER: {} FAMILY: {}\n", static_cast<int>(order), static_cast<int>(family));
    return -1;

}

bool walker::walkstep(float x, float y)
{
    short returnvalue;
    short ret1 = 0, ret2 = 0;
    short oldcurdir = curdir;
    float step = stepsize;
    float halfstep;
    Sint32 i;
    //walker *control1 = myscreen->viewob[0]->control;
    //walker *control2;
    short mycycle;

    // Repeat last walk.
    lastx = x*stepsize;
    lasty = y*stepsize;

    if (order == Order::Living)
    {
        const auto* fd = get_family_descriptor(family);
        if (fd && fd->is_stationary)
        {
            curdir = static_cast<signed char>(facing(x, y));
            enddir = curdir;
            lastx = x;
            lasty = y;
            return 1;
        }
    }
    returnvalue = walk(x*stepsize, y*stepsize);
    halfstep = 1;

    if (!returnvalue) // couldn't walk this direction ..
    {
        returnvalue = walk(x*halfstep, y*halfstep); // Now try a baby step
        if (!returnvalue) // if we still fail
        {
            if (user == -1) // means we are an npc
            {
                switch (facing(x, y))
                {
                    case FACE_UP:    // For cardinal directions, fail if
                        curdir = FACE_LEFT;
                        ret1 = walk(-step, 0);
                        break;
                    case FACE_RIGHT: // we can't walk this direction
                        curdir = FACE_UP;
                        ret1 = walk(0, -step);
                        break;
                    case FACE_DOWN:
                        curdir = FACE_RIGHT;
                        ret1 = walk(step, 0);
                        break;
                    case FACE_LEFT:
                        curdir = FACE_DOWN;
                        ret1 = walk(0, step);
                        break;
                        //return returnvalue;
                    case FACE_UP_RIGHT:
                        curdir = FACE_UP;
                        ret1 = walk(0, y*step);
                        curdir = FACE_RIGHT;
                        ret2 = walk(x*step, 0);
                        break;
                    case FACE_DOWN_RIGHT:
                        curdir = FACE_DOWN;
                        ret1 = walk(0, y*step);
                        curdir = FACE_RIGHT;
                        ret2 = walk(x*step, 0);
                        break;
                    case FACE_DOWN_LEFT:
                        curdir = FACE_DOWN;
                        ret1 = walk(0, y*step);
                        curdir = FACE_LEFT;
                        ret2 = walk(x*step, 0);
                        break;
                    case FACE_UP_LEFT:
                        curdir = FACE_UP;
                        ret1 = walk(0, y*step);
                        curdir = FACE_LEFT;
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
                mycycle = cycle;
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
                    const Sint32 step_i = static_cast<Sint32>(step);
                    for (i = 0; i < step_i; i++)
                    {
                        if (sim_level->query_passable(xpos, ypos + dy, this))
                        {
                            worldmove(0, dy);  // walk without turning ..
                            gotup = true;
                        }
                        if (sim_level->query_passable(xpos + dx, ypos, this))
                        {
                            worldmove(dx, 0);
                            gotover = true;
                        }
                        if (!gotup && gotover)  // moved horizontally
                        {
                            if(dx > 0)
                                curdir = FACE_RIGHT;
                            else
                                curdir = FACE_LEFT;
                        }
                        else if (gotup && !gotover) // moved vertically
                        {
                            if(dy < 0)
                                curdir = FACE_UP;
                            else
                                curdir = FACE_DOWN;
                        }
                        if (gotup || gotover) // we moved somewhere?
                        {
                            cycle = static_cast<signed char>(mycycle);
                            cycle++;
                            if (ani[curdir][cycle] == -1)
                                cycle = 0;
                            set_frame(ani[curdir][cycle]);
                        }  // end of cycled us a frame
                    }
                }
            }

            curdir = static_cast<char>(oldcurdir);
            return ( ret1 || ret2 );
        }
    }
    return returnvalue;
}

bool walker::walk(float x, float y)
{
    short dir;

    dir = facing(x, y);

    if (order == Order::Living)
    {
        const auto* fd = get_family_descriptor(family);
        if (fd && fd->is_stationary)
        {
            curdir = static_cast<signed char>(dir);
            return 1;
        }
    }

    if ( !x && !y)
    {
        //Log("walker %d:%d walking 0,0\n",order, family);
        //this happens sometimes, and shouldn't, but it is non-fatal
        return 1;
    }
    if (curdir == dir)  // if continue direction
    {
        // check if off map
        if (x+xpos < 0 ||
                x+xpos >= sim_level->grid.w*GRID_SIZE ||
                y+ypos < 0 ||
                y+ypos >= sim_level->grid.h*GRID_SIZE)
        {
            return 0;
        }

        // Here we check if the move is valid
        if (sim_level->query_passable(xpos+x, ypos+y, this))
        {
            // Control object does complete redraw anyway
            worldmove(x,y);
            cycle++;
            //if (!ani || (curdir*cycle > sizeof(ani)) )
            //  Log("WALKER::WALK: Bad ani!\n");
            if (ani[curdir][cycle] == -1)
                cycle = 0;
            set_frame(ani[curdir][cycle]);
            return 1;
        }
        else //Invalid move?
        {
            //we're not alive

            if (stats_->query_bit_flags(BIT_ANIMATE) )  // animate regardless..
            {
                cycle++;
                if (ani[curdir][cycle] == -1)
                    cycle = 0;
                set_frame(ani[curdir][cycle]);
            }
            return 0;
        }
    }
    else  // changed direction
    {
        curdir = static_cast<char>(dir);
        cycle = 0;
        set_frame(ani[curdir][cycle]);
        worldmove(0,0);
    }
    return 1;
}

float walker::get_current_angle()
{
    switch (curdir)
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

    //   We use a clock-ordered
    //   of directions to numbers) to a clock-ordered
    //   mapping of directions so we can calculate what
    //   our next facing should be based on our current one.

    // Find how  we have to turn.
    distance = static_cast<short>(curdir - targetdir);

    // Figure out if we should turn clockwise or counterclockwise
    if ( ( (distance >= -4) && (distance < 0) ) || (distance >= 4) )
        curdir = static_cast<char>((curdir+1) %8);
    else
        curdir = static_cast<char>((curdir+7) %8);

    // Now set our lastx and lasty (facing) variables correctly
    const bool stationary = (order == Order::Living) && [&]{
        const auto* fd = get_family_descriptor(family);
        return fd && fd->is_stationary;
    }();
    if (!stationary)
    {
        switch (curdir)
        {
            case FACE_UP:
                lastx = 0;
                lasty = -stepsize;
                break;
            case FACE_UP_RIGHT:
                lastx = stepsize;
                lasty = -stepsize;
                break;
            case FACE_RIGHT:
                lastx = stepsize;
                lasty = 0;
                break;
            case FACE_DOWN_RIGHT:
                lastx = stepsize;
                lasty = stepsize;
                break;
            case FACE_DOWN:
                lastx = 0;
                lasty = stepsize;
                break;
            case FACE_DOWN_LEFT:
                lastx = -stepsize;
                lasty = stepsize;
                break;
            case FACE_LEFT:
                lastx = -stepsize;
                lasty = 0;
                break;
            case FACE_UP_LEFT:
                lastx = -stepsize;
                lasty = -stepsize;
                break;
            default :
                lastx = 0;
                lasty = -stepsize;
        }
    }
    cycle = 0;
    set_frame(ani[curdir][cycle]);
    worldmove(0,0);
    return true;
}
