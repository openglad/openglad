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
#pragma once

#include <openglad/legacy/base.h>
#include <openglad/entities/obmap.h>
#include <openglad/data/gloader.h>
#include <openglad/render/pixien.h>
#include <openglad/render/text.h>
#include <openglad/runtime/screen.h>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <openglad/input/input.h>
class vbutton;

// Definition of a button

#define BUT_STR 0
#define BUT_DEX 1
#define BUT_CON 2
#define BUT_INT 3
#define BUT_ARMOR 4
#define BUT_LEVEL 5

// Button edge-colors
inline constexpr char BUTTON_FACING = 13; //12
inline constexpr char BUTTON_TOP    = 15; //14
inline constexpr char BUTTON_BOTTOM = 11; //10
inline constexpr char BUTTON_LEFT   = 14; //13
inline constexpr char BUTTON_RIGHT  = 12; //11

// Observer pointer. Owned by `runtime/screen_lifecycle` (`global_screen_owner()`).
extern screen *myscreen;

// Holds array indices for navigating menu buttons
class MenuNav
{
public:
    // Array indices for the button to move to.
    // -1 is invalid/unused direction
    int up, down, left, right;
    
    static MenuNav Up(int up);
    static MenuNav Down(int down);
    static MenuNav Left(int left);
    static MenuNav Right(int right);
    static MenuNav UpDown(int up, int down);
    static MenuNav UpLeft(int up, int left);
    static MenuNav UpRight(int up, int right);
    static MenuNav UpDownLeft(int up, int down, int left);
    static MenuNav UpDownRight(int up, int down, int right);
    static MenuNav UpLeftRight(int up, int left, int right);
    static MenuNav DownLeft(int down, int left);
    static MenuNav DownRight(int down, int right);
    static MenuNav DownLeftRight(int down, int left, int right);
    static MenuNav LeftRight(int left, int right);
    static MenuNav UpDownLeftRight(int up, int down, int left, int right);
    static MenuNav All(int up, int down, int left, int right);
    static MenuNav None();
private:
    MenuNav();
    MenuNav(int up, int down, int left, int right);
};

struct button
{
	std::string id;    // Unique identifier for test interaction API
	std::string label;
	int hotkey;
	Sint32 x, y;
	Sint32 sizex, sizey;
	Sint32 myfun; // Callback ID
	Sint32 arg1;  // argument to function fun
	MenuNav nav;
	bool hidden;  // Does not draw or accept clicks
	bool no_draw;  // Does not draw but still accepts clicks

	button(const std::string& id_, const std::string& label_, int hotkey_, Sint32 x_, Sint32 y_, Sint32 w_, Sint32 h_, Sint32 callback_ID_, Sint32 callback_arg_, const MenuNav& nav_, bool hidden_ = false)
        : id(id_), label(label_), hotkey(hotkey_), x(x_), y(y_), sizex(w_), sizey(h_), myfun(callback_ID_), arg1(callback_arg_), nav(nav_), hidden(hidden_), no_draw(false)
	{}
	button(const std::string& id_, Sint32 x_, Sint32 y_, Sint32 w_, Sint32 h_, Sint32 callback_ID_, Sint32 callback_arg_, const MenuNav& nav_, bool hidden_ = false, bool no_draw_ = false)
        : id(id_), hotkey(KEYSTATE_UNKNOWN), x(x_), y(y_), sizex(w_), sizey(h_), myfun(callback_ID_), arg1(callback_arg_), nav(nav_), hidden(hidden_), no_draw(no_draw_)
	{}
};

class vbutton
{
	public:
		vbutton();//this should only be used for pointers!!
		vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high, std::function<Sint32(Sint32)> func,
		        Sint32 pass, const std::string& msg, int hot );
		vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high, Sint32 func_code,
		        Sint32 pass, const std::string& msg, int hot );
		vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high, Sint32 func_code,
		        Sint32 pass, const std::string& msg, char family, int hot );
		~vbutton();
		void set_graphic(char family);
		Sint32 leftclick(button* buttons = nullptr);  // Checks all buttons for the click
		Sint32 leftclick(Sint32 whichone);  // Clicks this vbutton
		Sint32 rightclick(button* buttons = nullptr); //is called when the button is right clicked
		Sint32 rightclick(Sint32 whichone);  // Clicks this vbutton
		Sint32 mouse_on(); //determines if mouse is on this button, returns 1 if true
		void vdisplay();
		void vdisplay(Sint32 status); // display depressed
		Sint32 do_call(Sint32 whatfunc, Sint32 call_arg);
		Sint32 do_call_right(Sint32 whatfunc, Sint32 call_arg);  // for right-button

		std::string id; // Unique identifier for test interaction API
		Sint32 xloc; //the x position in screen-coords
		Sint32 yloc; //the y position in screen-coords
		std::string label; //the label on the button
		Sint32 width; // the buttons width in pixels
		Sint32 height; // the buttons height in pixels
		Sint32 xend; //xloc+width
		Sint32 yend; //yloc+height
		std::function<Sint32(Sint32)> fun; // optional direct callback
		Sint32 myfunc;
		Sint32 arg; //the arg to be passed to the function when called
		char had_focus; // did we recently have focus?
		char do_outline; // force an outline
		char depressed;
		std::unique_ptr<pixieN> mypixie;
		int hotkey;
		unsigned char color;
		bool hidden;
        bool no_draw;  // Does not draw but still accepts clicks
};

#define MAX_BUTTONS 50  // max buttons per screen
extern std::array<vbutton*, MAX_BUTTONS> allbuttons;
void clear_allbuttons();

vbutton * init_buttons(button * buttons, Sint32 numbuttons);
void draw_backdrop();
void draw_buttons(button * buttons, Sint32 numbuttons);

// These are for picker ..
Sint32 score_panel(screen *myscreen);
Sint32 mainmenu(Sint32 arg1);
Sint32 beginmenu(Sint32 arg1);
void quit(Sint32 arg1);
Sint32 load1(Sint32 arg1);  // Begin a preset scenario ..
Sint32 load2(Sint32 arg1);
Sint32 load3(Sint32 arg1);
Sint32 create_team_menu(Sint32 arg1); // Create / modify team members
Sint32 create_detail_menu(guy *arg1); // detailed character information
Sint32 create_view_menu(Sint32 arg1); // View team members
Sint32 create_hire_menu(Sint32 arg1);  // Purchase new team members
Sint32 create_train_menu(Sint32 arg1); // Edit or sell team members
Sint32 create_load_menu(Sint32 arg1); // Load a team
Sint32 create_save_menu(Sint32 arg1); // Save a team
Sint32 create_progress_menu(Sint32 arg1); // View level progress
Sint32 go_menu(Sint32 arg1); // run glad..
Sint32 increase_stat(Sint32 arg1, Sint32 howmuch=1); // increase a guy's stats
Sint32 decrease_stat(Sint32 arg1, Sint32 howmuch=1); // decrease a guy's stats
Uint32 calculate_hire_cost();
Uint32 calculate_train_cost(guy * oldguy);
Sint32 cycle_guy(Sint32 whichway);
Sint32 cycle_team_guy(Sint32 whichway);
Sint32 add_guy(Sint32 ignoreme);
Sint32 edit_guy(Sint32 arg1); // transfer stats .. hardcoded
Sint32 do_save(Sint32 arg1);  // dummy function for saving team list
Sint32 do_load(Sint32 arg1); // dummy function for loading team list
Sint32 delete_all(); // delete entire team
Sint32 delete_first(); // delete first guy on team list
Sint32 how_many(Sint32 whatfamily);   // how many guys of family X on the team?
void statscopy(guy *dest, guy *source); //copy stats from source => dest
Sint32 set_player_mode(Sint32 howmany);
Sint32 calculate_level(Uint32 temp_exp);
Uint32 calculate_exp(Sint32 level);
Sint32 return_menu(Sint32 arg);
Sint32 name_guy(Sint32 arg); // name the current guy
Sint32 do_set_scen_level(Sint32 arg1);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 set_difficulty();
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
Sint32 change_allied();
Sint32 level_editor();
Sint32 main_options();
Sint32 overscan_adjust(Sint32 arg);
Sint32 show_general_help();

enum class ButtonAction : Sint32
{
    Invalid = 0,
    BeginMenu = 1,
    CreateTeamMenu = 2,
    SetPlayerMode = 3,
    QuitMenu = 4,
    CreateViewMenu = 5,
    CreateTrainMenu = 6,
    CreateHireMenu = 7,
    CreateLoadMenu = 8,
    CreateSaveMenu = 9,
    GoMenu = 10,
    ReturnMenu = 11,
    CycleTeamGuy = 12,
    DecreaseStat = 13,
    IncreaseStat = 14,
    EditGuy = 15,
    CycleGuy = 16,
    AddGuy = 17,
    DoSave = 18,
    DoLoad = 19,
    NameGuy = 20,
    CreateDetailMenu = 21,
    NullMenu = 22,
    DoSetScenLevel = 23,
    SetDifficulty = 24,
    ChangeTeam = 25,
    AlliedMode = 26,
    ChangeHireTeam = 27,
    YesOrNo = 28,
    DoPickCampaign = 29,
    DoLevelEdit = 30,
    MainOptions = 31,
    ToggleSound = 32,
    ToggleRenderingEngine = 33,
    ToggleFullscreen = 34,
    OverscanAdjust = 35,
    ToggleMiniHpBar = 36,
    ToggleHitFlash = 37,
    ToggleHitRecoil = 38,
    ToggleAttackLunge = 39,
    ToggleHitAnim = 40,
    ToggleDamageNumbers = 41,
    ToggleHealNumbers = 42,
    ToggleGore = 43,
    RestoreDefaultSettings = 44,
    ShowHelp = 45,
    CreateProgressMenu = 46,
};

inline constexpr Sint32 button_action_id(ButtonAction action)
{
    return static_cast<Sint32>(action);
}

inline constexpr ButtonAction button_action_from_id(Sint32 action)
{
    switch (action)
    {
    case button_action_id(ButtonAction::BeginMenu): return ButtonAction::BeginMenu;
    case button_action_id(ButtonAction::CreateTeamMenu): return ButtonAction::CreateTeamMenu;
    case button_action_id(ButtonAction::SetPlayerMode): return ButtonAction::SetPlayerMode;
    case button_action_id(ButtonAction::QuitMenu): return ButtonAction::QuitMenu;
    case button_action_id(ButtonAction::CreateViewMenu): return ButtonAction::CreateViewMenu;
    case button_action_id(ButtonAction::CreateTrainMenu): return ButtonAction::CreateTrainMenu;
    case button_action_id(ButtonAction::CreateHireMenu): return ButtonAction::CreateHireMenu;
    case button_action_id(ButtonAction::CreateLoadMenu): return ButtonAction::CreateLoadMenu;
    case button_action_id(ButtonAction::CreateSaveMenu): return ButtonAction::CreateSaveMenu;
    case button_action_id(ButtonAction::GoMenu): return ButtonAction::GoMenu;
    case button_action_id(ButtonAction::ReturnMenu): return ButtonAction::ReturnMenu;
    case button_action_id(ButtonAction::CycleTeamGuy): return ButtonAction::CycleTeamGuy;
    case button_action_id(ButtonAction::DecreaseStat): return ButtonAction::DecreaseStat;
    case button_action_id(ButtonAction::IncreaseStat): return ButtonAction::IncreaseStat;
    case button_action_id(ButtonAction::EditGuy): return ButtonAction::EditGuy;
    case button_action_id(ButtonAction::CycleGuy): return ButtonAction::CycleGuy;
    case button_action_id(ButtonAction::AddGuy): return ButtonAction::AddGuy;
    case button_action_id(ButtonAction::DoSave): return ButtonAction::DoSave;
    case button_action_id(ButtonAction::DoLoad): return ButtonAction::DoLoad;
    case button_action_id(ButtonAction::NameGuy): return ButtonAction::NameGuy;
    case button_action_id(ButtonAction::CreateDetailMenu): return ButtonAction::CreateDetailMenu;
    case button_action_id(ButtonAction::NullMenu): return ButtonAction::NullMenu;
    case button_action_id(ButtonAction::DoSetScenLevel): return ButtonAction::DoSetScenLevel;
    case button_action_id(ButtonAction::SetDifficulty): return ButtonAction::SetDifficulty;
    case button_action_id(ButtonAction::ChangeTeam): return ButtonAction::ChangeTeam;
    case button_action_id(ButtonAction::AlliedMode): return ButtonAction::AlliedMode;
    case button_action_id(ButtonAction::ChangeHireTeam): return ButtonAction::ChangeHireTeam;
    case button_action_id(ButtonAction::YesOrNo): return ButtonAction::YesOrNo;
    case button_action_id(ButtonAction::DoPickCampaign): return ButtonAction::DoPickCampaign;
    case button_action_id(ButtonAction::DoLevelEdit): return ButtonAction::DoLevelEdit;
    case button_action_id(ButtonAction::MainOptions): return ButtonAction::MainOptions;
    case button_action_id(ButtonAction::ToggleSound): return ButtonAction::ToggleSound;
    case button_action_id(ButtonAction::ToggleRenderingEngine): return ButtonAction::ToggleRenderingEngine;
    case button_action_id(ButtonAction::ToggleFullscreen): return ButtonAction::ToggleFullscreen;
    case button_action_id(ButtonAction::OverscanAdjust): return ButtonAction::OverscanAdjust;
    case button_action_id(ButtonAction::ToggleMiniHpBar): return ButtonAction::ToggleMiniHpBar;
    case button_action_id(ButtonAction::ToggleHitFlash): return ButtonAction::ToggleHitFlash;
    case button_action_id(ButtonAction::ToggleHitRecoil): return ButtonAction::ToggleHitRecoil;
    case button_action_id(ButtonAction::ToggleAttackLunge): return ButtonAction::ToggleAttackLunge;
    case button_action_id(ButtonAction::ToggleHitAnim): return ButtonAction::ToggleHitAnim;
    case button_action_id(ButtonAction::ToggleDamageNumbers): return ButtonAction::ToggleDamageNumbers;
    case button_action_id(ButtonAction::ToggleHealNumbers): return ButtonAction::ToggleHealNumbers;
    case button_action_id(ButtonAction::ToggleGore): return ButtonAction::ToggleGore;
    case button_action_id(ButtonAction::RestoreDefaultSettings): return ButtonAction::RestoreDefaultSettings;
    case button_action_id(ButtonAction::ShowHelp): return ButtonAction::ShowHelp;
    case button_action_id(ButtonAction::CreateProgressMenu): return ButtonAction::CreateProgressMenu;
    default:
        return ButtonAction::Invalid;
    }
}

// Legacy integer aliases kept while callers migrate to ButtonAction.
#define BEGINMENU                button_action_id(ButtonAction::BeginMenu)
#define CREATE_TEAM_MENU         button_action_id(ButtonAction::CreateTeamMenu)
#define SET_PLAYER_MODE          button_action_id(ButtonAction::SetPlayerMode)
#define QUIT_MENU                button_action_id(ButtonAction::QuitMenu)
#define CREATE_VIEW_MENU         button_action_id(ButtonAction::CreateViewMenu)
#define CREATE_TRAIN_MENU        button_action_id(ButtonAction::CreateTrainMenu)
#define CREATE_HIRE_MENU         button_action_id(ButtonAction::CreateHireMenu)
#define CREATE_LOAD_MENU         button_action_id(ButtonAction::CreateLoadMenu)
#define CREATE_SAVE_MENU         button_action_id(ButtonAction::CreateSaveMenu)
#define GO_MENU                  button_action_id(ButtonAction::GoMenu)
#define RETURN_MENU              button_action_id(ButtonAction::ReturnMenu)
#define CYCLE_TEAM_GUY           button_action_id(ButtonAction::CycleTeamGuy)
#define DECREASE_STAT            button_action_id(ButtonAction::DecreaseStat)
#define INCREASE_STAT            button_action_id(ButtonAction::IncreaseStat)
#define EDIT_GUY                 button_action_id(ButtonAction::EditGuy)
#define CYCLE_GUY                button_action_id(ButtonAction::CycleGuy)
#define ADD_GUY                  button_action_id(ButtonAction::AddGuy)
#define DO_SAVE                  button_action_id(ButtonAction::DoSave)
#define DO_LOAD                  button_action_id(ButtonAction::DoLoad)
#define NAME_GUY                 button_action_id(ButtonAction::NameGuy)
#define CREATE_DETAIL_MENU       button_action_id(ButtonAction::CreateDetailMenu)
#define NULLMENU                 button_action_id(ButtonAction::NullMenu)
#define DO_SET_SCEN_LEVEL        button_action_id(ButtonAction::DoSetScenLevel)
#define SET_DIFFICULTY           button_action_id(ButtonAction::SetDifficulty)
#define CHANGE_TEAM              button_action_id(ButtonAction::ChangeTeam)
#define ALLIED_MODE              button_action_id(ButtonAction::AlliedMode)
#define CHANGE_HIRE_TEAM         button_action_id(ButtonAction::ChangeHireTeam)
#define YES_OR_NO                button_action_id(ButtonAction::YesOrNo)
#define DO_PICK_CAMPAIGN         button_action_id(ButtonAction::DoPickCampaign)
#define DO_LEVEL_EDIT            button_action_id(ButtonAction::DoLevelEdit)
#define MAIN_OPTIONS             button_action_id(ButtonAction::MainOptions)
#define TOGGLE_SOUND             button_action_id(ButtonAction::ToggleSound)
#define TOGGLE_RENDERING_ENGINE  button_action_id(ButtonAction::ToggleRenderingEngine)
#define TOGGLE_FULLSCREEN        button_action_id(ButtonAction::ToggleFullscreen)
#define OVERSCAN_ADJUST          button_action_id(ButtonAction::OverscanAdjust)
#define TOGGLE_MINI_HP_BAR       button_action_id(ButtonAction::ToggleMiniHpBar)
#define TOGGLE_HIT_FLASH         button_action_id(ButtonAction::ToggleHitFlash)
#define TOGGLE_HIT_RECOIL        button_action_id(ButtonAction::ToggleHitRecoil)
#define TOGGLE_ATTACK_LUNGE      button_action_id(ButtonAction::ToggleAttackLunge)
#define TOGGLE_HIT_ANIM          button_action_id(ButtonAction::ToggleHitAnim)
#define TOGGLE_DAMAGE_NUMBERS    button_action_id(ButtonAction::ToggleDamageNumbers)
#define TOGGLE_HEAL_NUMBERS      button_action_id(ButtonAction::ToggleHealNumbers)
#define TOGGLE_GORE              button_action_id(ButtonAction::ToggleGore)
#define RESTORE_DEFAULT_SETTINGS button_action_id(ButtonAction::RestoreDefaultSettings)
#define SHOW_HELP                button_action_id(ButtonAction::ShowHelp)
#define CREATE_PROGRESS_MENU     button_action_id(ButtonAction::CreateProgressMenu)
