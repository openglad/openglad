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
#include "version.h"
#include "graph.h"
#include "button.h"
#include "pal32.h"

//buffers:  using input.h instead #include "int32.h"
#include "input.h"
#include "util.h"

#include "SDL.h"
#include "gparser.h"
#include "campaign_picker.h"
#include "level_picker.h"
#include "game_context.h"
#include <cstring>
#include <format>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
#ifdef TESTING
#include <atomic>
#endif
// Z's script: #include <process.h>
// Z's script: #include <i86.h> //_enable, _disable

#define DOWN(x) (72+x*15)
#define VIEW_DOWN(x) (10+x*20)
#define RAISE 1.85  // please also change in guy.cpp

#define EXIT 1 //these are leftclick return values, exit means leave picker
#define REDRAW 2 //we just exited a menu, so redraw your buttons
#define OK 4 //this function was successful, continue normal operation

// For yes/no prompts
#define YES 5
#define NO 6
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void timed_dialog(const char* message, float delay_seconds = 3.0f);

bool prompt_for_string(const std::string& message, std::string& result);

#define BUTTON_HEIGHT 15
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

//int matherr (struct exception *);

void show_guy(Sint32 frames, Sint32 who, short centerx = 80, short centery = 45); // shows the current guy ..
Sint32 name_guy(Sint32 arg); // rename (or name) the current_guy

void glad_main(Sint32 playermode);
const char* get_saved_name(const char * filename);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);

Sint32 leftmouse(button* buttons);
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& localbuttons, button* buttons, int num_buttons, Sint32& retvalue);
const char* family_name_copy(short family);

// Zardus: PORT: put in a backpics var here so we can free the pixie files themselves
PixieData backpics[5];
pixieN *backdrops[5];

// Zardus: FIX: this is from view.cpp, so that we can delete it here

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Flag to signal that game should start (for state machine)
bool g_start_game_requested = false;
// Flag to track if picker has been initialized
static bool g_picker_initialized = false;
// Store the current menu state for frame-based operation
enum class PickerMenuState {
    Main,
    CreateTeam,
    Other
};
static PickerMenuState g_picker_menu_state = PickerMenuState::Main;
#endif

guy  *current_guy = nullptr;
guy  *old_guy = nullptr;

std::string  message;
Sint32 editguy = 0;        // Global for editing guys ..
PixieData main_title_logo_data, main_columns_data;
pixieN  *main_title_logo_pix,*main_columns_pix;

vbutton * localbuttons; //global so we can delete the buttons anywhere
short current_team_num = 0;

#ifdef TESTING
// Test infrastructure for picker_mainmenu_loop
int g_picker_mainmenu_calls = 0;
int g_picker_max_mainmenu_calls = 0;  // 0 = unlimited
// Set true while glad_main is running inside go_menu, so tests can
// wait for the game to finish before clicking menu buttons.
bool g_test_in_game = false;
// Monotonic counter incremented each time go_menu starts glad_main, so
// injector threads can't miss a fast start+finish transition.
std::atomic<int> g_test_game_epoch{0};
#endif

#ifdef __EMSCRIPTEN__
void picker_request_start_game()
{
    g_start_game_requested = true;
}
#endif

#ifdef TESTING
void picker_testing_mark_game_start()
{
    g_test_game_epoch.fetch_add(1, std::memory_order_release);
    g_test_in_game = true;
}

void picker_testing_mark_game_end()
{
    g_test_in_game = false;
}
#endif

Sint32 allowable_guys[] =
    { FAMILY_SOLDIER,
      FAMILY_BARBARIAN,
      FAMILY_ELF,
      FAMILY_ARCHER,
      FAMILY_MAGE,
      FAMILY_CLERIC,
      FAMILY_THIEF,
      FAMILY_DRUID,
      FAMILY_ORC,
      FAMILY_SKELETON,
      FAMILY_FIREELEMENTAL,
      FAMILY_SMALL_SLIME,
      FAMILY_FAERIE,
      FAMILY_GHOST
    };

Sint32 current_type = 0; // guy type we're looking at

// Used to label new hires, like "SOLDIER5"
Sint32 numbought[NUM_FAMILIES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// See guy.cpp
extern Sint32 costlist[NUM_FAMILIES];
extern Sint32 statlist[NUM_FAMILIES][6];
extern Sint32 statcosts[NUM_FAMILIES][6];

// Difficulty settings .. in percent, so 100 == normal
Sint32 current_difficulty = 1; // setting 'normal'
Sint32 difficulty_level[DIFFICULTY_SETTINGS] =
    {
        50,
        100,
        200,
    };  // end of difficulty settings
char difficulty_names[DIFFICULTY_SETTINGS][80] =
    {
        "Skirmish",
        "Battle",
        "Slaughter",
    };  // end of difficulty names

static cfg_store& active_config()
{
    if (ctx().config)
        return *ctx().config;
    return cfg;
}

// The mainmenu loop: keeps showing the main menu after submenus return.
// quit() calls exit(0) directly, so we only re-enter here from submenu BACK.
void picker_mainmenu_loop()
{
    while(true) {
        TRACE("picker", "mainmenu_loop_iteration");
        mainmenu(1);
#ifdef TESTING
        g_picker_mainmenu_calls++;
        if (g_picker_max_mainmenu_calls > 0 && g_picker_mainmenu_calls >= g_picker_max_mainmenu_calls)
            return;
#endif
    }
}

void picker_main(Sint32 argc, char  **argv)
{
	Sint32 i;

	for (i=0; i < MAX_BUTTONS; i++)
		allbuttons[i] = nullptr;

	// Get main dir ..
	//strcpy(main_dir, "");

	// Set backdrops to nullptr
	for (i=0; i < 5; i++)
		backdrops[i] = nullptr;

	backpics[0] = read_pixie_file("mainul.pix");
	backpics[1] = read_pixie_file("mainur.pix");
	backpics[2] = read_pixie_file("mainll.pix");
	backpics[3] = read_pixie_file("mainlr.pix");

	backdrops[0] = new pixieN(backpics[0]);
	backdrops[0]->setxy(0, 0);
	backdrops[1] = new pixieN(backpics[1]);
	backdrops[1]->setxy(160, 0);
	backdrops[2] = new pixieN(backpics[2]);
	backdrops[2]->setxy(0, 100);
	backdrops[3] = new pixieN(backpics[3]);
	backdrops[3]->setxy(160, 100);

	myscreen->viewob[0]->resize(PREF_VIEW_FULL);

	myscreen->clearbuffer();

	//main_title_logo_data = read_pixie_file("glad.pix");
	main_title_logo_data = read_pixie_file("title.pix"); // marbled gladiator title
	main_title_logo_pix = new pixieN(main_title_logo_data);


	//main_columns_data = read_pixie_file("mage.pix");
	main_columns_data = read_pixie_file("columns.pix");
	main_columns_pix = new pixieN(main_columns_data);

	// Get the mouse, timer, & keyboard ..
	grab_mouse();
	grab_timer();
	clear_keyboard();

	// Load the current saved game, if it exists .. (save0.gtl)
	SDL_RWops* loadgame = open_read_file("save/", "save0.gtl");
	if (loadgame)
	{
	    SDL_RWclose(loadgame);
		myscreen->save_data.load("save0");
	}

	picker_mainmenu_loop();
}

void picker_quit()
{
	int i;

	for (i = 0; i < 5; i ++)
	{
		if (backdrops[i])
		{
			delete backdrops[i];
			backdrops[i] = nullptr;
		}
		
        backpics[i].free();
	}

	for (i = 0; i < MAX_BUTTONS; i++)
	{
		if (allbuttons[i])
			delete allbuttons[i];
	}

	delete myscreen;
	delete main_columns_pix;
	main_columns_data.free();
	delete main_title_logo_pix;
	main_title_logo_data.free();

#if 0
	if (cfgfile)
		cfgfile = nullptr;
#endif
}

#ifdef USE_TOUCH_INPUT
#define DISABLE_MULTIPLAYER
#endif

// mainmenu

#ifndef DISABLE_MULTIPLAYER

#ifdef __EMSCRIPTEN__
// Web build: Replace QUIT with HELP (QUIT doesn't make sense in browser)
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20, BEGINMENU, 1 , MenuNav::Down(1), false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20, CREATE_TEAM_MENU, -1 , MenuNav::UpDown(0, 5)),

        button("4_player", "4 PLAYER", KEYSTATE_4, 152,125,68,20, SET_PLAYER_MODE, 4 , MenuNav::UpDownLeft(4, 6, 3)),
        button("3_player", "3 PLAYER", KEYSTATE_3, 80,125,68,20, SET_PLAYER_MODE,3 , MenuNav::UpDownRight(5, 6, 2)),
        button("2_player", "2 PLAYER", KEYSTATE_2, 152,100,68,20, SET_PLAYER_MODE,2 , MenuNav::UpDownLeft(1, 2, 5)),
        button("1_player", "1 PLAYER", KEYSTATE_1, 80,100,68,20, SET_PLAYER_MODE,1 , MenuNav::UpDownRight(1, 3, 4)),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 148, 140, 10, SET_DIFFICULTY, -1, MenuNav::UpDown(3, 7)),

        button("pvp_allied", "PVP: Allied", KEYSTATE_UNKNOWN, 80, 160, 68, 10, ALLIED_MODE, -1, MenuNav::UpDownRight(6, 9, 8)),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 152, 160, 68, 10, DO_LEVEL_EDIT, -1, MenuNav::UpDownLeft(6, 9, 7)),

        button("help", "HELP", KEYSTATE_UNKNOWN, 120, 175, 60, 20, SHOW_HELP, -1, MenuNav::UpLeft(7, 10)),
        button("options", "", KEYSTATE_UNKNOWN, 90, 175, 20, 20, MAIN_OPTIONS, -1, MenuNav::UpRight(7, 9))
    };
#define OPTIONS_BUTTON_INDEX 10

#else // Native build
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20, BEGINMENU, 1 , MenuNav::Down(1), false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20, CREATE_TEAM_MENU, -1 , MenuNav::UpDown(0, 5)),

        button("4_player", "4 PLAYER", KEYSTATE_4, 152,125,68,20, SET_PLAYER_MODE, 4 , MenuNav::UpDownLeft(4, 6, 3)),
        button("3_player", "3 PLAYER", KEYSTATE_3, 80,125,68,20, SET_PLAYER_MODE,3 , MenuNav::UpDownRight(5, 6, 2)),
        button("2_player", "2 PLAYER", KEYSTATE_2, 152,100,68,20, SET_PLAYER_MODE,2 , MenuNav::UpDownLeft(1, 2, 5)),
        button("1_player", "1 PLAYER", KEYSTATE_1, 80,100,68,20, SET_PLAYER_MODE,1 , MenuNav::UpDownRight(1, 3, 4)),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 148, 140, 10, SET_DIFFICULTY, -1, MenuNav::UpDown(3, 7)),

        button("pvp_allied", "PVP: Allied", KEYSTATE_UNKNOWN, 80, 160, 68, 10, ALLIED_MODE, -1, MenuNav::UpDownRight(6, 9, 8)),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 152, 160, 68, 10, DO_LEVEL_EDIT, -1, MenuNav::UpDownLeft(6, 9, 7)),

        button("quit", "QUIT ", KEYSTATE_ESCAPE, 120, 175, 60, 20, QUIT_MENU, 0 , MenuNav::UpLeft(7, 10)),
        button("options", "", KEYSTATE_UNKNOWN, 90, 175, 20, 20, MAIN_OPTIONS, -1, MenuNav::UpRight(7, 9))
    };
#define OPTIONS_BUTTON_INDEX 10
#endif // __EMSCRIPTEN__

#else // DISABLE_MULTIPLAYER

#ifdef __EMSCRIPTEN__
// Web build without multiplayer: Replace QUIT with HELP
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 70, 140, 20, BEGINMENU, 1 , MenuNav::Down(1), false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 95, 140, 20, CREATE_TEAM_MENU, -1 , MenuNav::UpDown(0, 2)),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 120, 140, 15, SET_DIFFICULTY, -1, MenuNav::UpDown(1, 3)),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 137, 140, 15, DO_LEVEL_EDIT, -1, MenuNav::UpDown(2, 4)),
        button("help", "HELP", KEYSTATE_UNKNOWN, 120, 154, 60, 20, SHOW_HELP, -1, MenuNav::UpLeft(3, 5)),
        button("options", "", KEYSTATE_UNKNOWN, 90, 154, 20, 20, MAIN_OPTIONS, -1, MenuNav::UpRight(3, 4))
    };
#define OPTIONS_BUTTON_INDEX 5

#else // Native build without multiplayer
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 70, 140, 20, BEGINMENU, 1 , MenuNav::Down(1), false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 95, 140, 20, CREATE_TEAM_MENU, -1 , MenuNav::UpDown(0, 2)),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 120, 140, 15, SET_DIFFICULTY, -1, MenuNav::UpDown(1, 3)),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 137, 140, 15, DO_LEVEL_EDIT, -1, MenuNav::UpDown(2, 4)),
        button("quit", "QUIT ", KEYSTATE_ESCAPE, 120, 154, 60, 20, QUIT_MENU, 0, MenuNav::UpLeft(3, 5)),
        button("options", "", KEYSTATE_UNKNOWN, 90, 154, 20, 20, MAIN_OPTIONS, -1, MenuNav::UpRight(3, 4))
    };
#define OPTIONS_BUTTON_INDEX 5
#endif // __EMSCRIPTEN__

#endif // DISABLE_MULTIPLAYER


#define BUTTON_HEIGHT 15
#define BUTTON_PADDING 8
#define BUTTON_PITCH (BUTTON_HEIGHT + BUTTON_PADDING)

button main_options_buttons[] =
{
    button("options_back", "BACK", KEYSTATE_ESCAPE, 40, 10, 50, 15, RETURN_MENU, EXIT, MenuNav::UpDownRight(12, 1, 14)),
    button("toggle_sound", "Sound", KEYSTATE_UNKNOWN, 135, 10 + BUTTON_PITCH, 50, 15, TOGGLE_SOUND, -1, MenuNav::UpDown(0, 2)),
    button("toggle_rendering", "NORMAL", KEYSTATE_UNKNOWN, 130, 10 + 2*BUTTON_PITCH, 60, 15, TOGGLE_RENDERING_ENGINE, -1, MenuNav::UpDownRight(1, 4, 3)),
    button("toggle_fullscreen", "Fullscreen", KEYSTATE_UNKNOWN, 210, 10 + 2*BUTTON_PITCH, 90, 15, TOGGLE_FULLSCREEN, -1, MenuNav::UpDownLeft(1, 5, 2)),
    button("overscan_minus", "- ", KEYSTATE_UNKNOWN, 130, 10 + 3*BUTTON_PITCH, 30, 15, OVERSCAN_ADJUST, -1, MenuNav::UpDownRight(2, 6, 5)),
    button("overscan_plus", "+ ", KEYSTATE_UNKNOWN, 170, 10 + 3*BUTTON_PITCH, 30, 15, OVERSCAN_ADJUST, 1, MenuNav::UpDownLeft(3, 7, 4)),
    button("toggle_mini_hp_bar", "Mini HP bar", KEYSTATE_UNKNOWN, 80, 10 + 4*BUTTON_PITCH, 90, 15, TOGGLE_MINI_HP_BAR, -1, MenuNav::UpDownRight(4, 8, 7)),
    button("toggle_hit_flash", "Hit flash", KEYSTATE_UNKNOWN, 210, 10 + 4*BUTTON_PITCH, 90, 15, TOGGLE_HIT_FLASH, -1, MenuNav::UpDownLeft(5, 9, 6)),
    button("toggle_hit_recoil", "Hit recoil", KEYSTATE_UNKNOWN, 80, 10 + 5*BUTTON_PITCH, 90, 15, TOGGLE_HIT_RECOIL, -1, MenuNav::UpDownRight(6, 10, 9)),
    button("toggle_attack_lunge", "Attack lunge", KEYSTATE_UNKNOWN, 210, 10 + 5*BUTTON_PITCH, 90, 15, TOGGLE_ATTACK_LUNGE, -1, MenuNav::UpDownLeft(7, 11, 8)),
    button("toggle_hit_sparks", "Hit sparks", KEYSTATE_UNKNOWN, 80, 10 + 6*BUTTON_PITCH, 90, 15, TOGGLE_HIT_ANIM, -1, MenuNav::UpDownRight(8, 12, 11)),
    button("toggle_damage_numbers", "Damage numbers", KEYSTATE_UNKNOWN, 210, 10 + 6*BUTTON_PITCH, 90, 15, TOGGLE_DAMAGE_NUMBERS, -1, MenuNav::UpDownLeft(9, 13, 10)),
    button("toggle_heal_numbers", "Healing numbers", KEYSTATE_UNKNOWN, 80, 10 + 7*BUTTON_PITCH, 90, 15, TOGGLE_HEAL_NUMBERS, -1, MenuNav::UpDownRight(10, 0, 13)),
    button("toggle_gore", "Gore", KEYSTATE_UNKNOWN, 210, 10 + 7*BUTTON_PITCH, 90, 15, TOGGLE_GORE, -1, MenuNav::UpDownLeft(11, 0, 12)),
    button("restore_defaults", "RESTORE DEFAULTS", KEYSTATE_UNKNOWN, 170, 10, 120, 15, RESTORE_DEFAULT_SETTINGS, -1, MenuNav::UpDownLeft(12, 1, 0)),
};

// beginmenu (first menu of new game), create_team_menu
button createmenu_buttons[] =
    {
        button("view_team", "VIEW TEAM", KEYSTATE_UNKNOWN, 30, 70, 80, 15, CREATE_VIEW_MENU, -1, MenuNav::DownRight(3, 1)),
        button("train_team", "TRAIN TEAM", KEYSTATE_UNKNOWN, 120, 70, 80, 15, CREATE_TRAIN_MENU, -1, MenuNav::DownLeftRight(4, 0, 2)),
        button("hire_troops", "HIRE TROOPS",  KEYSTATE_UNKNOWN, 210, 70, 80, 15, CREATE_HIRE_MENU, -1, MenuNav::DownLeft(5, 1)),
        button("load_team", "LOAD TEAM", KEYSTATE_UNKNOWN, 30, 100, 80, 15, CREATE_LOAD_MENU, -1, MenuNav::UpDownRight(0, 6, 4)),
        button("save_team", "SAVE TEAM", KEYSTATE_UNKNOWN, 120, 100, 80, 15, CREATE_SAVE_MENU, -1, MenuNav::UpLeftRight(1, 3, 5)),
        button("go", "GO", KEYSTATE_UNKNOWN,        210, 100, 80, 15, GO_MENU, -1, MenuNav::UpDownLeft(2, 8, 4)),

        button("back", "BACK", KEYSTATE_ESCAPE, 30, 140, 60, 30, RETURN_MENU, EXIT, MenuNav::UpRight(3, 7)),
        button("progress", "PROGRESS", KEYSTATE_UNKNOWN, 120, 140, 80, 20, CREATE_PROGRESS_MENU, -1, MenuNav::UpLeftRight(4, 6, 8)),
        button("set_level", "SET LEVEL", KEYSTATE_UNKNOWN, 210, 140, 80, 20, DO_SET_SCEN_LEVEL, EXIT, MenuNav::UpDownLeft(5, 9, 7)),
        button("set_campaign", "SET CAMPAIGN", KEYSTATE_UNKNOWN, 210, 170, 80, 20, DO_PICK_CAMPAIGN, EXIT, MenuNav::UpLeft(8, 7)),

    };

button viewteam_buttons[] =
    {
        //  button("TRAIN", KEYSTATE_e, 85, 170, 60, 20, CREATE_TRAIN_MENU, -1},
        //  button("HIRE",  KEYSTATE_b, 190, 170, 60, 20, CREATE_HIRE_MENU, -1},
        button("go", "GO", KEYSTATE_UNKNOWN,        270, 170, 40, 20, GO_MENU, -1, MenuNav::Left(1)),
        button("back", "BACK", KEYSTATE_ESCAPE,    10, 170, 44, 20, RETURN_MENU , EXIT, MenuNav::Right(0)),

    };

button details_buttons[] =
    {
        button("back", "BACK", KEYSTATE_ESCAPE, 10, 170, 40, 20, RETURN_MENU , EXIT, MenuNav::UpRight(1, 1)),
        button("promote", 160, 4, 315 - 160, 66 - 4, 0 , -1, MenuNav::DownLeft(0, 0), false, true) // PROMOTE
    };

button trainmenu_buttons[] =
    {
        button("prev", "PREV", KEYSTATE_UNKNOWN,  10, 40, 40, 20, CYCLE_TEAM_GUY, -1, MenuNav::DownRight(2, 1)),
        button("next", "NEXT", KEYSTATE_UNKNOWN,  110, 40, 40, 20, CYCLE_TEAM_GUY, 1, MenuNav::DownLeftRight(3, 0, 16)),
        button("dec_str", "", KEYSTATE_UNKNOWN,  16, 70, 16, 10, DECREASE_STAT, BUT_STR, MenuNav::UpDownRight(0, 4, 3)),
        button("inc_str", "", KEYSTATE_UNKNOWN,  126, 70, 16, 12, INCREASE_STAT, BUT_STR, MenuNav::UpDownLeft(1, 5, 2)),
        button("dec_dex", "", KEYSTATE_UNKNOWN,  16, 85, 16, 10, DECREASE_STAT, BUT_DEX, MenuNav::UpDownRight(2, 6, 5)),
        button("inc_dex", "", KEYSTATE_UNKNOWN,  126, 85, 16, 12, INCREASE_STAT, BUT_DEX, MenuNav::UpDownLeft(3, 7, 4)),
        button("dec_con", "", KEYSTATE_UNKNOWN,  16, 100, 16, 10, DECREASE_STAT, BUT_CON, MenuNav::UpDownRight(4, 8, 7)),
        button("inc_con", "", KEYSTATE_UNKNOWN,  126,100, 16, 12, INCREASE_STAT, BUT_CON, MenuNav::UpDownLeft(5, 9, 6)),
        button("dec_int", "", KEYSTATE_UNKNOWN,  16, 115, 16, 10, DECREASE_STAT, BUT_INT, MenuNav::UpDownRight(6, 10, 9)),
        button("inc_int", "", KEYSTATE_UNKNOWN,  126, 115, 16, 12, INCREASE_STAT, BUT_INT, MenuNav::UpDownLeft(7, 11, 8)),
        button("dec_armor", "", KEYSTATE_UNKNOWN,  16, 130, 16, 10, DECREASE_STAT, BUT_ARMOR, MenuNav::UpDownRight(8, 12, 11)),
        button("inc_armor", "", KEYSTATE_UNKNOWN,  126, 130, 16, 12, INCREASE_STAT, BUT_ARMOR, MenuNav::UpDownLeft(9, 13, 10)),
        button("dec_level", "", KEYSTATE_UNKNOWN,  16, 145, 16, 10, DECREASE_STAT, BUT_LEVEL, MenuNav::UpDownRight(10, 19, 13)),
        button("inc_level", "", KEYSTATE_UNKNOWN,  126, 145, 16, 12, INCREASE_STAT, BUT_LEVEL, MenuNav::UpDownLeftRight(11, 15, 12, 18)),
        button("view_team", "VIEW TEAM", KEYSTATE_UNKNOWN,  190, 170, 90, 20, CREATE_VIEW_MENU, -1, MenuNav::UpLeft(18, 15)),
        button("accept", "ACCEPT", KEYSTATE_UNKNOWN,  80, 170, 80, 20, EDIT_GUY, -1, MenuNav::UpLeftRight(13, 19, 14)),
        button("rename", "RENAME", KEYSTATE_UNKNOWN, 174,  8, 64, 22, NAME_GUY, 1, MenuNav::DownLeftRight(18, 1, 17)),
        button("details", "DETAILS..", KEYSTATE_UNKNOWN, 240, 8, 64, 22, CREATE_DETAIL_MENU, 0, MenuNav::DownLeft(18, 16)),
        button("change_team", "Playing on Team X", KEYSTATE_UNKNOWN, 174, 138, 133, 22, CHANGE_TEAM, 1, MenuNav::UpDownLeft(17, 14, 13)),
        button("back", "BACK", KEYSTATE_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT, MenuNav::UpRight(12, 15)),

    };

button hiremenu_buttons[] =
    {
        button("prev", "PREV", KEYSTATE_UNKNOWN,  10, 40, 40, 20, CYCLE_GUY, -1, MenuNav::DownRight(4, 1)),
        button("next", "NEXT", KEYSTATE_UNKNOWN,  110, 40, 40, 20, CYCLE_GUY, 1, MenuNav::DownLeftRight(3, 0, 3)),
        button("change_hire_team", "hiring for team X", KEYSTATE_UNKNOWN, 190, 170, 110, 20, CHANGE_HIRE_TEAM, 1, MenuNav::UpLeft(1, 3)),
        button("hire_me", "HIRE ME", KEYSTATE_UNKNOWN,  82, 166, 88, 28, ADD_GUY, -1, MenuNav::UpLeftRight(1, 4, 2)),
        button("back", "BACK", KEYSTATE_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT, MenuNav::UpRight(0, 3)),

    };


button saveteam_buttons[] =
    {
        button("save_slot_1", "SLOT ONE", KEYSTATE_UNKNOWN,  25, 25, 220, 10, DO_SAVE, 1, MenuNav::UpDown(10, 1)),
        button("save_slot_2", "SLOT TWO", KEYSTATE_UNKNOWN,  25, 40, 220, 10, DO_SAVE, 2, MenuNav::UpDown(0, 2)),
        button("save_slot_3", "SLOT THREE", KEYSTATE_UNKNOWN,25, 55, 220, 10, DO_SAVE, 3, MenuNav::UpDown(1, 3)),
        button("save_slot_4", "SLOT FOUR", KEYSTATE_UNKNOWN, 25, 70, 220, 10, DO_SAVE, 4, MenuNav::UpDown(2, 4)),
        button("save_slot_5", "SLOT FIVE", KEYSTATE_UNKNOWN, 25, 85, 220, 10, DO_SAVE, 5, MenuNav::UpDown(3, 5)),
        button("save_slot_6", "SLOT Six", KEYSTATE_UNKNOWN, 25, 100, 220, 10, DO_SAVE,  6, MenuNav::UpDown(4, 6)),
        button("save_slot_7", "SLOT Seven", KEYSTATE_UNKNOWN, 25, 115, 220, 10, DO_SAVE, 7, MenuNav::UpDown(5, 7)),
        button("save_slot_8", "SLOT Eight", KEYSTATE_UNKNOWN, 25, 130, 220, 10, DO_SAVE, 8, MenuNav::UpDown(6, 8)),
        button("save_slot_9", "SLOT Nine", KEYSTATE_UNKNOWN, 25, 145, 220, 10, DO_SAVE, 9, MenuNav::UpDown(7, 9)),
        button("save_slot_10", "SLOT Ten", KEYSTATE_UNKNOWN, 25, 160, 220, 10, DO_SAVE, 10, MenuNav::UpDown(8, 10)),
        button("back", "BACK", KEYSTATE_ESCAPE,25, 175, 40, 20, RETURN_MENU , EXIT, MenuNav::UpDown(9, 0)),

    };

button loadteam_buttons[] =
    {
        button("load_slot_1", "SLOT ONE", KEYSTATE_UNKNOWN,  25, 25, 220, 10, DO_LOAD, 1, MenuNav::UpDown(10, 1)),
        button("load_slot_2", "SLOT TWO", KEYSTATE_UNKNOWN,  25, 40, 220, 10, DO_LOAD, 2, MenuNav::UpDown(0, 2)),
        button("load_slot_3", "SLOT THREE", KEYSTATE_UNKNOWN,25, 55, 220, 10, DO_LOAD, 3, MenuNav::UpDown(1, 3)),
        button("load_slot_4", "SLOT FOUR", KEYSTATE_UNKNOWN, 25, 70, 220, 10, DO_LOAD, 4, MenuNav::UpDown(2, 4)),
        button("load_slot_5", "SLOT FIVE", KEYSTATE_UNKNOWN, 25, 85, 220, 10, DO_LOAD, 5, MenuNav::UpDown(3, 5)),
        button("load_slot_6", "SLOT Six", KEYSTATE_UNKNOWN, 25, 100, 220, 10, DO_LOAD,  6, MenuNav::UpDown(4, 6)),
        button("load_slot_7", "SLOT Seven", KEYSTATE_UNKNOWN, 25, 115, 220, 10, DO_LOAD, 7, MenuNav::UpDown(5, 7)),
        button("load_slot_8", "SLOT Eight", KEYSTATE_UNKNOWN, 25, 130, 220, 10, DO_LOAD, 8, MenuNav::UpDown(6, 8)),
        button("load_slot_9", "SLOT Nine", KEYSTATE_UNKNOWN, 25, 145, 220, 10, DO_LOAD, 9, MenuNav::UpDown(7, 9)),
        button("load_slot_10", "SLOT Ten", KEYSTATE_UNKNOWN, 25, 160, 220, 10, DO_LOAD, 10, MenuNav::UpDown(8, 10)),
        button("back", "BACK", KEYSTATE_ESCAPE,25, 175, 40, 20, RETURN_MENU , EXIT, MenuNav::UpDown(9, 0)),

    };


void view_team(short left, short top, short right, short bottom)
{
	char text_down = top+3;
	int i;
	std::string message;
	char namecolor, numguys = 0;
	text& mytext = myscreen->text_normal;

	myscreen->redrawme = 1;
	myscreen->draw_button(left, top, right, bottom, 2, 1);

	mytext.write_xy(left+5, text_down, "  Name  ", static_cast<unsigned char>(BLACK), 1);

	mytext.write_xy(left+80, text_down, "STR  DEX  CON  INT  ARM", static_cast<unsigned char>(BLACK), 1);

	mytext.write_xy(left+230, text_down, "Level", static_cast<unsigned char>(BLACK), 1);

	text_down+=6;

	for(i=0; i < myscreen->save_data.team_size; i++)
	{
	    auto& ourteam = myscreen->save_data.team_list;
		if (ourteam[i])
		{
			numguys++;

			// Pick a nice dark color based on family type
			namecolor = ((ourteam[i]->family +1) << 4) & 255;
			mytext.write_xy(left+5, text_down, ourteam[i]->name.c_str(), static_cast<unsigned char>(namecolor), 1);

			message = std::format("{:4d} {:4d} {:4d} {:4d} {:4d}",
			         ourteam[i]->strength, ourteam[i]->dexterity,
			         ourteam[i]->constitution, ourteam[i]->intelligence,
			         ourteam[i]->armor);
			mytext.write_xy(left+70, text_down, message.c_str(), static_cast<unsigned char>(BLACK), 1);

			message = std::format("{:2d}", ourteam[i]->level);
			mytext.write_xy(left+235, text_down, message.c_str(), static_cast<unsigned char>(BLACK), 1);

			mytext.write_xy(left+260, text_down, family_name_copy(ourteam[i]->family), static_cast<unsigned char>(namecolor), 1);

			text_down+=6;
		}
	}
	if (numguys == 0)
	{
		mytext.write_xy(left+80, 60, "*** YOU HAVE NO TEAM! ***", static_cast<unsigned char>(ORANGE_START), 1);
	}

	return;
}



void draw_version_number()
{
	text& mytext = myscreen->text_normal;

	myscreen->redrawme = 1;
	int w = static_cast<int>(std::string(OPENGLAD_VERSION_STRING).size())*6;
	int h = 8;
	int x = 320 - w - 80;
	int y = 200 - 12;
	myscreen->fastbox(x, y, w, h, PURE_BLACK);
	mytext.write_xy(x, y, OPENGLAD_VERSION_STRING, static_cast<unsigned char>(DARK_BLUE), 1);
}



const char* get_family_string(short family)
{
	switch(family)
	{
		case FAMILY_ARCHER:
			return "ARCHER";
		case FAMILY_CLERIC:
			return "CLERIC";
		case FAMILY_DRUID:
			return "DRUID";
		case FAMILY_ELF:
			return "ELF";
		case FAMILY_MAGE:
			return "MAGE";
		case FAMILY_SOLDIER:
			return "SOLDIER";
		case FAMILY_THIEF:
			return "THIEF";
		case FAMILY_ARCHMAGE:
			return "ARCHMAGE";
		case FAMILY_ORC:
			return "ORC";
		case FAMILY_BIG_ORC:
			return "ORC CAPTAIN";
		case FAMILY_BARBARIAN:
			return "BARBARIAN";
		case FAMILY_FIREELEMENTAL:
			return "ELEMENTAL";
		case FAMILY_SKELETON:
			return "SKELETON";
		case FAMILY_SLIME:
		case FAMILY_MEDIUM_SLIME:
		case FAMILY_SMALL_SLIME:
			return "SLIME";
		case FAMILY_FAERIE:
			return "FAERIE";
		case FAMILY_GHOST:
			return "GHOST";
		default:
			return "BEAST";
	}
}


const char* family_name_copy(short family)
{
	switch(family)
	{
		case FAMILY_ARCHER:    return "ARCHER";
		case FAMILY_CLERIC:    return "CLERIC";
		case FAMILY_DRUID:     return "DRUID";
		case FAMILY_ELF:       return "ELF";
		case FAMILY_MAGE:      return "MAGE";
		case FAMILY_SOLDIER:   return "SOLDIER";
		case FAMILY_THIEF:     return "THIEF";
		case FAMILY_ARCHMAGE:  return "ARCHMAGE";
		case FAMILY_ORC:       return "ORC";
		case FAMILY_BIG_ORC:   return "ORC CAP.";
		case FAMILY_BARBARIAN: return "BARBAR.";
		default:               return "BEAST";
	}
}


void quit(Sint32 arg1)
{
#ifdef TESTING
	TRACE("picker", "quit called (test mode - not exiting)");
#else
	myscreen->refresh();

	if (ctx().prefs)
	{
		delete ctx().prefs;
		ctx().prefs = nullptr;
	}
	picker_quit();  // deletes the screen objects
	Log("quit({})\n", arg1);
	exit(0);
#endif
}

void draw_toggle_effect_button(button& b, const std::string& category, const std::string& setting)
{
    if(b.hidden || b.no_draw)
        return;
    
    if(active_config().is_on(category, setting))
        myscreen->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, LIGHT_GREEN);
    else
        myscreen->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, RED);
    
    text& mytext = myscreen->text_normal;
    mytext.write_xy_center(b.x + b.sizex/2, b.y + b.sizey/2 - 3, DARK_BLUE, "%s", b.label.c_str());
}

Sint32 main_options()
{
    text& mytext = myscreen->text_normal;
    
	if(localbuttons != nullptr)
		delete localbuttons; //we'll make a new set
    
    #if defined(OUYA) || defined(ANDROID)
    main_options_buttons[3].hidden = main_options_buttons[3].no_draw = true;
    main_options_buttons[2].nav.right = -1;
    main_options_buttons[5].nav.up = 2;
    #endif
    
	button* buttons = main_options_buttons;
	int num_buttons = ARRAY_SIZE(main_options_buttons);
	int highlighted_button = 0;
	localbuttons = init_buttons(buttons, num_buttons);

	clear_keyboard();
    
    Sint32 retvalue = 0;
	while(!(retvalue & EXIT))
	{
	    // Input
		if(leftmouse(buttons))
        {
			if(localbuttons->leftclick() == EXIT)
                break;
        }
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        if(retvalue == EXIT)
            break;
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
        buttons[2].label = active_config().get_setting("graphics", "render");
        allbuttons[2]->label = buttons[2].label;
		
		// Draw
		myscreen->clear_window();  // Clearing entire window because the overscan may have been adjusted.
		
		myscreen->draw_button(0, 0, 320, 200, 0);
		myscreen->draw_button_inverted(4, 4, 312, 192);
		
        
        draw_buttons(buttons, num_buttons);
        
		draw_toggle_effect_button(buttons[1], "sound", "sound");
		myscreen->hor_line(60, buttons[2].y - BUTTON_PADDING/2, 200, PURE_WHITE);
		
		mytext.write_xy(20, buttons[2].y + 3, DARK_BLUE, "Rendering engine:");
		mytext.write_xy(20, buttons[2].y + 3 + 10, DARK_BLUE, " (needs restart)");
		draw_toggle_effect_button(buttons[3], "graphics", "fullscreen");
		mytext.write_xy(20, buttons[4].y + 3, DARK_BLUE, "Overscan adjust:");
		myscreen->hor_line(60, buttons[6].y - BUTTON_PADDING/2, 200, PURE_WHITE);
		
		mytext.write_xy(20, buttons[6].y + 3, DARK_BLUE, "Effects:");
		draw_toggle_effect_button(buttons[6], "effects", "mini_hp_bar");
		draw_toggle_effect_button(buttons[7], "effects", "hit_flash");
		draw_toggle_effect_button(buttons[8], "effects", "hit_recoil");
		draw_toggle_effect_button(buttons[9], "effects", "attack_lunge");
		draw_toggle_effect_button(buttons[10], "effects", "hit_anim");
		draw_toggle_effect_button(buttons[11], "effects", "damage_numbers");
		draw_toggle_effect_button(buttons[12], "effects", "heal_numbers");
		draw_toggle_effect_button(buttons[13], "effects", "gore");
        
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
	myscreen->soundp->set_sound(!active_config().is_on("sound", "sound"));
	active_config().save_settings();
    
    return REDRAW;
}

Sint32 overscan_adjust(Sint32 arg)
{
    overscan_percentage -= arg/100.0f;
    update_overscan_setting();
    
    return REDRAW;
}

Sint32 set_player_mode(Sint32 howmany)
{
	Sint32 count = 0;
	myscreen->save_data.numplayers = howmany;

	while (allbuttons[count])
	{
		allbuttons[count]->vdisplay();
		count++;
	}
	//buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

	return OK;
}


//new functions
Sint32 return_menu(Sint32 arg)
{
   return arg;
}

Sint32 create_detail_menu(guy *arg1)
{
#define DETAIL_LM 11             // left edge margin ..
#define DETAIL_MM 164            // center margin
#define DETAIL_LD(x) (90+(x*6))  // vertical line for text
#define WL(p,m) if (m[1] != ' ') mytext.write_xy(DETAIL_LM, DETAIL_LD(p), m, RED, 1); else mytext.write_xy(DETAIL_LM, DETAIL_LD(p), m, DARK_BLUE, 1)
#define WR(p,m) if (m[1] != ' ') mytext.write_xy(DETAIL_MM, DETAIL_LD(p), m, RED, 1); else mytext.write_xy(DETAIL_MM, DETAIL_LD(p), m, DARK_BLUE, 1)

   Sint32 retvalue = 0;
   guy *thisguy;
   Sint32 start_time = query_timer();

   if (arg1)
       thisguy = arg1;
   else
       thisguy = myscreen->save_data.team_list[editguy].get();

   release_mouse();

   if (localbuttons)
       delete localbuttons;
    
	button* buttons = details_buttons;
	int num_buttons = 2;
	int highlighted_button = 0;
	
	buttons[1].hidden = !(thisguy->family == FAMILY_MAGE && thisguy->level >= 6) && !(thisguy->family == FAMILY_ORC && thisguy->level >= 5);
	localbuttons = init_buttons(buttons, num_buttons);

   //leftmouse(buttons);
   //localbuttons->leftclick(buttons);

   while ( !(retvalue & EXIT) )
   {
       show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
    
       bool pressed = handle_menu_nav(buttons, highlighted_button, retvalue);
       
       MouseState& detailmouse = query_mouse();
       bool do_click = false;
       if(leftmouse(buttons))
       {
           do_click = true;
       }
       
       bool do_promote = !buttons[1].hidden && ((do_click && detailmouse.x >= 160 &&
                   detailmouse.x <= 315 &&
                   detailmouse.y >= 4   &&
                   detailmouse.y <= 66) || (pressed && highlighted_button == 1));
       if(do_promote)
       {
           if (thisguy->family == FAMILY_MAGE &&
                   thisguy->level >= 6)
           {
               // Become an archmage!
               thisguy->upgrade_to_level(( (thisguy->level-6) / 2) + 1);
               thisguy->family = FAMILY_ARCHMAGE;
               myscreen->soundp->play_sound(SOUND_EXPLODE);
               myscreen->soundp->play_sound(SOUND_EXPLODE);
               myscreen->soundp->play_sound(SOUND_EXPLODE);
               return REDRAW;
           }  // end of mage->archmage
           else if (thisguy->family == FAMILY_ORC &&
                    thisguy->level >= 5)
           {
               // Become an Orcish Captain!
               thisguy->upgrade_to_level(1);
               thisguy->family = FAMILY_BIG_ORC; // fake for now
               myscreen->soundp->play_sound(SOUND_DIE1);
               myscreen->soundp->play_sound(SOUND_DIE2);
               myscreen->soundp->play_sound(SOUND_DIE1);
               return REDRAW;
           } // end of orc->orc-captain
       }
        
        if(do_click)
            retvalue=localbuttons->leftclick(buttons);
       
       
    
        draw_backdrop();

       myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
       myscreen->draw_text_bar(36, 10, 124, 22);
       
       text& mytext = myscreen->text_normal;
       mytext.write_xy(80 - mytext.query_width(current_guy->name.c_str())/2, 14,
                        current_guy->name.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
       myscreen->draw_dialog(5, 68, 315, 167, "Character Special Abilities");
       myscreen->draw_text_bar(160, 90, 162, 160);

       // Text stuff, determined by character class & level
       switch (thisguy->family)
       {
           case FAMILY_SOLDIER:
               message = std::format("Level {} soldier has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things (charge)
               WL(2, " Charge");
               WL(3, "  Charge causes you to ");
               WL(4, "  run forward, damaging");
               WL(5, "  anything in your way.");
               // Level 4 things (boomerang)
               if (thisguy->level >= 4)
               {
                   WL(7, " Boomerang");
                   WL(8, "  The boomerang flies  ");
                   WL(9, "  out in a spiral,     ");
                   WL(10,"  hurting nearby foes. ");
               }
               // Level 7 things (whirl)
               if (thisguy->level >= 7)
               {
                   WR(0, " Whirl    ");
                   WR(1, "  The fighter whirls in");
                   WR(2, "  a spiral, hurting or ");
                   WR(3 ,"  stunning melee foes. ");
               }
               // Level 10 things (disarm)
               if (thisguy->level >= 10)
               {
                   WR(5, " Disarm   ");
                   WR(6, "  Cause a melee foe to ");
                   WR(7, "  temporarily lose the ");
                   WR(8 ,"  strength of attacks. ");
               }
               break;
           case FAMILY_BARBARIAN:
               message = std::format("Level {} barbarian has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things (hurl boulder)
               WL(2, " Hurl Boulder");
               WL(3, "  Throw a massive stone");
               WL(4, "  boulder at your      ");
               WL(5, "  enemies.             ");
               // Level 4 things (exploding boulder)
               if (thisguy->level >= 4)
               {
                   WL(7, " Exploding Boulder");
                   WL(8, "  Hurl a boulder so hard ");
                   WL(9, "  that it explodes and   ");
                   WL(10,"  hits foes all around.  ");
               }
               break;
           case FAMILY_ELF:
               message = std::format("Level {} elf has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things (rocks)
               WL(2, " Rocks/Forestwalk");
               WL(3, "  Rocks hurls a few rocks");
               WL(4, "  at the enemy.  Forest- ");
               WL(5, "  walk, dexterity-based, ");
               WL(6, "  lets you move in trees.");
               // Level 4 things (more rocks)
               if (thisguy->level >= 4)
               {
                   WL(7, " More Rocks");
                   WL(8, "  Like #1, but these    ");
                   WL(9, "  rocks bounce off walls");
                   WL(10,"  and other barricades. ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Lots of Rocks");
                   WR(1, "  Like #2, but more     ");
                   WR(2, "  rocks, with a longer  ");
                   WR(3 ,"  thrown range.         ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, " MegaRocks");
                   WR(6, "  This giant handful of ");
                   WR(7, "  rocks bounces far away");
                   WR(8 ,"  and packs a big punch.");
               }
               break;
           case FAMILY_ARCHER:
               message = std::format("Level {} archer has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Fire Arrows     ");
               WL(3, "  An archer can spin in a");
               WL(4, "  circle, firing off a   ");
               WL(5, "  ring of flaming bolts. ");
               //WL(6, "  lets you move in trees.");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " Barrage   ");
                   WL(8, "  Rather than a single  ");
                   WL(9, "  bolt, the archer sends");
                   WL(10,"  3 deadly bolts ahead. ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Exploding Bolt");
                   WR(1, "  This fatal bolt will  ");
                   WR(2, "  explode on contact,   ");
                   WR(3 ,"  dealing death to all. ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, "          ");
                   WR(6, "                        ");
                   WR(7, "                        ");
                   WR(8 ,"                        ");
               }
               break;
           case FAMILY_MAGE:
               message = std::format("Level {} Mage has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Teleport/Marker ");
               WL(3, "  Any mage can teleport  ");
               WL(4, "  randomly away easily.  ");
               WL(5, "  Leaving a marker for   ");
               WL(6, "  anchor requires 75 int.");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " Warp Space");
                   WL(8, "  Twist the fabric of   ");
                   WL(9, "  space around you to   ");
                   WL(10,"  deal death to enemies.");
               }
               // Can we change to archmage?
               if (thisguy->level >= 6)
               {
                   message = std::format("Level {} Archmage. This",
                           (thisguy->level-6)/2+1);
                   myscreen->draw_dialog(158, 4, 315, 66, "Become ArchMage");
                   WR(-10,"Your Mage is now of high");
                   WR( -9,"enough level to become a");
                   //WR( -8,"Level 1 Archmage. This  ");
                   WR(-8, message.c_str());
                   WR( -7,"change CANNOT be undone!");
                   WR( -6," Click here to change.  ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Freeze Time   ");
                   WR(1, "  Freeze time for all   ");
                   WR(2, "  but your team and kill");
                   WR(3 ,"  enemies with ease.    ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(4, " Energy Wave");
                   WR(5, "  Send a growing ripple ");
                   WR(6, "  of energy through     ");
                   WR(7 ,"  walls and foes.       ");
               }
               // Level 13 things
               if (thisguy->level >= 13)
               {
                   WR(8, " HeartBurst  ");
                   WR(9, "  Burst your enemies    ");
                   WR(10,"  into flame. More magic");
                   WR(11,"  means a bigger effect.");
               }
               break;
           case FAMILY_ARCHMAGE:
               message = std::format("Level {} ArchMage has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Teleport/Marker ");
               WL(3, "  Any mage can teleport  ");
               WL(4, "  randomly away easily.  ");
               WL(5, "  Leaving a marker for   ");
               WL(6, "  anchor requires 75 int.");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " HeartBurst/Lightning");
                   WL(8, "  Burst your enemies    ");
                   WL(9, "  into flame around you.");
                   WL(10,"  ALT: Chain lightning  ");
                   WL(11,"  bounces through foes. ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Summon Image/Sum. Elem.");
                   WR(1, "  Summon an illusionary ");
                   WR(2, "  ally to fight for you.");
                   WR(3 ,"  ALT: Summon a daemon, ");
                   WR(4 ,"  who uses your stamina.");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, " Mind Control");
                   WR(6,"  Convert nearby foes to");
                   WR(7,"  your team, for a time.");
               }
               break;

           case FAMILY_CLERIC:
               message = std::format("Level {} Cleric has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Heal            ");
               WL(3, "  Heal all teammates who ");
               WL(4, "  are close to you, for  ");
               WL(5, "  as much as you have SP.");
               //WL(6, "  lets you move in trees.");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " Raise/Turn Undead");
                   WL(8, "  Raise the gore of any ");
                   WL(9, "  victim to a skeleton. ");
                   WL(10,"  Alternate (turning)   ");
                   WL(11,"  requires 65 Int.      ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Raise/Turn Ghost");
                   WR(1, "  A more powerful raise,");
                   WR(2, "  you can now get ghosts");
                   WR(3 ,"  to fly and wail.      ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, " Resurrection");
                   WR(6, "  The ultimate Healing, ");
                   WR(7, "  this restores dead    ");
                   WR(8 ,"  friends to life, or   ");
                   WR(9 ,"  enemies to undead.    ");
                   WR(10,"  Beware: this will use ");
                   WR(11,"  your own EXP to cast! ");
               }
               break;
           case FAMILY_DRUID:
               message = std::format("Level {} Druid has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Plant Tree      ");
               WL(3, "  These magical trees    ");
               WL(4, "  will resist the enemy, ");
               WL(5, "  while allowing friends ");
               WL(6, "  to pass.               ");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " Summon Faerie");
                   WL(8, "  This spell brings to  ");
                   WL(9, "  you a small flying    ");
                   WL(10,"  faerie to stun foes.  ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Circle of Protection");
                   WR(1, "  Calls the winds to aid");
                   WR(2, "  your nearby friends by");
                   WR(3 ,"  circling them with a  ");
                   WR(4 ,"  shield of moving air. ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, " Reveal   ");
                   WR(6, "  Gives you a magical   ");
                   WR(7, "  view to see treasure, ");
                   WR(8 ,"  potions, outposts, and");
                   WR(9 ,"  invisible enemies.    ");
               }
               break;
           case FAMILY_THIEF:
               message = std::format("Level {} Thief has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Drop Bomb       ");
               WL(3, "  Leave a burning bomb to");
               WL(4, "  explode and hurt the   ");
               WL(5, "  unwary, friend or foe! ");
               //WL(6, "  to pass.               ");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " Cloak of Darkness");
                   WL(8, "  Cloak yourself in the ");
                   WL(9, "  shadows, slipping past");
                   WL(10,"  your enemies.         ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, " Taunt Enemies       ");
                   WR(1, "  Beckon your enemies   ");
                   WR(2, "  to you with jeers, and");
                   WR(3 ,"  confuse their attack. ");
                   //WR(4 ,"  shield of moving air. ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, " Poison Cloud");
                   WR(6, "  Release a cloud of    ");
                   WR(7, "  poisonous gas to roam ");
                   WR(8 ,"  at will and sicken    ");
                   WR(9 ,"  your foes.            ");
               }
               break;
           case FAMILY_ORC:
               message = std::format("Level {} Orc has:", thisguy->level);
               mytext.write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message.c_str(), 10, 1);
               mytext.write_xy(DETAIL_LM, DETAIL_LD(0), message.c_str(), DARK_BLUE, 1);
               // Level 1 things
               WL(2, " Howl            ");
               WL(3, "  Howl in rage, stunning ");
               WL(4, "  nearby enemies in their");
               WL(5, "  tracks.                ");
               //WL(6, "  to pass.               ");
               // Level 4 things
               if (thisguy->level >= 4)
               {
                   WL(7, " Devour Corpse    ");
                   WL(8, "  Regain health by      ");
                   WL(9, "  devouring the corpses ");
                   WL(10,"  of your foes.         ");
               }
               // Can we change to orc captain?
               if (thisguy->level >= 6)
               {
                   myscreen->draw_dialog(158, 4, 315, 66, "Become Orc Captain");
                   WR(-10,"Your Orc is now of high ");
                   WR( -9,"enough level to become a");
                   WR( -8,"Level 1 Orc Captain. You");
                   WR( -7,"CANNOT undo this action!");
                   WR( -6," Click here to change.  ");
               }
               // Level 7 things
               if (thisguy->level >= 7)
               {
                   WR(0, "                     ");
                   //WR(1, "  Beckon your enemies   ");
                   //WR(2, "  to you with jeers, and");
                   //WR(3 ,"  confuse their attack. ");
                   //WR(4 ,"  shield of moving air. ");
               }
               // Level 10 things
               if (thisguy->level >= 10)
               {
                   WR(5, "             ");
                   //WR(6, "  Release a cloud of    ");
                   //WR(7, "  poisonous gas to roam ");
                   //WR(8 ,"  at will and sicken    ");
                   //WR(9 ,"  your foes.            ");
               }
               break;
           default:
               break;
       }

       show_guy(0, 1);
       
       
       draw_buttons(buttons, num_buttons);
       draw_highlight_interior(buttons[highlighted_button]);
       myscreen->buffer_to_screen(0, 0, 320, 200);
   }
   return REDRAW;  // back to edit menu
}





int get_scen_num_from_filename(const char* name)
{
   if(!name)
    return -1;
    
   const char* n = name;
   while(isalpha(*n))
   {
       n++;
   }
   if(*n == '\0')
    return -1;
   else
    return atoi(n);
}


Sint32 do_pick_campaign(Sint32 arg1)
{
   CampaignResult result = pick_campaign(&myscreen->save_data);
   if(result.id.size() > 0)
   {
        // Load new campaign
        myscreen->save_data.current_campaign = result.id;
        myscreen->save_data.scen_num = load_campaign(result.id, myscreen->save_data.current_levels, result.first_level);
   }
   return REDRAW;
}

Sint32 do_set_scen_level(Sint32 arg1)
{
   Sint32 templevel = myscreen->save_data.scen_num;
   
   templevel = pick_level(myscreen, myscreen->level_data.id);
   
   // Have some feedback if the level changed
   if(templevel != myscreen->level_data.id)
   {
       int old_id = myscreen->level_data.id;
       myscreen->level_data.id = templevel;
       if (templevel < 0 || !myscreen->level_data.load())
       {
            myscreen->clearbuffer();
            popup_dialog("Load Failed", "Invalid level file.");
            
           myscreen->level_data.id = old_id;
           if(!myscreen->level_data.load())
           {
                myscreen->clearbuffer();
                popup_dialog("Big problem", "Also failed to reload current level...");
           }
       }
       else  // We're good
       {
           myscreen->save_data.scen_num = templevel;
           Log("Set level to {}\n", templevel);
       }
   }

   return REDRAW;
}

/*
int matherr(struct exception *problem)
{
 // Do nothing
 return 0;
}
*/

Sint32 set_difficulty()
{
   current_difficulty = (current_difficulty + 1) % DIFFICULTY_SETTINGS;
   std::string msg = std::format("Difficulty: {}", difficulty_names[current_difficulty]);
   #ifndef DISABLE_MULTIPLAYER
   allbuttons[6]->label = msg;
   #else
   allbuttons[2]->label = msg;
   #endif

   //allbuttons[6]->vdisplay();
   //myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}

Sint32 change_teamnum(Sint32 arg)
{
   // Change the team number of the current guy
   short current_team;

   // What is our current team number?
   if (!current_guy)
       return 0;
   current_team = current_guy->teamnum;

   // We can be from team 0 (default) to team 3 .. make sure
   // we don't exceed this range.
   current_team += static_cast<short>(arg);
   current_team %= 4;

   // Set our team number ..
   current_guy->teamnum = current_team;

   // Update our button display
   allbuttons[18]->label = std::format("Playing on Team {}", current_team + 1);
   //allbuttons[18]->do_outline = 1;
   //allbuttons[18]->vdisplay();
   //myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}

Sint32 change_hire_teamnum(Sint32 arg)
{
   // Change the team number of the hiring menu ..
   current_team_num += arg;
   current_team_num %= 4;

   // Change our guy, if he exists ..
   if (current_guy)
   {
       current_guy->teamnum = current_team_num;
   }

   // Update our button display
   allbuttons[2]->label = std::format("Hiring for Team {}", current_team_num + 1);

   return OK;
}

Sint32 change_allied()
{
   // Change our allied mode (on or off)
   myscreen->save_data.allied_mode += 1;
   myscreen->save_data.allied_mode %= 2;

   if (myscreen->save_data.allied_mode)
       allbuttons[7]->label = "PVP: Ally";
   else
       allbuttons[7]->label = "PVP: Enemy";

   //buffers: allbuttons[7]->vdisplay();
   //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}

#ifdef __EMSCRIPTEN__
// ============================================================================
// Emscripten State Machine Functions
// These functions allow the picker to work with a non-blocking main loop
// ============================================================================

// Check if game start was requested (called from main after picker_init)
bool picker_check_start_requested()
{
    Log("picker_check_start_requested: g_start_game_requested={}\n", g_start_game_requested);
    return g_start_game_requested;
}

// Initialize the picker (called once at startup from main)
void picker_init()
{
    Log("picker_init: Initializing picker\n");

    Sint32 i;

    for (i=0; i < MAX_BUTTONS; i++)
        allbuttons[i] = nullptr;

    // Set backdrops to nullptr
    for (i=0; i < 5; i++)
        backdrops[i] = nullptr;

    backpics[0] = read_pixie_file("mainul.pix");
    backpics[1] = read_pixie_file("mainur.pix");
    backpics[2] = read_pixie_file("mainll.pix");
    backpics[3] = read_pixie_file("mainlr.pix");

    backdrops[0] = new pixieN(backpics[0]);
    backdrops[0]->setxy(0, 0);
    backdrops[1] = new pixieN(backpics[1]);
    backdrops[1]->setxy(160, 0);
    backdrops[2] = new pixieN(backpics[2]);
    backdrops[2]->setxy(0, 100);
    backdrops[3] = new pixieN(backpics[3]);
    backdrops[3]->setxy(160, 100);

    myscreen->viewob[0]->resize(PREF_VIEW_FULL);
    myscreen->clearbuffer();

    main_title_logo_data = read_pixie_file("title.pix");
    main_title_logo_pix = new pixieN(main_title_logo_data);

    main_columns_data = read_pixie_file("columns.pix");
    main_columns_pix = new pixieN(main_columns_data);

    // Get the mouse, timer, & keyboard
    grab_mouse();
    grab_timer();
    clear_keyboard();

    // Load the current saved game, if it exists
    SDL_RWops* loadgame = open_read_file("save/", "save0.gtl");
    if (loadgame)
    {
        SDL_RWclose(loadgame);
        myscreen->save_data.load("save0");
    }

    g_picker_initialized = true;
    g_start_game_requested = false;

    // Start the main menu - this will run its blocking loop
    // When go_menu returns EXIT with g_start_game_requested, the loop exits
    mainmenu(1);

    Log("picker_init: mainmenu returned, g_start_game_requested={}\n", g_start_game_requested);
}

// Run one frame of the picker - returns true when game should start
bool picker_frame()
{
    // Check if game start was requested
    if (g_start_game_requested) {
        Log("picker_frame: Game start requested\n");
        g_start_game_requested = false;
        return true;  // Signal to transition to PLAYING state
    }

    // If we get here without g_start_game_requested, the menus exited normally
    // (e.g., user quit). For now, just restart the main menu.
    // In a more complete implementation, we'd track menu state.
    mainmenu(1);

    // Check again after menu returns
    if (g_start_game_requested) {
        Log("picker_frame: Game start requested after mainmenu\n");
        g_start_game_requested = false;
        return true;
    }

    return false;
}

// Prepare for transitioning to game (cleanup only, initialization done by state machine)
void picker_cleanup_for_game()
{
    Log("picker_cleanup_for_game: Preparing for game\n");
    // Game initialization is now done in the GAME_STATE_PLAYING handler
}

// Reinitialize picker after game ends
void picker_reinit_after_game()
{
    Log("picker_reinit_after_game: Reinitializing picker\n");

    // Fade out from game
    myscreen->fadeblack(0);
    myscreen->clearbuffer();

    grab_mouse();

    myscreen->reset(1);
    myscreen->viewob[0]->resize(PREF_VIEW_FULL);

    // Reload save data
    SDL_RWops* loadgame = open_read_file("save/", "save0.gtl");
    if (loadgame)
    {
        SDL_RWclose(loadgame);
        myscreen->save_data.load("save0");
    }

    g_start_game_requested = false;

    // Restart the main menu
    mainmenu(1);

    Log("picker_reinit_after_game: mainmenu returned, g_start_game_requested={}\n", g_start_game_requested);
}
#endif
