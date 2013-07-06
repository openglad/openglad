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

#include "level_picker.h"
#include "level_data.h"
#include "radar.h"
#include "walker.h"
#include "stats.h"
#include "text.h"
#include "guy.h"

#include <list>
#include <string>

#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>

using namespace std;


#define MAX_TEAM_SIZE 24 //max # of guys on a team
extern Sint32 *mymouse;





void getLevelStats(screen* screenp, int* max_enemy_level, float* average_enemy_level, int* num_enemies, float* difficulty, list<int>& exits)
{
    int num = 0;
    int level_sum = 0;
    int difficulty_sum = 0;
    int difficulty_sum_friends = 0;
    int diff_per_level = 3;
    
    int max_level = 0;
    exits.clear();
    
    // Go through objects
    oblink* fx = screenp->level_data.oblist;
	while(fx)
	{
		if(fx->ob)
		{
		    walker* ob = fx->ob;
		    switch(ob->query_order())
		    {
		        case ORDER_LIVING:
                    if(ob->team_num != 0)
                    {
                        num++;
                        level_sum += ob->stats->level;
                        difficulty_sum += diff_per_level*ob->stats->level;
                        if(ob->stats->level > max_level)
                            max_level = ob->stats->level;
                    }
                    else
                    {
                        difficulty_sum_friends += diff_per_level*ob->stats->level;
                    }
                break;
		    }
		}
		
		fx = fx->next;
	}
	
	// Go through effects
	fx = screenp->level_data.fxlist;
	while(fx)
	{
		if(fx->ob)
		{
		    walker* ob = fx->ob;
		    switch(ob->query_order())
		    {
                case ORDER_TREASURE:
                    if(ob->query_family() == FAMILY_EXIT)
                    {
                        exits.push_back(ob->stats->level);
                    }
                break;
		    }
		}
		
		fx = fx->next;
	}
	
	*num_enemies = num;
	*max_enemy_level = max_level;
	if(num == 0)
        *average_enemy_level = 0;
    else
        *average_enemy_level = level_sum/float(num);
    
    *difficulty = difficulty_sum - difficulty_sum_friends;
    
    exits.sort();
    exits.unique();
}


bool isDir(const string& filename)
{
    struct stat status;
    stat(filename.c_str(), &status);

    return (status.st_mode & S_IFDIR);
}


bool sort_scen(const string& first, const string& second)
{
    string s1;
    string s1num;
    string s2;
    string s2num;
    
    bool gotNum = false;
    for(string::const_iterator e = first.begin(); e != first.end(); e++)
    {
        if(!gotNum && isalpha(*e))
            s1 += *e;
        else
            s1num += *e;
    }
    
    gotNum = false;
    for(string::const_iterator e = second.begin(); e != second.end(); e++)
    {
        if(!gotNum && isalpha(*e))
            s2 += *e;
        else
            s2num += *e;
    }
    
    if(s1 == s2)
        return (atoi(s1num.c_str()) < atoi(s2num.c_str()));
    return (first < second);
}


class BrowserEntry
{
    public:
    
    SDL_Rect mapAreas;
    radar* radars;
    int max_enemy_level;
    float average_enemy_level;
    int num_enemies;
    float difficulty;
    oblink* oblist;
    oblink* fxlist;
    oblink* weaplist;
    char* level_name;
    list<int> exits;
    char scentext[80][80];                         // Array to hold scenario information
    char scentextlines;                    // How many lines of text in scenario info
    
    BrowserEntry(screen* screenp, int index, int scen_num);
    ~BrowserEntry();
    
    void draw(screen* screenp, text* loadtext, int scen_num);
};

void remove_all_objects(screen *master)
{
	oblink *fx = master->level_data.fxlist;

	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = master->level_data.oblist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = master->level_data.weaplist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	master->level_data.numobs = 0;
} // end remove_all_objects

BrowserEntry::BrowserEntry(screen* screenp, int index, int scen_num)
{
    // Clear the level so we can load the next one
    screenp->level_data.clear();
    screenp->level_data.id = scen_num;
    screenp->level_data.load();
    
    radar* r = new radar(NULL, screenp, 0);
    r->start();
    radars = r;
    

    int w = radars->xview;
    int h = radars->yview;
    
    mapAreas.w = w;
    mapAreas.h = h;
    mapAreas.x = 10;
    mapAreas.y = 5 + (53 + 12)*index;
    
    r->xloc = mapAreas.x + mapAreas.w/2 - w/2;
    r->yloc = mapAreas.y + 10;
    
    
    getLevelStats(screenp, &max_enemy_level, &average_enemy_level, &num_enemies, &difficulty, exits);
    
    // Store this level's objects
    oblist = screenp->level_data.oblist;
    screenp->level_data.oblist = NULL;
    fxlist = screenp->level_data.fxlist;
    screenp->level_data.fxlist = NULL;
    weaplist = screenp->level_data.weaplist;
    screenp->level_data.weaplist = NULL;
    level_name = new char[24];
    strncpy(level_name, screenp->level_data.title.c_str(), 23);
    if(level_name[20] != '\0')
    {
        level_name[20] = '.';
        level_name[21] = '.';
        level_name[22] = '.';
        level_name[23] = '\0';
    }
    
    scentextlines = screenp->level_data.description.size();
    int i = 0;
    for(std::list<std::string>::iterator e = screenp->level_data.description.begin(); e != screenp->level_data.description.end(); e++)
    {
        strncpy(scentext[i], e->c_str(), 80);
        i++;
        if(i >= 80)
            break;
    }
}


BrowserEntry::~BrowserEntry()
{
    // Delete all objects
    oblink *fx = fxlist;

	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		fx = fx->next;
	}

	fx = oblist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		fx = fx->next;
	}

	fx = weaplist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		fx = fx->next;
	}
    
    delete radars;
    delete[] level_name;
}

void BrowserEntry::draw(screen* screenp, text* loadtext, int scen_num)
{
    // Set the current objects
    screenp->level_data.oblist = oblist;
    screenp->level_data.fxlist = fxlist;
    screenp->level_data.weaplist = weaplist;
    
    int x = radars->xloc;
    int y = radars->yloc;
    int w = radars->xview;
    int h = radars->yview;
    screenp->draw_button(x - 2, y - 2, x + w + 2, y + h + 2, 1, 1);
    // Draw radar
    radars->draw();
    loadtext->write_xy(mapAreas.x, mapAreas.y, level_name, DARK_BLUE, 1);
    
    char buf[30];
    snprintf(buf, 30, "ID: %d", scen_num);
    loadtext->write_xy(x + w + 5, y, buf, WHITE, 1);
    snprintf(buf, 30, "Enemies: %d", num_enemies);
    loadtext->write_xy(x + w + 5, y + 8, buf, WHITE, 1);
    snprintf(buf, 30, "Max level: %d", max_enemy_level);
    loadtext->write_xy(x + w + 5, y + 16, buf, WHITE, 1);
    snprintf(buf, 30, "Avg level: %.1f", average_enemy_level);
    loadtext->write_xy(x + w + 5, y + 24, buf, WHITE, 1);
    snprintf(buf, 30, "Difficulty: %.0f", difficulty);
    loadtext->write_xy(x + w + 5, y + 32, buf, RED, 1);
    
    if(exits.size() > 0)
    {
        snprintf(buf, 30, "Exits: ");
        bool first = true;
        for(list<int>::iterator e = exits.begin(); e != exits.end(); e++)
        {
            char buf2[10];
            snprintf(buf2, 10, (first? "%d" : ", %d"), *e);
            strncat(buf, buf2, 30);
            first = false;
        }
        if(strlen(buf) > 19)
        {
            buf[17] = '.';
            buf[18] = '.';
            buf[19] = '.';
            buf[20] = '\0';
        }
        loadtext->write_xy(x + w + 5, y + 40, buf, WHITE, 1);
    }
}


#define NUM_BROWSE_RADARS 3

// Load a grid or scenario ..
int pick_level(screen *screenp)
{
    int result = screenp->level_data.id;
    
    const Uint8* mykeyboard = query_keyboard();
    
    // Clear all objects from the current level
    screenp->level_data.clear();
    
	text* loadtext = new text(screenp);
    
    // Here are the browser variables
    BrowserEntry* entries[NUM_BROWSE_RADARS];
    
    std::vector<int> level_list = list_levels_v();
    int level_list_length = level_list.size();
    
    // This indexes into the level_list.
    int current_level_index = 0;
    
    // Figure out the list index for the current scen_level, so we can jump straight there.
    for(int i = 0; i < level_list_length; i++)
    {
        if(level_list[i] == screenp->level_data.id)
            current_level_index = i;
    }
    
    // Load the radars (minimaps)
    for(int i = 0; i < NUM_BROWSE_RADARS; i++)
    {
        if(i < level_list_length)
            entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
        else
            entries[i] = NULL;
    }
    
    int selected_entry = -1;
    
    // Figure out how good the player's army is
    int army_power = 0;
	for(int i=0; i<MAX_TEAM_SIZE; i++)
	{
		if (myscreen->save_data.team_list[i])
		{
		    army_power += 3*myscreen->save_data.team_list[i]->level;
		}
	}
    
    // Buttons
    Sint16 screenW = 320;
    Sint16 screenH = 200;
    SDL_Rect prev = {Sint16(screenW - 150), 20, 30, 10};
    SDL_Rect next = {Sint16(screenW - 150), Sint16(screenH - 50), 30, 10};
    SDL_Rect descbox = {Sint16(prev.x - 40), Sint16(prev.y + 15), 185, Uint16(next.y - 10 - (prev.y + prev.h))};
    
    SDL_Rect choose = {Sint16(screenW - 50), Sint16(screenH - 30), 30, 10};
    SDL_Rect cancel = {Sint16(screenW - 100), Sint16(screenH - 30), 38, 10};
    
    bool done = false;
	while (!done)
	{
		// Reset the timer count to zero ...
		reset_timer();

		if (screenp->end)
			break;

		// Get keys and stuff
		get_input_events(POLL);
		
		// Quit if 'q' is pressed
		if(mykeyboard[KEYSTATE_q])
            done = true;
            
		if(mykeyboard[KEYSTATE_UP])
		{
		    // Scroll up
		    if(current_level_index > 0)
		    {
                selected_entry = -1;
		    
                current_level_index--;
                
                for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                {
                    if(i < level_list_length)
                    {
                        delete entries[i];
                        entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                    }
                }
		    }
            while (mykeyboard[KEYSTATE_UP])
                get_input_events(WAIT);
		}
		if(mykeyboard[KEYSTATE_DOWN])
		{
		    // Scroll down
		    if(current_level_index < level_list_length - NUM_BROWSE_RADARS)
		    {
                selected_entry = -1;
		    
                current_level_index++;
                
                for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                {
                    if(i < level_list_length)
                    {
                        delete entries[i];
                        entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                    }
                }
		    }
            while (mykeyboard[KEYSTATE_DOWN])
                get_input_events(WAIT);
		}
		
		// Mouse stuff ..
		mymouse = query_mouse();
		if (mymouse[MOUSE_LEFT])       // put or remove the current guy
		{
		    while(mymouse[MOUSE_LEFT])
                get_input_events(WAIT);
		    
			int mx = mymouse[MOUSE_X];
			int my = mymouse[MOUSE_Y];
			
		    
            
            // Prev
            if(prev.x <= mx && mx <= prev.x + prev.w
               && prev.y <= my && my <= prev.y + prev.h)
               {
                    if(current_level_index > 0)
                    {
                        selected_entry = -1;
                        current_level_index--;
                        
                        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                        {
                            if(i < level_list_length)
                            {
                                delete entries[i];
                                entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                            }
                        }
                    }
               }
            // Next
            else if(next.x <= mx && mx <= next.x + next.w
               && next.y <= my && my <= next.y + next.h)
               {
                    if(current_level_index < level_list_length - NUM_BROWSE_RADARS)
                    {
                        selected_entry = -1;
                        current_level_index++;
                        
                        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                        {
                            if(i < level_list_length)
                            {
                                delete entries[i];
                                entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                            }
                        }
                    }
               }
            // Choose
			else if(choose.x <= mx && mx <= choose.x + choose.w
               && choose.y <= my && my <= choose.y + choose.h)
               {
                   if(selected_entry != -1)
                   {
                       result = level_list[current_level_index + selected_entry];
                       done = true;
                       break;
                   }
               }
            // Cancel
			else if(cancel.x <= mx && mx <= cancel.x + cancel.w
               && cancel.y <= my && my <= cancel.y + cancel.h)
               {
                   done = true;
                   break;
               }
			else
			{
                selected_entry = -1;
                // Select
                for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                {
                    if(i < level_list_length && entries[i] != NULL)
                    {
                        int x = entries[i]->radars->xloc;
                        int y = entries[i]->radars->yloc;
                        int w = entries[i]->radars->xview;
                        int h = entries[i]->radars->yview;
                        SDL_Rect b = {Sint16(x - 2), Sint16(y - 2), Uint16(w + 2), Uint16(h + 2)};
                        if(b.x <= mx && mx <= b.x+b.w
                           && b.y <= my && my <= b.y+b.h)
                           {
                               selected_entry = i;
                               break;
                           }
                    }
                }
			}
		}
		
        
        // Draw
        screenp->clearscreen();
        
        char buf[20];
        snprintf(buf, 20, "Army power: %d", army_power);
        loadtext->write_xy(prev.x + 50, prev.y + 2, buf, RED, 1);
        
        screenp->draw_button(prev.x, prev.y, prev.x + prev.w, prev.y + prev.h, 1, 1);
        loadtext->write_xy(prev.x + 2, prev.y + 2, "Prev", DARK_BLUE, 1);
        screenp->draw_button(next.x, next.y, next.x + next.w, next.y + next.h, 1, 1);
        loadtext->write_xy(next.x + 2, next.y + 2, "Next", DARK_BLUE, 1);
        if(selected_entry != -1 && selected_entry < level_list_length && entries[selected_entry] != NULL)
        {
            screenp->draw_button(choose.x, choose.y, choose.x + choose.w, choose.y + choose.h, 1, 1);
            loadtext->write_xy(choose.x + 9, choose.y + 2, "OK", DARK_GREEN, 1);
            loadtext->write_xy(next.x, choose.y + 20, entries[selected_entry]->level_name, DARK_GREEN, 1);
        }
        screenp->draw_button(cancel.x, cancel.y, cancel.x + cancel.w, cancel.y + cancel.h, 1, 1);
        loadtext->write_xy(cancel.x + 2, cancel.y + 2, "Cancel", RED, 1);
        
        if(selected_entry != -1)
        {
            int i = selected_entry;
            if(i < level_list_length && entries[i] != NULL)
            {
                int x = entries[i]->radars->xloc - 4;
                int y = entries[i]->radars->yloc - 4;
                int w = entries[i]->radars->xview + 8;
                int h = entries[i]->radars->yview + 8;
                screenp->draw_box(x, y, x + w, y + h, DARK_BLUE, 1, 1);
            }
        }
        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
        {
            if(i < level_list_length && entries[i] != NULL)
                entries[i]->draw(screenp, loadtext, level_list[current_level_index + i]);
        }
        
        // Description
        if(selected_entry != -1 && selected_entry < level_list_length && entries[selected_entry] != NULL)
        {
            screenp->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
            for(int i = 0; i < entries[selected_entry]->scentextlines; i++)
            {
                if(prev.y + 20 + 10*i+1 > descbox.y + descbox.h)
                    break;
                loadtext->write_xy(descbox.x, descbox.y + 10*i+1, entries[selected_entry]->scentext[i], BLACK, 1);
            }
        }
        
        
		screenp->buffer_to_screen(0, 0, 320, 200);
		SDL_Delay(10);
	}
	
    while (mykeyboard[KEYSTATE_q])
        get_input_events(WAIT);
	
    for(int i = 0; i < NUM_BROWSE_RADARS; i++)
    {
        delete entries[i];
    }
    
    delete loadtext;
    
    
    // Clear all objects from the current level
    screenp->level_data.clear();
    
	return result;
}
