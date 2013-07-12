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
#include <vector>
#include <string>

extern Sint32 *mymouse;

int toInt(const std::string& s)
{
    return atoi(s.c_str());
}

// Unmounts old campaign, mounts new one, and returns the current level (scenario) that the player is on
int load_campaign(const std::string& campaign, std::map<std::string, int>& current_levels)
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
        return 1;
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
    y += 8;
    
    // Print completion progress
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




void pick_campaign(screen* screenp, SaveData& save_data)
{
    std::string old_campaign_id = save_data.current_campaign;
    CampaignEntry* result = NULL;

    text* loadtext = new text(screenp);
    
    unmount_campaign_package(old_campaign_id);

    // Here are the browser variables
    std::vector<CampaignEntry*> entries;
    
    // Load campaigns
    std::list<std::string> campaign_ids = list_campaigns();
    for(std::list<std::string>::iterator e = campaign_ids.begin(); e != campaign_ids.end(); e++)
    {
        entries.push_back(new CampaignEntry(screenp, *e, screenp->save_data.get_num_levels_completed(*e)));
    }
    
    unsigned int current_campaign_index = 0;

    // Figure out how good the player's army is
    int army_power = 0;
    for(int i=0; i<MAX_TEAM_SIZE; i++)
    {
        if (myscreen->save_data.team_list[i])
        {
            army_power += 3*myscreen->save_data.team_list[i]->level;
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
        if(keystates[KEYSTATE_q])
            done = true;

        if(keystates[KEYSTATE_LEFT])
        {
            // Scroll up
            if(current_campaign_index > 0)
            {
                current_campaign_index--;
            }
            while (keystates[KEYSTATE_LEFT])
                get_input_events(WAIT);
        }
        if(keystates[KEYSTATE_RIGHT])
        {
            // Scroll down
            if(current_campaign_index + 1 < entries.size())
            {
                current_campaign_index++;
            }
            while (keystates[KEYSTATE_RIGHT])
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
                if(current_campaign_index > 0)
                {
                    current_campaign_index--;
                }
            }
            // Next
            else if(next.x <= mx && mx <= next.x + next.w
                    && next.y <= my && my <= next.y + next.h)
            {
                if(current_campaign_index + 1 < entries.size())
                {
                    current_campaign_index++;
                }
            }
            // Choose
            else if(choose.x <= mx && mx <= choose.x + choose.w
                    && choose.y <= my && my <= choose.y + choose.h)
            {
                if(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL)
                {
                    result = entries[current_campaign_index];
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
        }


        // Draw
        screenp->clearscreen();

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
        
        // Draw entry
        if(current_campaign_index < entries.size() && entries[current_campaign_index] != NULL)
            entries[current_campaign_index]->draw(screenp, area, loadtext, army_power);

        screenp->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }

    while (keystates[KEYSTATE_q])
        get_input_events(WAIT);

    if(result != NULL)
    {
        // Load new campaign
        save_data.current_campaign = result->id;
        mount_campaign_package(result->id);
        if(old_campaign_id != result->id)
        {
            std::map<std::string, int>::const_iterator g = save_data.current_levels.find(result->id);
            if(g != save_data.current_levels.end())
            {
                // Start where we left off
                save_data.scen_num = g->second;
            }
            else
            {
                // Start from the beginning
                save_data.scen_num = result->first_level;
            }
        }
    }
    else  // Restore old campaign
        mount_campaign_package(old_campaign_id);
    
    for(std::vector<CampaignEntry*>::iterator e = entries.begin(); e != entries.end(); e++)
    {
        delete *e;
    }
    entries.clear();

    delete loadtext;

}
