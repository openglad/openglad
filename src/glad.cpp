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
#include "config.h"
#include "graph.h"

screen * myscreen;
smoother * mysmoother;

#include "colors.h"
#include <time.h>
#include "parser.h"
#include <string>
#include "util.h"
using namespace std;

// Z's script: #include <process.h>

void picker_main(long argc, char **argv);
void intro_main(long argc, char **argv);

short remaining_foes(screen *myscreen, char myteam);
short remaining_team(screen *myscreen, char myteam);
short score_panel(screen *myscreen);
short score_panel(screen *myscreen, short do_it);
short new_score_panel(screen *myscreen, short do_it);
void draw_value_bar(short left, short top, walker * control, short mode,screen * myscreen);
void new_draw_value_bar(short left, short top,
                        walker  * control, short mode, screen * myscreen);
void draw_percentage_bar(short left, short top, unsigned char somecolor,
                         short somelength, screen * myscreen);
void init_input();

void draw_radar_gems(screen  *myscreen);
void draw_gem(short x, short y, short color, screen * myscreen);

unsigned char *radarpic;
pixie *radarpix;

void create_dataopenglad();

void glad_main(screen *myscreen, long playermode);

// Zardus: FIX: from view.cpp. We need this here so that it doesn't
// try to create it before main and go nuts trying to load it
extern options *theprefs;

int main(int argc, char *argv[])
{
	char * filepath;
	filepath = get_file_path("openglad.cfg");
	cfg.parse(filepath);
	free(filepath);
	cfg.commandline(argc, argv);
	create_dataopenglad();

	theprefs = new options;
	myscreen = new screen(1);

	//buffers: setting the seed
	srand(time(NULL));

	init_input();
	intro_main(argc, argv);
	picker_main(argc, argv);
	return 0;
}

void glad_main(screen *myscreen, long playermode)
{
	unsigned char input;
	char somemessage[80];
	//  char soundpath[80];
	//  short cyclemode = 1;            // color cycling on or off
	char *keyboard;
	short dumbcount=0;
	short numviews;
	oblink *here, *before;
	text gladtext(myscreen);

	//long longtemp;
	//char message[50];
	short currentcycle = 0, cycletime = 3;

	numviews = (short) playermode;

	//screen  *myscreen;

	// Get sound path ..
	//if (!get_cfg_item("directories", "sound") )
	//     exit(1);
	//strcpy(soundpath, get_cfg_item("directories", "sound") );

#if 0
	// Do this BEFORE getting interrupts ..
	if (!get_pix_directory())
		exit(1);
#endif

	// Zardus: PORT: fade out
	clear_keyboard();
	myscreen->fadeblack(0);

	if (myscreen)
	{
		//myscreen->reset(numviews);
	}
	else
		myscreen = new screen(numviews);
	myscreen->clearbuffer();

	// Draw rainbow background
	//for (i = 0; i<320; i++)
	//  for (j = 0; j < 200; j++)
	//    myscreen->point(i,j,(unsigned char) (i-j)); //not sure if this is ok


	// Load the default saved-game ..
	load_saved_game("save0", myscreen); // over-rides current.fss ..

	// Zardus: PORT: doesn't seem to be neccessary
	// Prepare screen for fade in
	//myscreen->draw_panels(myscreen->numviews);

	//*******************************
	// Fade in
	//*******************************
	myscreen->input(0);
	myscreen->redraw();
	myscreen->fadeblack(1);


	//******************************
	// Keyboard loop
	//******************************


	//
	// This is the main program loop
	//
	keyboard = query_keyboard();

	//sprintf(somemessage, "SPEED SET TO %d", (20-myscreen->timer_wait)/2+1);

	strcpy(somemessage, "OPENGLAD V.");
	strcat(somemessage, PACKAGE_VERSION);
	myscreen->viewob[0]->set_display_text(somemessage, 100);
	myscreen->viewob[0]->set_display_text("PRESS F1 FOR HELP", 100);

	myscreen->redraw();
	myscreen->refresh();
	read_scenario(myscreen);
	myscreen->redrawme = 1;
	myscreen->framecount = 0;
	myscreen->timerstart = query_timer_control();

	while(1)
	{
		// Reset the timer count to zero ...
		reset_timer();

		myscreen->clearfontbuffer();

		input = (char) query_key();

		if (myscreen->redrawme)
		{
			myscreen->draw_panels(myscreen->numviews);
			score_panel(myscreen, 1);
			myscreen->refresh();
			//score_panel(myscreen, 1);
			myscreen->redrawme = 0;
		}
		if (myscreen->end)
			break;
		myscreen->act();
		myscreen->framecount++;
		if (myscreen->end)
			break;
		myscreen->redraw();
		//         myscreen->buffer_to_screen(0, 0, 320, 200);
		// this was for debugging illegal draws to bad areas.
		score_panel(myscreen);
		myscreen->refresh();

		myscreen->input(input);
		if (myscreen->end)
			break;

		//score_panel(myscreen);

		//if (input == SDLK_ESCAPE) break;
		if (keyboard[SDLK_ESCAPE])
		{
			//buffers: PORT: we will redo this: set_palette(myscreen->redpalette);
			myscreen->clearfontbuffer(160-80,80,160,40);
			dumbcount = myscreen->draw_dialog(160-80, 80, 160+80, 120, "Abort Mission");
			gladtext.write_xy(dumbcount, 80+24, "Quit this Mission? (Y/N)",
			                  (unsigned char) DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200); // refresh screen
			while (!keyboard[SDLK_y] && !keyboard[SDLK_n])
				get_input_events(WAIT);
			myscreen->redrawme = 1;
			if (keyboard[SDLK_y]) // player wants to quit
				break;
			else
			{
				set_palette(myscreen->ourpalette);  // restore normal palette
				adjust_palette(myscreen->ourpalette, myscreen->viewob[0]->gamma);
			}
		}

		// Now cycle palette ..
		if (myscreen->cyclemode)
			myscreen->do_cycle(currentcycle++, cycletime);

		// Zardus: PORT: this is the new FPS cap
		time_delay(myscreen->timer_wait - query_timer());

		// Zardus: PORT: this is the old FPS cap
		// Now check to see if we're slow enough
		//if (query_timer() < myscreen->timer_wait)
		//{
		//	while(query_timer() < myscreen->timer_wait)
		//	{} //do nothing until we are ready to go to next frame
		//}

	}

	clear_keyboard();

	// Delete all of our current information and abort ..
	here = myscreen->oblist;
	while (here)
	{
		if (here->ob)
			//myscreen->remove_ob(here->ob);
			delete here->ob;
		before = here;
		here = here->next;
		delete before;
		myscreen->oblist = here;
	}
	here = myscreen->weaplist; // do the weapons
	while (here)
	{
		if (here->ob)
			//myscreen->remove_ob(here->ob);
			delete here->ob;
		before = here;
		here = here->next;
		delete before;
		myscreen->weaplist = here;
	}
	here = myscreen->fxlist; // do the fx's
	while (here)
	{
		if (here->ob)
			//myscreen->remove_fx_ob(here->ob);
			delete here->ob;
		before = here;
		here = here->next;
		delete before;
		myscreen->fxlist = here;
	}
	return; // return to picker
	//  return 1;
}

// remaining_foes returns # of livings left not on control's team
short remaining_foes(screen *myscreen, char myteam)
{
	oblink  *here;
	short myfoes = 0;

	here = myscreen->oblist;
	while (here)
	{
		if (here->ob && !here->ob->dead &&
		        (here->ob->query_order() == ORDER_LIVING) &&
		        (myteam != here->ob->team_num) )
			myfoes++;
		here = here->next;
	}

	return myfoes;
}

// remaining_team returns # of livings left on team myteam
short remaining_team(screen *myscreen, char myteam)
{
	oblink  *here;
	short myfoes = 0;

	here = myscreen->oblist;
	while (here)
	{
		if (here->ob && !here->ob->dead &&
		        (here->ob->query_order() == ORDER_LIVING) &&
		        (myteam == here->ob->team_num) )
			myfoes++;
		here = here->next;
	}

	return myfoes;
}

short score_panel(screen *myscreen)
{
	return score_panel(myscreen, 0);
}

short score_panel(screen *myscreen, short do_it)
{
	text *mytext = new text(myscreen, TEXT_1);

	delete mytext;
	return new_score_panel(myscreen, 1);

}

void draw_radar_gems(screen  *myscreen)
{
	short upper_left_x = 246;
	short upper_left_y = 140;
	short upper_right_x = upper_left_x + 65;
	short upper_right_y = upper_left_y;
	short lower_left_x = upper_left_x;
	short lower_left_y = upper_left_y + 49;
	short lower_right_x = upper_right_x;
	short lower_right_y = lower_left_y;

	short team_light;

	static short old_team_num = -1;
	if (old_team_num == myscreen->viewob[0]->control->team_num)
		return;
	old_team_num = myscreen->viewob[0]->control->team_num;

	team_light = myscreen->viewob[0]->control->query_team_color();

	draw_gem(upper_left_x, upper_left_y, team_light,myscreen);
	draw_gem(upper_right_x, upper_right_y, team_light,myscreen);
	draw_gem(lower_left_x, lower_left_y, team_light,myscreen);
	draw_gem(lower_right_x, lower_right_y, team_light,myscreen);
}

void draw_gem(short x, short y, short color, screen * myscreen)
{
	short light = color;
	short med = light +2;
	short darker = med +2;
	short darkest = darker +2;

	myscreen->point(x, y, light);
	myscreen->point(x-1, y+1, light);
	myscreen->point(x, y+1, med);
	myscreen->point(x+1, y+1, darker);
	myscreen->point(x-2, y+2, light);
	myscreen->hor_line(x-1, y+2, 3, med);
	myscreen->point(x+2, y+2, darkest);
	myscreen->point(x-1, y+3, darker);
	myscreen->point(x, y+3, med);
	myscreen->point(x+1, y+3, darkest);
	myscreen->point(x, y+4, darkest);
}

void draw_value_bar(short left, short top,
                    walker  * control, short mode, screen * myscreen)
{
	short points;
	short totallength = 60;
	short bar_length=0;
	short bar_remainder = totallength - bar_length;
	short i, j;
	char whatcolor;

	if (mode == 0) // hitpoint bar
	{
		points = control->stats->hitpoints;

		if ( (points * 3) < control->stats->max_hitpoints)
			whatcolor = LOW_HP_COLOR;
		else if ( (points * 3 / 2) < control->stats->max_hitpoints)
			whatcolor = MID_HP_COLOR;
		else if (points < control->stats->max_hitpoints)
			whatcolor = HIGH_HP_COLOR;
		else if (points == control->stats->max_hitpoints)
			whatcolor = MAX_HP_COLOR;
		else
			whatcolor = ORANGE_START;

		if (points > control->stats->max_hitpoints)
			bar_length = 60;
		else
			bar_length =(short) points * 60 / control->stats->max_hitpoints;
		bar_remainder = 60 - bar_length;

		myscreen->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
		//myscreen->fastbox(left, top, 61, 6, BOX_COLOR, 1);
		if ( points > control->stats->max_hitpoints)
			for (i=0;i<bar_length/2;i++)
				for (j=0; j<3; j++)
				{
					myscreen->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, whatcolor+((i+j)%16));
					myscreen->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, whatcolor+((i+j)%16));
					myscreen->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, whatcolor+((i+j)%16));
					myscreen->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, whatcolor+((i+j)%16));
				}
		else
			myscreen->fastbox(left+1, top+1, bar_length, 5, whatcolor);
		myscreen->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

		for (i=0;i<4;i++)
		{
			//upper left
			myscreen->ver_line(left+i, top, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+i, top, 2-i, 27);
			//upper right
			myscreen->ver_line(left+61-i, top, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+61-i, top, 2-i, 27);
			//lower left
			myscreen->ver_line(left+i, top+4+i, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+i, top+5+i, 2-i, 27);
			//lower right
			myscreen->ver_line(left+61-i, top+4+i, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+61-i, top+5+i, 2-i, 27);
		}
	}  // end of doing hp stuff..
	else if (mode == 1) // sp stuff ..
	{
		points = control->stats->magicpoints;

		if ( (points * 3) < control->stats->max_magicpoints)
			whatcolor = LOW_MP_COLOR;
		else if ( (points * 3 / 2) < control->stats->max_magicpoints)
			whatcolor = MID_MP_COLOR;
		else if (points < control->stats->max_magicpoints)
			whatcolor = HIGH_MP_COLOR;
		else if (points == control->stats->max_magicpoints)
			whatcolor = MAX_MP_COLOR;
		else
			whatcolor = WATER_START;

		if (points > control->stats->max_magicpoints)
			bar_length = 60;
		else
			bar_length =(short) points * 60 / control->stats->max_magicpoints;
		bar_remainder = 60 - bar_length;

		myscreen->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
		if ( points > control->stats->max_magicpoints)
			for (i=0;i<bar_length/2;i++)
				for (j=0; j<3; j++)
				{
					myscreen->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, whatcolor+((i+j)%16));
					myscreen->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, whatcolor+((i+j)%16));
					myscreen->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, whatcolor+((i+j)%16));
					myscreen->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, whatcolor+((i+j)%16));
				}
		else
			myscreen->fastbox(left+1, top+1, bar_length, 5, whatcolor);
		myscreen->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

		for (i=0;i<4;i++)
		{
			//upper left
			myscreen->ver_line(left+i, top, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+i, top, 2-i, 27);
			//upper right
			myscreen->ver_line(left+61-i, top, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+61-i, top, 2-i, 27);
			//lower left
			myscreen->ver_line(left+i, top+4+i, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+i, top+5+i, 2-i, 27);
			//lower right
			myscreen->ver_line(left+61-i, top+4+i, 3-i, 0);
			if ((2-i)>0)
				myscreen->ver_line(left+61-i, top+5+i, 2-i, 27);
		}
	} // end of sp stuff
} // end of drawing routine ..

void new_draw_value_bar(short left, short top,
                        walker  * control, short mode, screen * myscreen)
{
	short points;
	short totallength = 60;
	short bar_length=0;
	short bar_remainder = totallength - bar_length;
	char whatcolor;

	if (mode == 0) // hitpoint bar
	{
		points = control->stats->hitpoints;

		if ( (points * 3) < control->stats->max_hitpoints)
			whatcolor = LOW_HP_COLOR;
		else if ( (points * 3 / 2) < control->stats->max_hitpoints)
			whatcolor = MID_HP_COLOR;
		else if (points < control->stats->max_hitpoints)
			whatcolor = HIGH_HP_COLOR;
		else if (points == control->stats->max_hitpoints)
			whatcolor = MAX_HP_COLOR;
		else
			whatcolor = ORANGE_START;

		if (points > control->stats->max_hitpoints)
			bar_length = 60;
		else
			bar_length =(short) points * 60 / control->stats->max_hitpoints;
		bar_remainder = 60 - bar_length;

		draw_percentage_bar(left, top, BAR_BACK_COLOR, 60, myscreen);
		draw_percentage_bar(left, top, whatcolor, bar_length, myscreen);

	}  // end of doing hp stuff..
	else if (mode == 1) // sp stuff ..
	{
		points = control->stats->magicpoints;

		if ( (points * 3) < control->stats->max_magicpoints)
			whatcolor = LOW_MP_COLOR;
		else if ( (points * 3 / 2) < control->stats->max_magicpoints)
			whatcolor = MID_MP_COLOR;
		else if (points < control->stats->max_magicpoints)
			whatcolor = HIGH_MP_COLOR;
		else if (points == control->stats->max_magicpoints)
			whatcolor = MAX_MP_COLOR;
		else
			whatcolor = WATER_START;

		if (points > control->stats->max_magicpoints)
			bar_length = 60;
		else
			bar_length =(short) points * 60 / control->stats->max_magicpoints;
		bar_remainder = 60 - bar_length;

		draw_percentage_bar(left, top, BAR_BACK_COLOR, 60, myscreen);
		draw_percentage_bar(left, top, whatcolor, bar_length, myscreen);

	} // end of sp stuff
} // end of drawing routine ..


short new_score_panel(screen *myscreen, short do_it)
{
#define L_D(x) x*8
	char message[50];
	//static
	char tempname[20];
	short tempfoes = 0;
	short players;
	short tempallies = 0;
	text *mytext = new text(myscreen, TEXT_1);
#if 0
	static unsigned long family[5]={-1,-1,-1,-1,-1},
	                               act[5]={-1, -1,-1,-1,-1};
#endif

	walker  *control;
	short lm, tm; // left and top margins
	short rm, bm; // right and bottom margins
	char draw_button;  // do we draw a button background?
	char text_color;
	static char namelist[NUM_FAMILIES][20] =
	    { "SOLDIER", "ELF", "ARCHER", "MAGE",
	      "SKELETON", "CLERIC", "ELEMENTAL",
	      "FAERIE", "SLIME", "SLIME", "SLIME",
	      "THIEF", "GHOST", "DRUID", "ORC",
	      "ORC CAPTAIN", "BARBARIAN", "ARCHMAGE",
	      "GOLEM", "GIANT SKEL", "TOWER",
	    };

	unsigned long myscore;
	static unsigned long scorecountup[4] = {
	                                           myscreen->m_score[0],
	                                           myscreen->m_score[1],
	                                           myscreen->m_score[2],
	                                           myscreen->m_score[3],
	                                       }
	                                       ;

	for (players = 0; players < myscreen->numviews; players++)
	{
		control = myscreen->viewob[players]->control;
		lm = myscreen->viewob[players]->xloc;
		tm = myscreen->viewob[players]->yloc;
		rm = myscreen->viewob[players]->endx;
		bm = myscreen->viewob[players]->endy;
		if (control && !control->dead)
		{
			// Get the button-drawing info ..
			draw_button = myscreen->viewob[players]->prefs[PREF_OVERLAY];
			if (draw_button)
				text_color = DARK_BLUE;
			else
				text_color = YELLOW;

			// Get current number of foes
			tempfoes = remaining_foes(myscreen, control->team_num);
			// Get current number of team-members
			tempallies = remaining_team(myscreen, control->team_num);

			// Draw the pretty gems
			//draw_radar_gems(myscreen);

			// Display name or type, upper left
			if (control->myguy)
				strcpy(tempname, control->myguy->name);
			else if ( strlen(control->stats->name) )
				strcpy(tempname, control->stats->name);
			else
				strcpy(tempname, namelist[control->query_family()]);

			//buffers: the name[] var doesn't seem to be used other then
			//buffers: here so i just commented it.
			//strcpy(name[players], tempname);
			//buffers: this strcpy actually copies the name to be displayed
			strcpy(message, tempname);

			if (draw_button)
				myscreen->draw_button(lm+1, tm+2, lm+63, tm+9, 1, 1);

			mytext->write_xy(lm+3, tm+4, message, text_color, 1);

			// HP/MP bars; dependent on user settings
			switch (myscreen->viewob[players]->prefs[PREF_LIFE])
			{
				case PREF_LIFE_TEXT: // display numeric values only
					if (draw_button)
						myscreen->draw_button(lm+1, tm+10, lm+63, tm+26, 1, 1);
					sprintf(message, "HP: %d", (unsigned ) control->stats->hitpoints);
					mytext->write_xy(lm+5, tm+12, message, text_color, (short) 1); // to buffer
					sprintf(message, "MP: %d", (unsigned) control->stats->magicpoints);
					mytext->write_xy(lm+5, tm+20, message, text_color, (short) 1);
					break; // end of 'text' case
				case PREF_LIFE_BARS: // display graphical bars only
					//if (draw_button)
					//  myscreen->draw_button(lm+1, tm+9, lm+63, tm+25, 1, 1);
					new_draw_value_bar(lm+2, tm+10, control, 0, myscreen);
					new_draw_value_bar(lm+2, tm+18, control, 1, myscreen);
					break; // end of 'bars' case
				case PREF_LIFE_OFF: // do nothing
					break;
				case PREF_LIFE_BOTH: // default case
				default:
					// HP STATUS BAR
					// HP_COLOR's are defined in graph.h
					//if (draw_button)
					//  myscreen->draw_button(lm+1, tm+9, lm+63, tm+25, 1, 1);
					new_draw_value_bar(lm+2, tm+10, control, 0, myscreen);
					sprintf(message, "HP: %d", (unsigned ) control->stats->hitpoints);
					mytext->write_xy(lm+5, tm+11, message, (unsigned char) BLACK, (short) 1); // to buffer

					//SP BAR
					//COLORS DEFINED IN GRAPH.H
					new_draw_value_bar(lm+2, tm+18, control, 1, myscreen);
					sprintf(message, "MP: %d", (unsigned) control->stats->magicpoints);
					mytext->write_xy(lm+5, tm+19, message, (unsigned char) BLACK, (short) 1);
					break; // end of 'both' case
			} // end of HP/MP display case

			if (myscreen->viewob[players]->prefs[PREF_SCORE] == PREF_SCORE_ON)
			{
				// Score, bottom left corner

				// Draw box, if needed
				if (draw_button)
					myscreen->draw_button(lm+1, bm-26, lm+98, bm-2, 1, 1);

				// Get our score ..
				if (control)
					myscore = myscreen->m_score[control->team_num];
				else
					myscore = 0;
				if (scorecountup[control->team_num] > myscore)
					scorecountup[control->team_num] = myscore;
				if (scorecountup[control->team_num] < myscore)
				{
					scorecountup[control->team_num]++;
					scorecountup[control->team_num] += (unsigned long) random( (myscore - scorecountup[control->team_num])/12 );
				}
				if (scorecountup[control->team_num] > myscore)
					scorecountup[control->team_num] = myscore;
				myscreen->m_score[control->team_num] = myscore;
				//above should count up the score towards the current amount
				sprintf(message, "SC: %ld", scorecountup[control->team_num]);
				mytext->write_xy(lm+2, bm-8, message, text_color, (short) 1);

				// Level or exp, 2nd bottom left
				if (control->myguy)
					sprintf(message, "XP: %ld", control->myguy->exp);
				else
					sprintf(message, "LEVEL: %i", control->stats->level);
				mytext->write_xy(lm+2, bm-16, message, text_color, (short) 1);

				// Currently-select special
				if (control->shifter_down &&
				        strcmp(myscreen->alternate_name[control->query_family()][control->current_special], "NONE") )
					sprintf(message, "SPC: %s", myscreen->alternate_name[control->query_family()][control->current_special]);
				else
					sprintf(message, "SPC: %s", myscreen->special_name[control->query_family()][control->current_special]);
				if (control->stats->magicpoints >= control->stats->special_cost[control->current_special])
					mytext->write_xy(lm+2, bm-24, message, text_color, (short) 1);
				else
					mytext->write_xy(lm+2, bm-24, message, (unsigned char) RED, (short) 1);

			} // end of score/exp display

			// Skip act-type for now
			/*
			  if (do_it || (act[0] != myscreen->viewob[0]->control->query_old_act_type()) )
			  {
			    act[0] = myscreen->viewob[0]->control->query_old_act_type();
			    myscreen->fastbox(S_RIGHT+18,S_UP+65,47,7,27);
			    switch(myscreen->viewob[0]->control->query_old_act_type())
			    {
			           case ACT_RANDOM: strcpy(message, "CHARGE"); break;
			           case ACT_GUARD: strcpy(message, "GUARD"); break;
			           default: break;
			    }
			    mytext->write_xy(S_RIGHT+18,S_UP+65,message, text_color, 1);
			  }
			*/

			// Number of allies, upper right
			if (myscreen->viewob[players]->prefs[PREF_FOES] == PREF_FOES_ON)
			{
				if (draw_button)
					myscreen->draw_button(rm-57, tm+1, rm-2, tm+16, 1, 1);

				sprintf(message, "TEAM: %d", tempallies);
				mytext->write_xy(rm - 55, tm+2, message, text_color, (short) 1);

				// Number of foes, 2nd upper right
				sprintf(message, "FOES: %d", tempfoes);
				mytext->write_xy(rm-55, tm+10, message, text_color, (short) 1);
			}

			//if (do_it && 0) // redraw radar border
			//    myscreen->putdata(244, 140, radarpic[1], radarpic[2], &(radarpic[3]) );
		}
	} // end of one-player mode
	delete mytext;
	return 1;

}

void draw_percentage_bar(short left, short top, unsigned char somecolor,
                         short somelength, screen * myscreen)
{
	short i, j;
	unsigned char tempcolor;

	// Draw the black border ..
	myscreen->fastbox(left+2, top, somelength-4, 1, 0, 1);
	myscreen->fastbox(left+1, top+1, 1,  1, 0, 1);
	myscreen->fastbox(left+58, top+1, 1, 1, 0, 1);
	myscreen->fastbox(left,  top+2, 1, 3,   0, 1);
	myscreen->fastbox(left+59, top+2, 1, 3, 0, 1);
	myscreen->fastbox(left+1, top+5, 1,  1, 0, 1);
	myscreen->fastbox(left+58, top+5, 1, 1, 0, 1);
	myscreen->fastbox(left+2, top+6, somelength-4, 1, 0, 1);

	// Draw the box ..
	myscreen->fastbox(left+2, top+1, somelength-4, 1, somecolor, 1);
	myscreen->fastbox(left+1, top+2, somelength-2, 3, somecolor, 1);
	myscreen->fastbox(left+2, top+5, somelength-4, 1, somecolor, 1);

	if ( (somecolor == ORANGE_START) || (somecolor == WATER_START) ) // rotating colors .. do special ..
	{
		tempcolor = somecolor; //+((i+j)%16

		//myscreen->fastbox(left+1, top+2, 1, top+4
		//for (j=2; j < 3; j++)
		//{
		//  myscreen->fastbox(left+1, top+1+(2-j), 1, 1, somecolor+(j%16), 1);
		//  myscreen->fastbox(left+1, top+1+(2+j), 1, 1, somecolor+(j%16), 1);
		//}
		for (i=0; i < (somelength-4)/2;i++)
			for (j=0; j < 3; j++)
			{
				myscreen->fastbox(left+(somelength/2)-i-1, top+1+(2-j), 1, 1, somecolor+((i+j)%16), 1);
				myscreen->fastbox(left+(somelength/2)-i-1, top+1+(2+j), 1, 1, somecolor+((i+j)%16), 1);
				myscreen->fastbox(left+i+(somelength/2),   top+1+(2-j), 1, 1, somecolor+((i+j)%16), 1);
				myscreen->fastbox(left+i+(somelength/2),   top+1+(2+j), 1, 1, somecolor+((i+j)%16), 1);
			}
	} // end of special color check ..

}
