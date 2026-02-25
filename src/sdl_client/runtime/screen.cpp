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
//screen.cpp

/* ChangeLog
	buffers: 7/31/02: *deleted some redundant headers.
			: *load_scenario now looks for all uppercase files in
			:  levels.001 if lowercase file fails
	buffers: 8/15/02: *load_scenario now checks for uppercase file names in
			   scen/ in case lowercase check fails
*/

#include <openglad/runtime/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registries.h>
#include <openglad/entities/obmap.h>
#include <openglad/entities/walker.h>
#include <openglad/data/smooth.h>
#include <openglad/render/view.h>
#include <openglad/core/util.h>
#include <openglad/platform/io.h>
#include <openglad/input/input.h>
#include <openglad/legacy/view_sizes.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/ui/results_screen.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/render/pal32.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/data/level_render.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <format>
#include <optional>

// Used by statistics::do_command() COMMAND_FOLLOW to find a leader walker.
// The function is declared in stats.cpp; defined here in the SDL build.
walker* find_follow_leader()
{
    if (!og::runtime::current_session || og::runtime::current_session->myscreen_ == nullptr)
        return nullptr;
    if (og::runtime::current_session->myscreen_->numviews == 1)
        return og::runtime::current_session->myscreen_->viewob[0]->control;
    // Multi-view: pick whichever view's controller has yo_delay set
    if (og::runtime::current_session->myscreen_->viewob[0]->control && og::runtime::current_session->myscreen_->viewob[0]->control->yo_delay)
        return og::runtime::current_session->myscreen_->viewob[0]->control;
    if (og::runtime::current_session->myscreen_->viewob[1]->control && og::runtime::current_session->myscreen_->viewob[1]->control->yo_delay)
        return og::runtime::current_session->myscreen_->viewob[1]->control;
    return nullptr;
}

namespace
{
const char* scenario_title_error_string(screen::ScenarioTitleError err)
{
    switch(err)
    {
        case screen::ScenarioTitleError::None:
            return "none";
        case screen::ScenarioTitleError::OpenReadFailed:
            return "open_read_failed";
        case screen::ScenarioTitleError::InvalidHeader:
            return "invalid_header";
        case screen::ScenarioTitleError::UnsupportedVersion:
            return "unsupported_version";
        case screen::ScenarioTitleError::ReadFailed:
            return "read_failed";
    }
    return "unknown";
}

bool rw_read_exact(SDL_RWops* infile, void* dst, size_t size, size_t count)
{
    return infile != nullptr && SDL_RWread(infile, dst, size, count) == count;
}
} // namespace

static inline cfg_store& active_config()
{
    return cfg;
}

void screen::sync_world_from_save()
{
	world_.my_team = save_data.my_team;
	world_.allied_mode = static_cast<unsigned char>(save_data.allied_mode);
	world_.current_scenario = save_data.scen_num;
	world_.withdraw_requested = false;
	world_.create_hit_effects = active_config().is_on("effects", "hit_anim");
	{
		auto it = save_data.completed_levels.find(save_data.current_campaign);
		world_.completed_levels = (it != save_data.completed_levels.end()) ? it->second : std::set<int>{};
	}
	std::copy(std::begin(save_data.m_score), std::end(save_data.m_score), std::begin(world_.m_score));
}

void screen::sync_save_from_world()
{
	save_data.my_team = world_.my_team;
	save_data.allied_mode = static_cast<short>(world_.allied_mode);
	save_data.scen_num = world_.current_scenario;
	std::copy(std::begin(world_.m_score), std::end(world_.m_score), std::begin(save_data.m_score));
	save_data.completed_levels[save_data.current_campaign] = world_.completed_levels;
}


// From picker.cpp
extern Sint32 calculate_level(Uint32 temp_exp);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

// Screen window boundries
inline constexpr int MAX_VIEWS = 5;
inline constexpr int S_UP = 0;
inline constexpr int S_LEFT = 0;
inline constexpr int S_DOWN = 200;
inline constexpr int S_RIGHT = 320;
inline constexpr int S_WIDTH = (S_RIGHT - S_LEFT);
inline constexpr int S_HEIGHT = (S_DOWN - S_UP);
inline constexpr int MAX_SPREAD = 10; // this controls find_near_foe

// load_version_* functions now live in level_data.cpp and take OgFile& + LevelData*



Uint32 random(Uint32 x)
{
	if (x < 1)
		return 0;
	return static_cast<Uint32>( (static_cast<Uint32>(rand())) % x);
}

// ************************************************************
//  SCREEN -- graphics routines
//
//  This object is the video graphics object.  All display
//  must pass through this object, and all on-screen objects
//  are found in this object.
// ************************************************************


// Common initialization logic shared by both constructors.
void screen::init_common(short howmany, bool has_display)
{
    // Register this screen as the current session's myscreen_ so the myscreen
    // macro resolves during the rest of construction (text rendering, etc.).
    if (og::runtime::current_session)
        og::runtime::current_session->myscreen_ = this;

    TRACE("init", "screen constructor: numviews=%d display=%d", howmany, has_display);

	Sint32 i, j;

	grab_timer();

	timerstart = query_timer_control();
	framecount = 0;

	world_.control_hp = 0;

	// Load the palette ..
	load_and_set_palette("our.pal", newpalette);

	// Loading-screen text references (only used when has_display is true).
	text& load_text = text_normal;
	constexpr Sint32 load_left = 66;

	if (has_display) {
		draw_button(60, 50, 260, 110, 2, 1);
		draw_text_bar(64, 54, 256, 62);
		load_text.write_y(56, "Loading Gladiator..Please Wait", RED, 1);
		draw_text_bar(64, 64, 256, 106);
		load_text.write_xy(load_left, 70, "Loading Graphics...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
		load_text.write_xy(load_left, 70, "Loading Graphics...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 78, "Loading Gameplay Info...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
	}

	update_overscan_setting();

	palmode = 0;
	world_.end = 0;
	world_.timer_wait = 6;
	redrawme = 1;
	cyclemode = 1;
	world_.enemy_freeze = 0;
	world_.level_done = 0;
	world_.retry = false;
	sync_world_from_save();
	level_visuals_.topx = 0;
	level_visuals_.topy = 0;
	load_map_data(level_visuals_.pixdata);
	level_visuals_.renderer_ = create_sdl_level_render(level_visuals_.pixdata);

	numviews = howmany;
    for (auto& view : viewob)
        view.reset();
    initialize_views();

	if (has_display) {
		load_text.write_xy(load_left, 78, "Loading Gameplay Info...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 86, "Initializing Display...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 94, "Initializing Sound...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
	}

    soundp = std::make_unique<soundob>();
    if(!active_config().is_on("sound", "sound"))
        soundp->set_sound(1);

	if (has_display) {
		load_text.write_xy(load_left, 94, "Initializing Sound...Done", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
	}

	init_all_registries();
	for (i=0; i < NUM_FAMILIES; i++)
	{
		auto* fd = get_family_descriptor(i);
		for (j=0; j < NUM_SPECIALS; j++)
		{
			special_name[i][j] = fd ? fd->special_names[j] : "NONE";
			alternate_name[i][j] = fd ? fd->alternate_names[j] : "NONE";
		}
	}
}

screen::screen(short howmany)
    : video(), level_data(1, false, &sdl_level_data_hooks(), &world_)
{
	init_common(howmany, true);
}

screen::screen(short howmany, bool create_display)
    : video(create_display), level_data(1, false, &sdl_level_data_hooks(), &world_)
{
	init_common(howmany, create_display);
}

screen::~screen()
{
	release_timer();
	soundp.reset();
	cleanup(1); //make sure we've cleaned up
}

void screen::initialize_views()
{
    // Even though it looks okay here, these positions and sizes are overridden by viewscreen::resize() later.
	if (numviews == 1)
	{
		viewob[0] = std::make_unique<viewscreen>( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0);
	}
	else if (numviews == 2)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT, 1);
	}
	else if (numviews == 3)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
		viewob[2] = std::make_unique<viewscreen>( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
	}
	else if (numviews == 4)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
		viewob[2] = std::make_unique<viewscreen>( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
		viewob[3] = std::make_unique<viewscreen>( T_LEFT_FOUR, T_UP_FOUR, T_HALF_WIDTH, T_HALF_HEIGHT, 3);
	}
	else
    {
        LogError("screen_init_views_failed numviews={}\n", numviews);
    }
}

void screen::cleanup(short howmany)
{
	Sint32 i;

    numviews = howmany; // # of viewscreens
    for (i=0; i < MAX_VIEWS; i++)
    {
        viewob[i].reset();
    }
}

void screen::ready_for_battle(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);
    
    initialize_views();

	world_.end = 0;
	
	world_.retry = false;

	redrawme = 1;

	timerstart = query_timer_control();
	framecount = 0;
	world_.enemy_freeze = 0;

	world_.control_hp = 0;
	sync_world_from_save();

	palmode = 0;

	redrawme = 1;

}

void screen::reset(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);

	if (numviews == 1)
	{
		viewob[0] = std::make_unique<viewscreen>( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0);
	}
	else if (numviews == 2)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
	}
	else if (numviews == 3)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
		viewob[2] = std::make_unique<viewscreen>( 112, 16, 100, 168, 2);
	}
	else if (numviews == 4)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
		viewob[2] = std::make_unique<viewscreen>( 112, 16, 100, 168, 2);
		viewob[3] = std::make_unique<viewscreen>( 112, 16, 100, 168, 3);
	}

	world_.end = 0;

	redrawme = 1;
	
	save_data.reset();
	level_data.clear();
	set_draw_pos(0, 0);

	timerstart = query_timer_control();
	framecount = 0;
	world_.enemy_freeze = 0;

	world_.control_hp = 0;
	sync_world_from_save();

	palmode = 0;

	world_.end = 0;

	redrawme = 1;

}

bool screen::query_grid_passable(float x, float y, walker  *ob)
{
	return level_data.query_grid_passable(x, y, ob);
}

bool screen::query_object_passable(float x, float y, walker  *ob)
{
	return level_data.query_object_passable(x, y, ob);
}

bool screen::query_passable(float x, float y, walker  *ob)
{
	return level_data.query_passable(x, y, ob);
}

void screen::clear()
{
	unsigned short i;

	//buffers: PORT:  for (i=0;i<64000;i++)
	//buffers: PORT:  {
	//buffers: PORT:         videobuffer[i] = 0;
	//buffers: PORT:  }
	clearbuffer();
	//SDL_FillRect(screen,nullptr,SDL_MapRGB(screen->format,0,0,0));

	for (i=0; i < numviews; i ++)
		viewob[i]->clear();
}

// REDRAW -- This function moves through the data on the grid (map)
//           finding which grid squares are on screen.  For each on
//           screen, it pashorts the appropriate graphics pixie onto
//           the screen by calling the function DRAW in PIXIE.
bool screen::redraw()
{
	short i;
	for (i=0; i < numviews; i++)
		viewob[i]->redraw();

	return 1;
}

void screen::set_draw_pos(std::int32_t new_topx, std::int32_t new_topy)
{
	level_visuals_.topx = new_topx;
	level_visuals_.topy = new_topy;
}

void screen::add_draw_pos(std::int32_t dx, std::int32_t dy)
{
	level_visuals_.topx += dx;
	level_visuals_.topy += dy;
}

void screen::draw_level_data(LevelData* level)
{
	if (!level)
		return;
	for (short i = 0; i < numviews; i++)
		viewob[i]->redraw(level, false);
}

void screen::reload_level_visuals()
{
	level_visuals_.renderer_.reset();
	for (int i = 0; i < PIX_MAX; i++)
		level_visuals_.pixdata[i].free();
	load_map_data(level_visuals_.pixdata);
	level_visuals_.renderer_ = create_sdl_level_render(level_visuals_.pixdata);
}


// REFRESH -- refreshes the viewscreens
void screen::refresh()
{
	short i;
	for (i=0; i < numviews; i++)
	{
		viewob[i]->refresh();
	}
}


// **************************
// Useful stuff again
// **************************

short screen::input(const SDL_Event& event)
{
	// static text mytext;
	short i;

	for (i=0; i < numviews; i++)
		viewob[i]->input(event);

	return 1;
}

short screen::continuous_input()
{
	// static text mytext;
	short i;

	for (i=0; i < numviews; i++)
		viewob[i]->continuous_input();

	return 1;
}

void screen::process_input(const InputState& input_state)
{
	for (short i = 0; i < numviews; i++)
		viewob[i]->process_input(input_state);
}

bool screen::act()
{
	// Delegate simulation tick to GameWorld.
	og::sim::SimEventLog& events = *og::gameplay::current_game->sim_events;
	world_.my_team = save_data.my_team;
	world_.create_hit_effects = active_config().is_on("effects", "hit_anim");
	world_.tick();
	sync_save_from_world();

	struct ExitRequest
	{
		short dest_level = -1;
		bool withdraw = false;
		std::string prompt;
	};
	std::optional<ExitRequest> pending_exit_request;
	std::optional<short> pending_withdraw_level;

	// Post-tick: clean up viewscreen control pointers for dead player entities.
	// This is a rendering concern that doesn't belong in the simulation layer.
	for (int i = 0; i < numviews; i++)
	{
		if (viewob[i]->control && viewob[i]->control->dead)
			viewob[i]->control = nullptr;
	}

	// Process simulation events: dispatch sounds, notifications, etc.
	// This is the key sim/render boundary — simulation emits events,
	// the runtime layer dispatches them to platform subsystems.
	for (const auto& ev : events.events())
	{
		switch (ev.kind)
		{
			case og::sim::EventKind::PlaySound:
				if (soundp)
					soundp->play_sound(static_cast<short>(ev.a));
				break;
			case og::sim::EventKind::Notification:
				if (!ev.text.empty())
				{
					short duration = ev.a ? static_cast<short>(ev.a)
					                      : STANDARD_TEXT_TIME;
					for (short vi = 0; vi < numviews; vi++)
						viewob[vi]->set_display_text(ev.text, duration);
				}
				break;
			case og::sim::EventKind::SetPalette:
				if (ev.a == 0)
					set_palette(ourpalette);
				else
					set_palette(bluepalette);
				break;
			case og::sim::EventKind::RequestRedraw:
				redrawme = 1;
				break;
			case og::sim::EventKind::EndGame:
				events.clear();
				return endgame(static_cast<short>(ev.a),
				               static_cast<short>(static_cast<std::int32_t>(ev.b)));
			case og::sim::EventKind::DamageTile:
				damage_tile(static_cast<short>(ev.a), static_cast<short>(ev.b));
				break;
			case og::sim::EventKind::SetEnd:
				world_.end = 1;
				break;
			case og::sim::EventKind::RequestExitConfirmation:
				// First request wins; duplicates in this tick are ignored.
				if (!pending_exit_request.has_value())
				{
					ExitRequest req;
					req.dest_level = static_cast<short>(static_cast<std::int32_t>(ev.a));
					req.withdraw = (ev.b != 0);
					req.prompt = ev.text;
					if (req.prompt.empty())
					{
						std::string scen_key = std::format("scen{}", req.dest_level);
						std::string title = get_scenario_title(scen_key.c_str());
						if (title == "none")
							title = std::format("Level {}", req.dest_level);
						req.prompt = std::format("{} to {}?", req.withdraw ? "Withdraw" : "Exit", title);
					}
					pending_exit_request = req;
				}
				break;
			case og::sim::EventKind::WithdrawToLevel:
				world_.withdraw_requested = true;
				if (!pending_withdraw_level.has_value())
					pending_withdraw_level = static_cast<short>(static_cast<std::int32_t>(ev.a));
				break;
			default:
				break;
		}
	}

	if (pending_exit_request.has_value())
	{
		const ExitRequest& req = *pending_exit_request;
		const bool accepted = yes_or_no_prompt("Exit Field", req.prompt.c_str(), false);
		redrawme = 1;
		if (accepted)
		{
			if (req.withdraw)
			{
				world_.withdraw_requested = true;
				const short dest_level = pending_withdraw_level.has_value()
					? *pending_withdraw_level : req.dest_level;
				save_data.load("save0");
				sync_world_from_save();
				save_data.scen_num = dest_level;
				save_data.save("save0");
				sync_world_from_save();
				events.clear();
				return endgame(1, dest_level);
			}

			events.clear();
			return endgame(0, req.dest_level);
		}

		// User declined the prompt; clear any pending withdrawal state.
		world_.withdraw_requested = false;
	}

	events.clear();

	// Handle level completion / game ending
	if (world_.game_ended && !world_.end)
	{
		return endgame(world_.ending, world_.next_level);
	}

	if (world_.end)
		return 1;

	return 1;
}

Uint32 get_time_bonus(int playernum);

short screen::endgame(short ending)
{
	return endgame(ending, -1);
}

short screen::endgame(short ending, short nextlevel)
{
	    if(world_.end)
	    {
	        return 1;
	    }

	// LevelData id tracks the currently loaded scenario throughout gameplay.
	world_.current_scenario = static_cast<short>(level_data.id);
	sync_save_from_world();
	
	
	std::map<int, guy*> before;
	std::map<int, walker*> after;
	
	// Get guys from before battle
	for(int i = 0; i < save_data.team_size; i++)
    {
        if(save_data.team_list[i] != nullptr)
            before.insert(std::make_pair(save_data.team_list[i]->id, save_data.team_list[i].get()));
    }
	
    // Get guys from the battle
    for(auto& uptr : level_data.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && ob->myguy)
			after.insert(std::make_pair(ob->myguy->id, ob));
	}
	
	// Let's show the results!
    world_.retry = results_screen(ending, nextlevel, before, after);
    
    if(world_.retry)
    {
        // Retry without updating the roster and saving the game
        world_.end = 1;
        return 1;
    }
    
	if (ending == 1)  // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
			world_.end = 1;
		}
		else // we're withdrawing to another level
		{
			world_.end = 1;
		}
	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		world_.end = 1;
	}
		else if (ending == 0) // we won
		{
	        Uint32 bonuscash[4] = {0, 0, 0, 0};
	        Uint32 allbonuscash = 0;
	        
			// Update all the money!
			for (int i=0; i < 4; i++)
			{
				save_data.m_totalscore[i] += save_data.m_score[i];
				save_data.m_totalcash[i] += (save_data.m_score[i]*2);
				save_data.m_score[i] = 0;
			}
			for (int i=0; i < 4; i++)
			{
	            bonuscash[i] = get_time_bonus(i);
			save_data.m_totalcash[i] += bonuscash[i];
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for (int i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
	    
		// Beat that level
		save_data.add_level_completed(save_data.current_campaign, save_data.scen_num); // this scenario is completed ..
		if (nextlevel != -1)
			save_data.scen_num = nextlevel;    // Fake jumping to next level ..
        
        // Grab our team out of the level
        save_data.update_guys(level_data.oblist);
        
        // Autosave because we won
		save_data.save("save0");

		world_.end = 1;
	}

	sync_world_from_save();
	return 1;
}

walker *screen::find_near_foe(walker  *ob)
{
	return level_data.find_near_foe(ob);
}

walker  *screen::find_far_foe(walker  *ob)
{
	return level_data.find_far_foe(ob);
}

walker* screen::set_walker(walker *ob, Order order, Sint32 family)
{
    return level_data.myloader->set_walker(ob, order, family);
}

screen::ScenarioTitleError screen::get_scen_title_with_error(const char *filename, std::string& out_title)
{
    out_title = "none";
    if(filename == nullptr || filename[0] == '\0')
        return ScenarioTitleError::OpenReadFailed;

    SDL_RWops  *infile = nullptr;
    char temptext[4] = {};
    char versionnumber = 0;
    char gridname[8] = {};
    char buffer[31] = {};

    std::string tempfile = std::string(filename) + ".fss";

    // Zardus: first get the file from scen/
    infile = open_read_file("scen/", tempfile.c_str());
    if(infile == nullptr)
        return ScenarioTitleError::OpenReadFailed;

    ScenarioTitleError err = ScenarioTitleError::None;
    if(!rw_read_exact(infile, temptext, 1, 3))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }
    if (std::string(temptext, 3) != "FSS")
    {
        err = ScenarioTitleError::InvalidHeader;
        goto close_and_return;
    }

    if(!rw_read_exact(infile, &versionnumber, 1, 1))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }
    if (versionnumber < 6)
    {
        err = ScenarioTitleError::UnsupportedVersion;
        goto close_and_return;
    }

    // Discard the grid name ...
    if(!rw_read_exact(infile, gridname, 1, 8))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }

    // Return the title, 30 bytes
    if(!rw_read_exact(infile, buffer, 1, 30))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }

    out_title = std::string(buffer);

close_and_return:
    if(infile != nullptr)
        SDL_RWclose(infile);

    if(err != ScenarioTitleError::None)
    {
        LogError("scenario_title_load_failed filename={} error={}\n",
            filename ? filename : "(null)", scenario_title_error_string(err));
    }
    return err;
}

const char* screen::get_scen_title(const char *filename, screen *master)
{
    (void)master;
    static char buffer[31] = {};
    std::string out_title;
    const ScenarioTitleError err = get_scen_title_with_error(filename, out_title);
    if(err != ScenarioTitleError::None)
        out_title = "none";
    std::snprintf(buffer, sizeof(buffer), "%s", out_title.c_str());
    return buffer;

}


// Look for the first non-dead instance of a given walker ..
walker  * screen::first_of(Order whatorder, unsigned char whatfamily,
                           int team_num)
{
	for(auto& uptr : level_data.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead)
		{
			if (ob->query_order() == whatorder &&
			        ob->family== whatfamily)
			{
				if (team_num == -1 || team_num == ob->team_num)
					return ob;
			}
		}
	}
	return nullptr;
}

void screen::draw_panels(short howmany)
{
	short i;

	// Force a memory clear ..
	clearbuffer();

	if (howmany)
		howmany = howmany;
	for (i=0; i < numviews; i++)
		if ( (viewob[i]->prefs[PREF_VIEW] == PREF_VIEW_FULL) ||
		        numviews == 4 )
			; // do nothing
		else
		{
			draw_button(viewob[i]->xloc-4, viewob[i]->yloc-3,
			            viewob[i]->endx+3, viewob[i]->endy+3, 3, 1);
			draw_box(viewob[i]->xloc-1, viewob[i]->yloc-1,
			         viewob[i]->endx, viewob[i]->endy, 0, 0,1);
		}


	redraw(); // repaint the screen area ..
	buffer_to_screen(0, 0, 320, 200);
}

walker  * screen::find_nearest_blood(walker  *who)
{
	return level_data.find_nearest_blood(who);
}

std::list<walker*> screen::find_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob)
{
	return level_data.find_in_range(somelist, range, howmany, ob);
}

walker* screen::find_nearest_player(walker *ob)
{
	return level_data.find_nearest_player(ob);
}

std::list<walker*> screen::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob)
{
	return level_data.find_foes_in_range(somelist, range, howmany, ob);
}

std::list<walker*> screen::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range,
                                      Sint32* howmany, walker* ob)
{
	return level_data.find_friends_in_range(somelist, range, howmany, ob);
}

std::list<walker*> screen::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob)
{
    return level_data.find_foe_weapons_in_range(somelist, range, howmany, ob);
}


// Uses pixel coordinates
char screen::damage_tile(short xloc, short yloc) // damage the specified tile
{
	short xover, yover;
	short gridloc;

	xover = static_cast<short>(xloc / GRID_SIZE);
	yover = static_cast<short>(yloc / GRID_SIZE);

	if (xover < 0 || yover < 0)
		return 0;
	if (xover >= level_data.grid.w || yover >= level_data.grid.h)
		return 0;

	gridloc = static_cast<short>(yover*level_data.grid.w+xover);

	switch (static_cast<unsigned char>(level_data.grid.data[gridloc]))
	{
		case PIX_GRASS1: // grass
		case PIX_GRASS2:
		case PIX_GRASS3:
		case PIX_GRASS4:
			level_data.grid.data[gridloc] = PIX_GRASS1_DAMAGED;
			break;
		default:
			break;
	}

	return level_data.grid.data[gridloc];
}

void screen::do_notify(std::string_view message, walker  *who)
{
	short i,sent=0;
	for(i=0;i<numviews;i++)
	{
		if (who && viewob[i]->control == who)
		{
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);
			sent = 1;
		}

	}
	if (!sent)
		for (i=0; i < numviews; i++)
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);

}

void screen::report_mem()
{
	meminfo Memory;
	Memory.FreeLinAddrSpace = 0;
	// Zardus: PORT: this is aparently an incomplete type:  union REGS regs;
	// Same here:  struct SREGS sregs;
	// Zardus: PORT: Undeclared because of problems above:  regs.x.eax = 0x00000500;
	// Same here:  memset( &sregs, 0, sizeof(sregs) );

	// See two lines up:  sregs.es = FP_SEG( &Memory );
	// See three lines up:  regs.x.edi = FP_OFF( &Memory );

	// See two lines up: (plus sounds like a dos thing):  int386x( DPMI_INT, &regs, &regs, &sregs );

	// Them:
	//sprintf(memreport, "Largest Block: %lu bytes",
	//  Memory.LargestBlockAvail);
	//viewob[0]->set_display_text(memreport, STANDARD_TEXT_TIME);
	std::string memreport = std::format("Free Linear address: {} pages",
	        Memory.FreeLinAddrSpace);
	//  Log(memreport);
	//  Log("\n");
	viewob[0]->set_display_text(memreport.c_str(), 25);
	/*
	       Log( "Largest available block (in bytes): %lu\n",
	               MemInfo.LargestBlockAvail );
	       Log( "Maximum unlocked page allocation: %lu\n",
	               MemInfo.MaxUnlockedPage );
	       Log( "Pages that can be allocated and locked: "
	               "%lu\n", MemInfo.LargestLockablePage );
	       Log( "Total linear address space including "
	               "allocated pages: %lu\n",
	               MemInfo.LinAddrSpace );
	       Log( "Number of free pages available: %lu\n",
	                MemInfo.NumFreePagesAvail );

	       Log( "Number of physical pages not in use: %lu\n",
	                MemInfo.NumPhysicalPagesFree );
	       Log( "Total physical pages managed by host: %lu\n",
	                MemInfo.TotalPhysicalPages );
	       Log( "Free linear address space (pages): %lu\n",
	                MemInfo.FreeLinAddrSpace );
	       Log( "Size of paging/file partition (pages): %lu\n",
	                MemInfo.SizeOfPageFile );
	 */
}
