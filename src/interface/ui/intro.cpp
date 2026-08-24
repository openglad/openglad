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
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <array>
#include <cstring>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace
{

#ifdef __EMSCRIPTEN__
EM_JS(void, clear_pending_web_intro_tap_js, (), {
    window.__opengladIntroTapPending = false;
});

EM_JS(void, begin_web_intro_page_js, (), {
    window.__opengladIntroTapGeneration =
        (window.__opengladIntroTapGeneration || 0) + 1;
    window.__opengladIntroTapReady = false;
    window.__opengladIntroTapPending = false;
});

EM_JS(void, set_web_intro_tap_ready_js, (int ready), {
    window.__opengladIntroTapReady = Boolean(ready);
});

EM_JS(int, take_pending_web_intro_tap_js, (), {
    if (!window.__opengladIntroTapPending)
        return 0;
    window.__opengladIntroTapPending = false;
    window.__opengladIntroTapAdvanceCount =
        (window.__opengladIntroTapAdvanceCount || 0) + 1;
    return 1;
});
#endif

void clear_pending_web_intro_tap()
{
#ifdef __EMSCRIPTEN__
    clear_pending_web_intro_tap_js();
#endif
}

void begin_web_intro_page()
{
#ifdef __EMSCRIPTEN__
    begin_web_intro_page_js();
#endif
}

void set_web_intro_tap_ready(bool ready)
{
#ifdef __EMSCRIPTEN__
    set_web_intro_tap_ready_js(ready ? 1 : 0);
#else
    (void)ready;
#endif
}

bool take_pending_web_intro_tap()
{
#ifdef __EMSCRIPTEN__
    return take_pending_web_intro_tap_js() != 0;
#else
    return false;
#endif
}

void yield_to_web_intro_input()
{
#if defined(__EMSCRIPTEN__) && defined(__ASYNCIFY__)
    // intro_main() is a synchronous legacy loop. Yielding lets the browser's
    // trusted pointer handler latch a tap without re-entering Wasm.
    emscripten_sleep(10);
#endif
}

} // namespace

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

	// Drop any click state accumulated before the intro (startup taps),
	// so a stale tap cannot fast-forward the first page.
	input_continue_ref() = false;
	while (take_pending_left_click()) {}
	clear_pending_web_intro_tap();
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

	if (show() < 0)
	{
		cleanup();
		return;
	}

	gladdata = read_pixie_file("glad2.png");
	bigdata = read_pixie_file("bigfoot.png");
	pixie gladiator2(gladdata);
	pixie bigfoot(bigdata);
	og::runtime::current_session->myscreen_->clear();
	bigfoot.drawMix(120,50,og::runtime::current_session->myscreen_->viewob[0].get());
	//gladiator->drawMix(110,65,myscreen->viewob[0].get());
	gladiator2.drawMix(100, 110, og::runtime::current_session->myscreen_->viewob[0].get());
	//myscreen->refresh();

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

	urdata = read_pixie_file("game2ur.png");
	pixie ur(urdata);
	ur.setxy(160, 12);
	ur.draw(og::runtime::current_session->myscreen_->viewob[0].get());

	lldata = read_pixie_file("game2ll.png");
	pixie ll(lldata);
	ll.setxy(41, 103);
	ll.draw(og::runtime::current_session->myscreen_->viewob[0].get());

	lrdata = read_pixie_file("game2lr.png");
	pixie lr(lrdata);
	lr.setxy(160, 103);
	lr.draw(og::runtime::current_session->myscreen_->viewob[0].get());

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

	lldata = read_pixie_file("game5.png");
	pixie ll2(lldata);
	ll2.setxy(160, 78);
	ll2.draw(og::runtime::current_session->myscreen_->viewob[0].get());

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
	// #237 ownership: the intro fades its last page out itself. After a
	// completed last page this is a no-op (the window is already black); a
	// key abort mid-page fades that page out instead of hard-cutting. Either
	// way the window is black, so the main menu's entry fades in only — the
	// cold-start black-to-black 500ms fade (#200's shape) cannot come back.
	og::runtime::current_session->myscreen_->fadeblack(FADE_TO);

		for (i = 0; i<256; i++)
		{
			red = pal[static_cast<std::size_t>(i)][0];
			green = pal[static_cast<std::size_t>(i)][1];
			blue = pal[static_cast<std::size_t>(i)][2];
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
	// Reset the tap latch before the fade so a tap during the previous
	// page's fade-out never carries into this page: one tap advances
	// exactly one page.
	input_continue_ref() = false;
	begin_web_intro_page();
	while (take_pending_left_click()) {}
	clear_pending_web_intro_tap();

	if (og::runtime::current_session->myscreen_->fadeblack(FADE_FROM) == -1) return -1;

	// fadeblack() is synchronous, so browser pointer events generated during
	// the transition cannot run until the Asyncify stack yields. Flush them
	// once while this page is explicitly not ready; shell.html also requires
	// the pointer-down generation to match this page on pointer-up.
	yield_to_web_intro_input();
	clear_pending_web_intro_tap();
	reset_timer();
	TRACE("intro_state", "page ready");
	set_web_intro_tap_ready(true);
	while (query_timer() < howlong)
	{
		yield_to_web_intro_input();
		get_input_events(POLL);
		// A tap / left click advances this page only (the touch skip
		// path — checked before the key so a click never aborts the
		// whole intro). The pending-click queue holds exactly one entry
		// per completed tap (queued on button-up), so consuming one
		// entry here means one tap = one page advance, and the click
		// cannot leak into the picker menu that follows the intro.
		if (take_pending_left_click() || take_pending_web_intro_tap())
		{
			input_continue_ref() = false;
			TRACE("intro", "page advanced by click");
			break;
		}
		if (query_key_press_event())
		{
			set_web_intro_tap_ready(false);
			TRACE("intro", "intro aborted by key");
			return -1;
		}
	}

	set_web_intro_tap_ready(false);
	if (og::runtime::current_session->myscreen_->fadeblack(FADE_TO) == -1) return -1;
	return 1;
}
