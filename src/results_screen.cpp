#include "results_screen.h"
#include "base.h"
#include "button.h"
#include "text.h"
#include "walker.h"
#include "guy.h"
#include "stats.h"

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
    
    int get_level();
    bool gained_level();
    bool lost_level();
    
    // These are percentages of what you need for the next level.
    float get_XP_base();
    float get_XP_gain();  // could be negative, in which case it is a percentage of the XP needed for the lost level
    
    int get_tallies();
    float get_HP();  // percentage of total
    bool is_dead();
    
    TroopResult(guy* before, walker* after);
};

TroopResult::TroopResult(guy* before, walker* after)
    : before(before), after(after)
{
    if(after != NULL && after->myguy == NULL)
        after = NULL;
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
    
    return (before->exp - calculate_exp(before->level))/float(calculate_exp(before->level + 1));
}

float TroopResult::get_XP_gain()
{
    if(after == NULL || before == NULL)
        return 0.0f;
    
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



void results_screen(int ending, int nextlevel, std::map<int, guy*>& before, std::map<int, walker*>& after)
{
    // Popup the ending dialog
    show_ending_popup(ending, nextlevel);
    
    LevelData& level_data = myscreen->level_data;
    SaveData& save_data = myscreen->save_data;
    
	char temp[50];
	text mytext(myscreen, TEXT_1);
	Uint32 bonuscash[4] = {0, 0, 0, 0};
	oblink *checklist = level_data.oblist;
	walker *target;
	Sint32 test1;
	int  i;
	Uint32 allscore = 0, allbonuscash = 0;

	for (i=0; i < 4; i++)
		allscore += save_data.m_score[i];
	
	if(ending == 0)  // we won
	{
	    // Calculate bonuses
		for (i=0; i < 4; i++)
		{
			bonuscash[i] = (save_data.m_score[i] * (TIME_BONUS + ((Sint32)level_data.par_value * LEVEL_BONUS) - myscreen->framecount))/(TIME_BONUS + ( ((Sint32)level_data.par_value * LEVEL_BONUS)/2));
			if (bonuscash[i] < 0 || myscreen->framecount > TIME_BONUS) // || framecount < 0)
				bonuscash[i] = 0;
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for (i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
	}
	
	
    // Now show the results
    
    for(std::map<int, guy*>::const_iterator e = before.begin(); e != before.end(); e++)
    {
        Log("Guy: %s (%d), HP: %.2f, XP: %.2f\n", e->second->name, e->second->id, TroopResult(e->second, after[e->first]).get_HP(), TroopResult(e->second, after[e->first]).get_XP_gain());
    }
    
    
    {
		// Check for guys who have gone up levels
        while (checklist)
        {
            if (checklist->ob)
                target = checklist->ob;
            else
                target = NULL;
            if (target && target->team_num==0
                    && target->query_order()==ORDER_LIVING
                    && !target->dead
                    && target->myguy
                    && target->myguy->level != calculate_level(target->myguy->exp)
               ) // check for living guy on our team, with guy pointer
            {
                //draw_button(30,82,290,132,4);
                if (target->myguy->level < calculate_level(target->myguy->exp))
                {
                    myscreen->draw_dialog(30, 70, 290, 134, "Congratulations!");
                    sprintf(temp, "%s reached level %d",
                            target->myguy->name,
                            calculate_level(target->myguy->exp) );
                }
                else // we lost levels :>
                {
                    myscreen->draw_dialog(30, 70, 290, 134, "Alas!");
                    sprintf(temp, "%s fell to level %d",
                            target->myguy->name,
                            calculate_level(target->myguy->exp) );
                }
                mytext.write_y(100,temp, DARK_BLUE, 1);
                test1 = calculate_level(target->myguy->exp) - 1;
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
                }
                mytext.write_y(120, CONTINUE_ACTION_STRING " TO CONTINUE", DARK_BLUE, 1);
                myscreen->buffer_to_screen(0, 0, 320, 200);
                clear_keyboard();
                while (!query_input_continue())
                    get_input_events(WAIT);
            }
            checklist = checklist->next;
        } // end of while checklist
        // end of full 'check for raised levels' routine
	}

}
