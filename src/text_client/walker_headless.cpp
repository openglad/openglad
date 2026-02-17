/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Headless definitions for walker member functions that are defined
// in walker_render_bridge.cpp for SDL builds. These skip all render
// component creation while still maintaining sim-level state fields.

#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/obmap.h>
#include <openglad/core/stats.h>
#include <openglad/data/level_data.h>

void walker::attach_render(const PixieData& data)
{
	// No render component in headless mode; just sync sim fields.
	sizex = data.w;
	sizey = data.h;
	frames = data.frames;
	frame = 0;
}

void walker::set_data(const PixieData& data)
{
	sizex = data.w;
	sizey = data.h;
	frames = data.frames;
}

const unsigned char* walker::bmp_data() const
{
	return nullptr;
}

short walker::set_frame(short framenum)
{
	if (framenum < 0 || framenum >= frames)
		return 0;
	frame = framenum;
	return 1;
}

void walker::set_direct_frame(short whichframe)
{
	frame = whichframe;
}

walker::~walker()
{
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	collide_ob = nullptr;
	dead = 1;

	obmap* active = (sim_level != nullptr) ? sim_level->myobmap.get() : nullptr;
	if (active != nullptr)
		active->remove(this);
	if (myobmap != nullptr && myobmap != active)
		myobmap->remove(this);

	stats_.reset();
	render_.reset();
	clear_myguy();
	myself_ = nullptr;
}
