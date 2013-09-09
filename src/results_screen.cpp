#include "results_screen.h"
#include "base.h"
#include "button.h"
#include "text.h"
#include "walker.h"
#include "guy.h"

extern Sint32 *mymouse;

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);

bool prompt_for_string(text* mytext, const std::string& message, std::string& result);

#define OG_OK 4
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);


#define TIME_BONUS (Sint32) 5000
#define LEVEL_BONUS (Sint32) 120

class TroopResult
{
public:
    
};

void results_screen(int ending, int nextlevel)
{
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

	if (ending == 1)  // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
			myscreen->draw_dialog(30, 70, 290, 134, "Defeat!");
			mytext.write_y(92,"YOUR MEN ARE CRUSHED!", DARK_BLUE, 1);
			sprintf(temp,"YOUR SCORE IS %u.\n", allscore);
			mytext.write_y(100,temp, DARK_BLUE, 1);
			mytext.write_y(110,"**" CONTINUE_ACTION_STRING " TO RETURN TO THE MENUS.**", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			// Zardus: all things should listen to get_input_events() for now until further notice
			clear_keyboard();
			while (!query_input_continue())
				get_input_events(WAIT);
		}
		else // we're withdrawing to another level
		{
			myscreen->draw_dialog(30, 70, 290, 134, "Retreat!");
			sprintf(temp, "Retreating to Level %d", nextlevel);
			mytext.write_y(92,temp, DARK_BLUE, 1);
			sprintf(temp,"(You may take this field later)");
			mytext.write_y(100,temp, DARK_BLUE, 1);
			mytext.write_y(110,"**" CONTINUE_ACTION_STRING " TO RETURN TO THE MENUS.**", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			clear_keyboard();
			while (!query_input_continue())
				get_input_events(WAIT);
		}

	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		myscreen->draw_dialog(30, 70, 290, 134, "Defeat!");
		mytext.write_y(92,"YOU ARE DEFEATED", DARK_BLUE, 1);
		sprintf(temp,"(YOU FAILED TO KEEP THE NPC'S ALIVE)" );
		mytext.write_y(100,temp, DARK_BLUE, 1);
		mytext.write_y(110,"**" CONTINUE_ACTION_STRING " TO RETURN TO THE MENUS.**", DARK_BLUE, 1);
		myscreen->buffer_to_screen(0, 0, 320, 200);
        clear_keyboard();
        while (!query_input_continue())
            get_input_events(WAIT);
	}
	else if (ending == 0) // we won
	{
		if (save_data.is_level_completed(save_data.scen_num)) // this scenario is completed ..
		{
			myscreen->draw_dialog(30, 70, 290, 134, "Traveling On..");
			sprintf(temp, "(Field Already Won)");
			mytext.write_y(100,temp, DARK_BLUE, 1);
		}
		else
		{
			myscreen->draw_dialog(30, 70, 290, 134, "Victory!");
			mytext.write_y(92,"YOU WIN.", DARK_BLUE, 1);
			sprintf(temp,"YOUR SCORE IS %u.\n", allscore);
			mytext.write_y(100,temp, DARK_BLUE, 1);
		}
		mytext.write_y(120,"**" CONTINUE_ACTION_STRING " TO CONTINUE.**", DARK_BLUE, 1);
		// Save the game status to a temp file (savetemp.gtl)
		for (i=0; i < 4; i++)
		{
			save_data.m_totalscore[i] += save_data.m_score[i];
			save_data.m_totalcash[i] += (save_data.m_score[i]*2);
		}
		for (i=0; i < 4; i++)
		{
			bonuscash[i] = (save_data.m_score[i] * (TIME_BONUS + ((Sint32)level_data.par_value * LEVEL_BONUS) - myscreen->framecount))/(TIME_BONUS + ( ((Sint32)level_data.par_value * LEVEL_BONUS)/2));
			if (bonuscash[i] < 0 || myscreen->framecount > TIME_BONUS) // || framecount < 0)
				bonuscash[i] = 0;
			save_data.m_totalcash[i] += bonuscash[i];
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for (i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
		sprintf(temp,"YOUR TIME BONUS IS %u.\n",allbonuscash);
		mytext.write_y(110,temp, DARK_BLUE, 1);
		myscreen->buffer_to_screen(0, 0, 320, 200);

		// Zardus: FIX: get_input_events should really be used instead of query_key while waiting for
		// actions
		clear_keyboard();
		while (!query_input_continue())
			get_input_events(WAIT); // pause

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
