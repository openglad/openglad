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
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/input/button.h>
#include <openglad/render/pal32.h>
#include <openglad/input/input.h>
#include <openglad/render/view.h>
#include <openglad/core/util.h>
#include <openglad/platform/io.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/walker_draw.h>

#include "SDL.h"
#include <openglad/ui/campaign_picker.h>
#include <openglad/ui/level_picker.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#define DOWN(x) (72 + static_cast<Sint32>((x) * 15))
#define VIEW_DOWN(x) (10 + static_cast<Sint32>((x) * 20))
#define RAISE 1.85  // please also change in guy.cpp

#define EXIT 1
#define REDRAW 2
#define OK 4
#define BUTTON_HEIGHT 15
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

extern std::unique_ptr<guy> current_guy;
extern guy* old_guy;
extern std::string message;
extern Sint32 editguy;
extern vbutton* localbuttons;
extern short current_team_num;
extern std::array<Sint32, 14> allowable_guys;
extern Sint32 current_type;
extern std::array<Sint32, NUM_FAMILIES> numbought;
extern button createmenu_buttons[];
extern button viewteam_buttons[];
extern button details_buttons[];
extern button trainmenu_buttons[];
extern button hiremenu_buttons[];
extern button saveteam_buttons[];
extern button loadteam_buttons[];
#ifdef __EMSCRIPTEN__
void picker_request_start_game();
#endif
#ifdef TESTING
void picker_testing_mark_game_start();
void picker_testing_mark_game_end();
#endif

extern Sint32 costlist[NUM_FAMILIES];
extern Sint32 statlist[NUM_FAMILIES][6];
extern Sint32 statcosts[NUM_FAMILIES][6];

bool prompt_for_string(const std::string& message, std::string& result);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void timed_dialog(const char* message, float delay_seconds = 3.0f);
void show_guy(Sint32 frames, Sint32 who, Sint32 centerx = 80, Sint32 centery = 45);
void view_team(short left, short top, short right, short bottom);
void draw_backdrop();
Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
void draw_highlight_interior(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& localbuttons, button* buttons, int num_buttons, Sint32& retvalue);
const char* family_name_copy(short family);
const char* get_family_string(Sint32 family);
std::string get_saved_name(const char* filename);
Sint32 create_view_menu(Sint32 arg1);
Sint32 create_hire_menu(Sint32 arg1);
Sint32 cycle_guy(Sint32 whichway);
Sint32 cycle_team_guy(Sint32 whichway);
Sint32 set_difficulty();
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
Sint32 create_detail_menu(guy *arg1);
void glad_main(Sint32 playermode);
void statscopy(guy *dest, guy *source);
Sint32 create_team_menu(Sint32 arg1)
{
	Sint32 retvalue=0;

	if (arg1 == 1)
    {
        // Go straight to the hiring screen if we just started a new game.
        retvalue = create_hire_menu(arg1);
    }

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

	myscreen->fadeblack(0);
	
	text& mytext = myscreen->text_normal;
	
	button* buttons = createmenu_buttons;
	int num_buttons = 10;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	draw_backdrop();
	draw_buttons(buttons, num_buttons);
	
	int last_level_id = -1;
	
	myscreen->fadeblack(1);
	
	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        
        // Reset buttons
        bool buttons_were_reset = reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
        if(last_level_id != myscreen->save_data.scen_num || buttons_were_reset)
        {
            retvalue = 0;
            last_level_id = myscreen->save_data.scen_num;
            myscreen->level_data.id = last_level_id;
            myscreen->level_data.load();
        }
        
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        // Level name
        int len = static_cast<int>(myscreen->level_data.title.size());
        myscreen->draw_rect_filled(buttons[7].x + buttons[7].sizex - 6*len - 2, buttons[7].y - 8 - 1, 6*len + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(buttons[7].x + buttons[7].sizex - 6*len, buttons[7].y - 8, WHITE, "%s", myscreen->level_data.title.c_str());
        // Campaign name
        len = static_cast<int>(myscreen->save_data.current_campaign.size());
        myscreen->draw_rect_filled(buttons[8].x + buttons[8].sizex - 6*len - 2, buttons[8].y - 8 - 1, 6*len + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(buttons[8].x + buttons[8].sizex - 6*static_cast<int>(myscreen->save_data.current_campaign.size()), buttons[8].y - 8, WHITE, "%s", myscreen->save_data.current_campaign.c_str());
        
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}

	// Propagate EXIT if that's why we left the loop
	if (retvalue & EXIT)
		return retvalue;

	return REDRAW;
}

Sint32 create_view_menu(Sint32 arg1)
{
	Sint32 retvalue = 0;

	if (arg1)
		arg1 = 1;

	myscreen->clearbuffer();

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	button* buttons = viewteam_buttons;
	int num_buttons = 2;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);

	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        view_team(5,5,314, 160);
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	myscreen->clearbuffer();

	return REDRAW;
}

// Helper struct for progress menu
struct LevelProgress {
    int id;
    std::string title;
    int num_enemies;
    bool is_cleared;
    bool is_current;
};

std::vector<int> get_accessible_levels();

Sint32 create_progress_menu(Sint32 arg1)
{
    Sint32 retvalue = 0;
    text& mytext = myscreen->text_normal;

    if (arg1)
        arg1 = 1;

    myscreen->clearbuffer();

	    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

    // Get accessible levels
    std::vector<int> level_ids = get_accessible_levels();
    std::vector<LevelProgress> levels;

    // Count cleared levels
    int num_cleared = 0;

    // Load level info for each accessible level
    for (int level_id : level_ids) {
        LevelProgress lp;
        lp.id = level_id;
        lp.is_cleared = myscreen->save_data.is_level_completed(level_id);
        lp.is_current = (level_id == myscreen->save_data.scen_num);

        if (lp.is_cleared)
            num_cleared++;

        // Load level to get title and enemy count
        LevelData ld(level_id);
        if (ld.load()) {
            if (ld.title.size() > 20) {
                lp.title = ld.title.substr(0, 17) + "...";
            } else {
                lp.title = ld.title;
            }

            // Count enemies
            int num_enemies = 0;
            std::list<int> unused_exits;
            getLevelStats(ld, nullptr, nullptr, &num_enemies, nullptr, unused_exits);
            lp.num_enemies = lp.is_cleared ? 0 : num_enemies;
        } else {
            lp.title = std::format("Level {}", level_id);
            lp.num_enemies = 0;
        }

        levels.push_back(lp);
    }

    // Scrolling state
    int scroll_offset = 0;
    int visible_rows = 10;

    // Buttons
    SDL_Rect prev_btn = {30, 170, 40, 20};
    SDL_Rect next_btn = {80, 170, 40, 20};
    SDL_Rect back_btn = {260, 170, 50, 20};

    button buttons[] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, prev_btn.x, prev_btn.y, prev_btn.w, prev_btn.h, 0, -1, MenuNav::Right(1)),
        button("next", "NEXT", KEYSTATE_UNKNOWN, next_btn.x, next_btn.y, next_btn.w, next_btn.h, 0, -1, MenuNav::LeftRight(0, 2)),
        button("back", "BACK", KEYSTATE_ESCAPE, back_btn.x, back_btn.y, back_btn.w, back_btn.h, RETURN_MENU, EXIT, MenuNav::Left(1)),
    };
    int num_buttons = 3;
    int highlighted_button = 2;
    localbuttons = init_buttons(buttons, num_buttons);

    while (!(retvalue & EXIT))
    {
        // Input
        if (leftmouse(buttons))
            retvalue = localbuttons->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Handle scroll buttons
        MouseState& mymouse = query_mouse();
        bool clicked = mymouse.left;
        const int mx = static_cast<int>(mymouse.x);
        const int my = static_cast<int>(mymouse.y);
        if (clicked) {
            while (mymouse.left) {
                SDL_Delay(1);
                get_input_events(POLL);
            }
        }

        bool prev_enabled = (scroll_offset > 0);
        bool next_enabled = (scroll_offset + visible_rows < static_cast<int>(levels.size()));

        bool do_prev = prev_enabled && ((clicked && prev_btn.x <= mx && mx <= prev_btn.x + prev_btn.w
                       && prev_btn.y <= my && my <= prev_btn.y + prev_btn.h)
                       || (retvalue == OK && highlighted_button == 0));
        bool do_next = next_enabled && ((clicked && next_btn.x <= mx && mx <= next_btn.x + next_btn.w
                       && next_btn.y <= my && my <= next_btn.y + next_btn.h)
                       || (retvalue == OK && highlighted_button == 1));

        if (do_prev) {
            scroll_offset--;
            retvalue = 0;
        }
        if (do_next) {
            scroll_offset++;
            retvalue = 0;
        }

        // Check for GO button clicks on level rows
        if (clicked) {
            int row_y = 36;
            int row_height = 13;
            int go_btn_x = 295;
            int go_btn_w = 20;
            for (int i = scroll_offset; i < static_cast<int>(levels.size()) && i < scroll_offset + visible_rows; i++) {
                LevelProgress& lp = levels[i];
                if (!lp.is_cleared) {
                    // Check if click is on this row's GO button
                    if (mx >= go_btn_x && mx <= go_btn_x + go_btn_w &&
                        my >= row_y && my <= row_y + row_height) {
                        // Set current level and exit
                        myscreen->save_data.scen_num = static_cast<short>(lp.id);
                        myscreen->clearbuffer();
                        return REDRAW;
                    }
                }
                row_y += row_height;
            }
        }

        // Reset
        if (retvalue == OK && highlighted_button != 2)
            retvalue = 0;

        // Draw
        myscreen->clearbuffer();

        // Header
        std::string header = std::format("Level Progress: {} cleared of {} discovered",
                 num_cleared, static_cast<int>(levels.size()));
        mytext.write_xy(160 - static_cast<int>(header.size()) * 3, 8, header.c_str(), DARK_GREEN, 1);

        // Column headers
        myscreen->draw_text_bar(10, 22, 310, 32);
        mytext.write_xy(12, 24, "ID", DARK_BLUE, 1);
        mytext.write_xy(36, 24, "Status", DARK_BLUE, 1);
        mytext.write_xy(100, 24, "Title", DARK_BLUE, 1);
        mytext.write_xy(250, 24, "Foes", DARK_BLUE, 1);

        // Level rows
        int y = 36;
        int row_height = 13;
        for (int i = scroll_offset; i < static_cast<int>(levels.size()) && i < scroll_offset + visible_rows; i++) {
            LevelProgress& lp = levels[i];

            // ID
            std::string buf = std::format("{}", lp.id);
            mytext.write_xy(12, y + 2, buf.c_str(), WHITE, 1);

            // Status
            unsigned char status_color;
            const char* status_text;
            if (lp.is_cleared) {
                status_text = "CLEARED";
                status_color = DARK_GREEN;
            } else if (lp.is_current) {
                status_text = "CURRENT";
                status_color = YELLOW;
            } else {
                status_text = "-------";
                status_color = WHITE;
            }
            mytext.write_xy(36, y + 2, status_text, status_color, 1);

            // Title
            mytext.write_xy(100, y + 2, lp.title.c_str(), WHITE, 1);

            // Enemy count
            if (lp.is_cleared) {
                mytext.write_xy(258, y + 2, "0", DARK_GREEN, 1);
            } else {
                buf = std::format("{}", lp.num_enemies);
                mytext.write_xy(258, y + 2, buf.c_str(), WHITE, 1);

                // GO button for non-cleared levels (right edge aligns with BACK button at x=310)
                myscreen->draw_button(292, y + 1, 310, y + 10, 1, 1);
                mytext.write_xy(296, y + 2, "GO", DARK_BLUE, 1);
            }

            y += row_height;
        }

        // Scroll indicator
        if (levels.size() > (size_t)visible_rows) {
            std::string scroll_info = std::format("{}-{} of {}",
                     scroll_offset + 1,
                     std::min(scroll_offset + visible_rows, static_cast<int>(levels.size())),
                     static_cast<int>(levels.size()));
            mytext.write_xy(140, 172, scroll_info.c_str(), WHITE, 1);
        }

        // Buttons
        draw_buttons(buttons, num_buttons);
        draw_highlight(buttons[highlighted_button]);

        myscreen->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }

    myscreen->clearbuffer();
    return REDRAW;
}

std::string get_class_description(unsigned char family)
{
    std::string result;
    
    switch(family)
    {
    case FAMILY_SOLDIER:
        result = "Your basic grunt, can     \n"
                 "absorb and deal damage and\n"
                 "move moderately fast. A   \n"
                 "good all-around fighter. A\n"
                 "soldier's normal weapon is\n"
                 "a magical returning blade.\n"
                 "\n"
                 "Special: Charge";
        break;
    case FAMILY_ELF:
        result = "Elves are small and weak, \n"
                 "but are harder to hit than\n"
                 "most classes. Alone of all\n"
                 "the classes, elves possess\n"
                 "the 'ForestWalk' ability. \n"
                 "\n"
                 "Special: Rocks";
        break;
    case FAMILY_ARCHER:
        result = "Archers are fleet of foot,\n"
                 "and their arrows have a   \n"
                 "long range. Although      \n"
                 "they're not as strong as  \n"
                 "other fighters, they can  \n"
                 "be a good squad backbone. \n"
                 "\n"
                 "Special: Fire Arrows";
        break;
    case FAMILY_MAGE:
        result = "Mages are slow, can't     \n"
                 "stand much damage, and are\n"
                 "horrible at hand-to-hand  \n"
                 "combat, but their magical \n"
                 "fireballs pack a big      \n"
                 "punch.                    \n"
                 "\n"
                 "Special: Teleport";
        break;
    case FAMILY_SKELETON:
        result = "Skeletons are the pathetic\n"
                 "remains of those who once \n"
                 "were among the living.    \n"
                 "They are not particularly \n"
                 "dangerous, but they move  \n"
                 "with blinding speed.      \n"
                 "\n"
                 "Special: Tunnel";
        break;
    case FAMILY_CLERIC:
        result = "Clerics, like mages, are  \n"
                 "slow, but have a stronger \n"
                 "hand-to-hand attack.      \n"
                 "Clerics possess abilities \n"
                 "related to healing and    \n"
                 "interaction with the dead.\n"
                 "\n"
                 "Special: Heal";
        break;
    case FAMILY_FIREELEMENTAL:
        result = "Strong and quick, fire    \n"
                 "elementals can expel      \n"
                 "flaming meteors in all    \n"
                 "directions to decimate    \n"
                 "enemies.                  \n"
                 "\n"
                 "Special: Starburst";
        break;
    case FAMILY_FAERIE:
        result = "The faerie are small,     \n"
                 "flying above friends and  \n"
                 "enemies alike unnoticed.  \n"
                 "Although they are delicate\n"
                 "and easily destroyed,     \n"
                 "faeries can sprinkle a    \n"
                 "magic powder which freezes\n"
                 "their enemies.";
        break;
    case FAMILY_SLIME:
    case FAMILY_SMALL_SLIME:
    case FAMILY_MEDIUM_SLIME:
        result = "Slimes are patches of ooze\n"
                 "which grow and split into \n"
                 "two smaller slimes, over- \n"
                 "whelming the enemy. Their \n"
                 "nebulous nature makes them\n"
                 "more susceptible to magic.\n"
                 "\n"
                 "Special: Grow";
        break;
    case FAMILY_THIEF:
        result = "Thieves are fast, though  \n"
                 "not so potent as the      \n"
                 "soldier. Thieves can throw\n"
                 "small blades rapidly and  \n"
                 "damage whole groups of    \n"
                 "enemies with their bombs. \n"
                 "\n"
                 "Special: Drop Bomb";
        break;
    case FAMILY_GHOST:
        result = "Ghosts can pass through   \n"
                 "walls, trees, and anything\n"
                 "else that gets in the way.\n"
                 "Their chilling touch can  \n"
                 "bring death quickly at    \n"
                 "close range.              \n"
                 "\n"
                 "Special: Scare";
        break;
    case FAMILY_DRUID:
        result = "Druids are the magicians  \n"
                 "of nature, and have power \n"
                 "over natural events. They \n"
                 "throw lightning bolts at  \n"
                 "their foes; the fast bolts\n"
                 "have long range.          \n"
                 "\n"
                 "Special: Plant Tree";
        break;
    case FAMILY_ORC:
        result = "Orcs are a basic 'grunt'; \n"
                 "strong and hard to hurt,  \n"
                 "they don't do much more   \n"
                 "than inflict pain. Orcs   \n"
                 "can't attack at range.    \n"
                 "\n"
                 "Special: Howl";
        break;
    case FAMILY_BIG_ORC:
        result = "Orcs captains are stronger\n"
                 "and smarter than the basic\n"
                 "orc.  They throw blades   \n"
                 "across the battlefield to \n"
                 "deal damage from afar.";
        break;
    case FAMILY_BARBARIAN:
        result = "Barbarians are powerful   \n"
                 "and resist some magic     \n"
                 "damage, but have more will\n"
                 "than skill. They are tough,\n"
                 "tending to bash their way \n"
                 "through trouble with heavy\n"
                 "iron hammers.             \n"
                 "Special: Hurl Boulder";
        break;
    case FAMILY_ARCHMAGE:
        result = "An Archmage takes the     \n"
                 "learnings of the Magi one \n"
                 "step further, possessing  \n"
                 "extraordinary firepower at\n"
                 "the expense of physical   \n"
                 "weakness.                 \n"
                 "\n"
                 "Special: Teleport";
        break;
    default:
        break;
    }
    
    return result;
}

// stat: str 0, dex 1, con 2, int 3, armor 4
	const char* get_training_cost_rating(unsigned char family, int stat)
	{
	    int value = 55/(statcosts[family][stat]);
	    int rating = (value * 5) / 11;
	    switch(rating)
	    {
    case 0:
        return "";
    case 1:
        return "*";
    case 2:
        return "**";
    case 3:
        return "***";
    case 4:
        return "****";
    case 5:
        return "*****";
    default:
        return "";
    }
}

Sint32 create_hire_menu(Sint32 arg1)
{
	Sint32 linesdown, retvalue = 0;
	Sint32 start_time = query_timer();
	unsigned char showcolor; // normally STAT_COLOR or STAT_CHANGED
	Uint32 current_cost;
	Sint32 clickvalue;

#define STAT_NUM_OFFSET 42
#define STAT_COLOR   DARK_BLUE // color for normal stat text
#define STAT_CHANGED RED       // color for changed stat text
#define STAT_LEVELED LIGHT_BLUE   // color for leveled up stat text
#define STAT_DISABLED BLACK   // color for disabled stat text
#define STAT_DERIVED DARK_BLUE + 3
    
    SDL_Rect stat_box = {196, 50 - 6 - 32, 104, 82 + 32};
    SDL_Rect stat_box_inner = {stat_box.x + 4, stat_box.y + 4 + 6, stat_box.w - 8, stat_box.h - 8 - 6};
    SDL_Rect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};
    
    SDL_Rect cost_box = {196, 130, 104, 31};
    SDL_Rect cost_box_inner = {cost_box.x + 4, cost_box.y + 4, cost_box.w - 8, cost_box.h - 8};
    SDL_Rect cost_box_content = {cost_box_inner.x + 4, cost_box_inner.y + 4, cost_box_inner.w - 8, cost_box_inner.h - 8};
    
    SDL_Rect description_box = {11, 71, 180, 90};
    SDL_Rect description_box_inner = {description_box.x + 4, description_box.y + 4, description_box.w - 8, description_box.h - 8};
    SDL_Rect description_box_content = {description_box_inner.x + 4, description_box_inner.y + 4, description_box_inner.w - 8, description_box_inner.h - 8};
    
    SDL_Rect name_box = {description_box.x + description_box.w/2 - (126-34)/2, description_box.y - 71 + 8, 126 - 34, 24 - 8};
    SDL_Rect name_box_inner = {name_box.x + 2, name_box.y + 2, name_box.w - 4, name_box.h - 4};
    
    hiremenu_buttons[0].x = description_box.x + description_box.w/2 - hiremenu_buttons[0].sizex - 4 - 30;
    hiremenu_buttons[0].y = name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2 - hiremenu_buttons[0].sizey/2;
    
    hiremenu_buttons[1].x = description_box.x + description_box.w/2 + 4 + 30;
    hiremenu_buttons[1].y = name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2 - hiremenu_buttons[1].sizey/2;
    
    hiremenu_buttons[2].hidden = (myscreen->save_data.numplayers == 1);
    
	myscreen->clearbuffer();

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	#ifdef DISABLE_MULTIPLAYER
	hiremenu_buttons[2].hidden = true;
	#endif
    
	button* buttons = hiremenu_buttons;
	int num_buttons = 5;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	
    cycle_guy(0);
    change_hire_teamnum(0);
    
    
    unsigned char last_family = current_guy->family;
    std::string description = get_class_description(last_family);
    std::list<std::string> desc = explode(description);
    const char* family_name = get_family_string(last_family);
	
	grab_mouse();

	while ( !(retvalue & EXIT) )
	{
	    // Input
		clickvalue = leftmouse(buttons);
		if (clickvalue == 1)
			retvalue = localbuttons->leftclick();
		else if (clickvalue == 2)
			retvalue = localbuttons->rightclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        if(retvalue == OK || retvalue == REDRAW)
        {
	            // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	            localbuttons = init_buttons(buttons, num_buttons);

            // Update our team-number display ..
            change_hire_teamnum(0);
            
            retvalue = 0;
        }
		
		// Draw
		myscreen->clearbuffer();
		
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        if (!current_guy)
            cycle_guy(0);
        
        // Name box
        myscreen->draw_button(name_box, 1);
        myscreen->draw_button_inverted(name_box_inner);
        
        text& mytext = myscreen->text_normal;
        mytext.write_xy(name_box.x + name_box.w/2 - 3*static_cast<Sint32>(strlen(family_name)), name_box.y + 6, family_name, static_cast<unsigned char>(DARK_BLUE), 1);
        
		show_guy(query_timer()-start_time, 0, description_box.x + description_box.w/2, name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2); // 0 means current_guy
        change_hire_teamnum(0);
        
        
        // Description box
        myscreen->draw_button(description_box, 1);
        myscreen->draw_button_inverted(description_box_inner);
        
        if(current_guy->family != last_family)
        {
            // Update description
            last_family = current_guy->family;
            description = get_class_description(last_family);
            desc = explode(description);
            
            family_name = get_family_string(last_family);
        }
        
        int i = 0;
        for(auto& line : desc)
        {
            mytext.write_xy(description_box_content.x, description_box_content.y + i*10, DARK_BLUE, "%s", line.c_str());
            i++;
        }
        
        // Cost box
        myscreen->draw_button(cost_box, 1);
        myscreen->draw_button_inverted(cost_box_inner);
        
        message = std::format("CASH: {}", myscreen->save_data.m_totalcash[current_team_num]);
        mytext.write_xy(cost_box_content.x, cost_box_content.y, message.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        current_cost = calculate_hire_cost();
        mytext.write_xy(cost_box_content.x, cost_box_content.y + 10, "COST: ", DARK_BLUE, 1);
        message = std::format("      {}", current_cost );
        if (current_cost > myscreen->save_data.m_totalcash[current_team_num])
            mytext.write_xy(cost_box_content.x + 10, cost_box_content.y + 10, message.c_str(), STAT_CHANGED, 1);
        else
            mytext.write_xy(cost_box_content.x + 10, cost_box_content.y + 10, message.c_str(), STAT_COLOR, 1);

        // Stat box
        myscreen->draw_button(stat_box, 1);
        mytext.write_xy(stat_box.x + 65, stat_box.y + 2, DARK_BLUE, "Train");
        myscreen->draw_button_inverted(stat_box_inner);

        // Stat box content
        linesdown = 0;
        int line_height = 10;
        
        showcolor = STAT_COLOR;
        
        // Strength
        message = std::format("{}", current_guy->strength);
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height, "STR:",
                         static_cast<unsigned char>(STAT_COLOR), 1);

        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*line_height, message.c_str(), showcolor, 1);
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*line_height, get_training_cost_rating(last_family, 0), showcolor, 1);
        
        linesdown++;
        // Dexterity
        message = std::format("{}", current_guy->dexterity);
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height, "DEX:",
                         static_cast<unsigned char>(STAT_COLOR), 1);

        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*line_height, message.c_str(), showcolor, 1);
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*line_height, get_training_cost_rating(last_family, 1), showcolor, 1);

        linesdown++;
        // Constitution
        message = std::format("{}", current_guy->constitution);
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height, "CON:",
                         static_cast<unsigned char>(STAT_COLOR), 1);

        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*line_height, message.c_str(), showcolor, 1);
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*line_height, get_training_cost_rating(last_family, 2), showcolor, 1);

        linesdown++;
        // Intelligence
        message = std::format("{}", current_guy->intelligence);
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height, "INT:",
                         static_cast<unsigned char>(STAT_COLOR), 1);

        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*line_height, message.c_str(), showcolor, 1);
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*line_height, get_training_cost_rating(last_family, 3), showcolor, 1);

        linesdown++;
        // Armor
        message = std::format("{}", current_guy->armor);
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height, "ARMOR:",
                         static_cast<unsigned char>(STAT_COLOR), 1);

        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*line_height, message.c_str(), showcolor, 1);
		
		// Separator bar
		SDL_Rect r = {stat_box_content.x + 10, stat_box_content.y + (linesdown+1)*line_height - 2, stat_box_content.w - 20, 2};
		myscreen->draw_button_inverted(r);
		
		int derived_offset = 3*STAT_NUM_OFFSET/4;
        linesdown++;
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height + 4, "HP:", STAT_DERIVED, 1);
        mytext.write_xy(stat_box_content.x + derived_offset - 9, stat_box_content.y + linesdown*line_height + 4, HIGH_HP_COLOR, "%.0f", ceilf(myscreen->level_data.myloader->hitpoints[PIX(Order::Living, last_family)] + current_guy->get_hp_bonus()));
        
        mytext.write_xy(stat_box_content.x + derived_offset + 18, stat_box_content.y + linesdown*line_height + 4, "MP:", STAT_DERIVED, 1);
        mytext.write_xy(stat_box_content.x + 2*derived_offset + 18 - 9, stat_box_content.y + linesdown*line_height + 4, MAX_MP_COLOR, "%.0f", ceilf(current_guy->get_mp_bonus()));
		
		linesdown++;
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height + 4, "ATK:", STAT_DERIVED, 1);
        mytext.write_xy(stat_box_content.x + derived_offset - 3, stat_box_content.y + linesdown*line_height + 4, showcolor, "%.0f", myscreen->level_data.myloader->damage[PIX(Order::Living, last_family)] + current_guy->get_damage_bonus());
        
        mytext.write_xy(stat_box_content.x + derived_offset + 18, stat_box_content.y + linesdown*line_height + 4, "DEF:", STAT_DERIVED, 1);
        mytext.write_xy(stat_box_content.x + 2*derived_offset + 18 - 3, stat_box_content.y + linesdown*line_height + 4, showcolor, "%.0f", current_guy->get_armor_bonus());
		
		linesdown++;
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height + 4, "SPD:", STAT_DERIVED, 1);
        mytext.write_xy(stat_box_content.x + derived_offset, stat_box_content.y + linesdown*line_height + 4, showcolor, "%.1f", myscreen->level_data.myloader->stepsizes[PIX(Order::Living, last_family)] + current_guy->get_speed_bonus());
        
		linesdown++;
        mytext.write_xy(stat_box_content.x, stat_box_content.y + linesdown*line_height + 4, "ATK SPD:", STAT_DERIVED, 1);
        // The 10.0f/fire_frequency is somewhat arbitrary, but it makes for good comparison info.
        float fire_freq = myscreen->level_data.myloader->fire_frequency[PIX(Order::Living, last_family)] - current_guy->get_fire_frequency_bonus();
        if(fire_freq < 1)
            fire_freq = 1;
        mytext.write_xy(stat_box_content.x + derived_offset + 21, stat_box_content.y + linesdown*line_height + 4, showcolor, "%.1f", 10.0f/fire_freq);
        
        
		
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
        
        if(arg1 == 1)
        {
            // Show popup on new game
            arg1 = -1;
            popup_dialog("HIRE TROOPS", "Get your team started here\nby hiring some fresh recruits.");
            
	            // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	            localbuttons = init_buttons(buttons, num_buttons);
        }
	}
	
	myscreen->clearbuffer();
	//myscreen->clearscreen();
	return REDRAW;
}

Sint32 create_train_menu(Sint32 arg1)
{
	float linesdown = 0.0f;
	Sint32 i, retvalue=0;
	unsigned char showcolor;
	Sint32 start_time = query_timer();
	Uint32 current_cost;
	Sint32 clickvalue;
	
    SDL_Rect stat_box = {38, 66, 82, 94};
    SDL_Rect stat_box_inner = {stat_box.x + 4, stat_box.y + 4, stat_box.w - 8, stat_box.h - 8};
    SDL_Rect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};
    
    SDL_Rect info_box_inner = {176, 34, 304-176, 112+22-34};
    SDL_Rect info_box_content = {info_box_inner.x + 4, info_box_inner.y + 4, info_box_inner.w - 8, info_box_inner.h - 8};
    
	if (arg1)
		arg1 = 1;

	// Make sure we have a valid team
	if (myscreen->save_data.team_size < 1)
	{
		popup_dialog("NEED A TEAM!", "You need to\nhire a team\nto train");
		
		return OK;
	}

	myscreen->clearbuffer();

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	
	#ifdef DISABLE_MULTIPLAYER
	trainmenu_buttons[18].hidden = true;
	#endif
    
	button* buttons = trainmenu_buttons;
	int num_buttons = 20;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	
	for (i=2; i < 14; i++)
	{
		if (!(i%2)) // 2, 4, ..., 12
			allbuttons[i]->set_graphic(FAMILY_MINUS);
		else
			allbuttons[i]->set_graphic(FAMILY_PLUS);
	}

	
	auto& ourteam = myscreen->save_data.team_list;
	
	// Set to first guy on list using global variable ..
	cycle_team_guy(0);
    guy* here = ourteam[editguy].get();
    current_cost = calculate_train_cost(here);

	grab_mouse();
	
    clear_keyboard();
    
    clear_key_press_event();

	while ( !(retvalue & EXIT) )
	{
	    // Input
		clickvalue = leftmouse(buttons);
		if (clickvalue == 1)
			retvalue = localbuttons->leftclick();
		else if (clickvalue == 2)
			retvalue = localbuttons->rightclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        if(localbuttons && (retvalue == OK || retvalue == REDRAW))
        {
            if(retvalue == REDRAW)
            {
					localbuttons = init_buttons(buttons, num_buttons);
				
				for (i=2; i < 14; i++)
				{
					if (!(i%2)) // 2, 4, ..., 12
						allbuttons[i]->set_graphic(FAMILY_MINUS);
					else
						allbuttons[i]->set_graphic(FAMILY_PLUS);
				}
				cycle_team_guy(0);
            }
            
            if (!current_guy)
                cycle_team_guy(0);
            if (here != ourteam[editguy].get())
                here = ourteam[editguy].get();
            current_cost = calculate_train_cost(here);
            retvalue = 0;
        }
		
        //current_cost = calculate_train_cost(here);
        
		// Draw
		myscreen->clearbuffer();
		
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
		show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
		

        linesdown = 0;

        myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
        myscreen->draw_text_bar(36, 10, 124, 22);
        
        text& mytext = myscreen->text_normal;
        mytext.write_xy(80 - mytext.query_width(current_guy->name.c_str())/2, 14,
                         current_guy->name.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        myscreen->draw_button(38, 66, 120, 160, 1, 1); // stats box
        myscreen->draw_text_bar(42, 70, 116, 156);

        
        bool level_increased = (old_guy->level < current_guy->level);
        bool stat_increased;
        if(level_increased)
            stat_increased = false;
        else
            stat_increased = (old_guy->strength < current_guy->strength
                   || old_guy->dexterity < current_guy->dexterity
                   || old_guy->constitution < current_guy->constitution
                   || old_guy->intelligence < current_guy->intelligence
                   || old_guy->armor < current_guy->armor);

        // Strength
        message = std::format("{}", current_guy->strength);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "  STR:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_LEVELED;
        else if (here->strength < current_guy->strength)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message.c_str(), showcolor, 1);

        // Dexterity
        message = std::format("{}", current_guy->dexterity);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "  DEX:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_LEVELED;
        else if (here->dexterity < current_guy->dexterity)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message.c_str(), showcolor, 1);

        // Constitution
        message = std::format("{}", current_guy->constitution);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "  CON:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_LEVELED;
        else if (here->constitution < current_guy->constitution)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message.c_str(), showcolor, 1);

        // Intelligence
        message = std::format("{}", current_guy->intelligence);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "  INT:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_LEVELED;
        else if (here->intelligence < current_guy->intelligence)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message.c_str(), showcolor, 1);

        // Armor
        message = std::format("{}", current_guy->armor);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "ARMOR:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_LEVELED;
        else if (here->armor < current_guy->armor)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message.c_str(), showcolor, 1);

        // Level
        message = std::format("{}", current_guy->level);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "LEVEL:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_CHANGED;
        else if(stat_increased)
            showcolor = STAT_DISABLED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message.c_str(), showcolor, 1);


        // Info box
        myscreen->draw_button(174, 32, 306, 114+22, 1, 1); // info box
        myscreen->draw_text_bar(176, 34, 304, 112+22); // main text box
        
	        showcolor = DARK_BLUE;
	        linesdown = 0.0f;
	        int line_height = 10;
	        auto info_y = [&](float line) -> Sint32 {
	            return info_box_content.y + static_cast<Sint32>(line * static_cast<float>(line_height));
	        };
		
		int derived_offset = 3*STAT_NUM_OFFSET/4;
		
        message = std::format("Total Kills: {}", current_guy->kills);
	        mytext.write_xy(180, info_y(linesdown), message.c_str(), DARK_BLUE, 1);

        linesdown++;
        if (current_guy->total_hits && current_guy->total_shots) // have we at least hit something? :)
        {
            message = std::format("   Accuracy: {}% ",
                    (current_guy->total_hits*100)/current_guy->total_shots);
	            mytext.write_xy(180, info_y(linesdown), message.c_str(), DARK_BLUE, 1);
        }
        else // haven't ever hit anyone
        {
	            mytext.write_xy(180, info_y(linesdown), "   Accuracy: N/A ", DARK_BLUE, 1);
        }

        linesdown++;
        message = std::format(" EXPERIENCE: {}", current_guy->exp);
	        mytext.write_xy(180, info_y(linesdown), message.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        
        
        linesdown++;
		// Separator bar
			SDL_Rect r = {info_box_content.x + 10, info_y(linesdown) - 2, info_box_content.w - 20, 2};
			myscreen->draw_button_inverted(r);
        
        linesdown += 0.4f;
        
	        mytext.write_xy(info_box_content.x, info_y(linesdown), "HP:", STAT_DERIVED, 1);
	        mytext.write_xy(info_box_content.x + derived_offset - 9, info_y(linesdown), HIGH_HP_COLOR, "%.0f", ceilf(myscreen->level_data.myloader->hitpoints[PIX(Order::Living, current_guy->family)] + current_guy->get_hp_bonus()));
        
	        mytext.write_xy(info_box_content.x + derived_offset + 18, info_y(linesdown), "MP:", STAT_DERIVED, 1);
	        mytext.write_xy(info_box_content.x + 2*derived_offset + 18 - 9, info_y(linesdown), MAX_MP_COLOR, "%.0f", ceilf(current_guy->get_mp_bonus()));
		
		linesdown++;
	        mytext.write_xy(info_box_content.x, info_y(linesdown), "ATK:", STAT_DERIVED, 1);
	        mytext.write_xy(info_box_content.x + derived_offset - 3, info_y(linesdown), showcolor, "%.0f", myscreen->level_data.myloader->damage[PIX(Order::Living, current_guy->family)] + current_guy->get_damage_bonus());
        
	        mytext.write_xy(info_box_content.x + derived_offset + 18, info_y(linesdown), "DEF:", STAT_DERIVED, 1);
	        mytext.write_xy(info_box_content.x + 2*derived_offset + 18 - 3, info_y(linesdown), showcolor, "%.0f", current_guy->get_armor_bonus());
		
		linesdown++;
	        mytext.write_xy(info_box_content.x, info_y(linesdown), "SPD:", STAT_DERIVED, 1);
	        mytext.write_xy(info_box_content.x + derived_offset, info_y(linesdown), showcolor, "%.1f", myscreen->level_data.myloader->stepsizes[PIX(Order::Living, current_guy->family)] + current_guy->get_speed_bonus());
        
		linesdown++;
	        mytext.write_xy(info_box_content.x, info_y(linesdown), "ATK SPD:", STAT_DERIVED, 1);
        float fire_freq = myscreen->level_data.myloader->fire_frequency[PIX(Order::Living, current_guy->family)] - current_guy->get_fire_frequency_bonus();
        if(fire_freq < 1)
            fire_freq = 1;
        // The 10.0f/fire_frequency is somewhat arbitrary, but it makes for good comparison info.
	        mytext.write_xy(info_box_content.x + derived_offset + 21, info_y(linesdown), showcolor, "%.1f", 10.0f/fire_freq);
        
        
        linesdown++;
		// Separator bar
			SDL_Rect r2 = {info_box_content.x + 10, info_y(linesdown) - 2, info_box_content.w - 20, 2};
			myscreen->draw_button_inverted(r2);
        
        linesdown += 0.4f;
        message = std::format("CASH: {}", myscreen->save_data.m_totalcash[current_guy->teamnum]);
	        mytext.write_xy(180, info_y(linesdown), message.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);

        linesdown++;
	        mytext.write_xy(180, info_y(linesdown), "COST: ", DARK_BLUE, 1);
        message = std::format("      {}", current_cost );
        if (current_cost > myscreen->save_data.m_totalcash[current_guy->teamnum])
	            mytext.write_xy(180, info_y(linesdown), message.c_str(), STAT_CHANGED, 1);
	        else
	            mytext.write_xy(180, info_y(linesdown), message.c_str(), STAT_COLOR, 1);

        // Display our team setting ..
        message = std::format("Playing on Team {}", current_guy->teamnum+1);
        allbuttons[18]->label = message;
        allbuttons[18]->vdisplay();

        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	myscreen->clearbuffer();
	//myscreen->clearscreen();
	return REDRAW;
}

Sint32 create_load_menu(Sint32 arg1)
{
	Sint32 retvalue=0;
	Sint32 i;
	std::string temp_filename;
	text& loadtext = myscreen->text_normal;
	std::string menu_message;

	if (arg1)
		arg1 = 1;

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	button* buttons = loadteam_buttons;
	int num_buttons = 11;
	int highlighted_button = 10;
	localbuttons = init_buttons(buttons, num_buttons);

	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
        {
			retvalue = localbuttons->leftclick();
			if(retvalue == REDRAW)
            {
                return REDRAW;
            }
        }
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        if(retvalue == REDRAW)
        {
            return REDRAW;
        }
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        myscreen->draw_button(15,  9, 255, 199, 1, 1);
        myscreen->draw_text_bar(19, 13, 251, 21);
        menu_message = "Gladiator: Load Game";
        loadtext.write_xy(135-(static_cast<int>(menu_message.size())*3), 15, menu_message.c_str(), RED, 1);
        for (i=0; i < 10; i++)
        {
            temp_filename = std::format("save{}", i+1);
            allbuttons[i]->label = get_saved_name(temp_filename.c_str());
            myscreen->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
            allbuttons[i]->vdisplay();
            myscreen->draw_box(allbuttons[i]->xloc-1,
                               allbuttons[i]->yloc-1,
                               allbuttons[i]->xend,
                               allbuttons[i]->yend, 0, 0, 1);
        }
        myscreen->draw_text_bar(23, allbuttons[10]->yloc-2, 66, allbuttons[10]->yend+1);
        allbuttons[10]->vdisplay();
        myscreen->draw_box(allbuttons[10]->xloc-1,
                           allbuttons[10]->yloc-1,
                           allbuttons[10]->xend,
                           allbuttons[10]->yend, 0, 0, 1);
                           
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
	return REDRAW;
}


Sint32 create_save_menu(Sint32 arg1)
{
	Sint32 retvalue=0;
	Sint32 i;
	std::string temp_filename;
	text& savetext = myscreen->text_normal;
	std::string menu_message;

	if (arg1)
		arg1 = 1;

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	button* buttons = saveteam_buttons;
	int num_buttons = 11;
	int highlighted_button = 10;
	localbuttons = init_buttons(buttons, num_buttons);
	

	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
        {
			retvalue = localbuttons->leftclick();
			if(retvalue == REDRAW)
            {
                return REDRAW;
            }
        }
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        if(retvalue == REDRAW)
        {
            return REDRAW;
        }
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        myscreen->draw_button(15,  9, 255, 199, 1, 1);
        myscreen->draw_text_bar(19, 13, 251, 21);
        menu_message = "Gladiator: Save Game";
        savetext.write_xy(135-(static_cast<int>(menu_message.size())*3), 15, menu_message.c_str(), RED, 1);
        for (i=0; i < 10; i++)
        {
            temp_filename = std::format("save{}", i+1);
            allbuttons[i]->label = get_saved_name(temp_filename.c_str());
            myscreen->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
            allbuttons[i]->vdisplay();
            myscreen->draw_box(allbuttons[i]->xloc-1,
                               allbuttons[i]->yloc-1,
                               allbuttons[i]->xend,
                               allbuttons[i]->yend, 0, 0, 1);
        }
        myscreen->draw_text_bar(23, allbuttons[10]->yloc-2, 66, allbuttons[10]->yend+1);
        allbuttons[10]->vdisplay();
        myscreen->draw_box(allbuttons[10]->xloc-1,
                           allbuttons[10]->yloc-1,
                           allbuttons[10]->xend,
                           allbuttons[10]->yend, 0, 0, 1);
                           
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	return REDRAW;

}

Sint32 increase_stat(Sint32 whatstat, Sint32 howmuch)
{
    bool level_increased = (old_guy->level < current_guy->level);
    bool stat_increased;
    const short delta = static_cast<short>(howmuch);
    if(level_increased)
    {
        stat_increased = false;
    }
    else
    {
        stat_increased = (old_guy->strength < current_guy->strength
               || old_guy->dexterity < current_guy->dexterity
               || old_guy->constitution < current_guy->constitution
               || old_guy->intelligence < current_guy->intelligence
               || old_guy->armor < current_guy->armor);
    }
    
	switch(whatstat)
		{
			case BUT_STR:
				if(!level_increased)
	                current_guy->strength = static_cast<short>(static_cast<Sint32>(current_guy->strength) + delta);
				break;
			case BUT_DEX:
				if(!level_increased)
	                current_guy->dexterity = static_cast<short>(static_cast<Sint32>(current_guy->dexterity) + delta);
				break;
			case BUT_CON:
				if(!level_increased)
	                current_guy->constitution = static_cast<short>(static_cast<Sint32>(current_guy->constitution) + delta);
				break;
			case BUT_INT:
				if(!level_increased)
	                current_guy->intelligence = static_cast<short>(static_cast<Sint32>(current_guy->intelligence) + delta);
				break;
			case BUT_ARMOR:
				if(!level_increased)
	                current_guy->armor = static_cast<short>(static_cast<Sint32>(current_guy->armor) + delta);
				break;
			case BUT_LEVEL:
			    if(!stat_increased)
			    {
	                short newlevel = static_cast<short>(static_cast<Sint32>(current_guy->level) + delta);
	                current_guy->upgrade_to_level(newlevel);
			    }
				break;
			default:
			break;
	}
	
	return OK;
}

Sint32 decrease_stat(Sint32 whatstat, Sint32 howmuch)
{
    bool level_increased = (old_guy->level < current_guy->level);
    bool stat_increased;
    const short delta = static_cast<short>(howmuch);
    if(level_increased)
    {
        stat_increased = false;
    }
    else
    {
        stat_increased = (old_guy->strength < current_guy->strength
               || old_guy->dexterity < current_guy->dexterity
               || old_guy->constitution < current_guy->constitution
               || old_guy->intelligence < current_guy->intelligence
               || old_guy->armor < current_guy->armor);
    }
    
	switch(whatstat)
		{
			case BUT_STR:
			    if(!level_increased)
	                current_guy->strength = static_cast<short>(static_cast<Sint32>(current_guy->strength) - delta);
				break;
			case BUT_DEX:
				if(!level_increased)
	                current_guy->dexterity = static_cast<short>(static_cast<Sint32>(current_guy->dexterity) - delta);
				break;
			case BUT_CON:
				if(!level_increased)
	                current_guy->constitution = static_cast<short>(static_cast<Sint32>(current_guy->constitution) - delta);
				break;
			case BUT_INT:
				if(!level_increased)
	                current_guy->intelligence = static_cast<short>(static_cast<Sint32>(current_guy->intelligence) - delta);
				break;
			case BUT_ARMOR:
				if(!level_increased)
	                current_guy->armor = static_cast<short>(static_cast<Sint32>(current_guy->armor) - delta);
				break;
			case BUT_LEVEL:
				if(!stat_increased)
	            {
				    short newlevel = static_cast<short>(static_cast<Sint32>(current_guy->level) - delta);
				    if(newlevel > 0 && newlevel >= myscreen->save_data.team_list[editguy]->level)
	                {
	                    current_guy->upgrade_to_level(newlevel);
	                    if(current_guy->level == myscreen->save_data.team_list[editguy]->level)
                        current_guy->exp = myscreen->save_data.team_list[editguy]->exp;
                }
			}
			break;
		default:
			break;
	}
	
	return OK;
}

Uint32 calculate_hire_cost()
{
	guy  *ob = current_guy.get();
	Sint32 temp;
	Sint32 myfamily;

	if (!ob)
		return 0;

	myfamily = ob->family;
	temp = costlist[myfamily];

		// Long check of various things ..
		if (ob->strength < statlist[myfamily][BUT_STR])
			ob->strength = static_cast<short>(statlist[myfamily][BUT_STR]);
		if (ob->dexterity < statlist[myfamily][BUT_DEX])
			ob->dexterity = static_cast<short>(statlist[myfamily][BUT_DEX]);
		if (ob->constitution < statlist[myfamily][BUT_CON])
			ob->constitution = static_cast<short>(statlist[myfamily][BUT_CON]);
		if (ob->intelligence < statlist[myfamily][BUT_INT])
			ob->intelligence = static_cast<short>(statlist[myfamily][BUT_INT]);
		if (ob->armor < statlist[myfamily][BUT_ARMOR])
			ob->armor = static_cast<short>(statlist[myfamily][BUT_ARMOR]);

	// Now figure out costs ..
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_STR]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_DEX]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_CON]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_INT]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_ARMOR]));
    
		if (ob->level < statlist[myfamily][BUT_LEVEL])
			ob->upgrade_to_level(static_cast<short>(statlist[myfamily][BUT_LEVEL]));
		
	if (static_cast<Sint32>(calculate_exp(ob->level)) < 0) // overflow
		ob->upgrade_to_level(1);
    
	temp += static_cast<Sint32>(calculate_exp(ob->level));
	
	if (temp < 0)
	{
		//guytemp = new guy(current_guy->family);
		//delete current_guy;
		//current_guy = guytemp;
		cycle_guy(0);
		//temp = -1;  // This used to be an error code checked by picker.cpp line 2213
		temp = 0;
	}
	return static_cast<Uint32>(temp);
}

// This version compares current_guy versus the old version ..
Uint32 calculate_train_cost(guy  *oldguy)
{
	guy  *ob = current_guy.get();
	Sint32 temp;
	Sint32 myfamily;

	if (!ob || !oldguy)
		return 0;

	myfamily = ob->family;
	temp = 0;

	// Long check of various things ..
	if (ob->strength < oldguy->strength)
		ob->strength = oldguy->strength;
	if (ob->dexterity < oldguy->dexterity)
		ob->dexterity = oldguy->dexterity;
	if (ob->constitution < oldguy->constitution)
		ob->constitution = oldguy->constitution;
	if (ob->intelligence < oldguy->intelligence)
		ob->intelligence = oldguy->intelligence;
	if (ob->armor < oldguy->armor)
		ob->armor = oldguy->armor;

	// Now figure out costs ..
    
	// Add on extra level cost ..
	if (ob->level < oldguy->level)
		ob->upgrade_to_level(oldguy->level);
	if (calculate_exp(ob->level) > oldguy->exp)
		temp += static_cast<Sint32>(calculate_exp(ob->level) - oldguy->exp);

	if (ob->level <= old_guy->level) // Only count these costs if the level is not being upgraded
    {
	// First we have our 'total increased value..'
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_STR]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_DEX]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_CON]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_INT]));
	temp += static_cast<Sint32>((pow( static_cast<Sint32>(ob->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_ARMOR]));

	// Now subtract what we've already paid for ..
	temp -= static_cast<Sint32>((pow( static_cast<Sint32>(oldguy->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_STR]));
	temp -= static_cast<Sint32>((pow( static_cast<Sint32>(oldguy->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_DEX]));
	temp -= static_cast<Sint32>((pow( static_cast<Sint32>(oldguy->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_CON]));
	temp -= static_cast<Sint32>((pow( static_cast<Sint32>(oldguy->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_INT]));
	temp -= static_cast<Sint32>((pow( static_cast<Sint32>(oldguy->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * static_cast<Sint32>(statcosts[myfamily][BUT_ARMOR]));
    }
    

	if (temp < 0)
	{
		temp = 0;
		statscopy(current_guy.get(), oldguy);
		cycle_team_guy(0);

	}

	return static_cast<Uint32>(temp);
}


#define GET_RAND_ELEM(array) (array[rand()%ARRAY_SIZE(array)])

const char* archer_names[] = {
    "Robin", "Green Arrow", 
    "Legolas", 
    "Yeoman", "Strider", "Longshot", "Bowyer", "Hunter", "Archy"
};

const char* cleric_names[] = {
    "Tuck", 
    "Brother", "Pater", "Drake", "Friar", "Francis", "John Paul", "Medic"
};

const char* druid_names[] = {
    "Roland", 
    "Merlin", 
    "Hippy", "Green Thumb", "Treefall", "Rain"
};

const char* elf_names[] = {
    "Legolas", "Took", "Elrond", 
    "Tanis", 
    "Acorn", "Lightfoot", "Treewee"
};

const char* mage_names[] = {
    "Gandalf", "Saruman", "Radagast", "Alatar", "Pallando", 
    "Raistlin", "Fizban", "Mordenkainen", 
    "Merlin", 
    "Harry", 
    "Manannan", "Mordack", 
    "Jace"
};

const char* soldier_names[] = {
    "Lothar", 
    "Arthur", "Uther", 
    "Achilles", "Lu Bu", "Wallace", "Leonidas", "Attila", "Alexander", "Ajax", "Nestor", "Priam", "Hector", 
    "Tom", "Bigfoot"
};

const char* thief_names[] = {
    "Shinobi", 
    "Dismas", 
    "Shadow", "Stabby", "Swiftstrike", "Scourge", "Rogue"
};

const char* orc_names[] = {
    "Grom", 
    "Thrull", 
    "Vernix", "Lanugo", 
    "Grok", "Horde", "Grog", "Krosh"
};

const char* barbarian_names[] = {
    "Thor", 
    "Conan", 
    "Beowulf", "Cronus", "Pallas", "Atlas", "Prometheus", 
    "Titan"
};

const char* elemental_names[] = {
    "Furnace", "Molten", "Burns", "Fire Eli", "Fireball", "Sunny", "Lava", "Heatwave", "Torch", "Scorch"
};

const char* skeleton_names[] = {
    "Drybones", 
    "Blackbeard", 
    "Boney", "Femur", "Patella", "Humerus", "Scapula"
};

const char* slime_names[] = {
    "Grimer", 
    "Goop", "Slurp", "Glopp", "Sludge", "Blob"
};

const char* faerie_names[] = {
    "Tink", 
    "Gem", "Glitter", "Jewel", "Blossom", "Ruby", "Muffin", "Flutter", "Sparkle", "Sprint", "Sprite", "Eve", "Twinkle", "Violet", "Daisy", "Lily"
};

const char* ghost_names[] = {
    "Casper", 
    "Slimer", 
    "Reaper", "Ecto", "Pepper", "Boo", "Banshee", "Nyx"
};

const char* get_random_name(unsigned char family)
{
	switch(family)
	{
		case FAMILY_ARCHER:
			return GET_RAND_ELEM(archer_names);
		case FAMILY_CLERIC:
			return GET_RAND_ELEM(cleric_names);
		case FAMILY_DRUID:
			return GET_RAND_ELEM(druid_names);
		case FAMILY_ELF:
			return GET_RAND_ELEM(elf_names);
		case FAMILY_MAGE:
			return GET_RAND_ELEM(mage_names);
		case FAMILY_SOLDIER:
			return GET_RAND_ELEM(soldier_names);
		case FAMILY_THIEF:
			return GET_RAND_ELEM(thief_names);
		case FAMILY_ARCHMAGE:
			return GET_RAND_ELEM(mage_names);
		case FAMILY_ORC:
			return GET_RAND_ELEM(orc_names);
		case FAMILY_BIG_ORC:
			return GET_RAND_ELEM(orc_names);
		case FAMILY_BARBARIAN:
			return GET_RAND_ELEM(barbarian_names);
		case FAMILY_FIREELEMENTAL:
			return GET_RAND_ELEM(elemental_names);
		case FAMILY_SKELETON:
			return GET_RAND_ELEM(skeleton_names);
		case FAMILY_SLIME:
		case FAMILY_MEDIUM_SLIME:
		case FAMILY_SMALL_SLIME:
			return GET_RAND_ELEM(slime_names);
		case FAMILY_FAERIE:
			return GET_RAND_ELEM(faerie_names);
		case FAMILY_GHOST:
			return GET_RAND_ELEM(ghost_names);
		default:
			return GET_RAND_ELEM(soldier_names);
	}
}

bool has_name_in_team(const char* name)
{
    auto& ourteam = myscreen->save_data.team_list;
    int team_size = myscreen->save_data.team_size;
    
    for(int i = 0; i < team_size; i++)
    {
        if(ourteam[i]->name == name)
            return true;
    }
    
    return false;
}

const char* get_new_name(unsigned char family)
{
    static std::string new_name_buffer;
    const char* result = get_random_name(family);

    // Try a few times to get a unique name
    int i = 0;
    while(has_name_in_team(result) && i < 10)
    {
        result = get_random_name(family);
        i++;
    }

    // A bare name is a duplicate?
    if(has_name_in_team(result))
    {
        // Append a number
        i = 2;
        do
        {
            new_name_buffer = std::format("{}{}", result, i);
            i++;
        }
        while(has_name_in_team(new_name_buffer.c_str()));

        result = new_name_buffer.c_str();
    }

    return result;
}

Sint32 cycle_guy(Sint32 whichway)
{
	Sint32 newfamily;

	if (!current_guy)
		newfamily = allowable_guys[0];
	else
	{
		current_type = current_type + whichway + static_cast<Sint32>(allowable_guys.size());
		current_type %= static_cast<Sint32>(allowable_guys.size());
		if (current_type < 0)
			current_type = static_cast<Sint32>(allowable_guys.size()) - 1;
		newfamily = allowable_guys[current_type];
		//newfamily = current_guy->family + whichway;
	}

	//newfamily = (newfamily + NUM_FAMILIES) % NUM_FAMILIES;

	// Make the new guy
	current_guy = std::make_unique<guy>(newfamily);
	current_guy->teamnum = current_team_num;
		current_guy->name = get_new_name(static_cast<unsigned char>(newfamily));

	show_guy(0, 0);

	//myscreen->buffer_to_screen(52, 24, 108, 64);

	grab_mouse();
	
	return OK;
}

	void show_guy(Sint32 frames, Sint32 who, Sint32 centerx, Sint32 centery) // shows the current guy ..
	{
		std::unique_ptr<walker> mywalker;
		Sint32 i;
		char newfamily;

	if (!current_guy)
		return;

	frames = abs(frames);

		(void)who; // always show current_guy
		newfamily = current_guy->family;

		mywalker = myscreen->level_data.myloader->create_walker_owned(Order::Living, newfamily, myscreen);
		mywalker->stats()->bit_flags = 0;
		mywalker->curdir = static_cast<signed char>(((frames/192) + FACE_DOWN)%8);
		mywalker->ani_type = ANI_WALK;
		for (i=0; i <= (frames/12)%4; i++)
			mywalker->animate();
		//mywalker->team_num = ourteam[editguy]->teamnum;
		mywalker->team_num = static_cast<unsigned char>(current_guy->teamnum);

	mywalker->setxy(centerx - (mywalker->sizex/2), centery - (mywalker->sizey/2));
	myscreen->draw_button(centerx - 80 + 54, centery - 45 + 26, centerx - 80 + 106, centery - 45 + 64, 1, 1);
	myscreen->draw_text_bar(centerx - 80 + 56, centery - 45 + 28, centerx - 80 + 104, centery - 45 + 62);
	draw_walker(*mywalker, myscreen->viewob[0].get());
}
// Sets current_guy to 'whichguy' in the teamlist, and
// returns a COPY of him as the function result
Sint32 cycle_team_guy(Sint32 whichway)
{
	if (myscreen->save_data.team_size < 1)
		return -1;
    
    auto& ourteam = myscreen->save_data.team_list;
    
	editguy += whichway;
	if (editguy < 0)
	{
		editguy += MAX_TEAM_SIZE;
		while (!ourteam[editguy])
			editguy--;
	}

	if (editguy < 0 || editguy >= MAX_TEAM_SIZE)
		editguy = 0;

	if (!whichway && !ourteam[editguy])
		whichway = 1;

	while (!ourteam[editguy])
	{
		editguy += whichway;
		if (editguy < 0 || editguy >= MAX_TEAM_SIZE)
			editguy = 0;
	}

	current_guy = std::make_unique<guy>(ourteam[editguy]->family);
	statscopy(current_guy.get(), ourteam[editguy].get());
	old_guy = ourteam[editguy].get();

	show_guy(0, 0);

	current_team_num = current_guy->teamnum;

	// Set our team button back to normal color ..
	// Zardus: FIX: added a check for null pointers
	if (allbuttons[18])
		allbuttons[18]->do_outline = 0;

	return OK;
}

Sint32 add_guy(guy *newguy)
{
	Sint32 i;

	for (i=0; i < MAX_TEAM_SIZE; i++)
    {
		if (!myscreen->save_data.team_list[i])
		{
			myscreen->save_data.team_list[i].reset(newguy);
			myscreen->save_data.team_size++;
			return i;
		}
    }

	// failed the case; too many guys
	return -1;
}

Sint32 name_guy(Sint32 arg)  // 0 == current_guy, 1 == ourteam[editguy]
{
	text& nametext = myscreen->text_normal;
	guy *someguy;

	if (arg)
		someguy = myscreen->save_data.team_list[editguy].get();
	else
		someguy = current_guy.get();

	if (!someguy)
		return REDRAW;

	release_mouse();
	
	myscreen->draw_button(174,  8, 306, 30, 1, 1); // text box
	nametext.write_xy(176, 12, "NAME THIS CHARACTER:", DARK_BLUE, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	
	clear_keyboard();
    std::optional<std::string> new_text = nametext.input_string_value(176, 20, 11, someguy->name.c_str());
	if(new_text.has_value())
		someguy->name = *new_text;
	myscreen->draw_button(174,  8, 306, 30, 1, 1); // text box

	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	return REDRAW;
}

Sint32 add_guy([[maybe_unused]] Sint32 ignoreme)
{
	Sint32 newfamily = current_guy->family;
	//buffers: changed typename to type_name due to some compile error
	std::string type_name;
	Sint32 i;

	if (myscreen->save_data.team_size >= MAX_TEAM_SIZE) // abort abort!
		return -1;

	if (!current_guy) // we should be adding current_guy
		return -1;

    Uint32 cost = calculate_hire_cost();
	if (cost == 0 || cost > myscreen->save_data.m_totalcash[current_team_num])
		return OK;

	myscreen->save_data.m_totalcash[current_team_num] -= cost;
    
    auto& ourteam = myscreen->save_data.team_list;
	for (i=0; i < MAX_TEAM_SIZE; i++)
    {
		if (!ourteam[i]) // found an empty slot
		{
			current_guy->teamnum = current_team_num;
			ourteam[i] = std::move(current_guy);
			myscreen->save_data.team_size++;
			release_mouse();
			
			std::string name = ourteam[i]->name;
			if(prompt_for_string("NAME THIS CHARACTER", name))
                ourteam[i]->name = name;
            
			grab_mouse();

			// Increment the next guy's number
			numbought[newfamily]++;

			// Ensure we have the right exp for our level
			ourteam[i]->exp = calculate_exp(ourteam[i]->level);

			// Grab a new, generic guy to be edited/bought
			current_guy = std::make_unique<guy>(newfamily);
			type_name = current_guy->name;
			statscopy(current_guy.get(), ourteam[i].get()); // set to same stats as just bought
			current_guy->name = type_name;
            current_guy->name = get_new_name(static_cast<unsigned char>(newfamily));

			// Return okay status
			return OK;
		}
    }

	return OK;
}

// Accept changes ..
Sint32 edit_guy(Sint32 arg1)
{
	guy *here;
	MouseState& cheatmouse = query_mouse();

	if (arg1)
		arg1 = 1;

	if (!current_guy)
		return -1;

	here = myscreen->save_data.team_list[editguy].get();
	if (!here)
		return -1;  // error case; should never happen

	// This is for cheating! Only CHEAT :)
	// When holding down the right mouse button, can always accept free changes
	if (CHEAT_MODE && cheatmouse.right)
	{
		if (here->level != current_guy->level)
			current_guy->upgrade_to_level(current_guy->level);
		statscopy(here, current_guy.get());
		return OK;
	}
    
    Uint32 cost = calculate_train_cost(here);
	if (cost > myscreen->save_data.m_totalcash[current_guy->teamnum])  // compare cost of here to current_guy
		return OK;

	myscreen->save_data.m_totalcash[current_guy->teamnum] -= cost;  // cost of new - old (current_guy - here)

    if (here->level != current_guy->level)
    {
        current_guy->upgrade_to_level(current_guy->level);
    }
	statscopy(here, current_guy.get());

	// Color our team button normally
	allbuttons[18]->do_outline = 0;

	return OK;
}

Sint32 how_many(Sint32 whatfamily)    // how many guys of family X on the team?
{
	Sint32 counter = 0;
	Sint32 i;

	for (i=0; i < MAX_TEAM_SIZE; i++)
    {
		if (myscreen->save_data.team_list[i] && myscreen->save_data.team_list[i]->family == whatfamily)
			counter++;
    }

	return counter;
}

Sint32 do_save(Sint32 arg1)
{
	release_mouse();
	clear_keyboard();
	
	std::string name = allbuttons[arg1-1]->label;
	if(prompt_for_string("NAME YOUR SAVED GAME", name))
    {
        myscreen->save_data.save_name = name;
        
        std::string newname = std::format("save{}", arg1);
        if(myscreen->save_data.save(newname))
            timed_dialog("GAME SAVED");
        else
            timed_dialog("SAVE FAILED");
    }
    else
    {
        timed_dialog("SAVE CANCELED");
    }

    grab_mouse();

	return REDRAW;
}

Sint32 do_load(Sint32 arg1)
{
	std::string newname = std::format("save{}", arg1);

	if(myscreen->save_data.load(newname))
    {
        timed_dialog("GAME LOADED");
    }
    else
    {
        timed_dialog("LOAD FAILED");
    }

    return REDRAW;
}

std::string get_saved_name(const char * filename)
{
	SDL_RWops  *infile;
	std::string temp_filename;

	char temptext[10] = "GTL";
    std::array<char, 40> savedgame{};
	char temp_version = 1;
	short temp_registered;

	// This only uses the first segment of the save format.
	// See load_team_list() for full format
	
	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number (from graph.h)
	// 2-bytes registered mark, version 7+ only
	// 40-byte saved-game name (version 2 and up only!)
	//   .
	//   .

	temp_filename = std::format("{}.gtl", filename); // gladiator team list

	if ( (infile = open_read_file("save/", temp_filename.c_str())) == nullptr ) // open for read
	{
		return std::string("EMPTY SLOT");
	}

	// Read id header
	SDL_RWread(infile, temptext, 3, 1);
	if ( std::string(temptext) != "GTL")
	{
	    SDL_RWclose(infile);
		return std::string("EMPTY SLOT");
	}

	// Read version number
	SDL_RWread(infile, &temp_version, 1, 1);
	if (temp_version != 1)
	{
		if (temp_version >= 2)
		{
			if (temp_version >= 7)
				SDL_RWread(infile, &temp_registered, 2, 1);
			SDL_RWread(infile, savedgame.data(), 40, 1);
		}
		else
		{
            SDL_RWclose(infile);
			return std::string("SAVED GAME");
		}
	}
	else
		return std::string("SAVED GAME");

    SDL_RWclose(infile);
    const size_t name_len = strnlen(savedgame.data(), savedgame.size());
    return std::string(savedgame.data(), name_len);
}

Sint32 delete_all()
{
	Sint32 counter = myscreen->save_data.team_size;

	for (int i = 0; i < myscreen->save_data.team_size; i++)
    {
        myscreen->save_data.team_list[i].reset();
    }
    
    myscreen->save_data.team_size = 0;

	return counter;
}

Sint32 go_menu(Sint32 arg1)
{
	// Save the current team in memory to save0.gtl, and
	// run gladiator.

	if (arg1)
		arg1 = 1;

    // Make sure we have a valid team
    if (myscreen->save_data.team_size < 1)
    {
        popup_dialog("NEED A TEAM!", "Please hire a\nteam before\nstarting the level");

        return REDRAW;
    }

#ifdef __EMSCRIPTEN__
    // For Emscripten: Set flag and return EXIT to unwind all menu loops
    // The state machine in main() will handle starting the game
    myscreen->save_data.save("save0");

    current_guy.reset();

    picker_request_start_game();
    Log("go_menu: Setting g_start_game_requested, returning EXIT\n");
    return EXIT;  // This will unwind all menu loops back to picker_main/picker_frame
#else
    // Native build: use blocking loop
    do
    {
        myscreen->save_data.save("save0");
        release_mouse();

        //*******************************
        // Fade out from MENU loop
        //*******************************
        // Zardus: PORT: fade out from menu code now in glad.cpp
        //clear_keyboard();
        //myscreen->fadeblack(0);

        current_guy.reset();

        // Reset viewscreen prefs
        myscreen->ready_for_battle(myscreen->save_data.numplayers);

#ifdef TESTING
        picker_testing_mark_game_start();
#endif
        glad_main(myscreen->save_data.numplayers);
#ifdef TESTING
        picker_testing_mark_game_end();
#endif

        Log("Returned from glad_main, retry={}\n", myscreen->retry);

        //*******************************
        // Fade out from ACTION loop
        //*******************************
        // Zardus: PORT: new fade code
        myscreen->fadeblack(0);

        // Zardus: PORT: doesn't seem to be neccessary
        myscreen->clearbuffer();

        // Zardus: PORT: they had this in just so that the pallettes got reset to
        // normal. It actually faded in a black screen, since fading in the menu
        // would mean messing with a bunch of things. Maybe we'll do the fade in
        // menu later, but for now we'll keep it like they had
        //*******************************
        // Fade in to MENU loop
        //*******************************
        // Zardus: PORT: new fade code
        //myscreen->fadeblack(1);

        grab_mouse();

        myscreen->reset(1);
        myscreen->viewob[0]->resize(PREF_VIEW_FULL);

        SDL_RWops* loadgame = open_read_file("save/", "save0.gtl");
        if (loadgame)
        {
            SDL_RWclose(loadgame);
            myscreen->save_data.load("save0");
        }
    }
    while(myscreen->retry);

	return CREATE_TEAM_MENU;
#endif
}

void statscopy(guy *dest, guy *source)
{
	dest->family = source->family;
	dest->strength = source->strength;
	dest->dexterity = source->dexterity;
	dest->constitution = source->constitution;
	dest->intelligence = source->intelligence;
	dest->level = source->level;
	dest->armor = source->armor;
	dest->exp = source->exp;
	dest->kills = source->kills;
	dest->level_kills = source->level_kills;
	dest->total_damage = source->total_damage;
	dest->total_hits   = source->total_hits;
	dest->total_shots  = source->total_shots;
	dest->teamnum = source->teamnum;
	
	dest->scen_damage = source->scen_damage;
	dest->scen_kills = source->scen_kills;
	dest->scen_damage_taken = source->scen_damage_taken;
	dest->scen_min_hp = source->scen_min_hp;
	dest->scen_shots = source->scen_shots;
	dest->scen_hits = source->scen_hits;

	dest->name = source->name;
}
