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

#include <openglad/gameplay/walker.h>
#include <openglad/interface/walker_render.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/level_runtime_data.h>

// ---------------------------------------------------------------------------
// WalkerRender stubs for headless mode.
// render_ is always nullptr in headless, but the linker still needs these
// symbols because shared code (e.g. treasure.cpp) references them.
// ---------------------------------------------------------------------------
struct WalkerRender::Impl {};
WalkerRender::WalkerRender(const PixieData&) {}
WalkerRender::~WalkerRender() = default;
const unsigned char* WalkerRender::bmp_data() const { return nullptr; }
void WalkerRender::set_frame(short) {}
void WalkerRender::set_data(const PixieData&) {}

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
	owning_world_ = nullptr;
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	collide_ob = nullptr;
	dead = 1;

	obmap* active = (current_game != nullptr && current_game->world != nullptr)
	    ? current_game->world->myobmap.get()
	    : nullptr;
	if (active != nullptr)
		active->remove(this);

	stats_.reset();
	render_.reset();
	clear_myguy();
	myself_ = nullptr;
}
