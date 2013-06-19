

#include "screen.h"
#include "view.h"
#include "radar.h"
#include "walker.h"
#include "smooth.h"
#include "input.h"
#include "util.h"

/* Changelog
 * 	8/8/02: Zardus: added scrolling-by-minimap
 * 		Zardus: added scrolling-by-keyboard
 */

#define OK 4 //this function was successful, continue normal operation

#include <string>
using namespace std;
#include <stdlib.h>
#define MINIMUM_TIME 0


#define S_LEFT 1
#define S_RIGHT 245
#define S_UP 1
#define S_DOWN 188

#define VERSION_NUM (char) 8 // save scenario type info
#define SCROLLSIZE 8

#define OBJECT_MODE 0
#define MAP_MODE 1

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

#define NORMAL_KEYBOARD(x)  clear_keyboard(); release_keyboard(); x grab_keyboard();

void remove_all_objects(screen *master);
void do_help(screen * myscreen);
Sint32 new_scenario_name();
Sint32 new_grid_name();
void set_screen_pos(screen *myscreen, Sint32 x, Sint32 y);
walker * some_hit(Sint32 x, Sint32 y, walker  *ob, screen *screenp);
char some_pix(Sint32 whatback);
char  * query_my_map_name();


bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

Sint32 do_load(screen *ascreen);  // load a scenario or grid
Sint32 do_save(screen *ascreen);  // save a scenario or grid
Sint32 display_panel(screen *myscreen);
Sint32 save_scenario(char * filename, screen * master, char *gridname);
void info_box(walker  *target, screen * myscreen);
void set_facing(walker *target, screen *myscreen);
void set_name(walker  *target, screen * myscreen);
void scenario_options(screen * myscreen);
Sint32 quit_level_editor(Sint32);

extern screen *myscreen;  // global for scen?

// Zardus: our prefs object from view.cpp
extern options * theprefs;

extern Sint32 *mymouse;
Uint8 *mykeyboard;
//scenario *myscen = new scenario;
Sint32 currentmode = OBJECT_MODE;
Uint32 currentlevel = 1;
char scen_name[10];
char grid_name[10];

unsigned char scenpalette[768];
Sint32 backcount=0, forecount = 0;
Sint32 myorder = ORDER_LIVING;
char currentteam = 0;
Sint32 event = 1;  // need to redraw?
Sint32 levelchanged = 0;  // has level changed?
Sint32 cyclemode = 0;      // for color cycling
Sint32 grid_aligned = 1;  // aligned by grid, default is on
//buffers: PORT: changed start_time to start_time_s to avoid conflict with
//input.cpp
Sint32 start_time_s; // for timer ops

extern smoother  *mysmoother;

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

Sint32 rowsdown = 0;
Sint32 maxrows = ((sizeof(backgrounds)/4) / 4);
text *scentext;



class SimpleButton
{
public:
    SDL_Rect area;
    const std::string _text;
    bool remove_border;
    int color;
    bool centered;
    
    SimpleButton(const std::string& _text, int x, int y, unsigned int w, unsigned int h, bool remove_border = false, int color = DARK_BLUE);
    
    void draw(screen* myscreen, text* mytext);
    bool contains(int x, int y) const;
};


SimpleButton::SimpleButton(const std::string& _text, int x, int y, unsigned int w, unsigned int h, bool remove_border, int color)
    : _text(_text), remove_border(remove_border), color(color), centered(false)
{
    area.x = x;
    area.y = y;
    area.w = w;
    area.h = h;
}

void SimpleButton::draw(screen* myscreen, text* mytext)
{
    myscreen->draw_button(area.x, area.y, area.x + area.w - 1, area.y + area.h - 1, (remove_border? 0 : 1), 1);
    if(centered)
        mytext->write_xy(area.x + area.w/2 - 3*_text.size(), area.y + area.h/2 - 2, _text.c_str(), color, 1);
    else
        mytext->write_xy(area.x + 2, area.y + area.h/2 - 2, _text.c_str(), color, 1);
}

bool SimpleButton::contains(int x, int y) const
{
    return (area.x <= x && x < area.x + area.w
            && area.y <= y && y < area.y + area.h);
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
bool mouse_on_menus(int mx, int my, const set<SimpleButton*>& menu_buttons, const std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >& current_menu)
{
    for(set<SimpleButton*>::const_iterator e = menu_buttons.begin(); e != menu_buttons.end(); e++)
    {
        if((*e)->contains(mx, my))
            return true;
    }
    
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

bool activate_sub_menu_button(int mx, int my, std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >& current_menu, SimpleButton& button, bool is_in_top_menu = false)
{
    // Make sure it is showing
    if(!button.contains(mx, my) || (!is_in_top_menu && !button_showing(current_menu, &button)))
        return false;
    
    while (mymouse[MOUSE_LEFT])
        get_input_events(WAIT);
    
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
    
    // No parent!
    return is_in_top_menu;
}

bool activate_menu_choice(int mx, int my, std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >& current_menu, SimpleButton& button, bool is_in_top_menu = false)
{
    // Make sure it is showing
    if(!button.contains(mx, my) || (!is_in_top_menu && !button_showing(current_menu, &button)))
        return false;
    
    while (mymouse[MOUSE_LEFT])
        get_input_events(WAIT);
    
    // Close menu
    current_menu.clear();
    return true;
}

Sint32 level_editor()
{
	Sint32 i,j;
	Sint32 extra;
	Sint32 windowx, windowy;
	walker  *newob;
	Sint32 mx, my;
	char mystring[80];
	short count;
	
	
    memset(scen_name, 0, 10);
    memset(grid_name, 0, 10);
    
    // Initialize palette for cycling
    load_and_set_palette("our.pal", scenpalette);
	
	scentext = new text(myscreen);
	// Set the un-set text to empty ..
	for (i=0; i < 60; i ++)
		myscreen->scentext[i][0] = 0;

    std::list<std::string> levels = list_levels();
    
	// Set our default par value ..
	myscreen->par_value = 1;
	// Loading the first level automatically
	load_scenario(levels.front().c_str(), myscreen);
	strncpy(scen_name, levels.front().c_str(), 10);
    strcpy(grid_name, query_my_map_name());

	myscreen->clearfontbuffer();
	myscreen->redraw();
	myscreen->refresh();
	
	// GUI
	using std::set;
	using std::pair;
	using std::list;
	
	// File menu
	SimpleButton fileButton("File", 0, 0, 30, 15);
	SimpleButton fileCampaignButton("Campaign >", 0, fileButton.area.y + fileButton.area.h, 65, 15, true);
	SimpleButton fileLevelButton("Level >", 0, fileCampaignButton.area.y + fileCampaignButton.area.h, 65, 15, true);
	SimpleButton fileQuitButton("Exit", 0, fileLevelButton.area.y + fileLevelButton.area.h, 65, 15, true);
	
	// File > Campaign submenu
	SimpleButton fileCampaignImportButton("Import...", fileCampaignButton.area.x + fileCampaignButton.area.w, fileCampaignButton.area.y, 65, 15, true);
	SimpleButton fileCampaignShareButton("Share...", fileCampaignImportButton.area.x, fileCampaignImportButton.area.y + fileCampaignImportButton.area.h, 65, 15, true);
	SimpleButton fileCampaignNewButton("New", fileCampaignImportButton.area.x, fileCampaignShareButton.area.y + fileCampaignShareButton.area.h, 65, 15, true);
	SimpleButton fileCampaignLoadButton("Load...", fileCampaignImportButton.area.x, fileCampaignNewButton.area.y + fileCampaignNewButton.area.h, 65, 15, true);
	SimpleButton fileCampaignSaveButton("Save", fileCampaignImportButton.area.x, fileCampaignLoadButton.area.y + fileCampaignLoadButton.area.h, 65, 15, true);
	SimpleButton fileCampaignSaveAsButton("Save As...", fileCampaignImportButton.area.x, fileCampaignSaveButton.area.y + fileCampaignSaveButton.area.h, 65, 15, true);
	
	// File > Level submenu
	SimpleButton fileLevelNewButton("New", fileLevelButton.area.x + fileLevelButton.area.w, fileLevelButton.area.y, 65, 15, true);
	SimpleButton fileLevelLoadButton("Load...", fileLevelNewButton.area.x, fileLevelNewButton.area.y + fileLevelNewButton.area.h, 65, 15, true);
	SimpleButton fileLevelSaveButton("Save", fileLevelNewButton.area.x, fileLevelLoadButton.area.y + fileLevelLoadButton.area.h, 65, 15, true);
	SimpleButton fileLevelSaveAsButton("Save As...", fileLevelNewButton.area.x, fileLevelSaveButton.area.y + fileLevelSaveButton.area.h, 65, 15, true);
	
	// Campaign menu
	SimpleButton campaignButton("Campaign", fileButton.area.x + fileButton.area.w, 0, 55, 15);
	SimpleButton campaignProfileButton("Profile >", campaignButton.area.x, campaignButton.area.y + campaignButton.area.h, 59, 15, true);
	SimpleButton campaignDetailsButton("Details >", campaignButton.area.x, campaignProfileButton.area.y + campaignProfileButton.area.h, 59, 15, true);
	SimpleButton campaignValidateButton("Validate", campaignButton.area.x, campaignDetailsButton.area.y + campaignDetailsButton.area.h, 59, 15, true);
	
	// Campaign > Profile submenu
	SimpleButton campaignProfileTitleButton("Title...", campaignProfileButton.area.x + campaignProfileButton.area.w, campaignProfileButton.area.y, 95, 15, true);
	SimpleButton campaignProfileDescriptionButton("Description...", campaignProfileTitleButton.area.x, campaignProfileTitleButton.area.y + campaignProfileTitleButton.area.h, 95, 15, true);
	SimpleButton campaignProfileIconButton("Icon...", campaignProfileTitleButton.area.x, campaignProfileDescriptionButton.area.y + campaignProfileDescriptionButton.area.h, 95, 15, true);
	SimpleButton campaignProfileAuthorsButton("Authors...", campaignProfileTitleButton.area.x, campaignProfileIconButton.area.y + campaignProfileIconButton.area.h, 95, 15, true);
	SimpleButton campaignProfileContributorsButton("Contributors...", campaignProfileTitleButton.area.x, campaignProfileAuthorsButton.area.y + campaignProfileAuthorsButton.area.h, 95, 15, true);
	
	// Campaign > Details submenu
	SimpleButton campaignDetailsVersionButton("Version...", campaignDetailsButton.area.x + campaignDetailsButton.area.w, campaignDetailsButton.area.y, 113, 15, true);
	SimpleButton campaignDetailsSuggestedPowerButton("Suggested power...", campaignDetailsVersionButton.area.x, campaignDetailsVersionButton.area.y + campaignDetailsVersionButton.area.h, 113, 15, true);
	SimpleButton campaignDetailsFirstLevelButton("First level...", campaignDetailsVersionButton.area.x, campaignDetailsSuggestedPowerButton.area.y + campaignDetailsSuggestedPowerButton.area.h, 113, 15, true);
	
	
	// Level menu
	SimpleButton levelButton("Level", campaignButton.area.x + campaignButton.area.w, 0, 40, 15);
	SimpleButton levelLevelNumberButton("Level number...", levelButton.area.x, levelButton.area.y + levelButton.area.h, 95, 15, true);
	SimpleButton levelTitleButton("Title...", levelButton.area.x, levelLevelNumberButton.area.y + levelLevelNumberButton.area.h, 95, 15, true);
	SimpleButton levelDescriptionButton("Description...", levelButton.area.x, levelTitleButton.area.y + levelTitleButton.area.h, 95, 15, true);
	SimpleButton levelMapSizeButton("Map size...", levelButton.area.x, levelMapSizeButton.area.y + levelMapSizeButton.area.h, 95, 15, true);
	
	// Selection menu
	SimpleButton selectionButton("Selection", levelButton.area.x + levelButton.area.w, 0, 65, 15);
	SimpleButton selectionLevelButton("Level >", selectionButton.area.x, selectionButton.area.y + selectionButton.area.h, 47, 15, true);
	SimpleButton selectionTeamButton("Team >", selectionButton.area.x, selectionLevelButton.area.y + selectionLevelButton.area.h, 47, 15, true);
	SimpleButton selectionClassButton("Class >", selectionButton.area.x, selectionTeamButton.area.y + selectionTeamButton.area.h, 47, 15, true);
	SimpleButton selectionCopyButton("Copy", selectionButton.area.x, selectionClassButton.area.y + selectionClassButton.area.h, 47, 15, true);
	SimpleButton selectionPasteButton("Paste", selectionButton.area.x, selectionCopyButton.area.y + selectionCopyButton.area.h, 47, 15, true);
	SimpleButton selectionDeleteButton("Delete", selectionButton.area.x, selectionPasteButton.area.y + selectionPasteButton.area.h, 47, 15, true);
	
	// Selection > Level submenu
	SimpleButton selectionLevelIncreaseButton("Increase", selectionLevelButton.area.x + selectionLevelButton.area.w, selectionLevelButton.area.y, 53, 15, true);
	SimpleButton selectionLevelDecreaseButton("Decrease", selectionLevelIncreaseButton.area.x, selectionLevelIncreaseButton.area.y + selectionLevelIncreaseButton.area.h, 53, 15, true);
	
	// Selection > Team submenu
	SimpleButton selectionTeamPreviousButton("Previous", selectionTeamButton.area.x + selectionTeamButton.area.w, selectionTeamButton.area.y, 53, 15, true);
	SimpleButton selectionTeamNextButton("Next", selectionTeamPreviousButton.area.x, selectionTeamPreviousButton.area.y + selectionTeamPreviousButton.area.h, 53, 15, true);
	
	// Selection > Class submenu
	SimpleButton selectionClassPreviousButton("Previous", selectionClassButton.area.x + selectionClassButton.area.w, selectionClassButton.area.y, 53, 15, true);
	SimpleButton selectionClassNextButton("Next", selectionClassPreviousButton.area.x, selectionClassPreviousButton.area.y + selectionClassPreviousButton.area.h, 53, 15, true);
	
	
	// Top menu
	set<SimpleButton*> menu_buttons;
	menu_buttons.insert(&fileButton);
	menu_buttons.insert(&campaignButton);
	menu_buttons.insert(&levelButton);
	menu_buttons.insert(&selectionButton);
	
	// The active menu buttons
	list<pair<SimpleButton*, set<SimpleButton*> > > current_menu;
	

	//******************************
	// Keyboard loop
	//******************************
    
    float cycletimer = 0.0f;
	grab_mouse();
	mykeyboard = query_keyboard();
	Uint32 last_ticks = SDL_GetTicks();
	Uint32 start_ticks = last_ticks;

	//
	// This is the main program loop
	//
	while(1)
	{
		// Reset the timer count to zero ...
		reset_timer();

		if (myscreen->end)
			break;

		//buffers: get keys and stuff
		get_input_events(POLL);

		// Zardus: COMMENT: I went through and replaced dumbcounts with get_input_events.
        
        if(query_key_press_event() && mykeyboard[KEYSTATE_ESCAPE])
        {
            if(!levelchanged)
                break;
            
            if(yes_or_no_prompt("Exit", "Quit without saving?", false))
                break;
            
            delete scentext;
            scentext = new text(myscreen);
            
            myscreen->clearfontbuffer();
            event = 1;
            
            // Wait until release
            while (mykeyboard[KEYSTATE_ESCAPE])
                get_input_events(WAIT);
        }
        
		// Delete all with ^D
		if (mykeyboard[KEYSTATE_d] && mykeyboard[KEYSTATE_LCTRL])
		{
			remove_all_objects(myscreen);
			levelchanged = 1;
			event = 1;
		}

		// Change teams ..
		if (mykeyboard[KEYSTATE_0])
		{
			currentteam = 0;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_1])
		{
			currentteam = 1;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_2])
		{
			currentteam = 2;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_3])
		{
			currentteam = 3;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_4])
		{
			currentteam = 4;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_5])
		{
			currentteam = 5;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_6])
		{
			currentteam = 6;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_7])
		{
			currentteam = 7;
			event = 1;
		}

		// Toggle grid alignment
		if (mykeyboard[KEYSTATE_g])
		{
			grid_aligned = (grid_aligned+1)%3;
			event = 1;
			while (mykeyboard[KEYSTATE_g])
				//buffers: dumbcount++;
				get_input_events(WAIT);
		}

		// Show help
		if (mykeyboard[KEYSTATE_h])
		{
			release_mouse();
			do_help(myscreen);
			myscreen->clearfontbuffer();
			grab_mouse();
			event = 1;
		}

		if (mykeyboard[KEYSTATE_KP_MULTIPLY]) // options menu
		{
			release_mouse();
			scenario_options(myscreen);
			grab_mouse();
			event = 1; // redraw screen
		}

		// Load scenario, etc. ..
		if (mykeyboard[KEYSTATE_l])
		{
			if (levelchanged)
			{
				myscreen->draw_button(30, 15, 220, 25, 1, 1);
				scentext->write_xy(32, 17, "Save level first? [Y/N]", DARK_BLUE, 1);
				myscreen->buffer_to_screen(0, 0, 320, 200);
				while ( !mykeyboard[KEYSTATE_y] && !mykeyboard[KEYSTATE_n])
					get_input_events(WAIT);
				if (mykeyboard[KEYSTATE_y]) // save first
					do_save(myscreen);
			}
			myscreen->draw_button(30, 15, 220, 25, 1, 1);
			scentext->write_xy(32, 17, "Loading Level...", DARK_BLUE, 1);
			do_load(myscreen);
			myscreen->clearfontbuffer();
		}

		// Save scenario or grid..
		if (mykeyboard[KEYSTATE_s])
		{
			do_save(myscreen);
		}  // end of saving routines


		// Switch modes ..
		if (mykeyboard[KEYSTATE_m])        // switch to map or guys ..
		{
			event = 1;
			currentmode = (currentmode+1) %2;
			while (mykeyboard[KEYSTATE_m])
				get_input_events(WAIT);
		}

		// New names
		if (mykeyboard[KEYSTATE_n])
		{
			event = 1;
			//gotoxy(1, 23);
			myscreen->draw_button(50, 30, 200, 40, 1, 1);
			scentext->write_xy(52, 32, "New name [G/S] : ", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			while ( !mykeyboard[KEYSTATE_g] && !mykeyboard[KEYSTATE_s] )
				get_input_events(WAIT);
			if (mykeyboard[KEYSTATE_s])
			{
				myscreen->draw_button(50, 30, 200, 40, 1, 1);
				myscreen->buffer_to_screen(0, 0, 320, 200);
				new_scenario_name();
				while (mykeyboard[KEYSTATE_s])
					get_input_events(WAIT);
			} // end new scenario name
			else if (mykeyboard[KEYSTATE_g])
			{
				myscreen->draw_button(50, 30, 200, 40, 1, 1);
				myscreen->buffer_to_screen(0, 0, 320, 200);
				new_grid_name();
				while (mykeyboard[KEYSTATE_g])
					get_input_events(WAIT);
			} // end new grid name
			myscreen->clearfontbuffer(50,30,150,10);
		}

		// Enter scenario text ..
		if (mykeyboard[KEYSTATE_t])
		{
#define TEXT_DOWN(x)  (14+((x)*7))
   #define TL 4
			//gotoxy(1, 1);
			myscreen->draw_button(0, 10, 200, 200, 2, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			scentext->write_xy(TL, TEXT_DOWN(0), "Enter new scenario text;", DARK_BLUE, 1);
			scentext->write_xy(TL, TEXT_DOWN(1), " PERIOD (.) alone to end.", DARK_BLUE, 1);
			scentext->write_xy(TL, TEXT_DOWN(2), "*--------*---------*---------*", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			myscreen->scentextlines = 0;
			count = 2;
			for (i=0; i < 23; i++)
				if (strlen(myscreen->scentext[i]))
					scentext->write_xy(TL, TEXT_DOWN(i+3), myscreen->scentext[i], DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			extra = 1;
			for (i=0; i < 60; i++)
			{
				count++;
				mystring[0] = 0;
				if (! (count%26) )
				{
					myscreen->draw_box(TL, TEXT_DOWN(3), 196, 196, 27, 1, 1);
					myscreen->buffer_to_screen(0, 0, 320, 200);
					for (j=0; j < 23; j++)
					{
						count = j+(23*extra);
						if (count < 60)
							if (strlen(myscreen->scentext[count]))
								scentext->write_xy(TL, TEXT_DOWN(j+3), myscreen->scentext[count], DARK_BLUE, 1);
					}
					count = 3;
					extra++;
					myscreen->buffer_to_screen(0, 0, 320, 200);
				}
				char* new_text = scentext->input_string(TL, TEXT_DOWN(count), 30, myscreen->scentext[i]);
				if(new_text == NULL)
                    new_text = myscreen->scentext[i];
				strcpy(mystring, new_text);
				strcpy(myscreen->scentext[i], mystring);
				if (!strcmp(".", mystring)) // says end ..
				{
					i = 70;
					myscreen->draw_box(0, 10, 200, 200, 0, 1, 1);
					myscreen->buffer_to_screen(0, 0, 320, 200);
					myscreen->scentext[i][0] = 0;
					event = 1;
				}
				else
					myscreen->scentextlines++;
			}
			myscreen->draw_box(0, 10, 200, 200, 0, 1, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			event = 1;
		}

		// Display the scenario help..
		if (mykeyboard[KEYSTATE_SLASH] && (mykeyboard[KEYSTATE_RSHIFT] || mykeyboard[KEYSTATE_LSHIFT]))
		{
			read_scenario(myscreen);
			myscreen->clearfontbuffer();
			event = 1;
		}

		// Change level of current guy being placed ..
		if (mykeyboard[KEYSTATE_RIGHTBRACKET])
		{
			currentlevel++;
			//while (mykeyboard[KEYSTATE_RIGHTBRACKET])
			//  dumbcount++;
			event = 1;
		}
		if (mykeyboard[KEYSTATE_LEFTBRACKET] && currentlevel > 1)
		{
			currentlevel--;
			//while (mykeyboard[KEYSTATE_LEFTBRACKET])
			//  dumbcount++;
			event = 1;
		}

		// Change between generator and living orders
		if (mykeyboard[KEYSTATE_o])        // this is letter o
		{
			if (myorder == ORDER_LIVING)
			{
				myorder = ORDER_GENERATOR;
				forecount = FAMILY_TENT;
			}
			else if (myorder == ORDER_GENERATOR)
				myorder = ORDER_SPECIAL;   // for placing team guys ..
			else if (myorder == ORDER_SPECIAL)
			{
				myorder = ORDER_TREASURE;
				forecount = FAMILY_DRUMSTICK;
			}
			else if (myorder == ORDER_TREASURE)
				myorder = ORDER_WEAPON;
			else if (myorder == ORDER_WEAPON)
				myorder = ORDER_LIVING;
			currentmode = OBJECT_MODE;
			event = 1; // change score panel
			while (mykeyboard[KEYSTATE_o])
				get_input_events(WAIT);
		}

		// Slide tile selector down ..
		if (mykeyboard[KEYSTATE_DOWN])
		{
			rowsdown++;
			event = 1;
			if (rowsdown >= maxrows)
				rowsdown -= maxrows;
			display_panel(myscreen);
			while (mykeyboard[KEYSTATE_DOWN])
				get_input_events(WAIT);
		}

		// Slide tile selector up ..
		if (mykeyboard[KEYSTATE_UP])
		{
			rowsdown--;
			event = 1;
			if (rowsdown < 0)
				rowsdown += maxrows;
			if (rowsdown <0 || rowsdown >= maxrows) // bad case
				rowsdown = 0;
			display_panel(myscreen);
			while (mykeyboard[KEYSTATE_UP])
				get_input_events(WAIT);
		}

		// Smooth current map, F5
		if (mykeyboard[KEYSTATE_F5])
		{
			if (mysmoother)
				delete mysmoother;
			mysmoother = new smoother();
			mysmoother->set_target(myscreen);
			mysmoother->smooth();
			while (mykeyboard[KEYSTATE_F5])
				get_input_events(WAIT);
			event = 1;
			levelchanged = 1;
		}

		// Change to new palette ..
		if (mykeyboard[KEYSTATE_F9])
		{
			load_and_set_palette("our.pal", scenpalette);
			while (mykeyboard[KEYSTATE_F9])
				get_input_events(WAIT);
		}

		// Toggle color cycling
		if (mykeyboard[KEYSTATE_F10])
		{
			cyclemode++;
			cyclemode %= 2;
			while (mykeyboard[KEYSTATE_F10])
				get_input_events(WAIT);
		}

		// Mouse stuff ..
		mymouse = query_mouse();

		// Scroll the screen ..
		// Zardus: ADD: added scrolling by keyboard
		// Zardus: PORT: disabled mouse scrolling
		if ((mykeyboard[KEYSTATE_KP_8] || mykeyboard[KEYSTATE_KP_7] || mykeyboard[KEYSTATE_KP_9]) // || mymouse[MOUSE_Y]< 2)
		        && myscreen->topy >= 0) // top of the screen
			set_screen_pos(myscreen, myscreen->topx,
			               myscreen->topy-SCROLLSIZE);
		if ((mykeyboard[KEYSTATE_KP_2] || mykeyboard[KEYSTATE_KP_1] || mykeyboard[KEYSTATE_KP_3]) // || mymouse[MOUSE_Y]> 198)
		        && myscreen->topy <= (GRID_SIZE*myscreen->maxy)-18) // scroll down
			set_screen_pos(myscreen, myscreen->topx,
			               myscreen->topy+SCROLLSIZE);
		if ((mykeyboard[KEYSTATE_KP_4] || mykeyboard[KEYSTATE_KP_7] || mykeyboard[KEYSTATE_KP_1]) // || mymouse[MOUSE_X]< 2)
		        && myscreen->topx >= 0) // scroll left
			set_screen_pos(myscreen, myscreen->topx-SCROLLSIZE,
			               myscreen->topy);
		if ((mykeyboard[KEYSTATE_KP_6] || mykeyboard[KEYSTATE_KP_3] || mykeyboard[KEYSTATE_KP_9]) // || mymouse[MOUSE_X] > 318)
		        && myscreen->topx <= (GRID_SIZE*myscreen->maxx)-18) // scroll right
			set_screen_pos(myscreen, myscreen->topx+SCROLLSIZE,
			               myscreen->topy);

		if (mymouse[MOUSE_LEFT])       // put or remove the current guy
		{
			event = 1;
			mx = mymouse[MOUSE_X];
			my = mymouse[MOUSE_Y];
            
            // Clicking on menu items
            if(mouse_on_menus(mx, my, menu_buttons, current_menu))
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
                    s.insert(&fileCampaignImportButton);
                    s.insert(&fileCampaignShareButton);
                    s.insert(&fileCampaignNewButton);
                    s.insert(&fileCampaignLoadButton);
                    s.insert(&fileCampaignSaveButton);
                    s.insert(&fileCampaignSaveAsButton);
                    current_menu.push_back(std::make_pair(&fileCampaignButton, s));
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignImportButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignShareButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignNewButton))
                {
                    // TODO: Confirm if unsaved
                    // TODO: Ask for campaign ID
                    
                    char campaign[50] = "org.openglad.testing";
                    create_new_campaign(campaign);
                    
                    // Mount new campaign
                    unmount_campaign_package(myscreen->current_campaign);
                    mount_campaign_package(campaign);
                    
                    // Load first scenario
                    levels = list_levels();
                    
                    if(levels.size() > 0)
                    {
                        load_scenario(levels.front().c_str(), myscreen);
                        strncpy(scen_name, levels.front().c_str(), 10);
                        strcpy(grid_name, query_my_map_name());
                    }
                    else
                    {
                        Log("Campaign has no scenarios!\n");
                    }
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignLoadButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignSaveButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignSaveAsButton))
                {
                    
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
                else if(activate_menu_choice(mx, my, current_menu, fileLevelNewButton))
                {
                    remove_all_objects(myscreen);
                    //clear_terrain(myscreen);
                    //clear_details(myscreen);
                    levelchanged = 1;
                }
                else if(activate_menu_choice(mx, my, current_menu, fileCampaignLoadButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileLevelSaveButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileLevelSaveAsButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, fileQuitButton))
                {
                    quit_level_editor(0);
                    break;
                }
                // CAMPAIGN
                else if(activate_sub_menu_button(mx, my, current_menu, campaignButton, true))
                {
                    set<SimpleButton*> s;
                    s.insert(&campaignProfileButton);
                    s.insert(&campaignDetailsButton);
                    s.insert(&campaignValidateButton);
                    current_menu.push_back(std::make_pair(&campaignButton, s));
                }
                // Profile >
                else if(activate_sub_menu_button(mx, my, current_menu, campaignProfileButton))
                {
                    set<SimpleButton*> s;
                    s.insert(&campaignProfileTitleButton);
                    s.insert(&campaignProfileDescriptionButton);
                    s.insert(&campaignProfileIconButton);
                    s.insert(&campaignProfileAuthorsButton);
                    s.insert(&campaignProfileContributorsButton);
                    current_menu.push_back(std::make_pair(&campaignProfileButton, s));
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignProfileTitleButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignProfileDescriptionButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignProfileIconButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignProfileAuthorsButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignProfileContributorsButton))
                {
                    
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
                else if(activate_menu_choice(mx, my, current_menu, campaignDetailsVersionButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignDetailsSuggestedPowerButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, campaignDetailsFirstLevelButton))
                {
                    
                }
                // LEVEL
                else if(activate_sub_menu_button(mx, my, current_menu, levelButton, true))
                {
                    set<SimpleButton*> s;
                    s.insert(&levelLevelNumberButton);
                    s.insert(&levelTitleButton);
                    s.insert(&levelDescriptionButton);
                    s.insert(&levelMapSizeButton);
                    current_menu.push_back(std::make_pair(&levelButton, s));
                }
                else if(activate_menu_choice(mx, my, current_menu, levelLevelNumberButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, levelTitleButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, levelDescriptionButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, levelMapSizeButton))
                {
                    
                }
                // SELECTION
                else if(activate_sub_menu_button(mx, my, current_menu, selectionButton, true))
                {
                    set<SimpleButton*> s;
                    s.insert(&selectionLevelButton);
                    s.insert(&selectionTeamButton);
                    s.insert(&selectionClassButton);
                    s.insert(&selectionCopyButton);
                    s.insert(&selectionPasteButton);
                    s.insert(&selectionDeleteButton);
                    current_menu.push_back(std::make_pair(&selectionButton, s));
                }
                // Level >
                else if(activate_sub_menu_button(mx, my, current_menu, selectionLevelButton))
                {
                    set<SimpleButton*> s;
                    s.insert(&selectionLevelIncreaseButton);
                    s.insert(&selectionLevelDecreaseButton);
                    current_menu.push_back(std::make_pair(&selectionLevelButton, s));
                }
                // Team >
                else if(activate_sub_menu_button(mx, my, current_menu, selectionTeamButton))
                {
                    set<SimpleButton*> s;
                    s.insert(&selectionTeamPreviousButton);
                    s.insert(&selectionTeamNextButton);
                    current_menu.push_back(std::make_pair(&selectionTeamButton, s));
                }
                // Class >
                else if(activate_sub_menu_button(mx, my, current_menu, selectionClassButton))
                {
                    set<SimpleButton*> s;
                    s.insert(&selectionClassPreviousButton);
                    s.insert(&selectionClassNextButton);
                    current_menu.push_back(std::make_pair(&selectionClassButton, s));
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionCopyButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionPasteButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionDeleteButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionLevelIncreaseButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionLevelDecreaseButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionTeamPreviousButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionTeamNextButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionClassPreviousButton))
                {
                    
                }
                else if(activate_menu_choice(mx, my, current_menu, selectionClassNextButton))
                {
                    
                }
            }
            else
            {
                // No menu click
                current_menu.clear();
                
                // Zardus: ADD: can move map by clicking on minimap
                if (mx > myscreen->viewob[0]->endx - myscreen->viewob[0]->myradar->xview - 4
                        && my > myscreen->viewob[0]->endy - myscreen->viewob[0]->myradar->yview - 4
                        && mx < myscreen->viewob[0]->endx - 4 && my < myscreen->viewob[0]->endy - 4)
                {
                    mx -= myscreen->viewob[0]->endx - myscreen->viewob[0]->myradar->xview - 4;
                    my -= myscreen->viewob[0]->endy - myscreen->viewob[0]->myradar->yview - 4;

                    // Zardus: above set_screen_pos doesn't take into account that minimap scrolls too. This one does.
                    set_screen_pos (myscreen, myscreen->viewob[0]->myradar->radarx * GRID_SIZE + mx * GRID_SIZE - 160,
                                    myscreen->viewob[0]->myradar->radary * GRID_SIZE + my * GRID_SIZE - 100);
                }
                else if ( (mx >= S_LEFT) && (mx <= S_RIGHT) &&
                          (my >= S_UP) && (my <= S_DOWN) )      // in the main window
                {
                    windowx = mymouse[MOUSE_X] + myscreen->topx - myscreen->viewob[0]->xloc; // - S_LEFT
                    if (grid_aligned==1)
                        windowx -= (windowx%GRID_SIZE);
                    windowy = mymouse[MOUSE_Y] + myscreen->topy - myscreen->viewob[0]->yloc; // - S_UP
                    if (grid_aligned==1)
                        windowy -= (windowy%GRID_SIZE);
                    if (mykeyboard[KEYSTATE_i]) // get info on current object
                    {
                        newob = myscreen->add_ob(ORDER_LIVING, FAMILY_ELF);
                        newob->setxy(windowx, windowy);
                        if (some_hit(windowx, windowy, newob, myscreen))
                            info_box(newob->collide_ob,myscreen);
                        myscreen->remove_ob(newob,0);
                        continue;
                    }  // end of info mode
                    if (mykeyboard[KEYSTATE_f]) // set facing of current object
                    {
                        newob = myscreen->add_ob(ORDER_LIVING, FAMILY_ELF);
                        newob->setxy(windowx, windowy);
                        if (some_hit(windowx, windowy, newob, myscreen))
                        {
                            set_facing(newob->collide_ob,myscreen);
                            levelchanged = 1;
                        }
                        myscreen->remove_ob(newob,0);
                        continue;
                    }  // end of set facing

                    if (mykeyboard[KEYSTATE_r]) // (re)name the current object
                    {
                        newob = myscreen->add_ob(ORDER_LIVING, FAMILY_ELF);
                        newob->setxy(windowx, windowy);
                        if (some_hit(windowx, windowy, newob, myscreen))
                        {
                            set_name(newob->collide_ob,myscreen);
                            levelchanged = 1;
                        }
                        myscreen->remove_ob(newob,0);
                    }  // end of info mode
                    else if (currentmode == OBJECT_MODE)
                    {
                        levelchanged = 1;
                        newob = myscreen->add_ob(myorder, forecount);
                        newob->setxy(windowx, windowy);
                        newob->team_num = currentteam;
                        newob->stats->level = currentlevel;
                        newob->dead = 0; // just in case
                        newob->collide_ob = 0;
                        if ( (grid_aligned==1) && some_hit(windowx, windowy, newob, myscreen))
                        {
                            if (mykeyboard[KEYSTATE_LCTRL] &&    // are we holding the erase?
                                    newob->collide_ob )                    // and hit a guy?
                            {
                                myscreen->remove_ob(newob->collide_ob,0);
                                while (mymouse[MOUSE_LEFT])
                                {
                                    mymouse = query_mouse();
                                }
                                levelchanged = 1;
                            } // end of deleting guy
                            if (newob)
                            {
                                myscreen->remove_ob(newob,0);
                                newob = NULL;
                            }
                        }  // end of failure to put guy
                        else if (grid_aligned == 2)
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
                        if (mykeyboard[KEYSTATE_LCTRL] && newob)
                        {
                            myscreen->remove_ob(newob,0);
                            newob = NULL;
                        }
                        //       while (mymouse[MOUSE_LEFT])
                        //         mymouse = query_mouse();
                    }  // end of putting a guy
                    if (currentmode == MAP_MODE)
                    {
                        windowx /= GRID_SIZE;  // get the map position ..
                        windowy /= GRID_SIZE;
                        // Set to our current selection
                        myscreen->grid[windowy*(myscreen->maxx)+windowx] = some_pix(backcount);
                        levelchanged = 1;
                        if (!mykeyboard[KEYSTATE_LCTRL]) // smooth a few squares, if not control
                        {
                            if (mysmoother)
                            {
                                delete mysmoother;
                                mysmoother = new smoother();
                                mysmoother->set_target(myscreen);
                            }
                            for (i=windowx-1; i <= windowx+1; i++)
                                for (j=windowy-1; j <=windowy+1; j++)
                                    if (i >= 0 && i < myscreen->maxx &&
                                            j >= 0 && j < myscreen->maxy)
                                        mysmoother->smooth(i, j);
                        }
                        else if (mysmoother) // update smoother anyway
                        {
                            delete mysmoother;
                            mysmoother = new smoother();
                            mysmoother->set_target(myscreen);
                        }
                        myscreen->viewob[0]->myradar->update();
                    }  // end of setting grid square
                } // end of main window
                //    if ( (mx >= PIX_LEFT) && (mx <= PIX_RIGHT) &&
                //        (my >= PIX_TOP) && (my <= PIX_BOTTOM) ) // grid menu
                if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
                {
                    //windowx = (mx - PIX_LEFT) / GRID_SIZE;
                    windowx = (mx-S_RIGHT) / GRID_SIZE;
                    windowy = (my - PIX_TOP) / GRID_SIZE;
                    backcount = backgrounds[ (windowx + ((windowy+rowsdown) * PIX_OVER))
                                             % (sizeof(backgrounds)/4)];
                    backcount %= NUM_BACKGROUNDS;
                    currentmode = MAP_MODE;
                } // end of background grid window
            }

		}      // end of left mouse button

		if (mymouse[MOUSE_RIGHT])      // cycle through things ...
		{
			event = 1;
			if (currentmode == OBJECT_MODE)
			{
				if (myorder == ORDER_LIVING)
					forecount = (forecount+1) % NUM_FAMILIES;
				else if (myorder == ORDER_TREASURE)
					forecount = (forecount+1) % (MAX_TREASURE+1);
				else if (myorder == ORDER_GENERATOR)
					forecount = (forecount+1) % 4;
				else if (myorder == ORDER_WEAPON)
					forecount = (forecount+1) % (FAMILY_DOOR+1); // use largest weapon
				else
					forecount = 0;
			} // end of if object mode
			if (currentmode == MAP_MODE)
			{
				windowx = mymouse[MOUSE_X] + myscreen->topx - myscreen->viewob[0]->xloc; // - S_LEFT
				windowx -= (windowx%GRID_SIZE);
				windowy = mymouse[MOUSE_Y] + myscreen->topy - myscreen->viewob[0]->yloc; // - S_UP
				windowy -= (windowy%GRID_SIZE);
				windowx /= GRID_SIZE;
				windowy /= GRID_SIZE;
				backcount = myscreen->grid[windowy*(myscreen->maxx)+windowx];
			}
			while (mymouse[MOUSE_RIGHT])
			{
				mymouse = query_mouse();
			}
		}

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
			event = 1;
		}
		
		// Redraw screen
		if (event)
		{
			//release_mouse();
			myscreen->redraw();
			for(set<SimpleButton*>::iterator e = menu_buttons.begin(); e != menu_buttons.end(); e++)
                (*e)->draw(myscreen, scentext);
            for(list<pair<SimpleButton*, set<SimpleButton*> > >::iterator e = current_menu.begin(); e != current_menu.end(); e++)
            {
                set<SimpleButton*>& s = e->second;
                for(set<SimpleButton*>::iterator f = s.begin(); f != s.end(); f++)
                    (*f)->draw(myscreen, scentext);
            }
			display_panel(myscreen);
			myscreen->refresh();
			//    display_panel(myscreen);
			//grab_mouse();
			myscreen->clearfontbuffer();
			SDL_Delay(10);
		}
		event = 0;

		if (mykeyboard[KEYSTATE_ESCAPE])
			quit_level_editor(0);
        
	    last_ticks = start_ticks;
	    start_ticks = SDL_GetTicks();

	}
	
	// Reset the screen position so it doesn't ruin the main menu
    set_screen_pos(myscreen, 0, 0);
    // Update the screen's position
    myscreen->redraw();
    // Clear the background
    myscreen->clearscreen();
    
	return OK;
}

Sint32 quit_level_editor(Sint32 num)
{
	// Delete scentext
	delete scentext;
	
	return num;
}

Sint32 display_panel(screen *myscreen)
{
	char message[50];
	Sint32 i, j; // for loops
	//   static Sint32 family=-1, hitpoints=-1, score=-1, act=-1;
	static Sint32 numobs = myscreen->numobs;
	static Sint32 lm = 245;
	Sint32 curline = 0;
	Sint32 whichback;
	static char treasures[20][NUM_FAMILIES] =
	    { "BLOOD", "DRUMSTICK", "GOLD", "SILVER",
	      "MAGIC", "INVIS", "INVULN", "FLIGHT",
	      "EXIT", "TELEPORTER", "LIFE GEM", "KEY", "SPEED", "CC",
	    };
	static char weapons[20][NUM_FAMILIES] =
	    { "KNIFE", "ROCK", "ARROW", "FIREBALL",
	      "TREE", "METEOR", "SPRINKLE", "BONE",
	      "BLOOD", "BLOB", "FIRE ARROW", "LIGHTNING",
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

	// Hide the mouse ..
	//release_mouse();

	// Draw the bounding box
	//myscreen->draw_dialog(lm-4, L_D(-1), 310, L_D(8), "Info");
	myscreen->draw_button(lm-4, L_D(-1)+4, 315, L_D(7)-2, 1, 1);

	// Show scenario and grid info
	strcpy(message, scen_name);
	uppercase(message);

	//myscreen->fastbox(lm, S_UP, 70, 8*5, 27, 1);
	scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);

	strcpy(message, grid_name);
	uppercase(message);
	scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);

	if (currentmode==MAP_MODE)
		scentext->write_xy(lm,L_D(curline++), "MODE: MAP", DARK_BLUE, 1);
	else if (currentmode==OBJECT_MODE)
		scentext->write_xy(lm,L_D(curline++), "MODE: OBS", DARK_BLUE, 1);

	// Get team number ..
	sprintf(message, "%d:", currentteam);
	if (myorder == ORDER_LIVING)
		strcat(message, livings[forecount]);
	else if (myorder == ORDER_GENERATOR)
		switch (forecount)      // who are we?
		{
			case FAMILY_TENT:
				strcat(message, "TENT");
				break;
			case FAMILY_TOWER:
				strcat(message, "TOWER");
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
	else if (myorder == ORDER_SPECIAL)
		strcat(message, "PLAYER");
	else if (myorder == ORDER_TREASURE)
		strcat(message, treasures[forecount]);
	else if (myorder == ORDER_WEAPON)
		strcat(message, weapons[forecount]);
	else
		strcat(message, "UNKNOWN");
	scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);

	// Level display
	sprintf(message, "LVL: %u", currentlevel);
	//myscreen->fastbox(lm,L_D(curline),55,7,27, 1);
	scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);

	// Is grid alignment on?
	//myscreen->fastbox(lm, L_D(curline),65, 7, 27, 1);
	if (grid_aligned==1)
		scentext->write_xy(lm, L_D(curline++), "ALIGN: ON", DARK_BLUE, 1);
	else if (grid_aligned==2)
		scentext->write_xy(lm, L_D(curline++), "ALIGN: STACK", DARK_BLUE, 1);
	else
		scentext->write_xy(lm, L_D(curline++), "ALIGN: OFF", DARK_BLUE, 1);

	numobs = myscreen->numobs;
	//myscreen->fastbox(lm,L_D(curline),55,7,27, 1);
	sprintf(message, "OB: %d", numobs);
	scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);

	// Show the background grid ..
	myscreen->putbuffer(lm+40, PIX_TOP-16, GRID_SIZE, GRID_SIZE,
	                    0, 0, 320, 200, myscreen->pixdata[backcount]+3);

	//   rowsdown = (NUM_BACKGROUNDS / 4) + 1;
	//   rowsdown = 0; // hack for now
	for (i=0; i < PIX_OVER; i++)
	{
		for (j=0; j < 4; j++)
		{
			//myscreen->back[i]->draw( S_RIGHT+(i*8), S_UP+100);
			//myscreen->back[0]->draw(64, 64);
			whichback = (i+(j+rowsdown)*4) % (sizeof(backgrounds)/4);
			myscreen->putbuffer(S_RIGHT+i*GRID_SIZE, PIX_TOP+j*GRID_SIZE,
			                    GRID_SIZE, GRID_SIZE,
			                    0, 0, 320, 200,
			                    myscreen->pixdata[ backgrounds[whichback] ]+3);
		}
	}
	myscreen->draw_box(S_RIGHT, PIX_TOP,
	                   S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, 0, 0, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	// Restore the mouse
	//grab_mouse();

	return 1;
}


void set_screen_pos(screen *myscreen, Sint32 x, Sint32 y)
{
	myscreen->topx = x;
	myscreen->topy = y;
	event = 1;
}


void remove_first_ob(screen *master)
{
	oblink  *here;

	here = master->oblist;

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

Sint32 save_map_file(char  * filename, screen *master)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes

	char numframes, x, y;
	//  char  *newpic;
	string fullpath(filename);
	SDL_RWops  *outfile;

	// Create the full pathname for the pixie file
	fullpath += ".pix";

	lowercase (fullpath);

	if ( (outfile = open_write_file("temp/pix/", fullpath.c_str())) == NULL )
	{
		master->draw_button(30, 30, 220, 60, 1, 1);
		scentext->write_xy(32, 32, "Error in saving map file", DARK_BLUE, 1);
		scentext->write_xy(32, 42, fullpath.c_str(), DARK_BLUE, 1);
		scentext->write_xy(32, 52, "Press SPACE to continue", DARK_BLUE, 1);
		master->buffer_to_screen(0, 0, 320, 200);
		while (!mykeyboard[KEYSTATE_SPACE])
			get_input_events(WAIT);
		return 0;
	}

	x = master->maxx;
	y = master->maxy;
	numframes = 1;
	SDL_RWwrite(outfile, &numframes, 1, 1);
	SDL_RWwrite(outfile, &x, 1, 1);
	SDL_RWwrite(outfile, &y, 1, 1);

	SDL_RWwrite(outfile, master->grid, 1, (x*y));

	SDL_RWclose(outfile);        // Close the data file
	return 1;

} // End of map-saving routine

Sint32 load_new_grid(screen *master)
{
	string tempstring;

	scentext->write_xy(52, 32, "Grid name: ", DARK_BLUE, 1);
    char* new_text = scentext->input_string(115, 32, 8, grid_name);
    if(new_text == NULL)
        new_text = grid_name;
	tempstring = new_text;
	
	if (tempstring.empty())
	{
		//buffers: our grid files are all lowercase...
		lowercase(tempstring);

		tempstring += grid_name;
	}

	//buffers: PORT: changed .PIX to .pix
	tempstring += ".pix";
	master->grid = read_pixie_file(tempstring.c_str());
	master->maxx = master->grid[1];
	master->maxy = master->grid[2];
	master->grid = master->grid + 3;
	
	//master->viewob[0]->myradar = new radar(master->viewob[0],
	//  master, 0);

	master->viewob[0]->myradar->start();
	master->viewob[0]->myradar->update();

	return 1;
}

Sint32 new_scenario_name()
{
	char tempstring[80];

	scentext->write_xy(52, 32, "Scenario name: ", DARK_BLUE, 1);
	char* new_text = scentext->input_string(135, 32, 8, scen_name);
	if(new_text == NULL)
        new_text = scen_name;
	strcpy(tempstring, new_text);
	
	if (strlen(tempstring))
	{
		strcpy(scen_name, tempstring);
		//buffers: all our files are lowercase....
		lowercase(scen_name);
	}

	return 1;
}

Sint32 new_grid_name()
{
	char tempstring[80];

	scentext->write_xy(52, 32, "Grid name: ", DARK_BLUE, 1);
	char* new_text = scentext->input_string(117, 32, 8, grid_name);
	if(new_text == NULL)
        new_text = grid_name;
	strcpy(tempstring, new_text);
	//NORMAL_KEYBOARD(SDLKf("%s", tempstring);)
	if (strlen(tempstring))
		strcpy(grid_name, tempstring);

	return 1;
}

void do_help(screen * myscreen)
{
	text *helptext = new text(myscreen);
	Sint32 lm = S_LEFT+4+43, tm=S_UP+15;  // left and top margins
	Sint32 lines = 0;

	// Zardus: new margins
	//myscreen->draw_button(S_LEFT+32,S_UP,S_RIGHT-1+16,S_DOWN-1,2, 1);
	myscreen->draw_button(S_LEFT+43,S_UP + 11,S_RIGHT-1+16,S_DOWN-1,2, 1);

	helptext->write_xy(lm + L_W(10), tm, "**HELP**", DARK_BLUE, 1);

	helptext->write_xy(lm, tm+L_H(++lines), "G : TOGGLE GRID ALIGNMENT", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "H : HELP", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "I : INFO ON CLICKED OBJECT", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "L : LOAD NEW SCEN OR GRID", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "M : TOGGLE OBJECT OR MAP MODE", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "N : NEW SCEN OR GRID NAME", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "O : TOGGLE LIVING/TENT/ETC", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "R : RENAME OBJECT", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "S : SAVE SCEN OR GRID", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "T : ENTER SCEN TEXT", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "* : TOGGLE SCENARIO OPTIONS", DARK_BLUE, 1);

	//lines +=1;
	helptext->write_xy(lm, tm+L_H(++lines), "ESC         : QUIT", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "LEFT CLICK  : PUT OB OR BACKGD", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "CTRL + LEFT : Remove Object", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "CTRL + D    : Remove all Obs", DARK_BLUE,1);

	helptext->write_xy(lm, tm+L_H(++lines), "RIGHT CLICK : CYCLE THRU OBS", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "KEYS 0-7    : CYCLE TEAM NUMBER", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "F5          : SMOOTH MAP TILES", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "[,]         : LOWER/RAISE OB LEVEL", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "?           : DISPLAY SCEN TEXT", DARK_BLUE,1);

	myscreen->buffer_to_screen(0, 0, 320, 200);

	wait_for_key(KEYSTATE_SPACE);

	delete helptext;
}

char some_pix(Sint32 whatback)
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

Sint32 save_scenario(char * filename, screen * master, char *gridname)
{
	Sint32 currentx, currenty;
	char temporder, tempfamily;
	char tempteam, tempfacing, tempcommand;
	short shortlevel;
	char filler[20] = "MSTRMSTRMSTRMSTR"; // for RESERVED
	SDL_RWops  *outfile;
	char temptext[10] = "FSS";
	char temp_grid[20] = "grid";  // default grid
	char temp_scen_type = master->scenario_type;
	oblink  * head = master->oblist;
	Sint32 listsize;
	Sint32 i;
	char temp_version = VERSION_NUM;
	char temp_filename[80];
	char numlines, tempwidth;
	char oneline[80];
	char tempname[12];
	char buffer[200];
	char scentitle[30];
	short temp_par;

	// Format of a scenario object list file is: (ver. 8)
	// 3-byte header: 'FSS'
	// 1-byte version number (from graph.h)
	// 8-byte grid file name
	// 30-byte scenario title
	// 1-byte scenario_type
	// 2-bytes par-value for level
	// 2-bytes (Sint32) = total objects to follow
	// List of n objects, each of 20-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte Sint32 xpos
	// 2-byte Sint32 ypos
	// 1-byte TEAM
	// 1-byte current facing
	// 1-byte current command
	// 1-byte level // this is 2 bytes in version 7+
	// 12-bytes name
	// ---
	// 10 bytes RESERVED
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line

	// Zardus: PORT: no longer need to put in scen/ in this part
	//strcpy(temp_filename, scen_directory);
	strcpy(temp_filename, filename);
	//buffers: PORT: changed .FSS to .fss
	strcat(temp_filename, ".fss");

	if ( (outfile = open_write_file("temp/scen/", temp_filename)) == NULL ) // open for write
	{
		//gotoxy(1, 22);
		Log("Could not open file for writing: %s\n", filename);

		master->draw_button(30, 30, 220, 60, 1, 1);
		sprintf(buffer, "Error in saving scenario file");
		scentext->write_xy(32, 32, buffer, DARK_BLUE, 1);
		sprintf(buffer, "%s", temp_filename);
		scentext->write_xy(32, 42, buffer, DARK_BLUE, 1);
		sprintf(buffer, "Press SPACE to continue");
		scentext->write_xy(32, 52, buffer, DARK_BLUE, 1);
		master->buffer_to_screen(0, 0, 320, 200);
		while (!mykeyboard[KEYSTATE_SPACE])
			get_input_events(WAIT);

		return 0;
	}

	// Write id header
	SDL_RWwrite(outfile, temptext, 3, 1);

	// Write version number
	SDL_RWwrite(outfile, &temp_version, 1, 1);

	// Write name of current grid...
	strcpy(temp_grid, gridname);  // Do NOT include extension

	// Set any chars under 8 not used to 0 ..
	for (i=strlen(temp_grid); i < 8; i++)
		temp_grid[i] = 0;
	SDL_RWwrite(outfile, temp_grid, 8, 1);

	// Write the scenario title, if it exists
	for (i=0; i < int(strlen(scentitle)); i++)
		scentitle[i] = 0;
	strcpy(scentitle, master->scenario_title);
	SDL_RWwrite(outfile, scentitle, 30, 1);

	// Write the scenario type info
	SDL_RWwrite(outfile, &temp_scen_type, 1, 1);

	// Write our par value (version 8+)
	temp_par = master->par_value;
	SDL_RWwrite(outfile, &temp_par, 2, 1);

	// Determine size of object list ...
	listsize = 0;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of oblist-size check

	// Also check the fx list ..
	head = master->fxlist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of fxlist-size check

	// And the weapon list ..
	head = master->weaplist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of weaplist-size check

	SDL_RWwrite(outfile, &listsize, 2, 1);

	// Okay, we've written header .. now dump the data ..
	head = master->oblist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
            {
                Log("Unexpected NULL object.\n");
                SDL_RWclose(outfile);
				return 0;  // Something wrong! Too few objects..
            }
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			//templevel = head->ob->stats->level;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
			SDL_RWwrite(outfile, &temporder, 1, 1);
			SDL_RWwrite(outfile, &tempfamily, 1, 1);
			SDL_RWwrite(outfile, &currentx, 2, 1);
			SDL_RWwrite(outfile, &currenty, 2, 1);
			SDL_RWwrite(outfile, &tempteam, 1, 1);
			SDL_RWwrite(outfile, &tempfacing, 1, 1);
			SDL_RWwrite(outfile, &tempcommand, 1, 1);
			SDL_RWwrite(outfile, &shortlevel, 2, 1);
			SDL_RWwrite(outfile, tempname, 12, 1);
			SDL_RWwrite(outfile, filler, 10, 1);
		}
		// Advance to next object ..
		head = head->next;
	}

	// Now dump the fxlist data ..
	head = master->fxlist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
            {
                Log("Unexpected NULL fx object.\n");
                SDL_RWclose(outfile);
				return 0;  // Something wrong! Too few objects..
            }
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			//templevel = head->ob->stats->level;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
			SDL_RWwrite(outfile, &temporder, 1, 1);
			SDL_RWwrite(outfile, &tempfamily, 1, 1);
			SDL_RWwrite(outfile, &currentx, 2, 1);
			SDL_RWwrite(outfile, &currenty, 2, 1);
			SDL_RWwrite(outfile, &tempteam, 1, 1);
			SDL_RWwrite(outfile, &tempfacing, 1, 1);
			SDL_RWwrite(outfile, &tempcommand, 1, 1);
			SDL_RWwrite(outfile, &shortlevel, 2, 1);
			SDL_RWwrite(outfile, tempname, 12, 1);
			SDL_RWwrite(outfile, filler, 10, 1);
		}
		// Advance to next object ..
		head = head->next;
	}

	// Now dump the weaplist data ..
	head = master->weaplist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
            {
                Log("Unexpected NULL weap object.\n");
                SDL_RWclose(outfile);
				return 0;  // Something wrong! Too few objects..
            }
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
			SDL_RWwrite(outfile, &temporder, 1, 1);
			SDL_RWwrite(outfile, &tempfamily, 1, 1);
			SDL_RWwrite(outfile, &currentx, 2, 1);
			SDL_RWwrite(outfile, &currenty, 2, 1);
			SDL_RWwrite(outfile, &tempteam, 1, 1);
			SDL_RWwrite(outfile, &tempfacing, 1, 1);
			SDL_RWwrite(outfile, &tempcommand, 1, 1);
			SDL_RWwrite(outfile, &shortlevel, 2, 1);
			SDL_RWwrite(outfile, tempname, 12, 1);
			SDL_RWwrite(outfile, filler, 10, 1);
		}
		// Advance to next object ..
		head = head->next;
	}

	numlines = master->scentextlines;
	//printf("saving %d lines\n", numlines);

	SDL_RWwrite(outfile, &numlines, 1, 1);
	for (i=0; i < numlines; i++)
	{
		strcpy(oneline, master->scentext[i]);
		tempwidth = strlen(oneline);
		SDL_RWwrite(outfile, &tempwidth, 1, 1);
		SDL_RWwrite(outfile, oneline, tempwidth, 1);
	}

	SDL_RWclose(outfile);
	
	Log("Scenario saved.\n");

	return 1;
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
walker * some_hit(Sint32 x, Sint32 y, walker  *ob, screen *screenp)
{
	oblink  *here;

	here = screenp->oblist;

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
	here = screenp->fxlist;
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
	here = screenp->weaplist;
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

// Display info about the target object ..
#define INFO_DOWN(x) (25+7*x)
void info_box(walker  *target,screen * myscreen)
{
	text *infotext = new text(myscreen);
	Sint32 linesdown = 0;
	Sint32 lm = 25+32;
	char message[80];
	treasure  *teleporter, *temp;

	static const char *orders[] =
	    { "LIVING", "WEAPON", "TREASURE", "GENERATOR", "FX", "SPECIAL", };
	static const char *livings[] =
	    { "SOLDIER", "ELF", "ARCHER", "MAGE",
	      "SKELETON", "CLERIC", "ELEMENTAL",
	      "FAERIE", "L-SLIME", "S-SLIME",
	      "M-SLIME", "THIEF", "GHOST",
	      "DRUID",
	    };
	static const char *treasures[] =
	    { "BLOODSTAIN", "DRUMSTICK: FOOD",
	      "GOLD BAR", "SILVER BAR",
	      "MAGIC POTION", "INVISIBILITY POTION",
	      "INVULNERABILITY POTION",
	      "FLIGHT POTION", "EXIT", "TELEPORTER",
	      "LIFE GEM", "KEY", "SPEED", "CC",
	    };

	release_mouse();
	myscreen->draw_button(20+32, 20, 220+32, 170, 1, 1);

	infotext->write_xy(lm, INFO_DOWN(linesdown++), "INFO TEXT", DARK_BLUE,1);
	linesdown++;

	if (strlen(target->stats->name)) // it has a name
	{
		sprintf(message, "Name    : %s", target->stats->name);
		infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);
	}

	sprintf(message, "Order   : %s", orders[(int)target->query_order()] );
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	if (target->query_order() == ORDER_LIVING)
		sprintf(message, "Family  : %s",
		        livings[(int)target->query_family()] );
	else if (target->query_order() == ORDER_TREASURE)
		sprintf(message, "Family  : %s",
		        treasures[(int)target->query_family()] );
	else
		sprintf(message, "Family  : %d", target->query_family());
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	sprintf(message, "Team Num: %d", target->team_num);
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE, 1);

	sprintf(message, "Position: %dx%d (%dx%d)", target->xpos, target->ypos,
	        (target->xpos/GRID_SIZE), (target->ypos/GRID_SIZE) );
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	if (target->query_order() == ORDER_TREASURE &&
	        target->query_family()== FAMILY_EXIT)
		sprintf(message, "Exits to: Level %d", target->stats->level);
	else if (target->query_order() == ORDER_TREASURE &&
	         target->query_family() == FAMILY_TELEPORTER)
	{
		sprintf(message, "Group # : %d", target->stats->level);
		infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);
		temp = (treasure  *) target;
		teleporter = (treasure  *) temp->find_teleport_target();
		if (!teleporter || teleporter == target)
			infotext->write_xy(lm, INFO_DOWN(linesdown++), "Goes to : Itself!", DARK_BLUE,1);
		else
		{
			sprintf(message, "Goes to : %dx%d (%dx%d)", teleporter->xpos,
			        teleporter->ypos, teleporter->xpos/GRID_SIZE, teleporter->ypos/GRID_SIZE);
		}
	}
	else
		sprintf(message, "Level   : %d", target->stats->level);
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	linesdown++;
	infotext->write_xy(lm, INFO_DOWN(linesdown++),
	                   "PRESS ESC TO EXIT", DARK_BLUE,1);

	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	// Wait for press and release of ESC
	while (!mykeyboard[KEYSTATE_ESCAPE])
		get_input_events(WAIT);
	while (mykeyboard[KEYSTATE_ESCAPE])
		get_input_events(WAIT);
}

// Set the stats->name value of a walker ..
void set_name(walker  *target, screen * master)
{
	char newname[11];
	char oldname[20];
	char buffer[200];

	//gotoxy(1,20);
	master->draw_button(30, 30, 220, 70, 1, 1);
	sprintf(buffer, "Renaming object");
	scentext->write_xy(32, 32, buffer, DARK_BLUE, 1);
	sprintf(buffer, "Enter '.' to not change.");
	scentext->write_xy(32, 42, buffer, DARK_BLUE, 1);

	if (strlen(target->stats->name))
	{
		sprintf(buffer, "Current name: %s", target->stats->name);
		strcpy(oldname, target->stats->name);
	}
	else
	{
		sprintf(buffer, "Current name: NOT SET");
		strcpy(oldname, "NOT SET");
	}
	scentext->write_xy(32, 52, buffer, DARK_BLUE, 1);
	scentext->write_xy(32, 62, "    New name:", DARK_BLUE, 1);

	master->buffer_to_screen(0, 0, 320, 200);

	// wait for key release
	while (mykeyboard[KEYSTATE_r])
		get_input_events(WAIT);
    
    char* new_text = scentext->input_string(115, 62, 9, oldname);
    if(new_text == NULL)
        new_text = oldname;
	strcpy(newname, new_text);
	newname[10] = 0;

	if (strcmp(newname, ".")) // didn't type '.'
		strcpy(target->stats->name, newname);

	info_box(target,master);

}

void scenario_options(screen *myscreen)
{
	static text opt_text(myscreen);
	Uint8 *opt_keys = query_keyboard();
	short lm, tm;
	char message[80];

	lm = 55;
	tm = 45;

#define OPT_LD(x) (short) (tm + (x*8) )
while (!opt_keys[KEYSTATE_ESCAPE])
        {


	myscreen->draw_button(lm-5, tm-5, 260, 160, 2, 1);

	opt_text.write_xy(lm, OPT_LD(0), "SCENARIO OPTIONS", DARK_BLUE, 1);

	if (myscreen->scenario_type & SCEN_TYPE_CAN_EXIT)
		opt_text.write_xy(lm, OPT_LD(2), "Can Always Exit (E)         : Yes", DARK_BLUE, 1);
	else
		opt_text.write_xy(lm, OPT_LD(2), "Can Always Exit (E)         : No ", DARK_BLUE, 1);

	if (myscreen->scenario_type & SCEN_TYPE_GEN_EXIT)
		opt_text.write_xy(lm, OPT_LD(3), " Kill Generators to Exit (G): Yes", DARK_BLUE, 1);
	else
		opt_text.write_xy(lm, OPT_LD(3), " Kill Generators to Exit (G): No ", DARK_BLUE, 1);

	if (myscreen->scenario_type & SCEN_TYPE_SAVE_ALL)
		opt_text.write_xy(lm, OPT_LD(4), " Must Save Named NPC's (N)  : Yes", DARK_BLUE, 1);
	else
		opt_text.write_xy(lm, OPT_LD(4), " Must Save Named NPC's (N)  : No ", DARK_BLUE, 1);

	sprintf(message, " Level Par Value (+,-)      : %d ", myscreen->par_value);
	opt_text.write_xy(lm, OPT_LD(5), message, DARK_BLUE, 1);


	myscreen->buffer_to_screen(0, 0, 320, 200);

	get_input_events(WAIT);
	if (opt_keys[KEYSTATE_e]) // toggle exit mode
	{
		if (myscreen->scenario_type & SCEN_TYPE_CAN_EXIT) // already set
			myscreen->scenario_type -= SCEN_TYPE_CAN_EXIT;
		else
			myscreen->scenario_type += SCEN_TYPE_CAN_EXIT;
	}
	if (opt_keys[KEYSTATE_g]) // toggle exit mode -- generators
	{
		if (myscreen->scenario_type & SCEN_TYPE_GEN_EXIT) // already set
			myscreen->scenario_type -= SCEN_TYPE_GEN_EXIT;
		else
			myscreen->scenario_type += SCEN_TYPE_GEN_EXIT;
	}
	if (opt_keys[KEYSTATE_n]) // toggle fail mode -- named guys
	{
		if (myscreen->scenario_type & SCEN_TYPE_SAVE_ALL) // already set
			myscreen->scenario_type -= SCEN_TYPE_SAVE_ALL;
		else
			myscreen->scenario_type += SCEN_TYPE_SAVE_ALL;
	}
	if (opt_keys[KEYSTATE_KP_MINUS]) // lower the par value
	{
		if (myscreen->par_value > 1)
			myscreen->par_value--;
	}
	if (opt_keys[KEYSTATE_KP_PLUS]) // raise the par value
	{
		myscreen->par_value++;
	}
}

while (opt_keys[KEYSTATE_ESCAPE])
	get_input_events(WAIT); // wait for key release

	myscreen->clearfontbuffer(lm-5, tm-5, 260-(lm-5), 160-(tm-5));
}

// Set an object's facing ..
void set_facing(walker *target, screen *myscreen)
{
	Uint8 *setkeys = query_keyboard();

	if (target)
		target = target;  // dummy code

	myscreen->draw_dialog(100, 50, 220, 170, "Set Facing");
	myscreen->buffer_to_screen(0, 0, 320, 200);

	while (setkeys[KEYSTATE_f])
		get_input_events(WAIT);

}


// Load a grid or scenario ..
Sint32 do_load(screen *ascreen)
{
	Sint32 i;
	text *loadtext = new text(ascreen);
	char buffer[200],temp[200];

	event = 1;
	
	// Load scenario
	{
		ascreen->draw_button(50, 30, 200, 40, 1, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		new_scenario_name();
		ascreen->clearfontbuffer(50, 30, 150, 10);
		loadtext->write_xy(52, 32, "Loading scenario..", DARK_BLUE, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		remove_all_objects(ascreen);  // kill   current obs
		for (i=0; i < 60; i ++)
			ascreen->scentext[i][0] = 0;
		short load_result = load_scenario(scen_name, ascreen);
		ascreen->viewob[0]->myradar->start();
		ascreen->viewob[0]->myradar->update();
		strcpy(grid_name, query_my_map_name());
		while (mykeyboard[KEYSTATE_s])
			//buffers: dumbcount++;
			get_input_events(WAIT);
        if(load_result > 0)
        {
            //buffers: PORT: stricmp isn't compiling... need to find replacement func
            //buffers: workaround: copy scenario_title to new buffer and make it all
            //buffers: lowercase and then compare it to lowercase 'none'
            strcpy(temp,ascreen->scenario_title);
            lowercase(temp);
            if (strlen(ascreen->scenario_title) &&
                    strcmp(temp, "none") )
            {
                ascreen->draw_button(10, 30, 238, 51, 1, 1);
                ascreen->clearfontbuffer(10, 30, 228, 21);
                sprintf(buffer, "Loaded: %s", ascreen->scenario_title);
                loadtext->write_xy(12, 33, buffer, DARK_BLUE, 1);
                loadtext->write_xy(12, 43, "Press space to continue", RED, 1);
                ascreen->buffer_to_screen(0, 0, 320, 200);
                while (!mykeyboard[KEYSTATE_SPACE])
                    get_input_events(WAIT);
            }
        }
	} // end load scenario

	delete loadtext;
	levelchanged = 0;
	return 1;
}

Sint32 do_save(screen *ascreen)  // save a scenario or grid
{
	text *savetext = new text(ascreen);
	Sint32 result = 1;

	event = 1;
	
	// save scenario
	{
		while (mykeyboard[KEYSTATE_s])
			get_input_events(WAIT);

		// Allow us to set the title, if desired
		ascreen->draw_button(20, 30, 235, 41, 1, 1);
		savetext->write_xy(22, 33, "Title:", DARK_BLUE, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		char* new_name = savetext->input_string(58, 33, 29, ascreen->scenario_title);
		if(new_name == NULL)
        {
            Log("Save canceled.\n");
            ascreen->clearfontbuffer(20, 30, 215, 15);
            savetext->write_xy(52, 33, "Save canceled.");
            ascreen->buffer_to_screen(0, 0, 320, 200);
            result = 0;
        }
        else
        {
            strcpy(ascreen->scenario_title, new_name);

            ascreen->clearfontbuffer(20, 30, 215, 15);
            savetext->write_xy(52, 33, "Saving scenario..");
            ascreen->buffer_to_screen(0, 0, 320, 200);
            
            if(unpack_campaign(ascreen->current_campaign))
            {
                // Save the map file ..
                if (!save_map_file(grid_name, ascreen) )
                {
                    Log("Save failed: Could not save grid.\n");
                    result = 0;
                }
                else
                {
                    save_scenario(scen_name, ascreen, grid_name);
                    
                    // Unmount campaign while it is changed
                    unmount_campaign_package(ascreen->current_campaign);
                    
                    if(!repack_campaign(ascreen->current_campaign))
                    {
                        Log("Save failed: Could not repack campaign: %s\n", ascreen->current_campaign);
                        result = 0;
                    }
                    
                    // Remount the new campaign package
                    mount_campaign_package(ascreen->current_campaign);
                }
            }
            else
            {
                Log("Save failed: Could not unpack campaign: %s\n", ascreen->current_campaign);
                result = 0;
            }
            cleanup_unpacked_campaign();

            ascreen->clearfontbuffer();
            clear_keyboard();
        }
	} // end of save scenario

	delete savetext;

    // If it saved, then it is not changed anymore.
	if(result)
		levelchanged = 0;
	return result;
}
