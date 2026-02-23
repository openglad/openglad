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
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/data/smooth.h>
#include <openglad/core/util.h>
#include <openglad/ui/campaign_picker.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include <algorithm>
#include <format>
#include <iterator>
#include <vector>

void popup_dialog(const char* title, const char* message);

LoadSavedGameError load_saved_game_with_error(const char *filename, screen *screenp)
{
	if(screenp == nullptr)
	{
		LogError("load_saved_game_failed file={} reason=missing_screen\n", filename ? filename : "(null)");
		return LoadSavedGameError::MissingScreen;
	}

	TRACE("game", "load_saved_game file=%s scen=%d", filename, screenp->save_data.scen_num);
	guy           *temp_guy;
	walker        *temp_walker,  *replace_walker;
	Order         myord{};
	short         myfam;
	bool used_fallback_level = false;

	screenp->numviews = screenp->save_data.numplayers;

	screenp->cleanup(screenp->numviews);
	screenp->initialize_views();

	// And load the scenario ..
	screenp->level_data.id = screenp->save_data.scen_num;
	if(!screenp->level_data.load())
	{
	    short old_scen = screenp->save_data.scen_num;
	    LogError("load_saved_game_level_load_failed file={} scen={} action=fallback_to_1\n",
	        filename ? filename : "(null)", old_scen);
	    // Failed?  Try level 1.
		screenp->save_data.scen_num = 1;
        screenp->level_data.id = 1;
        used_fallback_level = true;
        if(!screenp->level_data.load())
        {
				LogError("load_saved_game_failed file={} scen={} fallback=1 reason=fallback_level_load_failed\n",
					filename ? filename : "(null)", old_scen);
				return LoadSavedGameError::FallbackLevelLoadFailed;
        }
	}

	TRACE("game", "level loaded: scen%d", screenp->level_data.id);
	for(auto& uptr : screenp->level_data.oblist)
	{
	    walker* w = uptr.get();
		if (w)
			w->set_difficulty(static_cast<Uint32>(w->stats()->level));
	}

	// Cycle through the team list ..
	for(int guy_idx = 0; guy_idx < screenp->save_data.team_size; guy_idx++)
    {
	    temp_guy = screenp->save_data.team_list[guy_idx].get();
	    temp_walker = guy_create_and_add_walker(*temp_guy, screenp);
	    // Clear the new guy's battle data
	    temp_walker->myguy->scen_damage = 0;
	    temp_walker->myguy->scen_kills = 0;
	    temp_walker->myguy->scen_damage_taken = 0;
	    temp_walker->myguy->scen_min_hp = 5000000;
	    temp_walker->myguy->scen_shots = 0;
	    temp_walker->myguy->scen_hits = 0;

		// First, try to find a marker that's the correct team number ..
			replace_walker = screenp->first_of(Order::Special,
			                                    FAMILY_RESERVED_TEAM,
			                                    static_cast<int>(temp_guy->teamnum));
		// If that doesn't work, though, grab any marker we can ..
			if (!replace_walker)
				replace_walker = screenp->first_of(Order::Special, FAMILY_RESERVED_TEAM);
		if (replace_walker)
		{
			temp_walker->setxy(replace_walker->xpos, replace_walker->ypos);
			replace_walker->dead = 1;
		}
		else
		{
			// Scatter the overflowing characters..
			temp_walker->teleport();
		}
	}
    
	    // Destroy all player markers (by setting them to dead)
	replace_walker = screenp->first_of(Order::Special, FAMILY_RESERVED_TEAM);
	while (replace_walker)
	{
		replace_walker->dead = 1;
		replace_walker = screenp->first_of(Order::Special, FAMILY_RESERVED_TEAM);
	}

	std::vector<short> view_teams;
	view_teams.reserve(static_cast<std::size_t>(screenp->numviews));
	for(int guy_idx = 0; guy_idx < screenp->save_data.team_size; guy_idx++)
	{
		const guy* team_guy = screenp->save_data.team_list[guy_idx].get();
		if (!team_guy)
			continue;
		if (team_guy->teamnum == 0)
			continue;
		if (std::find(view_teams.begin(), view_teams.end(), team_guy->teamnum) == view_teams.end())
			view_teams.push_back(team_guy->teamnum);
	}

	// Have we already done this scenario?
	if (myscreen->save_data.is_level_completed(myscreen->save_data.scen_num))
	{
		//                Log("already done level\n");
		for(auto& uptr : myscreen->level_data.oblist)
		{
		    walker* w = uptr.get();
			if (w)
			{
			    // Kill everything except for our team, exits, and teleporters
				myfam = w->query_family();
				myord = w->query_order();
				if ( ( (w->team_num==0 || w->myguy) && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters
				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}

			for(auto& uptr : screenp->level_data.weaplist)
			{
			    walker* w = uptr.get();
			if (w)
			{
				myfam = w->query_family();
				myord = w->query_order();
				if ( (w->team_num==0 && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}

			for(auto& uptr : screenp->level_data.fxlist)
			{
			    walker* w = uptr.get();
			if (w)
			{
				myfam = w->query_family();
				myord = w->query_order();
				if ( (w->team_num==0 && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}
	}

    // Here we decide if all players are controlling team 0, or if they're
    // playing competing teams. Then pre-assign controls so the first redraw
    // (shown before/under scenario intro text) is centered on the player.
    short view_idx = 0;
    const short numviews = std::min<short>(screenp->numviews, static_cast<short>(std::size(screenp->viewob)));
    for (auto& view : screenp->viewob)
    {
        if (!view || view_idx >= numviews)
            break;

        if (view_idx < static_cast<short>(view_teams.size()))
            view->my_team = view_teams[static_cast<std::size_t>(view_idx)];
        else
            view->my_team = 0;
        view->control = view->find_next_control();
        if (view->control && view->control->user == -1)
        {
            view->control->user = static_cast<signed char>(view_idx);
            view->control->set_act_type(ACT_CONTROL);
            view->control->stats()->clear_command();
            if (view_idx == 0)
                screenp->control_hp = view->control->stats()->hitpoints;
        }
        ++view_idx;
    }

	return used_fallback_level ? LoadSavedGameError::UsedFallbackLevel : LoadSavedGameError::None;
}

short load_saved_game(const char *filename, screen  *screenp)
{
	const LoadSavedGameError err = load_saved_game_with_error(filename, screenp);
	if(err == LoadSavedGameError::FallbackLevelLoadFailed)
	{
		std::string buf = "Fallback loading failed.\nCould not load scenario.\nPlease report this problem to the developer!\n";
		popup_dialog("ERROR", buf.c_str());
		return 0;
	}
	return (err == LoadSavedGameError::MissingScreen) ? 0 : 1;
}
