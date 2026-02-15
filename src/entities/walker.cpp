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
//walker.cpp

/* ChangeLog
	buffers: 7/31/02: *deleted some redundant headers
*/

#include <openglad/core/combat_math.h>
#include <openglad/core/stats.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/entities/weapon_family_registry.h>
#include <openglad/entities/generator_family_descriptor.h>
#include <openglad/entities/generator_family_registry.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/render/smooth.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/legacy/test_trace.h>
#include <format>
#include <span>

// Shorthand for the injectable RNG
static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

static inline cfg_store& active_config()
{
    if(ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}

// ************************************************************
//  WALKER -- graphics routines
//
//  WALKER is a PIXIEN with automatic frame changing when
//  the direction it moves is changed.  This allows for
//  the concept of "facings", though currently no query-able
//  variable allows for external functions to learn the facing.
// ************************************************************

bool debug_draw_paths = false;

// From picker.cpp
extern Sint32 calculate_level(Uint32 temp_exp);
extern Sint32 difficulty_level[DIFFICULTY_SETTINGS];
extern Sint32 current_difficulty;

// from glad.cpp
short remaining_foes(screen *myscreen, walker* myguy);
short exp_from_action(ExpAction action, walker* w, walker* target, short value);

walker::walker(const PixieData& data)
    : pixieN(data)
{
	// Set our stats ..
	stats_ = std::make_unique<statistics>(this);

	curdir = FACE_DOWN;  // We are facing DOWN
	enddir = FACE_DOWN;  // We are trying to face DOWN
	lastx = 0;
	lasty = 0;
	act_type = ACT_RANDOM;
	collide_ob = nullptr;
	cycle = 0;
	ani = nullptr;
	team_num = 0;
	real_team_num = 255;  // to show nothing's changed
	ani_type = 0;
	busy = 0;
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	myguy = nullptr;
	myself_ = this;
	dead = 0; // we're alive

	death_called = 0;


	bonus_rounds = 0;
	shifter_down = 0; // the player's shifter/alternate is NOT pressed
	view_all = 0;     // by default can't see treasures, etc. on radar
	keys = 0; // no keys

	action = 0; // no special action mode
	ignore = 0; // don't ignore us! Collide with us...
	default_weapon = current_weapon = FAMILY_KNIFE; // just in case ..
	user = -1; // default user status = no user
	// Set our stats ..
	set_frame(0);

	yo_delay = 0;

	flight_left = 0;
	invulnerable_left = 0;
	invisibility_left = 0;
	speed_bonus = 0;
	speed_bonus_left = 0;
	regen_delay_ = 0;
	charm_left_ = 0;
	outline = 0;
	drawcycle = 0;

	skip_exit = 0;
	xpos = ypos = -1; //this to correct a problem with these not being alloced?
	worldx_ = worldy_ = -1;

	weapons_left = 1; // default, used for fighters
	// Do not implicitly bind to the global screen obmap here. Only walkers that
	// are registered into a LevelData list should participate in obmap collision
	// bookkeeping. (Unregistered test-only walkers otherwise leave stale pointers
	// behind across tests under ASan.)
	myobmap = nullptr;

	path_check_counter = 5 + rand()%10;
	hurt_flash = false;
	attack_lunge = 0.0f;
	hit_recoil = 0.0f;

	last_hitpoints = 0.0f;
}

void walker::set_myguy_view(guy* guy_view)
{
	owned_myguy_.reset();
	myguy = guy_view;
}

void walker::set_owned_myguy(std::unique_ptr<guy> owned_guy)
{
	owned_myguy_ = std::move(owned_guy);
	myguy = owned_myguy_.get();
}

void walker::clear_myguy()
{
	owned_myguy_.reset();
	myguy = nullptr;
}

void walker::move_myguy_to(walker* target)
{
	if (target == nullptr)
		return;

	if (owned_myguy_)
		target->set_owned_myguy(std::move(owned_myguy_));
	else
		target->set_myguy_view(myguy);

	myguy = nullptr;
}

bool
walker::reset(void)
{

	// double comments we're needed to make it work, maybe they are
	// not needed??

	//  curdir = 0;  // We are facing UP
	//  enddir = 0;  // We are trying to face UP
	//  lastx = 0;
	//  lasty = 0;
	//  act_type = ACT_RANDOM;
	//  collide_ob = nullptr;

	// // cycle = 0;

	//  ani = nullptr;

	// //  team_num = 0;
	// //  ani_type = 0;
	// //  busy = 0;

	//  foe = nullptr;
	//  leader = nullptr;
	//  owner = nullptr;
	//  myguy = nullptr;
	//  myself = this;
	//  ani = nullptr;
	dead = 0; // we're alive

	death_called = 0;


	//  bonus_rounds = 0;
	//  shifter_down = 0; // the player's shifter/alternate is NOT pressed
	//  view_all = 0;     // by default can't see treasures, etc. on radar
	//  keys = 0; // no keys

	//  action = 0; // no special action mode
	ignore = 0; // don't ignore us! Collide with us...
	//  default_weapon = current_weapon = FAMILY_KNIFE; // just in case ..
	//  user = -1; // default user status = no user
	// Set our stats ..
	//  set_frame(0);

	//  yo_delay = 0;

	flight_left = 0;
	//  invulnerable_left = 0;
	//  invisibility_left = 0;
	//  outline = 0;
	//  drawcycle = 0;

	//  skip_exit = 0;
	//  xpos = ypos = -1; //this to correct a problem with these not being alloced?

	//  weapons_left = 1; // default, used for fighters
	path_check_counter = 5 + rand()%10;
    regen_delay_ = 0;

	if (stats_)
		stats_->bit_flags = 0;

	hurt_flash = false;
	attack_lunge = 0.0f;
	hit_recoil = 0.0f;

	last_hitpoints = 0.0f;
	
	return 1;
}

walker::~walker()
{
	//  Log("(Death) Removed ORDER %d FAMILY %d, pos %dx%d\n", order, family,
	//    xpos, ypos); //debugging memory
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	collide_ob = nullptr;
	dead = 1;

	// Walkers can outlive a particular LevelData::myobmap instance in tests
	// (screen cleanup replaces the obmap). Ensure we remove from the current
	// active obmap as well as the one we were last bound to.
	obmap* active = (myscreen != nullptr) ? myscreen->level_data.myobmap.get() : nullptr;
	if (active != nullptr)
		active->remove(this);
	if (myobmap != nullptr && myobmap != active)
		myobmap->remove(this); // remove ourselves from obmap lists

	stats_.reset();
	bmp = nullptr;
	clear_myguy();
	myself_ = nullptr;


}

// Movement/facing/turning methods moved to walker_movement.cpp.

// This is the function you actually call when you want something
// to fire.  It initializes the animation if animation is valid
// and checks to see if the object is too busy.
bool walker::init_fire()
{
	return init_fire(static_cast<short>(lastx), static_cast<short>(lasty));
}

bool walker::init_fire(short xdir, short ydir)
{
	// Turn if we want to fire another direction

	// If a non-player fires in a set direction, turn!

	if (facing(xdir, ydir) != curdir)
	{
		enddir = static_cast<char>(facing(xdir, ydir));
	}
	if (curdir != enddir && query_order() == Order::Living)
	{
		//if (family==FAMILY_TOWER1)
		//  enddir = curdir;
		if (query_act_type() == ACT_CONTROL)
			return 0;
		else
			return turn(enddir);
	}

	if (busy > 0)
		return 0;  // Too busy

	busy += fire_frequency; // This pauses a few rounds

	//  if (ani_type == ANI_WALK && query_order() == Order::Living)
	if (ani_type == ANI_WALK)  // This should allow generators to animate
	{
		ani_type = ANI_ATTACK;
		cycle = 0;
		animate();
		return 1;
	}
	else
	{
		if (fire())
			return 1;
		else
			return 0;
	}
}

walker  * walker::fire()
{
	walker  *weapon = nullptr;
	signed char waver;
	//short xp, yp;

	// Do we have enough spellpoints for our weapon
	if (stats_->magicpoints < stats_->weapon_cost)
		return nullptr;

	weapon = create_weapon();
	if (!weapon)
		return nullptr;

	stats_->magicpoints -= stats_->weapon_cost;

	// Determine how much the thrown weapon can 'waver'
	waver = static_cast<signed char>((weapon->stepsize)/2); // Absolute amount ..
	waver = static_cast<signed char>(rng(waver+1) - waver/2);

	switch(facing(lastx, lasty))
	{
		case FACE_RIGHT:
			weapon->setxy(xpos+sizex+1,ypos+(sizey - weapon->sizey)/2);
			weapon->lastx = weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+(sizey-weapon->sizey)/2);
			weapon->lastx = -weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_DOWN:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos+sizey+1);
			weapon->lasty = weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos - weapon->sizey-1);
			weapon->lasty = - weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos-weapon->sizey-1);
			weapon->lastx = weapon->stepsize + waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_UP_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos-weapon->sizey-1);
			weapon->lastx = -weapon->stepsize - waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_DOWN_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos + sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = weapon->stepsize - waver;
			break;
		case FACE_DOWN_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = -weapon->stepsize + waver;
			break;
	}

	weapon->set_frame(frame);
	// Make sure our current direction is wrong so first walk
	// will just be draw (grumble curse)
	weapon->curdir = static_cast<char>((frame+1)%2);

	//xp = weapon->xpos;
	//yp = weapon->ypos;

	// Actual combat
	if (!myscreen->query_passable(weapon->xpos, weapon->ypos, weapon))
	{
		// *** Melee combat ***
		if (weapon->collide_ob && !weapon->collide_ob->dead)
		{
			if (attack(weapon->collide_ob) && on_screen() )
			{
				og::sim::emit_sound(SOUND_CLANG);
				
                if(active_config().is_on("effects", "attack_lunge"))
                {
                    if(query_order() == Order::Living)
                    {
                        attack_lunge = 1.0f;
                        attack_lunge_angle = get_current_angle();
                    }
                }
			}
			if (myguy)
            {
				myguy->total_shots++; // record that we fired/attacked
				myguy->scen_shots++;
            }
		}
		weapon->dead = 1;
		return nullptr;
	}
	else if (stats_->query_bit_flags(BIT_NO_RANGED))
	{
		weapon->dead = 1;
		return nullptr;
	}
	else
	{
		if (order == Order::Living)
		{
			const auto* fd = get_family_descriptor(family);
			if (fd && fd->on_fire_weapon)
			{
				if (!fd->on_fire_weapon(this, weapon))
					return nullptr;
			}
		}
        
		// Record our shot ..
		if (myguy)
        {
			myguy->total_shots++;
			myguy->scen_shots++;
        }

		// *** Ranged combat ***
		if (on_screen())
		{
			const auto* wfd = get_weapon_family_descriptor(weapon->query_family());
			og::sim::emit_sound(static_cast<std::uint32_t>(wfd ? wfd->fire_sound : SOUND_FWIP));
		}
		if (order == Order::Generator)
		{
			const auto* gfd = get_generator_family_descriptor(family);
			if (gfd)
			{
				if (gfd->spawn_ani_type != 0)
					weapon->ani_type = gfd->spawn_ani_type;
				if (gfd->has_lifetime)
					weapon->lifetime = 800 + stats_->level*11;
				weapon->stats()->level = static_cast<Sint32>(rng(static_cast<Uint32>(stats_->level))) + 1;
				weapon->set_difficulty(static_cast<Uint32>(weapon->stats()->level));
				if (gfd->clear_owner)
					weapon->owner = nullptr;
			}
		}
		// Living-family weapon modifications handled by on_fire_weapon above
		return weapon;
	}

}

void walker::set_weapon_heading(walker *weapon)
{
	signed char waver;

	// Determine how much the thrown weapon can 'waver'
	waver = static_cast<signed char>((weapon->stepsize)/2); // Absolute amount ..
	waver = static_cast<signed char>(rng(waver+1) - waver/2);

	switch(facing(lastx, lasty))  // these are from the 'owner'
	{
		case FACE_RIGHT:
			weapon->setxy(xpos+sizex+1,ypos+(sizey - weapon->sizey)/2);
			weapon->lastx = weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+(sizey-weapon->sizey)/2);
			weapon->lastx = -weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_DOWN:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos+sizey+1);
			weapon->lasty = weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos - weapon->sizey-1);
			weapon->lasty = - weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos-weapon->sizey-1);
			weapon->lastx = weapon->stepsize + waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_UP_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos-weapon->sizey-1);
			weapon->lastx = -weapon->stepsize - waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_DOWN_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos + sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = weapon->stepsize - waver;
			break;
		case FACE_DOWN_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = -weapon->stepsize + waver;
			break;
	}

}

// To avoid problems with limited precision
bool float_eq(float a, float b)
{
    return (a == b || (a - 0.000001f < b && a + 0.000001f > b));
}

void draw_smallHealthBar(walker* w, viewscreen* view_buf)
{
    if(!active_config().is_on("effects", "mini_hp_bar"))
    {
        return;
    }

    if(w->query_order() != Order::Living && w->query_order() != Order::Generator)
    {
        return;
    }

	Sint32 xscreen = static_cast<Sint32>(w->xpos - view_buf->topx + view_buf->xloc);
	Sint32 yscreen = static_cast<Sint32>(w->ypos - view_buf->topy + view_buf->yloc);

    const Sint32 walkerstartx = xscreen;
    const Sint32 walkerstarty = yscreen;
    const Sint32 portstartx = view_buf->xloc;
    const Sint32 portstarty = view_buf->yloc;
    const Sint32 portendx = view_buf->endx;
    const Sint32 portendy = view_buf->endy;

    SDL_Rect r{static_cast<int>(walkerstartx),
               static_cast<int>(walkerstarty + w->sizey + 1),
               static_cast<int>(w->sizex),
               1};
    if(r.x < portstartx || r.x > portendx || r.y < portstarty || r.y > portendy)
        return;
    
    // Last hit's effect
    float last_points = w->last_hitpoints;
    float last_ratio = float(last_points)/w->stats()->max_hitpoints;
    
    // Current HP
    float points = w->stats()->hitpoints;
    float ratio = float(points)/w->stats()->max_hitpoints;
    
    unsigned char whatcolor;
    
    if (float_eq(points, w->stats()->max_hitpoints))
        whatcolor = MAX_HP_COLOR;
    else if ( (points * 3) < w->stats()->max_hitpoints)
        whatcolor = LOW_HP_COLOR;
    else if ( (points * 3 / 2) < w->stats()->max_hitpoints)
        whatcolor = MID_HP_COLOR;
    else if (points < w->stats()->max_hitpoints)
        whatcolor = LIGHT_GREEN;//HIGH_HP_COLOR;
    else 
        whatcolor = ORANGE_START;
    
    if(ratio >= 0.0f)
    {
        if(ratio < 0.95f)
        {
            const Sint32 max_w = r.w;
            const float width_f = static_cast<float>(r.w);

            if(w->last_hitpoints > w->stats()->hitpoints && last_ratio <= 1.0f)
            {
                const Sint32 last_w = static_cast<Sint32>(width_f * last_ratio);
                myscreen->draw_box(r.x, r.y, r.x + last_w, r.y + r.h, static_cast<unsigned char>(53), 1);
            }

            const Sint32 cur_w = static_cast<Sint32>(width_f * ratio);
            myscreen->draw_box(r.x, r.y, r.x + cur_w, r.y + r.h, whatcolor, 1);
            myscreen->draw_box(r.x-1, r.y-1, r.x + max_w+1, r.y + r.h+1, BLACK, 0);
        }
    }
}


walker::DamageNumber::DamageNumber(float x_, float y_, float value_, unsigned char color_)
    : x(x_), y(y_), t(1.0f), value(value_), color(color_)
{}

void walker::DamageNumber::draw(viewscreen* view_buf)
{
	const float xscreen_f = x - static_cast<float>(view_buf->topx) + static_cast<float>(view_buf->xloc);
	const float yscreen_f = y - static_cast<float>(view_buf->topy) + static_cast<float>(view_buf->yloc);
	const Sint32 xscreen = static_cast<Sint32>(xscreen_f);
	const Sint32 yscreen = static_cast<Sint32>(yscreen_f);
	
	Uint8 alpha = 0;
	if (t >= 1.0f)
		alpha = 255;
	else if (t > 0.0f)
		alpha = static_cast<Uint8>(t * 255.0f);
	myscreen->text_normal.write_xy_center_alpha(xscreen, yscreen, color, alpha, "%.0f", value);
}

#define ATTACK_LUNGE_SIZE 5
#define HIT_RECOIL_SIZE 3

bool walker::draw(viewscreen  *view_buf)
{
    // Update the drawing coords from the real position
    xpos = static_cast<short>(worldx_);
    ypos = static_cast<short>(worldy_);
    
	Sint32 xscreen, yscreen;

	//no need for on screen check, it will be checked at the draw level
	//and the draw level code is cleaner anyway
	//if (!this) return 0;
	if (dead)
	{
		Log("drawing a dead guy!\n");
		return 0;
	}
	//if (!bmp) {Log("No bitmap!\n"); return 0;}
	drawcycle++;

	xscreen = static_cast<Sint32>(xpos - view_buf->topx + view_buf->xloc);
	yscreen = static_cast<Sint32>(ypos - view_buf->topy + view_buf->yloc);
	
	if(attack_lunge > 0.0f)
	    {
	        const float dx = attack_lunge * ATTACK_LUNGE_SIZE * cosf(attack_lunge_angle);
	        const float dy = attack_lunge * ATTACK_LUNGE_SIZE * sinf(attack_lunge_angle);
	        xscreen += static_cast<Sint32>(dx);
	        yscreen += static_cast<Sint32>(dy);
	    }
    
	if(hit_recoil > 0.0f)
	    {
	        const float dx = hit_recoil * HIT_RECOIL_SIZE * cosf(hit_recoil_angle);
	        const float dy = hit_recoil * HIT_RECOIL_SIZE * sinf(hit_recoil_angle);
	        xscreen += static_cast<Sint32>(dx);
	        yscreen += static_cast<Sint32>(dy);
	    }

	if (stats_->query_bit_flags( BIT_NAMED ) || invisibility_left || flight_left || invulnerable_left)
	{
		if (outline == OUTLINE_INVULNERABLE)
		{
			if      (flight_left)
				outline = OUTLINE_FLYING;
			else if (view_buf->control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
		}
		else if (outline == OUTLINE_FLYING)
		{
			//if      (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num)) outline = OUTLINE_NAMED;
			//else if (invisibility_left) outline = OUTLINE_INVISIBLE;
			//else if (invulnerable_left) outline = OUTLINE_INVULNERABLE;

			if (view_buf->control)
				if      (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
			{
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
				else if (invulnerable_left)
					outline = OUTLINE_INVULNERABLE;
			}
		}
		else if (outline == OUTLINE_NAMED)
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
		}
		else if (outline == OUTLINE_INVISIBLE)
		{
			if      (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (view_buf->control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;
		}
		else
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (view_buf->control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;
		}
	}
	else
	{
	    outline = 0;
	}
	
	if(view_buf->control != nullptr)
    {
        if(outline == 0 && user != -1 && this != view_buf->control && this->team_num == view_buf->control->team_num)
            outline = OUTLINE_INVISIBLE;
    }
    
    bool should_draw_hp = true;
    int fill_mode = 0;
    int outline_style = 0;
    int invisibility_amount = 0;
    int phantom_mode = 0;
    
	if (stats_->query_bit_flags(BIT_PHANTOM)) //WE ARE A PHANTOM
    {
        fill_mode = PHANTOM_MODE;
        phantom_mode = SHIFT_RANDOM;
        should_draw_hp = false;
    }
	else if (invisibility_left && view_buf->control != nullptr)  //WE ARE INVISIBLE
	{
		if (this->team_num == view_buf->control->team_num)
        {
            fill_mode = INVISIBLE_MODE;
            invisibility_amount = ( invisibility_left + 10 );
            outline_style = outline;
            should_draw_hp = false;
        }
	}
	else if (stats_->query_bit_flags(BIT_FORESTWALK) && 
	         myscreen->level_data.mysmoother.query_genre_x_y(xpos/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	         && !stats_->query_bit_flags(BIT_FLYING)
	         && (flight_left < 1) )
    {
        fill_mode = INVISIBLE_MODE;
        invisibility_amount = 1000;
        outline_style = 1;
        should_draw_hp = false;
    }
	else if (outline)    // WE HAVE SOME OUTLINE
	{
	    fill_mode = OUTLINE_MODE;
	    outline_style = outline;
	}
	
	// Draw me
	if(hurt_flash)
    {
        hurt_flash = false;
        
        auto bmp_span = std::span<const unsigned char>{bmp, static_cast<size_t>(sizex * sizey)};
        myscreen->walkputbuffer_flash(xscreen, yscreen, sizex, sizey,
                                   view_buf->xloc, view_buf->yloc,
                                   view_buf->endx, view_buf->endy,
                                   bmp_span, query_team_color());
    }
    else
    {
        auto bmp_span = std::span<const unsigned char>{bmp, static_cast<size_t>(sizex * sizey)};
        if(fill_mode == 0 && outline_style == 0)
        {
            myscreen->walkputbuffer(xscreen, yscreen, sizex, sizey,
                                   view_buf->xloc, view_buf->yloc,
                                   view_buf->endx, view_buf->endy,
                                   bmp_span, query_team_color());
        }
        else
        {
	            myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
	                                    view_buf->xloc, view_buf->yloc,
	                                    view_buf->endx, view_buf->endy,
	                                    bmp_span, query_team_color(),
	                                    static_cast<unsigned char>(fill_mode), //mode
	                                    invisibility_amount, //invisibility
	                                    static_cast<unsigned char>(outline_style), //outline
	                                    static_cast<unsigned char>(phantom_mode)); //type of phantom
	        }
	    }
	
	if(should_draw_hp)
        draw_smallHealthBar(this, view_buf);
	
	for(auto e = damage_numbers.begin(); e != damage_numbers.end();)
    {
        e->t -= 0.05f;
        if(e->t < 0)
        {
            e = damage_numbers.erase(e);
            continue;
        }
        
        e->y -= 1.5f;
        if(view_buf->control == this)
            e->draw(view_buf);
        e++;
    }
	
	if(debug_draw_paths)
        draw_path(view_buf);
	return 1;
}

void walker::compute_outline(const walker* viewer_control)
{
	if (stats_->query_bit_flags( BIT_NAMED ) || invisibility_left || flight_left || invulnerable_left)
	{
		if (outline == OUTLINE_INVULNERABLE)
		{
			if      (flight_left)
				outline = OUTLINE_FLYING;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
		}
		else if (outline == OUTLINE_FLYING)
		{
			if (viewer_control)
				if      (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
			{
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
				else if (invulnerable_left)
					outline = OUTLINE_INVULNERABLE;
			}
		}
		else if (outline == OUTLINE_NAMED)
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
		}
		else if (outline == OUTLINE_INVISIBLE)
		{
			if      (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;
		}
		else
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;
		}
	}
	else
	{
	    outline = 0;
	}

    if(outline == 0 && user != -1 && viewer_control && this != viewer_control && this->team_num == viewer_control->team_num)
        outline = OUTLINE_INVISIBLE;
}

bool walker::draw_tile(viewscreen  *view_buf)
{
	Sint32 xscreen, yscreen;

	//no need for on screen check, it will be checked at the draw level
	//and the draw level code is cleaner anyway
	//if (!this) return 0;
	if (dead)
	{
		Log("drawing a dead guy!\n");
		return 0;
	}
	//if (!bmp) {Log("No bitmap!\n"); return 0;}
	drawcycle++;

	xscreen = static_cast<Sint32>(xpos - view_buf->topx + view_buf->xloc);
	yscreen = static_cast<Sint32>(ypos - view_buf->topy + view_buf->yloc);

	compute_outline(view_buf->control);

	auto bmp_span = std::span<const unsigned char>{bmp, static_cast<size_t>(sizex * sizey)};

	if (stats_->query_bit_flags(BIT_PHANTOM)) //WE ARE A PHANTOM
		myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, query_team_color(),
		                        PHANTOM_MODE, //mode
		                        0, //invisibility
		                        0, //outline
		                        SHIFT_RANDOM); //type of phantom

	else if (invisibility_left)  //WE ARE INVISIBLE
	{
		if (this->team_num == view_buf->control->team_num)
			myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
			                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
			                        bmp_span, query_team_color(),
			                        INVISIBLE_MODE,  //mode
			                        ( invisibility_left + 10 ), //invisibility
			                        outline,  //outline
			                        0 ); //type of phantom
	}
	else if (stats_->query_bit_flags(BIT_FORESTWALK) &&
	         myscreen->level_data.mysmoother.query_genre_x_y(xpos/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	         && !stats_->query_bit_flags(BIT_FLYING)
	         && (flight_left < 1) )
		myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, query_team_color(),
		                        INVISIBLE_MODE,  //mode
		                        1000, //invisibility
		                        1,  //outline
		                        0 ); //type of phantom

	else if (outline)    // WE HAVE SOME OUTLINE
	{
		myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, query_team_color(),
		                        OUTLINE_MODE, //mode
		                        0, //invisibility
		                        outline, //outline
		                        0 ); //type of phantom

        draw_smallHealthBar(this, view_buf);
	}
	else
	{
		myscreen->walkputbuffer(xscreen, yscreen, sizex, sizey,
		                       view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                       bmp_span, query_team_color());

        draw_smallHealthBar(this, view_buf);
	}

	return 1;
}



bool walker::act()
{
	short temp;

	// Make sure everyone we're pointing to is valid
	if (foe && foe->dead)
		foe = nullptr;
	if (leader && leader->dead)
		leader = nullptr;
	if (owner && owner->dead)
		owner = nullptr;

	collide_ob = nullptr; // always start with no collison..

	// Complete previous animations (like firing)
	if (ani_type != ANI_WALK)
		return animate();

	// Are we frozen?
	if (stats_->frozen_delay)
	{
		stats_->frozen_delay--;
		return 1;
	}

	if (busy > 0)
		busy--; // This allows busy to be our FIRING delay.
	// Find new action

	// Turn if you want to
	//  if (curdir != enddir && query_order() == Order::Living)
	//       return turn(enddir);


	// No actions for us if we are ACT_CONTROL!
	//  if (act_type == ACT_CONTROL)
	//              stats_->clear_command();


	// Are we performing some action?
	if (stats_->has_commands())
	{
		temp = stats_->do_command();
		if (temp)
			return 1;
	}
	
	if(attack_lunge > 0.0f)
    {
        attack_lunge -= 0.4f;
        if(attack_lunge < 0.0f)
            attack_lunge = 0.0f;
    }
	
	if(hit_recoil > 0.0f)
    {
        hit_recoil -= 0.6f;
        if(hit_recoil < 0.0f)
            hit_recoil = 0.0f;
    }
	
	switch (act_type)
	{
			// We are the control character
		case ACT_CONTROL:
			{
				return 1;

				//break;
			}
			// We are a generator
		case ACT_GENERATE:
			{
				act_generate();
				break;
			}
			// We are a weapon
		case ACT_FIRE:
			{
				act_fire();
				return 1;
				//break;
			}
		case ACT_GUARD:
			{
				act_guard();
				break;
			}
		case ACT_DIE:
			{
				this->dead = 1;
				return 1;
			}
			// We are randomly walking toward enemy
		case ACT_RANDOM:
			{
				if (!rng(4) )
				{
					if (!rng(20))   // a 1 in 4 then 1 in 20 chance of rand walk
					{
						if (!special())
							stats_->try_command(COMMAND_WALK, rng(30),
							                   rng(3)-1, rng(3)-1);
						return 1;
					}
					act_random(); //1 in 4 followed by 19 in 20 of doing this
				}
				else    //3 of 4 times
				{
					if (!foe)
					{
						foe = myscreen->find_far_foe(this);
					}
					if (foe)
						//stats_->try_command(COMMAND_SEARCH, 60, 0, 0);
						stats_->try_command(COMMAND_SEARCH, 500, 0, 0);
					return 1;
				}
			}  // END RANDOM
			break;
		default:
			{
				Log("No act type set.\n");
				return 0;
			}
	}  // END SWITCH
	return 0;
}

short walker::set_act_type(short num)
{
	old_act_type = act_type;
	act_type = static_cast<char>(num);
	return num;
}

short walker::restore_act_type()
{
	act_type = old_act_type;
	return old_act_type;
}

short walker::query_act_type() const
{
	return act_type;
}

short walker::set_old_act_type(short num)
{
	old_act_type = static_cast<char>(num);
	return num;
}

short walker::query_old_act_type() const
{
	return old_act_type;
}

bool walker::collide(walker  *ob)
{
	collide_ob = ob;
	return 1;
}



bool walker::animate()
{
	const int ani_index = curdir + ani_type * NUM_FACINGS;
	const signed char* seq = ani[ani_index];
	if (!seq)
	{
		ani_type = ANI_WALK;
		cycle = 0;
		return 0;
	}

	// Animation sequences are sentinel-terminated with -1, but `cycle` is a signed
	// char and can wrap/retain stale values. Compute a safe sequence length and
	// clamp `cycle` to avoid reading past the sentinel. (ASan found an OOB read
	// here during test-driven menu/game loops.)
	int seq_len = 0;
	while (seq_len < 128 && seq[seq_len] != -1)
		seq_len++;
	if (seq_len <= 0 || seq_len >= 128)
	{
		ani_type = ANI_WALK;
		cycle = 0;
		return 0;
	}

	int c = static_cast<int>(cycle);
	if (c < 0)
		c = 0;

	// If cycle is already past the end (e.g. walk() advanced it beyond the
	// current animation's bounds), treat as end-of-animation rather than
	// restarting from frame 0.  Resetting to 0 loops forever when walk()
	// keeps pushing cycle forward each frame (attack-while-running bug).
	bool at_end;
	if (c >= seq_len)
	{
		at_end = true;
		cycle = 0;
	}
	else
	{
		set_frame(seq[c]);
		c++;
		at_end = (c >= seq_len);
		cycle = static_cast<signed char>(c);
	}

	if (at_end)
	{
		//          if (ani_type == ANI_ATTACK &&
		//                        query_order() == Order::Living)
		if (ani_type == ANI_ATTACK)
		{
			fire();
			ani_type = ANI_WALK;
			cycle = 0;
			return 1;
		}
		if (ani_type == ANI_SKEL_GROW && order == Order::Living)
		{
			const auto* fd = get_family_descriptor(family);
			if (fd && fd->init_ani_type == ANI_SKEL_GROW)
			{
				ani_type = ANI_WALK;
				cycle = 0;
				return 1;
			}
		}
		if (ani_type == ANI_TELE_OUT && order == Order::Living)
		{
			const auto* fd = get_family_descriptor(family);
			if (fd && fd->handle_teleport && fd->handle_teleport(this))
				return 1;
			// Default: no teleport handler, just stop
			ani_type = ANI_WALK;
			cycle = 0;
			return 0;
		}
		if (order == Order::Living)
		{
			const auto* fd2 = get_family_descriptor(family);
			if (fd2 && fd2->on_ani_complete && fd2->on_ani_complete(this))
				return 1;
		}

		ani_type = ANI_WALK;
		cycle = 0;
	}
	return 1;
}

bool walker::set_order_family(Order neworder, char newfamily)
{
	order = neworder;
	family = newfamily;
	return 1;
}

walker  *walker::create_weapon()
{
	walker  *weapon;
	short weapon_type;


	// Special case for generators
	if (query_order() == Order::Generator)
	{
		weapon = myscreen->level_data.add_ob(Order::Living, static_cast<char>(default_weapon));
		weapon->team_num = team_num;
		weapon->owner = this;
		weapon->set_difficulty(static_cast<Uint32>(stats_->level));
		return weapon;
	}
	// Normally, only livings fire
	weapon_type = current_weapon;

	weapon = myscreen->level_data.add_ob(Order::Weapon, static_cast<char>(weapon_type));
	weapon->team_num = team_num;
	weapon->owner = this;
	weapon->set_difficulty(static_cast<Uint32>(stats_->level));
	weapon->damage = (weapon->damage * (static_cast<float>(stats_->level) + 3.0f)) / 4.0f;
	if (myguy)
	{
		weapon->lineofsight += (myguy->strength / 23) + (myguy->dexterity / 31);
		weapon->damage += (myguy->strength / 7.0f);
	}
	else
	{
		weapon->damage *= static_cast<float>(stats_->level);
	}
	weapon->lineofsight += (stats_->level / 3);
	switch ( facing(lastx, lasty) ) // make 'circular' ranges
	{
		case FACE_UP:
		case FACE_RIGHT:
		case FACE_DOWN:
		case FACE_LEFT:
			// this will multiply by 1.207 ..
			weapon->lineofsight *= 309;
			weapon->lineofsight /= 256; // = 1.207 for circular range
			// this will multiply by 1.414
			weapon->stepsize *= 362;
			weapon->stepsize /= 256;
			break;
		default :
			break;
	}

	if (order == Order::Living)
	{
		const auto* fd = get_family_descriptor(family);
		if (fd && fd->customize_weapon)
			fd->customize_weapon(this, weapon);
	}
	return weapon;
}

bool walker::query_next_to()
{
	short newx, newy;

	newx = xpos;
	newy = ypos;

	if (lastx > 0)
		newx += sizex;
	else if (lastx < 0)
		newx += -sizex;
	if (lasty > 0)
		newy += sizey;
	else //if (lasty < 0)
		newy += -sizey;

	if (!myscreen->query_object_passable(newx, newy, this))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

bool walker::fire_check(short xdelta, short ydelta)
{
	walker  *weapon = nullptr;
	//  short newx=0, newy=0;
	short i, loops;
	short xdir = 0;
	short ydir = 0;
	Sint32 distance;
	short targetdir;

	// Allow generators to 'always' succeed
	if (order == Order::Generator)
		return 1;

	weapon = create_weapon();
	if (!weapon)
		return 0;
	set_weapon_heading(weapon); // set lastx, lasty based on our facing...
	weapon->collide_ob = nullptr;
	// Based on facing, we alter the weapon's proposed
	//   size so the collision check is fooled into checking
	//   a Sint32 strip equal to the lineofsight times the size
	//   of the weapon.
	if (!foe)     // nobody to fire at?
	{
		//Log("fire check, no foe.\n");
		//this does happen! but it appears harmless
		return 0;
	}

	if (stats_->query_bit_flags(BIT_NO_RANGED))
	{
		weapon->dead = 1;
		return 0;
	}

	if (stats_->weapon_cost > stats_->magicpoints)
	{
		weapon->dead = 1;
		return 0;
	}

	distance = distance_to_ob(foe);
	if (distance > static_cast<Sint32>( static_cast<Sint32>(weapon->stepsize) * static_cast<Sint32>(weapon->lineofsight)) )
	{
		weapon->dead = 1;
		return 0;
	}

	targetdir = facing(xdelta,ydelta);
	if (targetdir != curdir)
	{
		//         turn(targetdir);
		weapon->dead = 1;
		return 0;
	}

		if (xdelta != 0)
			xdir = (xdelta > 0) ? 1 : -1;

		if (ydelta != 0)
			ydir = (ydelta > 0) ? 1 : -1;

	/* // why are we assuming walls don't matter in these two cases?
	  if (!xdelta || !ydelta) // aligned on a major axis
	  {
	         weapon->dead = 1;
	         return 1;
	  }
	 
	  if ( abs( abs(xdelta) - abs(ydelta) ) < 3)
	  {
	         weapon->dead = 1;
	         return 1;
	  }
	  else
	  {
	         weapon->dead = 1;
	//         return 0;
	  }
	*/

	// Run weapon through where it would go if all went well ..
	for (i=0; i < weapon->lineofsight; i++)
	{
		weapon->setxy(weapon->xpos + i*weapon->lastx,
		              weapon->ypos + i*weapon->lasty);
		if ( !myscreen->query_grid_passable(weapon->xpos, weapon->ypos, weapon) )
		{
			// we hit a wall, so fail
			weapon->dead = 1;
			return 0;
		}
		if ( !myscreen->query_object_passable(weapon->xpos, weapon->ypos, weapon) )
		{
			// we hit an enemy, so good!
			weapon->dead = 1;
			return 1;
		}
	}
	// By this point, we should have won or lost .. fail if we went our
	// range and didn't hit anyone ..
	weapon->dead = 1;
	return 0;

	// Determine # of loops to look for guy
	if ( abs(xdelta) > abs(ydelta) )
		loops = abs(xdelta);
	else
		loops = abs(ydelta);

	// * 16 is to match with grid coords
	for (i=0; i <= loops; i+=8)  // half a grid square
		if ( !myscreen->query_grid_passable(xpos+i*xdir, ypos+i*ydir, weapon) )
		{
			weapon->dead = 1;
			//foe = nullptr;  // can't hit this guy
			//stats_->try_command(COMMAND_RANDOM_WALK, rng(8));
			return 0;
		}
	weapon->dead = 1;

	// We have a good chance of hitting, so ..
	return 1;

}

/****************************************************
*
*  Act routines (static)
*
****************************************************/

bool
walker::act_generate()
{
	if ( myscreen->level_data.numobs < MAXOBS &&
	        (rng(static_cast<Uint32>(stats_->level * 3)) > rng(static_cast<Uint32>(300 + (myscreen->level_data.numobs * 8))) )
	   )
	{
		lastx = static_cast<float>(1 - static_cast<Sint32>(rng(3)));
		lasty = static_cast<float>(1 - static_cast<Sint32>(rng(3)));
		if (!lastx && !lasty)
			lastx = 1;
		init_fire(static_cast<short>(lastx), static_cast<short>(lasty));
		//    lastx = 0;
		//    lasty = 0;
		stats_->hitpoints++;
		if (stats_->hitpoints > stats_->max_hitpoints)
			stats_->hitpoints--;
	}
	return 1;
}

bool
walker::act_fire()
{
	if (!(lineofsight--)) // this is the range of the weapon
	{
		dead = 1;
		death();
	}
	else if (!walk() || stats_->query_bit_flags(BIT_NO_COLLIDE))
	{
		// Hit the collide_ob;
		if (collide_ob && !collide_ob->dead)
		{
			attack(collide_ob);
		}
		if (!stats_->query_bit_flags(BIT_IMMORTAL))
		{
			dead = 1;
			death();
		}
	}
	return 1;
}

bool
walker::act_guard()
{

	// Check all directions for foes
	//   if we find one, fire
	//              if (fire_check(lastx, lasty) ||
	//                       fire_check(lasty, lastx) ||
	//                       fire_check(-lasty, -lastx) ||
	//                       fire_check(-lastx, -lasty))
	foe = myscreen->find_near_foe(this);
	if (foe)
	{
		curdir = static_cast<char>(facing(foe->xpos - xpos, foe->ypos-ypos));
		stats_->try_command(COMMAND_FIRE,rng(30));
		return 1;
	}
	else
		return 0;
}

bool
walker::act_random()
{
	short newx, newy;
	short xdist, ydist;

	// Specially put in to attempt to make enemy harder
	//if (rng(sizex/GRID_SIZE)) return 0;

	// Find our foe
	if (!rng(70) || (!foe))
		foe = myscreen->find_far_foe(this);
	if (!foe)
		return stats_->try_command(COMMAND_RANDOM_WALK,20);

	xdist = foe->xpos - xpos;
	ydist = foe->ypos - ypos;

	// If foe is in firing range, turn and fire
	if (abs(xdist) < lineofsight*GRID_SIZE &&
	        abs(ydist) < lineofsight*GRID_SIZE)
	{
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats_->set_command(COMMAND_FIRE, rng(24), xdist, ydist);
			return 1;
		}
		else
			// Nearest foe is blocked
			//foe = nullptr;
			turn(facing(xdist,ydist));
	}

	// Otherwise, try to walk toward foe
	newx = 0;
	newy = 0;

	if (foe)
	{
			newx = xdist;    // total horizontal distance..
			if (newx)                      // If it's not 0, then get
				newx = (newx > 0) ? 1 : -1;       // the normal of it..

			newy = ydist;
			if (newy)
				newy = (newy > 0) ? 1 : -1;
	}  // end of if we had a foe ..
		else
		{
			while ( !newx && !newy)
			{
				newx = static_cast<short>(1 - static_cast<Sint32>(rng(3)));   // Walk in some random direction
				newy = static_cast<short>(1 - static_cast<Sint32>(rng(3)));   // other than 0,0 :)
			}
		}

	// If blocked
	collide_ob = nullptr;

	// We can slide now, so always just walkstep, NOT using
	// stepsize ..
	return walkstep(newx, newy);
	//    return 1;
}

// Returns the spaces 'clear' around us, out of a maximum
// of eight ..
short walker::spaces_clear()
{
	short count = 0;
	short i, j;

	for (i=-1; i < 2; i++)
		for (j=-1; j < 2; j++)
			if (i || j) // don't check our own location
				if (myscreen->query_passable(xpos+(i*sizex), ypos+(j*sizey), this) )
					count++;

	return count;
}

void walker::transfer_stats(walker  *newob)
{
	short i;

	// First do the 'stats' stuff ..
	newob->stats()->hitpoints = stats_->hitpoints;
	newob->stats()->max_hitpoints = stats_->max_hitpoints;
	newob->stats()->heal_per_round = stats_->heal_per_round;
	newob->stats()->max_heal_delay = stats_->max_heal_delay;
	// Magic..
	newob->stats()->magicpoints = stats_->magicpoints;
	newob->stats()->max_magicpoints = stats_->max_magicpoints;
	newob->stats()->magic_per_round = stats_->magic_per_round/2;
	newob->stats()->max_magic_delay = stats_->max_magic_delay;

	newob->stats()->level = stats_->level;
	newob->stats()->frozen_delay = stats_->frozen_delay;
	for (i=0; i < 5; i++)
		newob->stats()->special_cost[i] = stats_->special_cost[i];
	newob->stats()->weapon_cost = stats_->weapon_cost;

	newob->stats()->bit_flags = stats_->bit_flags;
	newob->stats()->delete_me = stats_->delete_me;

	// Do we have a 'guy' ?
	if (myguy)
	{
		auto newguy = std::make_unique<guy>(*myguy);
		newob->set_owned_myguy(std::move(newguy));
	}
}

// change picture, etc. but NOT stats (use transfer_stats for that)

void walker::transform_to(Order whatorder, Sint32 whatfamily)
{
	short xcenter, ycenter;
	short tempxpos, tempypos;
	short reset = 0;
	short tempact = query_act_type();;

	// First remove us from the collision table..
	if (myobmap != nullptr)
		myobmap->remove(this);

	if (order == whatorder) // same object type
	{
		reset = 1;
		tempact = query_act_type();
	}

	// Reset bit flags
	stats_->clear_bit_flags();

	// Do this before resetting graphic so illegal
	//  family values don't try to set graphics.
	//  order and family are only set if legal
	myscreen->set_walker(this, whatorder, whatfamily);

	// Reset the graphics
	const PixieData& data = myscreen->level_data.myloader->graphics[PIX(order, family)];
	facings = data.data.get();
	bmp = data.data.get();
	frames = data.frames;
	frame = 0;
	cycle = 0;

	// Deal with resizing and centering ..
	xcenter = xpos + sizex/2;
	ycenter = ypos + sizey/2;

	sizex = data.w;
	sizey = data.h;
	size = static_cast<unsigned short>(sizex * sizey);

	tempxpos = xcenter - sizex/2;
	tempypos = ycenter - sizey/2;


	if (reset)
		set_act_type(tempact);

	setxy(tempxpos, tempypos);  // automatically re-adds us to the list ..
	// set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
	// Don't manually set the frame here -- it can break circles
	// of protection, etc., which are special cases .. instead:
	set_frame(0);
	animate();
}


// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
bool walker::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	walker  *newob = nullptr;
	Sint32 i;

	if (death_called)
		return 0;

	death_called = 1;

	// Ensure we are removed from collision bookkeeping as soon as we "die".
	// This prevents stale pointers in the obmap when callers manage walker
	// lifetimes outside LevelData's owning lists (common in tests).
	obmap* active = (myscreen != nullptr) ? myscreen->level_data.myobmap.get() : nullptr;
	if (active != nullptr)
		active->remove(this);
	if (myobmap != nullptr && myobmap != active)
		myobmap->remove(this);

	if (myguy) // were we a real character?  Then make a heart ..
	{
			newob = myscreen->level_data.add_ob(Order::Treasure, FAMILY_LIFE_GEM, 1);
			newob->stats()->hitpoints = static_cast<float>(myguy->query_heart_value());
			newob->stats()->hitpoints *= 0.75f / 2.0f;  // 75%, divided by 2, since score is doubled at end of level
			newob->team_num = team_num;
			newob->center_on(this);
		}

	switch (order)
	{
		case Order::Living:
			if (   (team_num == 0 || myguy) // our team
			        && (myscreen->level_data.type & SCEN_TYPE_SAVE_ALL)
			        && (stats_->name.size()) // we were named
			   )
				return myscreen->endgame(SCEN_TYPE_SAVE_ALL); // failed
			{
				auto* fd = get_family_descriptor(family);
				if (fd && fd->on_death)
				{
					fd->on_death(this);
				}
				else if (fd && !fd->leaves_bloodspot)
				{
					// Ghost, skeleton, tower etc. -- no bloodspot
				}
				else
				{
					generate_bloodspot();
				}
			}  // end of family dispatch
			break;  // end of order livings case
		case Order::Generator:  // go up in flames :>
			for (i=0; i < 4; i++)
			{
				newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION, 1);
				if (!newob) // failsafe
					break;
				newob->team_num = team_num;
				newob->stats()->level = stats_->level;
				newob->ani_type = ANI_EXPLODE;
				newob->setxy(xpos+rng(sizex-8)+4, ypos+4+rng(sizey-8) );
					newob->damage = static_cast<float>(stats_->level) * 2.0f;
					newob->set_frame(static_cast<short>(rng(3)));
				if (on_screen())
					og::sim::emit_sound(SOUND_EXPLODE);
			}
			break;
		case Order::FX:
			//case Order::Treasure:
			Log("Effect dying in walker?\n");
			break;          // end of effect object case
		default:
			break;
	}  // end of switching orders

	return 1;
}

// Generates bloodspot for desired walker...
void walker::generate_bloodspot()
{
	walker  *bloodstain;
	//char  *data;
	// Make permanent stain:

	dead = 1; // just in case ..

	bloodstain = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
	bloodstain->ignore = 1;
	transfer_stats(bloodstain);

	bloodstain->order  = Order::Treasure;
	bloodstain->family = FAMILY_STAIN;
	bloodstain->stats()->old_order = order;
	bloodstain->stats()->old_family= family;

	bloodstain->team_num = team_num;
	bloodstain->dead = 0;
	bloodstain->setxy(xpos, ypos);
	//data = myscreen->myloader->graphics[PIX(Order::Treasure, FAMILY_STAIN)];
	// We can't select other 'bloodspot' frames, because set_frame
	// appears to check the order and family and reset our picture
	// to a living guy .. we need to find a way around this ..
	bloodstain->set_frame(static_cast<short>(rng(4)));  // has no effect yet ..
	bloodstain->ani_type = ANI_WALK;
	//bloodstain->bmp = (char *) (data+3); // our image

}

bool walker::eat_me(walker  * eater)
{
	if (eater)
		Log("EATING A NON-TREASURE!\n");
	return 0;
}

void walker::set_direct_frame(short whichframe)
{
	frame = whichframe;

	const PixieData& data = myscreen->level_data.myloader->graphics[PIX(order, family)];
	bmp = data.data.get() + frame*size;

}

walker* walker::do_summon(char whatfamily, Sint32 summon_lifetime)
{
	if (whatfamily || summon_lifetime)
		Log("Should not be hitting walker::do_summon!\n");
	return nullptr;
}

bool walker::check_special()
{
	Log("Should not be hitting walker::check_special\n");
	return 0;
}

// Center us on target walker
void walker::center_on(walker  *target)
{
	short newx, newy;

	// First get the center of our target ..
	newx = target->xpos + target->sizex/2;
	newy = target->ypos + target->sizey/2;

	// Now adjust for our position ..
	newx -= sizex/2;
	newy -= sizey/2;

	// Now set our position ..
	setxy(newx, newy);
}

void walker::set_difficulty(Uint32 whatlevel)
{
	Uint32 temp, dif1;

	dif1 = difficulty_level[current_difficulty];

	switch (order)
	{
		case Order::Generator:
			temp = 100*whatlevel;
			temp = (temp * dif1) / 100;
			stats_->hitpoints = static_cast<float>(temp);
			break;
		default:  // adjust standard settings for the rest ..
			if (team_num != 0)  // do all EXCEPT player characters
			{
				const float dif = static_cast<float>(dif1);
				stats_->max_hitpoints = (stats_->max_hitpoints * dif) / 100.0f;
				stats_->max_magicpoints = (stats_->max_magicpoints * dif) / 100.0f;
				damage = (damage * dif) / 100.0f;
			}
			break;
	}

	return;
}

Sint32 walker::distance_to_ob(const walker  * target) const
{
	//Sint32 xdelta,ydelta;

	//xdelta = static_cast<Sint32>(target->xpos - xpos) +
	//         static_cast<Sint32>( (target->sizex - sizex) / 2 );
	//ydelta = static_cast<Sint32>(target->ypos - ypos) +
	//         static_cast<Sint32>( (target->sizey - sizey) / 2 );
	//return static_cast<Sint32>(xdelta*xdelta + ydelta*ydelta);
	return ( abs(target->xpos - xpos) + abs(target->ypos - ypos) );

}

Sint32 walker::distance_to_ob_center(const walker * target) const
{
	Sint32 xdelta,ydelta;

	xdelta = static_cast<Sint32>(target->xpos - xpos) +
	         static_cast<Sint32>( (target->sizex - sizex) / 2 );
	ydelta = static_cast<Sint32>(target->ypos - ypos) +
	         static_cast<Sint32>( (target->sizey - sizey) / 2 );
	return static_cast<Sint32>(xdelta*xdelta + ydelta*ydelta);
}

unsigned char walker::query_team_color() const
{
	// Debugging ..
	//if (foe && !foe->dead)
	return static_cast<unsigned char>(team_num*16+40);
	//else
	//  return static_cast<unsigned char>(7*16 + 40);
}

Sint32 walker::is_friendly(const walker *target) const
{
	// is_friendly determines if _target_ is "friendly"
	// towards this walker.
	//short allied_mode;
	short has_myguy;
	const walker *headguy;
	const walker *headus, *headtarget;

	// In case we're passed a null pointer somehow,
	// we're always unfriendly :)
	if (target == nullptr)
		return 0;
	// If either of us is dead, we're also unfriendly :)
	if (dead || target->dead)
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	headguy = this;
	while (headguy->owner && (headguy->owner->dead == 0) && (headguy->owner != headguy) )
		headguy = headguy->owner;
	headus = headguy;
	// Now our target ..
	headguy = target;
	while (headguy->owner && (headguy->owner->dead == 0) && (headguy->owner != headguy) )
		headguy = headguy->owner;
	headtarget = headguy;

	// First, get our allied setting from screen ..
	// 0 is "enemy," and non-zero is "friendly"
	//allied_mode = myscreen->allied_mode;

	// Now, if we or the target don't contain a "myguy" pointer,
	// then we don't care about allied_mode, and we'll
	// treat our state as always in 'enemy' mode
	if (headtarget->myguy == nullptr && headus->myguy == nullptr)
		has_myguy = 0;
    else if(headtarget->myguy == nullptr || headus->myguy == nullptr)
        has_myguy = 2;
	else
		has_myguy = 1;

	// Is allied mode set to zero (enemy)?
	// If so, then if our team numbers don't match,
	// we are not friendly
	if (myscreen->save_data.allied_mode == 0 || has_myguy == 0)
	{
		return (headus->team_num == headtarget->team_num);
	}
	
	// Allied
	if(has_myguy == 2)
    {
        // One person is missing a myguy pointer.
        // The one with a myguy pointer is owned by a player.
        // If the other person belongs to team 0 (red), then they are friendly.
        return (headtarget->myguy == nullptr && headtarget->team_num == 0) || (headus->myguy == nullptr && headus->team_num == 0);
    }

	// If we're in 'friendly' mode, then everyone with
	// a "myguy" pointer (a real, saved character)
	// is friendly to each other ..
	// By now we know that both us and the target have
	// myguy's, so we're friendly
	return 1;
}

Sint32 walker::is_friendly_to_team(unsigned char team) const
{
	// is_friendly_to_team determines if _team_ is "friendly"
	// towards this walker.
	//short allied_mode;
	short has_myguy;
	const walker *headguy;
	const walker *headus;
	
	// If dead, we're also unfriendly :)
	if (dead)
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	headguy = this;
	while (headguy->owner && (headguy->owner->dead == 0) && (headguy->owner != headguy) )
		headguy = headguy->owner;
	headus = headguy;
	
	// First, get our allied setting from screen ..
	// 0 is "enemy," and non-zero is "friendly"
	//allied_mode = myscreen->allied_mode;

	// Now, if we or the target don't contain a "myguy" pointer,
	// then we don't care about allied_mode, and we'll
	// treat our state as always in 'enemy' mode
	if (headus->myguy == nullptr)
		has_myguy = 0;
	else
		has_myguy = 1;

	// Is allied mode set to zero (enemy) or were we not hired (!myguy)?
	// If so, then our team number must match.
	if (myscreen->save_data.allied_mode == 0 || has_myguy == 0)
	{
		return (headus->team_num == team);
	}
	
	// If we're a hired guy in allied mode, then we're friendly with team 0 (red)
	return (has_myguy == 1 && team == 0);
}
