/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/ui/results_screen.h>
#include <openglad/ui/picker_common.h>
#include <openglad/legacy/base.h>
#include <openglad/input/button.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/text.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/render/view.h>
#include <openglad/render/walker_draw.h>
#include <algorithm>
#include <cstring>
#include <format>
#include <memory>

// For deterministic helper-only test coverage.
#ifdef TESTING
#include <openglad/ui/results_screen.h>
namespace {
bool s_force_full_results_ui = false;
}
#endif


bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(const std::string& message, std::string& result);
void popup_dialog(const char* title, const char* message);

inline constexpr Sint32 OG_OK = 4;
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
		    std::string buf = std::format("Retreating to Level {}\n(You may take this field later)", nextlevel);
		    popup_dialog("Retreat!", buf.c_str());
		}

	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
        popup_dialog("Defeat!", "YOU ARE DEFEATED!\nYOU FAILED TO KEEP YOUR ALLY ALIVE");
	}
	else if (ending == 0) // we won
	{
		if (og::runtime::current_session->myscreen_->save_data.is_level_completed(og::runtime::current_session->myscreen_->save_data.scen_num)) // this scenario is completed ..
		{
		    std::string buf = std::format("Moving on to Level {}", nextlevel);
		    popup_dialog("Traveling on...", buf.c_str());
		}
		else
		{
		    popup_dialog("Victory!", "You have won the battle!");
		}
	}
}

bool results_screen(int ending, int nextlevel)
{
#ifdef TESTING
    if (!s_force_full_results_ui)
    {
        // In test mode we still want to exercise the "ending" branching logic,
        // but must avoid modal UI loops that would hang CI.
        show_ending_popup(ending, nextlevel);
        return false;
    }
#endif
    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);
    return false;
}



class TroopResult
{
public:
    guy* before;
    walker* after;
    
    std::string get_name() const;
    std::string get_class_name() const;
    char get_family() const;
    int get_level() const;
    bool gained_level() const;
    bool lost_level() const;
    std::vector<std::string> get_gained_specials() const;
    
    // These are percentages of what you need for the next level.
    float get_XP_base() const;
    float get_XP_gain() const;  // could be negative, if a level is lost, it is a percentage of the XP needed for the lost level
    
    int get_tallies() const;
    float get_HP() const;  // percentage of total
    bool is_dead() const;
    bool is_new() const;
    
    void draw_guy(int cx, int cy, int frame);
    
    TroopResult(guy* before_, walker* after_);
};

TroopResult::TroopResult(guy* before_, walker* after_)
    : before(before_), after(after_)
{
    if(after != nullptr && after->myguy == nullptr)
        after = nullptr;
}

std::string TroopResult::get_name() const
{
    if(before != nullptr)
        return before->name;
    if(after != nullptr)
        return after->myguy->name;
    return std::string();
}

char TroopResult::get_family() const
{
    char family = FAMILY_SOLDIER;
    if(before != nullptr)
        family = before->family;
    if(after != nullptr)
        family = after->myguy->family;
    return family;
}

const char* get_family_string(Sint32 family);

std::string TroopResult::get_class_name() const
{
    if(before != nullptr)
        return og::ui::family_display_name(before->family);
    if(after != nullptr)
        return og::ui::family_display_name(after->myguy->family);
    return std::string();
}

int TroopResult::get_level() const
{
    if(after != nullptr)
        return calculate_level(after->myguy->exp);
    if(before != nullptr)
        return calculate_level(before->exp);
    
    return 0;
}

bool TroopResult::gained_level() const
{
    if(after == nullptr || before == nullptr)
        return false;
    
    return calculate_level(after->myguy->exp) > before->level;
}

bool TroopResult::lost_level() const
{
    if(after == nullptr || before == nullptr)
        return false;
    
    return calculate_level(after->myguy->exp) < before->level;
}

std::vector<std::string> TroopResult::get_gained_specials() const
{
    std::vector<std::string> result;
    
    int family = get_family();
    
    Sint32 test1 = get_level() - 1;
    if ( !(test1%3) ) // we're on a special-gaining level
    {
        test1 = (test1 / 3) + 1; // this is the special #
        if ( (test1 <= 4) // raise this when we have more than 4 specials
                && (og::runtime::current_session->myscreen_->special_name[family][test1] != "NONE") )
        {
            result.push_back(og::runtime::current_session->myscreen_->special_name[family][test1]);
        }
    }
    
    return result;
}

float TroopResult::get_XP_base() const
{
    if(before == nullptr)
        return 0.0f;
    
    if(gained_level())
        return 0.0f;
    
    const Uint32 exp_prev = calculate_exp(before->level);
    const Uint32 exp_next = calculate_exp(before->level + 1);
    return static_cast<float>(before->exp - exp_prev) / static_cast<float>(exp_next);
}

float TroopResult::get_XP_gain() const
{
    if(after == nullptr || before == nullptr)
        return 0.0f;
        
    if(gained_level())
    {
        const Uint32 exp_next = calculate_exp(before->level + 1);
        const Uint32 exp_next_next = calculate_exp(before->level + 2);
        return static_cast<float>(after->myguy->exp - exp_next) / static_cast<float>(exp_next_next);
    }
    
    if(lost_level())
    {
        const Sint64 delta = static_cast<Sint64>(after->myguy->exp) - static_cast<Sint64>(before->exp);
        return static_cast<float>(delta) / static_cast<float>(calculate_exp(before->level));
    }
    
    {
        const Sint64 delta = static_cast<Sint64>(after->myguy->exp) - static_cast<Sint64>(before->exp);
        return static_cast<float>(delta) / static_cast<float>(calculate_exp(before->level + 1));
    }
}

int TroopResult::get_tallies() const
{
    if(after == nullptr)
        return 0;
    
    return after->myguy->scen_kills;
}

float TroopResult::get_HP() const
{
    if(after == nullptr)
        return 0.0f;
    
    if(after->myguy->scen_min_hp > after->stats()->hitpoints)
        return 1.0f;
    
    return after->myguy->scen_min_hp/after->stats()->max_hitpoints;
}

bool TroopResult::is_dead() const
{
    return (get_HP() <= 0.0f);
}

bool TroopResult::is_new() const
{
    return (before == nullptr && after != nullptr);
}


void show_guy(Sint32 frames, guy* myguy, Sint32 centerx, Sint32 centery) // shows the current guy ..
{
	std::unique_ptr<walker> mywalker;
	Sint32 i;
	Sint32 newfamily;

	if (!myguy)
		return;

	frames = abs(frames);

	newfamily = myguy->family;

	mywalker = og::runtime::current_session->myscreen_->level_data.myloader->create_walker_owned(Order::Living,
	           newfamily);
	mywalker->stats()->bit_flags = 0;
	mywalker->curdir = FACE_DOWN;
	mywalker->ani_type = ANI_WALK;
	for (i=0; i <= (frames/4)%4; i++)
		mywalker->animate();
    
	mywalker->team_num = static_cast<unsigned char>(myguy->teamnum);
    
    viewscreen* view_buf = og::runtime::current_session->myscreen_->viewob[0].get();
	mywalker->setxy(centerx - (mywalker->sizex/2) + view_buf->topx - view_buf->xloc, centery - (mywalker->sizey/2) + view_buf->topy - view_buf->yloc);
	draw_walker(*mywalker, view_buf);
}

void TroopResult::draw_guy(int cx, int cy, int frame)
{
    guy* myguy = nullptr;
    if(after != nullptr)
        myguy = after->myguy;
    else if(before != nullptr)
        myguy = before;
    else
        return;
    
    if(!is_dead() && myguy != nullptr)
        show_guy(frame, myguy, cx, cy);
}




#define BEGIN_IF_IN_SCROLL_AREA \
if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h) {

#define END_IF_IN_SCROLL_AREA \
}

int get_num_foes(LevelData& level)
{
    int result = 0;
    
	for(auto& uptr : level.oblist)
	{
	    walker* ob = uptr.get();
	    // Not dead, not hired, not on red team
		if (ob && !ob->dead && ob->query_order() == Order::Living && ob->myguy == nullptr && ob->team_num != 0)
		{
		    result++;
		}
	}
	
	return result;
}

Uint32 get_time_bonus(int playernum)
{
    if(playernum > 0)
        return 0;
    Uint32 frames = og::runtime::current_session->myscreen_->framecount;
    Uint32 time_limit = (og::runtime::current_session->myscreen_->level_data.time_bonus_limit > 0? og::runtime::current_session->myscreen_->level_data.time_bonus_limit : 0);
    Log("Frames used: {}\n", frames);
    if(frames >= time_limit)
        return 0;
    
    short par_value = og::runtime::current_session->myscreen_->level_data.par_value;
    Uint32 score = og::runtime::current_session->myscreen_->save_data.m_score[playernum];
    float multiplier = (1.0f + static_cast<float>(par_value)/10.0f) * (static_cast<float>(time_limit - frames) / static_cast<float>(time_limit));
    Log("Time bonus: {:.0f}\n", static_cast<float>(score) * multiplier);
    return static_cast<Uint32>(static_cast<float>(score) * multiplier);
}

bool results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after)
{
#ifdef TESTING
    if (!s_force_full_results_ui)
    {
        // Same rationale as the 2-arg overload: cover branching without UI loops.
        show_ending_popup(ending, nextlevel);
        (void)before;
        (void)after;
        return false;
    }
#endif
    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);

    // Clear any stale input events after popup closes
    // This helps prevent ASYNCIFY state issues in Emscripten
    SDL_Delay(50);  // Small delay to let browser events settle
    get_input_events(POLL);  // Drain event queue

    LevelData& level_data = og::runtime::current_session->myscreen_->level_data;
    SaveData& save_data = og::runtime::current_session->myscreen_->save_data;
    
    int num_foes_left = get_num_foes(level_data);
    int num_foes_total = 0;
    {
        LevelData original_level(level_data.id);
        original_level.load();
        num_foes_total = get_num_foes(original_level);
}

	text& mytext = og::runtime::current_session->myscreen_->text_normal;
	text& bigtext = og::runtime::current_session->myscreen_->text_big;
	Uint32 bonuscash[4] = {0, 0, 0, 0};
	Uint32 allscore = 0, allbonuscash = 0;

	for(int i = 0; i < 4; i++)
		allscore += save_data.m_score[i];
	
	if(ending == 0)  // we won
	{
	    // Calculate bonuses
		for (int i = 0; i < 4; i++)
		{
			bonuscash[i] = get_time_bonus(i);
			
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
    for(auto& [id, g] : before)
    {
        used_troops.insert(id);
        troops.push_back(TroopResult(g, after[id]));
    }

    // Get the ones from "after" that weren't in "before"
    for(auto& [id, w] : after)
    {
        if(used_troops.insert(id).second)
            troops.push_back(TroopResult(before[id], w));
    }
    
    walker* mvp = nullptr;
    float mvp_points = 0;
    for(auto& troop : troops)
    {
        float points = 0;

        if(troop.after == nullptr)
            continue;

        points = troop.after->myguy->scen_damage + 3*troop.after->myguy->scen_damage_taken;

        if(mvp_points < points)
        {
            mvp = troop.after;
            mvp_points = points;
        }
    }
    
    // Hold indices for troops
    std::vector<int> recruits;
    std::vector<int> losses;
    int i = 0;
    for(auto& troop : troops)
    {
        if(troop.is_dead())
            losses.push_back(i);
        else if(troop.is_new())
            recruits.push_back(i);
        i++;
    }
    
    
    bool retry = false;
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
        button("ok", "OK", KEYSTATE_UNKNOWN, ok_rect.x, ok_rect.y, ok_rect.w, ok_rect.h, 0, -1 , MenuNav{.up=overview_index, .right=retry_index}),
        button("retry", "RETRY", KEYSTATE_UNKNOWN, retry_rect.x, retry_rect.y, retry_rect.w, retry_rect.h, 0, -1 , MenuNav{.up=troops_index, .left=ok_index}),
        button("overview", "OVERVIEW", KEYSTATE_UNKNOWN, overview_rect.x, overview_rect.y, overview_rect.w, overview_rect.h, 0, -1 , MenuNav{.down=ok_index, .right=troops_index}),
        button("troops", "TROOPS", KEYSTATE_UNKNOWN, troops_rect.x, troops_rect.y, troops_rect.w, troops_rect.h, 0, -1 , MenuNav{.down=retry_index, .left=overview_index}),
	};
	
	
    bool done = false;
    bool was_mouse_down = false;  // Track previous mouse state for edge detection
    while (!done)
    {
        // Reset the timer count to zero ...
        reset_timer();

        if(og::runtime::current_session->myscreen_->world_.end)
            break;

        // Get keys and stuff
        get_input_events(POLL);

        handle_menu_nav(buttons, highlighted_button, retvalue, false);

        // Mouse stuff ..
        // Use no_poll version since we already called get_input_events above
		MouseState& mymouse = query_mouse_no_poll();
        int mx = static_cast<int>(mymouse.x);
        int my = static_cast<int>(mymouse.y);

        // Detect mouse click (transition from not pressed to pressed)
        // This avoids blocking while loops that cause issues with Emscripten/ASYNCIFY
        bool mouse_down = mymouse.left;
        bool do_click = mouse_down && !was_mouse_down;
        was_mouse_down = mouse_down;

		scroll -= static_cast<float>(get_and_reset_scroll_amount());
		if(scroll < 0.0f)
            scroll = 0.0f;

		bool do_ok = ((do_click && ok_rect.x <= mx && mx <= ok_rect.x + ok_rect.w
               && ok_rect.y <= my && my <= ok_rect.y + ok_rect.h) || (retvalue == OG_OK && highlighted_button == ok_index));
		bool do_retry = ((do_click && retry_rect.x <= mx && mx <= retry_rect.x + retry_rect.w
               && retry_rect.y <= my && my <= retry_rect.y + retry_rect.h) || (retvalue == OG_OK && highlighted_button == retry_index));
		bool do_overview = ((do_click && overview_rect.x <= mx && mx <= overview_rect.x + overview_rect.w
               && overview_rect.y <= my && my <= overview_rect.y + overview_rect.h) || (retvalue == OG_OK && highlighted_button == overview_index));
		bool do_troops = ((do_click && troops_rect.x <= mx && mx <= troops_rect.x + troops_rect.w
               && troops_rect.y <= my && my <= troops_rect.y + troops_rect.h) || (retvalue == OG_OK && highlighted_button == troops_index));

       // Ok
       if(do_ok)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           done = true;
       }
       // Retry
       else if(do_retry)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           const char* msg = (ending == 0? "Try this level again?\nYou will lose your progress\non this level." : "Try this level again?");
           if(yes_or_no_prompt("Retry level", msg, false))
           {
               // Try again
               done = true;
               retry = true;
           }
       }
       // Overview
       else if(do_overview)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           mode = 0;
       }
       // Troops
       else if(do_troops)
       {
           og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
           mode = 1;
       }
       
        retvalue = 0;

        // Draw
        og::runtime::current_session->myscreen_->draw_button(area.x, area.y, area.x + area.w - 1, area.y + area.h - 1, 1, 1);
        og::runtime::current_session->myscreen_->draw_button_inverted(area_inner.x, area_inner.y, area_inner.w, area_inner.h);
        bigtext.write_xy_center(area.x + area.w/2, area.y + 4, RED, "RESULTS");
        
	        int y = 0;
        if(mode == 0)
        {
            // Overview
            int x = area.x + 12;
	            y = static_cast<int>(static_cast<float>(area.y + 30) - scroll);
            
            if(ending == 0)
            {
                // TODO: Show total possible gold collected?  How is this factored into allscore?
                if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h)
                {
                    std::string buf = std::format("{}", allscore*2);
                    mytext.write_xy_center_shadow(area.x + area.w/2, y, YELLOW, "%s Gold       ", buf.c_str());
                    std::string spaces(buf.size(), ' ');
                    mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%s      Gained", spaces.c_str());
                }
                if(area_inner.y < y && y + 10 < area_inner.y + area_inner.h)
                {
                    if(allbonuscash > 0)
                    {
                        mytext.write_xy_center_shadow(area.x + area.w/2, y + 9, YELLOW, "+ %d Time Bonus", allbonuscash);
                    }
                }
                y += 22;
            }
            
            BEGIN_IF_IN_SCROLL_AREA;
            if(ending == 0)
            {
                std::string buf = std::format("{}", num_foes_total - num_foes_left);
                mytext.write_xy_center_shadow(area.x + area.w/2, y, PURE_WHITE, "%s Foes         ", buf.c_str());
                std::string spaces(buf.size(), ' ');
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%s      Defeated", spaces.c_str());
            }
            else
            {
                std::string buf = std::format("{}", num_foes_total - num_foes_left);
                std::string buf2 = std::format("{}", num_foes_total);
                mytext.write_xy_center_shadow(area.x + area.w/2, y, PURE_WHITE, "%s of %s Foes         ", buf.c_str(), buf2.c_str());
                std::string spaces(buf.size(), ' ');
                std::string spaces2(buf2.size(), ' ');
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "%s    %s      Defeated", spaces.c_str(), spaces2.c_str());
            }
            END_IF_IN_SCROLL_AREA;
            y += 22;
            
            if(mvp != nullptr)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                
                mytext.write_xy_center(area.x + area.w/2, y, DARK_BLUE, "MVP: %s the %s", mvp->myguy->name.c_str(), og::ui::family_display_name(mvp->myguy->family));
                mytext.write_xy_center(area.x + area.w/2, y + 8, DARK_BLUE, "(%.0f pts)", mvp_points);
                y += 22;
                
                END_IF_IN_SCROLL_AREA;
            }
            
            if(ending == 0 && recruits.size() > 0)
            {
                BEGIN_IF_IN_SCROLL_AREA;
                mytext.write_xy(x, y, DARK_BLUE, "%d Recruits:", recruits.size());
                END_IF_IN_SCROLL_AREA;
                y += 22;
                for(auto idx : recruits)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(x, y, DARK_BLUE, " + %s the %s LVL %d", troops[idx].get_name().c_str(), troops[idx].get_class_name().c_str(), troops[idx].get_level());
                    END_IF_IN_SCROLL_AREA;
                    
                    y += 11;
                }
                y += 11;
            }
            
            if(ending != 1 && losses.size() > 0)  // won or lost due to NPC
            {
                BEGIN_IF_IN_SCROLL_AREA;
                mytext.write_xy(x, y, DARK_BLUE, "%d Losses:", losses.size());
                END_IF_IN_SCROLL_AREA;
                
                y += 22;
                for(auto idx : losses)
                {
                    BEGIN_IF_IN_SCROLL_AREA;
                    mytext.write_xy(x, y, DARK_BLUE, " - %s the %s LVL %d", troops[idx].get_name().c_str(), troops[idx].get_class_name().c_str(), troops[idx].get_level());
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
	            y = static_cast<int>(static_cast<float>(area.y + 30) - scroll);
	            for(size_t troop_idx = 0; troop_idx < troops.size(); troop_idx++)
	            {
	                int x = area.x + 12;
	                
	                int tallies = troops[troop_idx].get_tallies();
	                
	                BEGIN_IF_IN_SCROLL_AREA;
	                int name_w = mytext.write_xy(x, y, PURE_BLACK, "%s", troops[troop_idx].get_name().c_str());
	                name_w += mytext.write_xy(x + name_w, y, PURE_BLACK + 2, " the %s", troops[troop_idx].get_class_name().c_str());
	                if(troops[troop_idx].gained_level())
	                    mytext.write_xy(x + name_w, y, YELLOW, " LVL UP %d", troops[troop_idx].get_level());
	                else if(troops[troop_idx].lost_level())
	                    mytext.write_xy(x + name_w, y, RED, " LVL DOWN %d", troops[troop_idx].get_level());
	                else
	                    mytext.write_xy(x + name_w, y, DARK_GREEN, " LVL %d", troops[troop_idx].get_level());
	                END_IF_IN_SCROLL_AREA;
                
	                y += 10;
	                
	                BEGIN_IF_IN_SCROLL_AREA;
	                troops[troop_idx].draw_guy(x + 5, y + 2, static_cast<Sint32>(static_cast<float>(frame) * troops[troop_idx].get_HP()));
	                
	                // HP
	                if(troops[troop_idx].is_dead())
	                {
	                    mytext.write_xy(x + 15, y, RED, "LOST");
	                }
	                else
	                {
	                    x += 15;
	                    mytext.write_xy(x, y, RED, "HP");
	                    x += 14;
	                    og::runtime::current_session->myscreen_->fastbox(x, y, static_cast<Sint32>(60.0f * troops[troop_idx].get_HP()), barH, RED);
	                    og::runtime::current_session->myscreen_->fastbox_outline(x, y, 60, barH, PURE_BLACK);
	                    
	                    // XP
	                    x += 70;
	                    mytext.write_xy(x, y, DARK_GREEN, "EXP");
	                    x += 20;
	                    float base = 60.0f * troops[troop_idx].get_XP_base();
	                    float gain = 60.0f * troops[troop_idx].get_XP_gain();
	                    if(gain >= 0)
	                    {
	                        og::runtime::current_session->myscreen_->fastbox(x, y, static_cast<Sint32>(base), barH, DARK_GREEN);
	                        og::runtime::current_session->myscreen_->fastbox(x + static_cast<Sint32>(base), y, static_cast<Sint32>(gain), barH, LIGHT_GREEN);
	                    }
	                    else
	                        og::runtime::current_session->myscreen_->fastbox(x + 60 + static_cast<Sint32>(gain), y, static_cast<Sint32>(-gain), barH, RED);
	                    og::runtime::current_session->myscreen_->fastbox_outline(x, y, 60, barH, PURE_BLACK);
                    
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
	                
	                if(troops[troop_idx].gained_level())
	                {
	                    std::vector<std::string> specials = troops[troop_idx].get_gained_specials();
                    if(specials.size() > 0)
                    {
                        y += 10;
                        BEGIN_IF_IN_SCROLL_AREA;
                        mytext.write_xy(area.x + 20, y, DARK_BLUE, "Gained special%s:", (specials.size() == 1? "" : "s"));
                        END_IF_IN_SCROLL_AREA;
                        
                        for(auto& spec : specials)
                        {
                            y += 10;
                            BEGIN_IF_IN_SCROLL_AREA;
                            mytext.write_xy(area.x + 30, y, DARK_BLUE, "%s", spec.c_str());
                            END_IF_IN_SCROLL_AREA;
                        }
                    }
                }
                
                y += 13;
                
                BEGIN_IF_IN_SCROLL_AREA;
                og::runtime::current_session->myscreen_->hor_line(area_inner.x + 6, y - 3, area_inner.w - 30, GREY - 4);
                END_IF_IN_SCROLL_AREA;
            }
        }
        
        // Draw scroll indicator
	        if(static_cast<float>(y) + scroll > static_cast<float>(area_inner.y + area_inner.h - 30))
	        {
	            const float denom = static_cast<float>(y) + scroll - static_cast<float>(area.y);
	            const float y_off = (denom != 0.0f) ? (static_cast<float>(area_inner.h) * scroll / denom) : 0.0f;
	            og::runtime::current_session->myscreen_->ver_line(area_inner.x, static_cast<Sint32>(static_cast<float>(area_inner.y) + y_off), 6, PURE_BLACK);
	        }
        
        // Limit the scrolling depending on how long 'y' is.
	        if(y < area_inner.y + 30)
	            scroll = static_cast<float>(y - (area_inner.y + 30)) + scroll;
        
        
	        for(int button_i = 0; button_i < num_buttons; button_i++)
	        {
	            if((mode == 0 && button_i == overview_index) || (mode == 1 && button_i == troops_index))
	                og::runtime::current_session->myscreen_->draw_button_inverted(buttons[button_i].x, buttons[button_i].y, buttons[button_i].sizex, buttons[button_i].sizey);
	            else
	                og::runtime::current_session->myscreen_->draw_button(buttons[button_i].x, buttons[button_i].y, buttons[button_i].x + buttons[button_i].sizex - 1, buttons[button_i].y + buttons[button_i].sizey - 1, 1, 1);
	            mytext.write_xy(buttons[button_i].x + buttons[button_i].sizex/2 - 3*static_cast<Sint32>(buttons[button_i].label.size()), buttons[button_i].y + 2, buttons[button_i].label.c_str(), DARK_BLUE, 1);
	        }
        
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
        
        frame++;
        if(frame > 1000000)
            frame = 0;
    }
    
    return retry;
}

#ifdef TESTING
void results_screen_testing_set_force_full(bool enabled)
{
    s_force_full_results_ui = enabled;
}

int results_screen_test_exercise_internal()
{
    // Exercise the core computation helpers in this TU without entering any
    // interactive UI loops (safe for CI).
    int score = 0;

    // walker/pixie store raw pointers to PixieData buffers; keep the data alive
    // for the duration of this helper.
    PixieData one_px(1, 1, 1, new unsigned char[1]{0});

    // Ending popup branches.
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    show_ending_popup(1, -1); // generic defeat
    show_ending_popup(1, 2);  // retreat
    show_ending_popup(SCEN_TYPE_SAVE_ALL, -1);
    show_ending_popup(0, 2);  // victory not completed
    og::runtime::current_session->myscreen_->save_data.current_levels[og::runtime::current_session->myscreen_->save_data.current_campaign] = og::runtime::current_session->myscreen_->save_data.scen_num;
    show_ending_popup(0, 2);  // victory completed
    score++;

    // TroopResult logic paths.
    guy before{};
    before.name = "Before";
    before.family = FAMILY_MAGE;
    before.level = 3;
    before.exp = calculate_exp(before.level) + 10;

    // after with null myguy should be ignored by ctor.
    walker after_null(one_px);
    after_null.clear_myguy();
    TroopResult t0(&before, &after_null);
    if (t0.get_name() == "Before")
        score++;
    if (!t0.get_class_name().empty())
        score++;
    (void)t0.get_XP_base();
    (void)t0.get_XP_gain();
    (void)t0.get_gained_specials();
    (void)t0.is_new();

    // after with owned myguy to cover gained/lost/no-change XP branches and HP paths.
    walker after_owned(one_px);
    auto owned = std::make_unique<guy>(FAMILY_MAGE);
    owned->name = "After";
    owned->family = FAMILY_MAGE;
    owned->level = 4;
    owned->exp = calculate_exp(owned->level) + 5; // gained level vs before.level=3
    owned->scen_min_hp = 1;
    after_owned.set_owned_myguy(std::move(owned));
    after_owned.stats()->max_hitpoints = 10;
    after_owned.stats()->hitpoints = 5;

    TroopResult t1(&before, &after_owned);
    if (t1.gained_level())
        score++;
    (void)t1.get_XP_gain();
    (void)t1.get_HP();
    (void)t1.get_tallies();
    (void)t1.get_gained_specials();

    // lost level branch
    before.level = 6;
    before.exp = calculate_exp(before.level);
    after_owned.myguy->exp = calculate_exp(3); // lost
    TroopResult t2(&before, &after_owned);
    if (t2.lost_level())
        score++;
    (void)t2.get_XP_gain();

    // unchanged level branch
    before.level = 3;
    before.exp = calculate_exp(before.level) + 20;
    after_owned.myguy->exp = calculate_exp(before.level) + 25;
    TroopResult t3(&before, &after_owned);
    (void)t3.get_XP_gain();

    // HP edge: scen_min_hp > current hitpoints returns 1.0
    after_owned.myguy->scen_min_hp = 1000;
    after_owned.stats()->hitpoints = 1;
    TroopResult t4(nullptr, &after_owned);
    if (t4.get_HP() >= 0.99f)
        score++;
    t4.draw_guy(160, 100, 0);

    // Draw helper should no-op when dead.
    after_owned.myguy->scen_min_hp = 0;
    after_owned.stats()->hitpoints = 0;
    TroopResult t5(nullptr, &after_owned);
    if (t5.is_dead())
        score++;
    t5.draw_guy(160, 100, 0);

    return score;
}
#endif
