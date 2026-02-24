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
//view.cpp

/* ChangeLog
	buffers: 7/31/02: *include cleanup
*/

#include <openglad/input/input.h>
#include <openglad/runtime/cheat_handler.h>
#include <openglad/ui/picker_common.h>
#include <openglad/legacy/colors.h>
#include <openglad/core/version.h>
#include <openglad/core/util.h>
#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/io.h>
#include <openglad/render/pal32.h>
#include <openglad/render/pixien.h>
#include <openglad/render/radar.h>
#include <openglad/render/view.h>
#include <openglad/data/level_render.h>
#include <openglad/render/walker_draw.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/sim/sim_input_handler.h>
#include <string>
#include <format>
#include <openglad/legacy/view_sizes.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <openglad/legacy/test_trace.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
  #ifdef __ASYNCIFY__
    #define YIELD_SLEEP(ms) emscripten_sleep(ms)
  #else
    #warning "ASYNCIFY is not enabled; YIELD_SLEEP will be a no-op."
    #define YIELD_SLEEP(ms) ((void)(ms))
  #endif
#else
  #define YIELD_SLEEP(ms) ((void)(ms))
#endif

// these are for chad's team info page
inline constexpr int VIEW_TEAM_TOP = 2;
inline constexpr int VIEW_TEAM_LEFT = 20;
inline constexpr int VIEW_TEAM_BOTTOM = 198;
inline constexpr int VIEW_TEAM_RIGHT = 280;

inline constexpr SDL_Keycode SDLK_KP0 = SDLK_KP_0;
inline constexpr SDL_Keycode SDLK_KP1 = SDLK_KP_1;
inline constexpr SDL_Keycode SDLK_KP2 = SDLK_KP_2;
inline constexpr SDL_Keycode SDLK_KP3 = SDLK_KP_3;
inline constexpr SDL_Keycode SDLK_KP4 = SDLK_KP_4;
inline constexpr SDL_Keycode SDLK_KP5 = SDLK_KP_5;
inline constexpr SDL_Keycode SDLK_KP6 = SDLK_KP_6;
inline constexpr SDL_Keycode SDLK_KP7 = SDLK_KP_7;
inline constexpr SDL_Keycode SDLK_KP8 = SDLK_KP_8;
inline constexpr SDL_Keycode SDLK_KP9 = SDLK_KP_9;


// Zardus: these were originally static chars but are now ints
// Now define the arrays with their default values

int key1[] = {
                 SDLK_w, SDLK_e, SDLK_d, SDLK_c,  // movements
                 SDLK_x, SDLK_z, SDLK_a, SDLK_q,
                 SDLK_LCTRL, SDLK_LALT,                    // fire & special
                 SDLK_TAB,                               // switch guys
                 SDLK_1,                                 // change special
                 SDLK_s,                                 // Yell
                 SDLK_LSHIFT,                        // Shifter
                 SDLK_2,                                 // Options menu
                 SDLK_F5,                                 // Cheat key
             };
             
int key2[] = {
                 SDLK_KP8, SDLK_KP9, SDLK_KP6, SDLK_KP3,  // movements
                 SDLK_KP2, SDLK_KP1, SDLK_KP4, SDLK_KP7,
                 SDLK_KP0, SDLK_KP_ENTER,                    // fire & special
                 SDLK_KP_PLUS,                          // switch guys
                 SDLK_KP_MINUS,                         // change special
                 SDLK_KP5,                                // Yell
                 SDLK_KP_PERIOD,                                // Shifter
                 SDLK_KP_MULTIPLY,                         // Options menu
                 SDLK_F8,                                    // Cheat key
             };

int key3[] = {
                 SDLK_i, SDLK_o, SDLK_l, SDLK_PERIOD,  // movements
                 SDLK_COMMA, SDLK_m, SDLK_j, SDLK_u,
                 SDLK_SPACE, SDLK_SEMICOLON,                    // fire & special
                 SDLK_BACKSPACE,                               // switch guys
                 SDLK_7,                                 // change special
                 SDLK_k,                                 // Yell
                 SDLK_RSHIFT,                        // Shifter
                 SDLK_8,                                 // Options menu
                 SDLK_F7,                                 // Cheat key
             };

int key4[] = {
                 SDLK_t, SDLK_y, SDLK_h, SDLK_n,  // movements
                 SDLK_b, SDLK_v, SDLK_f, SDLK_r,
                 SDLK_5, SDLK_6,                    // fire & special
                 SDLK_EQUALS,                               // switch guys
                 SDLK_3,                                 // change special
                 SDLK_g,                                 // Yell
                 SDLK_MINUS,                        // Shifter
                 SDLK_4,                                 // Options menu
                 SDLK_F6,                                 // Cheat key
             };

// This is for saving/loading the key preferences
Sint32 save_key_prefs();
Sint32 load_key_prefs();
// Zardus: no longer unsigned
int get_keypress();
inline constexpr const char* KEY_FILE = "keyprefs.dat";

// This only exists so we can use the array constructor
//   for our prefs object (grumble grumble)
// Zardus: these used to be static chars too
int *normalkeys[] = {key1,key2,key3,key4};
// Zardus: keys is a sys var (apparently) so we'll use allkeys
int allkeys[4][16];

// theprefs is now a macro defined in view.h (dereferences current_session).
// myscreen is now a macro defined in base.h (dereferences current_session).

namespace
{
inline screen* active_screen()
{
    return og::runtime::current_session->myscreen_;
}

inline options* active_prefs()
{
    return og::runtime::current_session->theprefs_;
}
} // namespace

// ************************************************************
//  VIEWSCREEN -- It's nothing like viewscreen, it just looks like it
// ************************************************************
/*
  viewscreen(char,short,short,screen)    - initializes the viewscreen data (pix = char)
  short draw()
*/

// viewscreen -- this initializes the graphics data for the viewscreen,
// as well as its graphics x and y size.  In addition, it informs
// the viewscreen of the screen object it is linked to.
viewscreen::viewscreen(short x, short y, short width,
                       short height, short whatnum)
{
	Sint32 i;

	xview = width;
	yview = height;
	topx = topy = 0;
	xloc = x;  // where to display on the physical screen
	yloc = y;
	endx = xloc+width;
	endy = yloc+height;
	control = nullptr;
	gamma = 0;
	prefsob = active_prefs();

	// Key entries ..
	mynum = whatnum;              // what viewscreen am I?
	mykeys = allkeys[mynum]; // assign keyboard mappings

	// Set preferences to default values
	/*
	  prefs[PREF_LIFE]  = PREF_LIFE_BOTH; // display hp/sp bars and numbers
	  prefs[PREF_SCORE] = PREF_SCORE_ON;  // display score/exp info
	  prefs[PREF_VIEW]  = PREF_VIEW_FULL; // start at full screen
	  prefs[PREF_JOY]   = PREF_NO_JOY; //default to no joystick
	  prefs[PREF_RADAR] = PREF_RADAR_ON;
	  prefs[PREF_FOES]  = PREF_FOES_ON;
	  prefs[PREF_GAMMA] = 0;
	*/
	//load_key_prefs(); // load key prefs, if present
	prefsob->load(this);

	myradar = std::make_unique<radar>(this, active_screen(), mynum);
	radarstart = 0; //the radar has not yet been started

	for (i=0; i < MAX_MESSAGES; i++)
	{
		textcycles[i] = 0;
		textlist[i].clear(); // null message
	}

	resize(prefs[PREF_VIEW]); // Properly resize the viewscreen
}

// Destruct the viewscreen and its variables
viewscreen::~viewscreen()
{
}

void viewscreen::clear()
{
	unsigned short i;

	for (i=0;i<64000;i++)
	{
		active_screen()->videobuffer[i] = 0;
	}
}

bool viewscreen::redraw()
{
	Sint32 i,j;
	Sint32 xneg = 0;
	Sint32 yneg = 0;
	walker  *controlob = control;
	auto* renderer = active_screen()->level_data.renderer_.get();
	if (!renderer) return false;
	PixieData& gridp = active_screen()->level_data.grid;
	unsigned short maxx = gridp.w;
	unsigned short maxy = gridp.h;

	// check if we are partially into a grid square and require
	//   extra row
	if (controlob)
	{
		topx = controlob->xpos - (xview - controlob->sizex)/2;
		topy = controlob->ypos - (yview - controlob->sizey)/2;
	}
	else // no control object now ..
	{
		topx = active_screen()->level_data.topx;
		topy = active_screen()->level_data.topy;
	}


	if (topx < 0)
		xneg = 1;
	if (topy < 0)
		yneg = 1;

	//note  >> 4 is equivalent to /16 but faster, since it doesn't divide
	//likewise <<4 is equivalent to *16, but faster

	for (j=(topy/GRID_SIZE)-yneg;j < ((topy+(yview))/GRID_SIZE) +1; j++)
		for (i=(topx/GRID_SIZE)-xneg;i < ((topx+(xview))/GRID_SIZE) +1; i++)
		{
			// NOTE: back is a PIXIEN.
			// background graphic [grid(x,y)] -> put in buffer
			if (i<0 || j<0 || i>=maxx || j>=maxy)
			{
				if (j == -1 && i>-1 && i<maxx)  // show side of wall
					renderer->draw_tile(PIX_WALLSIDE1, i*GRID_SIZE, j*GRID_SIZE, this);
				else if (j == -2 && i>-1 && i<maxx)  // show top side of wall
					renderer->draw_tile(PIX_H_WALL1, i*GRID_SIZE, j*GRID_SIZE, this);
				else                                                                  // show only top of wall
					renderer->draw_tile(PIX_WALLTOP_H, i*GRID_SIZE, j*GRID_SIZE, this);
			}
			else if(gridp.valid())
				renderer->draw_tile(static_cast<int>(gridp.data[i + maxx * j]), i*GRID_SIZE, j*GRID_SIZE, this);
		}

	draw_obs(); //moved here to put the radar on top of obs
	if (control && !control->dead && control->user == mynum && prefs[PREF_RADAR] == PREF_RADAR_ON)
		myradar->draw();
	display_text();
	return 1;

}

bool viewscreen::redraw(LevelData* data, bool draw_radar)
{
	Sint32 i,j;
	Sint32 xneg = 0;
	Sint32 yneg = 0;
	walker  *controlob = control;
	auto* renderer = data->renderer_.get();
	if (!renderer) return false;
	PixieData& gridp = data->grid;
	unsigned short maxx = gridp.w;
	unsigned short maxy = gridp.h;

	// check if we are partially into a grid square and require
	//   extra row
	if (controlob)
	{
		topx = controlob->xpos - (xview - controlob->sizex)/2;
		topy = controlob->ypos - (yview - controlob->sizey)/2;
	}
	else // no control object now ..
	{
		topx = data->topx;
		topy = data->topy;
	}


	if (topx < 0)
		xneg = 1;
	if (topy < 0)
		yneg = 1;

	//note  >> 4 is equivalent to /16 but faster, since it doesn't divide
	//likewise <<4 is equivalent to *16, but faster

	for (j=(topy/GRID_SIZE)-yneg;j < ((topy+(yview))/GRID_SIZE) +1; j++)
		for (i=(topx/GRID_SIZE)-xneg;i < ((topx+(xview))/GRID_SIZE) +1; i++)
		{
			// NOTE: back is a PIXIEN.
			// background graphic [grid(x,y)] -> put in buffer
			if (i<0 || j<0 || i>=maxx || j>=maxy)
			{
				if (j == -1 && i>-1 && i<maxx)  // show side of wall
					renderer->draw_tile(PIX_WALLSIDE1, i*GRID_SIZE, j*GRID_SIZE, this);
				else if (j == -2 && i>-1 && i<maxx)  // show top side of wall
					renderer->draw_tile(PIX_H_WALL1, i*GRID_SIZE, j*GRID_SIZE, this);
				else                                                                  // show only top of wall
					renderer->draw_tile(PIX_WALLTOP_H, i*GRID_SIZE, j*GRID_SIZE, this);
			}
			else if(gridp.valid())
				renderer->draw_tile(static_cast<int>(gridp.data[i + maxx * j]), i*GRID_SIZE, j*GRID_SIZE, this);
		}

	draw_obs(data); //moved here to put the radar on top of obs
	if (draw_radar && control && !control->dead && control->user == mynum && prefs[PREF_RADAR] == PREF_RADAR_ON)
		myradar->draw(data);
	display_text();
	return 1;

}

void viewscreen::display_text()
{
	Sint32 i;

	for (i=0; i < MAX_MESSAGES; i++)
	{
		if (textcycles[i] > 0)  // Display text if there's any there ..
		{
			textcycles[i]--;
			active_screen()->text_normal.write_xy( (xview-static_cast<int>(textlist[i].size())*6)/2,
			                      30+i*6, textlist[i].c_str(), YELLOW, this );
		}
	}

	// Clean up any empty slots
	for (i=0; i < MAX_MESSAGES; i++)
		if (textcycles[i] < 1 && !textlist[i].empty() )
			shift_text(i); // shift text up, starting at position i
}

void viewscreen::shift_text(Sint32 row)
{
	Sint32 i;

	for (i=row; i < (MAX_MESSAGES-1) ; i++)
	{
		textlist[i] = textlist[i+1];
		textcycles[i] = textcycles[i+1];
	}
	textlist[MAX_MESSAGES-1].clear();
	textcycles[MAX_MESSAGES-1] = 0;
}

bool viewscreen::refresh()
{
	// The first two values are screwy... I don't know why
	active_screen()->buffer_to_screen(xloc, yloc, xview, yview);
	return 1;
}

walker* viewscreen::find_next_control()
{
    return sim_find_next_control(active_screen()->level_data, my_team);
}

short viewscreen::input(const SDL_Event& event)
{
	// Gameplay input (movement, fire, special, switch, yell, etc.) is now
	// handled by process_input() via the SDL-independent InputState snapshot.
	// This method only handles raw SDL events that cannot go through InputState:
	// debug/cheat keys that use specific SDL keycodes (F-keys, letter keys, etc.).

	Uint32 totaltime, totalframes, framespersec;

	if (!control || control->dead)
		return 1;

	const PlayerInput& pi = ctx().input.players[mynum];

	// --- Debug keys (require raw SDL keycode checks) ---
	if (!pi.is_held(InputAction::Cheat))
	{
		if (query_key_event(SDLK_F3, event))
		{
			totaltime = (query_timer_control() - active_screen()->timerstart)/72;
			totalframes = (active_screen()->framecount);
			framespersec = totalframes / totaltime;
			std::string somemessage = std::format("{} FRAMES PER SEC", framespersec);
			active_screen()->viewob[0]->set_display_text(somemessage.c_str(), STANDARD_TEXT_TIME);
		}

		if (query_key_event(SDLK_F4, event))
			active_screen()->report_mem();
	}

	// Redisplay scenario text (Shift+/ = "?") — needs raw SDL for SDLK_SLASH
	if (query_key_event(SDLK_SLASH, event) && pi.is_held(InputAction::Shift) && !pi.is_held(InputAction::Cheat))
	{
		read_scenario(active_screen());
		active_screen()->redrawme = 1;
		clear_keyboard();
	}

	// --- Cheat keys (sim mutations handled in runtime layer) ---
	handle_cheat_keys(control, mynum, event, pi, active_screen());

	return 1;
}

short viewscreen::continuous_input()
{
	// Movement, fire, special, shift, and other gameplay input is now
	// handled by process_input(const InputState&), which reads from the
	// SDL-independent InputState snapshot.  This method is retained only
	// as a legacy entry-point; it is a no-op because process_input()
	// runs first in the game loop.
	return 1;
}

void viewscreen::process_input(const InputState& input_state)
{
	// Per-player debounce state (persists across frames)
	static SimInputDebounce debounce[6] = {};

	const PlayerInput& pi = input_state.players[mynum];

	// --- Spectator mode: only allow switching the camera target ---
	if (og::ui::is_spectator_mode(active_screen()->save_data))
	{
		// SwitchChar cycles the camera target (no ACT_CONTROL claim)
		if (!pi.was_pressed(InputAction::SwitchChar))
			debounce[mynum].changedchar = 0;
		else if (!debounce[mynum].changedchar)
		{
			debounce[mynum].changedchar = 1;
			walker* oldcontrol = control;
			if (!oldcontrol)
			{
				control = find_next_control();
				return;
			}

			bool reverse = pi.is_held(InputAction::Shift);
			short team = my_team;
			auto filter = [team](const walker* w) {
				return !w->dead && w->query_order() == Order::Living
				       && w->team_num == team;
			};
			walker* found = sim_cycle_next_character(
				active_screen()->level_data.oblist, oldcontrol, reverse, filter);
			if (found)
				control = found;
			if (control && control->dead)
				control = find_next_control();
		}
		// If the current control died between frames, re-acquire
		if (control && control->dead)
			control = find_next_control();
		return; // No further input processing in spectator mode
	}

	// --- Prefs key (render-layer concern: opens a UI menu) ---
	if (!pi.is_held(InputAction::Cheat))
	{
		if (pi.was_pressed(InputAction::OpenPrefs))
		{
			options_menu();
			return;
		}
	}

	// Delegate all entity-driving logic to the sim layer
	SimInputResult result = sim_process_player_input(
		pi, control, active_screen()->level_data,
		mynum, my_team, debounce[mynum],
		active_screen()->special_name,
		ctx().sim_events.get());

	// Handle render-layer effects from the sim result
	if (result.endgame_requested)
	{
		active_screen()->endgame(result.endgame_type);
		return;
	}

	if (result.control_hp_changed)
		active_screen()->control_hp = result.control_hp;

	if (!result.notify_text.empty())
	{
		if (result.play_sound >= 0)
			active_screen()->soundp->play_sound(static_cast<short>(result.play_sound));
		active_screen()->do_notify(result.notify_text.c_str(), result.notify_source);
	}
}

void viewscreen::set_display_text(std::string_view newtext, short numcycles)
{
	Sint32 i;

	i = 0;
	while (!textlist[i].empty() && i < MAX_MESSAGES)
		i++;
	if (i >= MAX_MESSAGES) // no room, need to scroll messages
	{
		shift_text(0); // shift up, starting at 0
		i = MAX_MESSAGES - 1;
	}
	//strcpy(infotext, newtext);
	textlist[i] = newtext;

	if (numcycles > 0)
		textcycles[i] = numcycles;
	else
		textcycles[i] = 0;
}

// Blanks the screen text
void viewscreen::clear_text()
{
	Sint32 i;
	for (i=0; i < MAX_MESSAGES; i++)
		textlist[i].clear();
}

bool viewscreen::draw_obs()
{
    return draw_obs(&active_screen()->level_data);
}

bool viewscreen::draw_obs(LevelData* data)
{
	// First draw the special effects
	for(auto& uptr : data->fxlist)
	{
	    walker* w = uptr.get();
		if(w && !w->dead)
			draw_walker(*w, this);
	}

	// Now do real objects
	for(auto& uptr : data->oblist)
	{
	    walker* w = uptr.get();
		if(w && !w->dead)
			draw_walker(*w, this);
	}

	// Finally draw the weapons
	for(auto& uptr : data->weaplist)
	{
	    walker* w = uptr.get();
		if(w && !w->dead)
			draw_walker(*w, this);
	}

	return 1;
}

void viewscreen::resize(short x, short y, short length, short height)
{
	xloc = x;
	yloc = y;

	xview = length;
	yview = height;

	endx = xloc+length;
	endy = yloc+height;

	if (!myradar->bmp.empty())
		myradar->start();
	active_screen()->redrawme = 1;
}

void viewscreen::resize(char whatmode)
{
	switch (active_screen()->numviews)
	{
		case 1: //  one-player mode
			switch (whatmode)
			{
				case PREF_VIEW_PANELS:
					resize(44, 12, 232, 176); // room for score panel ..
					break;
				case PREF_VIEW_1:
					resize(64, 28, 192, 144);
					break;
				case PREF_VIEW_2:
					resize(86, 44, 148, 112);
					break;
				case PREF_VIEW_3:
					resize(106, 60, 108, 80);
					break;
				case PREF_VIEW_FULL:
				default:
					resize(T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT);
					break;
			}
			break;
		case 2: // two-player mode
			switch (mynum)  // left or right view?
			{
				case 0:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(4, 16, 152, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(4, 32, 152, 136);
							break;
						case PREF_VIEW_2:
							resize(4, 48, 152, 104);
							break;
						case PREF_VIEW_3:
							resize(4, 64, 152, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT);
							break;
					}
					break;
				case 1:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(164, 16, 152, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(164, 32, 152, 136);
							break;
						case PREF_VIEW_2:
							resize(164, 48, 152, 104);
							break;
						case PREF_VIEW_3:
							resize(164, 64, 152, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT);
							break;
					}
					break;
			} // end of mynum switch
			break;
		case 3: // 3-player mode
			switch (mynum)  // left or right view?
			{
				case 0:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(4, 16, 100, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(4, 32, 100, 136);
							break;
						case PREF_VIEW_2:
							resize(4, 48, 100, 104);
							break;
						case PREF_VIEW_3:
							resize(4, 64, 100, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT);
							break;
					}
					break;
				case 1:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(216, 16, 100, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(216, 32, 100, 136);
							break;
						case PREF_VIEW_2:
							resize(216, 48, 100, 104);
							break;
						case PREF_VIEW_3:
							resize(216, 64, 100, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT);
							break;
					}
					break;
				case 2:  // 3rd player
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(112, 16, 100, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(112, 32, 100, 136);
							break;
						case PREF_VIEW_2:
							resize(112, 48, 100, 104);
							break;
						case PREF_VIEW_3:
							resize(112, 64, 100, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT);
							break;
					}
					break;
			} // end of mynum switch
			break;
		case 4: // 4-player mode
		default:
			switch (mynum)  // left or right view?
			{
				case 0:
                    resize(T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
				case 1:
                    resize(T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
				case 2:
                    resize(T_LEFT_THREE_FOUR, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
				case 3:
				default:
                    resize(T_LEFT_FOUR, T_UP_FOUR, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
			} // end of mynum switch
			break;
	} // end of numviews switch

} // end of resize(whatmode)

unsigned char compute_hp_color(float hp, float maxhp)
{
    if ( (hp * 3) < maxhp)
        return LOW_HP_COLOR;
    else if ( (hp * 3 / 2) < maxhp)
        return MID_HP_COLOR -3;
    else if (hp < maxhp)
        return MAX_HP_COLOR+4;
    else if (hp == maxhp)
        return HIGH_HP_COLOR+2;
    else
        return ORANGE_START;
}

unsigned char compute_mp_color(float mp, float maxmp)
{
    if ( (mp * 3) < maxmp)
        return LOW_MP_COLOR;
    else if ( (mp * 3 / 2) < maxmp)
        return MID_MP_COLOR;
    else if (mp < maxmp)
        return MAX_MP_COLOR;
    else if (mp == maxmp)
        return HIGH_MP_COLOR+3;
    else
        return WATER_START;
}

void viewscreen::view_team()
{
	view_team(VIEW_TEAM_LEFT, VIEW_TEAM_TOP,
	          VIEW_TEAM_RIGHT, VIEW_TEAM_BOTTOM);
}

void viewscreen::view_team(short left, short top, short right, short bottom)
{
	short teamnum = my_team;
	short text_down = static_cast<short>(top + 3);
	std::string message;
	unsigned char hpcolor, mpcolor, namecolor, numguys = 0;
	float hp, mp, maxhp, maxmp;
	text& mytext = active_screen()->text_normal;
	
	active_screen()->redrawme = 1;
	active_screen()->draw_button(left, top, right, bottom, 2);

	mytext.write_xy(left+5, text_down, "  Name  ", static_cast<unsigned char>(BLACK));

	mytext.write_xy(left+80, text_down, "Health", static_cast<unsigned char>(BLACK));

	mytext.write_xy(left+140, text_down, "Power", static_cast<unsigned char>(BLACK));

	mytext.write_xy(left+190, text_down, "Level", static_cast<unsigned char>(BLACK));

	text_down+=6;
    
    // Build the list of characters
    std::list<walker*> ls;
	for(auto& uptr : active_screen()->level_data.oblist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead
		        && w->query_order() == Order::Living
		        && w->team_num == teamnum
		        && (!w->stats()->name.empty() || w->myguy)) //&& w->owner == nullptr)
		{
		    ls.push_back(w);
		}
	}
	
	// NOTE: The old code sorted the list by hitpoints.  I would do that again, but I'll probably just be removing this function anyway.
    
    // Go through the list and draw the entries
    for(auto* w : ls)
	{
		if (w)
		{
			if (numguys++ > 30)
				break;
			hp = w->stats()->hitpoints;
			mp = w->stats()->magicpoints;
			maxhp = w->stats()->max_hitpoints;
			maxmp = w->stats()->max_magicpoints;

			hpcolor = compute_hp_color(hp, maxhp);

			mpcolor = compute_mp_color(mp, maxmp);

			if (w == control)
				namecolor = RED;
			else
				namecolor = BLACK;

			if (w->myguy)
				message = w->myguy->name;
			else
				message = w->stats()->name;
			mytext.write_xy(left+5, text_down, message.c_str(), static_cast<unsigned char>(namecolor));

			message = std::format("{:4.0f}/{:.0f}", ceilf(hp), maxhp);
			mytext.write_xy(left+70, text_down, message.c_str(), static_cast<unsigned char>(hpcolor));

			message = std::format("{:4.0f}/{:.0f}", ceilf(mp), maxmp);
			mytext.write_xy(left+130, text_down, message.c_str(), static_cast<unsigned char>(mpcolor));

			message = std::format("{:2d}", w->stats()->level);
			mytext.write_xy(left+195, text_down, message.c_str(), static_cast<unsigned char>(BLACK));

			text_down+=6;
		}
	}

	active_screen()->swap();

#ifndef TESTING
	Sint32 currentcycle = 0;
	Sint32 cycletime = 30000;
	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);  // Yield to browser event loop
		active_screen()->do_cycle(currentcycle++, cycletime);
		get_input_events(POLL);
	}
	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}
#endif

	return;
}

void viewscreen::options_menu()
{
	text& optiontext = active_screen()->text_normal;
	Sint32 gamespeed;
	std::string message, tempstr;
	Sint32 gamma_val = prefs[PREF_GAMMA];

#define LEFT_OPS 49
#define TOP_OPS 44
#define TEXT_HEIGHT 5
#define OPLINES(y) (TOP_OPS + y*(TEXT_HEIGHT+3))
#define PANEL_COLOR 13

	if (!control)
	{
	    LogError("view_options_menu_failed reason=missing_control view={}\n", mynum);
		return;  // safety check; shouldn't happen
	}

	clear_keyboard();

	// Draw the menu button
	active_screen()->draw_button(40, 40, 280, 160, 2, 1);
	active_screen()->draw_text_bar(40+4, 40+4, 280-4, 40+12);
	std::string title = std::format("Options Menu ({})", mynum+1);
	optiontext.write_xy(160-6*6, OPLINES(0)+2, title.c_str(), static_cast<unsigned char>(RED), 1);


	gamespeed = change_speed(0);
	message = std::format("Change Game Speed (+/-): {:2d}  ", gamespeed);
	optiontext.write_xy(LEFT_OPS, OPLINES(2), message.c_str(), static_cast<unsigned char>(BLACK), 1);
	switch (prefs[PREF_VIEW])
	{
		case PREF_VIEW_FULL:
			tempstr = "Full Screen";
			break;
		case PREF_VIEW_PANELS:
			tempstr = "Large";
			break;
		case PREF_VIEW_1:
			tempstr = "Medium";
			break;
		case PREF_VIEW_2:
			tempstr = "Small";
			break;
		case PREF_VIEW_3:
			tempstr = "Tiny";
			break;
		default:
			tempstr = "Weird";
			break;
	}
	message = std::format("Change View Size ([,]) : {} ", tempstr);
	active_screen()->draw_box(LEFT_OPS, OPLINES(3), LEFT_OPS+static_cast<int>(message.size())*6, OPLINES(3)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(3), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	gamma_val = change_gamma(0);
	message = std::format("Change Brightness (<,>): {} ", gamma_val);
	active_screen()->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(4), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	if (prefs[PREF_RADAR])
		message = "Radar Display (R)      : ON ";
	else
		message = "Radar Display (R)      : OFF ";
	active_screen()->draw_box(45, OPLINES(5), 275, OPLINES(5)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(5), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	switch (prefs[PREF_LIFE])
	{
		case PREF_LIFE_TEXT:
			tempstr = "Text Only";
			break;
		case PREF_LIFE_BARS:
			tempstr = "Bars Only";
			break;
		case PREF_LIFE_BOTH:
			tempstr = "Bars and Text";
			break;
		case PREF_LIFE_OFF:
			tempstr = "Off";
			break;
		default:
		case PREF_LIFE_SMALL:
			tempstr = "On";
			break;
	}
	message = std::format("Hitpoint Display (H)   : {}", tempstr);
	active_screen()->draw_box(45, OPLINES(6), 275, OPLINES(6)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(6), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	if (prefs[PREF_FOES])
		message = "Foes Display (F)       : ON ";
	else
		message = "Foes Display (F)       : OFF ";
	active_screen()->draw_box(45, OPLINES(7), 275, OPLINES(7)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(7), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	if (prefs[PREF_SCORE])
		message = "Score Display (S)      : ON ";
	else
		message = "Score Display (S)      : OFF ";
	active_screen()->draw_box(45, OPLINES(8), 275, OPLINES(8)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(8), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	optiontext.write_xy(LEFT_OPS, OPLINES(9), "VIEW TEAM INFO (T)", static_cast<unsigned char>(BLACK), 1);

	if (active_screen()->cyclemode)
		message = "Color Cycling (C)      : ON ";
	else
		message = "Color Cycling (C)      : OFF ";
	active_screen()->draw_box(45,OPLINES(10),275,OPLINES(10)+6,PANEL_COLOR,1,1);
	optiontext.write_xy(LEFT_OPS,OPLINES(10),message.c_str(),static_cast<unsigned char>(BLACK),1);

	//if (prefs[PREF_JOY] == PREF_NO_JOY)
	if(!playerHasJoystick(mynum))
		message = "Joystick Mode (J)      : OFF ";
	else
		message = "Joystick Mode (J)      : ON ";
	active_screen()->draw_box(45,OPLINES(11),275,OPLINES(11)+6,PANEL_COLOR,1,1);
	optiontext.write_xy(LEFT_OPS,OPLINES(11),message.c_str(),static_cast<unsigned char>(BLACK),1);

	optiontext.write_xy(LEFT_OPS, OPLINES(12), "Configure controls from main menu", static_cast<unsigned char>(BLACK), 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(13), "  Options -> Player Controls", static_cast<unsigned char>(BLACK), 1);

	// Draw the current screen
	active_screen()->buffer_to_screen(0, 0, 320, 200);

	// Wait for esc for now
	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);  // Yield to browser event loop
		get_input_events(POLL);
		if (og::runtime::current_session->keystates_[KEYSTATE_KP_PLUS]) // faster game speed
		{
			gamespeed = change_speed(1);
			message = std::format("Change Game Speed (+/-): {:2d}  ", gamespeed);
			active_screen()->draw_box(LEFT_OPS, OPLINES(2), LEFT_OPS+static_cast<int>(message.size())*6, OPLINES(2)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(2), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_KP_PLUS])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
		if (og::runtime::current_session->keystates_[KEYSTATE_KP_MINUS]) // slower game speed
		{
			gamespeed = change_speed(-1);
			message = std::format("Change Game Speed (+/-): {:2d}  ", gamespeed);
			active_screen()->draw_box(LEFT_OPS, OPLINES(2), LEFT_OPS+static_cast<int>(message.size())*6, OPLINES(2)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(2), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_KP_MINUS])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
		if (og::runtime::current_session->keystates_[KEYSTATE_LEFTBRACKET]) // smaller view size
		{
			prefs[PREF_VIEW] = prefs[PREF_VIEW]+1;
			if (prefs[PREF_VIEW] > 4)
				prefs[PREF_VIEW] = 4;
			resize(prefs[PREF_VIEW]);

			switch (prefs[PREF_VIEW])
			{
				case PREF_VIEW_FULL:
					tempstr = "Full Screen";
					break;
				case PREF_VIEW_PANELS:
					tempstr = "Large";
					break;
				case PREF_VIEW_1:
					tempstr = "Medium";
					break;
				case PREF_VIEW_2:
					tempstr = "Small";
					break;
				case PREF_VIEW_3:
					tempstr = "Tiny";
					break;
				default:
					tempstr = "Weird";
					break;
			}
			message = std::format("Change View Size ([,]) : {}       ", tempstr);
			active_screen()->draw_box(45, OPLINES(3), 275, OPLINES(3)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(3), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_LEFTBRACKET])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
		if (og::runtime::current_session->keystates_[KEYSTATE_RIGHTBRACKET]) // larger view size
		{
			prefs[PREF_VIEW] = prefs[PREF_VIEW]-1;
			if (prefs[PREF_VIEW] < 0)
				prefs[PREF_VIEW] = 0;
			resize(prefs[PREF_VIEW]);

			switch (prefs[PREF_VIEW])
			{
				case PREF_VIEW_FULL:
					tempstr = "Full Screen";
					break;
				case PREF_VIEW_PANELS:
					tempstr = "Large";
					break;
				case PREF_VIEW_1:
					tempstr = "Medium";
					break;
				case PREF_VIEW_2:
					tempstr = "Small";
					break;
				case PREF_VIEW_3:
					tempstr = "Tiny";
					break;
				default:
					tempstr = "Weird";
					break;
			}
			message = std::format("Change View Size ([,]) : {}  ", tempstr);
			active_screen()->draw_box(45, OPLINES(3), 275, OPLINES(3)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(3), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_RIGHTBRACKET])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_COMMA]) // darken screen
			{
				gamma_val = change_gamma(-2);
				prefs[PREF_GAMMA] = static_cast<signed char>(gamma_val);
				message = std::format("Change Brightness (<,>): {} ", gamma_val);
			active_screen()->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(4), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_COMMA])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_PERIOD]) // lighten screen
			{
				gamma_val = change_gamma(+2);
				prefs[PREF_GAMMA] = static_cast<signed char>(gamma_val);
				message = std::format("Change Brightness (<,>): {} ", gamma_val);
			active_screen()->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(4), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_PERIOD])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_r]) // toggle radar display
			{
				prefs[PREF_RADAR] = static_cast<signed char>((prefs[PREF_RADAR] + 1) % 2);
			if (prefs[PREF_RADAR])
				message = "Radar Display (R)      : ON ";
			else
				message = "Radar Display (R)      : OFF ";
			active_screen()->draw_box(45, OPLINES(5), 275, OPLINES(5)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(5), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_r])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_h]) // toggle HP display
			{
				prefs[PREF_LIFE] = static_cast<signed char>((prefs[PREF_LIFE] + 1) % 5);
			switch (prefs[PREF_LIFE])
			{
				case PREF_LIFE_TEXT:
					tempstr = "Text Only";
					break;
				case PREF_LIFE_BARS:
					tempstr = "Bars Only";
					break;
				case PREF_LIFE_BOTH:
					tempstr = "Bars and Text";
					break;
				case PREF_LIFE_OFF:
					tempstr = "Off";
					break;
				default:
				case PREF_LIFE_SMALL:
					tempstr = "On";
					break;
			}
			message = std::format("Hitpoint Display (H)   : {}", tempstr);
			active_screen()->draw_box(45, OPLINES(6), 275, OPLINES(6)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(6), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_h])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_f]) // toggle foes display
			{
				prefs[PREF_FOES] = static_cast<signed char>((prefs[PREF_FOES] + 1) % 2);
			if (prefs[PREF_FOES])
				message = "Foes Display (F)       : ON ";
			else
				message = "Foes Display (F)       : OFF ";
			active_screen()->draw_box(45, OPLINES(7), 275, OPLINES(7)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(7), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_f])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_s]) // toggle score display
			{
				prefs[PREF_SCORE] = static_cast<signed char>((prefs[PREF_SCORE] + 1) % 2);
			if (prefs[PREF_SCORE])
				message = "Score Display (S)      : ON ";
			else
				message = "Score Display (S)      : OFF ";
			active_screen()->draw_box(45, OPLINES(8), 275, OPLINES(8)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(8), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_s])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_t])      // View the teamlist
		{
			view_team();
			active_screen()->redraw();
			options_menu();
			return;
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_c])
		{
			active_screen()->cyclemode= static_cast<short>((active_screen()->cyclemode+1) %2);
			while (og::runtime::current_session->keystates_[KEYSTATE_c])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
			if (active_screen()->cyclemode)
				message = "Color Cycling (C)      : ON ";
			else
				message = "Color Cycling (C)      : OFF ";
			active_screen()->draw_box(45,OPLINES(10),275,OPLINES(10)+6,PANEL_COLOR,1,1);
			optiontext.write_xy(LEFT_OPS,OPLINES(10),message.c_str(),static_cast<unsigned char>(BLACK),1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);

		}

		if (og::runtime::current_session->keystates_[KEYSTATE_j]) // toggle joystick display
		{
		    if(playerHasJoystick(mynum))
                disablePlayerJoystick(mynum);
		    else
                resetJoystick(mynum);
		    
		    // Update joystick display message
            if(!playerHasJoystick(mynum))
                message = "Joystick Mode (J)      : OFF ";
            else
                message = "Joystick Mode (J)      : ON ";
            active_screen()->draw_box(45,OPLINES(11),275,OPLINES(11)+6,PANEL_COLOR,1,1);
            optiontext.write_xy(LEFT_OPS,OPLINES(11),message.c_str(),static_cast<unsigned char>(BLACK),1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
            
            YIELD_SLEEP(500);
            clear_events();
		}

			if (og::runtime::current_session->keystates_[KEYSTATE_b]) // toggle button display
			{
				prefs[PREF_OVERLAY] = static_cast<signed char>((prefs[PREF_OVERLAY] + 1) % 2);
			if (prefs[PREF_OVERLAY])
				message = "Text-button Display (B): ON ";
			else
				message = "Text-button Display (B): OFF ";
			active_screen()->draw_box(45, OPLINES(13), 275, OPLINES(13)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(13), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_b])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}

	}  // end of wait for ESC press

	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}
	active_screen()->redrawme = 1;
	prefsob->save(this);
}


Sint32 viewscreen::change_speed(Sint32 whichway)
{
	if (whichway > 0)
	{
		active_screen()->timer_wait -= 2;
		if (active_screen()->timer_wait < 0)
			active_screen()->timer_wait = 0;
	}
	else if (whichway < 0)
	{
		active_screen()->timer_wait += 2;
		if (active_screen()->timer_wait > 20)
			active_screen()->timer_wait = 20;
	}
	return static_cast<Sint32>((20-active_screen()->timer_wait)/2+1);
}

Sint32 viewscreen::change_gamma(Sint32 whichway)
{
	if (whichway > 1)  // lighter
	{
		load_palette("our.pal", active_screen()->newpalette);
		adjust_palette(active_screen()->newpalette, ++gamma);
	}
	if (whichway < -1)  // darker
	{
		load_palette("our.pal", active_screen()->newpalette);
		adjust_palette(active_screen()->newpalette, --gamma);
	}
	if (whichway == -1) // set to default
	{
		gamma = 0;
		load_palette("our.pal", active_screen()->newpalette);
	}
	// So 0 just means report
	return static_cast<Sint32>(gamma);
}

// **************************************************
// Options object
// **************************************************

options::options()
{
	int i;
	SDL_RWops *infile;
	for(i = 0; i < 4; i++)
		std::copy_n(normalkeys[i], 16, allkeys[i]); // Copy default keys for each player

	// Set up preference defaults
	for(i=0; i<4; i++)
	{
		prefs[i][PREF_LIFE]  = PREF_LIFE_BOTH; // display hp/sp bars and numbers
		prefs[i][PREF_SCORE] = PREF_SCORE_ON;  // display score/exp info
		prefs[i][PREF_VIEW]  = PREF_VIEW_FULL; // start at full screen
		prefs[i][PREF_JOY]   = PREF_NO_JOY; //default to no joystick
		prefs[i][PREF_RADAR] = PREF_RADAR_ON;
		prefs[i][PREF_FOES]  = PREF_FOES_ON;
		prefs[i][PREF_GAMMA] = 0;
		prefs[i][PREF_OVERLAY] = PREF_OVERLAY_OFF; // no button behind text
	}

	infile = open_read_file(KEY_FILE);

	if (!infile) // failed to read
		return;

	// Read the blobs of data ..
	for (i=0; i < 4; i++)
	{
		SDL_RWread(infile, allkeys[i], 16 * sizeof(int), 1);
		SDL_RWread(infile, prefs[i], 10, 1);
	}

	SDL_RWclose(infile);
	return;
}

// It DOESN'T actually LOAD (tee hee), it only queries
//  the prefs object... but the stupid view objects
//  don't know that... don't tell them!
short options::load(viewscreen *viewp)
{
	short prefnum = viewp->mynum;
	// Yes, we are ACTUALLY COPYING the data
	std::copy_n(prefs[prefnum], 10, viewp->prefs);
	std::copy_n(allkeys[prefnum], 16, viewp->mykeys);
	return 1;
}


// This time, we actually DO access the file since the
//   bloke playing the game might decide to quit or
//   turn off the computer at any time and then
//   wonder later, "Where'd my prefs go! Bly'me!"
short options::save(viewscreen *viewp)
{
	short prefnum = viewp->mynum;
	Sint32 i;
	SDL_RWops *outfile;

	// Yes, we are ACTUALLY COPYING the data
	std::copy_n(viewp->prefs, 10, prefs[prefnum]);
	std::copy_n(viewp->mykeys, 16, allkeys[prefnum]);

	outfile = open_write_file(KEY_FILE);

	if (!outfile) // failed to write
		return 0;

	// Write the blobs of data ..
	for (i=0; i < 4; i++)
	{
		SDL_RWwrite(outfile, allkeys[i], 16 * sizeof(int), 1);
		SDL_RWwrite(outfile, prefs[i], 10, 1);
	}

	SDL_RWclose(outfile);

	return 1;
}

options::~options()
{}

/*
 
// save_key_prefs saves the state of all the player key preferences
// to the binary file KEY_FILE (currently keyprefs.dat)
// Returns success or failure
Sint32 save_key_prefs()
{
  Sint32 i;
  char *keypointer;
  FILE *outfile;
 
  outfile = open_misc_file(KEY_FILE, "", "wb");
 
  if (!outfile) // failed to write
    return 0;
 
  // Write the blobs of data ..
  for (i=0; i < 4; i++)
  {
    keypointer = keys[i];
    fwrite(keypointer, 16 * sizeof(int), 1, outfile);
  }
 
  fclose(outfile);
 
  return 1; 
 
}
 
// load_key_prefs loads the state of all the player key preferences
// from the binary file KEY_FILE (currently keyprefs.dat)
// Returns success or failure
Sint32 load_key_prefs()
{
  Sint32 i;
  char *keypointer;
  FILE *infile;
 
  infile = open_misc_file(KEY_FILE);
 
  if (!infile) // failed to read
    return 0;
 
  // Read the blobs of data ..
  for (i=0; i < 4; i++)
  {
    keypointer = keys[i];
    fread(keypointer, 16 * sizeof(int), 1, infile);
  }
 
  fclose(infile);
  return 1; 
}
 
*/



// set_key_prefs queries the user for key preferences, and
// places them into the proper key-press array.
// It returns success or failure.
Sint32 viewscreen::set_key_prefs()
{
	text& keytext = active_screen()->text_normal;

	clear_keyboard();

	// Draw the menu button
	active_screen()->draw_button(40, 40, 280, 160, 2, 1); // same as options menu
	keytext.write_xy(160-6*6, OPLINES(0), "Keyboard Menu", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);

	keytext.write_xy(LEFT_OPS, OPLINES(2), "Press a key for 'UP':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_UP);

	keytext.write_xy(LEFT_OPS, OPLINES(3), "Press a key for 'UP-RIGHT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_UP_RIGHT);

	keytext.write_xy(LEFT_OPS, OPLINES(4), "Press a key for 'RIGHT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_RIGHT);

	keytext.write_xy(LEFT_OPS, OPLINES(5), "Press a key for 'DOWN-RIGHT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_DOWN_RIGHT);

	keytext.write_xy(LEFT_OPS, OPLINES(6), "Press a key for 'DOWN':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_DOWN);

	keytext.write_xy(LEFT_OPS, OPLINES(7), "Press a key for 'DOWN-LEFT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_DOWN_LEFT);

	keytext.write_xy(LEFT_OPS, OPLINES(8), "Press a key for 'LEFT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_LEFT);

	keytext.write_xy(LEFT_OPS, OPLINES(9), "Press a key for 'UP-LEFT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_UP_LEFT);

	// Draw the menu button; back to the top for us!
	active_screen()->draw_button(40, 40, 280, 160, 2, 1); // same as options menu
	keytext.write_xy(160-6*6, OPLINES(0), "Keyboard Menu", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);

	keytext.write_xy(LEFT_OPS, OPLINES(2), "Press your 'FIRE' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_FIRE);

	keytext.write_xy(LEFT_OPS, OPLINES(3), "Press your 'SPECIAL' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SPECIAL);

	keytext.write_xy(LEFT_OPS, OPLINES(4), "Press your 'SPECIAL SWITCH' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SPECIAL_SWITCH);

	keytext.write_xy(LEFT_OPS, OPLINES(5), "Press your 'YELL' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_YELL);

	keytext.write_xy(LEFT_OPS, OPLINES(6), "Press your 'SWITCHING' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SWITCH);

	keytext.write_xy(LEFT_OPS, OPLINES(7), "Press your 'SHIFTER' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SHIFTER);

	//  keytext.write_xy(LEFT_OPS, OPLINES(8), "Press your 'MENU (PREFS)' key:", static_cast<unsigned char>(RED), 1);
	//  allkeys[mynum][KEY_PREFS] = get_keypress();

	if (CHEAT_MODE) // are cheats enabled?
	{
		keytext.write_xy(LEFT_OPS, OPLINES(9), "Press your 'CHEATS' key:", static_cast<unsigned char>(RED), 1);
		active_screen()->buffer_to_screen(0, 0, 320, 200);
        assignKeyFromWaitEvent(mynum, KEY_CHEAT);
	}

	active_screen()->redrawme = 1;

	//  return save_key_prefs();
	return 1;
}

// Helper function to convert SDL key names to displayable text
// Only shortens long modifier names to fit in the display
// NOTE: Uses a static buffer. The returned pointer is only valid until the next call
// to this function. Callers must use the result immediately before calling again.
static const char* get_key_display_name(int keycode)
{
	static std::string buffer;
	std::string sname = SDL_GetKeyName(keycode);

	// Map arrow keys to bitmap font arrow glyphs (indices 1-4)
	if (sname == "Up") { buffer = std::string(1, '\x01'); return buffer.c_str(); }
	if (sname == "Down") { buffer = std::string(1, '\x02'); return buffer.c_str(); }
	if (sname == "Left") { buffer = std::string(1, '\x03'); return buffer.c_str(); }
	if (sname == "Right") { buffer = std::string(1, '\x04'); return buffer.c_str(); }

	// Shorten long modifier key names to fit
	if (sname == "Left Ctrl") return "LCtrl";
	if (sname == "Right Ctrl") return "RCtrl";
	if (sname == "Left Shift") return "LShift";
	if (sname == "Right Shift") return "RShift";
	if (sname == "Left Alt") return "LAlt";
	if (sname == "Right Alt") return "RAlt";
	if (sname == "Backspace") return "BkSpc";
	if (sname == "CapsLock") return "Caps";

	// Truncate if too long for display
	if (sname.size() > 10) {
		buffer = sname.substr(0, 9);
		return buffer.c_str();
	}

	return SDL_GetKeyName(keycode);
}

// view_key_bindings displays the current key bindings for this player
void viewscreen::view_key_bindings()
{
	text& keytext = active_screen()->text_normal;

	clear_keyboard();

	// Draw the menu box
	active_screen()->draw_button(20, 20, 300, 180, 2, 1);
	keytext.write_xy(95, 28, "Current Key Bindings", static_cast<unsigned char>(RED), 1);

	// Movement keys label
	keytext.write_xy(55, 42, "-- Movement --", static_cast<unsigned char>(COLOR_BLUE), 1);

	// Visual 3x3 grid for directional keys
	// Row 1: UP-LEFT, UP, UP-RIGHT
	keytext.write_xy(40, 54, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_UP_LEFT]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(75, 54, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_UP]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(100, 54, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_UP_RIGHT]), static_cast<unsigned char>(BLACK), 1);

	// Row 2: LEFT, [center], RIGHT
	keytext.write_xy(40, 66, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_LEFT]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(70, 66, "---", static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(100, 66, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_RIGHT]), static_cast<unsigned char>(BLACK), 1);

	// Row 3: DOWN-LEFT, DOWN, DOWN-RIGHT
	keytext.write_xy(40, 78, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_DOWN_LEFT]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(75, 78, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_DOWN]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(100, 78, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_DOWN_RIGHT]), static_cast<unsigned char>(BLACK), 1);

	// Action keys label
	keytext.write_xy(180, 42, "-- Actions --", static_cast<unsigned char>(COLOR_BLUE), 1);

	// Action keys in right column
	std::string msg;
	msg = std::format("Fire: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_FIRE]));
	keytext.write_xy(165, 54, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Special: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SPECIAL]));
	keytext.write_xy(165, 66, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Yell: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_YELL]));
	keytext.write_xy(165, 78, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Shifter: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SHIFTER]));
	keytext.write_xy(165, 90, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	// Switching keys
	keytext.write_xy(55, 105, "-- Switching --", static_cast<unsigned char>(COLOR_BLUE), 1);

	msg = std::format("Switch Char: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SWITCH]));
	keytext.write_xy(40, 117, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Switch Special: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SPECIAL_SWITCH]));
	keytext.write_xy(40, 129, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	// Menu key info
	keytext.write_xy(165, 117, "Options: 1", static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(165, 129, "Help: Shift+/", static_cast<unsigned char>(BLACK), 1);

	keytext.write_xy(95, 160, "Press ESC to return", static_cast<unsigned char>(RED), 1);

	active_screen()->buffer_to_screen(0, 0, 320, 200);

	// Wait for ESC
	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);
		get_input_events(POLL);
	}
	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}

	active_screen()->redrawme = 1;
}

// Waits for a key to be pressed and then released ..
// returns this key.
int get_keypress()
{
	clear_key_press_event(); // clear any previous key
	while (!query_key_press_event())
		get_input_events(WAIT);
	return query_key();
}
