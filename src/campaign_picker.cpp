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

#include "campaign_picker.h"
#include "io.h"
#include "yam.h"
#include "pixie.h"
#include "text.h"
#include "guy.h"
#include "screen.h"
#include "button.h"
#include <vector>
#include <string>

extern Sint32 *mymouse;

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(text* mytext, const std::string& message, std::string& result);

#define OG_OK 4
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);

int toInt(const std::string& s)
{
    return atoi(s.c_str());
}

// Unmounts old campaign, mounts new one, and returns the current level (scenario) that the player is on
int load_campaign(const std::string& campaign, std::map<std::string, int>& current_levels, int first_level)
{
    std::string old_campaign = get_mounted_campaign();
    if(old_campaign != campaign)
    {
        if(!unmount_campaign_package(old_campaign))
        {
            Log("Failed to unmount campaign %s, which caused loading %s to fail.\n", old_campaign.c_str(), campaign.c_str());
            return -3;
        }
        
        if(!mount_campaign_package(campaign))
            return -2;
    }
    
    std::map<std::string, int>::const_iterator g = current_levels.find(campaign);
    if(g != current_levels.end())
        return g->second;
    else
        return first_level;
}

class CampaignEntry
{
public:
    
    std::string id;
    std::string title;
    float rating;
    std::string version;
    std::string authors;
    std::string contributors;
    std::string description;
    int suggested_power;
    int first_level;
    
    int num_levels;
    
    PixieData icondata;
    pixie* icon;
    
    // Player-specific
    int num_levels_completed;

    CampaignEntry(screen* screenp, const std::string& id, int num_levels_completed);
    ~CampaignEntry();
    
    void draw(screen* screenp, const SDL_Rect& area, text* loadtext, int team_power);
};

CampaignEntry::CampaignEntry(screen* screenp, const std::string& id, int num_levels_completed)
    : id(id), title("Untitled"), rating(0.0f), version("1.0"), description("No description."), suggested_power(0), first_level(1), num_levels(0), icon(NULL), num_levels_completed(num_levels_completed)
{
    // Load the campaign data from <user_data>/scen/<id>.glad
    if(mount_campaign_package(id))
    {
        SDL_RWops* rwops = open_read_file("campaign.yaml");
        
        Yam yam;
        yam.set_input(rwops_read_handler, rwops);
        
        while(yam.parse_next() == Yam::OK)
        {
            switch(yam.event.type)
            {
                case Yam::PAIR:
                    if(strcmp(yam.event.scalar, "title") == 0)
                        title = yam.event.value;
                    else if(strcmp(yam.event.scalar, "version") == 0)
                        version = yam.event.value;
                    else if(strcmp(yam.event.scalar, "authors") == 0)
                        authors = yam.event.value;
                    else if(strcmp(yam.event.scalar, "contributors") == 0)
                        contributors = yam.event.value;
                    else if(strcmp(yam.event.scalar, "description") == 0)
                        description = yam.event.value;
                    else if(strcmp(yam.event.scalar, "suggested_power") == 0)
                        suggested_power = toInt(yam.event.value);
                    else if(strcmp(yam.event.scalar, "first_level") == 0)
                        first_level = toInt(yam.event.value);
                break;
                default:
                    break;
            }
        }
        
        yam.close_input();
        SDL_RWclose(rwops);
        
        // TODO: Get rating from website
        rating = 0.0f;
        
        std::string icon_file = "icon.pix";
        icondata = read_pixie_file(icon_file.c_str());
        if(icondata.valid())
            icon = new pixie(icondata, screenp);
        
        // Count the number of levels
        std::list<int> levels = list_levels();
        num_levels = levels.size();
        
        unmount_campaign_package(id);
    }
}

CampaignEntry::~CampaignEntry()
{
	delete icon;
	icondata.free();
}

void CampaignEntry::draw(screen* screenp, const SDL_Rect& area, text* loadtext, int team_power)
{
    int x = area.x;
    int y = area.y;
    int w = area.w;
    int h = area.h;

    // Print title
    char buf[60];
    snprintf(buf, 30, "%s", title.c_str());
    loadtext->write_xy(x + w/2 - title.size()*3, y - 22, buf, WHITE, 1);
    
    // Rating stars
    std::string rating_text = "";
    for(int i = 0; i < int(rating); i++)
    {
        rating_text += '*';
    }
    snprintf(buf, 30, "%s", rating_text.c_str());
    loadtext->write_xy(x + w/2 - rating_text.size()*3, y - 14, buf, WHITE, 1);
    
    // Print version
    snprintf(buf, 30, "v%s", version.c_str());
    if(rating_text.size() > 0)
        loadtext->write_xy(x + w/2 + rating_text.size()*3 + 6, y - 14, buf, WHITE, 1);
    else
        loadtext->write_xy(x + w/2 - strlen(buf)*3, y - 14, buf, WHITE, 1);
    
    // Draw icon button
    screenp->draw_button(x - 2, y - 2, x + w + 2, y + h + 2, 1, 1);
    // Draw icon
	icon->drawMix(x, y, screenp->viewob[0]);
	y += h + 4;
	
	// Print suggested power
	if(team_power >= 0)
    {
        char buf2[30];
        snprintf(buf, 30, "Your Power: %d", team_power);
        if(suggested_power > 0)
            snprintf(buf2, 30, ", Suggested Power: %d", suggested_power);
        else
            buf2[0] = '\0';
        
        int len = strlen(buf);
        int len2 = strlen(buf2);
        loadtext->write_xy(x + w/2 - (len + len2)*3, y, buf, LIGHT_GREEN, 1);
        loadtext->write_xy(x + w/2 - (len + len2)*3 + len*6, y, buf2, (team_power >= suggested_power? LIGHT_GREEN : RED), 1);
    }
    else
    {
        if(suggested_power > 0)
            snprintf(buf, 30, "Suggested Power: %d", suggested_power);
        else
            buf[0] = '\0';
        
        int len = strlen(buf);
        loadtext->write_xy(x + w/2 - (len)*3, y, buf, LIGHT_GREEN, 1);
    }
    y += 8;
    
    // Print completion progress
    if(num_levels_completed < 0)
        snprintf(buf, 30, "%d level%s", num_levels, (num_levels == 1? "" : "s"));
    else
        snprintf(buf, 30, "%d out of %d completed", num_levels_completed, num_levels);
    loadtext->write_xy(x + w/2 - strlen(buf)*3, y, buf, WHITE, 1);
    y += 8;
    
    // Print authors
    if(authors.size() > 0)
    {
        snprintf(buf, 30, "By %s", authors.c_str());
        loadtext->write_xy(x + w/2 - strlen(buf)*3, y, buf, WHITE, 1);
    }
    
    // Draw description box
    SDL_Rect descbox = {160 - 225/2, Sint16(area.y + area.h + 35), 225, 60};
    screenp->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
    
    // Print description
    std::string desc = description;
    int j = 10;
    while(desc.size() > 0)
    {
        if(j + 10 > descbox.h)
            break;
        size_t pos = desc.find_first_of('\n');
        std::string line = desc.substr(0, pos);
        loadtext->write_xy(descbox.x + 5, descbox.y + j, line.c_str(), BLACK, 1);
        if(pos == std::string::npos)
            break;
        desc = desc.substr(pos+1, std::string::npos);
        j += 10;
    }
    y = descbox.y + descbox.h + 2;
    
    // Print contributors
    if(contributors.size() > 0)
    {
        snprintf(buf, 60, "Thanks to %s", contributors.c_str());
        loadtext->write_xy(x + w/2 - strlen(buf)*3, y, buf, WHITE, 1);
        y += 10;
    }
    
    snprintf(buf, 60, "%s", id.c_str());
    loadtext->write_xy(x + w/2 - strlen(buf)*3, y, buf, WHITE, 1);
    
}




CampaignResult pick_campaign(screen* screenp, SaveData* save_data, bool enable_delete)
{
    std::string old_campaign_id = get_mounted_campaign();
    CampaignEntry* result = NULL;
    CampaignResult ret_value;
    
    text* loadtext = new text(screenp);
    
    unmount_campaign_package(old_campaign_id);

    // Here are the browser variables
    std::vector<CampaignEntry*> entries;
    
    unsigned int current_campaign_index = 0;
    
    // Load campaigns
    std::list<std::string> campaign_ids = list_campaigns();
    int i = 0;
    for(std::list<std::string>::iterator e = campaign_ids.begin(); e != campaign_ids.end(); e++)
    {
        int num_completed = -1;
        if(save_data != NULL)
            num_completed = save_data->get_num_levels_completed(*e);
        entries.push_back(new CampaignEntry(screenp, *e, num_completed));
        
        if(*e == old_campaign_id)
            current_campaign_index = i;
        
        i++;
    }

    // Figure out how good the player's army is
    int army_power = -1;
    if(save_data != NULL)
    {
        army_power = 0;
        for(int i=0; i<MAX_TEAM_SIZE; i++)
        {
            if (save_data->team_list[i])
            {
                army_power += 3*save_data->team_list[i]->level;
            }
        }
    }
    
    // Campaign icon positioning
    SDL_Rect area;
    area.x = 160 - 16;
    area.y = 15 + 20;
    area.w = 32;
    area.h = 32;

    // Buttons
    Sint16 screenW = 320;
    Sint16 screenH = 200;
    SDL_Rect prev = {Sint16(area.x - 30 - 20), Sint16(area.y), 30, 10};
    SDL_Rect next = {Sint16(area.x + area.w + 20), Sint16(area.y), 30, 10};

    SDL_Rect choose = {Sint16(screenW/2 + 20), Sint16(screenH - 15), 30, 10};
    SDL_Rect cancel = {Sint16(screenW/2 - 38 - 20), Sint16(screenH - 15), 38, 10};
    SDL_Rect delete_button = {Sint16(screenW - 50), 10, 38, 10};
    SDL_Rect id_button = {Sint16(delete_button.x - 52 - 10), 10, 52, 10};
    SDL_Rect reset_button = delete_button;
    
    
    // Controller input
    int retvalue = 0;
	int highlighted_button = 3;
	
	int prev_index = 0;
	int next_index = 1;
	int choose_index = 2;
	int cancel_index = 3;
	int delete_index = 4;
	int id_index = 5;
	int reset_index = 6;
	
	button buttons[] = {
        button("PREV", KEYSTATE_UNKNOWN, prev.x, prev.y, prev.w, prev.h, 0, -1 , MenuNav::DownRight(cancel_index, next_index)),
        button("NEXT", KEYSTATE_UNKNOWN, next.x, next.y, next.w, next.h, 0, -1 , MenuNav::UpDownLeft(id_index, choose_index, prev_index)),
        button("OK", KEYSTATE_UNKNOWN, choose.x, choose.y, choose.w, choose.h, 0, -1 , MenuNav::UpLeft(next_index, cancel_index)),
        button("CANCEL", KEYSTATE_ESCAPE, cancel.x, cancel.y, cancel.w, cancel.h, 0, -1 , MenuNav::UpRight(prev_index, choose_index)),
        button("DELETE", KEYSTATE_UNKNOWN, delete_button.x, delete_button.y, delete_button.w, delete_button.h, 0, -1 , MenuNav::DownLeft(choose_index, id_index)),
        button("ENTER ID", KEYSTATE_UNKNOWN, id_button.x, id_button.y, id_button.w, id_button.h, 0, -1 , MenuNav::DownRight(next_index, delete_index)),
        button("RESET", KEYSTATE_UNKNOWN, delete_button.x, delete_button.y, delete_button.w, delete_button.h, 0, -1 , MenuNav::DownLeft(choose_index, id_index)),
	};
	
	buttons[prev_index].hidden = (current_campaign_index == 0);
	buttons[next_index].hidden = (current_campaign_index + 1 >= entries.size());
	buttons[choose_index].hidden = !(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL);
	buttons[delete_index].hidden = !enable_delete;
	buttons[reset_index].hidden = enable_delete;
	
	buttons[next_index].nav.down = (buttons[choose_index].hidden? cancel_index : choose_index);
	buttons[cancel_index].nav.up = (buttons[prev_index].hidden? (buttons[next_index].hidden? id_index : next_index) : prev_index);
	buttons[id_index].nav.down = (buttons[next_index].hidden? (buttons[prev_index].hidden? cancel_index : prev_index) : next_index);
	buttons[id_index].nav.right = (buttons[delete_index].hidden? reset_index : delete_index);

    bool done = false;
    while (!done)
    {
        // Reset the timer count to zero ...
        reset_timer();

        if (screenp->end)
            break;

        // Get keys and stuff
        get_input_events(POLL);
		
        handle_menu_nav(buttons, highlighted_button, retvalue, false);

        // Quit if 'q' is pressed
        if(keystates[KEYSTATE_q])
            done = true;

        // Mouse stuff ..
		mymouse = query_mouse();
        int mx = mymouse[MOUSE_X];
        int my = mymouse[MOUSE_Y];
        
        bool do_click = mymouse[MOUSE_LEFT];
		bool do_prev = !buttons[prev_index].hidden && ((do_click && prev.x <= mx && mx <= prev.x + prev.w
               && prev.y <= my && my <= prev.y + prev.h) || (retvalue == OG_OK && highlighted_button == prev_index));
        bool do_next = !buttons[next_index].hidden && ((do_click && next.x <= mx && mx <= next.x + next.w
               && next.y <= my && my <= next.y + next.h) || (retvalue == OG_OK && highlighted_button == next_index));
        bool do_choose = !buttons[choose_index].hidden && ((do_click && choose.x <= mx && mx <= choose.x + choose.w
               && choose.y <= my && my <= choose.y + choose.h) || (retvalue == OG_OK && highlighted_button == choose_index));
        bool do_cancel = (do_click && cancel.x <= mx && mx <= cancel.x + cancel.w
               && cancel.y <= my && my <= cancel.y + cancel.h) || (retvalue == OG_OK && highlighted_button == cancel_index) || keystates[buttons[cancel_index].hotkey];
        bool do_delete = !buttons[delete_index].hidden && ((do_click && enable_delete && delete_button.x <= mx && mx <= delete_button.x + delete_button.w
               && delete_button.y <= my && my <= delete_button.y + delete_button.h) || (retvalue == OG_OK && highlighted_button == delete_index));
        bool do_reset = !buttons[reset_index].hidden && ((do_click && reset_button.x <= mx && mx <= reset_button.x + reset_button.w
               && reset_button.y <= my && my <= reset_button.y + reset_button.h) || (retvalue == OG_OK && highlighted_button == reset_index));
        bool do_id = (do_click && id_button.x <= mx && mx <= id_button.x + id_button.w
               && id_button.y <= my && my <= id_button.y + id_button.h) || (retvalue == OG_OK && highlighted_button == id_index);
        
		if (mymouse[MOUSE_LEFT])
		{
		    while(mymouse[MOUSE_LEFT])
                get_input_events(WAIT);
		}

        // Prev
        if(do_prev)
        {
            if(current_campaign_index > 0)
            {
                current_campaign_index--;
            }
        }
        // Next
        else if(do_next)
        {
            if(current_campaign_index + 1 < entries.size())
            {
                current_campaign_index++;
            }
        }
        // Choose
        else if(do_choose)
        {
            if(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL)
            {
                result = entries[current_campaign_index];
                done = true;
                break;
            }
        }
        // Cancel
        else if(do_cancel)
        {
            while(keystates[buttons[cancel_index].hotkey])
                get_input_events(WAIT);
            done = true;
            break;
        }
        // Delete
        else if(do_delete)
       {
           if(yes_or_no_prompt("Delete campaign", "Delete this campaign permanently?", false)
              && no_or_yes_prompt("Delete campaign", "Are you really sure?", false))
           {
               delete_campaign(entries[current_campaign_index]->id);
               
               restore_default_campaigns();
               remount_campaign_package();  // Just in case we deleted the current campaign
               
               // Reload the picker
               for(std::vector<CampaignEntry*>::iterator e = entries.begin(); e != entries.end(); e++)
               {
                   delete *e;
               }
               entries.clear();
               
               campaign_ids = list_campaigns();
               
                for(std::list<std::string>::iterator e = campaign_ids.begin(); e != campaign_ids.end(); e++)
                {
                    int num_completed = -1;
                    if(save_data != NULL)
                        num_completed = save_data->get_num_levels_completed(*e);
                    entries.push_back(new CampaignEntry(screenp, *e, num_completed));
                }
                
                current_campaign_index = 0;
           }
       }
        // Enter ID
        else if(do_id)
       {
            std::string campaign;
            if(prompt_for_string(loadtext, "Enter Campaign ID", campaign) && campaign.size() > 0)
            {
                result = NULL;
                ret_value.id = campaign;
                done = true;
                break;
            }
       }
       // Reset progress
       else if(do_reset)
       {
           if(yes_or_no_prompt("Reset campaign", "Reset your progress\nin this campaign?", false)
              && no_or_yes_prompt("Reset campaign", "Are you really sure?", false))
           {
               myscreen->save_data.reset_campaign(entries[current_campaign_index]->id);
           }
       }
       
        retvalue = 0;

        // Update hidden buttons
        if(do_prev || do_next || do_choose || do_cancel || do_delete || do_id)
        {
            buttons[prev_index].hidden = (current_campaign_index == 0);
            buttons[next_index].hidden = (current_campaign_index + 1 >= entries.size());
            buttons[choose_index].hidden = !(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL);
            buttons[delete_index].hidden = !enable_delete;
            buttons[reset_index].hidden = enable_delete;

            buttons[next_index].nav.down = (buttons[choose_index].hidden? cancel_index : choose_index);
            buttons[cancel_index].nav.up = (buttons[prev_index].hidden? (buttons[next_index].hidden? id_index : next_index) : prev_index);
            buttons[id_index].nav.down = (buttons[next_index].hidden? (buttons[prev_index].hidden? cancel_index : prev_index) : next_index);
            buttons[id_index].nav.right = (buttons[delete_index].hidden? reset_index : delete_index);
            
            if(buttons[highlighted_button].hidden)
            {
                if(highlighted_button == prev_index && !buttons[next_index].hidden)
                    highlighted_button = next_index;
                else if(highlighted_button == next_index && !buttons[prev_index].hidden)
                    highlighted_button = prev_index;
                else
                    highlighted_button = cancel_index;
            }
        }

        // Draw
        screenp->clearbuffer();

        if(current_campaign_index > 0)
        {
            screenp->draw_button(prev.x, prev.y, prev.x + prev.w, prev.y + prev.h, 1, 1);
            loadtext->write_xy(prev.x + 2, prev.y + 2, "Prev", DARK_BLUE, 1);
        }
        
        if(current_campaign_index + 1 < entries.size())
        {
            screenp->draw_button(next.x, next.y, next.x + next.w, next.y + next.h, 1, 1);
            loadtext->write_xy(next.x + 2, next.y + 2, "Next", DARK_BLUE, 1);
        }
        
        if(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL)
        {
            screenp->draw_button(choose.x, choose.y, choose.x + choose.w, choose.y + choose.h, 1, 1);
            loadtext->write_xy(choose.x + 9, choose.y + 2, "OK", DARK_GREEN, 1);
        }
        screenp->draw_button(cancel.x, cancel.y, cancel.x + cancel.w, cancel.y + cancel.h, 1, 1);
        loadtext->write_xy(cancel.x + 2, cancel.y + 2, "Cancel", RED, 1);
        if(enable_delete)
        {
            screenp->draw_button(delete_button.x, delete_button.y, delete_button.x + delete_button.w, delete_button.y + delete_button.h, 1, 1);
            loadtext->write_xy(delete_button.x + 2, delete_button.y + 2, "Delete", RED, 1);
        }
        else
        {
            screenp->draw_button(reset_button.x, reset_button.y, reset_button.x + reset_button.w, reset_button.y + reset_button.h, 1, 1);
            loadtext->write_xy(reset_button.x + 2, reset_button.y + 2, "Reset", RED, 1);
        }
        
        screenp->draw_button(id_button.x, id_button.y, id_button.x + id_button.w, id_button.y + id_button.h, 1, 1);
        loadtext->write_xy(id_button.x + 2, id_button.y + 2, "Enter ID", DARK_BLUE, 1);
        
        // Draw entry
        if(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL)
            entries[current_campaign_index]->draw(screenp, area, loadtext, army_power);

        draw_highlight(buttons[highlighted_button]);
        screenp->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }

    while (keystates[KEYSTATE_q])
        get_input_events(WAIT);
    
    // Restore old campaign
    mount_campaign_package(old_campaign_id);
    
    if(result != NULL)
    {
        ret_value.id = result->id;
        ret_value.first_level = result->first_level;
    }
    
    for(std::vector<CampaignEntry*>::iterator e = entries.begin(); e != entries.end(); e++)
    {
        delete *e;
    }
    entries.clear();

    delete loadtext;
    
    return ret_value;
}
