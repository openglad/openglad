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
#include <openglad/data/level_data.h>

// Concrete IWalkerRender backed by a pixieN sprite.
class PixieNWalkerRender final : public IWalkerRender {
public:
	explicit PixieNWalkerRender(const PixieData& data)
		: pix_(std::make_unique<pixieN>(data)) {}

	const unsigned char* bmp_data() const override { return pix_->bmp_data(); }
	void set_frame(short framenum) override { pix_->set_frame(framenum); }
	void set_data(const PixieData& data) override { pix_->set_data(data); }

	pixieN* pixie() { return pix_.get(); }
	const pixieN* pixie() const { return pix_.get(); }

private:
	std::unique_ptr<pixieN> pix_;
};

void walker::attach_render(const PixieData& data)
{
	render_ = std::make_unique<PixieNWalkerRender>(data);
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

	// Walkers can outlive a particular LevelData::myobmap instance in tests
	// (screen cleanup replaces the obmap). Ensure we remove from the current
	// active obmap as well as the one we were last bound to.
	obmap* active = (sim_level != nullptr) ? sim_level->myobmap.get() : nullptr;
	if (active != nullptr)
		active->remove(this);
	if (myobmap != nullptr && myobmap != active)
		myobmap->remove(this); // remove ourselves from obmap lists

	stats_.reset();
	render_.reset();
	clear_myguy();
	myself_ = nullptr;
}
