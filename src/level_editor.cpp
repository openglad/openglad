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

#include "screen.h"
#include "view.h"
#include "radar.h"
#include "walker.h"
#include "smooth.h"
#include "input.h"
#include "util.h"
#include "text.h"
#include "stats.h"
#include "level_data.h"
#include "level_picker.h"
#include "campaign_picker.h"
#include "sai2x.h"

#ifdef OUYA
#include "OuyaController.h"
#endif
extern short scroll_amount;  // for scrolling up and down text popups

void quit(Sint32 arg1);

/* Changelog
 * 	8/8/02: Zardus: added scrolling-by-minimap
 * 		Zardus: added scrolling-by-keyboard
 */

#define OK 4 //this function was successful, continue normal operation

#include <string>
#include <vector>
using namespace std;
#include <stdlib.h>
#define MINIMUM_TIME 0


#define S_LEFT 1
#define S_RIGHT 245
#define S_UP 1
#define S_DOWN 188

#define VERSION_NUM (char) 8 // save scenario type info
#define SCROLLSIZE 8

#define NUM_BACKGROUNDS PIX_MAX

#define PIX_LEFT   (S_RIGHT+18)
#define PIX_TOP    (S_UP+79)
#define PIX_OVER   4
//#define PIX_DOWN   ((PIX_MAX/PIX_OVER)+1)
#define PIX_DOWN   4
#define PIX_RIGHT  (PIX_LEFT+(PIX_OVER*GRID_SIZE))
#define PIX_BOTTOM (PIX_TOP+(PIX_DOWN*GRID_SIZE))

#define L_D(x) ((S_UP+7)+8*x)
#define L_W(x) (x*8 + 9)
#define L_H(x) (x*8)


int toInt(const std::string& s);

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message, bool dim = false);
void timed_dialog(const char* message, float delay_seconds = 3.0f);

enum ModeEnum {TERRAIN, OBJECT, SELECT};

void set_screen_pos(screen *myscreen, Sint32 x, Sint32 y);
walker * some_hit(Sint32 x, Sint32 y, walker  *ob, LevelData* data);
char get_random_matching_tile(Sint32 whatback);

class EditorTerrainBrush;
class EditorObjectBrush;
void info_box(walker  *target, screen * myscreen);
void set_name(walker  *target, screen * myscreen);

extern screen *myscreen;  // global for scen?

// Zardus: our prefs object from view.cpp
extern options * theprefs;

extern Sint32 *mymouse;

unsigned char scenpalette[768];
Sint32 redraw = 1;  // need to redraw?
Sint32 campaignchanged = 0;  // has campaign changed?
Sint32 levelchanged = 0;  // has level changed?
Sint32 cyclemode = 1;      // for color cycling
//buffers: PORT: changed start_time to start_time_s to avoid conflict with
//input.cpp
Sint32 start_time_s; // for timer ops

class Rect
{
public:
    
    int x, y;
    unsigned int w, h;
    
    Rect();
    Rect(int x, int y, unsigned int w, unsigned int h);
    
    bool contains(int x, int y) const;
};

Rect::Rect()
    : x(0), y(0), w(0), h(0)
{}

Rect::Rect(int x, int y, unsigned int w, unsigned int h)
    : x(x), y(y), w(w), h(h)
{}

bool Rect::contains(int x, int y) const
{
    return (this->x <= x && x < int(this->x + w) && this->y <= y && y < int(this->y + h));
}

class Rectf
{
public:
    
    float x, y;
    float w, h;
    
    Rectf();
    Rectf(float x, float y, float w, float h);
    
    bool contains(float X, float Y) const;
};

Rectf::Rectf()
    : x(0), y(0), w(0), h(0)
{}

Rectf::Rectf(float x, float y, float w, float h)
    : x(x), y(y), w(w), h(h)
{}

bool Rectf::contains(float X, float Y) const
{
    if(w >= 0.0f && h >= 0.0f)
        return (x <= X && x + w >= X && y <= Y && y + h >= Y);
    if(w < 0.0f && h < 0.0f)
        return (x + w <= X && x >= X && y + h <= Y && y >= Y);
    if(w < 0.0f)
        return (x + w <= X && x >= X && y <= Y && y + h >= Y);
    // else h < 0.0f
    return (x <= X && x + w >= X && y + h <= Y && y >= Y);
}

Sint32 backgrounds[] = {
                         PIX_GRASS1, PIX_GRASS2, PIX_GRASS_DARK_1, PIX_GRASS_DARK_2,
                         //PIX_GRASS_DARK_B1, PIX_GRASS_DARK_BR, PIX_GRASS_DARK_R1, PIX_GRASS_DARK_R2,
                         PIX_BOULDER_1, PIX_GRASS_DARK_LL, PIX_GRASS_DARK_UR, PIX_GRASS_RUBBLE,

                         PIX_GRASS_LIGHT_LEFT_TOP, PIX_GRASS_LIGHT_1,
                         PIX_GRASS_LIGHT_RIGHT_TOP, PIX_WATER1,

                         PIX_WATERGRASS_U, PIX_WATERGRASS_D,
                         PIX_WATERGRASS_L, PIX_WATERGRASS_R,

                         PIX_DIRTGRASS_UR1, PIX_DIRT_1, PIX_DIRT_1, PIX_DIRTGRASS_LL1,
                         PIX_DIRTGRASS_LR1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRTGRASS_UL1,

                         PIX_DIRTGRASS_DARK_UR1, PIX_DIRTGRASS_DARK_LL1,
                         PIX_DIRTGRASS_DARK_LR1, PIX_DIRTGRASS_DARK_UL1,

                         PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2,
                         PIX_JAGGED_GROUND_3, PIX_JAGGED_GROUND_4,

                         PIX_PATH_1, PIX_PATH_2, PIX_PATH_3, PIX_PATH_4,
                         PIX_COBBLE_1, PIX_COBBLE_2, PIX_COBBLE_3, PIX_COBBLE_4,

                         //PIX_WALL2, PIX_WALL3, PIX_WALL4, PIX_WALL5,

                         PIX_WALL4, PIX_WALL_ARROW_GRASS,
                         PIX_WALL_ARROW_FLOOR, PIX_WALL_ARROW_GRASS_DARK,

                         PIX_WALL2, PIX_WALL3, PIX_H_WALL1, PIX_WALL_LL,

                         PIX_WALLSIDE_L, PIX_WALLSIDE_C, PIX_WALLSIDE_R, PIX_WALLSIDE1,

                         PIX_WALLSIDE_CRACK_C1, PIX_WALLSIDE_CRACK_C1,
                         PIX_TORCH1, PIX_VOID1,

                         //PIX_VOID1, PIX_FLOOR1, PIX_VOID1, PIX_VOID1,

                         PIX_CARPET_SMALL_TINY, PIX_CARPET_M2, PIX_PAVEMENT1, PIX_FLOOR1,

                         //PIX_PAVEMENT1, PIX_PAVEMENT2, PIX_PAVEMENT3, PIX_PAVEMENT3,
                         PIX_FLOOR_PAVEL, PIX_FLOOR_PAVEU, PIX_FLOOR_PAVED, PIX_FLOOR_PAVED,

                         PIX_WALL_LL,
                         PIX_WALLTOP_H,
                         PIX_PAVESTEPS1,
                         PIX_BRAZIER1,

                         PIX_PAVESTEPS2L, PIX_PAVESTEPS2, PIX_PAVESTEPS2R, PIX_PAVESTEPS1,
                         //PIX_TORCH1, PIX_TORCH2, PIX_TORCH3, PIX_TORCH3,

                         PIX_COLUMN1, PIX_COLUMN2, PIX_COLUMN2, PIX_COLUMN2,

                         PIX_TREE_T1, PIX_TREE_T1, PIX_TREE_T1, PIX_TREE_T1,
                         PIX_TREE_ML, PIX_TREE_M1, PIX_TREE_MT, PIX_TREE_MR,
                         PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,

                         PIX_CLIFF_BACK_L, PIX_CLIFF_BACK_1, PIX_CLIFF_BACK_2, PIX_CLIFF_BACK_R,
                         PIX_CLIFF_LEFT, PIX_CLIFF_BOTTOM, PIX_CLIFF_TOP, PIX_CLIFF_RIGHT,
                         PIX_CLIFF_LEFT, PIX_CLIFF_TOP_L, PIX_CLIFF_TOP_R, PIX_CLIFF_RIGHT,
                     };

class ObjectType
{
public:
    unsigned char order;
    unsigned char family;
    
    ObjectType()
        : order(0), family(0)
    {}
    ObjectType(unsigned char order, unsigned char family)
        : order(order), family(family)
    {}
};

std::vector<ObjectType> object_pane;

Sint32 rowsdown = 0;
Sint32 maxrows = ((sizeof(backgrounds)/4) / 4);

bool save_level_and_map(screen* ascreen);

bool does_campaign_exist(const std::string& campaign_id)
{
    std::list<std::string> ls = list_campaigns();
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
    {
        if(campaign_id == *e)
            return true;
    }
    
    return false;
}

bool create_new_campaign(const std::string& campaign_id)
{
    // Delete the temp directory
    cleanup_unpacked_campaign();
    
    // Create the necessities in the temp directory
    create_dir(get_user_path() + "temp/");
    create_dir(get_user_path() + "temp/pix");
    create_dir(get_user_path() + "temp/scen");
    create_dir(get_user_path() + "temp/sound");
    create_new_pix(get_user_path() + "temp/icon.pix", 32, 32);
    create_new_campaign_descriptor(get_user_path() + "temp/campaign.yaml");
    create_new_scen_file(get_user_path() + "temp/scen/scen1.fss", "scen0001");
    // Create the map file (grid)
    create_new_map_pix(get_user_path() + "temp/pix/scen0001.pix", 40, 60);
    
    bool result = repack_campaign(campaign_id);
    if(!result)
        return result;
    
    cleanup_unpacked_campaign();
    return true;
}

void importCampaignPicker()
{
    // TODO: Browse campaigns online and download some
}

void shareCampaign(screen* myscreen)
{
    // TODO: Send current campaign to the internets!
}


class SimpleButton
{
public:
    SDL_Rect area;
    std::string label;
    bool remove_border;
    bool draw_top_separator;
    int base_color;
    int high_color;
    int shadow_color;
    int text_color;
    bool centered;
    
    SimpleButton(const std::string& label, int x, int y, unsigned int w, unsigned int h, bool remove_border = false, bool draw_top_separator = false);
    
    void draw(screen* myscreen, text* mytext);
    bool contains(int x, int y) const;
    
    void set_colors_normal();
    void set_colors_enabled();
    void set_colors_disabled();
    void set_colors_active();
};


SimpleButton::SimpleButton(const std::string& label, int x, int y, unsigned int w, unsigned int h, bool remove_border, bool draw_top_separator)
    : label(label), remove_border(remove_border), draw_top_separator(draw_top_separator), centered(false)
{
    set_colors_normal();
    
    area.x = x;
    area.y = y;
    area.w = w;
    area.h = h;
}

void SimpleButton::draw(screen* myscreen, text* mytext)
{
    myscreen->draw_button_colored(area.x, area.y, area.x + area.w - 1, area.y + area.h - 1, !remove_border, base_color, high_color, shadow_color);
    if(remove_border && draw_top_separator)
        myscreen->hor_line(area.x, area.y, area.w, shadow_color);
    
    if(centered)
        mytext->write_xy(area.x + area.w/2 - 3*label.size(), area.y + area.h/2 - 2, label.c_str(), text_color, 1);
    else
        mytext->write_xy(area.x + 2, area.y + area.h/2 - 2, label.c_str(), text_color, 1);
}

bool SimpleButton::contains(int x, int y) const
{
    return (area.x <= x && x < area.x + area.w
            && area.y <= y && y < area.y + area.h);
}

void SimpleButton::set_colors_normal()
{
    text_color = DARK_BLUE;
    base_color = 13;
    high_color = 14;
    shadow_color = 12;
}

void SimpleButton::set_colors_enabled()
{
    text_color = 80;
    base_color = 64;
    high_color = 72;
    shadow_color = 74;
}

void SimpleButton::set_colors_disabled()
{
    text_color = 3;
    base_color = 10;
    high_color = 12;
    shadow_color = 14;
}

void SimpleButton::set_colors_active()
{
    text_color = WHITE;
    base_color = ORANGE_START;
    high_color = ORANGE_START+3;
    shadow_color = ORANGE_START+5;
}



bool prompt_for_string_block(text* mytext, const std::string& message, std::list<std::string>& result)
{
    int max_chars = 40;
    int max_lines = 8;
    
    int w = max_chars*6;
    int h = max_lines*10;
    int x = 160 - w/2;
    int y = 100 - h/2;
    
    // Background
    myscreen->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
    
    unsigned char forecolor = DARK_BLUE;
    //unsigned char backcolor = 13;
    

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	enable_keyrepeat();
    #ifdef USE_SDL2
    SDL_StartTextInput();
    #endif
    
    if(result.size() == 0)
        result.push_back("");
    
    std::list<std::string>::iterator s = result.begin();
    size_t cursor_pos = 0;
    size_t current_line = 0;
    
    bool cancel = false;
    bool done = false;
	while (!done)
	{
        
        // TODO: Need swipe controls for touch input
        // TODO: Needs a button for done
        if(query_key_press_event())
        {
            char c = query_key();
            clear_key_press_event();
            
            if (c == SDLK_RETURN)
            {
                #ifdef USE_TOUCH_INPUT
                // FIXME: SDL does not have keyboard customization, so we can't make newlines with RETURN.
                // I need to either modify SDL or add click/touch text navigation.
                done = true;
                break;
                #else
                std::string rest_of_line = s->substr(cursor_pos);
                s->erase(cursor_pos);
                s++;
                s = result.insert(s, rest_of_line);
                current_line++;
                cursor_pos = 0;
                #endif
            }
            else if (c == SDLK_BACKSPACE)
            {
                // At the beginning of the line?
                if(cursor_pos == 0)
                {
                    // Deleting a line break
                    // Not at the first line?
                    if(result.size() > 1 && current_line > 0)
                    {
                        // Then move up into the previous line, copying the old line
                        current_line--;
                        std::string old_line = *s;
                        s = result.erase(s);
                        s--;
                        cursor_pos = s->size();
                        // Append the old line
                        *s += old_line;
                    }
                }
                else
                {
                    // Delete previous character
                    cursor_pos--;
                    s->erase(cursor_pos, 1);
                }
            }
        }
        
        if(keystates[KEYSTATE_ESCAPE])
        {
            while(keystates[KEYSTATE_ESCAPE])
                get_input_events(WAIT);
            
            done = true;
            break;
        }
        if(keystates[KEYSTATE_DELETE])
        {
            if(cursor_pos < s->size())
                s->erase(cursor_pos, 1);
            
            while(keystates[KEYSTATE_DELETE])
                get_input_events(WAIT);
        }
        if(keystates[KEYSTATE_UP])
        {
            if(current_line > 0)
            {
                current_line--;
                s--;
                if(s->size() < cursor_pos)
                    cursor_pos = s->size();
            }
            
            while(keystates[KEYSTATE_UP])
                get_input_events(WAIT);
        }
        if(keystates[KEYSTATE_DOWN])
        {
            if(current_line+1 < result.size())
            {
                current_line++;
                s++;
            }
            else  // At the bottom already
                cursor_pos = s->size();
            
            if(s->size() < cursor_pos)
                cursor_pos = s->size();
            
            while(keystates[KEYSTATE_DOWN])
                get_input_events(WAIT);
        }
        if(keystates[KEYSTATE_LEFT])
        {
            if(cursor_pos > 0)
                cursor_pos--;
            else if(current_line > 0)
            {
                current_line--;
                s--;
                cursor_pos = s->size();
            }
            
            while(keystates[KEYSTATE_LEFT])
                get_input_events(WAIT);
        }
        if(keystates[KEYSTATE_RIGHT])
        {
            cursor_pos++;
            if(cursor_pos > s->size())
            {
                if(current_line+1 < result.size())
                {
                    // Go to next line
                    current_line++;
                    s++;
                    cursor_pos = 0;
                }
                else  // No next line
                    cursor_pos = s->size();
            }
            
            while(keystates[KEYSTATE_RIGHT])
                get_input_events(WAIT);
        }
        
        if(query_text_input_event())
        {
            char* temptext = query_text_input();
            
            if(temptext != NULL)
            {
                s->insert(cursor_pos, temptext);
                cursor_pos += strlen(temptext);
            }
        }
		
        clear_text_input_event();
        myscreen->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
        mytext->write_xy(x, y - 13, message.c_str(), BLACK, 1);
        myscreen->hor_line(x, y - 5, w, BLACK);
        
        int offset = 0;
        if(current_line > 3)
            offset = (current_line - 3)*10;
        int j = 0;
        for(std::list<std::string>::iterator e = result.begin(); e != result.end(); e++)
        {
            int ypos = y + j*10 - offset;
            if(y <= ypos && ypos <= y + h)
                mytext->write_xy(x, ypos, e->c_str(), forecolor, 1);
            j++;
        }
        myscreen->ver_line(x + cursor_pos*6, y + current_line*10 - 2 - offset, 10, RED);
		myscreen->buffer_to_screen(0, 0, 320, 200);
        
		// Wait for a key to be pressed ..
		while (!query_key_press_event() && !query_text_input_event())
			get_input_events(WAIT);
	}

    #ifdef USE_SDL2
    SDL_StopTextInput();
    #endif
	disable_keyrepeat();
	clear_keyboard();
    
    return !cancel;
}

bool prompt_for_string(text* mytext, const std::string& message, std::string& result)
{
    int max_chars = 29;
    
    int x = 58;
    int y = 60;
    int w = max_chars*6;
    int h = 10;
    
    myscreen->draw_button(x - 5, y - 20, x + w + 10, y + h + 10, 1);
    
    char* str = mytext->input_string_ex(x, y, max_chars, message.c_str(), result.c_str());
    
    if(str == NULL)
        return false;
    
    result = str;
    return true;
}





class EditorTerrainBrush
{
public:
    
    Sint32 terrain;
    bool use_smoothing;
    bool picking;
    
    EditorTerrainBrush()
        : terrain(PIX_GRASS1), use_smoothing(true), picking(false)
    {}
};

class EditorObjectBrush
{
public:
    
    bool snap_to_grid;
    Sint32 order;
    Sint32 family;
    char team;
    unsigned short level;
    bool picking;
    
    EditorObjectBrush()
        : snap_to_grid(true), order(ORDER_LIVING), family(0), team(1), level(1), picking(false)
    {}
    
    void set(walker* target)
    {
        if(target == NULL)
        {
            order = ORDER_LIVING;
            family = 0;
            team = 1;
            level = 1;
        }
        else
        {
            order = target->query_order();
            family = target->query_family();
            team = target->team_num;
            level = target->stats->level;
        }
    }
};

class SelectionInfo
{
public:
    bool valid;
    std::string name;
    short x, y;
    unsigned short w, h;
    unsigned char order;
    unsigned char family;
    unsigned short level;
    walker* target;
    
    
    SelectionInfo()
        : valid(false), x(0), y(0), w(GRID_SIZE), h(GRID_SIZE), order(ORDER_LIVING), family(FAMILY_SOLDIER), level(1), target(NULL)
    {}
    SelectionInfo(walker* target)
        : valid(false), x(0), y(0), w(GRID_SIZE), h(GRID_SIZE), order(ORDER_LIVING), family(FAMILY_SOLDIER), level(1), target(target)
    {
        set(target);
    }
    
    void clear()
    {
        valid = false;
        name.clear();
        x = 0;
        y = 0;
        w = GRID_SIZE;
        h = GRID_SIZE;
        order = ORDER_LIVING;
        family = FAMILY_SOLDIER;
        level = 1;
    }
    void set(walker* target)
    {
        if(target == NULL)
            clear();
        else
        {
            valid = true;
            name = target->stats->name;
            x = target->xpos;
            y = target->ypos;
            w = target->sizex;
            h = target->sizey;
            order = target->query_order();
            family = target->query_family();
            level = target->stats->level;
            this->target = target;
        }
    }
    
    walker* get_object(LevelData* level)
    {
        if(!valid)
            return NULL;
        
        return target;
    }
};


class LevelEditorData
{
public:
    CampaignData* campaign;
    LevelData* level;
    
    text* scentext;
	ModeEnum mode;
    EditorTerrainBrush terrain_brush;
    EditorObjectBrush object_brush;
    std::vector<SelectionInfo> selection;
    bool rect_selecting;
    Rectf selection_rect;
    bool dragging;
    
	radar myradar;
	
	Uint16 menu_button_height;
	
	set<SimpleButton*> menu_buttons;
	// The active menu buttons
	list<pair<SimpleButton*, set<SimpleButton*> > > current_menu;
	// The mode-specific buttons
	set<SimpleButton*> mode_buttons;
	set<SimpleButton*> pan_buttons;
	
	// Menu buttons
	
	// File menu
	SimpleButton fileButton, fileCampaignButton, fileLevelButton, fileQuitButton;
	
	// File > Campaign submenu
	SimpleButton fileCampaignImportButton, fileCampaignShareButton, fileCampaignNewButton, fileCampaignLoadButton, fileCampaignSaveButton, fileCampaignSaveAsButton;
	
	// File > Level submenu
	SimpleButton fileLevelNewButton, fileLevelLoadButton, fileLevelSaveButton, fileLevelSaveAsButton;
	
	// Campaign menu
	SimpleButton campaignButton, campaignInfoButton, campaignProfileButton, campaignDetailsButton, campaignValidateButton;
	
	// Campaign > Profile submenu
	SimpleButton campaignProfileTitleButton, campaignProfileDescriptionButton, campaignProfileIconButton, campaignProfileAuthorsButton, campaignProfileContributorsButton;
	
	// Campaign > Details submenu
	SimpleButton campaignDetailsVersionButton, campaignDetailsSuggestedPowerButton, campaignDetailsFirstLevelButton;
	
	// Level menu
	SimpleButton levelButton, levelInfoButton, levelProfileButton, levelDetailsButton, levelGoalsButton, levelResmoothButton, levelDeleteTerrainButton, levelDeleteObjectsButton;
	
	// Level > Profile submenu
	SimpleButton levelProfileTitleButton, levelProfileDescriptionButton;
	
	// Level > Details submenu
	SimpleButton levelDetailsMapSizeButton, levelDetailsParValueButton;
	
	// Level > Goals submenu
	SimpleButton levelGoalsEnemiesButton, levelGoalsGeneratorsButton, levelGoalsNPCsButton;
	
	// Edit menu
	SimpleButton modeButton, modeTerrainButton, modeObjectButton, modeSelectButton;
	
	// On-screen buttons
	SimpleButton pickerButton;
	SimpleButton gridSnapButton;
	SimpleButton terrainSmoothButton;
	
	SimpleButton setNameButton;
	SimpleButton prevTeamButton, nextTeamButton;
	SimpleButton prevLevelButton, nextLevelButton;
	SimpleButton prevClassButton, nextClassButton;
	SimpleButton facingButton;
	
	SimpleButton deleteButton;
	
	SimpleButton panUpButton, panDownButton, panLeftButton, panRightButton;
	SimpleButton panUpRightButton, panUpLeftButton, panDownRightButton, panDownLeftButton;
    
    
    LevelEditorData();
    ~LevelEditorData();
    
    bool loadCampaign(const std::string& id);
    bool reloadCampaign();
    
    bool loadLevel(int id);
    bool reloadLevel();
    
    bool saveCampaignAs(const std::string& id);
    bool saveCampaign();
    
    bool saveLevelAs(int id);
    bool saveLevel();
    
    void draw(screen* myscreen);
    Sint32 display_panel(screen* myscreen);
    
    bool mouse_on_menus(int mx, int my);
    void update_menu_buttons();
    void reset_mode_buttons();
    void activate_mode_button(SimpleButton* button);
    
    void clear_terrain();
    void resmooth_terrain();
    void mouse_down(int mx, int my);
    void mouse_motion(int mx, int my, int dx, int dy);
    void mouse_up(int mx, int my, int old_mx, int old_my, bool& done);
    void pick_by_mouse(int mx, int my);
    
    
    walker* get_object(int x, int y);
};

bool are_objects_outside_area(LevelData* level, int x, int y, int w, int h);

#define DEFAULT_EDITOR_MENU_BUTTON_HEIGHT 20

#ifdef REDUCE_OVERSCAN
#define OVERSCAN_PADDING 6
#else
#define OVERSCAN_PADDING 0
#endif

LevelEditorData::LevelEditorData()
    : campaign(new CampaignData("org.openglad.gladiator")), level(new LevelData(1)), scentext(NULL), mode(TERRAIN), rect_selecting(false), dragging(false), myradar(myscreen->viewob[0], myscreen, 0)
    , menu_button_height(DEFAULT_EDITOR_MENU_BUTTON_HEIGHT)
    
	, fileButton("File", OVERSCAN_PADDING, 0, 30, menu_button_height)
	, fileCampaignButton("Campaign >", OVERSCAN_PADDING, fileButton.area.y + fileButton.area.h, 65, menu_button_height, true)
	, fileLevelButton("Level >", OVERSCAN_PADDING, fileCampaignButton.area.y + fileCampaignButton.area.h, 65, menu_button_height, true, true)
	, fileQuitButton("Exit", OVERSCAN_PADDING, fileLevelButton.area.y + fileLevelButton.area.h, 65, menu_button_height, true, true)
	
	, fileCampaignImportButton("Import...", fileCampaignButton.area.x + fileCampaignButton.area.w, fileCampaignButton.area.y, 65, menu_button_height, true)
	, fileCampaignShareButton("Share...", fileCampaignImportButton.area.x, fileCampaignImportButton.area.y + fileCampaignImportButton.area.h, 65, menu_button_height, true, true)
	//, fileCampaignNewButton("New", fileCampaignImportButton.area.x, fileCampaignShareButton.area.y + fileCampaignShareButton.area.h, 65, menu_button_height, true, true)
	, fileCampaignNewButton("New", fileCampaignButton.area.x + fileCampaignButton.area.w, fileCampaignButton.area.y, 65, menu_button_height, true)
	, fileCampaignLoadButton("Load...", fileCampaignImportButton.area.x, fileCampaignNewButton.area.y + fileCampaignNewButton.area.h, 65, menu_button_height, true, true)
	, fileCampaignSaveButton("Save", fileCampaignImportButton.area.x, fileCampaignLoadButton.area.y + fileCampaignLoadButton.area.h, 65, menu_button_height, true, true)
	, fileCampaignSaveAsButton("Save As...", fileCampaignImportButton.area.x, fileCampaignSaveButton.area.y + fileCampaignSaveButton.area.h, 65, menu_button_height, true, true)
	
	, fileLevelNewButton("New", fileLevelButton.area.x + fileLevelButton.area.w, fileLevelButton.area.y, 65, menu_button_height, true)
	, fileLevelLoadButton("Load...", fileLevelNewButton.area.x, fileLevelNewButton.area.y + fileLevelNewButton.area.h, 65, menu_button_height, true, true)
	, fileLevelSaveButton("Save", fileLevelNewButton.area.x, fileLevelLoadButton.area.y + fileLevelLoadButton.area.h, 65, menu_button_height, true, true)
	, fileLevelSaveAsButton("Save As...", fileLevelNewButton.area.x, fileLevelSaveButton.area.y + fileLevelSaveButton.area.h, 65, menu_button_height, true, true)
	
	, campaignButton("Campaign", fileButton.area.x + fileButton.area.w, 0, 55, menu_button_height)
	, campaignInfoButton("Info...", campaignButton.area.x, campaignButton.area.y + campaignButton.area.h, 59, menu_button_height, true)
	, campaignProfileButton("Profile >", campaignButton.area.x, campaignInfoButton.area.y + campaignInfoButton.area.h, 59, menu_button_height, true, true)
	, campaignDetailsButton("Details >", campaignButton.area.x, campaignProfileButton.area.y + campaignProfileButton.area.h, 59, menu_button_height, true, true)
	, campaignValidateButton("Validate", campaignButton.area.x, campaignDetailsButton.area.y + campaignDetailsButton.area.h, 59, menu_button_height, true, true)
	
	, campaignProfileTitleButton("Title...", campaignProfileButton.area.x + campaignProfileButton.area.w, campaignProfileButton.area.y, 95, menu_button_height, true)
	, campaignProfileDescriptionButton("Description...", campaignProfileTitleButton.area.x, campaignProfileTitleButton.area.y + campaignProfileTitleButton.area.h, 95, menu_button_height, true, true)
	, campaignProfileIconButton("Icon...", campaignProfileTitleButton.area.x, campaignProfileDescriptionButton.area.y + campaignProfileDescriptionButton.area.h, 95, menu_button_height, true, true)
	//, campaignProfileAuthorsButton("Authors...", campaignProfileTitleButton.area.x, campaignProfileIconButton.area.y + campaignProfileIconButton.area.h, 95, menu_button_height, true, true)
	, campaignProfileAuthorsButton("Authors...", campaignProfileTitleButton.area.x, campaignProfileDescriptionButton.area.y + campaignProfileDescriptionButton.area.h, 95, menu_button_height, true, true)
	, campaignProfileContributorsButton("Contributors...", campaignProfileTitleButton.area.x, campaignProfileAuthorsButton.area.y + campaignProfileAuthorsButton.area.h, 95, menu_button_height, true, true)
	
	, campaignDetailsVersionButton("Version...", campaignDetailsButton.area.x + campaignDetailsButton.area.w, campaignDetailsButton.area.y, 113, menu_button_height, true)
	, campaignDetailsSuggestedPowerButton("Suggested power...", campaignDetailsVersionButton.area.x, campaignDetailsVersionButton.area.y + campaignDetailsVersionButton.area.h, 113, menu_button_height, true, true)
	, campaignDetailsFirstLevelButton("First level...", campaignDetailsVersionButton.area.x, campaignDetailsSuggestedPowerButton.area.y + campaignDetailsSuggestedPowerButton.area.h, 113, menu_button_height, true, true)
	
	, levelButton("Level", campaignButton.area.x + campaignButton.area.w, 0, 40, menu_button_height)
	, levelInfoButton("Info...", levelButton.area.x, levelButton.area.y + levelButton.area.h, 110, menu_button_height, true)
	, levelProfileButton("Profile >", levelButton.area.x, levelInfoButton.area.y + levelInfoButton.area.h, 110, menu_button_height, true, true)
	, levelDetailsButton("Details >", levelButton.area.x, levelProfileButton.area.y + levelProfileButton.area.h, 110, menu_button_height, true, true)
	, levelGoalsButton("Goals >", levelButton.area.x, levelDetailsButton.area.y + levelDetailsButton.area.h, 110, menu_button_height, true, true)
	, levelResmoothButton("Resmooth terrain", levelButton.area.x, levelGoalsButton.area.y + levelGoalsButton.area.h, 110, menu_button_height, true, true)
	, levelDeleteTerrainButton("Clear all terrain", levelButton.area.x, levelResmoothButton.area.y + levelResmoothButton.area.h, 110, menu_button_height, true, true)
	, levelDeleteObjectsButton("Clear all objects", levelButton.area.x, levelDeleteTerrainButton.area.y + levelDeleteTerrainButton.area.h, 110, menu_button_height, true, true)
	
	, levelProfileTitleButton("Title...", levelProfileButton.area.x + levelProfileButton.area.w, levelProfileButton.area.y, 95, menu_button_height, true)
	, levelProfileDescriptionButton("Description...", levelProfileTitleButton.area.x, levelProfileTitleButton.area.y + levelProfileTitleButton.area.h, 95, menu_button_height, true, true)
	
	, levelDetailsMapSizeButton("Map size...", levelDetailsButton.area.x + levelDetailsButton.area.w, levelDetailsButton.area.y, 95, menu_button_height, true)
	, levelDetailsParValueButton("Par value...", levelDetailsMapSizeButton.area.x, levelDetailsMapSizeButton.area.y + levelDetailsMapSizeButton.area.h, 95, menu_button_height, true, true)
	
	, levelGoalsEnemiesButton("Defeat enemies: On", levelGoalsButton.area.x + levelGoalsButton.area.w - 2*OVERSCAN_PADDING, levelGoalsButton.area.y, 125, menu_button_height, true)
	, levelGoalsGeneratorsButton("Beat generators: Off", levelGoalsEnemiesButton.area.x, levelGoalsEnemiesButton.area.y + levelGoalsEnemiesButton.area.h, 125, menu_button_height, true, true)
	, levelGoalsNPCsButton("Protect NPCs: Off", levelGoalsEnemiesButton.area.x, levelGoalsGeneratorsButton.area.y + levelGoalsGeneratorsButton.area.h, 125, menu_button_height, true, true)
	
	, modeButton("Edit (Terrain)", levelButton.area.x + levelButton.area.w, 0, 90, menu_button_height)
	, modeTerrainButton("Terrain Mode", modeButton.area.x, modeButton.area.y + modeButton.area.h, 75, menu_button_height, true)
	, modeObjectButton("Object Mode", modeButton.area.x, modeTerrainButton.area.y + modeTerrainButton.area.h, 75, menu_button_height, true, true)
	, modeSelectButton("Select Mode", modeButton.area.x, modeObjectButton.area.y + modeObjectButton.area.h, 75, menu_button_height, true, true)
    
    , pickerButton("Pick", OVERSCAN_PADDING, 20, 27, 15)
    , gridSnapButton("Snap", pickerButton.area.x+pickerButton.area.w+2, 20, 27, 15)
    , terrainSmoothButton("Smooth", pickerButton.area.x+pickerButton.area.w+2, 20, 39, 15)  // Same place as gridSnapButton
    , setNameButton("Set Name", OVERSCAN_PADDING, 10+gridSnapButton.area.y+gridSnapButton.area.h, 52, 15)
    , prevTeamButton("< Team", OVERSCAN_PADDING, setNameButton.area.y+setNameButton.area.h, 40, 15)
    , nextTeamButton("Team >", prevTeamButton.area.x + prevTeamButton.area.w, prevTeamButton.area.y, 40, 15)
    , prevLevelButton("< Lvl", OVERSCAN_PADDING, prevTeamButton.area.y+prevTeamButton.area.h, 40, 15)
    , nextLevelButton("Lvl >", prevLevelButton.area.x + prevLevelButton.area.w, prevLevelButton.area.y, 40, 15)
    , prevClassButton("< Class", OVERSCAN_PADDING, prevLevelButton.area.y+prevLevelButton.area.h, 48, 15)
    , nextClassButton("Class >", prevClassButton.area.x + prevClassButton.area.w, prevClassButton.area.y, 48, 15)
    , facingButton("Facing >", OVERSCAN_PADDING, prevClassButton.area.y+prevClassButton.area.h, 52, 15)
    , deleteButton("Delete", OVERSCAN_PADDING, 10+facingButton.area.y+facingButton.area.h, 40, 15)
    , panUpButton("U", OVERSCAN_PADDING + 18, 200 - 51, 15, 15)
    , panDownButton("D", OVERSCAN_PADDING + 18, 200 - 21, 15, 15)
    , panLeftButton("L", OVERSCAN_PADDING + 3, 200 - 36, 15, 15)
    , panRightButton("R", OVERSCAN_PADDING + 33, 200 - 36, 15, 15)
    , panUpRightButton("", OVERSCAN_PADDING + 33, 200 - 51, 15, 15)
    , panUpLeftButton("", OVERSCAN_PADDING + 3, 200 - 51, 15, 15)
    , panDownRightButton("", OVERSCAN_PADDING + 33, 200 - 21, 15, 15)
    , panDownLeftButton("", OVERSCAN_PADDING + 3, 200 - 21, 15, 15)
{
	// Top menu
	menu_buttons.insert(&fileButton);
	menu_buttons.insert(&campaignButton);
	menu_buttons.insert(&levelButton);
	menu_buttons.insert(&modeButton);
	
    gridSnapButton.set_colors_enabled();
    terrainSmoothButton.set_colors_enabled();
    
    #if defined(USE_TOUCH_INPUT) || defined(USE_CONTROLLER_INPUT)
    pan_buttons.insert(&panUpButton);
    pan_buttons.insert(&panDownButton);
    pan_buttons.insert(&panLeftButton);
    pan_buttons.insert(&panRightButton);
    pan_buttons.insert(&panUpRightButton);
    pan_buttons.insert(&panUpLeftButton);
    pan_buttons.insert(&panDownRightButton);
    pan_buttons.insert(&panDownLeftButton);
    #endif
    
    myradar.force_lower_position = true;
}

LevelEditorData::~LevelEditorData()
{
    delete campaign;
    delete level;
}

bool LevelEditorData::loadCampaign(const std::string& id)
{
    campaign->id = id;
    return campaign->load();
}

bool LevelEditorData::reloadCampaign()
{
    return campaign->load();
}


bool LevelEditorData::loadLevel(int id)
{
    level->id = id;
    bool result = level->load();
    update_menu_buttons();
    return result;
}

bool LevelEditorData::reloadLevel()
{
    bool result = level->load();
    update_menu_buttons();
    return result;
}


bool LevelEditorData::saveCampaignAs(const std::string& id)
{
    bool result = campaign->save_as(id);
    
    // Remount for consistency in PhysFS
    if(!remount_campaign_package())
    {
        Log("Failed to remount campaign after saving it.\n");
        return false;
    }
    
    return result;
}

bool LevelEditorData::saveCampaign()
{
    bool result = campaign->save();
    
    // Remount for consistency in PhysFS
    if(!remount_campaign_package())
    {
        Log("Failed to remount campaign after saving it.\n");
        return false;
    }
    
    return result;
}


bool LevelEditorData::saveLevelAs(int id)
{
    level->id = id;
    char buf[20];
    snprintf(buf, 20, "scen%d", id);
    level->grid_file = buf;
    
    std::string old_campaign = get_mounted_campaign();
    unpack_campaign(old_campaign);
    bool result = level->save();
    if(result)
        result = repack_campaign(old_campaign);
    cleanup_unpacked_campaign();
    
    // Remount for consistency in PhysFS
    remount_campaign_package();
    
    return result;
}



bool button_showing(const std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >& ls, SimpleButton* elem)
{
    for(std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >::const_iterator e = ls.begin(); e != ls.end(); e++)
    {
        const std::set<SimpleButton*>& s = e->second;
        if(s.find(elem) != s.end())
            return true;
    }
    return false;
}

// Wouldn't spatial partitioning be nice?  Too bad!
bool LevelEditorData::mouse_on_menus(int mx, int my)
{
    for(set<SimpleButton*>::const_iterator e = menu_buttons.begin(); e != menu_buttons.end(); e++)
    {
        if((*e)->contains(mx, my))
            return true;
    }
    
    for(set<SimpleButton*>::const_iterator e = mode_buttons.begin(); e != mode_buttons.end(); e++)
    {
        if((*e)->contains(mx, my))
            return true;
    }
    
    // Count anything in the area of the pan buttons
    if(pan_buttons.size() > 0 && Rect(panLeftButton.area.x, panUpButton.area.y, panRightButton.area.x + panRightButton.area.w - panLeftButton.area.x, panDownButton.area.y + panDownButton.area.h - panUpButton.area.y).contains(mx, my))
        return true;
    
    for(std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >::const_iterator e = current_menu.begin(); e != current_menu.end(); e++)
    {
        const set<SimpleButton*>& s = e->second;
        for(set<SimpleButton*>::const_iterator f = s.begin(); f != s.end(); f++)
        {
            if((*f)->contains(mx, my))
                return true;
        }
    }
    
    return false;
}

void LevelEditorData::update_menu_buttons()
{
    levelGoalsEnemiesButton.label = "Defeat enemies: ";
    levelGoalsEnemiesButton.label += (level->type & LevelData::TYPE_CAN_EXIT_WHENEVER? "Off" : "On");
    
    levelGoalsGeneratorsButton.label = "Beat generators: ";
    levelGoalsGeneratorsButton.label += (level->type & LevelData::TYPE_MUST_DESTROY_GENERATORS? "On" : "Off");
    
    levelGoalsNPCsButton.label = "Protect NPCs: ";
    levelGoalsNPCsButton.label += (level->type & LevelData::TYPE_MUST_PROTECT_NAMED_NPCS? "On" : "Off");
}

void LevelEditorData::reset_mode_buttons()
{
    mode_buttons.clear();
    switch(mode)
    {
        case TERRAIN:
        mode_buttons.insert(&pickerButton);
        mode_buttons.insert(&terrainSmoothButton);
        if(terrain_brush.picking)
            pickerButton.set_colors_active();
        else
            pickerButton.set_colors_normal();
        break;
        case OBJECT:
        mode_buttons.insert(&pickerButton);
        mode_buttons.insert(&gridSnapButton);
        mode_buttons.insert(&prevTeamButton);
        mode_buttons.insert(&nextTeamButton);
        if(object_brush.picking)
            pickerButton.set_colors_active();
        else
            pickerButton.set_colors_normal();
        break;
        case SELECT:
        mode_buttons.insert(&gridSnapButton);
        if(selection.size() == 1 && selection.front().order == ORDER_LIVING)
        {
            mode_buttons.insert(&setNameButton);
        }
        if(selection.size() > 0)
        {
            mode_buttons.insert(&prevTeamButton);
            mode_buttons.insert(&nextTeamButton);
            mode_buttons.insert(&prevLevelButton);
            mode_buttons.insert(&nextLevelButton);
            mode_buttons.insert(&prevClassButton);
            mode_buttons.insert(&nextClassButton);
            mode_buttons.insert(&facingButton);
            mode_buttons.insert(&deleteButton);
        }
        break;
    }
}

void LevelEditorData::activate_mode_button(SimpleButton* button)
{
    if(button == &pickerButton)
    {
        if(mode == TERRAIN)
        {
            terrain_brush.picking = !terrain_brush.picking;
            if(terrain_brush.picking)
                pickerButton.set_colors_active();
            else
                pickerButton.set_colors_normal();
        }
        else if(mode == OBJECT)
        {
            object_brush.picking = !object_brush.picking;
            if(object_brush.picking)
                pickerButton.set_colors_active();
            else
                pickerButton.set_colors_normal();
        }
    }
    else if(button == &gridSnapButton)
    {
        object_brush.snap_to_grid = !object_brush.snap_to_grid;
        if(object_brush.snap_to_grid)
            gridSnapButton.set_colors_enabled();
        else
            gridSnapButton.set_colors_normal();
    }
    else if(button == &terrainSmoothButton)
    {
        terrain_brush.use_smoothing = !terrain_brush.use_smoothing;
        if(terrain_brush.use_smoothing)
            terrainSmoothButton.set_colors_enabled();
        else
            terrainSmoothButton.set_colors_normal();
    }
    else if(button == &setNameButton)
    {
        if(selection.size() == 1 && selection.front().order == ORDER_LIVING)
        {
            walker* obj = selection.front().get_object(level);
            if(obj != NULL)
            {
                std::string name = obj->stats->name;
                if(prompt_for_string(scentext, "Rename", name))
                {
                    strncpy(obj->stats->name, name.c_str(), 11);
                    obj->stats->name[11] = '\0';
                    selection.front().name = obj->stats->name;
                    levelchanged = 1;
                }
            }
        }
    }
    else if(button == &prevTeamButton)
    {
        if(mode == SELECT)
        {
            for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
            {
                walker* obj = e->get_object(level);
                if(obj != NULL)
                {
                    if(obj->team_num > 0)
                        obj->team_num--;
                    else
                        obj->team_num = MAX_TEAM;
                    levelchanged = 1;
                }
            }
        }
        else if(mode == OBJECT)
        {
            if(object_brush.team > 0)
                object_brush.team--;
            else
                object_brush.team = MAX_TEAM;
        }
    }
    else if(button == &nextTeamButton)
    {
        if(mode == SELECT)
        {
            for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
            {
                walker* obj = e->get_object(level);
                if(obj != NULL)
                {
                    if(obj->team_num < MAX_TEAM)
                        obj->team_num++;
                    else
                        obj->team_num = 0;
                    levelchanged = 1;
                }
            }
        }
        else if(mode == OBJECT)
        {
            if(object_brush.team < MAX_TEAM)
                object_brush.team++;
            else
                object_brush.team = 0;
        }
    }
    else if(button == &prevLevelButton)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            walker* obj = e->get_object(level);
            if(obj != NULL)
            {
                if(obj->stats->level > 1)
                {
                    obj->stats->level--;
                    e->level = obj->stats->level;
                    levelchanged = 1;
                }
            }
        }
    }
    else if(button == &nextLevelButton)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            walker* obj = e->get_object(level);
            if(obj != NULL)
            {
                obj->stats->level++;
                e->level = obj->stats->level;
                levelchanged = 1;
            }
        }
    }
    else if(button == &prevClassButton)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            walker* obj = e->get_object(level);
            if(obj != NULL && obj->query_order() == ORDER_LIVING)
            {
                if(e->family > 0)
                    e->family--;
                else
                    e->family = NUM_FAMILIES-1;
                level->myloader->set_walker(obj, e->order, e->family);
                obj->ani_type = ANI_WALK;
                obj->transform_to(e->order, e->family);
                obj->set_frame(obj->ani[obj->curdir][0]);
                obj->setxy(e->x, e->y);
                e->set(obj);
                
                levelchanged = 1;
            }
        }
    }
    else if(button == &nextClassButton)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            walker* obj = e->get_object(level);
            if(obj != NULL && obj->query_order() == ORDER_LIVING)
            {
                if(e->family+1 < NUM_FAMILIES)
                    e->family++;
                else
                    e->family = 0;
                level->myloader->set_walker(obj, e->order, e->family);
                obj->ani_type = ANI_WALK;
                obj->transform_to(e->order, e->family);
                obj->set_frame(obj->ani[obj->curdir][0]);
                obj->setxy(e->x, e->y);
                e->set(obj);
                
                levelchanged = 1;
            }
        }
    }
    else if(button == &facingButton)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            walker* obj = e->get_object(level);
            if(obj != NULL)
            {
                if(obj->curdir < FACE_UP_LEFT)
                    obj->curdir++;
                else
                    obj->curdir = FACE_UP;
				obj->set_frame(obj->ani[obj->curdir][0]);
                levelchanged = 1;
            }
        }
    }
    else if(button == &deleteButton)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            walker* obj = e->get_object(level);
            if(obj != NULL)
            {
                level->remove_ob(obj);
                levelchanged = 1;
            }
        }
        selection.clear();
    }
}

bool activate_sub_menu_button(int mx, int my, std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >& current_menu, SimpleButton& button, bool is_in_top_menu = false)
{
    // Make sure it is showing
    if(!button.contains(mx, my) || (!is_in_top_menu && !button_showing(current_menu, &button)))
        return false;
    
    while (mymouse[MOUSE_LEFT])
        get_input_events(WAIT);
    
    if(current_menu.size() > 0)
    {
        // Close menu if already open
        if(current_menu.back().first == &button)
        {
            current_menu.pop_back();
            return false;
        }
        
        // Remove all menus up to the parent
        while(current_menu.size() > 0)
        {
            std::set<SimpleButton*>& s = current_menu.back().second;
            if(s.find(&button) == s.end())
                current_menu.pop_back();
            else
                return true; // Open this menu
        }
    }
    
    // No parent!
    return is_in_top_menu;
}

bool activate_menu_choice(int mx, int my, LevelEditorData& data, SimpleButton& button, bool is_in_top_menu = false)
{
    // Make sure it is showing
    if(!button.contains(mx, my) || (!is_in_top_menu && !button_showing(data.current_menu, &button)))
        return false;
    
    while (mymouse[MOUSE_LEFT])
        get_input_events(WAIT);
    
    // Close menu
    data.current_menu.clear();
    data.draw(myscreen);
    myscreen->refresh();
    return true;
}

bool activate_menu_toggle_choice(int mx, int my, LevelEditorData& data, SimpleButton& button, bool is_in_top_menu = false)
{
    // Make sure it is showing
    if(!button.contains(mx, my) || (!is_in_top_menu && !button_showing(data.current_menu, &button)))
        return false;
    
    while (mymouse[MOUSE_LEFT])
        get_input_events(WAIT);
    
    // Close menu
    data.draw(myscreen);
    myscreen->refresh();
    return true;
}

// Recursively get the connected levels
void get_connected_level_exits(int current_level, const std::list<int>& levels, std::set<int>& connected, std::list<std::string>& problems)
{
    // Stopping condition
    if(connected.find(current_level) != connected.end())
        return;
    
    connected.insert(current_level);
    
    // Load level
    LevelData d(current_level);
    if(!d.load())
    {
        char buf[40];
        snprintf(buf, 40, "Level %d failed to load.", current_level);
        problems.push_back(buf);
        return;
    }
    
    // Get the exits
    std::set<int> exits;
    oblink* fx = d.fxlist;
    while(fx != NULL)
    {
        if(fx->ob != NULL)
        {
            if(fx->ob->query_order() == ORDER_TREASURE && fx->ob->query_family() == FAMILY_EXIT && fx->ob->stats != NULL)
                exits.insert(fx->ob->stats->level);
        }
        
        fx = fx->next;
    }
    
    // With no exits, we'll progress directly to the next sequential level
    if(exits.size() == 0)
    {
        // Does the next sequential level exist?
        bool has_next = false;
        for(std::list<int>::const_iterator e = levels.begin(); e != levels.end(); e++)
        {
            if(current_level+1 == *e)
            {
                has_next = true;
                break;
            }
        }
        
        if(has_next)
        {
            exits.insert(current_level+1);
        }
        else
        {
            char buf[40];
            snprintf(buf, 40, "Level %d has no exits.", current_level);
            problems.push_back(buf);
            return;
        }
    }
    
    // Recursively call on exits
    for(std::set<int>::iterator e = exits.begin(); e != exits.end(); e++)
    {
        get_connected_level_exits(*e, levels, connected, problems);
    }
}

bool LevelEditorData::saveLevel()
{
    char buf[20];
    snprintf(buf, 20, "scen%d", level->id);
    level->grid_file = buf;
    
    std::string old_campaign = get_mounted_campaign();
    unpack_campaign(old_campaign);
    bool result = level->save();
    if(result)
        result = repack_campaign(get_mounted_campaign());
    cleanup_unpacked_campaign();
    
    // Remount for consistency in PhysFS
    remount_campaign_package();
    
    return result;
}

void LevelEditorData::draw(screen* myscreen)
{
    myscreen->clearbuffer();
    level->draw(myscreen);
    
    if(rect_selecting)
    {
        Rectf r(selection_rect.x - level->topx + myscreen->viewob[0]->xloc, selection_rect.y - level->topy + myscreen->viewob[0]->yloc, selection_rect.w, selection_rect.h);
        if(r.w < 0.0f)
        {
            r.x += r.w;
            r.w = -r.w;
        }
        if(r.h < 0.0f)
        {
            r.y += r.h;
            r.h = -r.h;
        }
        myscreen->draw_box(r.x, r.y, r.x + r.w, r.y + r.h, ORANGE_START, 0, 1);
        redraw = 1;
    }
    
    display_panel(myscreen);
    
}

#ifdef NO_BLOOD
#define BLOOD_STRING "REMAINS"
#else
#define BLOOD_STRING "BLOOD"
#endif

Sint32 LevelEditorData::display_panel(screen* myscreen)
{
    // Draw selection indicators
    if(mode == SELECT && selection.size() > 0)
    {
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            // Draw cursor
            int mx, my;
            mx = e->x - level->topx;
            my = e->y - level->topy;
            
            {
                // Draw target tile
                int worldx = mx + level->topx;
                int worldy = my + level->topy;
                int screenx = worldx - level->topx;
                int screeny = worldy - level->topy;
                myscreen->draw_box(screenx, screeny, screenx + e->w, screeny + e->h, dragging? ORANGE_START : YELLOW, 0, 1);
            }
        }
    }
    
    // Draw minimap
    myradar.draw(level);
    
    // Draw mode-specific buttons
    for(set<SimpleButton*>::iterator e = mode_buttons.begin(); e != mode_buttons.end(); e++)
        (*e)->draw(myscreen, scentext);
        
    if(pan_buttons.size() > 0)
    {
        Rect r(panLeftButton.area.x, panUpButton.area.y, panRightButton.area.x + panRightButton.area.w - panLeftButton.area.x, panDownButton.area.y + panDownButton.area.h - panUpButton.area.y);
        myscreen->fastbox(r.x, r.y, r.w, r.h, 13);
        for(set<SimpleButton*>::iterator e = pan_buttons.begin(); e != pan_buttons.end(); e++)
            (*e)->draw(myscreen, scentext);
    }
    
	char message[50];
	Sint32 i, j; // for loops
	//   static Sint32 family=-1, hitpoints=-1, score=-1, act=-1;
	Sint32 numobs = myscreen->level_data.numobs;
	Sint32 lm = 245;
	Sint32 curline = 0;
	Sint32 whichback;
	static char treasures[20][NUM_FAMILIES] =
	    { BLOOD_STRING, "DRUMSTICK", "GOLD", "SILVER",
	      "MAGIC", "INVIS", "INVULN", "FLIGHT",
	      "EXIT", "TELEPORTER", "LIFE GEM", "KEY", "SPEED", "CC",
	    };
	static char weapons[20][NUM_FAMILIES] =
	    { "KNIFE", "ROCK", "ARROW", "FIREBALL",
	      "TREE", "METEOR", "SPRINKLE", "BONE",
	      BLOOD_STRING, "BLOB", "FIRE ARROW", "LIGHTNING",
	      "GLOW", "WAVE 1", "WAVE 2", "WAVE 3",
	      "PROTECTION", "HAMMER", "DOOR",
	    };

	static char livings[NUM_FAMILIES][20] =
	    {  "SOLDIER", "ELF", "ARCHER", "MAGE",
	       "SKELETON", "CLERIC", "ELEMENTAL",
	       "FAERIE", "L SLIME", "S SLIME", "M SLIME",
	       "THIEF", "GHOST", "DRUID", "ORC",
	       "ORC CAPTAIN", "BARBARIAN", "ARCHMAGE",
	       "GOLEM", "G SKELETON", "TOWER1",
	    };

    // Info box for select mode
    if(mode == SELECT && selection.size() > 0)
    {
        // Draw the info box background
        myscreen->draw_button(lm-4, L_D(-1)+4, 315, L_D(7)-2, 1, 1);
        
        if(selection.size() > 1)
            scentext->write_xy(lm, L_D(curline++), "Selected:", RED, 1);
        int i = 0;
        for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
        {
            bool showing_name = false;
            
            // Too many names to show?
            if(i+1 == 6 && selection.size() > 6)
            {
                char buf[20];
                snprintf(buf, 20, "+%d more", selection.size() - 5);
                scentext->write_xy(lm, L_D(curline++), buf, DARK_BLUE, 1);
                break;  // No more
            }
            // Show name
            else if(e->name.size() > 0 && e->order == ORDER_LIVING)
            {
                scentext->write_xy(lm, L_D(curline++), ("\"" + e->name + "\"").c_str(), DARK_BLUE, 1);
                showing_name = true;
            }
            else if(selection.size() == 0)
                curline++;  // Skip name line for guy with no name
            
            if(selection.size() == 1 || !showing_name)
            {
                // Show family name
                message[0] = '\0';
                if (e->order == ORDER_LIVING)
                    strcat(message, livings[e->family]);
                else if (e->order == ORDER_GENERATOR)
                    switch (e->family)      // who are we?
                    {
                        case FAMILY_TENT:
                            strcat(message, "TENT");
                            break;
                        case FAMILY_TOWER:
                            strcat(message, "MAGE TOWER");
                            break;
                        case FAMILY_BONES:
                            strcat(message, "BONEPILE");
                            break;
                        case FAMILY_TREEHOUSE:
                            strcat(message, "TREEHOUSE");
                            break;
                        default:
                            strcat(message, "GENERATOR");
                            break;
                    }
                else if (e->order == ORDER_SPECIAL)
                    strcat(message, "START TILE");
                else if (e->order == ORDER_TREASURE)
                    strcat(message, treasures[e->family]);
                else if (e->order == ORDER_WEAPON)
                    strcat(message, weapons[e->family]);
                else
                    strcat(message, "UNKNOWN");
                scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);
            }
            
            i++;
            
            // Only show extended info for a single selection
            if(selection.size() > 1)
                continue;
            
            // More info for a single selection
            // Level display
            message[0] = '\0';
            switch(e->order)
            {
                case ORDER_LIVING:
                case ORDER_GENERATOR:
                    sprintf(message, "LEVEL: %u", e->level);
                    break;
                case ORDER_TREASURE:
                    if(e->family == FAMILY_GOLD_BAR || e->family == FAMILY_SILVER_BAR)
                        sprintf(message, "VALUE: %u", e->level);
                    else if(e->family == FAMILY_KEY)
                        sprintf(message, "DOOR ID: %u", e->level);
                    else if(e->family == FAMILY_TELEPORTER)
                        sprintf(message, "GROUP: %u", e->level);
                    else if(e->family == FAMILY_EXIT)
                        sprintf(message, "EXIT TO: %u", e->level);
                    else if(e->family != FAMILY_STAIN)
                        sprintf(message, "POWER: %u", e->level);
                    break;
                case ORDER_WEAPON:
                    if(e->family == FAMILY_DOOR)
                        sprintf(message, "DOOR ID: %u", e->level);
                    else
                        sprintf(message, "POWER: %u", e->level);
                    break;
                default:
                    break;
            }
            
            if(strlen(message) > 0)
                scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);
        }
        
    }
    
    if(mode == OBJECT)
    {
        // Draw the bounding box
        myscreen->draw_button(lm-4, L_D(-1)+4, 315, L_D(7)-2, 1, 1);
        
        // Get team number ..
        message[0] = '\0';
        if (object_brush.order == ORDER_LIVING)
            strcat(message, livings[object_brush.family]);
        else if (object_brush.order == ORDER_GENERATOR)
            switch (object_brush.family)      // who are we?
            {
                case FAMILY_TENT:
                    strcat(message, "TENT");
                    break;
                case FAMILY_TOWER:
                    strcat(message, "MAGE TOWER");
                    break;
                case FAMILY_BONES:
                    strcat(message, "BONEPILE");
                    break;
                case FAMILY_TREEHOUSE:
                    strcat(message, "TREEHOUSE");
                    break;
                default:
                    strcat(message, "GENERATOR");
                    break;
            }
        else if (object_brush.order == ORDER_SPECIAL)
            strcat(message, "START TILE");
        else if (object_brush.order == ORDER_TREASURE)
            strcat(message, treasures[object_brush.family]);
        else if (object_brush.order == ORDER_WEAPON)
            strcat(message, weapons[object_brush.family]);
        else
            strcat(message, "UNKNOWN");
        scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);

        // Level display
        message[0] = '\0';
        switch(object_brush.order)
        {
            case ORDER_LIVING:
            case ORDER_GENERATOR:
                sprintf(message, "LEVEL: %u", object_brush.level);
                break;
            case ORDER_TREASURE:
                if(object_brush.family == FAMILY_GOLD_BAR || object_brush.family == FAMILY_SILVER_BAR)
                    sprintf(message, "VALUE: %u", object_brush.level);
                else if(object_brush.family == FAMILY_KEY)
                    sprintf(message, "DOOR ID: %u", object_brush.level);
                else if(object_brush.family == FAMILY_TELEPORTER)
                    sprintf(message, "GROUP: %u", object_brush.level);
                else if(object_brush.family == FAMILY_EXIT)
                    sprintf(message, "EXIT TO: %u", object_brush.level);
                else if(object_brush.family != FAMILY_STAIN)
                    sprintf(message, "POWER: %u", object_brush.level);
                break;
            case ORDER_WEAPON:
                if(object_brush.family == FAMILY_DOOR)
                    sprintf(message, "DOOR ID: %u", object_brush.level);
                else
                    sprintf(message, "POWER: %u", object_brush.level);
                break;
            default:
                break;
        }
        
        if(strlen(message) > 0)
            scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);
        
        numobs = myscreen->level_data.numobs;
        //myscreen->fastbox(lm,L_D(curline),55,7,27, 1);
        sprintf(message, "OB: %d", numobs);
        scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);
    }
    
    if(mode == TERRAIN)
    {
        // Show the current brush
        myscreen->putbuffer(lm+25, PIX_TOP-16-1, GRID_SIZE, GRID_SIZE,
                            0, 0, 320, 200, myscreen->level_data.pixdata[terrain_brush.terrain].data);
        // Border
        myscreen->draw_box(lm+25, PIX_TOP-16-1, lm+25+GRID_SIZE, PIX_TOP-16-1+GRID_SIZE, RED, 0, 1);
        
        // Show the background grid
        for (i=0; i < PIX_OVER; i++)
        {
            for (j=0; j < 4; j++)
            {
                whichback = (i+(j+rowsdown)*4) % (sizeof(backgrounds)/4);
                myscreen->putbuffer(S_RIGHT+i*GRID_SIZE, PIX_TOP+j*GRID_SIZE,
                                    GRID_SIZE, GRID_SIZE,
                                    0, 0, 320, 200,
                                    myscreen->level_data.pixdata[ backgrounds[whichback] ].data);
            }
        }
        myscreen->draw_box(S_RIGHT, PIX_TOP,
                           S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, 0, 0, 1);
        
        #ifndef USE_TOUCH_INPUT
        
        // Draw cursor
        int mx, my;
        mx = mymouse[MOUSE_X];
        my = mymouse[MOUSE_Y];
        bool over_radar = (mx > myscreen->viewob[0]->endx - myradar.xview - 4
                        && my > myscreen->viewob[0]->endy - myradar.yview - 4
                        && mx < myscreen->viewob[0]->endx - 4 && my < myscreen->viewob[0]->endy - 4);
        if(!over_radar && !Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mx, my) && !mouse_on_menus(mx, my))
        {
            // Draw target tile
            int worldx = mx + level->topx;
            int worldy = my + level->topy;
            int gridx = worldx - (worldx)%GRID_SIZE;
            int gridy = worldy - (worldy)%GRID_SIZE;
            int screenx = gridx - level->topx;
            int screeny = gridy - level->topy;
            myscreen->draw_box(screenx, screeny, screenx + GRID_SIZE, screeny + GRID_SIZE, YELLOW, 0, 1);
        }
        #endif
    }
    else if(mode == OBJECT)
    {
        // Draw current brush
        // Background
        myscreen->draw_box(lm+25, PIX_TOP-16-1, lm+25+GRID_SIZE, PIX_TOP-16-1+GRID_SIZE, PURE_BLACK, 1, 1);
        // Guy
        walker* newob = level->add_ob(ORDER_LIVING, FAMILY_ELF);
        newob->setxy(lm+25 + level->topx, PIX_TOP-16-1 + level->topy);
        newob->set_data(level->myloader->graphics[PIX(object_brush.order, object_brush.family)]);
        level->myloader->set_walker(newob, object_brush.order, object_brush.family);
        newob->team_num = object_brush.team;
        newob->draw_tile(myscreen->viewob[0]);
        // Border
        myscreen->draw_box(lm+25, PIX_TOP-16-1, lm+25+GRID_SIZE, PIX_TOP-16-1+GRID_SIZE, RED, 0, 1);
        
        myscreen->draw_box(S_RIGHT, PIX_TOP,
                           S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, PURE_BLACK, 1, 1);
        myscreen->draw_box(S_RIGHT, PIX_TOP,
                           S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, WHITE, 0, 1);
        
        for (i=0; i < PIX_OVER; i++)
        {
            for (j=0; j < 4; j++)
            {
                int index = (i + ((j+rowsdown) * PIX_OVER)) % (object_pane.size());
                if(index < int(object_pane.size()))
                {
                    newob->setxy(S_RIGHT+i*GRID_SIZE + level->topx, PIX_TOP+j*GRID_SIZE + level->topy);
                    newob->set_data(level->myloader->graphics[PIX(object_pane[index].order, object_pane[index].family)]);
                    level->myloader->set_walker(newob, object_pane[index].order, object_pane[index].family);
                    newob->team_num = object_brush.team;
                    newob->draw_tile(myscreen->viewob[0]);
                }
            }
        }
        
        #ifndef USE_TOUCH_INPUT
        
        // Draw cursor
        int mx, my;
        mx = mymouse[MOUSE_X];
        my = mymouse[MOUSE_Y];
        bool over_radar = (mx > myscreen->viewob[0]->endx - myradar.xview - 4
                        && my > myscreen->viewob[0]->endy - myradar.yview - 4
                        && mx < myscreen->viewob[0]->endx - 4 && my < myscreen->viewob[0]->endy - 4);
        bool over_info = Rect(lm-4, L_D(-1)+4, 315 - (lm-4), L_D(7)-2 - L_D(-1)).contains(mx, my);
        if(!over_radar && !over_info && !Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mx, my) && !mouse_on_menus(mx, my))
        {
            // Prepare object sprite
            newob->setxy(mx + level->topx, my + level->topy);
            newob->set_data(level->myloader->graphics[PIX(object_brush.order, object_brush.family)]);
            level->myloader->set_walker(newob, object_brush.order, object_brush.family);
            newob->team_num = object_brush.team;
            
            // Get size rounded up to nearest GRID_SIZE
            int w = newob->sizex;
            int h = newob->sizey;
            w += GRID_SIZE - (w%GRID_SIZE == 0? GRID_SIZE : w%GRID_SIZE);
            h += GRID_SIZE - (h%GRID_SIZE == 0? GRID_SIZE : h%GRID_SIZE);
            
            // Draw target tile
            if(object_brush.snap_to_grid)
            {
                int worldx = mx + level->topx;
                int worldy = my + level->topy;
                int gridx = worldx - (worldx)%GRID_SIZE;
                int gridy = worldy - (worldy)%GRID_SIZE;
                int screenx = gridx - level->topx;
                int screeny = gridy - level->topy;
                myscreen->draw_box(screenx, screeny, screenx + w, screeny + h, YELLOW, 0, 1);
            }
            
            // Draw current brush near cursor
            newob->draw(myscreen->viewob[0]);
        }
        #endif
        
        level->remove_ob(newob,0);
    }
    
    
    
    // Draw top menu
    for(set<SimpleButton*>::iterator e = menu_buttons.begin(); e != menu_buttons.end(); e++)
        (*e)->draw(myscreen, scentext);
    
    // Draw submenus
    for(list<pair<SimpleButton*, set<SimpleButton*> > >::iterator e = current_menu.begin(); e != current_menu.end(); e++)
    {
        set<SimpleButton*>& s = e->second;
        for(set<SimpleButton*>::iterator f = s.begin(); f != s.end(); f++)
            (*f)->draw(myscreen, scentext);
    }
    
    
	myscreen->buffer_to_screen(0, 0, 320, 200);

	return 1;
}


void LevelEditorData::clear_terrain()
{
    int w = level->grid.w;
    int h = level->grid.h;
    
    memset(level->grid.data, 1, w*h);
    resmooth_terrain();
}

void LevelEditorData::resmooth_terrain()
{
    level->mysmoother.smooth();
    myradar.update(level);
}

// For released button
int mouse_up_button = 0;

void LevelEditorData::mouse_down(int mx, int my)
{
    dragging = false;
}

// Deltas for motion
int mouse_motion_x = 0;
int mouse_motion_y = 0;
int mouse_last_x = 0;
int mouse_last_y = 0;

extern float mouse_scale_x;
extern float mouse_scale_y;

void LevelEditorData::mouse_motion(int mx, int my, int dx, int dy)
{
    if(mymouse[MOUSE_LEFT])
    {
        if(mode == SELECT && !mouse_on_menus(mouse_last_x, mouse_last_y))
        {
            Sint32 worldx = mx + level->topx - myscreen->viewob[0]->xloc; // - S_LEFT
            Sint32 worldy = my + level->topy - myscreen->viewob[0]->yloc; // - S_UP
            
            walker* under_cursor = NULL;
            if(!dragging && !rect_selecting)
            {
                // Did we start dragging a selected object?
                under_cursor = get_object(worldx, worldy);
                
                walker* got_one = NULL;
                for(vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
                {
                    if(e->target == under_cursor)
                    {
                        got_one = under_cursor;
                        break;
                    }
                }
                under_cursor = got_one;
            }
            
            if((dragging || under_cursor != NULL) && selection.size() > 0)
            {
                // Drag the selected objects
                dragging = true;
                for(vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
                {
                    walker* w = e->get_object(level);
                    if(w != NULL)
                    {
                        w->setxy(w->xpos + dx, w->ypos + dy);
                        
                        // Update selection position
                        e->x = w->xpos;
                        e->y = w->ypos;
                    }
                }
            }
            
            if(!dragging)
            {
                // Select with a rectangle
                float worldx = mx + level->topx - myscreen->viewob[0]->xloc;
                float worldy = my + level->topy - myscreen->viewob[0]->yloc;
                if(!rect_selecting)
                {
                    selection_rect.x = worldx;
                    selection_rect.y = worldy;
                    selection_rect.w = 1;
                    selection_rect.h = 1;
                    rect_selecting = true;
                }
                
                selection_rect.w = worldx - selection_rect.x;
                selection_rect.h = worldy - selection_rect.y;
                
            }
        }
    }
}

bool is_in_selection(walker* w, const vector<SelectionInfo>& selection)
{
    for(vector<SelectionInfo>::const_iterator e = selection.begin(); e != selection.end(); e++)
    {
        if(e->target == w)
            return true;
    }
    return false;
}

// Make sure to use reset_mode_buttons() after this
void add_contained_objects_to_selection(LevelData* level, const Rectf& area, vector<SelectionInfo>& selection)
{
	oblink* here;

	here = level->oblist;
	while(here)
	{
		if(here->ob && area.contains(here->ob->xpos + here->ob->sizex/2, here->ob->ypos + here->ob->sizey/2))
		{
		    if(!is_in_selection(here->ob, selection))
                selection.push_back(SelectionInfo(here->ob));
		}
		
		here = here->next;
	}

	here = level->fxlist;
	while(here)
	{
		if(here->ob && area.contains(here->ob->xpos + here->ob->sizex/2, here->ob->ypos + here->ob->sizey/2))
		{
		    if(!is_in_selection(here->ob, selection))
                selection.push_back(SelectionInfo(here->ob));
		}
		
		here = here->next;
	}

	here = level->weaplist;
	while(here)
	{
		if(here->ob && area.contains(here->ob->xpos + here->ob->sizex/2, here->ob->ypos + here->ob->sizey/2))
		{
		    if(!is_in_selection(here->ob, selection))
                selection.push_back(SelectionInfo(here->ob));
		}
		
		here = here->next;
	}
}


void LevelEditorData::mouse_up(int mx, int my, int old_mx, int old_my, bool& done)
{
    if(dragging)
    {
        dragging = false;
        return;
    }
    
    bool mouse_on_menu = mouse_on_menus(mx, my);
    bool old_mouse_on_menu = mouse_on_menus(old_mx, old_my);
    bool on_menu = mouse_on_menu && old_mouse_on_menu;
    bool off_menu = !mouse_on_menu && !old_mouse_on_menu;
    
    // Clicking on menu items
    if(on_menu)
    {
        // FILE
        if(activate_sub_menu_button(mx, my, current_menu, fileButton, true))
        {
            set<SimpleButton*> s;
            s.insert(&fileCampaignButton);
            s.insert(&fileLevelButton);
            s.insert(&fileQuitButton);
            current_menu.push_back(std::make_pair(&fileButton, s));
        }
        // Campaign >
        else if(activate_sub_menu_button(mx, my, current_menu, fileCampaignButton))
        {
            set<SimpleButton*> s;
            //s.insert(&fileCampaignImportButton);
            //s.insert(&fileCampaignShareButton);
            s.insert(&fileCampaignNewButton);
            s.insert(&fileCampaignLoadButton);
            s.insert(&fileCampaignSaveButton);
            s.insert(&fileCampaignSaveAsButton);
            current_menu.push_back(std::make_pair(&fileCampaignButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignImportButton))
        {
            bool cancel = false;
            if(levelchanged)
            {
                cancel = !yes_or_no_prompt("Import", "Discard unsaved level changes?", false);
            }
            
            if(campaignchanged)
            {
                cancel = !yes_or_no_prompt("Import", "Discard unsaved campaign changes?", false);
            }
            
            if(!cancel)
            {
                popup_dialog("Import Campaign", "Not yet implemented.");
                importCampaignPicker();
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignShareButton))
        {
            bool cancel = false;
            if(levelchanged)
            {
                if(yes_or_no_prompt("Share", "Save level first?", false))
                {
                    if(saveLevel())
                    {
                        timed_dialog("Level saved.");
                        redraw = 1;
                        levelchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Save failed.");
                        redraw = 1;
                        
                        cancel = true;
                    }
                }
            }
            
            if(campaignchanged)
            {
                if(yes_or_no_prompt("Share", "Save campaign first?", false))
                {
                    if(saveCampaign())
                    {
                        timed_dialog("Campaign saved.");
                        redraw = 1;
                        campaignchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Save failed.");
                        redraw = 1;
                        
                        cancel = true;
                    }
                }
            }
            
            if(!cancel)
            {
                popup_dialog("Share Campaign", "Not yet implemented.");
                shareCampaign(myscreen);
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignNewButton))
        {
            // Confirm if unsaved
            bool cancel = false;
            if (levelchanged)
            {
                cancel = !yes_or_no_prompt("New Campaign", "Discard unsaved changes?", false);
            }
            
            
            if(!cancel)
            {
                // Ask for campaign ID
                std::string campaign = "com.example.new_campaign";
                if(prompt_for_string(scentext, "New Campaign", campaign))
                {
                    // TODO: Check if campaign already exists and prompt the user to overwrite
                    if(does_campaign_exist(campaign) && !yes_or_no_prompt("Overwrite?", "Overwrite existing campaign with that ID?", false))
                    {
                        cancel = true;
                    }
                    
                    if(!cancel)
                    {
                        if(create_new_campaign(campaign))
                        {
                            
                            // Load campaign data for the editor
                            if(loadCampaign(campaign))
                            {
                                // Mount new campaign
                                unmount_campaign_package(get_mounted_campaign());
                                mount_campaign_package(campaign);
                                
                                // Load first scenario
                                std::list<int> levels = list_levels();
                                
                                if(levels.size() > 0)
                                {
                                    loadLevel(levels.front());
                                    // Update minimap
                                    myradar.start(level);
                                    timed_dialog("Campaign created.");
                                    campaignchanged = 0;
                                    levelchanged = 0;
                                }
                                else
                                {
                                    timed_dialog("Campaign has no scenarios!");
                                    redraw = 1;
                                }
                            }
                            else
                            {
                                timed_dialog("Failed to load new campaign.");
                                redraw = 1;
                            }
                        }
                        else
                        {
                            timed_dialog("Failed to create new campaign.");
                            redraw = 1;
                        }
                    }
                }
                
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignLoadButton))
        {
            // Pick a campaign, then load it and load the first level
            redraw = 1;
            bool cancel = false;
            if(campaignchanged)
            {
                cancel = !yes_or_no_prompt("Load Campaign", "Discard unsaved changes?", false);
            }
            
            if(!cancel)
            {
                CampaignResult result = pick_campaign(myscreen, NULL, true);
                if(result.id.size() > 0)
                {
                    if(loadCampaign(result.id))
                    {
                        unmount_campaign_package(get_mounted_campaign());
                        mount_campaign_package(result.id);
                        campaignchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Failed to load campaign.");
                        cancel = true;
                    }
                    
                    if(!cancel)
                    {
                        myscreen->clearbuffer();
                        // Prompt to load starting level.  If we don't, then the user can transfer levels between campaigns here.
                        bool load_first_level = yes_or_no_prompt("Load Campaign", "Load first level?", false);
                        if(load_first_level && levelchanged)
                        {
                            load_first_level = yes_or_no_prompt("Load Level", "Discard unsaved changes?", false);
                        }
                        
                        if(load_first_level)
                        {
                            // Load first scenario
                            if(loadLevel(result.first_level))
                            {
                                // Update minimap
                                myradar.start(level);
                                timed_dialog("Campaign loaded.");
                                levelchanged = 0;
                            }
                            else
                            {
                                timed_dialog("Failed to load first level.");
                            }
                        }
                        else
                            timed_dialog("Campaign loaded.");
                    }
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignSaveButton))
        {
            if(saveCampaign())
            {
                timed_dialog("Campaign saved.");
                campaignchanged = 0;
                redraw = 1;
            }
            else
            {
                timed_dialog("Failed to save campaign.");
                redraw = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignSaveAsButton))
        {
            CampaignResult result = pick_campaign(myscreen, NULL, true);
            if(result.id.size() > 0)
            {
                std::list<std::string> campaigns = list_campaigns();
                if(list_find(campaigns.begin(), campaigns.end(), result.id) == campaigns.end()
                    || yes_or_no_prompt("Overwrite", "Overwrite existing campaign?", false))
                {
                    if(saveCampaignAs(result.id))
                    {
                        timed_dialog("Campaign saved.");
                        campaignchanged = 0;
                        redraw = 1;
                    }
                    else
                    {
                        timed_dialog("Failed to save campaign.");
                        redraw = 1;
                    }
                }
            }
        }
        // Level >
        else if(activate_sub_menu_button(mx, my, current_menu, fileLevelButton))
        {
            set<SimpleButton*> s;
            s.insert(&fileLevelNewButton);
            s.insert(&fileLevelLoadButton);
            s.insert(&fileLevelSaveButton);
            s.insert(&fileLevelSaveAsButton);
            current_menu.push_back(std::make_pair(&fileLevelButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelNewButton))
        {
            // New level
            level->clear();
            level->create_new_grid();
            myradar.start(level);
            levelchanged = 1;
            redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelLoadButton))
        {
            // Confirm if unsaved
            bool cancel = false;
            if (levelchanged)
            {
                cancel = !yes_or_no_prompt("Load Level", "Discard unsaved changes?", false);
            }
            
            if(!cancel)
            {
                // Browse for the level to load
                int id = pick_level(myscreen, level->id, true);
                // Don't bother loading the level if it is the same, unchanged level
                if(id >= 0 && (levelchanged || id != level->id))
                {
                    if(loadLevel(id))
                    {
                        timed_dialog("Level loaded.");
                        levelchanged = 0;
                        redraw = 1;
                    }
                    else
                    {
                        timed_dialog("Failed to load level.");
                        redraw = 1;
                    }
                    
                    myradar.start(level);
                    redraw = 1;
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelSaveButton))
        {
            if(saveLevel())
            {
                timed_dialog("Level saved.");
                redraw = 1;
                levelchanged = 0;
            }
            else
            {
                timed_dialog("Save failed.");
                redraw = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelSaveAsButton))
        {
            int id = pick_level(myscreen, level->id, true);
            
            if(id >= 0 && id != level->id)
            {
                std::list<int> levels = list_levels();
                if(list_find(levels.begin(), levels.end(), id) == levels.end()
                    || yes_or_no_prompt("Overwrite", "Overwrite existing level?", false))
                {
                    if(saveLevelAs(id))
                    {
                        timed_dialog("Level saved.");
                        redraw = 1;
                        levelchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Save failed.");
                        redraw = 1;
                    }
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileQuitButton))
        {
            if((!levelchanged && !campaignchanged)
                || yes_or_no_prompt("Exit", "Quit without saving?", false))
            {
                done = true;
            }
        }
        // CAMPAIGN
        else if(activate_sub_menu_button(mx, my, current_menu, campaignButton, true))
        {
            set<SimpleButton*> s;
            s.insert(&campaignInfoButton);
            s.insert(&campaignProfileButton);
            s.insert(&campaignDetailsButton);
            s.insert(&campaignValidateButton);
            current_menu.push_back(std::make_pair(&campaignButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, campaignInfoButton))
        {
            char buf[512];
            snprintf(buf, 512, "%s\nID: %s\nTitle: %s\nVersion: %s\nAuthors: %s\nContributors: %s\nSugg. Power: %d\nFirst level: %d", 
                        (campaignchanged? "(unsaved)" : ""), campaign->id.c_str(), campaign->title.c_str(), campaign->version.c_str(), campaign->authors.c_str(), campaign->contributors.c_str(), campaign->suggested_power, campaign->first_level);
            popup_dialog("Campaign Info", buf);
        }
        // Profile >
        else if(activate_sub_menu_button(mx, my, current_menu, campaignProfileButton))
        {
            set<SimpleButton*> s;
            s.insert(&campaignProfileTitleButton);
            s.insert(&campaignProfileDescriptionButton);
            //s.insert(&campaignProfileIconButton);
            s.insert(&campaignProfileAuthorsButton);
            s.insert(&campaignProfileContributorsButton);
            current_menu.push_back(std::make_pair(&campaignProfileButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileTitleButton))
        {
            std::string title = campaign->title;
            if(prompt_for_string(scentext, "Campaign Title", title))
            {
                campaign->title = title;
                campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileDescriptionButton))
        {
            std::list<std::string> desc = campaign->description;
            if(prompt_for_string_block(scentext, "Campaign Description", desc))
            {
                campaign->description = desc;
                campaignchanged = 1;
            }
            redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileIconButton))
        {
            popup_dialog("Edit Icon", "Not yet implemented.");
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileAuthorsButton))
        {
            std::string authors = campaign->authors;
            if(prompt_for_string(scentext, "Campaign Authors", authors))
            {
                campaign->authors = authors;
                campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileContributorsButton))
        {
            std::string contributors = campaign->contributors;
            if(prompt_for_string(scentext, "Campaign Contributors", contributors))
            {
                campaign->contributors = contributors;
                campaignchanged = 1;
            }
        }
        // Details >
        else if(activate_sub_menu_button(mx, my, current_menu, campaignDetailsButton))
        {
            set<SimpleButton*> s;
            s.insert(&campaignDetailsVersionButton);
            s.insert(&campaignDetailsSuggestedPowerButton);
            s.insert(&campaignDetailsFirstLevelButton);
            current_menu.push_back(std::make_pair(&campaignDetailsButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, campaignDetailsVersionButton))
        {
            std::string version = campaign->version;
            if(prompt_for_string(scentext, "Campaign Version", version))
            {
                campaign->version = version;
                campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignDetailsSuggestedPowerButton))
        {
            char buf[20];
            snprintf(buf, 20, "%d", campaign->suggested_power);
            std::string power = buf;
            if(prompt_for_string(scentext, "Suggested Power", power))
            {
                campaign->suggested_power = toInt(power);
                campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignDetailsFirstLevelButton))
        {
            char buf[20];
            snprintf(buf, 20, "%d", campaign->first_level);
            std::string level = buf;
            if(prompt_for_string(scentext, "First Level", level))
            {
                campaign->first_level = toInt(level);
                campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignValidateButton))
        {
            std::list<int> levels = list_levels();
            std::set<int> connected;
            std::list<std::string> problems;
            
            // Are the levels all connected to the first level?
            int current_level = campaign->first_level;
            get_connected_level_exits(current_level, levels, connected, problems);
            
            for(std::list<int>::iterator e = levels.begin(); e != levels.end(); e++)
            {
                if(connected.find(*e) == connected.end())
                {
                    char buf[40];
                    snprintf(buf, 40, "Level %d is not connected.", *e);
                    problems.push_back(buf);
                }
            }
            
            // Get ready to show the user the problems
            char buf[512];
            if(problems.size() == 0)
            {
                snprintf(buf, 512, "No problems!");
            }
            else
            {
                // Only show the first 6 problems and "More problems..."
                if(problems.size() > 6)
                {
                    int num_over = problems.size() - 6;
                    while(problems.size() > 6)
                        problems.pop_back();
                    char buf[40];
                    snprintf(buf, 40, "%d more problems...", num_over);
                    problems.push_back(buf);
                }
                
                // Put all the problems together for the printer
                buf[0] = '\0';
                for(std::list<std::string>::iterator e = problems.begin(); e != problems.end(); e++)
                {
                    if(e->size() + strlen(buf) + 1 >= 512)
                        break;
                    strcat(buf, e->c_str());
                    strcat(buf, "\n");
                }
            }
            
            // Show user the problems
            popup_dialog("Validate Campaign", buf);
        }
        // LEVEL
        else if(activate_sub_menu_button(mx, my, current_menu, levelButton, true))
        {
            set<SimpleButton*> s;
            s.insert(&levelInfoButton);
            s.insert(&levelProfileButton);
            s.insert(&levelDetailsButton);
            s.insert(&levelGoalsButton);
            s.insert(&levelResmoothButton);
            s.insert(&levelDeleteTerrainButton);
            s.insert(&levelDeleteObjectsButton);
            current_menu.push_back(std::make_pair(&levelButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, levelInfoButton))
        {
            char buf[512];
            snprintf(buf, 512, "%s\nID number: %d\nTitle: %s\nSize: %ux%u",
                     (levelchanged? "(unsaved)" : ""), level->id, level->title.c_str(), level->grid.w, level->grid.h);
            popup_dialog("Level Info", buf);
        }
        // Profile >
        else if(activate_sub_menu_button(mx, my, current_menu, levelProfileButton))
        {
            set<SimpleButton*> s;
            s.insert(&levelProfileTitleButton);
            s.insert(&levelProfileDescriptionButton);
            current_menu.push_back(std::make_pair(&levelProfileButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, levelProfileTitleButton))
        {
            std::string title = level->title;
            if(prompt_for_string(scentext, "Level Title", title))
            {
                level->title = title;
                levelchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, levelProfileDescriptionButton))
        {
            std::list<std::string> desc = level->description;
            if(prompt_for_string_block(scentext, "Level Description", desc))
            {
                level->description = desc;
                levelchanged = 1;
            }
            redraw = 1;
        }
        // Details >
        else if(activate_sub_menu_button(mx, my, current_menu, levelDetailsButton))
        {
            set<SimpleButton*> s;
            s.insert(&levelDetailsMapSizeButton);
            s.insert(&levelDetailsParValueButton);
            current_menu.push_back(std::make_pair(&levelDetailsButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, levelDetailsMapSizeButton))
        {
            // Using two prompts sequentially
            
            char buf[20];
            snprintf(buf, 20, "%u", level->grid.w);
            std::string width = buf;
            snprintf(buf, 20, "%u", level->grid.h);
            std::string height = buf;
            
            if(prompt_for_string(scentext, "Map Width", width))
            {
                int w = toInt(width);
                int h;
                
                #ifdef ANDROID
                // The soft keyboard on Android might take a little while to be ready again, so opening it right away doesn't always work.
                SDL_Delay(1000);
                #endif
                if(prompt_for_string(scentext, "Map Height", height))
                {
                    h = toInt(height);
                    
                    // Validate here so we can tell the user
                    // Size is limited to one byte in the file format
                    if(w < 3 || h < 3 || w > 255 || h > 255)
                    {
                        char buf[200];
                        snprintf(buf, 200, "Can't resize grid to %dx%d\n", w, h);
                        if(w < 3)
                            strcat(buf, "Width is too small.\n");
                        if(h < 3)
                            strcat(buf, "Height is too small.\n");
                        if(w > 255)
                            strcat(buf, "Width is too big (max 255).\n");
                        if(h > 255)
                            strcat(buf, "Height is too big (max 255).\n");
                        
                        popup_dialog("Resize Map", buf);
                    }
                    else
                    {
                        if((w >= level->grid.w && h >= level->grid.h)
                            || !are_objects_outside_area(level, 0, 0, w, h)
                            || yes_or_no_prompt("Resize Map", "Delete objects outside of map?", false))
                        {
                            // Now change it
                            level->resize_grid(w, h);
                            
                            // Reset the minimap
                            myradar.start(level);
                            
                            draw(myscreen);
                            myscreen->refresh();
                            
                            char buf[30];
                            snprintf(buf, 30, "Resized map to %ux%u", level->grid.w, level->grid.h);
                            timed_dialog(buf);
                            redraw = 1;
                            levelchanged = 1;
                        }
                        else
                        {
                            timed_dialog("Resize canceled.");
                            redraw = 1;
                        }
                    }
                }
                else
                {
                    timed_dialog("Resize canceled.");
                    redraw = 1;
                }
            }
            else
            {
                timed_dialog("Resize canceled.");
                redraw = 1;
            }
        }
        // Goals >
        else if(activate_sub_menu_button(mx, my, current_menu, levelGoalsButton))
        {
            set<SimpleButton*> s;
            s.insert(&levelGoalsEnemiesButton);
            s.insert(&levelGoalsGeneratorsButton);
            s.insert(&levelGoalsNPCsButton);
            current_menu.push_back(std::make_pair(&levelGoalsButton, s));
        }
        else if(activate_menu_toggle_choice(mx, my, *this, levelGoalsEnemiesButton))
        {
            level->type ^= LevelData::TYPE_CAN_EXIT_WHENEVER;
            update_menu_buttons();
        }
        else if(activate_menu_toggle_choice(mx, my, *this, levelGoalsGeneratorsButton))
        {
            level->type ^= LevelData::TYPE_MUST_DESTROY_GENERATORS;
            update_menu_buttons();
        }
        else if(activate_menu_toggle_choice(mx, my, *this, levelGoalsNPCsButton))
        {
            level->type ^= LevelData::TYPE_MUST_PROTECT_NAMED_NPCS;
            update_menu_buttons();
        }
        else if(activate_menu_choice(mx, my, *this, levelDetailsParValueButton))
        {
            char buf[20];
            snprintf(buf, 20, "%d", level->par_value);
            std::string par = buf;
            if(prompt_for_string(scentext, "Par Value (num)", par))
            {
                int v = toInt(par);
                if(v > 0)
                {
                    level->par_value = v;
                    levelchanged = 1;
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, levelResmoothButton))
        {
            resmooth_terrain();
            levelchanged = 1;
            redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, levelDeleteTerrainButton))
        {
            if(yes_or_no_prompt("Clear Terrain", "Delete all terrain?", false))
            {
                clear_terrain();
                myradar.update(level);
                levelchanged = 1;
            }
            redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, levelDeleteObjectsButton))
        {
            if(yes_or_no_prompt("Clear Objects", "Delete all objects?", false))
            {
                level->delete_objects();
                myradar.update(level);
                levelchanged = 1;
            }
            redraw = 1;
        }
        // MODE
        else if(activate_sub_menu_button(mx, my, current_menu, modeButton, true))
        {
            set<SimpleButton*> s;
            s.insert(&modeTerrainButton);
            s.insert(&modeObjectButton);
            s.insert(&modeSelectButton);
            current_menu.push_back(std::make_pair(&modeButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, modeTerrainButton))
        {
            mode = TERRAIN;
            modeButton.label = "Edit (Terrain)";
            reset_mode_buttons();
        }
        else if(activate_menu_choice(mx, my, *this, modeObjectButton))
        {
            mode = OBJECT;
            modeButton.label = "Edit (Objects)";
            reset_mode_buttons();
        }
        else if(activate_menu_choice(mx, my, *this, modeSelectButton))
        {
            mode = SELECT;
            modeButton.label = "Edit (Select)";
            reset_mode_buttons();
        }
        else
        {
            // Check mode-specific buttons
            for(std::set<SimpleButton*>::iterator e = mode_buttons.begin(); e != mode_buttons.end(); e++)
            {
                if((*e)->contains(mx, my))
                {
                    activate_mode_button(*e);
                    redraw = 1;
                    break;
                }
            }
            
        }
    }
    else
    {
        // Either press or release was off of the menus
        // Close open menus
        if(current_menu.size() > 0)
        {
            current_menu.clear();
        }
    }
    
    if(off_menu)
    {
        // Clicked and released off the menu
        
        // Zardus: ADD: can move map by clicking on minimap
        if ((mode != SELECT || (!rect_selecting && !dragging)) && mx > myscreen->viewob[0]->endx - myradar.xview - 4
                && my > myscreen->viewob[0]->endy - myradar.yview - 4
                && mx < myscreen->viewob[0]->endx - 4 && my < myscreen->viewob[0]->endy - 4)
        {
            // Radar clicking is done by holding (in the level_editor function
        }
        else  // in the main window
        {
            Sint32 windowx = mx + level->topx - myscreen->viewob[0]->xloc; // - S_LEFT
            Sint32 windowy = my + level->topy - myscreen->viewob[0]->yloc; // - S_UP
            if (object_brush.snap_to_grid)
            {
                windowx -= (windowx%GRID_SIZE);
                windowy -= (windowy%GRID_SIZE);
            }

            if (mode == SELECT)
            {
                walker* newob = NULL;
                
                if(rect_selecting && (fabs(selection_rect.w) > 15 || fabs(selection_rect.h > 15)))
                {
                    rect_selecting = false;
                    
                    // Select guys in the rectangle
                    if(!keystates[KEYSTATE_LCTRL] && !keystates[KEYSTATE_RCTRL])
                        selection.clear();
                    add_contained_objects_to_selection(level, selection_rect, selection);
                    reset_mode_buttons();
                }
                else if (keystates[KEYSTATE_r]) // (re)name the current object
                {
                    newob = level->add_ob(ORDER_LIVING, FAMILY_ELF);
                    newob->setxy(windowx, windowy);
                    if (some_hit(windowx, windowy, newob, level))
                    {
                        std::string name = newob->collide_ob->stats->name;
                        if(prompt_for_string(scentext, "Rename", name))
                        {
                            strncpy(newob->collide_ob->stats->name, name.c_str(), 11);
                            newob->collide_ob->stats->name[11] = '\0';
                            levelchanged = 1;
                        }
                    }
                    level->remove_ob(newob,0);
                }
                else // select this object
                {
                    rect_selecting = false;
                    if(mx < 245-4 || my > L_D(7)-2)
                    {
                        newob = level->add_ob(ORDER_LIVING, FAMILY_ELF);
                        newob->setxy(windowx, windowy);
                        if (some_hit(windowx, windowy, newob, level))
                        {
                            // Clicked on a guy
                            walker* w = newob->collide_ob;
                            if(keystates[KEYSTATE_LCTRL] || keystates[KEYSTATE_RCTRL])
                            {
                                // Select/deselect another guy
                                bool deselected = false;
                                for(std::vector<SelectionInfo>::iterator e = selection.begin(); e != selection.end(); e++)
                                {
                                    // Identify the guy.  Not the best way...
                                    if(e->x == w->xpos && e->y == w->ypos && e->w == w->sizex && e->h == w->sizey)
                                    {
                                        deselected = true;
                                        selection.erase(e);
                                        break;
                                    }
                                }
                                if(!deselected)
                                    selection.push_back(SelectionInfo(w));
                            }
                            else
                            {
                                // Choose a single guy
                                selection.clear();
                                selection.push_back(SelectionInfo(w));
                            }
                        }
                        else if(!(keystates[KEYSTATE_LCTRL] || keystates[KEYSTATE_RCTRL]))
                            selection.clear();  // Deselect if not trying to grab more
                        
                        level->remove_ob(newob,0);
                        
                        reset_mode_buttons();
                    }
                }  // end of info mode
            }
            else if (mode == OBJECT)
            {
                if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
                {
                    //windowx = (mx - PIX_LEFT) / GRID_SIZE;
                    windowx = (mx-S_RIGHT) / GRID_SIZE;
                    windowy = (my - PIX_TOP) / GRID_SIZE;
                    int index =  (windowx + ((windowy+rowsdown) * PIX_OVER)) % (object_pane.size());
                    if(index < int(object_pane.size()))
                    {
                        object_brush.order = object_pane[index].order;
                        object_brush.family = object_pane[index].family;
                    }
                } // end of background grid window
                else if(mx < 245-4 || my > L_D(7)-2)
                {
                    walker* newob = NULL;
                    if(!object_brush.picking)
                    {
                        // Create new object here (apply brush)
                        levelchanged = 1;
                        newob = level->add_ob(object_brush.order, object_brush.family);
                        newob->setxy(windowx, windowy);
                        newob->team_num = object_brush.team;
                        newob->stats->level = object_brush.level;
                        newob->dead = 0; // just in case
                        newob->collide_ob = 0;
                        // Is there already something there?
                        if ( object_brush.snap_to_grid && some_hit(windowx, windowy, newob, level))
                        {
                            if (newob)
                            {
                                level->remove_ob(newob,0);
                                newob = NULL;
                            }
                        }  // end of failure to put guy
                        else if(!object_brush.snap_to_grid)
                        {
                            newob->draw(myscreen->viewob[0]);
                            myscreen->buffer_to_screen(0, 0, 320, 200);
                            start_time_s = query_timer();
                            while ( mymouse[MOUSE_LEFT] && (query_timer()-start_time_s) < 36 )
                            {
                                mymouse = query_mouse();
                            }
                            levelchanged = 1;
                        }
                    }
                    else
                    {
                        pick_by_mouse(mx, my);
                        object_brush.picking = false;
                        pickerButton.set_colors_normal();
                    }
                }
            }  // end of putting a guy
            if (mode == TERRAIN)
            {
                if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
                {
                    //windowx = (mx - PIX_LEFT) / GRID_SIZE;
                    windowx = (mx-S_RIGHT) / GRID_SIZE;
                    windowy = (my - PIX_TOP) / GRID_SIZE;
                    terrain_brush.terrain = backgrounds[ (windowx + ((windowy+rowsdown) * PIX_OVER))
                                             % (sizeof(backgrounds)/4)];
                    terrain_brush.terrain %= NUM_BACKGROUNDS;
                } // end of background grid window
                else
                {
                    windowx /= GRID_SIZE;  // get the map position ..
                    windowy /= GRID_SIZE;
                    
                    // Terrain painting is done by holding in level_editor()
                    
                    if(terrain_brush.picking)
                    {
                        // Set brush to the grid tile
                        pick_by_mouse(mx, my);
                        terrain_brush.picking = false;
                        pickerButton.set_colors_normal();
                    }
                }
            }  // end of setting grid square
        } // end of main window
    }
}

void LevelEditorData::pick_by_mouse(int mx, int my)
{
    Sint32 windowx = mx + level->topx - myscreen->viewob[0]->xloc; // - S_LEFT
    Sint32 windowy = my + level->topy - myscreen->viewob[0]->yloc; // - S_UP
    
    // Set brush to the grid tile
    if(mode == TERRAIN)
    {
        // Snap to grid
        windowx -= (windowx%GRID_SIZE);
        windowy -= (windowy%GRID_SIZE);
        
        // Reduce to array dims
        windowx /= GRID_SIZE;
        windowy /= GRID_SIZE;
        
        // Get tile from grid array
        terrain_brush.terrain = level->grid.data[windowy*(level->grid.w)+windowx];
    }
    else if(mode == OBJECT)
    {
        // Snap to grid
        if (object_brush.snap_to_grid)
        {
            windowx -= (windowx%GRID_SIZE);
            windowy -= (windowy%GRID_SIZE);
        }
        
        // Get object from level
        walker* w = get_object(windowx, windowy);
        if(w != NULL)
        {
            object_brush.set(w);
        }
    }
}


    
walker* LevelEditorData::get_object(int x, int y)
{
    walker* result = NULL;
    walker* newob = level->add_ob(ORDER_LIVING, FAMILY_ELF);
    newob->setxy(x, y);
    if (some_hit(x, y, newob, level))
    {
        result = newob->collide_ob;
    }
    level->remove_ob(newob,0);
    return result;
}


bool are_objects_outside_area(LevelData* level, int x, int y, int w, int h)
{
    x *= GRID_SIZE;
    y *= GRID_SIZE;
    w *= GRID_SIZE;
    h *= GRID_SIZE;
    
	oblink* here;

	here = level->oblist;
	while(here)
	{
		if(here->ob && (x > here->ob->xpos || here->ob->xpos >= x + w || y > here->ob->ypos || here->ob->ypos >= y + h))
		{
		    return true;
		}
		
		here = here->next;
	}

	here = level->fxlist;
	while(here)
	{
		if(here->ob && (x > here->ob->xpos || here->ob->xpos >= x + w || y > here->ob->ypos || here->ob->ypos >= y + h))
		{
		    return true;
		}
		
		here = here->next;
	}

	here = level->weaplist;
	while(here)
	{
		if(here->ob && (x > here->ob->xpos || here->ob->xpos >= x + w || y > here->ob->ypos || here->ob->ypos >= y + h))
		{
		    return true;
		}
		
		here = here->next;
	}
	
	return false;
}

enum EventTypeEnum {HANDLED_EVENT, TEXT_EVENT, SCROLL_EVENT, MOUSE_MOTION_EVENT, MOUSE_DOWN_EVENT, MOUSE_UP_EVENT, KEY_DOWN_EVENT};

EventTypeEnum handle_basic_editor_event(const SDL_Event& event)
{
    switch (event.type)
    {
#ifdef USE_SDL2
    case SDL_WINDOWEVENT:   
        handle_window_event(event);
        return HANDLED_EVENT;
    case SDL_TEXTINPUT:
        handle_text_event(event);
        return TEXT_EVENT;
    case SDL_MOUSEWHEEL:
        handle_mouse_event(event);
        return SCROLL_EVENT;
    case SDL_FINGERMOTION:
        handle_mouse_event(event);
        mouse_motion_x = event.tfinger.dx*(320);
        mouse_motion_y = event.tfinger.dy*(200);
        return MOUSE_MOTION_EVENT;
    case SDL_FINGERUP:
        {
            int left_state = mymouse[MOUSE_LEFT];
            int right_state = mymouse[MOUSE_RIGHT];
            handle_mouse_event(event);
            if(left_state != mymouse[MOUSE_LEFT])
                mouse_up_button = MOUSE_LEFT;
            else if(right_state != mymouse[MOUSE_RIGHT])
                mouse_up_button = MOUSE_RIGHT;
            else
                mouse_up_button = 0;
        }
        return MOUSE_UP_EVENT;
    case SDL_FINGERDOWN:
        handle_mouse_event(event);
        return MOUSE_DOWN_EVENT;
#endif
    case SDL_KEYDOWN:
        handle_key_event(event);
        return KEY_DOWN_EVENT;
    case SDL_KEYUP:
        handle_key_event(event);
        return HANDLED_EVENT;
    case SDL_MOUSEMOTION:
        handle_mouse_event(event);
        mouse_motion_x = event.motion.xrel / mouse_scale_x;
        mouse_motion_y = event.motion.yrel / mouse_scale_y;
        return MOUSE_MOTION_EVENT;
    case SDL_MOUSEBUTTONUP:
        {
            int left_state = mymouse[MOUSE_LEFT];
            int right_state = mymouse[MOUSE_RIGHT];
            handle_mouse_event(event);
            if(left_state != mymouse[MOUSE_LEFT])
                mouse_up_button = MOUSE_LEFT;
            else if(right_state != mymouse[MOUSE_RIGHT])
                mouse_up_button = MOUSE_RIGHT;
            else
                mouse_up_button = 0;
        }
        return MOUSE_UP_EVENT;
    case SDL_MOUSEBUTTONDOWN:
        handle_mouse_event(event);
        return MOUSE_DOWN_EVENT;
    case SDL_JOYAXISMOTION:
        handle_joy_event(event);
        return HANDLED_EVENT;
    case SDL_JOYBUTTONDOWN:
        handle_joy_event(event);
        return HANDLED_EVENT;
    case SDL_JOYBUTTONUP:
        handle_joy_event(event);
        return HANDLED_EVENT;
    case SDL_QUIT:
        quit(0);
        return HANDLED_EVENT;
    default:
        return HANDLED_EVENT;
    }
}

#define PAN_LIMIT_UP -60
#define PAN_LIMIT_DOWN (GRID_SIZE*data.level->grid.h - 200 + 80)
#define PAN_LIMIT_LEFT -60
#define PAN_LIMIT_RIGHT (GRID_SIZE*data.level->grid.w - 320 + 80)

bool pan_left = false;
bool pan_right = false;
bool pan_up = false;
bool pan_down = false;

Sint32 level_editor()
{
    static LevelEditorData data;
    EditorTerrainBrush& terrain_brush = data.terrain_brush;
    EditorObjectBrush& object_brush = data.object_brush;
    text*& scentext = data.scentext;
    ModeEnum& mode = data.mode;
    radar& myradar = data.myradar;
    
	Sint32 i,j;
	Sint32 windowx, windowy;
	Sint32 mx, my;
    
    // Initialize palette for cycling
    load_and_set_palette("our.pal", scenpalette);
	
	scentext = new text(myscreen);
    
    if(data.reloadCampaign())
        Log("Loaded campaign data successfully.\n");
    else
        Log("Failed to load campaign data.\n");
    
    std::string old_campaign = get_mounted_campaign();
    if(old_campaign.size() > 0)
        unmount_campaign_package(old_campaign);
    mount_campaign_package(data.campaign->id);
    

    std::list<int> levels = list_levels();
    if(levels.size() > 0)
    {
        if(data.loadLevel(levels.front()))
        {
            Log("Loaded level data successfully.\n");
        }
        else
            Log("Failed to load level data.\n");
    }
    else
        Log("Campaign has no valid levels!\n");

	redraw = 1;  // Redraw right away
	
	object_pane.clear();
	for(int i = 0; i < NUM_FAMILIES; i++)
    {
        object_pane.push_back(ObjectType(ORDER_LIVING, i));
    }
	for(int i = 0; i < MAX_TREASURE+1; i++)
    {
        object_pane.push_back(ObjectType(ORDER_TREASURE, i));
    }
	for(int i = 0; i < 4; i++)
    {
        object_pane.push_back(ObjectType(ORDER_GENERATOR, i));
    }
    
    object_pane.push_back(ObjectType(ORDER_WEAPON, FAMILY_DOOR));
    object_pane.push_back(ObjectType(ORDER_SPECIAL, FAMILY_RESERVED_TEAM));
	
	// Minimap
	myradar.start(data.level);
	
	// GUI
	using std::set;
	using std::pair;
	using std::list;
	
	data.reset_mode_buttons();
	
    #ifdef USE_CONTROLLER_INPUT
    mymouse[MOUSE_X] = 160;
    mymouse[MOUSE_Y] = 100;
    #endif
	
    mouse_last_x = mymouse[MOUSE_X];
    mouse_last_y = mymouse[MOUSE_Y];
    
    float cycletimer = 0.0f;
	grab_mouse();
	Uint32 last_ticks = SDL_GetTicks();
	Uint32 start_ticks = last_ticks;

	//
	// This is the main program loop
	//
	bool done = false;
	SDL_Event event;
	while(!done)
	{
		// Reset the timer count to zero ...
		reset_timer();

		if (myscreen->end)
		{
		    done = true;
			break;
		}
		
        while(SDL_PollEvent(&event))
        {
            #ifdef USE_CONTROLLER_INPUT
            if(didPlayerPressKey(0, KEY_FIRE, event))
            {
                // Send fake mouse down event
                SDL_Event event;
                
                event.type = SDL_MOUSEBUTTONDOWN;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.x = mymouse[MOUSE_X]*mouse_scale_x;
                event.button.y = mymouse[MOUSE_Y]*mouse_scale_y;
                SDL_PushEvent(&event);
                continue;
            }
            if(didPlayerReleaseKey(0, KEY_FIRE, event))
            {
                // Send fake mouse up event
                SDL_Event event;
                
                event.type = SDL_MOUSEBUTTONUP;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.x = mymouse[MOUSE_X]*mouse_scale_x;
                event.button.y = mymouse[MOUSE_Y]*mouse_scale_y;
                SDL_PushEvent(&event);
                continue;
            }
            #endif
            switch(handle_basic_editor_event(event))
            {
            case MOUSE_MOTION_EVENT:
                data.mouse_motion(mymouse[MOUSE_X], mymouse[MOUSE_Y], mouse_motion_x, mouse_motion_y);
                break;
            case MOUSE_DOWN_EVENT:
                if(mymouse[MOUSE_LEFT])
                {
                    mouse_last_x = mymouse[MOUSE_X];
                    mouse_last_y = mymouse[MOUSE_Y];
                    
                    data.mouse_down(mymouse[MOUSE_X], mymouse[MOUSE_Y]);
                }
                break;
            case MOUSE_UP_EVENT:
                
                if(mouse_up_button == MOUSE_LEFT)
                {
                    data.mouse_up(mymouse[MOUSE_X], mymouse[MOUSE_Y], mouse_last_x, mouse_last_y, done);
                    redraw = 1;
                }
                else if(mouse_up_button == MOUSE_RIGHT)
                {
                    // Picking with right mouse button
                    data.pick_by_mouse(mymouse[MOUSE_X], mymouse[MOUSE_Y]);
                    redraw = 1;
                }
                break;
            case KEY_DOWN_EVENT:
                redraw = 1;
                if(event.key.keysym.sym == SDLK_ESCAPE)
                {
                    if((!levelchanged && !campaignchanged)
                        || yes_or_no_prompt("Exit", "Quit without saving?", false))
                    {
                        done = true;
                        break;
                    }
                }
                
                // Change teams ..
                else if(event.key.keysym.sym == SDLK_0)
                    object_brush.team = 0;
                else if(event.key.keysym.sym == SDLK_1)
                    object_brush.team = 1;
                else if(event.key.keysym.sym == SDLK_2)
                    object_brush.team = 2;
                else if(event.key.keysym.sym == SDLK_3)
                    object_brush.team = 3;
                else if(event.key.keysym.sym == SDLK_4)
                    object_brush.team = 4;
                else if(event.key.keysym.sym == SDLK_5)
                    object_brush.team = 5;
                else if(event.key.keysym.sym == SDLK_6)
                    object_brush.team = 6;
                else if(event.key.keysym.sym == SDLK_7)
                    object_brush.team = 7;
                // Toggle grid alignment
                else if(event.key.keysym.sym == SDLK_g)
                {
                    if(mode == OBJECT || mode == SELECT)
                        data.activate_mode_button(&data.gridSnapButton);
                }
                // Save scenario
                else if(event.key.keysym.sym == SDLK_s && (event.key.keysym.mod & KMOD_CTRL))
                {
                    bool saved = false;
                    if(levelchanged)
                    {
                        if(data.saveLevel())
                        {
                            levelchanged = 0;
                            saved = true;
                        }
                        else
                            timed_dialog("Failed to save level.");
                    }
                    if(campaignchanged)
                    {
                        if(data.saveCampaign())
                        {
                            campaignchanged = 0;
                            saved = true;
                        }
                        else
                            timed_dialog("Failed to save campaign.");
                    }
                    
                    if(saved)
                        timed_dialog("Saved.");
                    else if(!levelchanged && !campaignchanged)
                        timed_dialog("No changes to save.");
                }  // end of saving routines

                // Change level of current guy being placed ..
                else if(event.key.keysym.sym == SDLK_RIGHTBRACKET)
                {
                    if(mode == OBJECT)
                        object_brush.level++;
                }
                else if(event.key.keysym.sym == SDLK_LEFTBRACKET)
                {
                    if(mode == OBJECT && object_brush.level > 1)
                        object_brush.level--;
                }
                else if(event.key.keysym.sym == SDLK_DELETE)
                {
                    if(mode == SELECT)
                        data.activate_mode_button(&data.deleteButton);
                }
                else if(event.key.keysym.sym == SDLK_o)
                {
                    if(mode == OBJECT)
                    {
                        mode = SELECT;
                        data.modeButton.label = "Edit (Select)";
                    }
                    else
                    {
                        mode = OBJECT;
                        data.modeButton.label = "Edit (Objects)";
                    }
                    data.reset_mode_buttons();
                }
                else if(event.key.keysym.sym == SDLK_t)
                {
                    if(mode == TERRAIN)
                    {
                        mode = SELECT;
                        data.modeButton.label = "Edit (Select)";
                    }
                    else
                    {
                        mode = TERRAIN;
                        data.modeButton.label = "Edit (Terrain)";
                    }
                    data.reset_mode_buttons();
                }
                // Smooth current map, F5
                else if(event.key.keysym.sym == SDLK_F5)
                {
                    data.resmooth_terrain();
                    levelchanged = 1;
                }
                // Change to new palette ..
                else if(event.key.keysym.sym == SDLK_F9)
                {
                    load_and_set_palette("our.pal", scenpalette);
                }
                break;
            default:
                break;
            }
        }

        #ifdef USE_CONTROLLER_INPUT
        {
            int dx = 0;
            int dy = 0;
            OuyaController& c = OuyaControllerManager::getController(0);
            if(!c.isStickBeyondDeadzone(OuyaController::AXIS_LS_X))
            {
                if(isPlayerHoldingKey(0, KEY_UP) || isPlayerHoldingKey(0, KEY_UP_LEFT) || isPlayerHoldingKey(0, KEY_UP_RIGHT))
                {
                    dy = -5;
                }
                if(isPlayerHoldingKey(0, KEY_DOWN) || isPlayerHoldingKey(0, KEY_DOWN_LEFT) || isPlayerHoldingKey(0, KEY_DOWN_RIGHT))
                {
                    dy = 5;
                }
                if(isPlayerHoldingKey(0, KEY_LEFT) || isPlayerHoldingKey(0, KEY_UP_LEFT) || isPlayerHoldingKey(0, KEY_DOWN_LEFT))
                {
                    dx = -5;
                }
                if(isPlayerHoldingKey(0, KEY_RIGHT) || isPlayerHoldingKey(0, KEY_UP_RIGHT) || isPlayerHoldingKey(0, KEY_DOWN_RIGHT))
                {
                    dx = 5;
                }
            }
            else
            {
                dx = 5*c.getAxisValue(OuyaController::AXIS_LS_X);
                dy = 5*c.getAxisValue(OuyaController::AXIS_LS_Y);
            }
            
            if(mymouse[MOUSE_X] + dx < 0)
                mymouse[MOUSE_X] = 0;
            if(mymouse[MOUSE_X] + dx > 320)
                mymouse[MOUSE_X] = 320;
            if(mymouse[MOUSE_Y] + dy < 0)
                mymouse[MOUSE_Y] = 0;
            if(mymouse[MOUSE_Y] + dy > 200)
                mymouse[MOUSE_Y] = 200;
            
            if(dx != 0 || dy != 0)
            {
                int x, y;
                SDL_Event event;
                
                event.type = SDL_MOUSEMOTION;
                event.motion.type = SDL_MOUSEMOTION;
                event.motion.windowID = 0;
                event.motion.which = 0;
                event.motion.state = SDL_GetMouseState(&x, &y);
                event.motion.xrel = dx*mouse_scale_x;
                event.motion.yrel = dy*mouse_scale_y;
                event.motion.x = mymouse[MOUSE_X]*mouse_scale_x + event.motion.xrel;
                event.motion.y = mymouse[MOUSE_Y]*mouse_scale_y + event.motion.yrel;
                SDL_PushEvent(&event);
            }
        }
        
        #ifdef OUYA
            
            const OuyaController& c = OuyaControllerManager::getController(0);
            
            float vx = c.getAxisValue(OuyaController::AXIS_RS_X);
            float vy = c.getAxisValue(OuyaController::AXIS_RS_Y);
            
            // Scroll the tile selector when over it
            if(Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mymouse[MOUSE_X], mymouse[MOUSE_Y]))
            {
                if(fabs(vy) > OuyaController::DEADZONE)
                    scroll_amount = -2*vy;
                else
                    scroll_amount = 0;
            }
            else
            {
                scroll_amount = 0;
                
                // Panning
                pan_left = (vx < -OuyaController::DEADZONE);
                pan_right = (vx > OuyaController::DEADZONE);
                pan_up = (vy < -OuyaController::DEADZONE);
                pan_down = (vy > OuyaController::DEADZONE);
            }
        #endif
        
        #endif

		short scroll_amount = get_and_reset_scroll_amount();
		#if defined(USE_TOUCH_INPUT)
		// Only scroll the tile selector when touching it and you've already moved a bit
		if(mymouse[MOUSE_LEFT] && Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mymouse[MOUSE_X], mymouse[MOUSE_Y]) && fabs(mouse_last_y - mymouse[MOUSE_Y]) > 4)
        {
		#endif
		// Slide tile selector down ..
		if (keystates[KEYSTATE_DOWN] || scroll_amount < 0)
		{
			rowsdown++;
			if (rowsdown >= maxrows)
				rowsdown -= maxrows;
            
            redraw = 1;
            
			while (keystates[KEYSTATE_DOWN])
				get_input_events(WAIT);
		}

		// Slide tile selector up ..
		if (keystates[KEYSTATE_UP] || scroll_amount > 0)
		{
			rowsdown--;
			if (rowsdown < 0)
				rowsdown += maxrows;
			if (rowsdown <0 || rowsdown >= maxrows) // bad case
				rowsdown = 0;
            
            redraw = 1;
            
			while (keystates[KEYSTATE_UP])
				get_input_events(WAIT);
		}
		#if defined(USE_TOUCH_INPUT)
        }
		#endif


		// Scroll the screen (panning)
		#ifndef OUYA
		pan_left = (keystates[KEYSTATE_KP_4] || keystates[KEYSTATE_KP_7] || keystates[KEYSTATE_KP_1] || keystates[KEYSTATE_a]);
		pan_right = (keystates[KEYSTATE_KP_6] || keystates[KEYSTATE_KP_3] || keystates[KEYSTATE_KP_9] || keystates[KEYSTATE_d]);
		pan_up = (keystates[KEYSTATE_KP_8] || keystates[KEYSTATE_KP_7] || keystates[KEYSTATE_KP_9] || keystates[KEYSTATE_w]);
		pan_down = (keystates[KEYSTATE_KP_2] || keystates[KEYSTATE_KP_1] || keystates[KEYSTATE_KP_3] || keystates[KEYSTATE_s]);
		#endif
		if (pan_up && data.level->topy >= PAN_LIMIT_UP) // top of the screen
        {
            redraw = 1;
			data.level->add_draw_pos(0, -SCROLLSIZE);
        }
		if (pan_down && data.level->topy <= PAN_LIMIT_DOWN) // scroll down
        {
            redraw = 1;
			data.level->add_draw_pos(0, SCROLLSIZE);
        }
		if (pan_left && data.level->topx >= PAN_LIMIT_LEFT) // scroll left
        {
            redraw = 1;
			data.level->add_draw_pos(-SCROLLSIZE, 0);
        }
		if (pan_right && data.level->topx <= PAN_LIMIT_RIGHT) // scroll right
        {
            redraw = 1;
			data.level->add_draw_pos(SCROLLSIZE, 0);
        }


		// Mouse stuff ..
		mymouse = query_mouse_no_poll();
		
		if (mymouse[MOUSE_LEFT])       // put or remove the current guy
		{
			redraw = 1;
			mx = mymouse[MOUSE_X];
			my = mymouse[MOUSE_Y];
            
            // Holding on menu items
            bool mouse_on_menu = data.mouse_on_menus(mx, my);
            bool old_mouse_on_menu = data.mouse_on_menus(mouse_last_x, mouse_last_y);
            bool on_menu = mouse_on_menu && old_mouse_on_menu;
            bool off_menu = !mouse_on_menu && !old_mouse_on_menu;
            
            if(on_menu)
            {
                // Panning with mouse (touch)
                if(data.panUpButton.contains(mx, my) && data.level->topy >= PAN_LIMIT_UP) // top of the screen
                {
                    redraw = 1;
                    data.level->add_draw_pos(0, -SCROLLSIZE);
                }
                else if(data.panUpRightButton.contains(mx, my))
                {
                    redraw = 1;
                    if(data.level->topy >= PAN_LIMIT_UP)
                        data.level->add_draw_pos(0, -SCROLLSIZE);
                    if(data.level->topx <= PAN_LIMIT_RIGHT)
                        data.level->add_draw_pos(SCROLLSIZE, 0);
                }
                else if(data.panUpLeftButton.contains(mx, my))
                {
                    redraw = 1;
                    if(data.level->topy >= PAN_LIMIT_UP)
                        data.level->add_draw_pos(0, -SCROLLSIZE);
                    if(data.level->topx >= PAN_LIMIT_LEFT)
                        data.level->add_draw_pos(-SCROLLSIZE, 0);
                }
                else if(data.panDownButton.contains(mx, my) && data.level->topy <= PAN_LIMIT_DOWN) // scroll down
                {
                    redraw = 1;
                    data.level->add_draw_pos(0, SCROLLSIZE);
                }
                else if(data.panDownRightButton.contains(mx, my))
                {
                    redraw = 1;
                    if(data.level->topy <= PAN_LIMIT_DOWN)
                        data.level->add_draw_pos(0, SCROLLSIZE);
                    if(data.level->topx <= PAN_LIMIT_RIGHT)
                        data.level->add_draw_pos(SCROLLSIZE, 0);
                }
                else if(data.panDownLeftButton.contains(mx, my))
                {
                    redraw = 1;
                    if(data.level->topy <= PAN_LIMIT_DOWN)
                        data.level->add_draw_pos(0, SCROLLSIZE);
                    if(data.level->topx >= PAN_LIMIT_LEFT)
                        data.level->add_draw_pos(-SCROLLSIZE, 0);
                }
                else if(data.panLeftButton.contains(mx, my) && data.level->topx >= PAN_LIMIT_LEFT) // scroll left
                {
                    redraw = 1;
                    data.level->add_draw_pos(-SCROLLSIZE, 0);
                }
                else if(data.panRightButton.contains(mx, my) && data.level->topx <= PAN_LIMIT_RIGHT) // scroll right
                {
                    redraw = 1;
                    data.level->add_draw_pos(SCROLLSIZE, 0);
                }
                    
            }
            else if(off_menu)
            {
                // Zardus: ADD: can move map by clicking on minimap
                if ((mode != SELECT || (!data.rect_selecting && !data.dragging)) && mx > myscreen->viewob[0]->endx - myradar.xview - 4
                        && my > myscreen->viewob[0]->endy - myradar.yview - 4
                        && mx < myscreen->viewob[0]->endx - 4 && my < myscreen->viewob[0]->endy - 4)
                {
                    mx -= myscreen->viewob[0]->endx - myradar.xview - 4;
                    my -= myscreen->viewob[0]->endy - myradar.yview - 4;

                    // Zardus: above set_screen_pos doesn't take into account that minimap scrolls too. This one does.
                    data.level->set_draw_pos(myradar.radarx * GRID_SIZE + mx * GRID_SIZE - 160,
                                    myradar.radary * GRID_SIZE + my * GRID_SIZE - 100);
                }
                else  // in the main window
                {
                    windowx = mymouse[MOUSE_X] + data.level->topx - myscreen->viewob[0]->xloc; // - S_LEFT
                    windowx -= (windowx%GRID_SIZE);
                    windowy = mymouse[MOUSE_Y] + data.level->topy - myscreen->viewob[0]->yloc; // - S_UP
                    windowy -= (windowy%GRID_SIZE);

                    if (mode == TERRAIN)
                    {
                        if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
                        {
                            // Picking the tile is done in LevelEditorData::mouse_up()
                        } // end of background grid window
                        else
                        {
                            windowx /= GRID_SIZE;  // get the map position ..
                            windowy /= GRID_SIZE;
                            
                            if(!terrain_brush.picking)
                            {
                                // Set to our current selection (apply brush)
                                data.level->grid.data[windowy*(data.level->grid.w)+windowx] = get_random_matching_tile(terrain_brush.terrain);
                                levelchanged = 1;
                                if (terrain_brush.use_smoothing) // smooth a few squares, if not control
                                {
                                    for (i=windowx-1; i <= windowx+1; i++)
                                        for (j=windowy-1; j <=windowy+1; j++)
                                            if (i >= 0 && i < data.level->grid.w &&
                                                    j >= 0 && j < data.level->grid.h)
                                                data.level->mysmoother.smooth(i, j);
                                }
                                
                                myradar.update(data.level);
                            }
                        }
                    }  // end of setting grid square
                } // end of main window
            }

		}      // end of left mouse button

		// Now perform color cycling if selected
		if (cyclemode)
		{
		    cycletimer -= (start_ticks - last_ticks)/1000.0f;
		    if(cycletimer <= 0)
            {
                cycletimer = 0.5f;
                cycle_palette(scenpalette, WATER_START, WATER_END, 1);
                cycle_palette(scenpalette, ORANGE_START, ORANGE_END, 1);
            }
			redraw = 1;
		}
		
		// Redraw screen
		if (redraw)
		{
            redraw = 0;
			data.draw(myscreen);
			
            #ifdef USE_CONTROLLER_INPUT
            myscreen->fastbox(mymouse[MOUSE_X]-1, mymouse[MOUSE_Y]-1, 4, 4, PURE_WHITE);
            myscreen->fastbox(mymouse[MOUSE_X], mymouse[MOUSE_Y], 2, 2, PURE_BLACK);
            #endif
            myscreen->refresh();
		}
        
        SDL_Delay(10);
        
	    last_ticks = start_ticks;
	    start_ticks = SDL_GetTicks();

	}
	
	// Reset the screen position so it doesn't ruin the main menu
    data.level->set_draw_pos(0, 0);
    // Update the screen's position
    data.level->draw(myscreen);
    // Clear the background
    myscreen->clearbuffer();
    
    unmount_campaign_package(data.campaign->id);
    mount_campaign_package(old_campaign);
    
    delete scentext;
    
	return OK;
}


void set_screen_pos(screen *myscreen, Sint32 x, Sint32 y)
{
	myscreen->level_data.topx = x;
	myscreen->level_data.topy = y;
	redraw = 1;
}


void remove_first_ob(screen *master)
{
	oblink  *here;

	here = master->level_data.oblist;

	while (here)
	{
		if (here->ob)
		{
			delete here->ob;
			return;
		}
		else
			here = here->next;
	}
}

char get_random_matching_tile(Sint32 whatback)
{
	Sint32 i;

	i = random(4);  // max # of types of any particular ..

	switch (whatback)
	{
		case PIX_GRASS1:
			switch (i)
			{
				case 0:
					return PIX_GRASS1;
				case 1:
					return PIX_GRASS2;
				case 2:
					return PIX_GRASS3;
				case 3:
					return PIX_GRASS4;
				default:
					return PIX_GRASS1;
			}
			//break;
		case PIX_GRASS_DARK_1:
			switch (i)
			{
				case 0:
					return PIX_GRASS_DARK_1;
				case 1:
					return PIX_GRASS_DARK_2;
				case 2:
					return PIX_GRASS_DARK_3;
				case 3:
					return PIX_GRASS_DARK_4;
				default:
					return PIX_GRASS_DARK_1;
			}
			//break;
		case PIX_GRASS_DARK_B1:
		case PIX_GRASS_DARK_B2:
			switch (i)
			{
				case 0:
				case 1:
					return PIX_GRASS_DARK_B1;
				case 2:
				case 3:
				default:
					return PIX_GRASS_DARK_B2;
			}
			//break;
		case PIX_GRASS_DARK_R1:
		case PIX_GRASS_DARK_R2:
			switch (i)
			{
				case 0:
				case 1:
					return PIX_GRASS_DARK_R1;
				case 2:
				case 3:
				default:
					return PIX_GRASS_DARK_R2;
			}
			//break;
		case PIX_WATER1:
			switch (i)
			{
				case 0:
					return PIX_WATER1;
				case 1:
					return PIX_WATER2;
				case 2:
					return PIX_WATER3;
				default:
					return PIX_WATER1;
			}
			//break;
		case PIX_PAVEMENT1:
			switch (random(12))
			{
				case 0:
					return PIX_PAVEMENT1;
				case 1:
					return PIX_PAVEMENT2;
				case 2:
					return PIX_PAVEMENT3;
				default:
					return PIX_PAVEMENT1;
			}
			//break;
		case PIX_COBBLE_1:
			switch (random(i))
			{
				case 0:
					return PIX_COBBLE_1;
				case 1:
					return PIX_COBBLE_2;
				case 2:
					return PIX_COBBLE_3;
				case 3:
					return PIX_COBBLE_4;
				default:
					return PIX_COBBLE_1;
			}
			//break;
		case PIX_BOULDER_1:
			switch (random(i))
			{
				case 0:
					return PIX_BOULDER_1;
				case 1:
					return PIX_BOULDER_2;
				case 2:
					return PIX_BOULDER_3;
				case 3:
					return PIX_BOULDER_4;
				default:
					return PIX_BOULDER_1;
			}
			//break;
		case PIX_JAGGED_GROUND_1:
			switch (i)
			{
				case 0:
					return PIX_JAGGED_GROUND_1;
				case 1:
					return PIX_JAGGED_GROUND_2;
				case 2:
					return PIX_JAGGED_GROUND_3;
				case 3:
					return PIX_JAGGED_GROUND_4;
				default:
					return PIX_JAGGED_GROUND_1;
			}
		default:
			return whatback;
	}
}

// Copy of collide from obmap; used manually .. :(
Sint32 check_collide(Sint32 x,  Sint32 y,  Sint32 xsize,  Sint32 ysize,
                   Sint32 x2, Sint32 y2, Sint32 xsize2, Sint32 ysize2)
{
	if (x < x2)
	{
		if (y < y2)
		{
			if (x2 - x < xsize &&
			        y2 - y < ysize)
				return 1;
		}
		else // y >= y2
		{
			if (x2 - x < xsize &&
			        y - y2 < ysize2)
				return 1;
		}
	}
	else // x >= x2
	{
		if (y < y2)
		{
			if (x - x2 < xsize2 &&
			        y2 - y < ysize)
				return 1;
		}
		else // y >= y2
		{
			if (x - x2 < xsize2 &&
			        y - y2 < ysize2)
				return 1;
		}
	}
	return 0;
}

// The old-fashioned hit check ..
walker * some_hit(Sint32 x, Sint32 y, walker  *ob, LevelData* data)
{
	oblink  *here;

	here = data->oblist;

	while (here)
	{
		if (here->ob && here->ob != ob)
			if (check_collide(x, y, ob->sizex, ob->sizey,
			                  here->ob->xpos, here->ob->ypos,
			                  here->ob->sizex, here->ob->sizey) )
			{
				ob->collide_ob = here->ob;
				return here->ob;
			}
		here = here->next;
	}

	// Also check the fx list ..
	here = data->fxlist;
	while (here)
	{
		if (here->ob && here->ob != ob)
			if (check_collide(x, y, ob->sizex, ob->sizey,
			                  here->ob->xpos, here->ob->ypos,
			                  here->ob->sizex, here->ob->sizey) )
			{
				ob->collide_ob = here->ob;
				return here->ob;
			}
		here = here->next;
	}

	// Also check the weapons list ..
	here = data->weaplist;
	while (here)
	{
		if (here->ob && !here->ob->dead && here->ob != ob)
			if (check_collide(x, y, ob->sizex, ob->sizey,
			                  here->ob->xpos, here->ob->ypos,
			                  here->ob->sizex, here->ob->sizey) )
			{
				ob->collide_ob = here->ob;
				return here->ob;
			}
		here = here->next;
	}

	ob->collide_ob = NULL;
	return NULL;
}


