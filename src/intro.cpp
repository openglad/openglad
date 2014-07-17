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
#define FROM 1
#define TO 0
#include "input.h"

int show();
int show(int howlong);
int cleanup();

int pal[256][3];
unsigned char mypalette[768];
//screen *myscreen;

void intro_main(Sint32 argc, char** argv)
{
	// Zardus: PORT: doesn't seem to be used, and causes a memory leak
	//char **args = (char **)new int;
	text& mytext = myscreen->text_normal;
	pixie *gladiator;
	pixie *bigfoot;
	pixie *ul, *ur, *ll, *lr; // for full-screen displays
	PixieData uldata, urdata, lldata, lrdata;
	PixieData gladdata, bigdata;
	char message[80];

	ul = ur = ll = lr = NULL;

	myscreen->viewob[0]->resize(PREF_VIEW_FULL);
	grab_timer();
	
	load_and_set_palette("our.pal", mypalette);
	//  load_scenario("current", myscreen);
	//buffers: PORT:  for (i=0;i<256;i++)
	//buffers: PORT:         set_palette_reg(i,0,0,0);
    
    myscreen->fadeblack(TO);
    
	myscreen->clear();
	gladdata = read_pixie_file("dinomage.pix");
	gladiator = new pixie(gladdata);
	gladiator->drawMix(120,55,myscreen->viewob[0]);
	mytext.write_y(120,"DinoMage Games presents", 230, myscreen->viewob[0]);
	delete gladiator;
	gladdata.free();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	myscreen->clear();
	gladdata = read_pixie_file("glad2.pix");
	gladiator = new pixie(gladdata);
	bigdata = read_pixie_file("bigfoot.pix");
	bigfoot = new pixie(bigdata);
	                    
	bigfoot->drawMix(120,50,myscreen->viewob[0]);
	gladiator->drawMix(100, 110, myscreen->viewob[0]);

	gladdata.free();
	delete gladiator;
	delete bigfoot;
	bigdata.free();

	if (show() < 0)
	{
		cleanup();
		return;
	}
	
	myscreen->clear();
	gladdata = read_pixie_file("3mages2.pix");
	gladiator = new pixie(gladdata);
	gladiator->drawMix(120,55,myscreen->viewob[0]);
	mytext.write_y(105,"GAME BY   ", 230, myscreen->viewob[0]);
	mytext.write_y(120,"FORGOTTEN SAGES   ", 230, myscreen->viewob[0]);
	delete gladiator;
	gladdata.free();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	// Programming Credits, Page 1
	myscreen->clear();
	mytext.write_y(80,"Programming By:", 230, myscreen->viewob[0]);
	mytext.write_y(100,"Chad Lawrence  Doug McCreary", 230, myscreen->viewob[0]);
	mytext.write_y(110,"Tom Ricket  Michael Scandizzo", 230, myscreen->viewob[0]);

	//myscreen->refresh();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	// First 'interlude' snapshot
	myscreen->clear();
	uldata = read_pixie_file("game2ul.pix");
	ul = new pixie(uldata);
	ul->setxy(41, 12);
	ul->draw(myscreen->viewob[0]);
	delete ul;
	uldata.free();
	ul = NULL;

	urdata = read_pixie_file("game2ur.pix");
	ur = new pixie(urdata);
	ur->setxy(160, 12);
	ur->draw(myscreen->viewob[0]);
	delete ur;
	urdata.free();
	ur = NULL;

	lldata = read_pixie_file("game2ll.pix");
	ll = new pixie(lldata);
	ll->setxy(41, 103);
	ll->draw(myscreen->viewob[0]);
	delete ll;
	lldata.free();
	ll = NULL;

	lrdata = read_pixie_file("game2lr.pix");
	lr = new pixie(lrdata);
	lr->setxy(160, 103);
	lr->draw(myscreen->viewob[0]);
	delete lr;
	lrdata.free();
	lr = NULL;

	//myscreen->refresh();

	if (show(SHOW_TIME+30) < 0)
	{
		cleanup();
		return;
	}

	// Programming Credits, Page 2
	myscreen->clear();
	mytext.write_y(70,"Additional Coding by Doug Ricket", 230, myscreen->viewob[0]);
	//buffers: PORT: w00t w00t
	mytext.write_y(90,"SDL port by Odo and Zardus",230,myscreen->viewob[0]);
	mytext.write_y(110,"Android port by Jonathan Dearborn",230,myscreen->viewob[0]);
	//myscreen->refresh();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	// Second 'interlude' & extra credits
	myscreen->clear();
	uldata = read_pixie_file("game4.pix");
	ul = new pixie(uldata);
	ul->setxy(0, 0);
	ul->draw(myscreen->viewob[0]);
	delete ul;
	uldata.free();
	ul = NULL;

	lldata = read_pixie_file("game5.pix");
	ll = new pixie(lldata);
	ll->setxy(160, 78);
	ll->draw(myscreen->viewob[0]);
	delete ll;
	lldata.free();
	ll = NULL;

	strcpy(message, "Additional Artwork By:");
	mytext.write_xy(310-mytext.query_width(message),
	                 30, message, 230, myscreen->viewob[0]);
	strcpy(message, "Doug Ricket");
	mytext.write_xy(310-mytext.query_width(message),
	                 50, message, 230, myscreen->viewob[0]);
	strcpy(message, "Stefan Scandizzo");
	mytext.write_xy(310-mytext.query_width(message),
	                 60, message, 230, myscreen->viewob[0]);

	strcpy(message, "Special Thanks To:");
	mytext.write_xy(2, 130, message, 230, myscreen->viewob[0]);
	strcpy(message, "Kim Kelly  Lara Kirkendall");
	mytext.write_xy(2, 150, message, 230, myscreen->viewob[0]);
	strcpy(message, "Lee Martin  Karyn McCreary");
	mytext.write_xy(2, 160, message, 230, myscreen->viewob[0]);
	strcpy(message, "Loki, Ishara, & Mootz");
	mytext.write_xy(2, 170, message, 230, myscreen->viewob[0]);
	strcpy(message, "And many others!");
	mytext.write_xy(2, 180, message, 230, myscreen->viewob[0]);


	if (show(SHOW_TIME*4) < 0)
	{
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

	cleanup();
}

int cleanup()
{
	Sint32 i;
	int red,green,blue; //buffers: PORT: changed to ints
	query_palette_reg((unsigned char)0, &red, &green, &blue); // Resets palette to read mode
	release_timer();
	myscreen->clear();
	myscreen->refresh();

	for (i = 0; i<256; i++)
	{
		red = pal[i][0];
		green = pal[i][1];
		blue = pal[i][2];
		set_palette_reg(i, red, green, blue);
	}
	load_and_set_palette("our.pal", mypalette);
	return 1;
}

int show() // default uses SHOW_TIME
{
	return show(SHOW_TIME);
}

int show(int howlong)
{
	if (myscreen->fadeblack(FROM) == -1) return -1;

	reset_timer();
	while (query_timer() < howlong)
	{
		get_input_events(POLL);
		if (query_key_press_event())
			return -1;
	}

	if (myscreen->fadeblack(TO) == -1) return -1;
	return 1;
}
