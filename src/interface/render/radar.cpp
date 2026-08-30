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
//radar.cpp

/* ChangeLog
	buffers: 7/31/02: *include cleanup
*/
#include <openglad/core/colors.h>
#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/family_presentation.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <algorithm>
#include <array>
#include <span>
#include <openglad/interface/game_context.h>
static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

namespace
{
// #209 radar_ping: pinged families draw an oversized blip pulsing on this
// RENDER-side counter — bumped once per radar draw, never fed by the game
// rng, so the sim stream is untouched (parity-inert by construction).
unsigned g_radar_ping_phase = 0;

// The declared radar blip for any (order, family), or nullptr when the
// family has no descriptor. Existing orders retain their draw-site colour
// rules; descriptor-declared FX landmarks use the full record because they
// have no legacy per-order colour rule.
const og::RadarBlip* radar_blip_for(Order order, int family)
{
    switch (order)
    {
    case Order::Living:
        if (const auto* d = get_family_descriptor(family))
            return &d->radar;
        return nullptr;
    case Order::Weapon:
        if (const auto* d = get_weapon_family_descriptor(family))
            return &d->radar;
        return nullptr;
    case Order::Treasure:
        if (const auto* d = get_treasure_family_descriptor(family))
            return &d->radar;
        return nullptr;
    case Order::Generator:
        if (const auto* d = get_generator_family_descriptor(family))
            return &d->radar;
        return nullptr;
    case Order::FX:
        if (const auto* d = get_effect_family_descriptor(family))
            return &d->radar;
        return nullptr;
    default:
        return nullptr;
    }
}

// Oversized pulsing blip: a plus that swells to a 3x3 block on alternate
// pulse phases, brightness walking the palette band the way the jitter
// flicker does — but from the render counter, not rng. Every pixel is
// clamped to the radar box so the pulse never paints outside it.
void plot_ping_blip(short cx, short cy, unsigned char base_color,
                    unsigned char alpha, short xloc, short yloc,
                    short xview, short yview)
{
    const unsigned phase = g_radar_ping_phase;
    const unsigned char color =
        static_cast<unsigned char>(base_color + ((phase >> 2) & 3u));
    const bool swollen = ((phase >> 3) & 1u) != 0;
    for (short dy = -1; dy <= 1; ++dy)
    {
        for (short dx = -1; dx <= 1; ++dx)
        {
            const bool corner = dx != 0 && dy != 0;
            if (corner && !swollen)
                continue;
            const short px = static_cast<short>(cx + dx);
            const short py = static_cast<short>(cy + dy);
            if (px < xloc || px >= xloc + xview || py < yloc ||
                py >= yloc + yview)
                continue;
            og::runtime::current_session->myscreen_->pointb(px, py, color,
                                                            alpha);
        }
    }
}

template <typename WalkerList>
bool contains_walker_ptr(const WalkerList& list, const walker* candidate)
{
    return std::any_of(list.begin(), list.end(),
                       [candidate](const auto& entry) {
                           return entry.get() == candidate;
                       });
}

bool control_pointer_is_live(LevelRuntimeData& level, const walker* candidate)
{
    if (candidate == nullptr)
        return false;

    return contains_walker_ptr(level.world().oblist, candidate)
        || contains_walker_ptr(level.world().fxlist, candidate)
        || contains_walker_ptr(level.world().weaplist, candidate)
        || contains_walker_ptr(level.world().dead_list, candidate);
}

walker* sanitize_radar_control(viewscreen* view, LevelRuntimeData& level)
{
    if (view == nullptr)
        return nullptr;

    walker* candidate = view->control;
    if (candidate != nullptr && !control_pointer_is_live(level, candidate))
        view->control = nullptr;
    return view->control;
}

// Which floor's terrain (and blips) the radar should show: the level
// editor's floor override when active, else the control walker's floor.
// Single-floor levels always report 0, so the legacy radar path is
// untouched (pixel-identical).
short radar_terrain_floor(viewscreen* view, walker* control,
                          LevelRuntimeData& level)
{
    const int floors = level.world().floor_count();
    if (floors <= 1)
        return 0;
    int f = 0;
    if (view != nullptr && view->editor_floor_override_ >= 0)
        f = static_cast<int>(view->editor_floor_override_);
    else if (control != nullptr)
        f = static_cast<int>(control->floor());
    if (f < 0)
        f = 0;
    if (f >= floors)
        f = floors - 1;
    return static_cast<short>(f);
}
} // namespace

// ************************************************************
//  RADAR -- It's nothing like pixie, it just looks like it
// ************************************************************
/*
  radar(char,short,short,screen)    - initializes the radar data (pix = char)
  short draw()
  short on_screen()
*/


// Radar -- this initializes the graphics data for the radar,
// as well as its graphics x and y size.  In addition, it informs
// the radar of the screen object it is linked to.
radar::radar(viewscreen * myview, screen * screen_ctx, short whatnum)
{
	screenp = screen_ctx;
	viewscreenp = myview;
	mynum = whatnum; //what number viewscreen we are, to get control's position
    force_lower_position = false;
}

void radar::start()
{
    start(&og::runtime::current_session->myscreen_->level_runtime_data());
}

void radar::start(LevelRuntimeData* data)
{
	sync_to_grid(data);
	update(data);

}

// Derive the radar extents, view clamps, on-screen placement, and bmp
// allocation from the live grid. start() runs this unconditionally; update()
// re-runs it whenever the recorded dimensions disagree with the grid (the
// level editor resizes the grid out from under the radar, and the editor's
// radar can receive update() before any start()), so the bmp writes in
// update() always match the allocation.
void radar::sync_to_grid(LevelRuntimeData* data)
{
	sizex = static_cast<short>(data->world().grid.w);
	sizey = static_cast<short>(data->world().grid.h);
	size = static_cast<unsigned short>((static_cast<unsigned short>(sizex))*(static_cast<unsigned short>(sizey)));
	const auto [block_w, block_h] = radar_block_extents(sizex, sizey);
	xview = static_cast<short>(block_w);
	yview = static_cast<short>(block_h);
	radarx = 0;
	radary = 0;

	sync_position_to_view();
    bmp.resize(size);
}

std::pair<int, int> radar_block_extents(int grid_w, int grid_h)
{
	int w = RADAR_X;
	int h = RADAR_Y;

	if (w > grid_w)
		w = grid_w;
	if (h > grid_h)
		h = grid_h;
	return {w, h};
}

RadarBlock radar_block_for_pane(int pane_yloc, int pane_endx, int pane_endy,
                                int w, int h, bool force_lower)
{
	RadarBlock block;
	block.w = w;
	block.h = h;
        #ifdef REDUCE_OVERSCAN
        block.margin = 8;
        #else
        block.margin = 4;
        #endif
        block.x = (pane_endx - w) - block.margin;
        #ifdef USE_TOUCH_INPUT
        if(force_lower)  // used by level editor to place minimap
        {
            // At bottom
            block.y = (pane_endy - h) - block.margin;
        }
        else
        {
            // At top
            block.y = pane_yloc + block.margin;
        }
        #else
            (void)force_lower;
            (void)pane_yloc;
            // At bottom
            block.y = (pane_endy - h) - block.margin;
        #endif
	return block;
}

void radar::sync_position_to_view()
{
	if(viewscreenp)
	{
		const RadarBlock block = radar_block_for_pane(
		    static_cast<int>(viewscreenp->yloc),
		    static_cast<int>(viewscreenp->endx),
		    static_cast<int>(viewscreenp->endy),
		    static_cast<int>(xview), static_cast<int>(yview),
		    force_lower_position);
		xloc = static_cast<short>(block.x);
		yloc = static_cast<short>(block.y);
	}
}


// Destruct the radar and its variables
radar::~radar() = default;

short radar::draw()
{
    return draw(&og::runtime::current_session->myscreen_->level_runtime_data());
}

short radar::draw(LevelRuntimeData* data)
{
	Sint32 tempx, tempy, tempz;
	unsigned char tempcolor;
	Order oborder{};
	short obfamily, obteam;
	short can_see = 0, do_show = 0;
	Sint32 listtype = 0;
	std::array<bool, og::sim::kModeBeacons> beacon_target_drawn{};

	// #209: advance the ping pulse (render-side; see plot_ping_blip).
	++g_radar_ping_phase;

	radarx = 0;
	radary = 0;
	sync_position_to_view();

	if (viewscreenp && !viewscreenp->radarstart)
	{
		start(data);
		viewscreenp->radarstart = 1;
	}

    walker* control = sanitize_radar_control(viewscreenp, *data);
	if (control)
	{
		radarx = static_cast<short>(control->xpos()/GRID_SIZE - xview/2);
		radary = static_cast<short>(control->ypos()/GRID_SIZE - yview/2);
		if (control->view_all() > 0)
			can_see = 1;
		obteam = control->team_num();
	}
	else
	{
		radarx = static_cast<short>(data->level_visuals().topx / GRID_SIZE - xview / 2);
		radary = static_cast<short>(data->level_visuals().topy / GRID_SIZE - yview / 2);
		obteam = 0;
	}
	if (radarx > (sizex - xview))
		radarx = static_cast<short>(sizex - xview);
	if (radary > (sizey - yview))
		radary = static_cast<short>(sizey - yview);
	if (radarx < 0)
		radarx = 0;
	if (radary < 0)
		radary = 0;

	// Multi-floor: the minimap tracks the floor being played (or edited).
	// Re-bake the terrain bmp only when that floor changes — update() is
	// slow — and never on single-floor levels (radar_terrain_floor pins 0).
	const short terrain_floor = radar_terrain_floor(viewscreenp, control, *data);
	if (terrain_floor != bmp_floor_)
	{
		bmp_floor_ = terrain_floor;
		update(data);
	}
    
    unsigned char alpha = 255;
    // Deliberately keyed on numviews (humans), never on the camera-inclusive
    // layout_pane_count() — docs/camera-views-design.md constraint 7 (a
    // docked camera must not fade the seat radars).
    if(og::runtime::current_session->myscreen_->numviews > 2 && !(og::runtime::current_session->myscreen_->numviews == 3 && mynum == 0))
    {
        alpha = 127;
    }
    TRACE("radar", "alpha view=%d a=%d", mynum, static_cast<int>(alpha));
	{
		size_t offset = static_cast<size_t>(radarx + (radary * sizex));
		auto radar_span = std::span<const unsigned char>{bmp.data() + offset, bmp.size() - offset};
		og::runtime::current_session->myscreen_->putbuffer_alpha(xloc, yloc,
		                   sizex,sizey,
		                   xloc,yloc,xloc + xview,yloc + yview,
		                   radar_span, alpha);
	}

	// Now determine what objects are visible on the radar ..
	while (listtype <= 1)
	{
			const GameWorld::EntityList* ls;
			if (listtype == 0) // do oblist, standard
			{
				ls = &data->world().oblist;
				listtype++;
			}
			else if (listtype == 1) // do weapons
			{
				ls = &data->world().weaplist;
				listtype++;
			}
		else
			continue;

        for(auto e = ls->begin(); e != ls->end(); e++)
		{
		    walker* ob = e->get();
		    // Delayed spawns: dormant walkers are not in the world yet.
		    if (ob && ob->dormant())
		        continue;
		    // Multi-floor: only blip entities on the floor the radar shows
		    // (the terrain floor baked just above).
		    if (ob && data->world().floor_count() > 1 &&
		        static_cast<int>(ob->floor()) != static_cast<int>(bmp_floor_))
		        continue;
            oborder = ob->query_order();
			const og::RadarBlip* fam_blip =
				radar_blip_for(oborder, ob->family());
			const bool fam_blip_has_color =
				fam_blip != nullptr &&
				(fam_blip->color > 0 ||
				 fam_blip->color == og::kRadarColorTeam);
			const bool fx_landmark =
				oborder == Order::FX && !ob->dead() && fam_blip_has_color &&
				fam_blip->landmark;
			do_show = 0; // don't show, by default
			// Treasures normally live in fxlist (blipped from their
			// descriptors further down). The two families below are the ones
			// levels park in the oblist, and they blip a FIXED COLOR_FIRE
			// here rather than their descriptor colour — an "is this entity
			// worth a blip in the object list" rule with no colour to read,
			// so there is nothing on RadarBlip to drive it with. Widening it
			// to every oblist treasure would light up loot the player has not
			// earned treasure sight for.
			const bool object_blip =
				oborder == Order::Living || oborder == Order::Weapon
			            || (oborder == Order::Treasure && (ob->family() == FAMILY_LIFE_GEM))
			            || (oborder == Order::Treasure && (ob->family() == FAMILY_EXIT))
			            || (oborder == Order::Generator && can_see)
			            || fx_landmark;
			const bool visible_to_control =
				obteam == ob->team_num() || ob->invisibility_left() < 1 || can_see;
			if (object_blip
			        // A declared landmark is public navigation information, just
			        // like a treasure landmark below. Invisibility must not hide
			        // a mode objective from an opposing radar.
			        && (fx_landmark || visible_to_control)
			        && on_screen( static_cast<short>((ob->xpos()+1)/GRID_SIZE), static_cast<short>((ob->ypos()+1)/GRID_SIZE), radarx, radary)
			   )
				do_show = 1;
			if (do_show)
			{
				tempx = xloc + ((ob->xpos()+1)/GRID_SIZE - radarx);
				tempy = yloc + ((ob->ypos()+1)/GRID_SIZE - radary);
				if ( (tempx < xloc) || (tempx > (xloc + xview)) )
				{} //do nothing
				else if ( (tempy < yloc) || (tempy > (yloc + yview)) )
				{} //also do nothing
				else
				{
					// Offset sanity check against the active canvas area
					// (the legacy 320/64000 constants on a 320x200 canvas).
					tempz = (tempx+(tempy*og::runtime::current_session->myscreen_->canvas_w())); //this may need fixing
					if (tempz > og::runtime::current_session->myscreen_->canvas_w() * og::runtime::current_session->myscreen_->canvas_h() || tempz < 0)
					{
						Log("bad radar, bad\n");
						return 1;
					}
					tempcolor = (ob->query_team_color());
					if (fx_landmark)
					{
						const Uint32 base =
							fam_blip->color == og::kRadarColorTeam
								? static_cast<Uint32>(tempcolor)
								: static_cast<Uint32>(fam_blip->color);
						tempcolor = static_cast<unsigned char>(
							fam_blip->jitter > 0
								? base + rng(static_cast<Uint32>(fam_blip->jitter))
								: base);
					}
					if (viewscreenp && viewscreenp->control == ob)
					{
						tempcolor = static_cast<unsigned char>(rng(256));
						if (tempx >= (xloc + xview - 1) && tempy < (yloc+yview) )
						{
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy+1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy+1,tempcolor, alpha);

						}
						else if (tempx >= (xloc + xview -1) )
						{
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy-1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy-1,tempcolor, alpha);

						}
						else if (tempy >= (yloc + yview -1) && tempx < (xloc+xview) )
						{
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy-1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy-1,tempcolor, alpha);
						}
						else
						{
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy+1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy+1,tempcolor, alpha);
						}
					}
					else if (fam_blip != nullptr && fam_blip->ping)
					{
						// #209: pinged families draw LOUD — an oversized
						// pulsing blip in the colour the plain path would
						// have used for this order.
						unsigned char ping_base;
						if (oborder == Order::Living)
							ping_base = tempcolor;
						else if (oborder == Order::Generator)
							ping_base = static_cast<unsigned char>(tempcolor+1);
						else if (oborder == Order::Treasure)
							ping_base = COLOR_FIRE;
						else if (fx_landmark)
							ping_base = tempcolor;
						else
							ping_base = COLOR_WHITE;
						plot_ping_blip(static_cast<short>(tempx),
						               static_cast<short>(tempy), ping_base,
						               alpha, xloc, yloc, xview, yview);
						TRACE("radar", "ping_blip fam=%d",
						      static_cast<int>(ob->family()));
					}
					else if (oborder == Order::Living)
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
					else if (oborder == Order::Generator)
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,static_cast<unsigned char>(tempcolor+1), alpha);
					else if (oborder == Order::Treasure) // currently life gems
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,COLOR_FIRE, alpha);
					else if (fx_landmark)
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
					else
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,COLOR_WHITE, alpha);

					// A scripted beacon may name this exact landmark too (soccer's
					// ball does). Remember every matching slot only after the blip
					// was actually painted, so the beacon pass cannot overwrite the
					// descriptor pulse with a second centre dot.
					if (fx_landmark)
					{
						for (std::size_t slot = 0;
						     slot < beacon_target_drawn.size(); ++slot)
						{
							if (data->world().mode.beacons[slot].entity_id ==
							    static_cast<std::int32_t>(ob->entity_id()))
								beacon_target_drawn[slot] = true;
						}
					}
				}//draw the blob onto the radar
			}
		}
	} // go back to new screen lists (weapons, etc.)

    for (auto& uptr : data->world().fxlist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead() && !ob->dormant())
		{
			if (data->world().floor_count() > 1 &&
			    static_cast<int>(ob->floor()) != static_cast<int>(bmp_floor_))
				continue;
			oborder  = ob->query_order();
			obfamily = ob->family();
			const og::RadarBlip* fam_blip =
				radar_blip_for(oborder, obfamily);
			const bool fx_landmark =
				oborder == Order::FX && fam_blip != nullptr &&
				fam_blip->landmark &&
				(fam_blip->color > 0 ||
				 fam_blip->color == og::kRadarColorTeam);

			do_show = 0; // don't show, by default
			if (oborder == Order::Treasure)
			{
				// Blip colour and flicker span come from the family
				// descriptor (og::RadarBlip), so a class pack's treasure
				// lights up the minimap with no engine change. Two rules the
				// legacy switch encoded and the draw path must keep:
				//   * jitter == 0 means "make NO rng call" — this render path
				//     draws from the game rng, so its call count is part of
				//     the stream and may not move.
				//   * a family with no colour draws nothing (and rolls
				//     nothing).
				const TreasureFamilyDescriptor* td =
					get_treasure_family_descriptor(obfamily);
				const og::RadarBlip blip = td ? td->radar : og::RadarBlip{};
				const bool has_blip =
					blip.color > 0 || blip.color == og::kRadarColorTeam;
				// WHICH treasures ignore view_all: navigation markers and
				// mode objectives are landmarks every player can see;
				// everything else is loot that needs treasure sight. Pack
				// families declare this via `radar_landmark`; the two core
				// navigation families keep their hardcoded entries (their
				// descriptors declare no landmark flag, and classic
				// behavior must stay identical).
				const bool ignores_view_all =
					obfamily == FAMILY_EXIT || obfamily == FAMILY_TELEPORTER
					|| blip.landmark;
				if (has_blip && blip.landmark && !can_see)
				{
					TRACE("radar", "landmark_blip fam=%d",
					      static_cast<int>(obfamily));
				}
				if (has_blip && (can_see || ignores_view_all))
				{
					const Uint32 base =
						(blip.color == og::kRadarColorTeam)
							? static_cast<Uint32>(ob->query_team_color())
							: static_cast<Uint32>(blip.color);
					do_show = static_cast<short>(
						blip.jitter > 0
							? base + rng(static_cast<Uint32>(blip.jitter))
							: base);
				}
			}
			else if (fx_landmark)
			{
				TRACE("radar", "landmark_blip fam=%d",
				      static_cast<int>(obfamily));
				const Uint32 base =
					fam_blip->color == og::kRadarColorTeam
						? static_cast<Uint32>(ob->query_team_color())
						: static_cast<Uint32>(fam_blip->color);
				do_show = static_cast<short>(
					fam_blip->jitter > 0
						? base + rng(static_cast<Uint32>(fam_blip->jitter))
						: base);
			}
			if (!on_screen( static_cast<short>((ob->xpos()+1)/GRID_SIZE),
			                static_cast<short>((ob->ypos()+1)/GRID_SIZE),
			                radarx, radary) )
				do_show = 0;
			if (do_show)
			{
				tempx = xloc + ((ob->xpos()+1)/GRID_SIZE - radarx);
				tempy = yloc + ((ob->ypos()+1)/GRID_SIZE - radary);
				if ( (tempx < xloc) || (tempx > (xloc + xview)) )
				{} //do nothing
				else if ( (tempy < yloc) || (tempy > (yloc + yview)) )
				{} //also do nothing
				else
				{
					// Offset sanity check against the active canvas area
					// (the legacy 320/64000 constants on a 320x200 canvas).
					tempz = (tempx+(tempy*og::runtime::current_session->myscreen_->canvas_w())); //this may need fixing
					if (tempz > og::runtime::current_session->myscreen_->canvas_w() * og::runtime::current_session->myscreen_->canvas_h() || tempz < 0)
					{
						Log("bad radar, bad\n");
						return 1;
					}
					if (fam_blip != nullptr && fam_blip->ping)
					{
						// #209: oversized pulsing blip in the descriptor
						// colour do_show already resolved (any jitter roll
						// above already happened — the rng call count is
						// unchanged by the ping).
						plot_ping_blip(static_cast<short>(tempx),
						               static_cast<short>(tempy),
						               static_cast<unsigned char>(do_show),
						               alpha, xloc, yloc, xview, yview);
						TRACE("radar", "ping_blip fam=%d",
						      static_cast<int>(obfamily));
					}
					else
					{
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,static_cast<unsigned char>(do_show), alpha);
					}
					if (fx_landmark)
					{
						for (std::size_t slot = 0;
						     slot < beacon_target_drawn.size(); ++slot)
						{
							if (data->world().mode.beacons[slot].entity_id ==
							    static_cast<std::int32_t>(ob->entity_id()))
								beacon_target_drawn[slot] = true;
						}
					}
					}//draw the blob onto the radar
			} // end of valid do_show
		}  // end of if here->ob
	} // end of while (here)

	// Scripted-mode beacon blips (og.set_beacon): unconditional landmarks
	// resolved through the replicated entity id. Legacy beacon targets keep
	// their team-coloured point; descriptor-declared FX landmarks use their
	// own colour and pulse. Jitter 0 must make NO game-rng call.
	{
		const GameWorld& beacon_world = data->world();
		if ((beacon_world.type & GameWorld::TYPE_SCRIPTED) &&
		    beacon_world.mode.active)
		{
			for (std::size_t slot = 0; slot < beacon_world.mode.beacons.size();
			     ++slot)
			{
				const og::sim::ModeBeacon& beacon =
					beacon_world.mode.beacons[slot];
				if (beacon.entity_id == 0)
					continue;
				if (beacon_target_drawn[slot])
					continue;
				const walker* target = beacon_world.find_by_id(
					static_cast<std::uint32_t>(beacon.entity_id));
				if (target == nullptr || target->dead() || target->dormant())
					continue;
				if (beacon_world.floor_count() > 1 &&
				    static_cast<int>(target->floor()) !=
				        static_cast<int>(bmp_floor_))
					continue;
				if (!on_screen(
				        static_cast<short>((target->xpos() + 1) / GRID_SIZE),
				        static_cast<short>((target->ypos() + 1) / GRID_SIZE),
				        radarx, radary))
					continue;
				const Sint32 bx =
					xloc + ((target->xpos() + 1) / GRID_SIZE - radarx);
				const Sint32 by =
					yloc + ((target->ypos() + 1) / GRID_SIZE - radary);
				if (bx < xloc || bx > (xloc + xview) ||
				    by < yloc || by > (yloc + yview))
					continue;
				// An FX landmark can deliberately use its beacon as a ground
				// proxy (basketball's shadow): honour the proxy descriptor's
				// colour, jitter and pulse. Other beacon orders retain the exact
				// historical team-coloured single-point path below.
				const og::RadarBlip* descriptor_blip =
					target->query_order() == Order::FX
						? radar_blip_for(target->query_order(), target->family())
						: nullptr;
				const bool descriptor_fx_landmark =
					descriptor_blip != nullptr && descriptor_blip->landmark &&
					(descriptor_blip->color > 0 ||
					 descriptor_blip->color == og::kRadarColorTeam);
				unsigned char beacon_color;
				if (descriptor_fx_landmark)
				{
					const Uint32 base =
						descriptor_blip->color == og::kRadarColorTeam
							? static_cast<Uint32>(target->query_team_color())
							: static_cast<Uint32>(descriptor_blip->color);
					beacon_color = static_cast<unsigned char>(
						descriptor_blip->jitter > 0
							? base + rng(static_cast<Uint32>(descriptor_blip->jitter))
							: base);
					if (descriptor_blip->ping)
						plot_ping_blip(static_cast<short>(bx),
						               static_cast<short>(by), beacon_color,
						               alpha, xloc, yloc, xview, yview);
					else
						og::runtime::current_session->myscreen_->pointb(
							bx, by, beacon_color, alpha);
				}
				else
				{
					// Score teams and FFA band bytes both name a ramp; anything
					// else (255) falls back to the target's own team color.
					beacon_color =
						og::sim::is_scoring_identity(static_cast<int>(beacon.team))
							? og::sim::team_ramp_base(
								  static_cast<int>(beacon.team))
							: target->query_team_color();
					og::runtime::current_session->myscreen_->pointb(
						bx, by, beacon_color, alpha);
				}
				TRACE("radar", "beacon_blip id=%d x=%d y=%d color=%d",
				      static_cast<int>(beacon.entity_id),
				      static_cast<int>(bx), static_cast<int>(by),
				      static_cast<int>(beacon_color));
			}
		}
	}

	return 1;
}

//short radar::refresh()
//{
// The first two values are screwy... I don't know why
// myscreen->buffer_to_screen(xloc, yloc, xview, yview);
// return 1;
//}
// In theory the above function will NOT be required

short radar::on_screen(short whatx, short whaty,
                       short hor, short ver)
{
	// Return 0 if off radar.
	// These measurements are grid coords, not pixels.
	if (whatx < hor || whatx >= (hor+xview) ||
	        whaty < ver || whaty >= (ver+yview) )
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

// This function re-initializes the radar map data.  Do not
// call it often, as it is very slow ..
void radar::update()
{
    update(&og::runtime::current_session->myscreen_->level_runtime_data());
}

// Radar color for a decor id, or `base` (the cell's base-tile color) for ids
// that inherit (DECOR_NONE, BONES, out-of-range bytes). The overrides
// reproduce the legacy combined tiles' radar colors exactly: TORCH*/BRAZIER
// -> COLOR_FIRE, BOULDER_* (and COLUMN_*) -> 24 (wall grey), PEBBLES -> the
// legacy randomized-green rubble arm (the GRASS_DARK_1 base alone would give
// the fixed GREEN+3), SHRUB -> the trees green.
static short decor_radar_color(unsigned char d, short base)
{
	switch (d)
	{
		case DECOR_TORCH1:
		case DECOR_TORCH2:
		case DECOR_TORCH3:
		case DECOR_BRAZIER:
			return COLOR_FIRE;
		case DECOR_BOULDER_1:
		case DECOR_BOULDER_2:
		case DECOR_BOULDER_3:
		case DECOR_BOULDER_4:
		case DECOR_COLUMN_BOTTOM:
		case DECOR_COLUMN_TOP:
			return 24;
		case DECOR_PEBBLES:
			return static_cast<short>(COLOR_GREEN + rng(3) + 3);
		case DECOR_SHRUB:
			return COLOR_TREES;
		default:
			return base;
	}
}

// This function re-initializes the radar map data.  Do not
// call it often, as it is very slow ..
void radar::update(LevelRuntimeData* data)
{
	short temp, i, j;

	// The grid can change size between syncs (editor resize / clear paths
	// call update() directly, and an editor radar may never see start());
	// writing bmp[i+sizex*j] against stale extents is a heap overflow, so
	// re-derive everything whenever the recorded extents disagree.
	if (sizex != static_cast<short>(data->world().grid.w)
	    || sizey != static_cast<short>(data->world().grid.h)
	    || bmp.size() != static_cast<std::size_t>(size))
	{
		sync_to_grid(data);
	}

	// Multi-floor: bake the terrain of the floor the radar shows. Fall back
	// to the base grid when that floor's grid is missing or of a different
	// size (extents above were derived from the base grid). bmp_floor_ never
	// leaves 0 on single-floor levels, so this reads the legacy grid there.
	const PixieData& floor_grid = data->world().grid_for_floor(bmp_floor_);
	const bool use_floor_grid = floor_grid.valid()
	    && static_cast<short>(floor_grid.w) == sizex
	    && static_cast<short>(floor_grid.h) == sizey;
	const PixieData& baked_grid =
	    use_floor_grid ? floor_grid : data->world().grid;

	// Decor plane of the same floor (BASE+DECOR layering): where a cell
	// carries decor with a defined radar color, the decor wins over the base
	// tile color (see decor_radar_color). Gated on validity + matching dims,
	// so no-decor levels bake through exactly the legacy switch below.
	const PixieData& decor_plane = data->world().decor_for_floor(bmp_floor_);
	const bool has_decor = decor_plane.valid()
	    && static_cast<short>(decor_plane.w) == sizex
	    && static_cast<short>(decor_plane.h) == sizey;

	for (i = 0; i < sizex; i++)
		for (j = 0; j < sizey; j++)
		{
			// Check if item in background grid
			switch (static_cast<unsigned char>(baked_grid.data[static_cast<std::size_t>(i+sizex*j)]))
			{
				case PIX_GRASS1:  // grass is green
				case PIX_GRASS_DARK_1:
				case PIX_GRASS_DARK_B1:
				case PIX_GRASS_DARK_BR:
					temp = COLOR_GREEN+3;
					break;
				case PIX_GRASS2:
				case PIX_GRASS_DARK_2:
				case PIX_GRASS_DARK_B2:
				case PIX_WALL_ARROW_GRASS:
					temp = COLOR_GREEN+4;
					break;
				case PIX_GRASS3:
				case PIX_GRASS_DARK_3:
				case PIX_GRASS_DARK_R1:
				case PIX_WALL_ARROW_GRASS_DARK:
					temp = COLOR_GREEN+5;
					break;
				case PIX_GRASS4:
				case PIX_GRASS_DARK_4:
				case PIX_GRASS_DARK_R2:
					temp = COLOR_GREEN+5;
					break;
				case PIX_GRASS_DARK_LL:
				case PIX_GRASS_DARK_UR:
				case PIX_GRASS_RUBBLE:
				case PIX_GRASS_LIGHT_1: // lighter grass
				case PIX_GRASS_LIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT:
				case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT:
				case PIX_GRASS_LIGHT_LEFT_TOP:
					temp = static_cast<short>(COLOR_GREEN + rng(3) + 3);
					break;
				case PIX_TREE_M1: // Trees are green
				case PIX_TREE_ML:
				case PIX_TREE_T1:
				case PIX_TREE_MR:
				case PIX_TREE_MT:
					temp = static_cast<short>(COLOR_TREES + rng(3));
					break;
				case PIX_TREE_B1: // Trunks are brown
					temp = COLOR_BROWN + 6;
					break;
				case PIX_PAVEMENT1:   // pavement dark grey
				case PIX_PAVEMENT2:
				case PIX_PAVEMENT3:
				case PIX_PAVESTEPS1:
				case PIX_PAVESTEPS2:
				case PIX_PAVESTEPS2L:
				case PIX_PAVESTEPS2R:
				case PIX_COBBLE_1:
				case PIX_COBBLE_2:
				case PIX_COBBLE_3:
				case PIX_COBBLE_4:
					temp = 17;
					break;
				case PIX_FLOOR_PAVEL: // wood is brown
				case PIX_FLOOR_PAVER:
				case PIX_FLOOR_PAVEU:
				case PIX_FLOOR_PAVED:
				case PIX_FLOOR1:
				case PIX_WALL_ARROW_FLOOR:
					temp = COLOR_BROWN+4;
					break;
				case PIX_DIRT_1: // path is brown
				case PIX_DIRTGRASS_UL1:
				case PIX_DIRTGRASS_UR1:
				case PIX_DIRTGRASS_LL1:
				case PIX_DIRTGRASS_LR1:
				case PIX_DIRT_DARK_1:
				case PIX_DIRTGRASS_DARK_UL1:
				case PIX_DIRTGRASS_DARK_UR1:
				case PIX_DIRTGRASS_DARK_LL1:
				case PIX_DIRTGRASS_DARK_LR1:
					temp = COLOR_BROWN+5;
					break;
				case PIX_JAGGED_GROUND_1:
				case PIX_JAGGED_GROUND_2:
				case PIX_JAGGED_GROUND_3:
				case PIX_JAGGED_GROUND_4:
					temp = COLOR_BROWN+5;
					break;
				case PIX_CLIFF_BOTTOM:  // slightly darker
				case PIX_CLIFF_TOP:
				case PIX_CLIFF_LEFT:
				case PIX_CLIFF_RIGHT:
				case PIX_CLIFF_BACK_1:
				case PIX_CLIFF_BACK_2:
				case PIX_CLIFF_BACK_L:
				case PIX_CLIFF_BACK_R:
				case PIX_CLIFF_TOP_L:
				case PIX_CLIFF_TOP_R:
					temp = COLOR_BROWN+6;
					break;
				case PIX_CARPET_LL:   // carpet is purple
				case PIX_CARPET_B:
				case PIX_CARPET_LR:
				case PIX_CARPET_UR:
				case PIX_CARPET_U:
				case PIX_CARPET_UL:
				case PIX_CARPET_L:
				case PIX_CARPET_M:
				case PIX_CARPET_M2:
				case PIX_CARPET_R:
				case PIX_CARPET_SMALL_HOR:
                case PIX_CARPET_SMALL_VER:
				case PIX_CARPET_SMALL_CUP:
				case PIX_CARPET_SMALL_CAP:
				case PIX_CARPET_SMALL_LEFT:
				case PIX_CARPET_SMALL_RIGHT:
				case PIX_CARPET_SMALL_TINY:
					temp = COLOR_PURPLE+4;
					break;
				case PIX_H_WALL1: // walls are light grey
				case PIX_WALL2:
				case PIX_WALL3:
				case PIX_WALL_LL:
				case PIX_WALLTOP_H:
				case PIX_WALL4:
				case PIX_WALL5:
				case PIX_BOULDER_1:
				case PIX_BOULDER_2:
				case PIX_BOULDER_3:
				case PIX_BOULDER_4:
				case PIX_PATH_1:      // sparser cobblestone/grass
				case PIX_PATH_2:
				case PIX_PATH_3:
				case PIX_PATH_4:
					temp = 24;
					break;
				case PIX_WATER1:      // Water is dark blue
				case PIX_WATER2:
				case PIX_WATER3:
				case PIX_WATERGRASS_LL:
				case PIX_WATERGRASS_LR:
				case PIX_WATERGRASS_UL:
				case PIX_WATERGRASS_UR:
				case PIX_WATERGRASS_U:
				case PIX_WATERGRASS_L:
				case PIX_WATERGRASS_R:
				case PIX_WATERGRASS_D:
				case PIX_GRASSWATER_LL:
				case PIX_GRASSWATER_LR:
				case PIX_GRASSWATER_UL:
				case PIX_GRASSWATER_UR:
					temp = COLOR_BLUE+2;
					break;

				case PIX_WALLSIDE_L:  // White, maybe?
				case PIX_WALLSIDE1:
				case PIX_WALLSIDE_R:
				case PIX_WALLSIDE_C:
				case PIX_WALLSIDE_CRACK_C1:
					temp = COLOR_WHITE-1;
					break;

				case PIX_TORCH1:
				case PIX_TORCH2:
				case PIX_TORCH3:
				case PIX_BRAZIER1:
					temp = COLOR_FIRE;
					break;

				case PIX_SNOW1:       // snow is white
				case PIX_SNOW2:
					temp = COLOR_WHITE;
					break;
				case PIX_LAVA1:       // lava: static ember orange. COLOR_FIRE
				case PIX_LAVA2:       // (224) sits in the cycled ORANGE band
					temp = 233;       // 224-231 and strobes every frame on
					break;            // large lava fields; 233 never cycles.
				case PIX_MARSH1:      // bog is dark green (distinct from the trees ramp)
				case PIX_MARSH2:
					temp = COLOR_GREEN+7;
					break;
				case PIX_ASH1:        // ash is warm dark grey (vs pavement 17 / walls 24)
				case PIX_ASH2:
					temp = 249;
					break;
				// Z-stairs: two STATIC pinned colors (no pulse, never
				// ctx().rng — a cycled/random index would strobe the bake).
				// Tile bytes 140/141 are branch-new and absent from all
				// legacy content, so single-floor radars stay byte-identical.
				case PIX_ZSTAIR_UP:   // climb objective pops (reads on snow)
					temp = YELLOW;      // 88
					break;
				case PIX_ZSTAIR_DOWN: // exit-idiom cyan
					temp = LIGHT_BLUE;  // 120
					break;

				default:
					temp =  0;
			}
			if (has_decor)
				temp = decor_radar_color(
				    static_cast<unsigned char>(decor_plane.data[static_cast<std::size_t>(i + sizex * j)]),
				    temp);
			bmp[static_cast<std::size_t>(i+sizex*j)] = static_cast<unsigned char>(temp);
		}

}
