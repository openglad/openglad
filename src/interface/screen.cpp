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
//screen.cpp

/* ChangeLog
	buffers: 7/31/02: *deleted some redundant headers.
			: *load_scenario now looks for all uppercase files in
			:  levels.001 if lowercase file fails
	buffers: 8/15/02: *load_scenario now checks for uppercase file names in
			   scen/ in case lowercase check fails
*/

#include <openglad/interface/game_context.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <openglad/legacy/view_sizes.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/resources/level_data_hooks.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <format>

// Used by statistics::do_command() COMMAND_FOLLOW to find a leader walker.
// The function is declared in stats.cpp; defined here in the SDL build.
walker* find_follow_leader()
{
    if (!og::runtime::current_session || og::runtime::current_session->myscreen_ == nullptr)
        return nullptr;
    if (og::runtime::current_session->myscreen_->numviews == 1)
        return og::runtime::current_session->myscreen_->viewob[0]->control;
    // Multi-view: pick whichever view's controller has yo_delay set
    if (og::runtime::current_session->myscreen_->viewob[0]->control && og::runtime::current_session->myscreen_->viewob[0]->control->yo_delay)
        return og::runtime::current_session->myscreen_->viewob[0]->control;
    if (og::runtime::current_session->myscreen_->viewob[1]->control && og::runtime::current_session->myscreen_->viewob[1]->control->yo_delay)
        return og::runtime::current_session->myscreen_->viewob[1]->control;
    return nullptr;
}

namespace
{
const char* scenario_title_error_string(screen::ScenarioTitleError err)
{
    switch(err)
    {
        case screen::ScenarioTitleError::None:
            return "none";
        case screen::ScenarioTitleError::OpenReadFailed:
            return "open_read_failed";
        case screen::ScenarioTitleError::InvalidHeader:
            return "invalid_header";
        case screen::ScenarioTitleError::UnsupportedVersion:
            return "unsupported_version";
        case screen::ScenarioTitleError::ReadFailed:
            return "read_failed";
    }
    return "unknown";
}

screen::ScenarioTitleError map_level_file_error(og::data::LevelFileIoError err)
{
    switch (err)
    {
        case og::data::LevelFileIoError::None:
            return screen::ScenarioTitleError::None;
        case og::data::LevelFileIoError::OpenReadFailed:
            return screen::ScenarioTitleError::OpenReadFailed;
        case og::data::LevelFileIoError::InvalidHeader:
            return screen::ScenarioTitleError::InvalidHeader;
        case og::data::LevelFileIoError::UnsupportedVersion:
            return screen::ScenarioTitleError::UnsupportedVersion;
        case og::data::LevelFileIoError::ParseFailed:
        case og::data::LevelFileIoError::OpenWriteFailed:
        case og::data::LevelFileIoError::SerializeFailed:
            return screen::ScenarioTitleError::ReadFailed;
    }
    return screen::ScenarioTitleError::ReadFailed;
}

const char* save_data_io_error_string(SaveDataIoError err)
{
    switch (err)
    {
        case SaveDataIoError::None:
            return "none";
        case SaveDataIoError::OpenReadFailed:
            return "open_read_failed";
        case SaveDataIoError::OpenWriteFailed:
            return "open_write_failed";
        case SaveDataIoError::ReadFailed:
            return "read_failed";
        case SaveDataIoError::WriteFailed:
            return "write_failed";
        case SaveDataIoError::InvalidHeader:
            return "invalid_header";
        case SaveDataIoError::UnsupportedVersion:
            return "unsupported_version";
        case SaveDataIoError::CampaignLoadFailed:
            return "campaign_load_failed";
    }
    return "unknown";
}

} // namespace

static inline cfg_store& active_config()
{
    return cfg;
}


// From picker.cpp
extern Sint32 calculate_level(Uint32 temp_exp);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
loader* sdl_entity_loader();

// Screen window boundries
inline constexpr int MAX_VIEWS = 5;
inline constexpr int S_UP = 0;
inline constexpr int S_LEFT = 0;
inline constexpr int S_DOWN = 200;
inline constexpr int S_RIGHT = 320;
inline constexpr int S_WIDTH = (S_RIGHT - S_LEFT);
inline constexpr int S_HEIGHT = (S_DOWN - S_UP);
inline constexpr int MAX_SPREAD = 10; // this controls find_near_foe

// load_version_* functions now live in level_runtime_data.cpp and take
// OgFile& + LevelRuntimeData*



Uint32 random(Uint32 x)
{
	if (x < 1)
		return 0;
	return static_cast<Uint32>( (static_cast<Uint32>(rand())) % x);
}

void screen::set_fullscreen(bool fullscreen)
{
    video_impl_->set_fullscreen(fullscreen);
}

void screen::clearbuffer()
{
    video_impl_->clearbuffer();
}

void screen::clearbuffer(int x, int y, int w, int h)
{
    video_impl_->clearbuffer(x, y, w, h);
}

void screen::clear_window()
{
    video_impl_->clear_window();
}

std::span<unsigned char> screen::getbuffer()
{
    return video_impl_->getbuffer();
}

void screen::putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize)
{
    video_impl_->putblack(startx, starty, xsize, ysize);
}

void screen::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     unsigned char color)
{
    video_impl_->fastbox(startx, starty, xsize, ysize, color);
}

void screen::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     unsigned char color, unsigned char flag)
{
    video_impl_->fastbox(startx, starty, xsize, ysize, color, flag);
}

void screen::fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize,
                             Sint32 ysize, unsigned char color)
{
    video_impl_->fastbox_outline(startx, starty, xsize, ysize, color);
}

void screen::point(Sint32 x, Sint32 y, unsigned char color)
{
    video_impl_->point(x, y, color);
}

void screen::pointb(Sint32 x, Sint32 y, unsigned char color)
{
    video_impl_->pointb(x, y, color);
}

void screen::pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha)
{
    video_impl_->pointb(x, y, color, alpha);
}

void screen::pointb(int offset, unsigned char color)
{
    video_impl_->pointb(offset, color);
}

void screen::pointb(Sint32 x, Sint32 y, int r, int g, int b)
{
    video_impl_->pointb(x, y, r, g, b);
}

void screen::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
    video_impl_->hor_line(x, y, length, color);
}

void screen::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
    video_impl_->ver_line(x, y, length, color);
}

void screen::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color,
                      Sint32 tobuffer)
{
    video_impl_->hor_line(x, y, length, color, tobuffer);
}

void screen::hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color,
                            Uint8 alpha)
{
    video_impl_->hor_line_alpha(x, y, length, color, alpha);
}

void screen::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color,
                      Sint32 tobuffer)
{
    video_impl_->ver_line(x, y, length, color, tobuffer);
}

void screen::draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                       unsigned char color)
{
    video_impl_->draw_line(x1, y1, x2, y2, color);
}

void screen::do_cycle(Sint32 curmode, Sint32 maxmode)
{
    video_impl_->do_cycle(curmode, maxmode);
}

void screen::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     std::span<const unsigned char> sourcedata)
{
    video_impl_->putdata(startx, starty, xsize, ysize, sourcedata);
}

void screen::putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize,
                           Sint32 ysize,
                           std::span<const unsigned char> sourcedata,
                           unsigned char alpha)
{
    video_impl_->putdata_alpha(startx, starty, xsize, ysize, sourcedata, alpha);
}

void screen::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                         std::span<const unsigned char> sourcedata)
{
    video_impl_->putdatatext(startx, starty, xsize, ysize, sourcedata);
}

void screen::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     std::span<const unsigned char> sourcedata,
                     unsigned char color)
{
    video_impl_->putdata(startx, starty, xsize, ysize, sourcedata, color);
}

void screen::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                         std::span<const unsigned char> sourcedata,
                         unsigned char color)
{
    video_impl_->putdatatext(startx, starty, xsize, ysize, sourcedata, color);
}

void screen::putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                       Sint32 tilewidth, Sint32 tileheight, Sint32 portstartx,
                       Sint32 portstarty, Sint32 portendx, Sint32 portendy,
                       std::span<const unsigned char> sourceptr)
{
    video_impl_->putbuffer(tilestartx, tilestarty, tilewidth, tileheight,
                           portstartx, portstarty, portendx, portendy, sourceptr);
}

void screen::putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
                             Sint32 tilewidth, Sint32 tileheight,
                             Sint32 portstartx, Sint32 portstarty,
                             Sint32 portendx, Sint32 portendy,
                             std::span<const unsigned char> sourceptr,
                             unsigned char alpha)
{
    video_impl_->putbuffer_alpha(tilestartx, tilestarty, tilewidth, tileheight,
                                 portstartx, portstarty, portendx, portendy,
                                 sourceptr, alpha);
}

void screen::putbuffer_surface(Sint32 tilestartx, Sint32 tilestarty,
                               Sint32 tilewidth, Sint32 tileheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               void* sourceptr)
{
    video_impl_->putbuffer_surface(tilestartx, tilestarty, tilewidth, tileheight,
                                   portstartx, portstarty, portendx, portendy,
                                   sourceptr);
}

void screen::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                           Sint32 walkerwidth, Sint32 walkerheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           std::span<const unsigned char> sourceptr,
                           unsigned char teamcolor)
{
    video_impl_->walkputbuffer(walkerstartx, walkerstarty, walkerwidth,
                               walkerheight, portstartx, portstarty, portendx,
                               portendy, sourceptr, teamcolor);
}

void screen::walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
                                 Sint32 walkerwidth, Sint32 walkerheight,
                                 Sint32 portstartx, Sint32 portstarty,
                                 Sint32 portendx, Sint32 portendy,
                                 std::span<const unsigned char> sourceptr,
                                 unsigned char teamcolor)
{
    video_impl_->walkputbuffer_flash(walkerstartx, walkerstarty, walkerwidth,
                                     walkerheight, portstartx, portstarty,
                                     portendx, portendy, sourceptr, teamcolor);
}

void screen::walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
                               Sint32 walkerwidth, Sint32 walkerheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               std::span<const unsigned char> sourceptr,
                               unsigned char teamcolor)
{
    video_impl_->walkputbuffertext(walkerstartx, walkerstarty, walkerwidth,
                                   walkerheight, portstartx, portstarty,
                                   portendx, portendy, sourceptr, teamcolor);
}

void screen::walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                     Sint32 walkerwidth, Sint32 walkerheight,
                                     Sint32 portstartx, Sint32 portstarty,
                                     Sint32 portendx, Sint32 portendy,
                                     std::span<const unsigned char> sourceptr,
                                     unsigned char teamcolor, Uint8 alpha)
{
    video_impl_->walkputbuffertext_alpha(walkerstartx, walkerstarty, walkerwidth,
                                         walkerheight, portstartx, portstarty,
                                         portendx, portendy, sourceptr,
                                         teamcolor, alpha);
}

void screen::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                           Sint32 walkerwidth, Sint32 walkerheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           std::span<const unsigned char> sourceptr,
                           unsigned char teamcolor, unsigned char mode,
                           Sint32 invisibility, unsigned char outline,
                           unsigned char shifttype)
{
    video_impl_->walkputbuffer(walkerstartx, walkerstarty, walkerwidth,
                               walkerheight, portstartx, portstarty, portendx,
                               portendy, sourceptr, teamcolor, mode,
                               invisibility, outline, shifttype);
}

void screen::buffer_to_screen(Sint32 viewstartx, Sint32 viewstarty,
                              Sint32 viewwidth, Sint32 viewheight)
{
    video_impl_->buffer_to_screen(viewstartx, viewstarty, viewwidth, viewheight);
}

void screen::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                      unsigned char color, Sint32 filled)
{
    video_impl_->draw_box(x1, y1, x2, y2, color, filled);
}

void screen::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                      unsigned char color, Sint32 filled, Sint32 tobuffer)
{
    video_impl_->draw_box(x1, y1, x2, y2, color, filled, tobuffer);
}

void screen::draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h,
                              unsigned char color, Uint8 alpha)
{
    video_impl_->draw_rect_filled(x, y, w, h, color, alpha);
}

void screen::draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    video_impl_->draw_button_inverted(x, y, w, h);
}

void screen::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border)
{
    video_impl_->draw_button(x1, y1, x2, y2, border);
}

void screen::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                         Sint32 border, Sint32 tobuffer)
{
    video_impl_->draw_button(x1, y1, x2, y2, border, tobuffer);
}

void screen::draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                                 bool use_border, int base_color, int high_color,
                                 int shadow_color)
{
    video_impl_->draw_button_colored(x1, y1, x2, y2, use_border, base_color,
                                     high_color, shadow_color);
}

Sint32 screen::draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                           const char* header)
{
    return video_impl_->draw_dialog(x1, y1, x2, y2, header);
}

void screen::draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2)
{
    video_impl_->draw_text_bar(x1, y1, x2, y2);
}

void screen::darken_screen()
{
    video_impl_->darken_screen();
}

void screen::swap()
{
    video_impl_->swap();
}

void screen::get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b)
{
    video_impl_->get_pixel(x, y, r, g, b);
}

int screen::get_pixel(int x, int y, int* index)
{
    return video_impl_->get_pixel(x, y, index);
}

int screen::get_pixel(int offset)
{
    return video_impl_->get_pixel(offset);
}

bool screen::save_screenshot()
{
    return video_impl_->save_screenshot();
}

void screen::fade_between24(void* surface, const Uint8* from, const Uint8* to,
                            int amount)
{
    video_impl_->fade_between24(surface, from, to, amount);
}

int screen::fade_between(void* old_surface, void* new_surface, void* dest_surface)
{
    return video_impl_->fade_between(old_surface, new_surface, dest_surface);
}

int screen::fadeblack(bool fade_in)
{
    return video_impl_->fadeblack(fade_in);
}

// ************************************************************
//  SCREEN -- graphics routines
//
//  This object is the video graphics object.  All display
//  must pass through this object, and all on-screen objects
//  are found in this object.
// ************************************************************


// Common initialization logic shared by both constructors.
void screen::init_common(short howmany, bool has_display)
{
    // Register this screen as the current session's myscreen_ so the myscreen
    // macro resolves during the rest of construction (text rendering, etc.).
    if (og::runtime::current_session)
        og::runtime::current_session->myscreen_ = this;
    myloader = sdl_entity_loader();
    level_runtime_data_.attach_world(&world_);

    TRACE("init", "screen constructor: numviews=%d display=%d", howmany, has_display);

	Sint32 i, j;

	grab_timer();

	timerstart = query_timer_control();
	framecount = 0;

	control_hp = 0;

	// Load the palette ..
	load_and_set_palette("our.pal", newpalette);

	// Loading-screen text references (only used when has_display is true).
	text& load_text = text_normal;
	constexpr Sint32 load_left = 66;

	if (has_display) {
		draw_button(60, 50, 260, 110, 2, 1);
		draw_text_bar(64, 54, 256, 62);
		load_text.write_y(56, "Loading Gladiator..Please Wait", RED, 1);
		draw_text_bar(64, 64, 256, 106);
		load_text.write_xy(load_left, 70, "Loading Graphics...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
		load_text.write_xy(load_left, 70, "Loading Graphics...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 78, "Loading Gameplay Info...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
	}

	update_overscan_setting();

	palmode = 0;
	end = 0;
	timer_wait = 6;
	redrawme = 1;
	cyclemode = 1;
	enemy_freeze = 0;
	level_done = 0;
	retry = false;

	numviews = howmany;
    for (auto& view : viewob)
        view.reset();
    initialize_views();

	if (has_display) {
		load_text.write_xy(load_left, 78, "Loading Gameplay Info...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 86, "Initializing Display...Done", DARK_BLUE, 1);
		load_text.write_xy(load_left, 94, "Initializing Sound...", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
	}

    soundp = create_soundob(false);
    if (!active_config().is_on("sound", "sound")) {
        soundp->set_sound(1);
    }

	if (has_display) {
		load_text.write_xy(load_left, 94, "Initializing Sound...Done", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
	}

	init_all_registries();
	for (i=0; i < NUM_FAMILIES; i++)
	{
		auto* fd = get_family_descriptor(i);
		for (j=0; j < NUM_SPECIALS; j++)
		{
			special_name[i][j] = fd ? fd->special_names[j] : "NONE";
			alternate_name[i][j] = fd ? fd->alternate_names[j] : "NONE";
		}
	}

	sync_world_from_save_data();
}

screen::screen(GameWorld& world, std::unique_ptr<video> video_impl, short howmany,
               bool has_display)
    : video()
    , video_impl_(std::move(video_impl))
    , ourpalette(video_impl_->ourpalette_ref())
    , redpalette(video_impl_->redpalette_ref())
    , bluepalette(video_impl_->bluepalette_ref())
    , dospalette(video_impl_->dospalette_ref())
    , videobuffer(video_impl_->videobuffer_ref())
    , cyclemode(video_impl_->cyclemode_ref())
    , text_normal(video_impl_->text_normal_ref())
    , text_big(video_impl_->text_big_ref())
    , world_(world)
    , control_hp(world_.control_hp)
    , end(world_.end)
    , timer_wait(world_.timer_wait)
    , level_done(world_.level_done)
    , retry(world_.retry)
    , enemy_freeze(world_.enemy_freeze)
    , myloader(nullptr)
    , level_runtime_data_(1, false, &sdl_level_data_hooks(), &level_visuals_)
{
    init_common(howmany, has_display);
}

screen::~screen()
{
	release_timer();
	soundp.reset();
	cleanup(1); //make sure we've cleaned up
}

void screen::initialize_views()
{
    // Even though it looks okay here, these positions and sizes are overridden by viewscreen::resize() later.
	if (numviews == 1)
	{
		viewob[0] = std::make_unique<viewscreen>( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0);
	}
	else if (numviews == 2)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT, 1);
	}
	else if (numviews == 3)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
		viewob[2] = std::make_unique<viewscreen>( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
	}
	else if (numviews == 4)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
		viewob[2] = std::make_unique<viewscreen>( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
		viewob[3] = std::make_unique<viewscreen>( T_LEFT_FOUR, T_UP_FOUR, T_HALF_WIDTH, T_HALF_HEIGHT, 3);
	}
	else
    {
        LogError("screen_init_views_failed numviews={}\n", numviews);
    }
}

void screen::cleanup(short howmany)
{
	Sint32 i;

    numviews = howmany; // # of viewscreens
    for (i=0; i < MAX_VIEWS; i++)
    {
        viewob[i].reset();
    }
}

void screen::ready_for_battle(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);
    
    initialize_views();
	world_.reset_level_progress();

	end = 0;
	
	retry = false;

	redrawme = 1;

	timerstart = query_timer_control();
	framecount = 0;
	enemy_freeze = 0;

	control_hp = 0;

	palmode = 0;

	redrawme = 1;

}

void screen::reset(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);

	if (numviews == 1)
	{
		viewob[0] = std::make_unique<viewscreen>( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0);
	}
	else if (numviews == 2)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
	}
	else if (numviews == 3)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
		viewob[2] = std::make_unique<viewscreen>( 112, 16, 100, 168, 2);
	}
	else if (numviews == 4)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
		viewob[2] = std::make_unique<viewscreen>( 112, 16, 100, 168, 2);
		viewob[3] = std::make_unique<viewscreen>( 112, 16, 100, 168, 3);
	}

	end = 0;

	redrawme = 1;
	
	save_data.reset();
	level_runtime_data_.clear();
	sync_world_from_save_data();

	timerstart = query_timer_control();
	framecount = 0;
	enemy_freeze = 0;

	control_hp = 0;

	palmode = 0;

	end = 0;

	redrawme = 1;

}

void screen::sync_world_from_save_data()
{
    world_.my_team = save_data.my_team;
    world_.allied_mode = save_data.allied_mode;
    world_.current_scenario = save_data.scen_num;
    for (int i = 0; i < 4; ++i)
        world_.m_score[i] = save_data.m_score[i];

    auto e = save_data.completed_levels.find(save_data.current_campaign);
    if (e != save_data.completed_levels.end())
        world_.completed_levels = e->second;
    else
        world_.completed_levels.clear();

    world_.withdraw_requested = false;
    world_.withdraw_level = -1;
}

void screen::sync_save_data_from_world()
{
    save_data.my_team = world_.my_team;
    save_data.allied_mode = world_.allied_mode;
    save_data.scen_num = world_.current_scenario;
    for (int i = 0; i < 4; ++i)
        save_data.m_score[i] = world_.m_score[i];

    save_data.completed_levels[save_data.current_campaign] = world_.completed_levels;
}

bool screen::load_level()
{
    return level_runtime_data_.load();
}

bool screen::save_level()
{
    return level_runtime_data_.save();
}

LevelRuntimeData::IoError screen::load_level_with_error()
{
    return level_runtime_data_.load_with_error();
}

LevelRuntimeData::IoError screen::save_level_with_error()
{
    return level_runtime_data_.save_with_error();
}

bool screen::query_grid_passable(float x, float y, walker  *ob)
{
	return world_.query_grid_passable(x, y, ob);
}

bool screen::query_object_passable(float x, float y, walker  *ob)
{
	return world_.query_object_passable(x, y, ob);
}

bool screen::query_passable(float x, float y, walker  *ob)
{
	return world_.query_passable(x, y, ob);
}

void screen::clear()
{
	unsigned short i;

	//buffers: PORT:  for (i=0;i<64000;i++)
	//buffers: PORT:  {
	//buffers: PORT:         videobuffer[i] = 0;
	//buffers: PORT:  }
	clearbuffer();

	for (i=0; i < numviews; i ++)
		viewob[i]->clear();
}

// REDRAW -- This function moves through the data on the grid (map)
//           finding which grid squares are on screen.  For each on
//           screen, it pashorts the appropriate graphics pixie onto
//           the screen by calling the function DRAW in PIXIE.
bool screen::redraw()
{
	short i;
	for (i=0; i < numviews; i++)
		viewob[i]->redraw();

	return 1;
}


// REFRESH -- refreshes the viewscreens
void screen::refresh()
{
	short i;
	for (i=0; i < numviews; i++)
	{
		viewob[i]->refresh();
	}
}


// **************************
// Useful stuff again
// **************************

short screen::input(const void* native_event)
{
	// static text mytext;
	short i;
	if (native_event == nullptr)
		return 1;

	for (i=0; i < numviews; i++)
		viewob[i]->input(native_event);

	return 1;
}

short screen::continuous_input()
{
	// static text mytext;
	short i;

	for (i=0; i < numviews; i++)
		viewob[i]->continuous_input();

	return 1;
}

void screen::process_input(const InputState& input_state)
{
	for (short i = 0; i < numviews; i++)
		viewob[i]->process_input(input_state);
}

bool screen::act()
{
	// Delegate simulation tick to GameWorld.
	// GameWorld encapsulates the deterministic entity update logic that
	// was previously embedded directly in this method.
	if (current_game == nullptr || current_game->sim_events == nullptr)
		return 1;
	og::sim::SimEventLog& events = *current_game->sim_events;
	world_.tick();

	// Post-tick: clean up viewscreen control pointers for dead player entities.
	// This is a rendering concern that doesn't belong in the simulation layer.
	for (int i = 0; i < numviews; i++)
	{
		if (viewob[i]->control && viewob[i]->control->dead)
			viewob[i]->control = nullptr;
	}

	// Process simulation events: dispatch sounds, notifications, etc.
	// This is the key sim/render boundary — simulation emits events,
	// the runtime layer dispatches them to platform subsystems.
	const og::sim::Event* first_exit_request = nullptr;
	const og::sim::Event* first_withdraw_request = nullptr;
	for (const auto& ev : events.events())
	{
		switch (ev.kind)
		{
			case og::sim::EventKind::PlaySound:
				if (soundp)
					soundp->play_sound(static_cast<short>(ev.a));
				break;
			case og::sim::EventKind::Notification:
				if (!ev.text.empty())
				{
					short duration = ev.a ? static_cast<short>(ev.a)
					                      : STANDARD_TEXT_TIME;
					for (short vi = 0; vi < numviews; vi++)
						viewob[vi]->set_display_text(ev.text, duration);
				}
				break;
			case og::sim::EventKind::SetPalette:
				if (ev.a == 0)
					set_palette(ourpalette);
				else
					set_palette(bluepalette);
				break;
			case og::sim::EventKind::RequestRedraw:
				redrawme = 1;
				break;
			case og::sim::EventKind::EndGame:
				sync_save_data_from_world();
				events.clear();
				return endgame(static_cast<short>(ev.a),
				               static_cast<short>(static_cast<std::int32_t>(ev.b)));
			case og::sim::EventKind::DamageTile:
				damage_tile(static_cast<short>(ev.a), static_cast<short>(ev.b));
				break;
			case og::sim::EventKind::SetEnd:
				end = 1;
				break;
			case og::sim::EventKind::RequestExitConfirmation:
				if (first_exit_request == nullptr)
					first_exit_request = &ev;
				break;
			case og::sim::EventKind::WithdrawToLevel:
				if (first_withdraw_request == nullptr)
					first_withdraw_request = &ev;
				break;
			default:
				break;
		}
	}

	if (first_exit_request != nullptr)
	{
		const short destination_level =
			static_cast<short>(static_cast<std::int32_t>(first_exit_request->a));
		const bool is_withdraw_prompt = first_exit_request->b != 0;
		std::string prompt_text = first_exit_request->text;
		if (prompt_text.empty())
		{
			if (is_withdraw_prompt)
				prompt_text = std::format("Withdraw to Level {}?", destination_level);
			else
				prompt_text = std::format("Exit to Level {}?", destination_level);
		}

		const bool accepted = yes_or_no_prompt("Exit Field", prompt_text.c_str(), false);
		redrawme = 1;
		if (accepted)
		{
			if (is_withdraw_prompt)
			{
				short withdraw_level = destination_level;
				if (first_withdraw_request != nullptr)
				{
					withdraw_level = static_cast<short>(
						static_cast<std::int32_t>(first_withdraw_request->a));
				}

				const SaveDataIoError load_error = save_data.load_with_error("save0");
				if (load_error != SaveDataIoError::None)
				{
					LogError("withdraw_load_failed target_level={} error={}\n",
					         withdraw_level,
					         save_data_io_error_string(load_error));
					sync_save_data_from_world();
					world_.withdraw_requested = false;
					world_.withdraw_level = -1;
					events.clear();
					return 1;
				}

				save_data.scen_num = withdraw_level;
				const SaveDataIoError save_error = save_data.save_with_error("save0");
				if (save_error != SaveDataIoError::None)
				{
					LogError("withdraw_save_failed target_level={} error={}\n",
					         withdraw_level,
					         save_data_io_error_string(save_error));
					sync_save_data_from_world();
					world_.withdraw_requested = false;
					world_.withdraw_level = -1;
					events.clear();
					return 1;
				}
				sync_world_from_save_data();

				events.clear();
				return endgame(1, withdraw_level);
			}

			sync_save_data_from_world();
			world_.withdraw_requested = false;
			world_.withdraw_level = -1;
			events.clear();
			return endgame(0, destination_level);
		}

		world_.withdraw_requested = false;
		world_.withdraw_level = -1;
	}
	else if (first_withdraw_request != nullptr)
	{
		// Defensive clear for dropped duplicate withdraw events in the same tick.
		world_.withdraw_requested = false;
		world_.withdraw_level = -1;
	}

	events.clear();

	// Handle level completion / game ending.
	if (world_.game_ended && !end)
	{
		sync_save_data_from_world();
		return endgame(world_.ending, world_.next_level);
	}

	if (end)
		return 1;

	return 1;
}

Uint32 get_time_bonus(int playernum);

short screen::endgame(short ending)
{
	return endgame(ending, -1);
}

short screen::endgame(short ending, short nextlevel)
{
	    if(end)
	    {
	        return 1;
	    }

	sync_save_data_from_world();
	
	
	std::map<int, guy*> before;
	std::map<int, walker*> after;
	
	// Get guys from before battle
	for(int i = 0; i < save_data.team_size; i++)
    {
        if(save_data.team_list[i] != nullptr)
            before.insert(std::make_pair(save_data.team_list[i]->id, save_data.team_list[i].get()));
    }
	
    // Get guys from the battle
    for(auto& uptr : world_.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && ob->myguy)
			after.insert(std::make_pair(ob->myguy->id, ob));
	}
	
	// Let's show the results!
    retry = results_screen(ending, nextlevel, before, after);
    
    if(retry)
    {
        // Retry without updating the roster and saving the game
        end = 1;
        sync_world_from_save_data();
        return 1;
    }
    
	if (ending == 1)  // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
			end = 1;
		}
		else // we're withdrawing to another level
		{
			end = 1;
		}
	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		end = 1;
	}
		else if (ending == 0) // we won
		{
	        Uint32 bonuscash[4] = {0, 0, 0, 0};
	        Uint32 allbonuscash = 0;
	        
			// Update all the money!
			for (int i=0; i < 4; i++)
			{
				save_data.m_totalscore[i] += save_data.m_score[i];
				save_data.m_totalcash[i] += (save_data.m_score[i]*2);
				save_data.m_score[i] = 0;
			}
			for (int i=0; i < 4; i++)
			{
	            bonuscash[i] = get_time_bonus(i);
			save_data.m_totalcash[i] += bonuscash[i];
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for (int i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
	    
		// Beat that level
		save_data.add_level_completed(save_data.current_campaign, save_data.scen_num); // this scenario is completed ..
		if (nextlevel != -1)
			save_data.scen_num = nextlevel;    // Fake jumping to next level ..
        
        // Grab our team out of the level
        save_data.update_guys(world_.oblist);
        
        // Autosave because we won
		save_data.save("save0");

		end = 1;
	}

	sync_world_from_save_data();
	return 1;
}

walker *screen::find_near_foe(walker  *ob)
{
	return world_.find_near_foe(ob);
}

walker  *screen::find_far_foe(walker  *ob)
{
	return world_.find_far_foe(ob);
}

walker* screen::set_walker(walker *ob, Order order, Sint32 family)
{
    if (myloader == nullptr)
        return nullptr;
    return myloader->set_walker(ob, order, family);
}

screen::ScenarioTitleError screen::get_scen_title_with_error(const char *filename, std::string& out_title)
{
    out_title = "none";
    const og::data::LevelFileIoError io_err =
        og::data::load_scenario_title_with_error(filename, out_title);
    const ScenarioTitleError err = map_level_file_error(io_err);

    if(err != ScenarioTitleError::None)
    {
        LogError("scenario_title_load_failed filename={} error={}\n",
            filename ? filename : "(null)", scenario_title_error_string(err));
    }
    return err;
}

const char* screen::get_scen_title(const char *filename, screen *master)
{
    (void)master;
    static char buffer[31] = {};
    std::string out_title;
    const ScenarioTitleError err = get_scen_title_with_error(filename, out_title);
    if(err != ScenarioTitleError::None)
        out_title = "none";
    std::snprintf(buffer, sizeof(buffer), "%s", out_title.c_str());
    return buffer;

}


// Look for the first non-dead instance of a given walker ..
walker  * screen::first_of(Order whatorder, unsigned char whatfamily,
                           int team_num)
{
	for(auto& uptr : world_.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead)
		{
			if (ob->query_order() == whatorder &&
			        ob->family== whatfamily)
			{
				if (team_num == -1 || team_num == ob->team_num)
					return ob;
			}
		}
	}
	return nullptr;
}

void screen::draw_panels(short howmany)
{
	short i;

	// Force a memory clear ..
	clearbuffer();

	if (howmany)
		howmany = howmany;
	for (i=0; i < numviews; i++)
		if ( (viewob[i]->prefs[PREF_VIEW] == PREF_VIEW_FULL) ||
		        numviews == 4 )
			; // do nothing
		else
		{
			draw_button(viewob[i]->xloc-4, viewob[i]->yloc-3,
			            viewob[i]->endx+3, viewob[i]->endy+3, 3, 1);
			draw_box(viewob[i]->xloc-1, viewob[i]->yloc-1,
			         viewob[i]->endx, viewob[i]->endy, 0, 0,1);
		}


	redraw(); // repaint the screen area ..
	buffer_to_screen(0, 0, 320, 200);
}

walker  * screen::find_nearest_blood(walker  *who)
{
	return world_.find_nearest_blood(who);
}

std::list<walker*> screen::find_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob)
{
	return world_.find_in_range(somelist, range, howmany, ob);
}

walker* screen::find_nearest_player(walker *ob)
{
	return world_.find_nearest_player(ob);
}

std::list<walker*> screen::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob)
{
	return world_.find_foes_in_range(somelist, range, howmany, ob);
}

std::list<walker*> screen::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range,
                                      Sint32* howmany, walker* ob)
{
	return world_.find_friends_in_range(somelist, range, howmany, ob);
}

std::list<walker*> screen::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob)
{
    return world_.find_foe_weapons_in_range(somelist, range, howmany, ob);
}


// Uses pixel coordinates
char screen::damage_tile(short xloc, short yloc) // damage the specified tile
{
	short xover, yover;
	short gridloc;

	xover = static_cast<short>(xloc / GRID_SIZE);
	yover = static_cast<short>(yloc / GRID_SIZE);

	if (xover < 0 || yover < 0)
		return 0;
	if (xover >= world_.grid.w || yover >= world_.grid.h)
		return 0;

	gridloc = static_cast<short>(yover*world_.grid.w+xover);

	switch (static_cast<unsigned char>(world_.grid.data[gridloc]))
	{
		case PIX_GRASS1: // grass
		case PIX_GRASS2:
		case PIX_GRASS3:
		case PIX_GRASS4:
			world_.grid.data[gridloc] = PIX_GRASS1_DAMAGED;
			break;
		default:
			break;
	}

	return world_.grid.data[gridloc];
}

void screen::do_notify(std::string_view message, walker  *who)
{
	short i,sent=0;
	for(i=0;i<numviews;i++)
	{
		if (who && viewob[i]->control == who)
		{
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);
			sent = 1;
		}

	}
	if (!sent)
		for (i=0; i < numviews; i++)
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);

}

void screen::report_mem()
{
	meminfo Memory;
	Memory.FreeLinAddrSpace = 0;
	// Zardus: PORT: this is aparently an incomplete type:  union REGS regs;
	// Same here:  struct SREGS sregs;
	// Zardus: PORT: Undeclared because of problems above:  regs.x.eax = 0x00000500;
	// Same here:  memset( &sregs, 0, sizeof(sregs) );

	// See two lines up:  sregs.es = FP_SEG( &Memory );
	// See three lines up:  regs.x.edi = FP_OFF( &Memory );

	// See two lines up: (plus sounds like a dos thing):  int386x( DPMI_INT, &regs, &regs, &sregs );

	// Them:
	//sprintf(memreport, "Largest Block: %lu bytes",
	//  Memory.LargestBlockAvail);
	//viewob[0]->set_display_text(memreport, STANDARD_TEXT_TIME);
	std::string memreport = std::format("Free Linear address: {} pages",
	        Memory.FreeLinAddrSpace);
	//  Log(memreport);
	//  Log("\n");
	viewob[0]->set_display_text(memreport.c_str(), 25);
	/*
	       Log( "Largest available block (in bytes): %lu\n",
	               MemInfo.LargestBlockAvail );
	       Log( "Maximum unlocked page allocation: %lu\n",
	               MemInfo.MaxUnlockedPage );
	       Log( "Pages that can be allocated and locked: "
	               "%lu\n", MemInfo.LargestLockablePage );
	       Log( "Total linear address space including "
	               "allocated pages: %lu\n",
	               MemInfo.LinAddrSpace );
	       Log( "Number of free pages available: %lu\n",
	                MemInfo.NumFreePagesAvail );

	       Log( "Number of physical pages not in use: %lu\n",
	                MemInfo.NumPhysicalPagesFree );
	       Log( "Total physical pages managed by host: %lu\n",
	                MemInfo.TotalPhysicalPages );
	       Log( "Free linear address space (pages): %lu\n",
	                MemInfo.FreeLinAddrSpace );
	       Log( "Size of paging/file partition (pages): %lu\n",
	                MemInfo.SizeOfPageFile );
	 */
}
