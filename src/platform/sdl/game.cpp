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
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/core/util.h>
#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/render/view.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/screen.h>
#include <algorithm>
#include <vector>
#include <format>
#include <iterator>

void popup_dialog(const char* title, const char* message);

namespace
{
class ScopedGameplayLoadActivation
{
public:
    explicit ScopedGameplayLoadActivation(og::runtime::SessionState* session)
        : session_(session)
        , previous_(session_ ? session_->gameplay_active_ : false)
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = true;
    }

    ~ScopedGameplayLoadActivation()
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = previous_;
    }

    ScopedGameplayLoadActivation(const ScopedGameplayLoadActivation&) = delete;
    ScopedGameplayLoadActivation& operator=(const ScopedGameplayLoadActivation&) = delete;

private:
    og::runtime::SessionState* session_ = nullptr;
    bool previous_ = false;
};
} // namespace

LoadSavedGameError load_saved_game_with_error(const char *filename, screen *screenp)
{
	if(screenp == nullptr)
	{
		LogError("load_saved_game_failed file={} reason=missing_screen\n", filename ? filename : "(null)");
		return LoadSavedGameError::MissingScreen;
	}

    if (filename != nullptr && filename[0] != '\0')
    {
        const SaveDataIoError load_error =
            screenp->save_data.load_with_error(filename);
        if (load_error != SaveDataIoError::None)
        {
            LogError("load_saved_game_failed file={} reason=save_data_load_failed io_error={}\n",
                     filename,
                     static_cast<int>(load_error));
            return LoadSavedGameError::SaveDataLoadFailed;
        }
    }

	TRACE("game", "load_saved_game file=%s scen=%d", filename, screenp->save_data.scen_num);
	guy           *temp_guy;
	walker        *temp_walker,  *replace_walker;
	Order         myord{};
	short         myfam;
	bool used_fallback_level = false;
    ScopedGameplayLoadActivation gameplay_load_active(og::runtime::current_session);

	// Spectator mode (numplayers==0) still needs 1 viewscreen for the camera
	screenp->numviews = (screenp->save_data.numplayers == 0)
	    ? 1 : screenp->save_data.numplayers;

	screenp->cleanup(screenp->numviews);
	screenp->initialize_views();
	screenp->sync_world_from_save_data();

	// And load the scenario ..
	screenp->world().id = screenp->save_data.scen_num;
	if(!screenp->load_level())
	{
	    short old_scen = screenp->save_data.scen_num;
	    // Failed? Try the campaign's first level. Campaigns may not start at
	    // 1 (CTF starts at 500, arenas at 300), so a cursor that ran past the
	    // last level wraps to the lowest scenario the mounted campaign ships.
	    short fallback_level = 1;
	    const std::vector<int> levels = list_levels_v();
	    if (!levels.empty())
	        fallback_level = static_cast<short>(levels.front());
	    LogError("load_saved_game_level_load_failed file={} scen={} action=fallback_to_{}\n",
	        filename ? filename : "(null)", old_scen, fallback_level);
		screenp->save_data.scen_num = fallback_level;
		screenp->sync_world_from_save_data();
        screenp->world().id = fallback_level;
        used_fallback_level = true;
        if(!screenp->load_level())
        {
				LogError("load_saved_game_failed file={} scen={} fallback={} reason=fallback_level_load_failed\n",
					filename ? filename : "(null)", old_scen, fallback_level);
				return LoadSavedGameError::FallbackLevelLoadFailed;
        }
	}

	// LevelRuntimeData::load() clears GameWorld transient/persistent gameplay fields.
	// Reapply SaveData-owned state before simulation begins.
	screenp->sync_world_from_save_data();
    screenp->world().difficulty =
        static_cast<short>(og::ui::difficulty_percent(og::runtime::current_session->current_difficulty_));

	TRACE("game", "level loaded: scen%d", screenp->world().id);
	for(auto& uptr : screenp->world().oblist)
	{
	    walker* w = uptr.get();
		if (w)
			w->set_difficulty(static_cast<Uint32>(w->stats()->level()));
	}

	// Cycle through the team list ..
	for(int guy_idx = 0; guy_idx < screenp->save_data.team_size; guy_idx++)
    {
	    temp_guy = screenp->save_data.team_list[guy_idx].get();
	    if (!temp_guy) continue;
	    // §4.2: benched members stay in the save but never enter the level —
	    // must match spawn_team_from_save (the authoritative server world) or
	    // the display would show a hero the sim never spawned.
	    if (!temp_guy->deployed) continue;
	    temp_walker = guy_create_and_add_walker(*temp_guy, screenp);
	    if (!temp_walker) continue;
	    // Clear the new guy's battle data
	    temp_walker->myguy->scen_damage = 0;
	    temp_walker->myguy->scen_kills = 0;
	    temp_walker->myguy->scen_damage_taken = 0;
	    temp_walker->myguy->scen_min_hp = 5000000;
	    temp_walker->myguy->scen_shots = 0;
	    temp_walker->myguy->scen_hits = 0;

		// A start marker belongs to exactly one team. Never fall through to a
		// foreign marker: selectable roster teams made the old "grab any marker"
		// fallback visibly deploy yellow/green/blue heroes from red starts. If a
		// map has no marker for this team (or its markers are exhausted), use the
		// neutral teleport fallback below.
			replace_walker = screenp->first_of(Order::Special,
			                                    FAMILY_RESERVED_TEAM,
			                                    static_cast<int>(temp_guy->teamnum));
		if (replace_walker)
		{
			// Inherit the start marker's floor so the player spawns on the
			// authored floor in multi-floor levels. set_floor MUST precede setxy:
			// setxy re-buckets the floor-keyed obmap at the walker's current
			// floor. Single-floor safe: legacy markers are floor 0 (a no-op).
			temp_walker->set_floor(replace_walker->floor());
			temp_walker->setxy(replace_walker->xpos(), replace_walker->ypos());
			replace_walker->set_dead(1);
		}
		else
		{
			// Scatter the overflowing characters..
			temp_walker->teleport();
		}
		// Record the level-entry spawn point (the classic respawn feature
		// revives heroes here). Read back from the walker so the RNG teleport
		// fallback position is captured exactly.
		temp_walker->set_spawn_point(temp_walker->xpos(), temp_walker->ypos(),
		                             static_cast<std::uint8_t>(temp_walker->floor()));
	}
    
	    // Destroy all player markers (by setting them to dead)
	replace_walker = screenp->first_of(Order::Special, FAMILY_RESERVED_TEAM);
	while (replace_walker)
	{
		replace_walker->set_dead(1);
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

	const short preferred_team = screenp->save_data.my_team;
	const bool has_preferred_team = std::any_of(
		screenp->save_data.team_list.begin(),
		screenp->save_data.team_list.end(),
		[preferred_team](const std::unique_ptr<guy>& member) {
			return member != nullptr && member->teamnum == preferred_team;
		});
	if (has_preferred_team)
	{
		view_teams.erase(
			std::remove(view_teams.begin(), view_teams.end(), preferred_team),
			view_teams.end());
		view_teams.insert(view_teams.begin(), preferred_team);
	}

	// Have we already done this scenario? CTF maps skip the purge: rematches
	// replay the full map (flags, control points, and bot squads stay).
	if (og::runtime::current_session->myscreen_->save_data.is_level_completed(og::runtime::current_session->myscreen_->save_data.scen_num) &&
	    !(og::runtime::current_session->myscreen_->world().type & GameWorld::TYPE_CTF))
	{
		//                Log("already done level\n");
		for(auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
		{
		    walker* w = uptr.get();
			if (w)
			{
			    // Kill everything except for our team, exits, and teleporters
				myfam = w->family();
				myord = w->query_order();
				if ( ( (w->team_num() ==0 || w->myguy) && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters
				   )
				{
					// do nothing); legal guy
				}
				else
					w->set_dead(1);
			}
		}

			for(auto& uptr : screenp->world().weaplist)
			{
			    walker* w = uptr.get();
			if (w)
			{
				myfam = w->family();
				myord = w->query_order();
				if ( (w->team_num() ==0 && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing); legal guy
				}
				else
					w->set_dead(1);
			}
		}

			for(auto& uptr : screenp->world().fxlist)
			{
			    walker* w = uptr.get();
			if (w)
			{
				myfam = w->family();
				myord = w->query_order();
				if ( (w->team_num() ==0 && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing); legal guy
				}
				else
					w->set_dead(1);
			}
		}
	}

    // Here we decide if all players are controlling team 0, or if they're
    // playing competing teams. Then pre-assign controls so the first redraw
    // (shown before/under scenario intro text) is centered on the player.
    const bool spectator = og::ui::is_spectator_mode(screenp->save_data);
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
        if (spectator)
        {
            // Spectator mode: set a camera target but don't claim player control
            if (view->control && view_idx == 0)
                screenp->world().control_hp = view->control->stats()->hitpoints();
        }
        else if (view->control && view->control->user() == -1)
        {
            view->control->set_user(static_cast<signed char>(view_idx));
            view->control->set_act_type(ACT_CONTROL);
            view->control->stats()->clear_command();
            if (view_idx == 0)
                screenp->world().control_hp = view->control->stats()->hitpoints();
        }
        ++view_idx;
    }

	return used_fallback_level ? LoadSavedGameError::UsedFallbackLevel : LoadSavedGameError::None;
}

short load_saved_game(const char *filename, screen  *screenp)
{
	const LoadSavedGameError err = load_saved_game_with_error(filename, screenp);
	if(err == LoadSavedGameError::SaveDataLoadFailed)
	{
		std::string buf = "Could not load the saved game.\nPlease report this problem to the developer!\n";
		popup_dialog("ERROR", buf.c_str());
		return 0;
	}
	if(err == LoadSavedGameError::FallbackLevelLoadFailed)
	{
		std::string buf = "Fallback loading failed.\nCould not load scenario.\nPlease report this problem to the developer!\n";
		popup_dialog("ERROR", buf.c_str());
		return 0;
	}
	return (err == LoadSavedGameError::MissingScreen) ? 0 : 1;
}
