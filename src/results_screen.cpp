#include "results_screen.h"
#include "base.h"
#include "button.h"
#include "text.h"
#include "walker.h"
#include "guy.h"
#include "stats.h"
#include "view.h"

extern Sint32 *mymouse;

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(text* mytext, const std::string& message, std::string& result);
void popup_dialog(const char* title, const char* message);

#define OG_OK 4
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);


void show_ending_popup(int ending, int nextlevel)
{
	if (ending == 1)  // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
		    popup_dialog("Defeat!", "YOUR MEN ARE CRUSHED!");
		}
		else // we're withdrawing to another level
		{
		    char buf[255];
		    snprintf(buf, 255, "Retreating to Level %d\n(You may take this field later)", nextlevel);
		    popup_dialog("Retreat!", buf);
		}

	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
        popup_dialog("Defeat!", "YOU ARE DEFEATED!\nYOU FAILED TO KEEP YOUR ALLY ALIVE");
	}
	else if (ending == 0) // we won
	{
		if (myscreen->save_data.is_level_completed(myscreen->save_data.scen_num)) // this scenario is completed ..
		{
		    char buf[255];
		    snprintf(buf, 255, "Moving on to Level %d", nextlevel);
		    popup_dialog("Traveling on...", buf);
		}
		else
		{
		    popup_dialog("Victory!", "You have won the battle!");
		}
	}
}

void results_screen(int ending, int nextlevel)
{
    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);
}


#define TIME_BONUS (Sint32) 5000
#define LEVEL_BONUS (Sint32) 120

class TroopResult
{
public:
    guy* before;
    walker* after;
    
    std::string get_name();
    std::string get_class_name();
    int get_level();
    bool gained_level();
    bool lost_level();
    
    // These are percentages of what you need for the next level.
    float get_XP_base();
    float get_XP_gain();  // could be negative, if a level is lost, it is a percentage of the XP needed for the lost level
    
    int get_tallies();
    float get_HP();  // percentage of total
    bool is_dead();
    bool is_new();
    
    void draw_guy(int cx, int cy, int frame);
    
    TroopResult(guy* before, walker* after);
};

TroopResult::TroopResult(guy* before, walker* after)
    : before(before), after(after)
{
    if(after != NULL && after->myguy == NULL)
        after = NULL;
}

std::string TroopResult::get_name()
{
    if(before != NULL)
        return before->name;
    if(after != NULL)
        return after->myguy->name;
    return std::string();
}

const char* get_family_string(short family);

std::string TroopResult::get_class_name()
{
    if(before != NULL)
        return get_family_string(before->family);
    if(after != NULL)
        return get_family_string(after->myguy->family);
    return std::string();
}

int TroopResult::get_level()
{
    if(after != NULL)
        return calculate_level(after->myguy->exp);
    if(before != NULL)
        return calculate_level(before->exp);
    
    return 0;
}

bool TroopResult::gained_level()
{
    if(after == NULL || before == NULL)
        return false;
    
    return calculate_level(after->myguy->exp) > before->level;
}

bool TroopResult::lost_level()
{
    if(after == NULL || before == NULL)
        return false;
    
    return calculate_level(after->myguy->exp) < before->level;
}

float TroopResult::get_XP_base()
{
    if(before == NULL)
        return 0.0f;
    
    if(gained_level())
        return 0.0f;
    
    return (before->exp - calculate_exp(before->level))/float(calculate_exp(before->level + 1));
}

float TroopResult::get_XP_gain()
{
    if(after == NULL || before == NULL)
        return 0.0f;
        
    if(gained_level())
        return (after->myguy->exp - calculate_exp(before->level + 1))/float(calculate_exp(before->level + 2));
    
    if(lost_level())
        return (after->myguy->exp - before->exp)/float(calculate_exp(before->level));
    
    return (after->myguy->exp - before->exp)/float(calculate_exp(before->level + 1));
}

int TroopResult::get_tallies()
{
    if(after == NULL)
        return 0;
    
    return after->myguy->level_kills;
}

float TroopResult::get_HP()
{
    if(after == NULL)
        return 0.0f;
    
    return after->stats->hitpoints/float(after->stats->max_hitpoints);
}

bool TroopResult::is_dead()
{
    return (get_HP() <= 0.0f);
}

bool TroopResult::is_new()
{
    return (before == NULL && after != NULL);
}


void show_guy(Sint32 frames, guy* myguy, short centerx, short centery) // shows the current guy ..
{
	walker *mywalker;
	Sint32 i;
	Sint32 newfamily;

	if (!myguy)
		return;

	frames = abs(frames);

	newfamily = myguy->family;

	mywalker = myscreen->level_data.myloader->create_walker(ORDER_LIVING,
	           newfamily,myscreen);
	mywalker->stats->bit_flags = 0;
	mywalker->curdir = FACE_DOWN;
	mywalker->ani_type = ANI_WALK;
	for (i=0; i <= (frames/4)%4; i++)
		mywalker->animate();
    
	mywalker->team_num = myguy->teamnum;
    
    viewscreen* view_buf = myscreen->viewob[0];
	mywalker->xpos = centerx - (mywalker->sizex/2) + view_buf->topx - view_buf->xloc;
	mywalker->ypos = centery - (mywalker->sizey/2) + view_buf->topy - view_buf->yloc;
	mywalker->draw(view_buf);
	delete mywalker;
}

void TroopResult::draw_guy(int cx, int cy, int frame)
{
    guy* myguy = NULL;
    if(after != NULL)
        myguy = after->myguy;
    else if(before != NULL)
        myguy = before;
    else
        return;
    
    if(!is_dead() && myguy != NULL)
        show_guy(frame, myguy, cx, cy);
}




#define BEGIN_IF_IN_SCROLL_AREA \
if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h) {

#define END_IF_IN_SCROLL_AREA \
}


void results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after)
{
    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);
    
    LevelData& level_data = myscreen->level_data;
    SaveData& save_data = myscreen->save_data;
    
	text mytext(myscreen, TEXT_1);
	text bigtext(myscreen, TEXT_BIG);
	Uint32 bonuscash[4] = {0, 0, 0, 0};
	Uint32 allscore = 0, allbonuscash = 0;

	for(int i = 0; i < 4; i++)
		allscore += save_data.m_score[i];
	
	if(ending == 0)  // we won
	{
	    // Calculate bonuses
		for (int i = 0; i < 4; i++)
		{
			bonuscash[i] = (save_data.m_score[i] * (TIME_BONUS + ((Sint32)level_data.par_value * LEVEL_BONUS) - myscreen->framecount))/(TIME_BONUS + ( ((Sint32)level_data.par_value * LEVEL_BONUS)/2));
			if (bonuscash[i] < 0 || myscreen->framecount > TIME_BONUS) // || framecount < 0)
				bonuscash[i] = 0;
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for(int i = 0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
	}
	
    // Now show the results
    
    std::set<int> used_troops;
    std::vector<TroopResult> troops;
    
    // Get the guys from "before"
    for(std::map<int, guy*>::iterator e = before.begin(); e != before.end(); e++)
    {
        used_troops.insert(e->first);
        troops.push_back(TroopResult(e->second, after[e->first]));
    }
    
    // Get the ones from "after" that weren't in "before"
    for(std::map<int, walker*>::iterator e = after.begin(); e != after.end(); e++)
    {
        if(used_troops.insert(e->first).second)
            troops.push_back(TroopResult(before[e->first], e->second));
    }
    
    walker* mvp = NULL;
    for(std::map<int, walker*>::iterator e = after.begin(); e != after.end(); e++)
    {
        // FIXME: Is total_damage persistent?  We only want damage for this level.
        if(mvp == NULL || (e->second != NULL && mvp->myguy->total_damage < e->second->myguy->total_damage))
            mvp = e->second;
    }
    
    // Hold indices for troops
    std::vector<int> recruits;
    std::vector<int> losses;
    int i = 0;
    for(std::vector<TroopResult>::iterator e = troops.begin(); e != troops.end(); e++)
    {
        if(e->is_dead())
            losses.push_back(i);
        else if(e->is_new())
            recruits.push_back(i);
        i++;
    }
    
    // TODO: Somehow show that a character has gained a special ability on level up
    // Maybe clicking on a character will display more details
    /*test1 = calculate_level(target->myguy->exp) - 1;
    if ( !(test1%3) ) // we're on a special-gaining level
    {
        test1 = (test1 / 3) + 1; // this is the special #
        if ( (test1 <= 4) // raise this when we have more than 4 specials
                && (strcmp(myscreen->special_name[(int)target->query_family()][test1], "NONE") )
           )
        {
            sprintf(temp, "New Ability: %s!",
                    myscreen->special_name[(int)target->query_family()][test1]);
            mytext.write_y(110, temp, DARK_BLUE, 1);
        }
    }*/
    
    int mode = 0;
    float scroll = 0.0f;
    int frame = 0;
    
    Sint16 screenW = 320;
    Sint16 screenH = 200;
    
    SDL_Rect area;
    area.x = 50;
    area.y = 20;
    area.w = screenW - 2*area.x;
    area.h = screenH - 2*area.y;
    
    SDL_Rect area_inner = {area.x + 3, area.y + 17, area.w - 6, area.h - 34};

    // Buttons
    SDL_Rect ok_rect = {Sint16(area.x + area.w/2 - 45), Sint16(area.y + area.h - 14), 35, 10};
    SDL_Rect retry_rect = {Sint16(area.x + area.w/2 + 10), Sint16(area.y + area.h - 14), 35, 10};

    SDL_Rect overview_rect = {Sint16(area.x + area.w/2 - 100), Sint16(area.y + 4), 50, 10};
    SDL_Rect troops_rect = {Sint16(area.x + area.w/2 + 50), Sint16(area.y + 4), 50, 10};
    
    
    // Controller input
    int retvalue = 0;
	int highlighted_button = 0;
	
	int ok_index = 0;
	int retry_index = 1;
	int overview_index = 2;
	int troops_index = 3;
	int num_buttons = 4;
	
	button buttons[] = {
        button("OK", KEYSTATE_UNKNOWN, ok_rect.x, ok_rect.y, ok_rect.w, ok_rect.h, 0, -1 , MenuNav::UpRight(overview_index, retry_index)),
        button("RETRY", KEYSTATE_UNKNOWN, retry_rect.x, retry_rect.y, retry_rect.w, retry_rect.h, 0, -1 , MenuNav::UpLeft(troops_index, ok_index)),
        button("OVERVIEW", KEYSTATE_UNKNOWN, overview_rect.x, overview_rect.y, overview_rect.w, overview_rect.h, 0, -1 , MenuNav::DownRight(ok_index, troops_index)),
        button("TROOPS", KEYSTATE_UNKNOWN, troops_rect.x, troops_rect.y, troops_rect.w, troops_rect.h, 0, -1 , MenuNav::DownLeft(retry_index, overview_index)),
	};
	
	
    bool done = false;
    while (!done)
    {
        // Reset the timer count to zero ...
        reset_timer();

        if(myscreen->end)
            break;

        // Get keys and stuff
        get_input_events(POLL);
		
        handle_menu_nav(buttons, highlighted_button, retvalue, false);

        // Mouse stuff ..
		mymouse = query_mouse();
        int mx = mymouse[MOUSE_X];
        int my = mymouse[MOUSE_Y];
        
		scroll += get_and_reset_scroll_amount();
		if(scroll < 0.0f)
            scroll = 0.0f;
        
        bool do_click = mymouse[MOUSE_LEFT];
		bool do_ok = ((do_click && ok_rect.x <= mx && mx <= ok_rect.x + ok_rect.w
               && ok_rect.y <= my && my <= ok_rect.y + ok_rect.h) || (retvalue == OG_OK && highlighted_button == ok_index));
		bool do_retry = ((do_click && retry_rect.x <= mx && mx <= retry_rect.x + retry_rect.w
               && retry_rect.y <= my && my <= retry_rect.y + retry_rect.h) || (retvalue == OG_OK && highlighted_button == retry_index));
		bool do_overview = ((do_click && overview_rect.x <= mx && mx <= overview_rect.x + overview_rect.w
               && overview_rect.y <= my && my <= overview_rect.y + overview_rect.h) || (retvalue == OG_OK && highlighted_button == overview_index));
		bool do_troops = ((do_click && troops_rect.x <= mx && mx <= troops_rect.x + troops_rect.w
               && troops_rect.y <= my && my <= troops_rect.y + troops_rect.h) || (retvalue == OG_OK && highlighted_button == troops_index));
        
		if (mymouse[MOUSE_LEFT])
		{
		    while(mymouse[MOUSE_LEFT])
                get_input_events(WAIT);
		}

       // Ok
       if(do_ok)
       {
           myscreen->soundp->play_sound(SOUND_BOW);
           done = true;
       }
       // Retry
       else if(do_retry)
       {
           myscreen->soundp->play_sound(SOUND_BOW);
           const char* msg = (ending == 0? "Try this level again?\nYou will lose your progress\non this level." : "Try this level again?");
           if(yes_or_no_prompt("Retry level", msg, false))
           {
               // FIXME: Try again
           }
       }
       // Overview
       else if(do_overview)
       {
           myscreen->soundp->play_sound(SOUND_BOW);
           mode = 0;
       }
       // Troops
       else if(do_troops)
       {
           myscreen->soundp->play_sound(SOUND_BOW);
           mode = 1;
       }
       
        retvalue = 0;

        // Draw
        myscreen->draw_button(area.x, area.y, area.x + area.w - 1, area.y + area.h - 1, 1, 1);
        myscreen->draw_button_inverted(area_inner.x, area_inner.y, area_inner.w, area_inner.h);
        bigtext.write_xy_center(area.x + area.w/2, area.y + 4, RED, "RESULTS");
        
        int y = 0;
        if(mode == 0)
        {
            // Overview
            int x = area.x + 12;
            y = area.y + 30 - scroll;
            
            if(ending == 0)
            {
                // TODO: Show total possible gold
                if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h)
                    mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%d Gold Gained", allscore*2);
                if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h)
                {
                    if(allbonuscash > 0)
                        mytext.write_xy_center(area.x + area.w/2, y + 9, DARK_BLUE, "+ %d Bonus Gold", allbonuscash);
                }
                y += 22;
            }
            
            BEGIN_IF_IN_SCROLL_AREA;
            // FIXME: Put in right # foes
            if(ending == 0)
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%d Foes Defeated", 0);
            else
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%d of %d Foes Defeated", 0, 0);
            END_IF_IN_SCROLL_AREA;
            y += 22;
            
            if(mvp != NULL)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "MVP: %s the %s", mvp->myguy->name, get_family_string(mvp->myguy->family));
                y += 22;
                
                END_IF_IN_SCROLL_AREA;
            }
            
            if(ending == 0 && recruits.size() > 0)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                mytext.write_xy(x, y, DARK_BLUE, "Recruits:");
                END_IF_IN_SCROLL_AREA;
                y += 22;
                for(std::vector<int>::iterator e = recruits.begin(); e != recruits.end(); e++)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(x, y, DARK_BLUE, " + %s the %s LVL %d", troops[*e].get_name().c_str(), troops[*e].get_class_name().c_str(), troops[*e].get_level());
                    END_IF_IN_SCROLL_AREA;
                    
                    y += 11;
                }
                y += 11;
            }
            
            if(ending != 1 && losses.size() > 0)  // won or lost due to NPC
            {
                BEGIN_IF_IN_SCROLL_AREA;
                mytext.write_xy(x, y, DARK_BLUE, "Losses:");
                END_IF_IN_SCROLL_AREA;
                
                y += 22;
                for(std::vector<int>::iterator e = losses.begin(); e != losses.end(); e++)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(x, y, DARK_BLUE, " - %s the %s LVL %d", troops[*e].get_name().c_str(), troops[*e].get_class_name().c_str(), troops[*e].get_level());
                    END_IF_IN_SCROLL_AREA;
                    
                    y += 11;
                }
                y += 11;
            }
        }
        else if(mode == 1)
        {
            int barH = 5;
            // Troops
            y = area.y + 30 - scroll;
            for(size_t i = 0; i < troops.size(); i++)
            {
                int x = area.x + 12;
                
                int tallies = troops[i].get_tallies();
                
                BEGIN_IF_IN_SCROLL_AREA;
                int name_w = mytext.write_xy(x, y, PURE_BLACK, "%s", troops[i].get_name().c_str());
                name_w += mytext.write_xy(x + name_w, y, PURE_BLACK + 2, " the %s", troops[i].get_class_name().c_str());
                if(troops[i].gained_level())
                    mytext.write_xy(x + name_w, y, YELLOW, " LVL UP %d", troops[i].get_level());
                else if(troops[i].lost_level())
                    mytext.write_xy(x + name_w, y, RED, " LVL DOWN %d", troops[i].get_level());
                else
                    mytext.write_xy(x + name_w, y, DARK_GREEN, " LVL %d", troops[i].get_level());
                END_IF_IN_SCROLL_AREA;
                
                y += 10;
                
                BEGIN_IF_IN_SCROLL_AREA;
                troops[i].draw_guy(x + 5, y + 2, frame*troops[i].get_HP());
                
                // HP
                if(troops[i].is_dead())
                {
                    mytext.write_xy(x + 15, y, RED, "LOST");
                }
                else
                {
                    x += 15;
                    mytext.write_xy(x, y, RED, "HP");
                    x += 14;
                    myscreen->fastbox(x, y, 60*troops[i].get_HP(), barH, RED);
                    myscreen->fastbox_outline(x, y, 60, barH, PURE_BLACK);
                    
                    // XP
                    x += 70;
                    mytext.write_xy(x, y, DARK_GREEN, "EXP");
                    x += 20;
                    float base = 60*troops[i].get_XP_base();
                    float gain = 60*troops[i].get_XP_gain();
                    if(gain >= 0)
                    {
                        myscreen->fastbox(x, y, base, barH, DARK_BLUE);
                        myscreen->fastbox(x + base, y, gain, barH, DARK_GREEN);
                    }
                    else
                        myscreen->fastbox(x + 60 + gain, y, -gain, barH, RED);
                    myscreen->fastbox_outline(x, y, 60, barH, PURE_BLACK);
                    
                    if(gain > 0.0f)
                        mytext.write_xy(x + 63, y, DARK_GREEN, "+");
                    else if(gain < 0.0f)
                        mytext.write_xy(x + 63, y, RED, "-");
                }
                END_IF_IN_SCROLL_AREA;
                
                
                if(tallies > 0)
                {
                    y += 10;
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(area.x + 20, y, DARK_GREEN, "%d Tall%s", tallies, (tallies == 1? "y" : "ies"));
                    END_IF_IN_SCROLL_AREA;
                }
                
                y += 13;
                
                BEGIN_IF_IN_SCROLL_AREA;
                myscreen->hor_line(area_inner.x + 6, y - 3, area_inner.w - 30, GREY - 4);
                END_IF_IN_SCROLL_AREA;
            }
        }
        
        // Draw scroll indicator
        if(y + scroll > area_inner.y + area_inner.h - 30)
        {
            myscreen->ver_line(area_inner.x, area_inner.y + (area_inner.h) * scroll/(y + scroll - area.y), 6, PURE_BLACK);
        }
        
        // Limit the scrolling depending on how long 'y' is.
        if(y < area_inner.y + 30)
            scroll = y + scroll - (area_inner.y + 30);
        
        
        for(int i = 0; i < num_buttons; i++)
        {
            if((mode == 0 && i == overview_index) || (mode == 1 && i == troops_index))
                myscreen->draw_button_inverted(buttons[i].x, buttons[i].y, buttons[i].sizex, buttons[i].sizey);
            else
                myscreen->draw_button(buttons[i].x, buttons[i].y, buttons[i].x + buttons[i].sizex - 1, buttons[i].y + buttons[i].sizey - 1, 1, 1);
            mytext.write_xy(buttons[i].x + buttons[i].sizex/2 - 3*buttons[i].label.size(), buttons[i].y + 2, buttons[i].label.c_str(), DARK_BLUE, 1);
        }
        
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
        
        frame++;
        if(frame > 1000000)
            frame = 0;
    }
}
