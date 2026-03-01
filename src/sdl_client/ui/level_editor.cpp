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

#include <openglad/runtime/screen.h>
#include <openglad/render/pal32.h>
#include <openglad/render/pixien.h>
#include <openglad/render/view.h>
#include <openglad/render/radar.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/data/smooth.h>
#include <openglad/render/walker_draw.h>
#include <openglad/input/input.h>
#include <openglad/core/util.h>
#include <openglad/platform/io.h>
#include <openglad/render/text.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/level_runtime_data.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/ui/level_picker.h>
#include <span>
#include <openglad/ui/campaign_picker.h>
#include <openglad/data/gparser.h>
#include <openglad/render/sai2x.h>
#include <openglad/runtime/level_editor_state.h>
#include <openglad/runtime/game_session.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <memory>

static inline LevelEditorState& eds() { return *og::runtime::current_session->editor_; }

// scroll_amount is now a macro via input.h → current_session->scroll_amount_

void quit(Sint32 arg1);

/* Changelog
 * 	8/8/02: Zardus: added scrolling-by-minimap
 * 		Zardus: added scrolling-by-keyboard
 */

#define OK 4 //this function was successful, continue normal operation

#include <string>
#include <vector>
#include <cstdlib>
#define MINIMUM_TIME 0

static inline cfg_store& active_config()
{
    return cfg;
}


#define S_LEFT 1
#define S_RIGHT 245
#define S_UP 1
#define S_DOWN 188

static constexpr char VERSION_NUM = 8; // save scenario type info
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
void popup_dialog(const char* title, const char* message);
void timed_dialog(const char* message, float delay_seconds = 3.0f);

enum class Mode { Terrain, Object, Select };

void set_screen_pos(screen *scr, Sint32 x, Sint32 y);
walker * some_hit(Sint32 x, Sint32 y, walker  *ob, LevelRuntimeData* data);
Sint32 get_random_matching_tile(Sint32 whatback);

class EditorTerrainBrush;
class EditorObjectBrush;
void info_box(walker  *target, screen * scr);
void set_name(walker  *target, screen * scr);

// myscreen and theprefs are now macros defined in base.h / view.h

// eds().scenpalette, eds().redraw, eds().campaignchanged, eds().levelchanged, eds().cyclemode, start_time_s
// moved into LevelEditorState (per-session via eds())

class Rect
{
public:
    
    int x, y;
    unsigned int w, h;
    
	Rect();
	Rect(int x_, int y_, unsigned int w_, unsigned int h_);
	    
	bool contains(int px, int py) const;
};

Rect::Rect()
    : x(0), y(0), w(0), h(0)
{}

Rect::Rect(int x_, int y_, unsigned int w_, unsigned int h_)
    : x(x_), y(y_), w(w_), h(h_)
{}

bool Rect::contains(int px, int py) const
{
    return (this->x <= px && px < int(this->x + w) && this->y <= py && py < int(this->y + h));
}

class Rectf
{
public:
    
    float x, y;
    float w, h;
    
	Rectf();
	Rectf(float x_, float y_, float w_, float h_);
	    
	bool contains(float X, float Y) const;
};

Rectf::Rectf()
    : x(0), y(0), w(0), h(0)
{}

Rectf::Rectf(float x_, float y_, float w_, float h_)
    : x(x_), y(y_), w(w_), h(h_)
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
    Order order;
    unsigned char family;

	    ObjectType()
	        : order(Order::Living), family(0)
	    {}
	    ObjectType(Order order_, unsigned char family_)
	        : order(order_), family(family_)
	    {}
};

std::vector<ObjectType> object_pane;

// eds().rowsdown and eds().maxrows moved into LevelEditorState (per-session via eds())

bool save_level_and_map(screen* ascreen);
bool does_campaign_exist(const std::string& campaign_id);
bool create_new_campaign(const std::string& campaign_id);
void importCampaignPicker();
void shareCampaign(screen* scr);

bool prompt_for_string_block(const std::string& message, std::list<std::string>& result);
bool prompt_for_string(const std::string& message, std::string& result);


class SimpleButton
{
public:
    SDL_Rect area;
    std::string label;
    bool remove_border;
    bool draw_top_separator;
    unsigned char base_color;
    unsigned char high_color;
    unsigned char shadow_color;
    unsigned char text_color;
    bool centered;
    
    SimpleButton(const std::string& label_, int x_, int y_, unsigned int w_, unsigned int h_, bool remove_border_ = false, bool draw_top_separator_ = false);
	    
    void draw(screen* s);
    bool contains(int x, int y) const;
    
    void set_colors_normal();
    void set_colors_enabled();
    void set_colors_disabled();
    void set_colors_active();
};


SimpleButton::SimpleButton(const std::string& label_, int x_, int y_, unsigned int w_, unsigned int h_, bool remove_border_, bool draw_top_separator_)
    : label(label_), remove_border(remove_border_), draw_top_separator(draw_top_separator_), centered(false)
{
    set_colors_normal();
	    
    area.x = x_;
    area.y = y_;
    area.w = w_;
    area.h = h_;
}

void SimpleButton::draw(screen* s)
{
    s->draw_button_colored(area.x, area.y, area.x + area.w - 1, area.y + area.h - 1, !remove_border, base_color, high_color, shadow_color);
    if(remove_border && draw_top_separator)
        s->hor_line(area.x, area.y, area.w, shadow_color);
	    
    text& mytext = s->text_normal;
	    
    if(centered)
        mytext.write_xy(area.x + area.w/2 - 3*static_cast<Sint32>(label.size()), area.y + area.h/2 - 2, label.c_str(), static_cast<unsigned char>(text_color), 1);
    else
        mytext.write_xy(area.x + 2, area.y + area.h/2 - 2, label.c_str(), static_cast<unsigned char>(text_color), 1);
}

bool SimpleButton::contains(int x, int y) const
{
    return (area.x <= x && x < area.x + area.w
            && area.y <= y && y < area.y + area.h);
}

void SimpleButton::set_colors_normal()
{
    text_color = static_cast<unsigned char>(DARK_BLUE);
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
    text_color = static_cast<unsigned char>(WHITE);
    base_color = static_cast<unsigned char>(ORANGE_START);
    high_color = static_cast<unsigned char>(ORANGE_START + 3);
    shadow_color = static_cast<unsigned char>(ORANGE_START + 5);
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
    Order order;
    Sint32 family;
    char team;
    Sint32 level;
    bool picking;
    
    EditorObjectBrush()
        : snap_to_grid(true), order(Order::Living), family(0), team(1), level(1), picking(false)
    {}
    
    void set(walker* target)
    {
        if(target == nullptr)
        {
            order = Order::Living;
            family = 0;
            team = 1;
            level = 1;
        }
        else
        {
            order = target->query_order();
            family = target->family;
            team = target->team_num;
            level = target->stats()->level;
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
    Order order;
    Sint32 family;
    Sint32 level;
    walker* target;


    SelectionInfo()
        : valid(false), x(0), y(0), w(GRID_SIZE), h(GRID_SIZE), order(Order::Living), family(FAMILY_SOLDIER), level(1), target(nullptr)
    {}
    SelectionInfo(walker* target_)
        : valid(false), x(0), y(0), w(GRID_SIZE), h(GRID_SIZE), order(Order::Living), family(FAMILY_SOLDIER), level(1), target(target_)
    {
        set(target_);
    }
    
    void clear()
    {
        valid = false;
        name.clear();
        x = 0;
        y = 0;
        w = GRID_SIZE;
        h = GRID_SIZE;
        order = Order::Living;
        family = FAMILY_SOLDIER;
        level = 1;
    }
    void set(walker* target_)
    {
        if(target_ == nullptr)
            clear();
        else
        {
            valid = true;
            name = target_->stats()->name;
            x = target_->xpos;
            y = target_->ypos;
            w = target_->sizex;
            h = target_->sizey;
            order = target_->query_order();
            family = target_->family;
            level = target_->stats()->level;
            this->target = target_;
        }
    }
    
    walker* get_object(LevelRuntimeData* /*level_data*/)
    {
        if(!valid)
            return nullptr;
        
        return target;
    }
};

std::string get_editor_family_label(Order order, Sint32 family, char livings[][20], const char* treasures[], const char* weapons[]);
std::string get_editor_level_label(Order order, Sint32 family, Sint32 level);

class LevelEditorData
{
public:
    std::unique_ptr<CampaignData> campaign;
    std::unique_ptr<LevelRuntimeData> level;
    
	Mode mode;
    EditorTerrainBrush terrain_brush;
    EditorObjectBrush object_brush;
    std::vector<SelectionInfo> selection;
    bool rect_selecting;
    Rectf selection_rect;
    bool dragging;
    
	radar myradar;
	
	Uint16 menu_button_height;
	
	std::set<SimpleButton*> menu_buttons;
	// The active menu buttons
	std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > > current_menu;
	// The mode-specific buttons
	std::set<SimpleButton*> mode_buttons;
	std::set<SimpleButton*> pan_buttons;
	
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
	SimpleButton levelDetailsMapSizeButton, levelDetailsParValueButton, levelDetailsTimeLimitButton;
	
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
    
    void draw(screen* s);
    Sint32 display_panel(screen* s);
    
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
    
    bool is_in_grid(int x, int y);
    unsigned char get_terrain(int x, int y);
    void set_terrain(int x, int y, unsigned char terrain);
    walker* get_object(int x, int y);
};

bool are_objects_outside_area(LevelRuntimeData* level, int x, int y, int w, int h);
enum class EventType;
EventType handle_basic_editor_event(const SDL_Event& event);

#define DEFAULT_EDITOR_MENU_BUTTON_HEIGHT 20

#ifdef REDUCE_OVERSCAN
#define OVERSCAN_PADDING 6
#else
#define OVERSCAN_PADDING 0
#endif

LevelEditorData::LevelEditorData()
    : campaign(std::make_unique<CampaignData>("org.openglad.gladiator")), level(std::make_unique<LevelRuntimeData>(1, false, &sdl_level_data_hooks())), mode(Mode::Terrain), rect_selecting(false), dragging(false), myradar(og::runtime::current_session->myscreen_->viewob[0].get(), og::runtime::current_session->myscreen_, 0)
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
	, levelDetailsTimeLimitButton("Time limit...", levelDetailsParValueButton.area.x, levelDetailsParValueButton.area.y + levelDetailsParValueButton.area.h, 95, menu_button_height, true, true)
	
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
    level->world().id = id;
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
    if(remount_campaign_package_with_error() != CampaignPackageIoError::None)
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
    if(remount_campaign_package_with_error() != CampaignPackageIoError::None)
    {
        Log("Failed to remount campaign after saving it.\n");
        return false;
    }
    
    return result;
}


bool LevelEditorData::saveLevelAs(int id)
{
    level->world().id = id;
    level->grid_file = std::format("scen{}", id);
    
    std::string old_campaign = get_mounted_campaign();
    unpack_campaign(old_campaign);
    bool result = level->save();
    if(result)
        result = repack_campaign(old_campaign);
    cleanup_unpacked_campaign();

    // Remount for consistency in PhysFS
    (void)remount_campaign_package_with_error();

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
    for(std::set<SimpleButton*>::const_iterator e = menu_buttons.begin(); e != menu_buttons.end(); e++)
    {
        if((*e)->contains(mx, my))
            return true;
    }
    
    for(std::set<SimpleButton*>::const_iterator e = mode_buttons.begin(); e != mode_buttons.end(); e++)
    {
        if((*e)->contains(mx, my))
            return true;
    }
    
    // Count anything in the area of the pan buttons
    if(pan_buttons.size() > 0 && Rect(panLeftButton.area.x, panUpButton.area.y, panRightButton.area.x + panRightButton.area.w - panLeftButton.area.x, panDownButton.area.y + panDownButton.area.h - panUpButton.area.y).contains(mx, my))
        return true;
    
    for(std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >::const_iterator e = current_menu.begin(); e != current_menu.end(); e++)
    {
        const std::set<SimpleButton*>& s = e->second;
        for(std::set<SimpleButton*>::const_iterator f = s.begin(); f != s.end(); f++)
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
    levelGoalsEnemiesButton.label += (level->world().type & GameWorld::TYPE_CAN_EXIT_WHENEVER? "Off" : "On");
    
    levelGoalsGeneratorsButton.label = "Beat generators: ";
    levelGoalsGeneratorsButton.label += (level->world().type & GameWorld::TYPE_MUST_DESTROY_GENERATORS? "On" : "Off");
    
    levelGoalsNPCsButton.label = "Protect NPCs: ";
    levelGoalsNPCsButton.label += (level->world().type & GameWorld::TYPE_MUST_PROTECT_NAMED_NPCS? "On" : "Off");
}

void LevelEditorData::reset_mode_buttons()
{
    mode_buttons.clear();
    switch(mode)
    {
        case Mode::Terrain:
        mode_buttons.insert(&pickerButton);
        mode_buttons.insert(&terrainSmoothButton);
        if(terrain_brush.picking)
            pickerButton.set_colors_active();
        else
            pickerButton.set_colors_normal();
        break;
        case Mode::Object:
        mode_buttons.insert(&pickerButton);
        mode_buttons.insert(&gridSnapButton);
        mode_buttons.insert(&prevTeamButton);
        mode_buttons.insert(&nextTeamButton);
        if(object_brush.picking)
            pickerButton.set_colors_active();
        else
            pickerButton.set_colors_normal();
        break;
        case Mode::Select:
        mode_buttons.insert(&gridSnapButton);
        if(selection.size() == 1 && selection.front().order == Order::Living)
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
        if(mode == Mode::Terrain)
        {
            terrain_brush.picking = !terrain_brush.picking;
            if(terrain_brush.picking)
                pickerButton.set_colors_active();
            else
                pickerButton.set_colors_normal();
        }
        else if(mode == Mode::Object)
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
        if(selection.size() == 1 && selection.front().order == Order::Living)
        {
            walker* obj = selection.front().get_object(level.get());
            if(obj != nullptr)
            {
                std::string name = obj->stats()->name;
                if(prompt_for_string("Rename", name))
                {
                    obj->stats()->name = name;
                    selection.front().name = obj->stats()->name;
                    eds().levelchanged = 1;
                }
            }
        }
    }
    else if(button == &prevTeamButton)
    {
        if(mode == Mode::Select)
        {
            for(auto& sel : selection)
            {
                walker* obj = sel.get_object(level.get());
                if(obj != nullptr)
                {
                    if(obj->team_num > 0)
                        obj->team_num = obj->team_num - 1;
                    else
                        obj->team_num = MAX_TEAM;
                    eds().levelchanged = 1;
                }
            }
        }
        else if(mode == Mode::Object)
        {
            if(object_brush.team > 0)
                object_brush.team--;
            else
                object_brush.team = MAX_TEAM;
        }
    }
    else if(button == &nextTeamButton)
    {
        if(mode == Mode::Select)
        {
            for(auto& sel : selection)
            {
                walker* obj = sel.get_object(level.get());
                if(obj != nullptr)
                {
                    if(obj->team_num < MAX_TEAM)
                        obj->team_num = obj->team_num + 1;
                    else
                        obj->team_num = 0;
                    eds().levelchanged = 1;
                }
            }
        }
        else if(mode == Mode::Object)
        {
            if(object_brush.team < MAX_TEAM)
                object_brush.team++;
            else
                object_brush.team = 0;
        }
    }
    else if(button == &prevLevelButton)
    {
        for(auto& sel : selection)
        {
            walker* obj = sel.get_object(level.get());
            if(obj != nullptr)
            {
                if(obj->stats()->level > 1)
                {
                    obj->stats()->level--;
                    sel.level = obj->stats()->level;
                    eds().levelchanged = 1;
                }
            }
        }
    }
    else if(button == &nextLevelButton)
    {
        for(auto& sel : selection)
        {
            walker* obj = sel.get_object(level.get());
            if(obj != nullptr)
            {
                obj->stats()->level++;
                sel.level = obj->stats()->level;
                eds().levelchanged = 1;
            }
        }
    }
    else if(button == &prevClassButton)
    {
        for(auto& sel : selection)
        {
            walker* obj = sel.get_object(level.get());
            if(obj != nullptr && obj->query_order() == Order::Living)
            {
                if(sel.family > 0)
                    sel.family--;
                else
                    sel.family = NUM_FAMILIES-1;
                og::runtime::current_session->myscreen_->myloader->set_walker(obj, sel.order, sel.family);
                obj->ani_type = ANI_WALK;
                obj->transform_to(sel.order, sel.family);
                obj->set_frame(obj->ani[obj->curdir][0]);
                obj->setxy(sel.x, sel.y);
                sel.set(obj);

                eds().levelchanged = 1;
            }
        }
    }
    else if(button == &nextClassButton)
    {
        for(auto& sel : selection)
        {
            walker* obj = sel.get_object(level.get());
            if(obj != nullptr && obj->query_order() == Order::Living)
            {
                if(sel.family+1 < NUM_FAMILIES)
                    sel.family++;
                else
                    sel.family = 0;
                og::runtime::current_session->myscreen_->myloader->set_walker(obj, sel.order, sel.family);
                obj->ani_type = ANI_WALK;
                obj->transform_to(sel.order, sel.family);
                obj->set_frame(obj->ani[obj->curdir][0]);
                obj->setxy(sel.x, sel.y);
                sel.set(obj);

                eds().levelchanged = 1;
            }
        }
    }
    else if(button == &facingButton)
    {
        for(auto& sel : selection)
        {
            walker* obj = sel.get_object(level.get());
            if(obj != nullptr)
            {
                if(obj->curdir < FACE_UP_LEFT)
                    obj->curdir = obj->curdir + 1;
                else
                    obj->curdir = FACE_UP;
				obj->set_frame(obj->ani[obj->curdir][0]);
                eds().levelchanged = 1;
            }
        }
    }
    else if(button == &deleteButton)
    {
        for(auto& sel : selection)
        {
            walker* obj = sel.get_object(level.get());
            if(obj != nullptr)
            {
                level->remove_ob(obj);
                eds().levelchanged = 1;
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
    
    MouseState& mymouse = query_mouse_no_poll();
    while (mymouse.left)
    {
        SDL_Delay(1);
        get_input_events(POLL);
    }

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
    
    MouseState& mymouse = query_mouse_no_poll();
    while (mymouse.left)
    {
        SDL_Delay(1);
        get_input_events(POLL);
    }

    // Close menu
    data.current_menu.clear();
    data.draw(og::runtime::current_session->myscreen_);
    og::runtime::current_session->myscreen_->refresh();
    return true;
}

bool activate_menu_toggle_choice(int mx, int my, LevelEditorData& data, SimpleButton& button, bool is_in_top_menu = false)
{
    // Make sure it is showing
    if(!button.contains(mx, my) || (!is_in_top_menu && !button_showing(data.current_menu, &button)))
        return false;
    
    MouseState& mymouse = query_mouse_no_poll();
    while (mymouse.left)
    {
        SDL_Delay(1);
        get_input_events(POLL);
    }

    // Close menu
    data.draw(og::runtime::current_session->myscreen_);
    og::runtime::current_session->myscreen_->refresh();
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
    LevelRuntimeData d(current_level);
    if(!d.load())
    {
        problems.push_back(std::format("Level {} failed to load.", current_level));
        return;
    }
    
    // Get the exits
    std::set<int> exits;
    for(auto& uptr : d.world().fxlist)
    {
        walker* w = uptr.get();
        if(w->query_order() == Order::Treasure && w->family == FAMILY_EXIT && w->stats() != nullptr)
            exits.insert(w->stats()->level);
    }
    
    // With no exits, we'll progress directly to the next sequential level
    if(exits.size() == 0)
    {
        // Does the next sequential level exist?
        bool has_next = false;
        for(auto lvl : levels)
        {
            if(current_level+1 == lvl)
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
            problems.push_back(std::format("Level {} has no exits.", current_level));
            return;
        }
    }
    
    // Recursively call on exits
    for(auto exit_level : exits)
    {
        get_connected_level_exits(exit_level, levels, connected, problems);
    }
}

bool LevelEditorData::saveLevel()
{
    level->grid_file = std::format("scen{}", level->world().id);

    std::string old_campaign = get_mounted_campaign();
    unpack_campaign(old_campaign);
    bool result = level->save();
    if(result)
        result = repack_campaign(get_mounted_campaign());
    cleanup_unpacked_campaign();

    // Remount for consistency in PhysFS
    (void)remount_campaign_package_with_error();

    return result;
}

void LevelEditorData::draw(screen* s)
{
    s->clearbuffer();
    level->draw(s);
    
    if(rect_selecting)
    {
        Rectf r(selection_rect.x - static_cast<float>(level->level_visuals().topx) + static_cast<float>(s->viewob[0]->xloc),
                selection_rect.y - static_cast<float>(level->level_visuals().topy) + static_cast<float>(s->viewob[0]->yloc),
                selection_rect.w, selection_rect.h);
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
        const Sint32 x1 = static_cast<Sint32>(r.x);
        const Sint32 y1 = static_cast<Sint32>(r.y);
        const Sint32 x2 = static_cast<Sint32>(r.x + r.w);
        const Sint32 y2 = static_cast<Sint32>(r.y + r.h);
        s->draw_box(x1, y1, x2, y2, ORANGE_START, 0, 1);
        eds().redraw = 1;
    }
    
    display_panel(s);
    
}

Sint32 LevelEditorData::display_panel(screen* s)
{
    text& scentext = s->text_normal;
    // Draw selection indicators
    if(mode == Mode::Select && selection.size() > 0)
    {
        for(auto& sel : selection)
        {
            // Draw cursor
            int mx, my;
            mx = sel.x - level->level_visuals().topx;
            my = sel.y - level->level_visuals().topy;

            {
                // Draw target tile
                int worldx = mx + level->level_visuals().topx;
                int worldy = my + level->level_visuals().topy;
                int screenx = worldx - level->level_visuals().topx;
                int screeny = worldy - level->level_visuals().topy;
                s->draw_box(screenx, screeny, screenx + sel.w, screeny + sel.h, dragging? ORANGE_START : YELLOW, 0, 1);
            }
        }
    }
    
    // Draw minimap
    myradar.draw(level.get());
    
    // Draw mode-specific buttons
    for(auto* btn : mode_buttons)
        btn->draw(s);
        
    if(pan_buttons.size() > 0)
    {
        Rect r(panLeftButton.area.x, panUpButton.area.y, panRightButton.area.x + panRightButton.area.w - panLeftButton.area.x, panDownButton.area.y + panDownButton.area.h - panUpButton.area.y);
        s->fastbox(r.x, r.y, r.w, r.h, 13);
        for(auto* btn : pan_buttons)
            btn->draw(s);
    }
    
	std::string message;
	Sint32 i, j; // for loops
	//   static Sint32 family=-1, hitpoints=-1, score=-1, act=-1;
	Sint32 numobs = s->living_count();
	Sint32 lm = 245;
	Sint32 curline = 0;
	Sint32 whichback;
	
	const char* blood_string;
	if(active_config().is_on("effects", "gore"))
	{
        blood_string = "BLOOD";
	}
    else
	{
        blood_string = "REMAINS";
	}
    
	const char* treasures[NUM_FAMILIES] =
	    { blood_string, "DRUMSTICK", "GOLD", "SILVER",
	      "MAGIC", "INVIS", "INVULN", "FLIGHT",
	      "EXIT", "TELEPORTER", "LIFE GEM", "KEY", "SPEED", "CC",
	    };
	const char* weapons[NUM_FAMILIES] =
	    { "KNIFE", "ROCK", "ARROW", "FIREBALL",
	      "TREE", "METEOR", "SPRINKLE", "BONE",
	      blood_string, "BLOB", "FIRE ARROW", "LIGHTNING",
	      "GLOW", "WAVE 1", "WAVE 2", "WAVE 3",
	      "PROTECTION", "HAMMER", "DOOR",
	    };

	static char livings[NUM_FAMILIES][20] = {};
	static bool livings_init = false;
	if (!livings_init) {
	    for (int i = 0; i < NUM_FAMILIES; i++) {
	        const auto* fd = get_family_descriptor(i);
	        if (fd && fd->name)
	            snprintf(livings[i], 20, "%s", fd->name);
	        else
	            snprintf(livings[i], 20, "BEAST");
	    }
	    livings_init = true;
	}

    // Info box for select mode
    if(mode == Mode::Select && selection.size() > 0)
    {
        // Draw the info box background
        s->draw_button(lm-4, L_D(-1)+4, 315, L_D(7)-2, 1, 1);
        
        if(selection.size() > 1)
            scentext.write_xy(lm, L_D(curline++), "Selected:", RED, 1);
        int sel_index = 0;
        for(auto& sel : selection)
        {
            bool showing_name = false;

            // Too many names to show?
            if(sel_index+1 == 6 && selection.size() > 6)
            {
                std::string buf = std::format("+{} more", int(selection.size()) - 5);
                scentext.write_xy(lm, L_D(curline++), buf.c_str(), DARK_BLUE, 1);
                break;  // No more
            }
            // Show name
            else if(sel.name.size() > 0 && sel.order == Order::Living)
            {
                scentext.write_xy(lm, L_D(curline++), ("\"" + sel.name + "\"").c_str(), DARK_BLUE, 1);
                showing_name = true;
            }
            else if(selection.size() == 0)
                curline++;  // Skip name line for guy with no name

            if(selection.size() == 1 || !showing_name)
            {
                // Show family name
                message = get_editor_family_label(sel.order, sel.family, livings, treasures, weapons);
                scentext.write_xy(lm, L_D(curline++), message.c_str(), DARK_BLUE, 1);
            }

            sel_index++;

            // Only show extended info for a single selection
            if(selection.size() > 1)
                continue;

            // More info for a single selection
            // Level display
            message = get_editor_level_label(sel.order, sel.family, sel.level);

            if(!message.empty())
                scentext.write_xy(lm, L_D(curline++), message.c_str(), DARK_BLUE, 1);
        }
        
    }
    
    if(mode == Mode::Object)
    {
        // Draw the bounding box
        s->draw_button(lm-4, L_D(-1)+4, 315, L_D(7)-2, 1, 1);
        
        // Get team number ..
        message = get_editor_family_label(object_brush.order, object_brush.family, livings, treasures, weapons);
        scentext.write_xy(lm, L_D(curline++), message.c_str(), DARK_BLUE, 1);

        // Level display
        message = get_editor_level_label(object_brush.order, object_brush.family, object_brush.level);

        if(!message.empty())
            scentext.write_xy(lm, L_D(curline++), message.c_str(), DARK_BLUE, 1);

        numobs = s->living_count();
        //myscreen->fastbox(lm,L_D(curline),55,7,27, 1);
        message = std::format("OB: {}", numobs);
        scentext.write_xy(lm,L_D(curline++),message.c_str(), DARK_BLUE, 1);
    }
    
    if(mode == Mode::Terrain)
    {
        // Show the current brush
        {
            auto& pix = s->level_visuals_.pixdata[terrain_brush.terrain];
            s->putbuffer(lm+25, PIX_TOP-16-1, GRID_SIZE, GRID_SIZE,
                                0, 0, 320, 200, {pix.data.get(), static_cast<size_t>(pix.w * pix.h * pix.frames)});
        }
        // Border
        s->draw_box(lm+25, PIX_TOP-16-1, lm+25+GRID_SIZE, PIX_TOP-16-1+GRID_SIZE, RED, 0, 1);
        
        // Show the background grid
        for (i=0; i < PIX_OVER; i++)
        {
            for (j=0; j < 4; j++)
            {
                whichback = (i+(j+eds().rowsdown)*4) % (sizeof(backgrounds)/4);
                {
                    auto& pix = s->level_visuals_.pixdata[ backgrounds[whichback] ];
                    s->putbuffer(S_RIGHT+i*GRID_SIZE, PIX_TOP+j*GRID_SIZE,
                                        GRID_SIZE, GRID_SIZE,
                                        0, 0, 320, 200,
                                        {pix.data.get(), static_cast<size_t>(pix.w * pix.h * pix.frames)});
                }
            }
        }
        s->draw_box(S_RIGHT, PIX_TOP,
                           S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, 0, 0, 1);
        
        #ifndef USE_TOUCH_INPUT
        
        // Draw cursor
        int mx, my;
        MouseState& mymouse = query_mouse_no_poll();
        mx = static_cast<int>(mymouse.x);
        my = static_cast<int>(mymouse.y);
        bool over_radar = (mx > s->viewob[0]->endx - myradar.xview - 4
                        && my > s->viewob[0]->endy - myradar.yview - 4
                        && mx < s->viewob[0]->endx - 4 && my < s->viewob[0]->endy - 4);
        if(!over_radar && !Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mx, my) && !mouse_on_menus(mx, my))
        {
            // Draw target tile
            int worldx = mx + level->level_visuals().topx;
            int worldy = my + level->level_visuals().topy;
            int gridx = worldx - (worldx)%GRID_SIZE;
            int gridy = worldy - (worldy)%GRID_SIZE;
            int screenx = gridx - level->level_visuals().topx;
            int screeny = gridy - level->level_visuals().topy;
            s->draw_box(screenx, screeny, screenx + GRID_SIZE, screeny + GRID_SIZE, YELLOW, 0, 1);
        }
        #endif
    }
    else if(mode == Mode::Object)
    {
        // Draw current brush
        // Background
        s->draw_box(lm+25, PIX_TOP-16-1, lm+25+GRID_SIZE, PIX_TOP-16-1+GRID_SIZE, PURE_BLACK, 1, 1);
        // Guy
        walker* newob = level->add_ob(Order::Living, FAMILY_ELF);
        newob->setxy(lm+25 + level->level_visuals().topx, PIX_TOP-16-1 + level->level_visuals().topy);
        newob->set_data(s->myloader->graphics[PIX(object_brush.order, object_brush.family)]);
        s->myloader->set_walker(newob, object_brush.order, object_brush.family);
        newob->team_num = static_cast<unsigned char>(object_brush.team);
        draw_walker_tile(*newob, s->viewob[0].get());
        // Border
        s->draw_box(lm+25, PIX_TOP-16-1, lm+25+GRID_SIZE, PIX_TOP-16-1+GRID_SIZE, RED, 0, 1);
        
        s->draw_box(S_RIGHT, PIX_TOP,
                           S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, PURE_BLACK, 1, 1);
        s->draw_box(S_RIGHT, PIX_TOP,
                           S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, WHITE, 0, 1);
        
        for (i=0; i < PIX_OVER; i++)
        {
            for (j=0; j < 4; j++)
            {
                const int pane_size = static_cast<int>(object_pane.size());
                int index = 0;
                if(pane_size > 0)
                {
                    index = (i + ((j+eds().rowsdown) * PIX_OVER)) % pane_size;
                    newob->setxy(S_RIGHT+i*GRID_SIZE + level->level_visuals().topx, PIX_TOP+j*GRID_SIZE + level->level_visuals().topy);
                    newob->set_data(s->myloader->graphics[PIX(object_pane[index].order, object_pane[index].family)]);
                    s->myloader->set_walker(newob, object_pane[index].order, object_pane[index].family);
                    newob->team_num = static_cast<unsigned char>(object_brush.team);
                    draw_walker_tile(*newob, s->viewob[0].get());
                }
            }
        }

        #ifndef USE_TOUCH_INPUT
        
        // Draw cursor
        int mx, my;
        MouseState& mymouse = query_mouse_no_poll();
        mx = static_cast<int>(mymouse.x);
        my = static_cast<int>(mymouse.y);
        bool over_radar = (mx > s->viewob[0]->endx - myradar.xview - 4
                        && my > s->viewob[0]->endy - myradar.yview - 4
                        && mx < s->viewob[0]->endx - 4 && my < s->viewob[0]->endy - 4);
        bool over_info = Rect(lm-4, L_D(-1)+4, 315 - (lm-4), L_D(7)-2 - L_D(-1)).contains(mx, my);
        if(!over_radar && !over_info && !Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mx, my) && !mouse_on_menus(mx, my))
        {
            // Prepare object sprite
            newob->setxy(mx + level->level_visuals().topx, my + level->level_visuals().topy);
            newob->set_data(s->myloader->graphics[PIX(object_brush.order, object_brush.family)]);
            s->myloader->set_walker(newob, object_brush.order, object_brush.family);
            newob->team_num = static_cast<unsigned char>(object_brush.team);
            
            // Get size rounded up to nearest GRID_SIZE
            int w = newob->sizex;
            int h = newob->sizey;
            w += GRID_SIZE - (w%GRID_SIZE == 0? GRID_SIZE : w%GRID_SIZE);
            h += GRID_SIZE - (h%GRID_SIZE == 0? GRID_SIZE : h%GRID_SIZE);
            
            // Draw target tile
            if(object_brush.snap_to_grid)
            {
                int worldx = mx + level->level_visuals().topx;
                int worldy = my + level->level_visuals().topy;
                int gridx = worldx - (worldx)%GRID_SIZE;
                int gridy = worldy - (worldy)%GRID_SIZE;
                int screenx = gridx - level->level_visuals().topx;
                int screeny = gridy - level->level_visuals().topy;
                s->draw_box(screenx, screeny, screenx + w, screeny + h, YELLOW, 0, 1);
            }
            
            // Draw current brush near cursor
            draw_walker(*newob, s->viewob[0].get());
        }
        #endif
        
        level->remove_ob(newob);
    }
    
    
    
    // Draw top menu
    for(auto* btn : menu_buttons)
        btn->draw(s);

    // Draw submenus
    for(auto& [btn, btnSet] : current_menu)
    {
        for(auto* sub_btn : btnSet)
            sub_btn->draw(s);
    }
    
    
	s->buffer_to_screen(0, 0, 320, 200);

	return 1;
}


void LevelEditorData::clear_terrain()
{
    int w = level->world().grid.w;
    int h = level->world().grid.h;
    
    std::fill_n(level->world().grid.data.get(), w*h, static_cast<unsigned char>(1));
    resmooth_terrain();
}

void LevelEditorData::resmooth_terrain()
{
    level->world().mysmoother.smooth();
    myradar.update(level.get());
}

// eds().mouse_up_button moved into LevelEditorState (per-session via eds())

void LevelEditorData::mouse_down(int mx, int my)
{
    (void)mx;
    (void)my;
    dragging = false;
}

// eds().mouse_motion_x/y, eds().mouse_last_x/y moved into LevelEditorState (per-session via eds())

void LevelEditorData::mouse_motion(int mx, int my, int dx, int dy)
{
    MouseState& mymouse = query_mouse_no_poll();
    if(mymouse.left)
    {
        if(mode == Mode::Select && !mouse_on_menus(eds().mouse_last_x, eds().mouse_last_y))
        {
            Sint32 worldx = mx + level->level_visuals().topx - og::runtime::current_session->myscreen_->viewob[0]->xloc; // - S_LEFT
            Sint32 worldy = my + level->level_visuals().topy - og::runtime::current_session->myscreen_->viewob[0]->yloc; // - S_UP
            
            walker* under_cursor = nullptr;
            if(!dragging && !rect_selecting)
            {
                // Did we start dragging a selected object?
                under_cursor = get_object(worldx, worldy);
                
                walker* got_one = nullptr;
                for(auto& sel : selection)
                {
                    if(sel.target == under_cursor)
                    {
                        got_one = under_cursor;
                        break;
                    }
                }
                under_cursor = got_one;
            }
            
            if((dragging || under_cursor != nullptr) && selection.size() > 0)
            {
                // Drag the selected objects
                dragging = true;
                for(auto& sel : selection)
                {
                    walker* w = sel.get_object(level.get());
                    if(w != nullptr)
                    {
                        w->setxy(w->xpos + dx, w->ypos + dy);

                        // Update selection position
                        sel.x = w->xpos;
                        sel.y = w->ypos;
                    }
                }
            }
            
            if(!dragging)
            {
                // Select with a rectangle
                const float worldx_f = static_cast<float>(mx + level->level_visuals().topx - og::runtime::current_session->myscreen_->viewob[0]->xloc);
                const float worldy_f = static_cast<float>(my + level->level_visuals().topy - og::runtime::current_session->myscreen_->viewob[0]->yloc);
                if(!rect_selecting)
                {
                    selection_rect.x = worldx_f;
                    selection_rect.y = worldy_f;
                    selection_rect.w = 1;
                    selection_rect.h = 1;
                    rect_selecting = true;
                }
                
                selection_rect.w = worldx_f - selection_rect.x;
                selection_rect.h = worldy_f - selection_rect.y;
                
            }
        }
    }
}

bool is_in_selection(walker* w, const std::vector<SelectionInfo>& selection)
{
    for(std::vector<SelectionInfo>::const_iterator e = selection.begin(); e != selection.end(); e++)
    {
        if(e->target == w)
            return true;
    }
    return false;
}

// Make sure to use reset_mode_buttons() after this
void add_contained_objects_to_selection(LevelRuntimeData* level, const Rectf& area, std::vector<SelectionInfo>& selection)
{
    for(auto& uptr : level->world().oblist)
	{
	    walker* w = uptr.get();
		if(w && area.contains(w->xpos + w->sizex/2, w->ypos + w->sizey/2))
		{
		    if(!is_in_selection(w, selection))
                selection.push_back(SelectionInfo(w));
		}
	}

    for(auto& uptr : level->world().fxlist)
	{
	    walker* w = uptr.get();
		if(w && area.contains(w->xpos + w->sizex/2, w->ypos + w->sizey/2))
		{
		    if(!is_in_selection(w, selection))
                selection.push_back(SelectionInfo(w));
		}
	}

    for(auto& uptr : level->world().weaplist)
	{
	    walker* w = uptr.get();
		if(w && area.contains(w->xpos + w->sizex/2, w->ypos + w->sizey/2))
		{
		    if(!is_in_selection(w, selection))
                selection.push_back(SelectionInfo(w));
		}
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
            std::set<SimpleButton*> s;
            s.insert(&fileCampaignButton);
            s.insert(&fileLevelButton);
            s.insert(&fileQuitButton);
            current_menu.push_back(std::make_pair(&fileButton, s));
        }
        // Campaign >
        else if(activate_sub_menu_button(mx, my, current_menu, fileCampaignButton))
        {
            std::set<SimpleButton*> s;
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
            if(eds().levelchanged)
            {
                cancel = !yes_or_no_prompt("Import", "Discard unsaved level changes?", false);
            }
            
            if(eds().campaignchanged)
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
            if(eds().levelchanged)
            {
                if(yes_or_no_prompt("Share", "Save level first?", false))
                {
                    if(saveLevel())
                    {
                        timed_dialog("Level saved.");
                        eds().redraw = 1;
                        eds().levelchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Save failed.");
                        eds().redraw = 1;
                        
                        cancel = true;
                    }
                }
            }
            
            if(eds().campaignchanged)
            {
                if(yes_or_no_prompt("Share", "Save campaign first?", false))
                {
                    if(saveCampaign())
                    {
                        timed_dialog("Campaign saved.");
                        eds().redraw = 1;
                        eds().campaignchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Save failed.");
                        eds().redraw = 1;
                        
                        cancel = true;
                    }
                }
            }
            
            if(!cancel)
            {
                popup_dialog("Share Campaign", "Not yet implemented.");
                shareCampaign(og::runtime::current_session->myscreen_);
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignNewButton))
        {
            // Confirm if unsaved
            bool cancel = false;
            if (eds().levelchanged)
            {
                cancel = !yes_or_no_prompt("New Campaign", "Discard unsaved changes?", false);
            }
            
            
            if(!cancel)
            {
                // Ask for campaign ID
                std::string campaign_id = "com.example.new_campaign";
                if(prompt_for_string("New Campaign", campaign_id))
                {
                    // TODO: Check if campaign already exists and prompt the user to overwrite
                    if(does_campaign_exist(campaign_id) && !yes_or_no_prompt("Overwrite?", "Overwrite existing campaign with that ID?", false))
                    {
                        cancel = true;
                    }
                    
                    if(!cancel)
                    {
                        if(create_new_campaign(campaign_id))
                        {
                            
                            // Load campaign data for the editor
                            if(loadCampaign(campaign_id))
                            {
                                // Mount new campaign
                                (void)unmount_campaign_package_with_error(get_mounted_campaign());
                                (void)mount_campaign_package_with_error(campaign_id);

                                // Load first scenario
                                std::list<int> levels = list_levels();
                                
                                if(levels.size() > 0)
                                {
                                    loadLevel(levels.front());
                                    // Update minimap
                                    myradar.start(level.get());
                                    timed_dialog("Campaign created.");
                                    eds().campaignchanged = 0;
                                    eds().levelchanged = 0;
                                }
                                else
                                {
                                    timed_dialog("Campaign has no scenarios!");
                                    eds().redraw = 1;
                                }
                            }
                            else
                            {
                                timed_dialog("Failed to load new campaign.");
                                eds().redraw = 1;
                            }
                        }
                        else
                        {
                            timed_dialog("Failed to create new campaign.");
                            eds().redraw = 1;
                        }
                    }
                }
                
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignLoadButton))
        {
            // Pick a campaign, then load it and load the first level
            eds().redraw = 1;
            bool cancel = false;
            if(eds().campaignchanged)
            {
                cancel = !yes_or_no_prompt("Load Campaign", "Discard unsaved changes?", false);
            }
            
            if(!cancel)
            {
                CampaignResult result = pick_campaign(nullptr, true);
                if(result.id.size() > 0)
                {
                    if(loadCampaign(result.id))
                    {
                        (void)unmount_campaign_package_with_error(get_mounted_campaign());
                        (void)mount_campaign_package_with_error(result.id);
                        eds().campaignchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Failed to load campaign.");
                        cancel = true;
                    }
                    
                    if(!cancel)
                    {
                        og::runtime::current_session->myscreen_->clearbuffer();
                        // Prompt to load starting level.  If we don't, then the user can transfer levels between campaigns here.
                        bool load_first_level = yes_or_no_prompt("Load Campaign", "Load first level?", false);
                        if(load_first_level && eds().levelchanged)
                        {
                            load_first_level = yes_or_no_prompt("Load Level", "Discard unsaved changes?", false);
                        }
                        
                        if(load_first_level)
                        {
                            // Load first scenario
                            if(loadLevel(result.first_level))
                            {
                                // Update minimap
                                myradar.start(level.get());
                                timed_dialog("Campaign loaded.");
                                eds().levelchanged = 0;
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
                eds().campaignchanged = 0;
                eds().redraw = 1;
            }
            else
            {
                timed_dialog("Failed to save campaign.");
                eds().redraw = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileCampaignSaveAsButton))
        {
            CampaignResult result = pick_campaign(nullptr, true);
            if(result.id.size() > 0)
            {
                std::list<std::string> campaigns = list_campaigns();
                if(std::find(campaigns.begin(), campaigns.end(), result.id) == campaigns.end()
                    || yes_or_no_prompt("Overwrite", "Overwrite existing campaign?", false))
                {
                    if(saveCampaignAs(result.id))
                    {
                        timed_dialog("Campaign saved.");
                        eds().campaignchanged = 0;
                        eds().redraw = 1;
                    }
                    else
                    {
                        timed_dialog("Failed to save campaign.");
                        eds().redraw = 1;
                    }
                }
            }
        }
        // Level >
        else if(activate_sub_menu_button(mx, my, current_menu, fileLevelButton))
        {
            std::set<SimpleButton*> s;
            s.insert(&fileLevelNewButton);
            s.insert(&fileLevelLoadButton);
            s.insert(&fileLevelSaveButton);
            s.insert(&fileLevelSaveAsButton);
            current_menu.push_back(std::make_pair(&fileLevelButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelNewButton))
        {
            // Confirm if unsaved
            bool cancel = false;
            if (eds().levelchanged)
            {
                cancel = !yes_or_no_prompt("Load Level", "Discard unsaved changes?", false);
            }
            
            if(!cancel)
            {
                // New level
                level->clear();
                level->create_new_grid();
                myradar.start(level.get());
                eds().levelchanged = 1;
                eds().redraw = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelLoadButton))
        {
            // Confirm if unsaved
            bool cancel = false;
            if (eds().levelchanged)
            {
                cancel = !yes_or_no_prompt("Load Level", "Discard unsaved changes?", false);
            }
            
            if(!cancel)
            {
                // Browse for the level to load
                int id = pick_level(og::runtime::current_session->myscreen_, level->world().id, true);
                // Don't bother loading the level if it is the same, unchanged level
                if(id >= 0 && (eds().levelchanged || id != level->world().id))
                {
                    if(loadLevel(id))
                    {
                        timed_dialog("Level loaded.");
                        eds().levelchanged = 0;
                        eds().redraw = 1;
                    }
                    else
                    {
                        timed_dialog("Failed to load level.");
                        eds().redraw = 1;
                    }
                    
                    myradar.start(level.get());
                    eds().redraw = 1;
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelSaveButton))
        {
            if(saveLevel())
            {
                timed_dialog("Level saved.");
                eds().redraw = 1;
                eds().levelchanged = 0;
            }
            else
            {
                timed_dialog("Save failed.");
                eds().redraw = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileLevelSaveAsButton))
        {
            int id = pick_level(og::runtime::current_session->myscreen_, level->world().id, true);
            
            if(id >= 0 && id != level->world().id)
            {
                std::list<int> levels = list_levels();
                if(std::find(levels.begin(), levels.end(), id) == levels.end()
                    || yes_or_no_prompt("Overwrite", "Overwrite existing level?", false))
                {
                    if(saveLevelAs(id))
                    {
                        timed_dialog("Level saved.");
                        eds().redraw = 1;
                        eds().levelchanged = 0;
                    }
                    else
                    {
                        timed_dialog("Save failed.");
                        eds().redraw = 1;
                    }
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, fileQuitButton))
        {
            if((!eds().levelchanged && !eds().campaignchanged)
                || yes_or_no_prompt("Exit", "Quit without saving?", false))
            {
                done = true;
            }
        }
        // CAMPAIGN
        else if(activate_sub_menu_button(mx, my, current_menu, campaignButton, true))
        {
            std::set<SimpleButton*> s;
            s.insert(&campaignInfoButton);
            s.insert(&campaignProfileButton);
            s.insert(&campaignDetailsButton);
            s.insert(&campaignValidateButton);
            current_menu.push_back(std::make_pair(&campaignButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, campaignInfoButton))
        {
            std::string buf = std::format("{}\nID: {}\nTitle: {}\nVersion: {}\nAuthors: {}\nContributors: {}\nSugg. Power: {}\nFirst level: {}",
                        (eds().campaignchanged? "(unsaved)" : ""), campaign->id, campaign->title, campaign->version, campaign->authors, campaign->contributors, campaign->suggested_power, campaign->first_level);
            popup_dialog("Campaign Info", buf.c_str());
        }
        // Profile >
        else if(activate_sub_menu_button(mx, my, current_menu, campaignProfileButton))
        {
            std::set<SimpleButton*> s;
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
            if(prompt_for_string("Campaign Title", title))
            {
                campaign->title = title;
                eds().campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileDescriptionButton))
        {
            std::list<std::string> desc = campaign->description;
            if(prompt_for_string_block("Campaign Description", desc))
            {
                campaign->description = desc;
                eds().campaignchanged = 1;
            }
            eds().redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileIconButton))
        {
            popup_dialog("Edit Icon", "Not yet implemented.");
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileAuthorsButton))
        {
            std::string authors = campaign->authors;
            if(prompt_for_string("Campaign Authors", authors))
            {
                campaign->authors = authors;
                eds().campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignProfileContributorsButton))
        {
            std::string contributors = campaign->contributors;
            if(prompt_for_string("Campaign Contributors", contributors))
            {
                campaign->contributors = contributors;
                eds().campaignchanged = 1;
            }
        }
        // Details >
        else if(activate_sub_menu_button(mx, my, current_menu, campaignDetailsButton))
        {
            std::set<SimpleButton*> s;
            s.insert(&campaignDetailsVersionButton);
            s.insert(&campaignDetailsSuggestedPowerButton);
            s.insert(&campaignDetailsFirstLevelButton);
            current_menu.push_back(std::make_pair(&campaignDetailsButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, campaignDetailsVersionButton))
        {
            std::string version = campaign->version;
            if(prompt_for_string("Campaign Version", version))
            {
                campaign->version = version;
                eds().campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignDetailsSuggestedPowerButton))
        {
            std::string power = std::format("{}", campaign->suggested_power);
            if(prompt_for_string("Suggested Power", power))
            {
                campaign->suggested_power = toInt(power);
                eds().campaignchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, campaignDetailsFirstLevelButton))
        {
            std::string level_str = std::format("{}", campaign->first_level);
            if(prompt_for_string("First Level", level_str))
            {
                campaign->first_level = toInt(level_str);
                eds().campaignchanged = 1;
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
            
            for(auto lvl : levels)
            {
                if(connected.find(lvl) == connected.end())
                {
                    problems.push_back(std::format("Level {} is not connected.", lvl));
                }
            }
            
            // Get ready to show the user the problems
            std::string buf;
            if(problems.size() == 0)
            {
                buf = "No problems!";
            }
            else
            {
                // Only show the first 6 problems and "More problems..."
                if(problems.size() > 6)
                {
                    int num_over = static_cast<int>(problems.size()) - 6;
                    while(problems.size() > 6)
                        problems.pop_back();
                    problems.push_back(std::format("{} more problems...", num_over));
                }

                // Put all the problems together for the printer
                for(auto& prob : problems)
                {
                    buf += prob;
                    buf += "\n";
                }
            }

            // Show user the problems
            popup_dialog("Validate Campaign", buf.c_str());
        }
        // LEVEL
        else if(activate_sub_menu_button(mx, my, current_menu, levelButton, true))
        {
            std::set<SimpleButton*> s;
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
            std::string buf = std::format("{}\nID number: {}\nTitle: {}\nSize: {}x{}",
                     (eds().levelchanged? "(unsaved)" : ""), level->world().id, level->world().title, level->world().grid.w, level->world().grid.h);
            popup_dialog("Level Info", buf.c_str());
        }
        // Profile >
        else if(activate_sub_menu_button(mx, my, current_menu, levelProfileButton))
        {
            std::set<SimpleButton*> s;
            s.insert(&levelProfileTitleButton);
            s.insert(&levelProfileDescriptionButton);
            current_menu.push_back(std::make_pair(&levelProfileButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, levelProfileTitleButton))
        {
            std::string title = level->world().title;
            if(prompt_for_string("Level Title", title))
            {
                level->world().title = title;
                eds().levelchanged = 1;
            }
        }
        else if(activate_menu_choice(mx, my, *this, levelProfileDescriptionButton))
        {
            std::list<std::string> desc = level->description;
            if(prompt_for_string_block("Level Description", desc))
            {
                level->description = desc;
                eds().levelchanged = 1;
            }
            eds().redraw = 1;
        }
        // Details >
        else if(activate_sub_menu_button(mx, my, current_menu, levelDetailsButton))
        {
            std::set<SimpleButton*> s;
            s.insert(&levelDetailsMapSizeButton);
            s.insert(&levelDetailsParValueButton);
            s.insert(&levelDetailsTimeLimitButton);
            current_menu.push_back(std::make_pair(&levelDetailsButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, levelDetailsMapSizeButton))
        {
            // Using two prompts sequentially
            
            std::string width = std::format("{}", level->world().grid.w);
            std::string height = std::format("{}", level->world().grid.h);
            
            if(prompt_for_string("Map Width", width))
            {
                int w = toInt(width);
                int h;
                
                #ifdef ANDROID
                // The soft keyboard on Android might take a little while to be ready again, so opening it right away doesn't always work.
                SDL_Delay(1000);
                #endif
                if(prompt_for_string( "Map Height", height))
                {
                    h = toInt(height);
                    
                    // Validate here so we can tell the user
                    // Size is limited to one byte in the file format
                    if(w < 3 || h < 3 || w > 255 || h > 255)
                    {
                        std::string errmsg = std::string("Can't resize grid to ") + std::to_string(w) + "x" + std::to_string(h) + "\n";
                        if(w < 3)
                            errmsg += "Width is too small.\n";
                        if(h < 3)
                            errmsg += "Height is too small.\n";
                        if(w > 255)
                            errmsg += "Width is too big (max 255).\n";
                        if(h > 255)
                            errmsg += "Height is too big (max 255).\n";

                        popup_dialog("Resize Map", errmsg.c_str());
                    }
                    else
                    {
                        if((w >= level->world().grid.w && h >= level->world().grid.h)
                            || !are_objects_outside_area(level.get(), 0, 0, w, h)
                            || yes_or_no_prompt("Resize Map", "Delete objects outside of map?", false))
                        {
                            // Now change it
                            level->resize_grid(w, h);
                            
                            // Reset the minimap
                            myradar.start(level.get());
                            
                            draw(og::runtime::current_session->myscreen_);
                            og::runtime::current_session->myscreen_->refresh();
                            
                            std::string resize_msg = std::format("Resized map to {}x{}", level->world().grid.w, level->world().grid.h);
                            timed_dialog(resize_msg.c_str());
                            eds().redraw = 1;
                            eds().levelchanged = 1;
                        }
                        else
                        {
                            timed_dialog("Resize canceled.");
                            eds().redraw = 1;
                        }
                    }
                }
                else
                {
                    timed_dialog("Resize canceled.");
                    eds().redraw = 1;
                }
            }
            else
            {
                timed_dialog("Resize canceled.");
                eds().redraw = 1;
            }
        }
        // Goals >
        else if(activate_sub_menu_button(mx, my, current_menu, levelGoalsButton))
        {
            std::set<SimpleButton*> s;
            s.insert(&levelGoalsEnemiesButton);
            s.insert(&levelGoalsGeneratorsButton);
            s.insert(&levelGoalsNPCsButton);
            current_menu.push_back(std::make_pair(&levelGoalsButton, s));
        }
        else if(activate_menu_toggle_choice(mx, my, *this, levelGoalsEnemiesButton))
        {
            level->world().type ^= GameWorld::TYPE_CAN_EXIT_WHENEVER;
            update_menu_buttons();
        }
        else if(activate_menu_toggle_choice(mx, my, *this, levelGoalsGeneratorsButton))
        {
            level->world().type ^= GameWorld::TYPE_MUST_DESTROY_GENERATORS;
            update_menu_buttons();
        }
        else if(activate_menu_toggle_choice(mx, my, *this, levelGoalsNPCsButton))
        {
            level->world().type ^= GameWorld::TYPE_MUST_PROTECT_NAMED_NPCS;
            update_menu_buttons();
        }
        else if(activate_menu_choice(mx, my, *this, levelDetailsParValueButton))
        {
            std::string par = std::format("{}", level->world().par_value);
            if(prompt_for_string("Par Value (num)", par))
            {
                int v = toInt(par);
                if(v > 0)
                {
                    level->world().par_value = static_cast<short>(v);
                    eds().levelchanged = 1;
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, levelDetailsTimeLimitButton))
        {
            std::string par = std::format("{}", level->world().time_bonus_limit);
            if(prompt_for_string("Time Bonus Limit (num)", par))
            {
                int v = toInt(par);
                if(v > 0)
                {
                    level->world().time_bonus_limit = static_cast<short>(v);
                    eds().levelchanged = 1;
                }
            }
        }
        else if(activate_menu_choice(mx, my, *this, levelResmoothButton))
        {
            resmooth_terrain();
            eds().levelchanged = 1;
            eds().redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, levelDeleteTerrainButton))
        {
            if(yes_or_no_prompt("Clear Terrain", "Delete all terrain?", false))
            {
                clear_terrain();
                myradar.update(level.get());
                eds().levelchanged = 1;
            }
            eds().redraw = 1;
        }
        else if(activate_menu_choice(mx, my, *this, levelDeleteObjectsButton))
        {
            if(yes_or_no_prompt("Clear Objects", "Delete all objects?", false))
            {
                level->delete_objects();
                myradar.update(level.get());
                eds().levelchanged = 1;
            }
            eds().redraw = 1;
        }
        // MODE
        else if(activate_sub_menu_button(mx, my, current_menu, modeButton, true))
        {
            std::set<SimpleButton*> s;
            s.insert(&modeTerrainButton);
            s.insert(&modeObjectButton);
            s.insert(&modeSelectButton);
            current_menu.push_back(std::make_pair(&modeButton, s));
        }
        else if(activate_menu_choice(mx, my, *this, modeTerrainButton))
        {
            mode = Mode::Terrain;
            modeButton.label = "Edit (Terrain)";
            reset_mode_buttons();
        }
        else if(activate_menu_choice(mx, my, *this, modeObjectButton))
        {
            mode = Mode::Object;
            modeButton.label = "Edit (Objects)";
            reset_mode_buttons();
        }
        else if(activate_menu_choice(mx, my, *this, modeSelectButton))
        {
            mode = Mode::Select;
            modeButton.label = "Edit (Select)";
            reset_mode_buttons();
        }
        else
        {
            // Check mode-specific buttons
            for(auto* btn : mode_buttons)
            {
                if(btn->contains(mx, my))
                {
                    activate_mode_button(btn);
                    eds().redraw = 1;
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
        if ((mode != Mode::Select || (!rect_selecting && !dragging)) && mx > og::runtime::current_session->myscreen_->viewob[0]->endx - myradar.xview - 4
                && my > og::runtime::current_session->myscreen_->viewob[0]->endy - myradar.yview - 4
                && mx < og::runtime::current_session->myscreen_->viewob[0]->endx - 4 && my < og::runtime::current_session->myscreen_->viewob[0]->endy - 4)
        {
            // Radar clicking is done by holding (in the level_editor function
        }
        else  // in the main window
        {
            Sint32 windowx = mx + level->level_visuals().topx - og::runtime::current_session->myscreen_->viewob[0]->xloc; // - S_LEFT
            Sint32 windowy = my + level->level_visuals().topy - og::runtime::current_session->myscreen_->viewob[0]->yloc; // - S_UP
            if (object_brush.snap_to_grid)
            {
                windowx -= (windowx%GRID_SIZE);
                windowy -= (windowy%GRID_SIZE);
            }

            if (mode == Mode::Select)
            {
                walker* newob = nullptr;
                
                if(rect_selecting && (fabs(selection_rect.w) > 15 || fabs(selection_rect.h > 15)))
                {
                    rect_selecting = false;
                    
                    // Select guys in the rectangle
                    if(!og::runtime::current_session->keystates_[KEYSTATE_LCTRL] && !og::runtime::current_session->keystates_[KEYSTATE_RCTRL])
                        selection.clear();
                    add_contained_objects_to_selection(level.get(), selection_rect, selection);
                    reset_mode_buttons();
                }
                else if (og::runtime::current_session->keystates_[KEYSTATE_r]) // (re)name the current object
                {
                    newob = level->add_ob(Order::Living, FAMILY_ELF);
                    newob->setxy(windowx, windowy);
                    if (some_hit(windowx, windowy, newob, level.get()))
                    {
                        std::string name = newob->collide_ob->stats()->name;
                        if(prompt_for_string("Rename", name))
                        {
                            newob->collide_ob->stats()->name = name;
                            eds().levelchanged = 1;
                        }
                    }
                    level->remove_ob(newob);
                }
                else // select this object
                {
                    rect_selecting = false;
                    if(mx < 245-4 || my > L_D(7)-2)
                    {
                        newob = level->add_ob(Order::Living, FAMILY_ELF);
                        newob->setxy(windowx, windowy);
                        if (some_hit(windowx, windowy, newob, level.get()))
                        {
                            // Clicked on a guy
                            walker* w = newob->collide_ob;
                            if(og::runtime::current_session->keystates_[KEYSTATE_LCTRL] || og::runtime::current_session->keystates_[KEYSTATE_RCTRL])
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
                        else if(!(og::runtime::current_session->keystates_[KEYSTATE_LCTRL] || og::runtime::current_session->keystates_[KEYSTATE_RCTRL]))
                            selection.clear();  // Deselect if not trying to grab more
                        
                        level->remove_ob(newob);

                        reset_mode_buttons();
                    }
                }  // end of info mode
            }
            else if (mode == Mode::Object)
            {
                if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
                {
                    //windowx = (mx - PIX_LEFT) / GRID_SIZE;
                    windowx = (mx-S_RIGHT) / GRID_SIZE;
                    windowy = (my - PIX_TOP) / GRID_SIZE;
                    const int pane_size = static_cast<int>(object_pane.size());
                    if(pane_size > 0)
                    {
                        const int index = (windowx + ((windowy+eds().rowsdown) * PIX_OVER)) % pane_size;
                        object_brush.order = object_pane[index].order;
                        object_brush.family = object_pane[index].family;
                    }
                } // end of background grid window
                else if(mx < 245-4 || my > L_D(7)-2)
                {
                    walker* newob = nullptr;
                    if(!object_brush.picking)
                    {
                        // Create new object here (apply brush)
                        eds().levelchanged = 1;
                        newob = level->add_ob(object_brush.order, object_brush.family);
                        newob->setxy(windowx, windowy);
                        newob->team_num = static_cast<unsigned char>(object_brush.team);
                        newob->stats()->level = object_brush.level;
                        newob->dead = 0; // just in case
                        newob->collide_ob = nullptr;
                        // Is there already something there?
                        if ( object_brush.snap_to_grid && some_hit(windowx, windowy, newob, level.get()))
                        {
                            if (newob)
                            {
                                level->remove_ob(newob);
                                newob = nullptr;
                            }
                        }  // end of failure to put guy
                        else if(!object_brush.snap_to_grid)
                        {
                            draw_walker(*newob, og::runtime::current_session->myscreen_->viewob[0].get());
                            og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
                            eds().start_time_s = query_timer();
                            MouseState& mymouse = query_mouse_no_poll();
                            while ( mymouse.left && (query_timer()-eds().start_time_s) < 36 )
                            {
                                SDL_Delay(1);
                                mymouse = query_mouse();
                            }
                            eds().levelchanged = 1;
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
            if (mode == Mode::Terrain)
            {
                if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
                {
                    //windowx = (mx - PIX_LEFT) / GRID_SIZE;
                    windowx = (mx-S_RIGHT) / GRID_SIZE;
                    windowy = (my - PIX_TOP) / GRID_SIZE;
                    terrain_brush.terrain = backgrounds[ (windowx + ((windowy+eds().rowsdown) * PIX_OVER))
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
    Sint32 windowx = mx + level->level_visuals().topx - og::runtime::current_session->myscreen_->viewob[0]->xloc; // - S_LEFT
    Sint32 windowy = my + level->level_visuals().topy - og::runtime::current_session->myscreen_->viewob[0]->yloc; // - S_UP
    
    // Set brush to the grid tile
    if(mode == Mode::Terrain)
    {
        // Snap to grid
        windowx -= (windowx%GRID_SIZE);
        windowy -= (windowy%GRID_SIZE);
        
        // Reduce to array dims
        windowx /= GRID_SIZE;
        windowy /= GRID_SIZE;
        
        // Get tile from grid array
        if(is_in_grid(windowx, windowy))
            terrain_brush.terrain = get_terrain(windowx, windowy);
    }
    else if(mode == Mode::Object)
    {
        // Snap to grid
        if (object_brush.snap_to_grid)
        {
            windowx -= (windowx%GRID_SIZE);
            windowy -= (windowy%GRID_SIZE);
        }
        
        // Get object from level
        walker* w = get_object(windowx, windowy);
        if(w != nullptr)
        {
            object_brush.set(w);
        }
    }
}


bool LevelEditorData::is_in_grid(int x, int y)
{
    return (x >= 0 && y >= 0 && x < level->world().grid.w && y < level->world().grid.h);
}

unsigned char LevelEditorData::get_terrain(int x, int y)
{
    if(!is_in_grid(x, y))
        return 0;
    
    return level->world().grid.data[y*level->world().grid.w + x];
}

void LevelEditorData::set_terrain(int x, int y, unsigned char terrain)
{
    if(!is_in_grid(x, y))
        return;
    
    level->world().grid.data[y*level->world().grid.w + x] = terrain;
}

walker* LevelEditorData::get_object(int x, int y)
{
    walker* result = nullptr;
    walker* newob = level->add_ob(Order::Living, FAMILY_ELF);
    newob->setxy(x, y);
    if (some_hit(x, y, newob, level.get()))
    {
        result = newob->collide_ob;
    }
    level->remove_ob(newob);
    return result;
}

#ifdef TESTING
int level_editor_test_exercise_internal_helpers()
{
    int score = 0;

    Rect r;
    if (!r.contains(1, 1))
        score++;

    Rectf rf_pos(10.0f, 10.0f, 5.0f, 5.0f);
    Rectf rf_neg(10.0f, 10.0f, -5.0f, -5.0f);
    Rectf rf_neg_w(10.0f, 10.0f, -5.0f, 5.0f);
    Rectf rf_neg_h(10.0f, 10.0f, 5.0f, -5.0f);
    if (rf_pos.contains(12.0f, 12.0f))
        score++;
    if (rf_neg.contains(8.0f, 8.0f))
        score++;
    if (rf_neg_w.contains(8.0f, 12.0f))
        score++;
    if (rf_neg_h.contains(12.0f, 8.0f))
        score++;
    SimpleButton btn("X", 0, 0, 20, 10);
    if (btn.contains(1, 1))
        score++;
    btn.set_colors_disabled();
    btn.set_colors_active();

    EditorObjectBrush brush;
    brush.set(nullptr);
    if (brush.order == Order::Living && brush.family == 0)
        score++;

    LevelEditorData data;
    if (data.level != nullptr)
    {
        data.level->create_new_grid();
        // Avoid smoother/radar update paths in this helper; those are exercised elsewhere
        // and have global UI dependencies that make this test nondeterministic.
        std::fill_n(data.level->world().grid.data.get(),
                    data.level->world().grid.w * data.level->world().grid.h,
                    static_cast<unsigned char>(1));
        data.set_terrain(0, 0, PIX_GRASS2);
        if (data.get_terrain(0, 0) == PIX_GRASS2)
            score++;
        if (data.get_terrain(-1, -1) == 0)
            score++;

        walker* inside = data.level->add_ob(Order::Living, FAMILY_SOLDIER);
        if (inside != nullptr)
        {
            inside->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            SelectionInfo sel(inside);
            if (sel.get_object(data.level.get()) == inside)
                score++;
            std::vector<SelectionInfo> selection;
            Rectf area(static_cast<float>(inside->xpos - 2),
                       static_cast<float>(inside->ypos - 2),
                       static_cast<float>(inside->sizex + 4),
                       static_cast<float>(inside->sizey + 4));
            add_contained_objects_to_selection(data.level.get(), area, selection);
            if (is_in_selection(inside, selection))
                score++;
            if (data.get_object(inside->xpos, inside->ypos) == inside)
                score++;
        }

        data.mode = Mode::Terrain;
        data.reset_mode_buttons();
        if (data.mode_buttons.find(&data.terrainSmoothButton) != data.mode_buttons.end())
            score++;
        data.activate_mode_button(&data.pickerButton);
        if (data.terrain_brush.picking)
            score++;

        data.mode = Mode::Object;
        data.object_brush.snap_to_grid = true;
        data.reset_mode_buttons();
        data.activate_mode_button(&data.gridSnapButton);
        if (!data.object_brush.snap_to_grid)
            score++;
    }

    return score;
}
#endif

std::string get_editor_family_label(Order order, Sint32 family, char livings[][20], const char* treasures[], const char* weapons[])
{
    if(family < 0 || family >= NUM_FAMILIES)
        return "UNKNOWN";

    if (order == Order::Living)
        return livings[family];
    if (order == Order::Generator)
    {
        switch (family)
        {
            case FAMILY_TENT: return "TENT";
            case FAMILY_TOWER: return "MAGE TOWER";
            case FAMILY_BONES: return "BONEPILE";
            case FAMILY_TREEHOUSE: return "TREEHOUSE";
            default: return "GENERATOR";
        }
    }
    if (order == Order::Special)
        return "START TILE";
    if (order == Order::Treasure)
        return treasures[family];
    if (order == Order::Weapon)
        return weapons[family];
    return "UNKNOWN";
}

std::string get_editor_level_label(Order order, Sint32 family, Sint32 level)
{
    switch (order)
    {
        case Order::Living:
        case Order::Generator:
            return std::format("LEVEL: {}", level);
        case Order::Treasure:
            if (family == FAMILY_GOLD_BAR || family == FAMILY_SILVER_BAR)
                return std::format("VALUE: {}", level);
            if (family == FAMILY_KEY)
                return std::format("DOOR ID: {}", level);
            if (family == FAMILY_TELEPORTER)
                return std::format("GROUP: {}", level);
            if (family == FAMILY_EXIT)
                return std::format("EXIT TO: {}", level);
            if (family != FAMILY_STAIN)
                return std::format("POWER: {}", level);
            return "";
        case Order::Weapon:
            if (family == FAMILY_DOOR)
                return std::format("DOOR ID: {}", level);
            return std::format("POWER: {}", level);
        default:
            return "";
    }
}

enum class EventType { Handled, Text, Scroll, MouseMotion, MouseDown, MouseUp, KeyDown };

EventType handle_basic_editor_event(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_WINDOWEVENT:   
        handle_window_event(event);
        return EventType::Handled;
    case SDL_TEXTINPUT:
        handle_text_event(event);
        return EventType::Text;
    case SDL_MOUSEWHEEL:
        handle_mouse_event(event);
        return EventType::Scroll;
    case SDL_FINGERMOTION:
        handle_mouse_event(event);
        eds().mouse_motion_x = static_cast<int>(event.tfinger.dx * 320.0f);
        eds().mouse_motion_y = static_cast<int>(event.tfinger.dy * 200.0f);
        return EventType::MouseMotion;
    case SDL_FINGERUP:
        {
            MouseState& mymouse = query_mouse_no_poll();
            int left_state = mymouse.left;
            int right_state = mymouse.right;
            handle_mouse_event(event);
            if(left_state != mymouse.left)
                eds().mouse_up_button = MOUSE_LEFT;
            else if(right_state != mymouse.right)
                eds().mouse_up_button = MOUSE_RIGHT;
            else
                eds().mouse_up_button = 0;
        }
        return EventType::MouseUp;
    case SDL_FINGERDOWN:
        handle_mouse_event(event);
        return EventType::MouseDown;
    case SDL_KEYDOWN:
        handle_key_event(event);
        return EventType::KeyDown;
    case SDL_KEYUP:
        handle_key_event(event);
        return EventType::Handled;
    case SDL_MOUSEMOTION:
        handle_mouse_event(event);
        eds().mouse_motion_x = static_cast<int>(static_cast<float>(event.motion.xrel) * (320.0f / og::runtime::current_session->viewport_w_));
        eds().mouse_motion_y = static_cast<int>(static_cast<float>(event.motion.yrel) * (200.0f / og::runtime::current_session->viewport_h_));
        return EventType::MouseMotion;
    case SDL_MOUSEBUTTONUP:
        {
            MouseState& mymouse = query_mouse_no_poll();
            int left_state = mymouse.left;
            int right_state = mymouse.right;
            handle_mouse_event(event);
            if(left_state != mymouse.left)
                eds().mouse_up_button = MOUSE_LEFT;
            else if(right_state != mymouse.right)
                eds().mouse_up_button = MOUSE_RIGHT;
            else
                eds().mouse_up_button = 0;
        }
        return EventType::MouseUp;
    case SDL_MOUSEBUTTONDOWN:
        handle_mouse_event(event);
        return EventType::MouseDown;
    case SDL_JOYAXISMOTION:
        handle_joy_event(event);
        return EventType::Handled;
    case SDL_JOYBUTTONDOWN:
        handle_joy_event(event);
        return EventType::Handled;
    case SDL_JOYBUTTONUP:
        handle_joy_event(event);
        return EventType::Handled;
    case SDL_QUIT:
        quit(0);
        return EventType::Handled;
    default:
        return EventType::Handled;
    }
}

#define PAN_LIMIT_UP -60
#define PAN_LIMIT_DOWN (GRID_SIZE*data.level->world().grid.h - 200 + 80)
#define PAN_LIMIT_LEFT -60
#define PAN_LIMIT_RIGHT (GRID_SIZE*data.level->world().grid.w - 320 + 80)

// eds().pan_left/right/up/down moved into LevelEditorState (per-session via eds())

Sint32 level_editor()
{
    static LevelEditorData data;
    EditorTerrainBrush& terrain_brush = data.terrain_brush;
    EditorObjectBrush& object_brush = data.object_brush;
    
    Mode& mode = data.mode;
    radar& myradar = data.myradar;
    
	Sint32 i,j;
	Sint32 windowx, windowy;
	Sint32 mx, my;
    
    // Initialize palette for cycling
    load_and_set_palette("our.pal", eds().scenpalette);
    eds().maxrows = ((sizeof(backgrounds)/4) / 4);
    
    if(data.reloadCampaign())
        Log("Loaded campaign data successfully.\n");
    else
        Log("Failed to load campaign data.\n");
    
    std::string old_campaign = get_mounted_campaign();
    if(old_campaign.size() > 0)
        (void)unmount_campaign_package_with_error(old_campaign);
    (void)mount_campaign_package_with_error(data.campaign->id);
    

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
    {
        Log("Campaign has no valid levels!\n");
    }

	eds().redraw = 1;  // Redraw right away
	
	object_pane.clear();
	for(int family_idx = 0; family_idx < NUM_FAMILIES; family_idx++)
    {
        object_pane.push_back(ObjectType(Order::Living, static_cast<unsigned char>(family_idx)));
    }
	for(int treasure_idx = 0; treasure_idx < MAX_TREASURE+1; treasure_idx++)
    {
        object_pane.push_back(ObjectType(Order::Treasure, static_cast<unsigned char>(treasure_idx)));
    }
	for(int gen_idx = 0; gen_idx < 4; gen_idx++)
    {
        object_pane.push_back(ObjectType(Order::Generator, static_cast<unsigned char>(gen_idx)));
    }
    
    object_pane.push_back(ObjectType(Order::Weapon, FAMILY_DOOR));
    object_pane.push_back(ObjectType(Order::Special, FAMILY_RESERVED_TEAM));
	
	// Minimap
		myradar.start(data.level.get());
	
	// GUI
	using std::set;
	using std::pair;
	using std::list;
	
	data.reset_mode_buttons();
	
    MouseState& mymouse = query_mouse_no_poll();
    
    #ifdef USE_CONTROLLER_INPUT
    mymouse.x = 160;
    mymouse.y = 100;
    #endif
    
    eds().mouse_last_x = static_cast<int>(mymouse.x);
    eds().mouse_last_y = static_cast<int>(mymouse.y);
    
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

		if (og::runtime::current_session->myscreen_->end)
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
                event.button.x = mymouse.x * (og::runtime::current_session->viewport_w_ / 320) + og::runtime::current_session->viewport_offset_x_;
                event.button.y = mymouse.y * (og::runtime::current_session->viewport_h_ / 200) + og::runtime::current_session->viewport_offset_y_;
                SDL_PushEvent(&event);
                continue;
            }
            if(didPlayerReleaseKey(0, KEY_FIRE, event))
            {
                // Send fake mouse up event
                SDL_Event event;
                
                event.type = SDL_MOUSEBUTTONUP;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.x = mymouse.x * (og::runtime::current_session->viewport_w_ / 320) + og::runtime::current_session->viewport_offset_x_;
                event.button.y = mymouse.y * (og::runtime::current_session->viewport_h_ / 200) + og::runtime::current_session->viewport_offset_y_;
                SDL_PushEvent(&event);
                continue;
            }
            #endif
            switch(handle_basic_editor_event(event))
            {
            case EventType::MouseMotion:
                data.mouse_motion(static_cast<int>(mymouse.x), static_cast<int>(mymouse.y), eds().mouse_motion_x, eds().mouse_motion_y);
                break;
            case EventType::MouseDown:
                if(mymouse.left)
                {
                    eds().mouse_last_x = static_cast<int>(mymouse.x);
                    eds().mouse_last_y = static_cast<int>(mymouse.y);
                    
                    data.mouse_down(static_cast<int>(mymouse.x), static_cast<int>(mymouse.y));
                }
                break;
            case EventType::MouseUp:
                
                if(eds().mouse_up_button == MOUSE_LEFT)
                {
                    data.mouse_up(static_cast<int>(mymouse.x), static_cast<int>(mymouse.y), eds().mouse_last_x, eds().mouse_last_y, done);
                    eds().redraw = 1;
                }
                else if(eds().mouse_up_button == MOUSE_RIGHT)
                {
                    // Picking with right mouse button
                    data.pick_by_mouse(static_cast<int>(mymouse.x), static_cast<int>(mymouse.y));
                    eds().redraw = 1;
                }
                break;
            case EventType::KeyDown:
                eds().redraw = 1;
                if(event.key.keysym.sym == SDLK_ESCAPE)
                {
                    if((!eds().levelchanged && !eds().campaignchanged)
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
                    if(mode == Mode::Object || mode == Mode::Select)
                        data.activate_mode_button(&data.gridSnapButton);
                }
                // Save scenario
                else if(event.key.keysym.sym == SDLK_s && (event.key.keysym.mod & KMOD_CTRL))
                {
                    bool saved = false;
                    if(eds().levelchanged)
                    {
                        if(data.saveLevel())
                        {
                            eds().levelchanged = 0;
                            saved = true;
                        }
                        else
                            timed_dialog("Failed to save level.");
                    }
                    if(eds().campaignchanged)
                    {
                        if(data.saveCampaign())
                        {
                            eds().campaignchanged = 0;
                            saved = true;
                        }
                        else
                            timed_dialog("Failed to save campaign.");
                    }
                    
                    if(saved)
                        timed_dialog("Saved.");
                    else if(!eds().levelchanged && !eds().campaignchanged)
                        timed_dialog("No changes to save.");
                }  // end of saving routines

                // Change level of current guy being placed ..
                else if(event.key.keysym.sym == SDLK_RIGHTBRACKET)
                {
                    if(mode == Mode::Object)
                        object_brush.level++;
                }
                else if(event.key.keysym.sym == SDLK_LEFTBRACKET)
                {
                    if(mode == Mode::Object && object_brush.level > 1)
                        object_brush.level--;
                }
                else if(event.key.keysym.sym == SDLK_DELETE)
                {
                    if(mode == Mode::Select)
                        data.activate_mode_button(&data.deleteButton);
                }
                else if(event.key.keysym.sym == SDLK_o)
                {
                    if(mode == Mode::Object)
                    {
                        mode = Mode::Select;
                        data.modeButton.label = "Edit (Select)";
                    }
                    else
                    {
                        mode = Mode::Object;
                        data.modeButton.label = "Edit (Objects)";
                    }
                    data.reset_mode_buttons();
                }
                else if(event.key.keysym.sym == SDLK_t)
                {
                    if(mode == Mode::Terrain)
                    {
                        mode = Mode::Select;
                        data.modeButton.label = "Edit (Select)";
                    }
                    else
                    {
                        mode = Mode::Terrain;
                        data.modeButton.label = "Edit (Terrain)";
                    }
                    data.reset_mode_buttons();
                }
                // Smooth current map, F5
                else if(event.key.keysym.sym == SDLK_F5)
                {
                    data.resmooth_terrain();
                    eds().levelchanged = 1;
                }
                // Change to new palette ..
                else if(event.key.keysym.sym == SDLK_F9)
                {
                    load_and_set_palette("our.pal", eds().scenpalette);
                }
                break;
            default:
                break;
            }
        }

		short scroll_delta = get_and_reset_scroll_amount();
		#if defined(USE_TOUCH_INPUT)
		// Only scroll the tile selector when touching it and you've already moved a bit
		if(mymouse.left && Rect(S_RIGHT, PIX_TOP, 4*GRID_SIZE, 4*GRID_SIZE).contains(mymouse.x, mymouse.y) && fabs(eds().mouse_last_y - mymouse.y) > 4)
        {
		#endif
		// Slide tile selector down ..
		if (og::runtime::current_session->keystates_[KEYSTATE_DOWN] || scroll_delta < 0)
		{
			eds().rowsdown++;
			if (eds().rowsdown >= eds().maxrows)
				eds().rowsdown -= eds().maxrows;
            
            eds().redraw = 1;
            
			while (og::runtime::current_session->keystates_[KEYSTATE_DOWN])
			{
				SDL_Delay(1);
				get_input_events(POLL);
			}
		}

		// Slide tile selector up ..
		if (og::runtime::current_session->keystates_[KEYSTATE_UP] || scroll_delta > 0)
		{
			eds().rowsdown--;
			if (eds().rowsdown < 0)
				eds().rowsdown += eds().maxrows;
			if (eds().rowsdown <0 || eds().rowsdown >= eds().maxrows) // bad case
				eds().rowsdown = 0;
            
            eds().redraw = 1;
            
			while (og::runtime::current_session->keystates_[KEYSTATE_UP])
			{
				SDL_Delay(1);
				get_input_events(POLL);
			}
		}
		#if defined(USE_TOUCH_INPUT)
        }
		#endif


		// Scroll the screen (panning)
		#ifndef OUYA
		eds().pan_left = (og::runtime::current_session->keystates_[KEYSTATE_KP_4] || og::runtime::current_session->keystates_[KEYSTATE_KP_7] || og::runtime::current_session->keystates_[KEYSTATE_KP_1] || og::runtime::current_session->keystates_[KEYSTATE_a]);
		eds().pan_right = (og::runtime::current_session->keystates_[KEYSTATE_KP_6] || og::runtime::current_session->keystates_[KEYSTATE_KP_3] || og::runtime::current_session->keystates_[KEYSTATE_KP_9] || og::runtime::current_session->keystates_[KEYSTATE_d]);
		eds().pan_up = (og::runtime::current_session->keystates_[KEYSTATE_KP_8] || og::runtime::current_session->keystates_[KEYSTATE_KP_7] || og::runtime::current_session->keystates_[KEYSTATE_KP_9] || og::runtime::current_session->keystates_[KEYSTATE_w]);
		eds().pan_down = (og::runtime::current_session->keystates_[KEYSTATE_KP_2] || og::runtime::current_session->keystates_[KEYSTATE_KP_1] || og::runtime::current_session->keystates_[KEYSTATE_KP_3] || og::runtime::current_session->keystates_[KEYSTATE_s]);
		#endif
		if (eds().pan_up && data.level->level_visuals().topy >= PAN_LIMIT_UP) // top of the screen
        {
            eds().redraw = 1;
			data.level->add_draw_pos(0, -SCROLLSIZE);
        }
		if (eds().pan_down && data.level->level_visuals().topy <= PAN_LIMIT_DOWN) // scroll down
        {
            eds().redraw = 1;
			data.level->add_draw_pos(0, SCROLLSIZE);
        }
		if (eds().pan_left && data.level->level_visuals().topx >= PAN_LIMIT_LEFT) // scroll left
        {
            eds().redraw = 1;
			data.level->add_draw_pos(-SCROLLSIZE, 0);
        }
		if (eds().pan_right && data.level->level_visuals().topx <= PAN_LIMIT_RIGHT) // scroll right
        {
            eds().redraw = 1;
			data.level->add_draw_pos(SCROLLSIZE, 0);
        }


		// Mouse stuff ..
		mymouse = query_mouse_no_poll();
		
			if (mymouse.left)       // put or remove the current guy
			{
				eds().redraw = 1;
				mx = static_cast<Sint32>(mymouse.x);
				my = static_cast<Sint32>(mymouse.y);
            
            // Holding on menu items
            bool mouse_on_menu = data.mouse_on_menus(mx, my);
            bool old_mouse_on_menu = data.mouse_on_menus(eds().mouse_last_x, eds().mouse_last_y);
            bool on_menu = mouse_on_menu && old_mouse_on_menu;
            bool off_menu = !mouse_on_menu && !old_mouse_on_menu;
            
            if(on_menu)
            {
                // Panning with mouse (touch)
                if(data.panUpButton.contains(mx, my) && data.level->level_visuals().topy >= PAN_LIMIT_UP) // top of the screen
                {
                    eds().redraw = 1;
                    data.level->add_draw_pos(0, -SCROLLSIZE);
                }
                else if(data.panUpRightButton.contains(mx, my))
                {
                    eds().redraw = 1;
                    if(data.level->level_visuals().topy >= PAN_LIMIT_UP)
                        data.level->add_draw_pos(0, -SCROLLSIZE);
                    if(data.level->level_visuals().topx <= PAN_LIMIT_RIGHT)
                        data.level->add_draw_pos(SCROLLSIZE, 0);
                }
                else if(data.panUpLeftButton.contains(mx, my))
                {
                    eds().redraw = 1;
                    if(data.level->level_visuals().topy >= PAN_LIMIT_UP)
                        data.level->add_draw_pos(0, -SCROLLSIZE);
                    if(data.level->level_visuals().topx >= PAN_LIMIT_LEFT)
                        data.level->add_draw_pos(-SCROLLSIZE, 0);
                }
                else if(data.panDownButton.contains(mx, my) && data.level->level_visuals().topy <= PAN_LIMIT_DOWN) // scroll down
                {
                    eds().redraw = 1;
                    data.level->add_draw_pos(0, SCROLLSIZE);
                }
                else if(data.panDownRightButton.contains(mx, my))
                {
                    eds().redraw = 1;
                    if(data.level->level_visuals().topy <= PAN_LIMIT_DOWN)
                        data.level->add_draw_pos(0, SCROLLSIZE);
                    if(data.level->level_visuals().topx <= PAN_LIMIT_RIGHT)
                        data.level->add_draw_pos(SCROLLSIZE, 0);
                }
                else if(data.panDownLeftButton.contains(mx, my))
                {
                    eds().redraw = 1;
                    if(data.level->level_visuals().topy <= PAN_LIMIT_DOWN)
                        data.level->add_draw_pos(0, SCROLLSIZE);
                    if(data.level->level_visuals().topx >= PAN_LIMIT_LEFT)
                        data.level->add_draw_pos(-SCROLLSIZE, 0);
                }
                else if(data.panLeftButton.contains(mx, my) && data.level->level_visuals().topx >= PAN_LIMIT_LEFT) // scroll left
                {
                    eds().redraw = 1;
                    data.level->add_draw_pos(-SCROLLSIZE, 0);
                }
                else if(data.panRightButton.contains(mx, my) && data.level->level_visuals().topx <= PAN_LIMIT_RIGHT) // scroll right
                {
                    eds().redraw = 1;
                    data.level->add_draw_pos(SCROLLSIZE, 0);
                }
                    
            }
            else if(off_menu)
            {
                // Zardus: ADD: can move map by clicking on minimap
                if ((mode != Mode::Select || (!data.rect_selecting && !data.dragging)) && mx > og::runtime::current_session->myscreen_->viewob[0]->endx - myradar.xview - 4
                        && my > og::runtime::current_session->myscreen_->viewob[0]->endy - myradar.yview - 4
                        && mx < og::runtime::current_session->myscreen_->viewob[0]->endx - 4 && my < og::runtime::current_session->myscreen_->viewob[0]->endy - 4)
                {
                    mx -= og::runtime::current_session->myscreen_->viewob[0]->endx - myradar.xview - 4;
                    my -= og::runtime::current_session->myscreen_->viewob[0]->endy - myradar.yview - 4;

                    // Zardus: above set_screen_pos doesn't take into account that minimap scrolls too. This one does.
                    data.level->set_draw_pos(myradar.radarx * GRID_SIZE + mx * GRID_SIZE - 160,
                                    myradar.radary * GRID_SIZE + my * GRID_SIZE - 100);
                }
                else  // in the main window
                {
                    windowx = static_cast<Sint32>(mymouse.x) + data.level->level_visuals().topx - og::runtime::current_session->myscreen_->viewob[0]->xloc; // - S_LEFT
                    windowx -= (windowx%GRID_SIZE);
                    windowy = static_cast<Sint32>(mymouse.y) + data.level->level_visuals().topy - og::runtime::current_session->myscreen_->viewob[0]->yloc; // - S_UP
                    windowy -= (windowy%GRID_SIZE);

                    if (mode == Mode::Terrain)
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
                                data.set_terrain(windowx, windowy, static_cast<unsigned char>(get_random_matching_tile(terrain_brush.terrain)));
                                eds().levelchanged = 1;
                                if (terrain_brush.use_smoothing) // smooth a few squares, if not control
                                {
                                    for (i=windowx-1; i <= windowx+1; i++)
                                        for (j=windowy-1; j <=windowy+1; j++)
                                            if (i >= 0 && i < data.level->world().grid.w &&
                                                    j >= 0 && j < data.level->world().grid.h)
                                                data.level->world().mysmoother.smooth(i, j);
                                }
                                
                                myradar.update(data.level.get());
                            }
                        }
                    }  // end of setting grid square
                } // end of main window
            }

		}      // end of left mouse button

		// Now perform color cycling if selected
			if (eds().cyclemode)
			{
			    cycletimer -= static_cast<float>(start_ticks - last_ticks) / 1000.0f;
			    if(cycletimer <= 0)
	            {
	                cycletimer = 0.5f;
	                cycle_palette(eds().scenpalette, WATER_START, WATER_END, 1);
                cycle_palette(eds().scenpalette, ORANGE_START, ORANGE_END, 1);
            }
			eds().redraw = 1;
		}
		
		// Redraw screen
		if (eds().redraw)
		{
            eds().redraw = 0;
			data.draw(og::runtime::current_session->myscreen_);
			
            #ifdef USE_CONTROLLER_INPUT
            og::runtime::current_session->myscreen_->fastbox(mymouse.x-1, mymouse.y-1, 4, 4, PURE_WHITE);
            og::runtime::current_session->myscreen_->fastbox(mymouse.x, mymouse.y, 2, 2, PURE_BLACK);
            #endif
            og::runtime::current_session->myscreen_->refresh();
		}
        
        SDL_Delay(10);
        
	    last_ticks = start_ticks;
	    start_ticks = SDL_GetTicks();

	}
	
	// Reset the screen position so it doesn't ruin the main menu
    data.level->set_draw_pos(0, 0);
    // Update the screen's position
    data.level->draw(og::runtime::current_session->myscreen_);
    // Clear the background
    og::runtime::current_session->myscreen_->clearbuffer();
    
    (void)unmount_campaign_package_with_error(data.campaign->id);
    (void)mount_campaign_package_with_error(old_campaign);
    
	return OK;
}


Sint32 get_random_matching_tile(Sint32 whatback)
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
walker * some_hit(Sint32 x, Sint32 y, walker  *ob, LevelRuntimeData* data)
{
    for(auto& uptr : data->world().oblist)
	{
	    walker* w = uptr.get();
		if (w && w != ob
            && check_collide(x, y, ob->sizex, ob->sizey,
			                  w->xpos, w->ypos,
			                  w->sizex, w->sizey) )
        {
            ob->collide_ob = w;
            return w;
        }
	}

    for(auto& uptr : data->world().fxlist)
	{
	    walker* w = uptr.get();
		if (w && w != ob
            && check_collide(x, y, ob->sizex, ob->sizey,
			                  w->xpos, w->ypos,
			                  w->sizex, w->sizey) )
        {
            ob->collide_ob = w;
            return w;
        }
	}

    for(auto& uptr : data->world().weaplist)
	{
	    walker* w = uptr.get();
		if (w && w != ob
            && check_collide(x, y, ob->sizex, ob->sizey,
			                  w->xpos, w->ypos,
			                  w->sizex, w->sizey) )
        {
            ob->collide_ob = w;
            return w;
        }
	}

	ob->collide_ob = nullptr;
	return nullptr;
}
