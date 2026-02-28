/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Walker rendering functions, extracted from entity code into the render layer.
// These were originally walker::draw(), walker::draw_tile(), etc.

#include <openglad/render/walker_draw.h>
#include <openglad/legacy/base.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/pathfinding_grid.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include <openglad/core/stats.h>
#include <openglad/data/level_data.h>
#include <openglad/data/gparser.h>
#include <openglad/data/smooth.h>
#include <span>
#include <cmath>


static inline cfg_store& active_config()
{
    return cfg;
}

// ---- Helpers ----

static bool float_eq(float a, float b)
{
    return (a == b || (a - 0.000001f < b && a + 0.000001f > b));
}

static void draw_damage_number(walker::DamageNumber& dn, viewscreen* view_buf)
{
	const float xscreen_f = dn.x - static_cast<float>(view_buf->topx) + static_cast<float>(view_buf->xloc);
	const float yscreen_f = dn.y - static_cast<float>(view_buf->topy) + static_cast<float>(view_buf->yloc);
	const Sint32 xscreen = static_cast<Sint32>(xscreen_f);
	const Sint32 yscreen = static_cast<Sint32>(yscreen_f);

	Uint8 alpha = 0;
	if (dn.t >= 1.0f)
		alpha = 255;
	else if (dn.t > 0.0f)
		alpha = static_cast<Uint8>(dn.t * 255.0f);
	og::runtime::current_session->myscreen_->text_normal.write_xy_center_alpha(xscreen, yscreen, dn.color, alpha, "%.0f", dn.value);
}

// ---- Public API ----

void draw_small_health_bar(walker* w, viewscreen* view_buf)
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
                og::runtime::current_session->myscreen_->draw_box(r.x, r.y, r.x + last_w, r.y + r.h, static_cast<unsigned char>(53), 1);
            }

            const Sint32 cur_w = static_cast<Sint32>(width_f * ratio);
            og::runtime::current_session->myscreen_->draw_box(r.x, r.y, r.x + cur_w, r.y + r.h, whatcolor, 1);
            og::runtime::current_session->myscreen_->draw_box(r.x-1, r.y-1, r.x + max_w+1, r.y + r.h+1, BLACK, 0);
        }
    }
}

#define ATTACK_LUNGE_SIZE 5
#define HIT_RECOIL_SIZE 3

bool draw_walker(walker& w, viewscreen* view_buf)
{
    const bool show_attack_lunge = active_config().is_on("effects", "attack_lunge");
    const bool show_hit_recoil = active_config().is_on("effects", "hit_recoil");
    const bool show_hit_flash = active_config().is_on("effects", "hit_flash");
    const bool show_hit_anim = active_config().is_on("effects", "hit_anim");
    const bool show_damage_numbers = active_config().is_on("effects", "damage_numbers");
    const bool show_heal_numbers = active_config().is_on("effects", "heal_numbers");

    // Update the drawing coords from the real position
    w.xpos = static_cast<short>(w.worldx());
    w.ypos = static_cast<short>(w.worldy());

	Sint32 xscreen, yscreen;

	if (w.dead)
	{
		Log("drawing a dead guy!\n");
		return 0;
	}
	w.drawcycle++;

    if (!show_hit_anim && w.query_order() == Order::FX && w.family == FAMILY_HIT)
        return true;

	xscreen = static_cast<Sint32>(w.xpos - view_buf->topx + view_buf->xloc);
	yscreen = static_cast<Sint32>(w.ypos - view_buf->topy + view_buf->yloc);

	if(show_attack_lunge && w.attack_lunge > 0.0f)
	    {
	        const float dx = w.attack_lunge * ATTACK_LUNGE_SIZE * cosf(w.attack_lunge_angle);
	        const float dy = w.attack_lunge * ATTACK_LUNGE_SIZE * sinf(w.attack_lunge_angle);
	        xscreen += static_cast<Sint32>(dx);
	        yscreen += static_cast<Sint32>(dy);
	    }

	if(show_hit_recoil && w.hit_recoil > 0.0f)
	    {
	        const float dx = w.hit_recoil * HIT_RECOIL_SIZE * cosf(w.hit_recoil_angle);
	        const float dy = w.hit_recoil * HIT_RECOIL_SIZE * sinf(w.hit_recoil_angle);
	        xscreen += static_cast<Sint32>(dx);
	        yscreen += static_cast<Sint32>(dy);
	    }

	w.compute_outline(view_buf->control);

	bool should_draw_hp = true;
    int fill_mode = 0;
    int outline_style = 0;
    int invisibility_amount = 0;
    int phantom_mode = 0;

	if (w.stats()->query_bit_flags(BIT_PHANTOM)) //WE ARE A PHANTOM
    {
        fill_mode = PHANTOM_MODE;
        phantom_mode = SHIFT_RANDOM;
        should_draw_hp = false;
    }
	else if (w.invisibility_left && view_buf->control != nullptr)  //WE ARE INVISIBLE
	{
		if (w.team_num == view_buf->control->team_num)
        {
            fill_mode = INVISIBLE_MODE;
            invisibility_amount = ( w.invisibility_left + 10 );
            outline_style = w.outline;
            should_draw_hp = false;
        }
	}
	else if (w.stats()->query_bit_flags(BIT_FORESTWALK) &&
	         og::runtime::current_session->myscreen_->level_data.world().mysmoother.query_genre_x_y(w.xpos/GRID_SIZE, w.ypos/GRID_SIZE) == TYPE_TREES
	         && !w.stats()->query_bit_flags(BIT_FLYING)
	         && (w.flight_left < 1) )
    {
        fill_mode = INVISIBLE_MODE;
        invisibility_amount = 1000;
        outline_style = 1;
        should_draw_hp = false;
    }
	else if (w.outline)    // WE HAVE SOME OUTLINE
	{
	    fill_mode = OUTLINE_MODE;
	    outline_style = w.outline;
	}

	// Draw me
	if(show_hit_flash && w.hurt_flash)
    {
        w.hurt_flash = false;

        auto bmp_span = std::span<const unsigned char>{w.bmp_data(), static_cast<size_t>(w.sizex * w.sizey)};
        og::runtime::current_session->myscreen_->walkputbuffer_flash(xscreen, yscreen, w.sizex, w.sizey,
                                   view_buf->xloc, view_buf->yloc,
                                   view_buf->endx, view_buf->endy,
                                   bmp_span, w.query_team_color());
    }
    else
    {
        auto bmp_span = std::span<const unsigned char>{w.bmp_data(), static_cast<size_t>(w.sizex * w.sizey)};
        if(fill_mode == 0 && outline_style == 0)
        {
            og::runtime::current_session->myscreen_->walkputbuffer(xscreen, yscreen, w.sizex, w.sizey,
                                   view_buf->xloc, view_buf->yloc,
                                   view_buf->endx, view_buf->endy,
                                   bmp_span, w.query_team_color());
        }
        else
        {
	            og::runtime::current_session->myscreen_->walkputbuffer( xscreen, yscreen, w.sizex, w.sizey,
	                                    view_buf->xloc, view_buf->yloc,
	                                    view_buf->endx, view_buf->endy,
	                                    bmp_span, w.query_team_color(),
	                                    static_cast<unsigned char>(fill_mode), //mode
	                                    invisibility_amount, //invisibility
	                                    static_cast<unsigned char>(outline_style), //outline
	                                    static_cast<unsigned char>(phantom_mode)); //type of phantom
	        }
	    }

	if(should_draw_hp)
        draw_small_health_bar(&w, view_buf);

    if (show_damage_numbers || show_heal_numbers)
    {
	    for(auto e = w.damage_numbers.begin(); e != w.damage_numbers.end();)
        {
            const bool is_heal = (e->color == 56);
            const bool should_render = is_heal ? show_heal_numbers : show_damage_numbers;
            if (!should_render)
            {
                ++e;
                continue;
            }

            e->t -= 0.05f;
            if(e->t < 0)
            {
                e = w.damage_numbers.erase(e);
                continue;
            }

            e->y -= 1.5f;
            if(view_buf->control == &w)
                draw_damage_number(*e, view_buf);
            e++;
        }
    }

	if(og::runtime::current_session->debug_draw_paths_)
        draw_walker_path(w, view_buf);
	return 1;
}

bool draw_walker_tile(walker& w, viewscreen* view_buf)
{
    if (!active_config().is_on("effects", "hit_anim") &&
        w.query_order() == Order::FX && w.family == FAMILY_HIT)
    {
        return true;
    }

	Sint32 xscreen, yscreen;

	if (w.dead)
	{
		Log("drawing a dead guy!\n");
		return 0;
	}
	w.drawcycle++;

	xscreen = static_cast<Sint32>(w.xpos - view_buf->topx + view_buf->xloc);
	yscreen = static_cast<Sint32>(w.ypos - view_buf->topy + view_buf->yloc);

	w.compute_outline(view_buf->control);

	auto bmp_span = std::span<const unsigned char>{w.bmp_data(), static_cast<size_t>(w.sizex * w.sizey)};

	if (w.stats()->query_bit_flags(BIT_PHANTOM)) //WE ARE A PHANTOM
		og::runtime::current_session->myscreen_->walkputbuffer( xscreen, yscreen, w.sizex, w.sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, w.query_team_color(),
		                        PHANTOM_MODE, //mode
		                        0, //invisibility
		                        0, //outline
		                        SHIFT_RANDOM); //type of phantom

	else if (w.invisibility_left)  //WE ARE INVISIBLE
	{
		if (w.team_num == view_buf->control->team_num)
			og::runtime::current_session->myscreen_->walkputbuffer( xscreen, yscreen, w.sizex, w.sizey,
			                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
			                        bmp_span, w.query_team_color(),
			                        INVISIBLE_MODE,  //mode
			                        ( w.invisibility_left + 10 ), //invisibility
			                        w.outline,  //outline
			                        0 ); //type of phantom
	}
	else if (w.stats()->query_bit_flags(BIT_FORESTWALK) &&
	         og::runtime::current_session->myscreen_->level_data.world().mysmoother.query_genre_x_y(w.xpos/GRID_SIZE, w.ypos/GRID_SIZE) == TYPE_TREES
	         && !w.stats()->query_bit_flags(BIT_FLYING)
	         && (w.flight_left < 1) )
		og::runtime::current_session->myscreen_->walkputbuffer( xscreen, yscreen, w.sizex, w.sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, w.query_team_color(),
		                        INVISIBLE_MODE,  //mode
		                        1000, //invisibility
		                        1,  //outline
		                        0 ); //type of phantom

	else if (w.outline)    // WE HAVE SOME OUTLINE
	{
		og::runtime::current_session->myscreen_->walkputbuffer( xscreen, yscreen, w.sizex, w.sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, w.query_team_color(),
		                        OUTLINE_MODE, //mode
		                        0, //invisibility
		                        w.outline, //outline
		                        0 ); //type of phantom

        draw_small_health_bar(&w, view_buf);
	}
	else
	{
		og::runtime::current_session->myscreen_->walkputbuffer(xscreen, yscreen, w.sizex, w.sizey,
		                       view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                       bmp_span, w.query_team_color());

        draw_small_health_bar(&w, view_buf);
	}

	return 1;
}

void draw_walker_path(walker& w, viewscreen* view_buf)
{
    if (w.path_to_foe.size() == 0)
        return;

    const unsigned char mycolor = static_cast<unsigned char>(w.query_team_color() + static_cast<unsigned char>(reinterpret_cast<intptr_t>(&w) % 5));

    const Sint32 offsetx = view_buf->topx - view_buf->xloc - 8;
    const Sint32 offsety = view_buf->topy - view_buf->yloc - 8;

    std::vector<MicroPatherState>::iterator e = w.path_to_foe.begin();
    int px = GET_STATE_X(*e) - offsetx;
    int py = GET_STATE_Y(*e) - offsety;
    while (e != w.path_to_foe.end())
    {
        int x1 = GET_STATE_X(*e) - offsetx;
        int y1 = GET_STATE_Y(*e) - offsety;

        og::runtime::current_session->myscreen_->draw_line(px, py, x1, y1, mycolor);
        og::runtime::current_session->myscreen_->fastbox_outline(x1 - 1, y1 - 1, 2, 2, mycolor);
        e++;
        px = x1;
        py = y1;
    }
}
