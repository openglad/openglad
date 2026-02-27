/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Walker member functions that require the full pixieN definition.
// Separated from src/entities/walker.cpp so that the entities module
// does not depend on the render module (pixien.h).

#include <openglad/entities/walker.h>
#include <openglad/entities/walker_render.h>
#include <openglad/entities/guy.h>
#include <openglad/render/pixien.h>
#include <openglad/entities/obmap.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/data/level_data.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

// WalkerRender PIMPL implementation (backed by pixieN sprite)
struct WalkerRender::Impl {
	std::unique_ptr<pixieN> pix;
	explicit Impl(const PixieData& data) : pix(std::make_unique<pixieN>(data)) {}
};

WalkerRender::WalkerRender(const PixieData& data)
	: impl_(std::make_unique<Impl>(data)) {}

WalkerRender::~WalkerRender() = default;

const unsigned char* WalkerRender::bmp_data() const { return impl_->pix->bmp_data(); }
void WalkerRender::set_frame(short framenum) { impl_->pix->set_frame(framenum); }
void WalkerRender::set_data(const PixieData& data) { impl_->pix->set_data(data); }

void walker::attach_render(const PixieData& data)
{
	render_ = std::make_unique<WalkerRender>(data);
	// Sync size from PixieData into SimEntity fields
	sizex = data.w;
	sizey = data.h;
	frames = data.frames;
	frame = 0;
}

void walker::set_data(const PixieData& data)
{
	// Update render graphics and sync sim-level size/frame fields
	sizex = data.w;
	sizey = data.h;
	frames = data.frames;
	if (render_)
		render_->set_data(data);
}

const unsigned char* walker::bmp_data() const
{
	return render_ ? render_->bmp_data() : nullptr;
}

short walker::set_frame(short framenum)
{
	if (framenum < 0 || framenum >= frames)
		return 0;
	frame = framenum;
	if (render_)
		render_->set_frame(framenum);
	return 1;
}

void walker::set_direct_frame(short whichframe)
{
	frame = whichframe;

	// Update render component's bmp pointer if available
	if (render_)
		render_->set_frame(whichframe);
}

walker::~walker()
{
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	collide_ob = nullptr;
	dead = 1;

	// Walkers can outlive a particular GameWorld::myobmap instance in tests
	// (screen cleanup replaces the obmap). Ensure we remove from the current
	// active obmap if present.
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

std::unique_ptr<pixieN> loader::create_pixieN_owned(Order order, std::int32_t family)
{
	if (!graphics[PIX(order, family)].valid())
	{
		Log("Alert! No valid graphics for pixieN\n");
		return nullptr;
	}

	return std::make_unique<pixieN>(graphics[PIX(order, family)]);
}
