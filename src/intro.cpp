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
//intro.cpp
/* ChangeLog
	buffers: 8/9/02: *added return 0 to finish func
*/

#include "graph.h"
//#include "pixie.h"
// Z's script: #include <process.h>

#define SHOW_TIME 130
#include "input.h"

int show();
int show(int howlong);
int cleanup();

int pal[256][3];
char mypalette[768];
//screen *myscreen;

void intro_main(long argc, char** argv)
{
	int i,j;
	// Zardus: PORT: doesn't seem to be used, and causes a memory leak
	//char **args = (char **)new int;
	unsigned char red, green, blue;
	double delay;
	int xnewsize, ynewsize;
	text *mytext;
	pixie *gladiator;
	pixie *bigfoot;
	pixie *ul, *ur, *ll, *lr; // for full-screen displays
	unsigned char *uldata, *urdata, *lldata, *lrdata;
	unsigned char *gladdata, *bigdata;
	int currentcycle = 0, cycletime = 3;
	char message[80];

#if 0
	if (!get_pix_directory())
		exit(1);
#endif

	ul = ur = ll = lr = NULL;

	if (!myscreen)
		myscreen = new screen(1);

	myscreen->viewob[0]->resize(PREF_VIEW_FULL);
	grab_timer();
	grab_keyboard();
	mytext = new text(myscreen);
	load_and_set_palette("our.pal", (char *)mypalette);
	//  load_scenario("current", myscreen);
	//buffers: PORT:  for (i=0;i<256;i++)
	//buffers: PORT:         set_palette_reg(i,0,0,0);
	myscreen->clear();

	gladdata = read_pixie_file("3mages2.pix");
	gladiator = new pixie(&(gladdata[3]), (int)gladdata[1],
	                      (int)gladdata[2], myscreen);
	gladiator->drawMix(120,55,myscreen->viewob[0]);
	mytext->write_y(100,"FORGOTTEN SAGES PRESENTS", 230, myscreen->viewob[0]);
	myscreen->refresh();
	delete gladiator;
	delete gladdata;

	if (show() < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	//gladdata = read_pixie_file("glad.pix");
	gladdata = read_pixie_file("glad2.pix");
	gladiator = new pixie(&(gladdata[3]), (int)gladdata[1],
	                      (int)gladdata[2], myscreen);
	bigdata = read_pixie_file("bigfoot.pix");
	bigfoot = new pixie(&(bigdata[3]), (int)bigdata[1],
	                    (int)bigdata[2], myscreen);
	myscreen->clear();
	bigfoot->drawMix(120,50,myscreen->viewob[0]);
	//gladiator->drawMix(110,65,myscreen->viewob[0]);
	gladiator->drawMix(100, 110, myscreen->viewob[0]);
	myscreen->refresh();

	delete gladdata;
	delete gladiator;
	delete bigfoot;
	delete bigdata;

	if (show() < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	myscreen->clear();
	mytext->write_y(70,"THOSE WHO ARE ABOUT TO DIE SALUTE YOU", 230, myscreen->viewob[0]);
	myscreen->refresh();

	if (show() < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	// Programming Credits, Page 1
	myscreen->clear();
	mytext->write_y(80,"Programming By:", 230, myscreen->viewob[0]);
	mytext->write_y(100,"Chad Lawrence  Doug McCreary", 230, myscreen->viewob[0]);
	mytext->write_y(110,"Tom Ricket  Michael Scandizzo", 230, myscreen->viewob[0]);

	myscreen->refresh();

	if (show() < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	// First 'interlude' snapshot
	myscreen->clear();
	uldata = read_pixie_file("game2ul.pix");
	ul = new pixie(&(uldata[3]), (int)uldata[1],
	               (int)uldata[2], myscreen);
	ul->setxy(41, 12);
	ul->draw(myscreen->viewob[0]);
	delete ul;
	delete uldata;
	ul = NULL;

	urdata = read_pixie_file("game2ur.pix");
	ur = new pixie(&(urdata[3]), (int)urdata[1],
	               (int)urdata[2], myscreen);
	ur->setxy(160, 12);
	ur->draw(myscreen->viewob[0]);
	delete ur;
	delete urdata;
	ur = NULL;

	lldata = read_pixie_file("game2ll.pix");
	ll = new pixie(&(lldata[3]), (int)lldata[1],
	               (int)lldata[2], myscreen);
	ll->setxy(41, 103);
	ll->draw(myscreen->viewob[0]);
	delete ll;
	delete lldata;
	ll = NULL;

	lrdata = read_pixie_file("game2lr.pix");
	lr = new pixie(&(lrdata[3]), (int)lrdata[1],
	               (int)lrdata[2], myscreen);
	lr->setxy(160, 103);
	lr->draw(myscreen->viewob[0]);
	delete lr;
	delete lrdata;
	lr = NULL;

	myscreen->refresh();

	if (show(SHOW_TIME+30) < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	// Programming Credits, Page 2
	myscreen->clear();
	mytext->write_y(90,"Additional Coding by Doug Ricket", 230, myscreen->viewob[0]);
	//buffers: PORT: w00t w00t
	mytext->write_y(110,"Ported by Odo and Zardus",230,myscreen->viewob[0]);
	myscreen->refresh();

	if (show() < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	// Second 'interlude' & extra credits
	myscreen->clear();
	uldata = read_pixie_file("game4.pix");
	ul = new pixie(&(uldata[3]), (int)uldata[1],
	               (int)uldata[2], myscreen);
	ul->setxy(0, 0);
	ul->draw(myscreen->viewob[0]);
	delete ul;
	delete uldata;
	ul = NULL;

	lldata = read_pixie_file("game5.pix");
	ll = new pixie(&(lldata[3]), (int)lldata[1],
	               (int)lldata[2], myscreen);
	ll->setxy(160, 78);
	ll->draw(myscreen->viewob[0]);
	delete ll;
	delete lldata;
	ll = NULL;

	strcpy(message, "Additional Artwork By:");
	mytext->write_xy(310-mytext->query_width(message),
	                 30, message, 230, myscreen->viewob[0]);
	strcpy(message, "Doug Ricket");
	mytext->write_xy(310-mytext->query_width(message),
	                 50, message, 230, myscreen->viewob[0]);
	strcpy(message, "Stefan Scandizzo");
	mytext->write_xy(310-mytext->query_width(message),
	                 60, message, 230, myscreen->viewob[0]);

	strcpy(message, "Special Thanks To:");
	mytext->write_xy(2, 130, message, 230, myscreen->viewob[0]);
	strcpy(message, "Kim Kelly  Lara Kirkendall");
	mytext->write_xy(2, 150, message, 230, myscreen->viewob[0]);
	strcpy(message, "Lee Martin  Karyn McCreary");
	mytext->write_xy(2, 160, message, 230, myscreen->viewob[0]);
	strcpy(message, "Loki, Ishara, & Mootz");
	mytext->write_xy(2, 170, message, 230, myscreen->viewob[0]);
	strcpy(message, "And many others!");
	mytext->write_xy(2, 180, message, 230, myscreen->viewob[0]);

	myscreen->refresh();

	if (show(SHOW_TIME*4) < 0)
	{
		delete mytext;
		cleanup();
		return;
	}

	// cleanup
	/*
	  for (i = 0; i<256; i++)
	  {
	         red = pal[i][0];
	         green = pal[i][1];
	         blue = pal[i][2];
	         set_palette_reg(i, red, green, blue);
	  }
	*/

	delete mytext;
	cleanup();
}

int cleanup()
{
	long i;
	int red,green,blue; //buffers: PORT: changed to ints
	query_palette_reg((unsigned char)0, &red, &green, &blue); // Resets palette to read mode
	release_timer();
	release_keyboard();
	myscreen->clear();
	myscreen->refresh();

	for (i = 0; i<256; i++)
	{
		red = pal[i][0];
		green = pal[i][1];
		blue = pal[i][2];
		set_palette_reg(i, red, green, blue);
	}
	load_and_set_palette("our.pal", (char *)mypalette);
	return 1;
}

int show() // default uses SHOW_TIME
{
	return show(SHOW_TIME);
}

int show(int howlong)
{
	short i,j;
	int red,green,blue; //buffers: PORT: changed to ints from chars
	//*******************************
	// Fade in loop
	//*******************************
	/* buffers: PORT:
	  for(j=0;j<64;j++)
	  {
	  reset_timer();
	  while (query_timer() < 1);
	 
	         for(i=0;i<256;i++)
	         {
	                query_palette_reg((unsigned char)i,&red,&green,&blue);
	                if (red < mypalette[i*3]) red++;
	                if (green < mypalette[i*3+1]) green++;
	                if (blue < mypalette[i*3+2]) blue++;
	                set_palette_reg((unsigned char)i,red,green,blue);
	         }
	 
	  }
	*/ //buffers: PORT
	//  myscreen->soundp->play_sound(SOUND_TELEPORT);
	reset_timer();
	while (query_timer() < howlong)
	{
		get_input_events(POLL);
		if (query_key_press_event())
			return -1;
	}

	// Fade out
	/* //buffers: PORT:
	for (i = 0; i<256; i++)
	{
	       query_palette_reg((unsigned char)i, &red, &green, &blue);
	       pal[i][0] = red;
	       pal[i][1] = green;
	       pal[i][2] = blue;
	}
	for(j = 0; j <= 64; j++)
	{
	       reset_timer();
	       while (query_timer() < 1);

	       for (i = 0; i < 256; i++)
	       {
	              query_palette_reg((unsigned char)i, &red, &green, &blue);
	              if (red) red--;
	              if (green) green--;
	              if(blue) blue--;
	              set_palette_reg((unsigned char)i, red, green, blue);
	       }
	}
	*/ //buffers: PORT:
	return 1;
}
