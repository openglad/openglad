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

#include <openglad/interface/screen.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/pixie.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <array>
#include <cstring>

inline constexpr int SHOW_TIME = 130;
inline constexpr int FADE_FROM = 1;
inline constexpr int FADE_TO = 0;
int show();
int show(int howlong);
int cleanup();

std::array<std::array<int, 3>, 256> pal{};
std::array<unsigned char, 768> mypalette{};
//screen *myscreen;

void intro_main(Sint32 argc, char** argv)
{
	(void)argc;
	(void)argv;
	// Zardus: PORT: doesn't seem to be used, and causes a memory leak
	//char **args = (char **)new int;
	text& mytext = og::runtime::current_session->myscreen_->text_normal;
	PixieData uldata, urdata, lldata, lrdata;
	PixieData gladdata, bigdata;
	const char* message;

	og::runtime::current_session->myscreen_->viewob[0]->resize(PREF_VIEW_FULL);
	grab_timer();
	
	load_and_set_palette("our.pal", mypalette);
	//  load_scenario("current", myscreen);
	//buffers: PORT:  for (i=0;i<256;i++)
	//buffers: PORT:         set_palette_reg(i,0,0,0);
    
    og::runtime::current_session->myscreen_->fadeblack(FADE_TO);
    
	og::runtime::current_session->myscreen_->clear();

	gladdata = read_pixie_file("3mages2.png");
	pixie gladiator(gladdata);
	gladiator.drawMix(120,55,og::runtime::current_session->myscreen_->viewob[0].get());
	mytext.write_y(100,"FORGOTTEN SAGES PRESENTS", 230, og::runtime::current_session->myscreen_->viewob[0].get());
	//myscreen->refresh();
	gladdata.free();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	//gladdata = read_pixie_file("glad.pix");
	gladdata = read_pixie_file("glad2.png");
	bigdata = read_pixie_file("bigfoot.png");
	pixie gladiator2(gladdata);
	pixie bigfoot(bigdata);
	og::runtime::current_session->myscreen_->clear();
	bigfoot.drawMix(120,50,og::runtime::current_session->myscreen_->viewob[0].get());
	//gladiator->drawMix(110,65,myscreen->viewob[0].get());
	gladiator2.drawMix(100, 110, og::runtime::current_session->myscreen_->viewob[0].get());
	//myscreen->refresh();

	gladdata.free();
	bigdata.free();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	og::runtime::current_session->myscreen_->clear();
	mytext.write_y(70,"THOSE WHO ARE ABOUT TO DIE SALUTE YOU", 230, og::runtime::current_session->myscreen_->viewob[0].get());
	//myscreen->refresh();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	// Programming Credits, Page 1
	og::runtime::current_session->myscreen_->clear();
	mytext.write_y(80,"Programming By:", 230, og::runtime::current_session->myscreen_->viewob[0].get());
	mytext.write_y(100,"Chad Lawrence  Doug McCreary", 230, og::runtime::current_session->myscreen_->viewob[0].get());
	mytext.write_y(110,"Tom Ricket  Michael Scandizzo", 230, og::runtime::current_session->myscreen_->viewob[0].get());

	//myscreen->refresh();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	// First 'interlude' snapshot
	og::runtime::current_session->myscreen_->clear();
	uldata = read_pixie_file("game2ul.png");
	pixie ul(uldata);
	ul.setxy(41, 12);
	ul.draw(og::runtime::current_session->myscreen_->viewob[0].get());
	uldata.free();

	urdata = read_pixie_file("game2ur.png");
	pixie ur(urdata);
	ur.setxy(160, 12);
	ur.draw(og::runtime::current_session->myscreen_->viewob[0].get());
	urdata.free();

	lldata = read_pixie_file("game2ll.png");
	pixie ll(lldata);
	ll.setxy(41, 103);
	ll.draw(og::runtime::current_session->myscreen_->viewob[0].get());
	lldata.free();

	lrdata = read_pixie_file("game2lr.png");
	pixie lr(lrdata);
	lr.setxy(160, 103);
	lr.draw(og::runtime::current_session->myscreen_->viewob[0].get());
	lrdata.free();

	//myscreen->refresh();

	if (show(SHOW_TIME+30) < 0)
	{
		cleanup();
		return;
	}

	// Programming Credits, Page 2
	og::runtime::current_session->myscreen_->clear();
	mytext.write_y(90,"Additional Coding by Doug Ricket", 230, og::runtime::current_session->myscreen_->viewob[0].get());
	//buffers: PORT: w00t w00t
	mytext.write_y(110,"SDL port by Odo and Zardus",230,og::runtime::current_session->myscreen_->viewob[0].get());
	//myscreen->refresh();

	if (show() < 0)
	{
		cleanup();
		return;
	}

	// Second 'interlude' & extra credits
	og::runtime::current_session->myscreen_->clear();
	uldata = read_pixie_file("game4.png");
	pixie ul2(uldata);
	ul2.setxy(0, 0);
	ul2.draw(og::runtime::current_session->myscreen_->viewob[0].get());
	uldata.free();

	lldata = read_pixie_file("game5.png");
	pixie ll2(lldata);
	ll2.setxy(160, 78);
	ll2.draw(og::runtime::current_session->myscreen_->viewob[0].get());
	lldata.free();

	message = "Additional Artwork By:";
	mytext.write_xy(310-mytext.query_width(message),
	                 30, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());
	message = "Doug Ricket";
	mytext.write_xy(310-mytext.query_width(message),
	                 50, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());
	message = "Stefan Scandizzo";
	mytext.write_xy(310-mytext.query_width(message),
	                 60, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());

	message = "Special Thanks To:";
	mytext.write_xy(2, 130, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());
	message = "Kim Kelly  Lara Kirkendall";
	mytext.write_xy(2, 150, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());
	message = "Lee Martin  Karyn McCreary";
	mytext.write_xy(2, 160, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());
	message = "Loki, Ishara, & Mootz";
	mytext.write_xy(2, 170, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());
	message = "And many others!";
	mytext.write_xy(2, 180, message, 230, og::runtime::current_session->myscreen_->viewob[0].get());

	//myscreen->refresh();

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
			set_palette_reg(static_cast<unsigned char>(i), red, green, blue);
		}
	*/

	cleanup();
}


int cleanup()
{
	Sint32 i;
	int red,green,blue; //buffers: PORT: changed to ints
	query_palette_reg(static_cast<unsigned char>(0), &red, &green, &blue); // Resets palette to read mode
	release_timer();
	og::runtime::current_session->myscreen_->clear();
	og::runtime::current_session->myscreen_->refresh();

		for (i = 0; i<256; i++)
		{
			red = pal[i][0];
			green = pal[i][1];
			blue = pal[i][2];
			set_palette_reg(static_cast<unsigned char>(i), red, green, blue);
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
	if (og::runtime::current_session->myscreen_->fadeblack(FADE_FROM) == -1) return -1;

	reset_timer();
	while (query_timer() < howlong)
	{
		get_input_events(POLL);
		if (query_key_press_event())
			return -1;
	}

	if (og::runtime::current_session->myscreen_->fadeblack(FADE_TO) == -1) return -1;
	return 1;
}
