#include "campaign_picker.h"
#include "io.h"
#include "yam.h"
#include "pixie.h"
#include "text.h"
#include "guy.h"
#include "screen.h"
#include <vector>
#include <string>

#define MAXTEAM 24 //max # of guys on a team
extern guy *ourteam[MAXTEAM];
extern Sint32 *mymouse;
extern Sint32 scen_level;

int toInt(const std::string& s)
{
    return atoi(s.c_str());
}

int load_campaign(const std::string& old_campaign, const std::string& campaign, std::map<std::string, int>& current_levels)
{
    if(!unmount_campaign_package(old_campaign))
        return -1;
    
    if(!mount_campaign_package(campaign))
        return -2;
    
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
    
    unsigned char* icondata;
    pixie* icon;
    
    // Player-specific
    int num_levels_completed;

    CampaignEntry(screen* screenp, const std::string& id, int num_levels_completed);
    ~CampaignEntry();
    
    void draw(screen* screenp, const SDL_Rect& area, text* loadtext, int team_power);
};

CampaignEntry::CampaignEntry(screen* screenp, const std::string& id, int num_levels_completed)
    : id(id), title("Untitled"), rating(0.0f), version("1.0"), description("No description."), suggested_power(0), first_level(1), num_levels(0), icondata(NULL), icon(NULL), num_levels_completed(num_levels_completed)
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
        if(icondata != NULL)
            icon = new pixie(icondata+3, icondata[1], icondata[2], screenp);
        
        // Count the number of levels
        std::list<std::string> levels = list_levels();
        num_levels = levels.size();
        
        unmount_campaign_package(id);
    }
}

CampaignEntry::~CampaignEntry()
{
	delete icon;
	free(icondata);
}

void CampaignEntry::draw(screen* screenp, const SDL_Rect& area, text* loadtext, int team_power)
{
    int x = area.x;
    int y = area.y;
    int w = area.w;
    int h = area.h;
    screenp->draw_button(x - 2, y - 2, x + w + 2, y + h + 2, 1, 1);
    
    // TODO: Draw icon
	icon->drawMix(x, y, screenp->viewob[0]);

    // TODO: Print important details
    char buf[30];
    snprintf(buf, 30, "%s", title.c_str());
    loadtext->write_xy(x + w + 5, y, buf, WHITE, 1);
    snprintf(buf, 30, "Sugg. Power: %d", suggested_power);
    loadtext->write_xy(x + w + 5, y + 8, buf, (team_power >= suggested_power? WHITE : RED), 1);
    snprintf(buf, 30, "Completed: %d/%d", num_levels_completed, num_levels);
    loadtext->write_xy(x + w + 5, y + 16, buf, WHITE, 1);

}




void pick_campaign(screen *screenp)
{
    std::string old_campaign_id = screenp->current_campaign;
    CampaignEntry* result = NULL;

    Uint8* mykeyboard = query_keyboard();

    text* loadtext = new text(screenp);
    
    unmount_campaign_package(old_campaign_id);

    // Here are the browser variables
    std::vector<CampaignEntry*> entries;
    
    // Load campaigns
    std::list<std::string> campaign_ids = list_campaigns();
    for(std::list<std::string>::iterator e = campaign_ids.begin(); e != campaign_ids.end(); e++)
    {
        entries.push_back(new CampaignEntry(screenp, *e, screenp->get_num_levels_completed(*e)));
    }
    
    unsigned int current_campaign_index = 0;
    int selected_entry = -1;

    // Figure out how good the player's army is
    int army_power = 0;
    for(int i=0; i<MAXTEAM; i++)
    {
        if (ourteam[i])
        {
            army_power += 3*ourteam[i]->level;
        }
    }
    
    // Campaign icon positioning
    SDL_Rect area[3];
    for(int i = 0; i < 3; i++)
    {
        area[i].x = 10;
        area[i].y = 10 + 40*i;
        area[i].w = 40;
        area[i].h = 30;
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
            if(current_campaign_index > 0)
            {
                selected_entry = -1;

                current_campaign_index--;
            }
            while (mykeyboard[KEYSTATE_UP])
                get_input_events(WAIT);
        }
        if(mykeyboard[KEYSTATE_DOWN])
        {
            // Scroll down
            if(current_campaign_index < entries.size() - 3)
            {
                selected_entry = -1;

                current_campaign_index++;
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
                if(current_campaign_index > 0)
                {
                    selected_entry = -1;
                    current_campaign_index--;
                }
            }
            // Next
            else if(next.x <= mx && mx <= next.x + next.w
                    && next.y <= my && my <= next.y + next.h)
            {
                if(current_campaign_index < entries.size() - 3)
                {
                    selected_entry = -1;
                    current_campaign_index++;
                }
            }
            // Choose
            else if(choose.x <= mx && mx <= choose.x + choose.w
                    && choose.y <= my && my <= choose.y + choose.h)
            {
                if(selected_entry != -1)
                {
                    result = entries[current_campaign_index + selected_entry];
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
                for(int i = 0; i < 3; i++)
                {
                    if(current_campaign_index + i < entries.size() && entries[current_campaign_index + i] != NULL)
                    {
                        SDL_Rect b = area[i];
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
        if(selected_entry != -1 && current_campaign_index + selected_entry < entries.size() && entries[current_campaign_index + selected_entry] != NULL)
        {
            screenp->draw_button(choose.x, choose.y, choose.x + choose.w, choose.y + choose.h, 1, 1);
            loadtext->write_xy(choose.x + 9, choose.y + 2, "OK", DARK_GREEN, 1);
            loadtext->write_xy(next.x, choose.y + 20, entries[current_campaign_index + selected_entry]->title.c_str(), DARK_GREEN, 1);
        }
        screenp->draw_button(cancel.x, cancel.y, cancel.x + cancel.w, cancel.y + cancel.h, 1, 1);
        loadtext->write_xy(cancel.x + 2, cancel.y + 2, "Cancel", RED, 1);

        if(selected_entry != -1)
        {
            int i = selected_entry;
            if(current_campaign_index + i < entries.size() && entries[current_campaign_index + i] != NULL)
            {
                SDL_Rect b = area[i];
                screenp->draw_box(b.x, b.y, b.x + b.w, b.y + b.h, DARK_BLUE, 1, 1);
            }
        }
        for(int i = 0; i < 3; i++)
        {
            if(current_campaign_index + i < entries.size() && entries[current_campaign_index + i] != NULL)
                entries[current_campaign_index + i]->draw(screenp, area[i], loadtext, army_power);
        }

        // Description
        if(selected_entry != -1 && current_campaign_index + selected_entry < entries.size() && entries[current_campaign_index + selected_entry] != NULL)
        {
            screenp->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
            // TODO: Print description
            /*for(int i = 0; i < entries[selected_entry]->scentextlines; i++)
            {
                if(prev.y + 20 + 10*i+1 > descbox.y + descbox.h)
                    break;
                loadtext->write_xy(descbox.x, descbox.y + 10*i+1, entries[selected_entry]->scentext[i], BLACK, 1);
            }*/
        }


        screenp->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }

    while (mykeyboard[KEYSTATE_q])
        get_input_events(WAIT);

    if(result != NULL)
    {
        // Load new campaign
        strcpy(screenp->current_campaign, result->id.c_str());
        mount_campaign_package(result->id);
        if(old_campaign_id != result->id)
        {
            std::map<std::string, int>::const_iterator g = screenp->current_levels.find(result->id);
            if(g != screenp->current_levels.end())
            {
                // Start where we left off
                screenp->scen_num = g->second;
                scen_level = g->second;
            }
            else
            {
                // Start from the beginning
                screenp->scen_num = result->first_level;
                scen_level = result->first_level;
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
