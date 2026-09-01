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
	set_sizex(data.w);
	set_sizey(data.h);
	frames = data.frames;
	set_frame_state(0);
}

void walker::set_data(const PixieData& data)
{
	set_sizex(data.w);
	set_sizey(data.h);
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
	set_frame_state(framenum);
	return 1;
}

void walker::set_direct_frame(short whichframe)
{
	set_frame_state(whichframe);
}

walker::~walker()
{
	set_foe(nullptr);
	set_leader(nullptr);
	set_owner(nullptr);
	set_collide_ob(nullptr);
	set_dead(1);

	GameWorld* const owning_world = owning_world_;
	obmap* active = nullptr;
	// Dormant walkers are invariantly absent from obmap (set_dormant owns the
	// removal). The detached weapon-profile probe reuses that invariant.
	if (!dormant())
	{
		active =
			(owning_world != nullptr) ? owning_world->myobmap.get() : nullptr;
		if (active == nullptr && current_game != nullptr &&
		    current_game->world != nullptr)
		{
			active = current_game->world->myobmap.get();
		}
	}
	if (active != nullptr)
		active->remove(this);
	owning_world_ = nullptr;

	stats_.reset();
	render_.reset();
	clear_myguy();
	myself_ = nullptr;
}
