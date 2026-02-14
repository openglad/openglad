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

#include <openglad/core/version.h>
#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_loop.h>
#include <openglad/runtime/screen_lifecycle.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <openglad/legacy/colors.h>
#include <ctime>
#include <openglad/data/gparser.h>
#include <string>
#include <cstring>
#include <format>
#include <stdexcept>
#include <openglad/core/util.h>
#include <openglad/platform/io.h>
#include <openglad/render/text.h>
#include <openglad/ui/results_screen.h>
#include <openglad/runtime/game_context.h>
extern options *theprefs;

namespace
{
inline screen* active_screen()
{
    if(ctx().game_screen != nullptr)
        return ctx().game_screen;
    return myscreen;
}

inline options* active_prefs()
{
    if (ctx().prefs)
        return ctx().prefs.get();
    return theprefs;
}

inline cfg_store& active_config()
{
    if(ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}
} // namespace

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

#ifdef OUYA
#include <openglad/legacy/OuyaController.h>
#endif


#ifdef __EMSCRIPTEN__
// Game state machine for Emscripten - allows single main loop to handle all states
enum class GameState {
    Intro,
    Picker,
    Playing,
    Quit
};
static GameState g_game_state = GameState::Intro;
static bool g_state_initialized = false;
#endif

extern bool debug_draw_paths;
extern bool debug_draw_obmap;

#ifdef TESTING
// Defined in src/runtime/glad_gameplay.cpp (linked into test binaries via og_game_test).
extern bool g_test_remove_exits;
#endif

// Z's script: #include <process.h>

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);

void picker_main(Sint32 argc, char **argv);
void intro_main(Sint32 argc, char **argv);

short remaining_foes(screen *myscreen, walker* myguy);
short remaining_team(screen *myscreen, char myteam);
short score_panel(screen *myscreen);
short score_panel(screen *myscreen, short do_it);
short new_score_panel(screen *s, short /*do_it*/);
void draw_value_bar(short left, short top, walker * control, short mode,screen * myscreen);
void new_draw_value_bar(Sint32 left, Sint32 top,
                        walker  * control, short mode, screen * myscreen);
void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor,
                         short somelength, screen * myscreen);
void init_input();

void draw_radar_gems(screen  *myscreen);
void draw_gem(short x, short y, short color, screen * myscreen);

void glad_main(screen *myscreen, Sint32 playermode);

// Zardus: FIX: from view.cpp. We need this here so that it doesn't
// try to create it before main and go nuts trying to load it
extern options *theprefs;

// Frame state for main game loop (used by Emscripten and native builds)
extern GameLoopFrameState g_frame_state;

// Forward declarations
void glad_init();

#ifdef __EMSCRIPTEN__
// Forward declarations for state handlers
void picker_init();
bool picker_frame();  // Returns true when should transition to game
void picker_cleanup_for_game();
void picker_reinit_after_game();

// Emscripten frame wrapper with timing control
// The browser calls this at ~60 FPS via requestAnimationFrame
// We accumulate time and only run game logic at the intended frame rate
static void emscripten_frame_wrapper() {
	screen* current_screen = active_screen();
	// Calculate time since last call
	Uint32 current_time = SDL_GetTicks();
	Uint32 delta = current_time - g_frame_state.last_frame_time;
	g_frame_state.last_frame_time = current_time;
	g_frame_state.accumulated_time += delta;

	// Calculate target frame time based on timer_wait (in ticks, 1 tick = 13.6ms)
	// timer_wait defaults to 6, giving ~82ms per frame (~12 FPS)
	short timer_wait = 6; // Safe default until myscreen is initialized
	if (current_screen && current_screen->timer_wait > 0) {
		timer_wait = current_screen->timer_wait;
	}
	Uint32 target_frame_time;
	if (g_game_speed_factor == 0.0f) {
		target_frame_time = 0; // Max speed: run every browser frame
	} else {
		target_frame_time = static_cast<Uint32>(timer_wait * 13.6f / g_game_speed_factor);
		if (target_frame_time < 16) target_frame_time = 16; // Minimum ~60 FPS cap
	}

	// Only run logic if enough time has accumulated
	if (g_frame_state.accumulated_time >= target_frame_time) {
		switch (g_game_state) {
			case GameState::Picker:
				if (!g_state_initialized) {
					picker_reinit_after_game();
					g_state_initialized = true;
				}
				if (picker_frame()) {
					// Transition to playing state
					Log("Transitioning from PICKER to PLAYING\n");
					picker_cleanup_for_game();
					g_game_state = GameState::Playing;
					g_state_initialized = false;
				}
				break;

			case GameState::Playing:
				if (!g_state_initialized) {
					// Initialize game state
					Log("GameState::Playing: Initializing game\n");
					release_mouse();
					if(current_screen == nullptr)
					{
						LogError("game_state_init_failed state=playing reason=missing_screen\n");
						g_game_state = GameState::Quit;
						break;
					}
					current_screen->ready_for_battle(current_screen->save_data.numplayers);
					glad_init();
					g_frame_state.done = false;
					g_frame_state.currentcycle = 0;
					g_frame_state.cycletime = 3;
					g_state_initialized = true;
				}
				game_frame(*current_screen, g_frame_state);
				if (g_frame_state.done) {
					Log("Game done, transitioning back to PICKER\n");
					clear_keyboard();
					current_screen->level_data.delete_objects();
					g_game_state = GameState::Picker;
					g_state_initialized = false;
				}
				break;

			case GameState::Quit:
				emscripten_cancel_main_loop();
				break;

			default:
				break;
		}

		// Subtract one frame's worth of time (don't reset to 0 to handle remainder)
		g_frame_state.accumulated_time -= target_frame_time;
		// Clamp to prevent spiral of death if frames take too long
		if (g_frame_state.accumulated_time > target_frame_time * 2) {
			g_frame_state.accumulated_time = 0;
		}
	}
}
#endif

#ifndef TESTING
int main(int argc, char *argv[])
{
	try
	{
		init_logging();  // Set up logging output (uses JS console on web)
		io_init(argc, argv);

		active_config().load_settings();
		active_config().save_settings();
		active_config().commandline(argc, argv);

		ctx().prefs = std::make_unique<options>();
		theprefs = ctx().prefs.get();
		create_global_screen(1);
		ctx().config = &active_config();

    #ifdef OUYA
    OuyaControllerManager::init();
    #endif

		//buffers: setting the seed
		srand(static_cast<unsigned int>(time(nullptr)));

		init_input();
		intro_main(argc, argv);

	#ifdef __EMSCRIPTEN__
	// For Emscripten, initialize picker and start the unified main loop
	picker_init();
	Log("main: After picker_init, about to check if game should start\n");

	// Check if picker_init resulted in a game start request
	extern bool picker_check_start_requested();
	if (picker_check_start_requested()) {
		Log("main: Game start was requested during picker_init, starting in PLAYING state\n");
		g_game_state = GameState::Playing;
		g_state_initialized = false;  // Will trigger glad_init on first frame
	} else {
		Log("main: No game start requested, starting in PICKER state\n");
		g_game_state = GameState::Picker;
		g_state_initialized = true;
	}

	// Initialize timing
	g_frame_state.last_frame_time = SDL_GetTicks();
	g_frame_state.accumulated_time = 0;

		// Start the unified main loop - handles all game states
		emscripten_set_main_loop(emscripten_frame_wrapper, 0, 1);
	#else
		// Native build: use traditional blocking calls
		picker_main(argc, argv);
		text_shutdown();
		io_exit();
	#endif

	}
	catch (const std::runtime_error& e)
	{
		LogError("Unrecoverable error: {}\n", e.what());
		return 1;
	}

	return 0;
}
#endif // TESTING

void draw_radar_gems(screen* s)
{
	short upper_left_x = 246;
	short upper_left_y = 140;
	short upper_right_x = upper_left_x + 65;
	short upper_right_y = upper_left_y;
	short lower_left_x = upper_left_x;
	short lower_left_y = upper_left_y + 49;
	short lower_right_x = upper_right_x;
	short lower_right_y = lower_left_y;

	short team_light;

	static short old_team_num = -1;
	if (old_team_num == s->viewob[0]->control->team_num)
		return;
	old_team_num = s->viewob[0]->control->team_num;

	team_light = s->viewob[0]->control->query_team_color();

	draw_gem(upper_left_x, upper_left_y, team_light, s);
	draw_gem(upper_right_x, upper_right_y, team_light, s);
	draw_gem(lower_left_x, lower_left_y, team_light, s);
	draw_gem(lower_right_x, lower_right_y, team_light, s);
}

void draw_gem(short x, short y, short color, screen* s)
{
	const unsigned char light = static_cast<unsigned char>(color);
	const unsigned char med = static_cast<unsigned char>(light + 2);
	const unsigned char darker = static_cast<unsigned char>(med + 2);
	const unsigned char darkest = static_cast<unsigned char>(darker + 2);

	s->point(x, y, light);
	s->point(x-1, y+1, light);
	s->point(x, y+1, med);
	s->point(x+1, y+1, darker);
	s->point(x-2, y+2, light);
	s->hor_line(x-1, y+2, 3, med);
	s->point(x+2, y+2, darkest);
	s->point(x-1, y+3, darker);
	s->point(x, y+3, med);
	s->point(x+1, y+3, darkest);
	s->point(x, y+4, darkest);
}

bool float_eq(float a, float b);

void draw_value_bar(short left, short top,
                    walker  * control, short mode, screen * s)
{
	float points;
	Sint32 bar_length = 0;
	Sint32 bar_remainder = 60 - bar_length;
	Sint32 i = 0;
	Sint32 j = 0;
	unsigned char whatcolor = 0;

	if (mode == 0) // hitpoint bar
	{
		points = control->stats()->hitpoints;

		if (float_eq(points, control->stats()->max_hitpoints))
			whatcolor = MAX_HP_COLOR;
		else if ( (points * 3) < control->stats()->max_hitpoints)
			whatcolor = LOW_HP_COLOR;
		else if ( (points * 3 / 2) < control->stats()->max_hitpoints)
			whatcolor = MID_HP_COLOR;
		else if (points < control->stats()->max_hitpoints)
			whatcolor = HIGH_HP_COLOR;
			else 
				whatcolor = ORANGE_START;

			if (points > control->stats()->max_hitpoints)
				bar_length = 60;
			else
				bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_hitpoints));
			bar_remainder = 60 - bar_length;

			s->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
			//myscreen->fastbox(left, top, 61, 6, BOX_COLOR, 1);
			if ( points > control->stats()->max_hitpoints)
				for (i=0;i<bar_length/2;i++)
					for (j=0; j<3; j++)
					{
						const unsigned char col = static_cast<unsigned char>(whatcolor + static_cast<unsigned char>((i + j) % 16));
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, col);
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, col);
					}
			else
				s->fastbox(left+1, top+1, bar_length, 5, whatcolor);
			s->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

			for (i=0;i<4;i++)
			{
				//upper left
				s->ver_line(left+i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top, 2-i, 27);
				//upper right
				s->ver_line(left+61-i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top, 2-i, 27);
				//lower left
				s->ver_line(left+i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top+5+i, 2-i, 27);
				//lower right
				s->ver_line(left+61-i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top+5+i, 2-i, 27);
			}
	}  // end of doing hp stuff..
	else if (mode == 1) // sp stuff ..
	{
		points = control->stats()->magicpoints;

		if (float_eq(points, control->stats()->max_magicpoints))
			whatcolor = MAX_MP_COLOR;
		else if ( (points * 3) < control->stats()->max_magicpoints)
			whatcolor = LOW_MP_COLOR;
		else if ( (points * 3 / 2) < control->stats()->max_magicpoints)
			whatcolor = MID_MP_COLOR;
		else if (points < control->stats()->max_magicpoints)
			whatcolor = HIGH_MP_COLOR;
			else 
				whatcolor = WATER_START;

			if (points > control->stats()->max_magicpoints)
				bar_length = 60;
			else
				bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_magicpoints));
			bar_remainder = 60 - bar_length;

			s->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
			if ( points > control->stats()->max_magicpoints)
				for (i=0;i<bar_length/2;i++)
					for (j=0; j<3; j++)
					{
						const unsigned char col = static_cast<unsigned char>(whatcolor + static_cast<unsigned char>((i + j) % 16));
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, col);
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, col);
					}
			else
				s->fastbox(left+1, top+1, bar_length, 5, whatcolor);
			s->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

			for (i=0;i<4;i++)
			{
				//upper left
				s->ver_line(left+i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top, 2-i, 27);
				//upper right
				s->ver_line(left+61-i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top, 2-i, 27);
				//lower left
				s->ver_line(left+i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top+5+i, 2-i, 27);
				//lower right
				s->ver_line(left+61-i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top+5+i, 2-i, 27);
			}
	} // end of sp stuff
} // end of drawing routine ..

// Note: new_draw_value_bar/new_score_panel/draw_percentage_bar moved to
// src/runtime/score_panel.cpp so score_panel() can be linked without main().
