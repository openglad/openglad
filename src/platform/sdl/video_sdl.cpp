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
// Video object code

#include <openglad/platform/video_sdl.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/platform/sai2x.h>
#include <openglad/resources/gparser.h>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/io.h>
#include <algorithm>
#include <functional>
#include <utility>
#include <array>
#include <cmath>
#include <format>
#include <cstring>
#include <openglad/interface/game_context.h>
#include <memory>
#include <limits>
#include <span>
#include <tuple>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

namespace og::platform
{
std::pair<int, int> display_mode_pixel_size(const SDL_DisplayMode& mode)
{
	const double density = std::isfinite(mode.pixel_density) && mode.pixel_density > 0.0f
		? static_cast<double>(mode.pixel_density)
		: 1.0;
	const auto to_pixels = [density](int logical) {
		if (logical <= 0)
			return 0;
		const double scaled = std::ceil(static_cast<double>(logical) * density);
		if (!std::isfinite(scaled) ||
		    scaled >= static_cast<double>(std::numeric_limits<int>::max()))
		{
			return std::numeric_limits<int>::max();
		}
		return static_cast<int>(scaled);
	};
	return {to_pixels(mode.w), to_pixels(mode.h)};
}
} // namespace og::platform

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

// Defined with the pixel-format memo below; must run alongside SDL_Quit.
static void reset_format_detail_cache();
// Defined with the display-settings apply below; shared with the boot path.
[[maybe_unused]] static SDL_DisplayID display_for_window();
[[maybe_unused]] static bool apply_exclusive_mode(SDL_DisplayID display, int w, int h);

static constexpr int kMaxConfiguredDisplayDimension = 16384;

// Dimensions of the ACTIVE canvas (world or UI; see CanvasTarget in
// video.h). Every offset conversion (offset = x + y*width), full-frame
// present rect and fade-surface size derives from these — byte-identical to
// the retired VIDEO_WIDTH/VIDEO_SIZE/CX_SCREEN/CY_SCREEN 320x200 constants
// while the active canvas is 320x200. The kUiCanvas fallbacks only
// matter for display-less (headless test) sessions, which never plot.
static inline int active_canvas_w() { return E_Screen ? E_Screen->canvas_w() : kUiCanvasW; }
static inline int active_canvas_h() { return E_Screen ? E_Screen->canvas_h() : kUiCanvasH; }


// videoptr lives in GameSession — access via current_session->videoptr_.

std::unique_ptr<Screen> E_Screen;

static void video_init_palettes(sdl_video& v)
{
	// Load our palettes ..
	load_and_set_palette("our.pal", v.ourpalette);
	load_palette("our.pal", v.redpalette);

	// Create the red-shifted palette
	for (Sint32 i = 32; i < 256; i++)
	{
		v.redpalette[static_cast<std::size_t>(i*3+1)] /= 2;
		v.redpalette[static_cast<std::size_t>(i*3+2)] /= 2;
	}

	load_palette("our.pal", v.bluepalette);
}

static void video_create_display(int windowed_base_w, int windowed_base_h)
{
	RenderEngine render = RenderEngine::NoZoom;

	// graphics/fullscreen: "off" | "borderless" | "exclusive" (legacy "on"
	// reads as borderless). Always create a desktop window first, then issue
	// the configured fullscreen request through the tracked live-apply path.
	// SDL3 exposes create-time pending flags through its getters; a Windowed
	// baseline gives timeout handling a real confirmed state to preserve.
	[[maybe_unused]] const og::ui::DisplayMode boot_display_mode =
	    og::ui::parse_display_mode(cfg.get_setting("graphics", "fullscreen"));

	// graphics/render (the legacy whole-canvas present engine) is retired:
	// SAI/Eagle smoothed the menus and ran a software 2x pass over the entire
	// active canvas. Smoothing now lives in
	// graphics/smoothing and applies to the world canvas only.
	render = RenderEngine::NoZoom;

	int requested_w = 640;
	int requested_h = 400;

#ifdef __EMSCRIPTEN__
	double css_w = 0.0;
	double css_h = 0.0;
	if (emscripten_get_element_css_size(
	        "#canvas", &css_w, &css_h) == EMSCRIPTEN_RESULT_SUCCESS &&
	    std::isfinite(css_w) && std::isfinite(css_h) &&
	    css_w > 0.0 && css_h > 0.0)
	{
		requested_w = std::clamp(
			static_cast<int>(std::lround(css_w)), 1,
			kMaxConfiguredDisplayDimension);
		requested_h = std::clamp(
			static_cast<int>(std::lround(css_h)), 1,
			kMaxConfiguredDisplayDimension);
	}
#else
	const std::pair<int, int> boot_res = og::ui::parse_resolution(
	    cfg.get_setting("graphics", "width"), cfg.get_setting("graphics", "height"));
	requested_w = std::clamp(boot_res.first, 320, kMaxConfiguredDisplayDimension);
	requested_h = std::clamp(boot_res.second, 200, kMaxConfiguredDisplayDimension);
#endif
	// Exclusive cfg dimensions are physical display pixels, not SDL logical
	// window coordinates. Create the remembered logical Windowed base and
	// attach the exact enumerated mode below; passing a Retina 4K value here
	// would otherwise request a 3840x2160-point window before mode selection.
#ifdef __EMSCRIPTEN__
	// The browser page owns fullscreen, and the shipped desktop default still
	// says "on". Do not let that ignored setting replace the browser-owned
	// logical canvas size with the persisted desktop Windowed size.
	const bool fullscreen_boot = false;
#else
	const bool fullscreen_boot =
		boot_display_mode != og::ui::DisplayMode::Windowed;
#endif
	const int w = fullscreen_boot ? windowed_base_w : requested_w;
	const int h = fullscreen_boot ? windowed_base_h : requested_h;
	Log("Creating screen {}x{}\n", w, h);
	E_Screen = std::make_unique<Screen>(render, w, h, 0);
	TRACE("init", "video initialized: %dx%d", w, h);
}

void apply_world_scale_from_cfg()
{
	if (!E_Screen)
		return;
	// The zoom model (graphics/zoom 0.1..1.0 + graphics/smoothing
	// off/sai/eagle): zoom 1.0 restores master's classic-density default,
	// aspect-expanded instead of stretched; lower values show more world
	// within a bounded resource budget.
	const std::string zoom_value = cfg.get_setting("graphics", "zoom");
	// Pre-zoom configs stored the full-frame filter in graphics/render.
	// Preserve SAI/Eagle through the new world-only path, while letting an
	// explicit graphics/smoothing value win.
	const std::string smoothing_value = og::ui::effective_smoothing_setting(
	    cfg.get_setting("graphics", "smoothing"),
	    cfg.get_setting("graphics", "render"));
	const int zoom_steps = og::parse_zoom_steps(zoom_value);
	const og::WorldScaleMode smoothing = og::parse_smoothing_setting(smoothing_value);
	int window_w = og::runtime::current_session != nullptr
		? static_cast<int>(og::runtime::current_session->window_w_) : 0;
	int window_h = og::runtime::current_session != nullptr
		? static_cast<int>(og::runtime::current_session->window_h_) : 0;
	if ((window_w <= 0 || window_h <= 0) && E_Screen->window != nullptr)
		SDL_GetWindowSize(E_Screen->window, &window_w, &window_h);
	E_Screen->set_world_zoom(zoom_steps, smoothing, window_w, window_h);
	const int applied_zoom_steps = E_Screen->world_zoom_steps();
	if (applied_zoom_steps != zoom_steps)
	{
		// Canvas replacement is transactional: an SDL allocation failure
		// retains the previous live canvas. Reflect that effective zoom into
		// cfg, then apply the requested smoothing to the retained canvas so
		// every DISPLAY label and persisted value describes what is rendering.
		cfg.apply_setting("graphics", "zoom",
		                  applied_zoom_steps == og::kZoomStepsMax
		                      ? "1.0"
		                      : "0." + std::to_string(applied_zoom_steps));
		E_Screen->set_world_zoom(applied_zoom_steps, smoothing,
		                        window_w, window_h);
	}
	TRACE("canvas", "zoom steps=%d smoothing=%d canvas=%dx%d", applied_zoom_steps,
	      static_cast<int>(smoothing), E_Screen->world_w(), E_Screen->world_h());
}

void restore_web_canvas_backing_size(int logical_w, int logical_h)
{
#ifdef __EMSCRIPTEN__
	if (!E_Screen || !E_Screen->window)
		return;
	logical_w = std::clamp(logical_w, 1, kMaxConfiguredDisplayDimension);
	logical_h = std::clamp(logical_h, 1, kMaxConfiguredDisplayDimension);

	int window_w = 0;
	int window_h = 0;
	SDL_GetWindowSize(E_Screen->window, &window_w, &window_h);
	const bool logical_size_changed =
		window_w != logical_w || window_h != logical_h;
	if (logical_size_changed)
	{
		SDL_SetWindowSize(E_Screen->window, logical_w, logical_h);
	}

	int expected_pixel_w = logical_w;
	int expected_pixel_h = logical_h;
	SDL_GetWindowSizeInPixels(
		E_Screen->window, &expected_pixel_w, &expected_pixel_h);
	int backing_w = 0;
	int backing_h = 0;
	if (emscripten_get_canvas_element_size(
	        "#canvas", &backing_w, &backing_h) == EMSCRIPTEN_RESULT_SUCCESS &&
	    (backing_w != expected_pixel_w || backing_h != expected_pixel_h))
	{
		// Cover a DOM-only mutation too: SDL_SetWindowSize may short-circuit
		// when its internal logical size was already repaired.
		emscripten_set_canvas_element_size(
			"#canvas", expected_pixel_w, expected_pixel_h);
	}

	const bool session_size_changed =
		og::runtime::current_session != nullptr &&
		(og::runtime::current_session->window_w_ != static_cast<float>(logical_w) ||
		 og::runtime::current_session->window_h_ != static_cast<float>(logical_h));
	if (session_size_changed)
	{
		og::runtime::current_session->window_w_ =
			static_cast<float>(logical_w);
		og::runtime::current_session->window_h_ =
			static_cast<float>(logical_h);
		update_overscan_setting();
	}
	if (logical_size_changed || session_size_changed)
	{
		apply_world_scale_from_cfg();
		if (og::runtime::current_session != nullptr &&
		    og::runtime::current_session->myscreen_ != nullptr)
		{
			og::runtime::current_session->myscreen_->relayout_views();
		}
	}
#else
	(void)logical_w;
	(void)logical_h;
#endif
}

static std::pair<int, int> configured_windowed_resolution(
	const og::ui::DisplayMode boot_mode)
{
	const std::string persisted_w =
		cfg.get_setting("graphics", "windowed_width");
	const std::string persisted_h =
		cfg.get_setting("graphics", "windowed_height");
	std::pair<int, int> resolution{640, 400};
	if (!persisted_w.empty() && !persisted_h.empty())
	{
		resolution = og::ui::parse_resolution(persisted_w, persisted_h);
	}
	else if (boot_mode != og::ui::DisplayMode::Exclusive)
	{
		// Migration for existing Windowed/Borderless configs, where the main
		// width/height pair already describes the logical Windowed size.
		resolution = og::ui::parse_resolution(
			cfg.get_setting("graphics", "width"),
			cfg.get_setting("graphics", "height"));
	}
	return {
		std::clamp(resolution.first, 320, kMaxConfiguredDisplayDimension),
		std::clamp(resolution.second, 200, kMaxConfiguredDisplayDimension)};
}

sdl_video::sdl_video()
    : text_normal(TEXT_1), text_big(TEXT_BIG)
{
	fullscreen = 0;
	fadeDuration = 500;
	owns_display_ = true;

	video_init_palettes(*this);
	const std::string boot_fullscreen = cfg.get_setting("graphics", "fullscreen");
	const std::string boot_width = cfg.get_setting("graphics", "width");
	const std::string boot_height = cfg.get_setting("graphics", "height");
	const og::ui::DisplayMode boot_mode =
		og::ui::parse_display_mode(boot_fullscreen);
	const auto windowed_res = configured_windowed_resolution(boot_mode);
	remembered_window_w_ = windowed_res.first;
	remembered_window_h_ = windowed_res.second;
	video_create_display(remembered_window_w_, remembered_window_h_);
	reflect_display_settings_from_window(DisplayStateConfirmation::Synchronized);
#ifndef __EMSCRIPTEN__
	if (boot_mode != og::ui::DisplayMode::Windowed)
	{
		// Baseline confirmation truthfully records the just-created Windowed
		// state. Restore the saved request before routing it through the same
		// tracked live-apply transaction used by the DISPLAY menu.
		cfg.apply_setting("graphics", "fullscreen", boot_fullscreen);
		cfg.apply_setting("graphics", "width", boot_width);
		cfg.apply_setting("graphics", "height", boot_height);
		apply_display_settings_from_cfg();
	}
	else
#endif
		apply_world_scale_from_cfg();
}

sdl_video::sdl_video(bool create_display)
    : text_normal(TEXT_1), text_big(TEXT_BIG)
{
	fullscreen = 0;
	fadeDuration = 500;
	owns_display_ = create_display;

	video_init_palettes(*this);
	if (create_display) {
		const std::string boot_fullscreen = cfg.get_setting("graphics", "fullscreen");
		const std::string boot_width = cfg.get_setting("graphics", "width");
		const std::string boot_height = cfg.get_setting("graphics", "height");
		const og::ui::DisplayMode boot_mode =
			og::ui::parse_display_mode(boot_fullscreen);
		const auto windowed_res = configured_windowed_resolution(boot_mode);
		remembered_window_w_ = windowed_res.first;
		remembered_window_h_ = windowed_res.second;
		video_create_display(remembered_window_w_, remembered_window_h_);
		reflect_display_settings_from_window(DisplayStateConfirmation::Synchronized);
#ifndef __EMSCRIPTEN__
		if (boot_mode != og::ui::DisplayMode::Windowed)
		{
			cfg.apply_setting("graphics", "fullscreen", boot_fullscreen);
			cfg.apply_setting("graphics", "width", boot_width);
			cfg.apply_setting("graphics", "height", boot_height);
			apply_display_settings_from_cfg();
		}
		else
#endif
			apply_world_scale_from_cfg();
	}
}

sdl_video::~sdl_video()
{
	// Free the multi-floor compositing scratch surfaces (independent of the
	// display) before any SDL_Quit below.
	if (floor_layer_) { SDL_DestroySurface(floor_layer_); floor_layer_ = nullptr; }
	if (floor_layer_scaled_) { SDL_DestroySurface(floor_layer_scaled_); floor_layer_scaled_ = nullptr; }

	// Only the display-owning video instance tears down SDL.
	// IMPORTANT: All non-owning video instances (sub-sessions with
	// owns_display_=false) must be destroyed BEFORE the owning instance,
	// because SDL_Quit() shuts down all subsystems globally.  The demo
	// enforces this by destroying sub-sessions before the host session.
	if (owns_display_) {
		E_Screen.reset();
		reset_format_detail_cache();
		SDL_Quit();
	}
}

void sdl_video::set_fullscreen(bool enable_fullscreen)
{
    (void)enable_fullscreen;
    // FIXME: A bug in my copy of SDL is making FULLSCREEN -> WINDOWED -> FULLSCREEN take up a partial portion of the screen and ruin the game.
    /*if(fullscreen)
    {
        SDL_SetWindowFullscreen(E_Screen->window, true);
    }
    else
    {
        SDL_SetWindowFullscreen(E_Screen->window, false);
        SDL_SetWindowSize(E_Screen->window, 640, 400);
    }
    
    int w, h;
    SDL_GetWindowSize(E_Screen->window, &w, &h);
    og::runtime::current_session->window_w_ = w;
    og::runtime::current_session->window_h_ = h;
    update_overscan_setting();*/
}

std::span<unsigned char> sdl_video::getbuffer()
{
	return videobuffer;
}

void sdl_video::clearbuffer()
{
    E_Screen->clear();
}

void sdl_video::clearbuffer(int x, int y, int w, int h)
{
    E_Screen->clear(x, y, w, h);
}

void sdl_video::clear_window()
{
    E_Screen->clear_window();
}

void sdl_video::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled)
{
	Sint32 xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	Sint32 ylength = y2 - y1 + 1;
	Sint32 i;

	if (!filled)          // Hollow box
	{
		hor_line(x1, y1, xlength, color);
		hor_line(x1, y2, xlength, color);
		ver_line(x1, y1, ylength, color);
		ver_line(x2, y1, ylength, color);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, color);
	}
}

void sdl_video::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled, Sint32 tobuffer)
{
	Sint32 xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	Sint32 ylength = y2 - y1 + 1;
	Sint32 i;

	if (!filled)          // Hollow box
	{
		hor_line(x1, y1, xlength, color, tobuffer);
		hor_line(x1, y2, xlength, color, tobuffer);
		ver_line(x1, y1, ylength, color, tobuffer);
		ver_line(x2, y1, ylength, color, tobuffer);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, color, tobuffer);
	}
}

void sdl_video::draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h, unsigned char color, Uint8 alpha)
{
    for (Uint32 i = 0; i < h; i++)
        hor_line_alpha(x, static_cast<Sint32>(static_cast<Uint32>(y)+i), static_cast<Sint32>(w), color, alpha);
}


void sdl_video::draw_button(const SDL_Rect& rect, Sint32 border)
{
    draw_button(rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1, border);
}

void sdl_video::draw_button_inverted(const SDL_Rect& rect)
{
    draw_text_bar(rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1);
}

void sdl_video::draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    draw_text_bar(x, y, static_cast<Sint32>(static_cast<Uint32>(x) + w - 1), static_cast<Sint32>(static_cast<Uint32>(y) + h - 1));
}


void sdl_video::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border)
{
	Sint32 xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	Sint32 ylength = y2 - y1 + 1;
	Sint32 i;

	if (border)           // Hollow box
	{
		hor_line(x1, y1, xlength, 15); // top, old 14
		hor_line(x1, y2, xlength, 11); // bottom, old 10
		ver_line(x1, y1, ylength, 14); // left, old 13
		ver_line(x2, y1, ylength, 12); // right, old 11
		draw_button(x1+1,y1+1,x2-1,y2-1,border-1);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, 13); // facing, old 12
	}
}

void sdl_video::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer)
{
	Sint32 xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	Sint32 ylength = y2 - y1 + 1;
	Sint32 i;

	if (border)           // Hollow box
	{
		hor_line(x1, y1, xlength, 15, tobuffer); // top, old 14
		hor_line(x1, y2, xlength, 11, tobuffer); // bottom, old 10
		ver_line(x1, y1, ylength, 14, tobuffer); // left, old 13
		ver_line(x2, y1, ylength, 12, tobuffer); // right, old 11
		draw_button(x1+1,y1+1,x2-1,y2-1,border-1, tobuffer);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, 13, tobuffer); // facing, old 12
	}
}

void sdl_video::draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, bool use_border, int base_color, int high_color, int shadow_color)
{
	Sint32 xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	Sint32 ylength = y2 - y1 + 1;
	Sint32 i;
	Sint32 tobuffer = 1;
	const unsigned char base = static_cast<unsigned char>(base_color);
	const unsigned char high = static_cast<unsigned char>(high_color);
	const unsigned char shadow = static_cast<unsigned char>(shadow_color);
    
    if(use_border)
    {
        // Fill
        for (i = 0; i < ylength-2; i++)
            hor_line(x1+1, y1+1+i, xlength-2, base, tobuffer); // facing

        // Borders
        hor_line(x1, y1, xlength, high, tobuffer); // top
        hor_line(x1, y2, xlength, shadow, tobuffer); // bottom
        ver_line(x1, y1, ylength, high, tobuffer); // left
        ver_line(x2, y1, ylength, shadow, tobuffer); // right
    }
    else
    {
        // Fill
        for (i = 0; i < ylength; i++)
            hor_line(x1, y1+i, xlength, base, tobuffer); // facing
    }
}

// Draws an empty but headed dialog box, returns the edge at
// which to draw text ... does NOT display to screen.
Sint32 sdl_video::draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                        const char *header)
{
	text& dialogtext = text_big; // large text
	Sint32 centerx = x1 + ( (x2-x1) /2 ), left;
	Sint32 textwidth;

	draw_button(x1, y1, x2, y2, 1, 1); // single-border width, to buffer
	draw_text_bar(x1+4, y1+4, x2-4, y1+18); // header field
	textwidth = dialogtext.query_width(header);
	left = centerx - (textwidth/2);

	if (header && header[0] != '\0') // display a title?
		dialogtext.write_xy(left, y1+6, header,
		                    static_cast<unsigned char>(RED), 1); // draw header to buffer
	draw_text_bar(x1+4, y1+20, x2-4, y2-4); // draw box for text

	return x1+6;  // where text should begin to display, left-aligned

}

void sdl_video::draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2)
{
	Sint32 xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	Sint32 ylength = y2 - y1 + 1;

	// First draw the filled, generic grey bar facing
	draw_box(x1, y1, x2, y2, 12, 1, 1); // filled, to buffer

	// Draw the indented border
	hor_line(x1, y1, xlength, 10, 1);  // top
	hor_line(x1, y2, xlength, 15, 1);  // bottom
	ver_line(x1, y1, ylength, 11, 1);  // left
	ver_line(x2, y1, ylength, 14, 1);  // right

}

void sdl_video::darken_screen()
{
    const int cw = active_canvas_w();
    const int ch = active_canvas_h();
    for(int i = 0; i < cw; i++)
    {
        for(int j = 0; j < ch; j++)
        {
            pointb(i, j, PURE_BLACK, 100);
        }
    }
}



void sdl_video::putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize)
{
	Sint32 curx, cury;
	Sint32 curpoint;

	if (!og::runtime::current_session->videoptr_) return;  // no direct video buffer to clear

	const Sint32 cw = active_canvas_w();
	const Sint32 canvas_size = cw * active_canvas_h();
	for(cury = starty;cury < starty +ysize;cury++)
	{
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curpoint = (curx + (cury*cw));
			if (curpoint > 0 && curpoint < canvas_size)
				og::runtime::current_session->videoptr_[curpoint] = 0;
		}
	}
}

// This version of fastbox writes directly to screen memory;
// The following version, with an extra parameter, writes to
// the buffer instead.  Note that it does NOT update (to screen)
// the area which it changes..
void sdl_video::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color)
{
	//buffers: we should always draw into the back buffer
	fastbox(startx,starty,xsize,ysize,color,1);
}


// SDL3's per-surface color helpers (SDL_MapSurfaceRGB & co.) and
// SDL_GetPixelFormatDetails resolve the pixel format through a global hash
// table on every call; SDL2 read a cached pointer straight off the surface.
// These sit on per-pixel paths (point/pointb/putpixel/blend_pixel and the
// sprite blit loops), where the lookup dominated browser frame time under
// software GL and starved CI's sanitizer runners. SDL interns the details
// structs for the process lifetime, so memoizing per format is safe.
static SDL_PixelFormat og_cached_details_format = SDL_PIXELFORMAT_UNKNOWN;
static const SDL_PixelFormatDetails* og_cached_details = nullptr;
static SDL_PixelFormat og_cached_lut_format = SDL_PIXELFORMAT_UNKNOWN;

// SDL frees the interned SDL_PixelFormatDetails structs in SDL_Quit(), so the
// memo below must not outlive the library: sessions churn SDL_Init/SDL_Quit
// (tests do so constantly) and a stale pointer is a use-after-free.
static void reset_format_detail_cache()
{
    og_cached_details_format = SDL_PIXELFORMAT_UNKNOWN;
    og_cached_details = nullptr;
    og_cached_lut_format = SDL_PIXELFORMAT_UNKNOWN;
}

static const SDL_PixelFormatDetails* cached_format_details(SDL_PixelFormat format)
{
    if (format != og_cached_details_format || og_cached_details == nullptr) {
        og_cached_details = SDL_GetPixelFormatDetails(format);
        og_cached_details_format = format;
    }
    return og_cached_details;
}

// SDL_MapSurfaceRGB without the per-call hash lookup (SDL_GetSurfacePalette
// is a plain field read).
static inline Uint32 map_surface_rgb_fast(SDL_Surface* surface, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapRGB(cached_format_details(surface->format),
                      SDL_GetSurfacePalette(surface), r, g, b);
}


// Per-pixel palette conversion (query_palette_reg + SDL_MapRGB pairs) still
// dominates the sprite/text blit loops (~5M pixels/s at 72fps full-screen).
// Cache the 256-entry mapped-color table; the palette lives in session state
// and only changes on loads/fades, so a 768-byte compare per blit call keeps
// the table fresh at negligible cost.
static const Uint32* palette_color_lut(SDL_Surface* target)
{
    static std::array<unsigned char, 768> cached_pal{};
    static std::array<Uint32, 256> lut{};
    const unsigned char* cur = og::runtime::current_session->curpal_.data();
    if (target->format != og_cached_lut_format ||
        std::memcmp(cached_pal.data(), cur, cached_pal.size()) != 0)
    {
        std::memcpy(cached_pal.data(), cur, cached_pal.size());
        og_cached_lut_format = target->format;
        const SDL_PixelFormatDetails* det = cached_format_details(target->format);
        SDL_Palette* pal = SDL_GetSurfacePalette(target);
        for (int i = 0; i < 256; ++i)
        {
            lut[static_cast<std::size_t>(i)] = SDL_MapRGB(
                det, pal,
                static_cast<Uint8>(cached_pal[static_cast<std::size_t>(i) * 3] * 4),
                static_cast<Uint8>(cached_pal[static_cast<std::size_t>(i) * 3 + 1] * 4),
                static_cast<Uint8>(cached_pal[static_cast<std::size_t>(i) * 3 + 2] * 4));
        }
    }
    return lut.data();
}

Uint32 get_Uint32_color(unsigned char color)
{
	return palette_color_lut(E_Screen->render)[color];
}

// This is the version which writes to the buffer..
void sdl_video::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color, unsigned char flag)
{
	SDL_Rect rect;
	int r,g,b;

	// Zardus: FIX: small check to make sure we're not trying to put in antimatter or something
	if (xsize < 0 || ysize < 0 || startx < 0 || starty < 0)
		return;

	if (!flag) // then write to screen directly
	{
		fastbox(startx, starty, xsize, ysize, color);
		return ;
	}

	//buffers: create the rect to fill with SDL_FillSurfaceRect
	rect.x = startx;
	rect.y = starty;
	rect.w = xsize;
	rect.h = ysize;

	query_palette_reg(color,&r,&g,&b);
	SDL_FillSurfaceRect(E_Screen->render, &rect, map_surface_rgb_fast(E_Screen->render,
	                                                 static_cast<Uint8>(r * 4),
	                                                 static_cast<Uint8>(g * 4),
	                                                 static_cast<Uint8>(b * 4)));
}

void sdl_video::fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color)
{
    draw_box(startx, starty, startx + xsize, starty + ysize, color, 0);
}

// Place a point on the screen
//buffers: PORT: this point func is equivalent to drawing directly to screen
void sdl_video::point(Sint32 x, Sint32 y, unsigned char color)
{
	pointb(x,y,color);
	//buffers: PORT: SDL_UpdateRect(screen,x,y,1,1);
}

void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    if(x < 0 || y < 0 || x >= surface->w || y >= surface->h)
        return;
    
    const SDL_PixelFormatDetails* d = cached_format_details(surface->format);
    int bpp = d->bytes_per_pixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = static_cast<Uint8>(pixel);
        break;

    case 2:
        *reinterpret_cast<Uint16*>(p) = static_cast<Uint16>(pixel);
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = static_cast<Uint8>((pixel >> 16) & 0xff);
            p[1] = static_cast<Uint8>((pixel >> 8) & 0xff);
            p[2] = static_cast<Uint8>(pixel & 0xff);
        } else {
            p[0] = static_cast<Uint8>(pixel & 0xff);
            p[1] = static_cast<Uint8>((pixel >> 8) & 0xff);
            p[2] = static_cast<Uint8>((pixel >> 16) & 0xff);
        }
        break;

    case 4:
        *reinterpret_cast<Uint32*>(p) = pixel;
        break;
    }
}

//buffers: PORT: this draws a point in the offscreen buffer
//buffers: PORT: used for all the funcs that draw stuff in the offscreen buf
void sdl_video::pointb(Sint32 x, Sint32 y, unsigned char color)
{
	int r,g,b;
	int c;

	//buffers: this does bound checking (just to be safe)
	//buffers: bound check against the CURRENT render target (mirrors
	// get_pixel). During a padded floor-layer redirect the target is the
	// grown off-screen layer, which extends past the legacy 320x200 logical
	// screen; a hardcoded 319/199 clip would truncate the padded window.
	if (x < 0 || y < 0 || x >= E_Screen->render->w || y >= E_Screen->render->h)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = static_cast<int>(map_surface_rgb_fast(E_Screen->render,
	               static_cast<Uint8>(r * 4),
	               static_cast<Uint8>(g * 4),
	               static_cast<Uint8>(b * 4)));

    putpixel(E_Screen->render, x, y, static_cast<Uint32>(c));
}

void blend_pixel(SDL_Surface* surface, int x, int y, Uint32 color, Uint8 alpha)
{
    const SDL_PixelFormatDetails* d = cached_format_details(surface->format);
    Uint32 Rmask = d->Rmask, Gmask = d->Gmask, Bmask = d->Bmask, Amask = d->Amask;
    Uint32 R,G,B,A=0;//SDL_ALPHA_OPAQUE;
    Uint32* pixel;
    switch (d->bytes_per_pixel)
    {
        case 1: { /* Assuming 8-bpp */

                Uint8 *pixel8 = static_cast<Uint8*>(surface->pixels) + y*surface->pitch + x;

                SDL_Palette* pal = SDL_GetSurfacePalette(surface);
                Uint8 dR = pal->colors[*pixel8].r;
                Uint8 dG = pal->colors[*pixel8].g;
                Uint8 dB = pal->colors[*pixel8].b;
                Uint8 sR = pal->colors[color].r;
                Uint8 sG = pal->colors[color].g;
                Uint8 sB = pal->colors[color].b;
                
                dR = static_cast<Uint8>(dR + (((sR - dR) * alpha) >> 8));
                dG = static_cast<Uint8>(dG + (((sG - dG) * alpha) >> 8));
                dB = static_cast<Uint8>(dB + (((sB - dB) * alpha) >> 8));
            
                *pixel8 = static_cast<Uint8>(map_surface_rgb_fast(surface, dR, dG, dB));
                
        }
        break;

        case 2: { /* Probably 15-bpp or 16-bpp */		
            
                Uint16 *pixel16 = static_cast<Uint16*>(surface->pixels) + y*surface->pitch/2 + x;
                Uint32 dc = *pixel16;
            
                R = ((dc & Rmask) + (( (color & Rmask) - (dc & Rmask) ) * alpha >> 8)) & Rmask;
                G = ((dc & Gmask) + (( (color & Gmask) - (dc & Gmask) ) * alpha >> 8)) & Gmask;
                B = ((dc & Bmask) + (( (color & Bmask) - (dc & Bmask) ) * alpha >> 8)) & Bmask;
                if( Amask )
                    A = ((dc & Amask) + (( (color & Amask) - (dc & Amask) ) * alpha >> 8)) & Amask;

                *pixel16 = static_cast<Uint16>(R | G | B | A);
                
        }
        break;

        case 3: { /* Slow 24-bpp mode, usually not used */
            Uint8 *pix = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x*3;
            Uint8 rshift8=d->Rshift/8;
            Uint8 gshift8=d->Gshift/8;
            Uint8 bshift8=d->Bshift/8;
            Uint8 ashift8=d->Ashift/8;
            
            
            
                Uint8 dR, dG, dB, dA=0;
                Uint8 sR, sG, sB, sA=0;
                
                pix = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x*3;
                
                dR = *((pix)+rshift8); 
                dG = *((pix)+gshift8);
                dB = *((pix)+bshift8);
                dA = *((pix)+ashift8);
                
                sR = (color>>d->Rshift)&0xff;
                sG = (color>>d->Gshift)&0xff;
                sB = (color>>d->Bshift)&0xff;
                sA = (color>>d->Ashift)&0xff;
                
                dR = static_cast<Uint8>(dR + (((sR - dR) * alpha) >> 8));
                dG = static_cast<Uint8>(dG + (((sG - dG) * alpha) >> 8));
                dB = static_cast<Uint8>(dB + (((sB - dB) * alpha) >> 8));
                dA = static_cast<Uint8>(dA + (((sA - dA) * alpha) >> 8));

                *((pix)+rshift8) = dR; 
                *((pix)+gshift8) = dG;
                *((pix)+bshift8) = dB;
                *((pix)+ashift8) = dA;
                
        }
        break;

        case 4: /* Probably 32-bpp */
            pixel = static_cast<Uint32*>(surface->pixels) + y*surface->pitch/4 + x;
            Uint32 dc = *pixel;

			// The smart-smoothing gameplay overlay starts fully transparent and
			// is composited over the filtered world later. Preserve the caller's
			// coverage as real straight alpha here; the legacy canvases and floor
			// layers keep their historical RGB-only blend below.
			if (Amask && E_Screen != nullptr &&
			    E_Screen->active_canvas() == CanvasTarget::GameplayUI &&
			    E_Screen->gameplay_ui_overlay_active())
			{
				const auto channel = [](Uint32 value, Uint32 mask, Uint8 shift) {
					return static_cast<Uint32>((value & mask) >> shift);
				};
				const Uint32 src_a = alpha;
				const Uint32 dst_a = channel(dc, Amask, d->Ashift);
				const Uint32 inv_a = 255u - src_a;
				const Uint32 out_a = src_a + (dst_a * inv_a + 127u) / 255u;
				const auto composite_channel = [&](Uint32 mask, Uint8 shift) {
					if (out_a == 0)
						return Uint32{0};
					const Uint32 src = channel(color, mask, shift);
					const Uint32 dst = channel(dc, mask, shift);
					const Uint32 premul = src * src_a +
						(dst * dst_a * inv_a + 127u) / 255u;
					return (premul + out_a / 2u) / out_a;
				};
				*pixel = SDL_MapRGBA(
					d, SDL_GetSurfacePalette(surface),
					static_cast<Uint8>(composite_channel(Rmask, d->Rshift)),
					static_cast<Uint8>(composite_channel(Gmask, d->Gshift)),
					static_cast<Uint8>(composite_channel(Bmask, d->Bshift)),
					static_cast<Uint8>(out_a));
				break;
			}

            R = color & Rmask;
            G = color & Gmask;
            B = color & Bmask;
            A = 0;  // keep this as 0 to avoid corruption of non-alpha surfaces
            
            // Blend and keep dest alpha
            if( alpha != SDL_ALPHA_OPAQUE )
            {
                R = ((dc & Rmask) + (( R - (dc & Rmask) ) * alpha >> 8)) & Rmask;
                G = ((dc & Gmask) + (( G - (dc & Gmask) ) * alpha >> 8)) & Gmask;
                B = ((dc & Bmask) + (( B - (dc & Bmask) ) * alpha >> 8)) & Bmask;
            }
            if(Amask)
                A = (dc & Amask);
            
            *pixel = R | G | B | A;
        break;
    }
}

void sdl_video::pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha)
{
	int r,g,b;
	int c;

	//buffers: this does bound checking (just to be safe)
	//buffers: bound check against the CURRENT render target (mirrors
	// get_pixel). During a padded floor-layer redirect the target is the
	// grown off-screen layer, which extends past the legacy 320x200 logical
	// screen; a hardcoded 319/199 clip would truncate the padded window.
	if (x < 0 || y < 0 || x >= E_Screen->render->w || y >= E_Screen->render->h)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = static_cast<int>(map_surface_rgb_fast(E_Screen->render,
	               static_cast<Uint8>(r * 4),
	               static_cast<Uint8>(g * 4),
	               static_cast<Uint8>(b * 4)));

    blend_pixel(E_Screen->render, x, y, static_cast<Uint32>(c), alpha);
}

//buffers: this sets the color using raw RGB values. no *4...
void sdl_video::pointb(Sint32 x, Sint32 y, int r, int g, int b)
{
	SDL_Rect  rect;
	int c;
	c = static_cast<int>(map_surface_rgb_fast(E_Screen->render,
	               static_cast<Uint8>(r),
	               static_cast<Uint8>(g),
	               static_cast<Uint8>(b)));

	rect.x = x;
	rect.y = y;
	rect.w = 1;
	rect.h = 1;
	SDL_FillSurfaceRect(E_Screen->render,&rect,static_cast<Uint32>(c));
}

//buffers: draw color using an offset
void sdl_video::pointb(int offset, unsigned char color)
{
	int x, y;

	const int cw = active_canvas_w();
	y = offset/cw;
	x = offset - y*cw;

	pointb(x,y,color);
}

// Place a horizontal line on the screen.
//buffers: this func originally drew directly to the screen
void sdl_video::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
	hor_line(x,y,length,color,1);
}

void sdl_video::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer)
{
	Sint32 i;

	if (!tobuffer)
	{
		hor_line(x,y,length,color);
		return;
	}
	
	for (i = 0; i < length; i++)
		pointb(x+i,y,color);
}

void sdl_video::hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Uint8 alpha)
{
	Sint32 i;

	for (i = 0; i < length; i++)
		pointb(x+i,y,color, alpha);
}


// Place a vertical line on the screen.
// buffers: this func originally drew directly to the screen
void sdl_video::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
	//buffers: we always want to draw to the back buffer now
	ver_line(x,y,length,color,1);
}

void sdl_video::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer)
{
	Sint32 i;

	if (!tobuffer)
	{
		ver_line(x,y,length,color);
		return;
	}
	
	for (i = 0; i < length; i++)
		pointb(x,y+i,color);
}

// From SPriG
void sdl_video::draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color)
{
    SDL_Surface* Surface = E_Screen->render;
    if(Surface == nullptr)
        return;
    
    // Did the line miss the screen completely?
    if((x1 < 0 && x2 < 0) || (y1 < 0 && y2 < 0))
        return;
    if((x1 >= Surface->w && x2 >= Surface->w) || (y1 >= Surface->h && y2 >= Surface->h))
        return;
    
    Uint32 Color = get_Uint32_color(color);
    Sint32 dx, dy, sdx, sdy, x, y, px, py;

    dx = x2 - x1;
    dy = y2 - y1;

    sdx = (dx < 0) ? -1 : 1;
    sdy = (dy < 0) ? -1 : 1;

    dx = sdx * dx + 1;
    dy = sdy * dy + 1;

    x = y = 0;

    px = x1;
    py = y1;

    if (dx >= dy)
    {
        for (x = 0; x < dx; x++)
        {
            putpixel(Surface, px, py, Color);

            y += dy;
            if (y >= dx)
            {
                y -= dx;
                py += sdy;
            }
            px += sdx;
        }
    }
    else
    {
        for (y = 0; y < dy; y++)
        {
            putpixel(Surface, px, py, Color);

            x += dx;
            if (x >= dy)
            {
                x -= dy;
                px += sdx;
            }
            py += sdy;
        }
    }
}

//
//sdl_video::do_cycle
//cycle the palette for flame and water motion
// query and set functions are located in pal32.cpp
//buffers: PORT: added & to the last 3 args of the query_palette_reg funcs
void sdl_video::do_cycle(Sint32 curmode, Sint32 maxmode)
{
	Sint32 i;
	//buffers: PORT: changed these two arrays to ints
	std::array<int, 3> tempcol{};
	std::array<int, 3> curcol{};

	curmode %= maxmode;   // avoid over-runs

	if (!curmode)  // then cycle on 0
	{
		// For orange:
		query_palette_reg(ORANGE_END, &tempcol[0],
		                  &tempcol[1], &tempcol[2]);        // get first color
		for (i=ORANGE_END; i > ORANGE_START; i--)
		{
			query_palette_reg(static_cast<unsigned char>(static_cast<char>(i-1)), &curcol[0], &curcol[1], &curcol[2]);
			set_palette_reg(static_cast<unsigned char>(static_cast<char>(i)), static_cast<char>(curcol[0]),static_cast<char>(curcol[1]), static_cast<char>(curcol[2]));
		}
		set_palette_reg(ORANGE_START, tempcol[0],
		                tempcol[1], tempcol[2]);        // reassign last to first

		// For blue:
		query_palette_reg(WATER_END, &tempcol[0],
		                  &tempcol[1], &tempcol[2]);        // get first color
		for (i=WATER_END; i > WATER_START; i--)
		{
			query_palette_reg(static_cast<unsigned char>(static_cast<char>(i-1)), &curcol[0], &curcol[1], &curcol[2]);
			set_palette_reg(static_cast<unsigned char>(static_cast<char>(i)), curcol[0], curcol[1], curcol[2]);
		}
		set_palette_reg(WATER_START, tempcol[0],
		                tempcol[1], tempcol[2]);        // reassign last to first
	}
}

//sdl_video::putdata
//draws objects to screen, respecting transparency
//used by text
void sdl_video::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Uint32 num = 0;

	for(cury = starty;cury < starty +ysize;cury++)
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curcolor = sourcedata[num++];
			if (!curcolor)
				continue;
			//buffers: PORT: targ = (curx + (cury*VIDEO_WIDTH));
			//buffers: PORT: if (targ>0 && targ<VIDEO_SIZE)
			//buffers: PORT: videoptr[targ] = curcolor;
			point(curx,cury,curcolor);//buffers: PORT: draw the point
		}
}

// putdata with alpha blending
void sdl_video::putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata, unsigned char alpha)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Uint32 num = 0;

	for(cury = starty;cury < starty +ysize;cury++)
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curcolor = sourcedata[num++];
			if (!curcolor)
				continue;
            
			pointb(curx,cury,curcolor, alpha);
		}
}


void sdl_video::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata)
{
        Sint32 curx, cury;
        unsigned char curcolor;
       	Uint32 num = 0;
	int color;
	SDL_Rect rect;

	for(cury = starty;cury < starty +ysize;cury++)
 	{
		for (curx = startx; curx < startx +xsize; curx++)
	        {
			curcolor = sourcedata[num++];
			if (!curcolor)
		        	continue;
			//point(curx,cury,curcolor);//buffers: PORT: draw the poin
			color = static_cast<int>(palette_color_lut(E_Screen->render)[curcolor]);

			rect.x = curx;
			rect.y = cury;
			rect.w = 1;
			rect.h = 1;
			SDL_FillSurfaceRect(E_Screen->render,&rect,static_cast<Uint32>(color));
		}
    	}
}

//sdl_video::putdata
//draws objects to screen, respecting transparency
//used by text
void sdl_video::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata, unsigned char color)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Uint32 num = 0;

	for(cury = starty;cury < starty +ysize;cury++)
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curcolor = sourcedata[num++];
			if (!curcolor)
				continue;
			//if (curcolor>=248) curcolor = color+(curcolor-248);
			if (curcolor>247)
				curcolor = color;
			//buffers: PORT: targ = (curx + (cury*VIDEO_WIDTH));
			//buffers: PORT: if (targ>0 && targ<VIDEO_SIZE)
			//buffers: PORT: videoptr[targ] = curcolor;
			point(curx,cury,curcolor);
		}
}

void sdl_video::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata, unsigned char color)
{
        Sint32 curx, cury;
        unsigned char curcolor;
        Uint32 num = 0;
	int scolor;
	SDL_Rect rect;

       for(cury = starty;cury < starty +ysize;cury++)
	       for (curx = startx; curx < startx +xsize; curx++)
               {
	                curcolor = sourcedata[num++];
                        if (!curcolor)
  	                      	continue;
				//if (curcolor>=248) curcolor = color+(curcolor-248);
	        if (curcolor>247)
	        {
		        curcolor = color;
	        }
			scolor = static_cast<int>(palette_color_lut(E_Screen->render)[curcolor]);

            rect.x = curx;
            rect.y = cury;
			rect.w = 1;	
			rect.h = 1;
			SDL_FillSurfaceRect(E_Screen->render,&rect,static_cast<Uint32>(scolor));
		}
}

// sdl_video::putbuffer
// used to put tiles into the buffer as we compose the screen
// tilestartx,tilestarty are the ul corner of the tiles position on
//    screen, which may be negative since we have tiles offscreen
// tilewidth,tileheight are the tile size, which will usually be GRID_SIZE
//    but this leaves things open
// portstartx portstarty portendx porthendy allow us to clip to
//    a rectangular window on screen, ie a viewscreen
// sourceptr is a pointer to the video data to be copied into the buffer
void sdl_video::putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                      Sint32 tilewidth, Sint32 tileheight,
                      Sint32 portstartx, Sint32 portstarty,
                      Sint32 portendx, Sint32 portendy,
                      std::span<const unsigned char> sourceptr)
{
	int i,j,num;
	Sint32 xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
	//Uint32 targetshifter,sourceshifter; //these let you wrap around in the arrays
	Sint32 totrows,rowsize; //number of rows and width of each row in the source
	//Uint32 offssource,offstarget; //offsets into each array, for clipping and wrap
	const unsigned char * sourcebufptr = sourceptr.data();
	if (tilestartx >= portendx || tilestarty >= portendy )
		return; // abort, the tile is drawing outside the clipping region

	if ((tilestartx + tilewidth) > portendx)   //this clips on the right edge
		xmax = portendx - tilestartx; //stop drawing after xmax bytes

	else if (tilestartx < portstartx) //this clips on the left edge
	{
		xmin = portstartx - tilestartx;
		tilestartx = portstartx;
	}

	if ((tilestarty + tileheight) > portendy) //this clips on the bottom edge
		ymax = portendy - tilestarty;

	else if (tilestarty < portstarty) //this clips the top edge
	{
		ymin = portstarty - tilestarty;
		tilestarty = portstarty;
	}

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
	//sourceshifter = tilewidth - rowsize;  //this will wrap the source around

	//offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
	//offssource = (ymin * tilewidth) + xmin; //start at u-l position

	//buffers: draws graphic. actually uses the above bound checking now (7/18/02)
	SDL_Surface* target = E_Screen->render;
	if (cached_format_details(target->format)->bytes_per_pixel == 4)
	{
		// Fast path: palette-LUT convert straight into the 32bpp canvas.
		// pointb's per-pixel palette query + format map + putpixel dispatch
		// dominated frame time on the throttled web profile; tiles are
		// opaque, so this is a plain converting row copy.
		const Uint32* lut = palette_color_lut(target);
		for (i = ymin; i < ymax; i++)
		{
			const Sint32 dy = tilestarty + (i - ymin);
			if (dy < 0 || dy >= target->h)
				continue;
			Uint32* row = reinterpret_cast<Uint32*>(
				static_cast<Uint8*>(target->pixels) +
				static_cast<std::size_t>(dy) * static_cast<std::size_t>(target->pitch));
			for (j = xmin; j < xmax; j++)
			{
				const Sint32 dx = tilestartx + (j - xmin);
				if (dx < 0 || dx >= target->w)
					continue;
				row[dx] = lut[sourcebufptr[i * tilewidth + j]];
			}
		}
		return;
	}
	num=0;
	for(i=ymin;i<ymax;i++)
	{
		for(j=xmin;j<xmax;j++)
		{
			num = i*tilewidth + j;
			pointb(j+tilestartx-xmin,i+tilestarty-ymin,sourcebufptr[num]);
		}
	}
}

void sdl_video::putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
                      Sint32 tilewidth, Sint32 tileheight,
                      Sint32 portstartx, Sint32 portstarty,
                      Sint32 portendx, Sint32 portendy,
                      std::span<const unsigned char> sourceptr, unsigned char alpha)
{
	int i,j,num;
	Sint32 xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
	//Uint32 targetshifter,sourceshifter; //these let you wrap around in the arrays
	Sint32 totrows,rowsize; //number of rows and width of each row in the source
	//Uint32 offssource,offstarget; //offsets into each array, for clipping and wrap
	const unsigned char * sourcebufptr = sourceptr.data();
	if (tilestartx >= portendx || tilestarty >= portendy )
		return; // abort, the tile is drawing outside the clipping region

	if ((tilestartx + tilewidth) > portendx)   //this clips on the right edge
		xmax = portendx - tilestartx; //stop drawing after xmax bytes

	else if (tilestartx < portstartx) //this clips on the left edge
	{
		xmin = portstartx - tilestartx;
		tilestartx = portstartx;
	}

	if ((tilestarty + tileheight) > portendy) //this clips on the bottom edge
		ymax = portendy - tilestarty;

	else if (tilestarty < portstarty) //this clips the top edge
	{
		ymin = portstarty - tilestarty;
		tilestarty = portstarty;
	}

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
	//sourceshifter = tilewidth - rowsize;  //this will wrap the source around

	//offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
	//offssource = (ymin * tilewidth) + xmin; //start at u-l position

	//buffers: draws graphic. actually uses the above bound checking now (7/18/02)
	num=0;
	for(i=ymin;i<ymax;i++)
	{
		for(j=xmin;j<xmax;j++)
		{
			num = i*tilewidth + j;
			pointb(j+tilestartx-xmin,i+tilestarty-ymin,sourcebufptr[num], alpha);
		}
	}
}

//buffers: this is the SDL_Surface accelerated version of putbuffer
void sdl_video::putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                      Sint32 tilewidth, Sint32 tileheight,
                      Sint32 portstartx, Sint32 portstarty,
                      Sint32 portendx, Sint32 portendy,
                      SDL_Surface *sourceptr)
{
	SDL_Rect rect,temp;
	Sint32 xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
	//Uint32 targetshifter,sourceshifter; //these let you wrap around in the arrays
	Sint32 totrows,rowsize; //number of rows and width of each row in the source
	//Uint32 offssource,offstarget; //offsets into each array, for clipping and wrap
	//buffers: unsigned char * sourcebufptr = &sourceptr[0];
	if (tilestartx >= portendx || tilestarty >= portendy )
		return; // abort, the tile is drawing outside the clipping region

	if ((tilestartx + tilewidth) > portendx)   //this clips on the right edge
		xmax = portendx - tilestartx; //stop drawing after xmax bytes
	else if (tilestartx < portstartx) //this clips on the left edge
	{
		xmin = portstartx - tilestartx;
		tilestartx = portstartx;
	}

	if ((tilestarty + tileheight) > portendy) //this clips on the bottom edge
		ymax = portendy - tilestarty;
	else if (tilestarty < portstarty) //this clips the top edge
	{
		ymin = portstarty - tilestarty;
		tilestarty = portstarty;
	}

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
	//sourceshifter = tilewidth - rowsize;  //this will wrap the source around

	//offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
	//offssource = (ymin * tilewidth) + xmin; //start at u-l position

	rect.x = (tilestartx);
	rect.y = (tilestarty);
	temp.x = xmin;
	temp.y = ymin;
	temp.w = (xmax-xmin);
	temp.h = (ymax-ymin);
	SDL_BlitSurface(sourceptr,&temp,E_Screen->render,&rect);
}

void sdl_video::putbuffer_surface(Sint32 tilestartx, Sint32 tilestarty,
                                  Sint32 tilewidth, Sint32 tileheight,
                                  Sint32 portstartx, Sint32 portstarty,
                                  Sint32 portendx, Sint32 portendy,
                                  void* sourceptr)
{
    putbuffer(tilestartx, tilestarty, tilewidth, tileheight, portstartx,
              portstarty, portendx, portendy,
              static_cast<SDL_Surface*>(sourceptr));
}

void* sdl_video::create_accel_surface(std::span<const unsigned char> indexed_pixels,
                                      Sint32 width, Sint32 height)
{
    if (width <= 0 || height <= 0)
        return nullptr;

    const std::size_t expected_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (indexed_pixels.size() < expected_size)
        return nullptr;

    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        LogError("sdl_video::create_accel_surface: SDL_CreateSurface failed: {}\n",
                 SDL_GetError());
        return nullptr;
    }

    if (SDL_MUSTLOCK(surface) && !SDL_LockSurface(surface)) {
        LogError("sdl_video::create_accel_surface: SDL_LockSurface failed: {}\n",
                 SDL_GetError());
        SDL_DestroySurface(surface);
        return nullptr;
    }

    Uint32* pixels = static_cast<Uint32*>(surface->pixels);
    const std::size_t pitch_pixels = static_cast<std::size_t>(surface->pitch) /
                                     sizeof(Uint32);
    std::size_t src_index = 0;
    for (Sint32 y = 0; y < height; ++y)
    {
        for (Sint32 x = 0; x < width; ++x, ++src_index)
        {
            pixels[static_cast<std::size_t>(y) * pitch_pixels +
                   static_cast<std::size_t>(x)] =
                palette_color_lut(surface)[indexed_pixels[src_index]];
        }
    }

    if (SDL_MUSTLOCK(surface))
        SDL_UnlockSurface(surface);

    return surface;
}

void sdl_video::destroy_accel_surface(void* surface)
{
    if (!surface)
        return;
    SDL_DestroySurface(static_cast<SDL_Surface*>(surface));
}

// ---- Multi-floor vertical-parallax off-screen layer compositing ----
//
// A non-camera floor that is faded/ghosted is drawn 1:1 onto a transparent
// off-screen layer (so adjacent tiles abut exactly — no per-tile sub-pixel
// seams), then composited back onto the real render surface as ONE bitmap,
// smoothly (bilinear) scaled about the viewport centre and faded by the
// floor's depth alpha. Un-drawn cells (air holes / out-of-map) stay
// transparent and reveal the floors below.
bool sdl_video::floor_layer_begin(Sint32 x, Sint32 y, Sint32 w, Sint32 h)
{
    if (!E_Screen || !E_Screen->render)
        return false;
    const Sint64 extent_w = static_cast<Sint64>(x) + w;
    const Sint64 extent_h = static_cast<Sint64>(y) + h;
    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        extent_w <= 0 || extent_h <= 0 ||
        extent_w > std::numeric_limits<int>::max() ||
        extent_h > std::numeric_limits<int>::max())
    {
        TRACE("render", "floor_layer_fallback reason=invalid source=%d,%d %dx%d",
              static_cast<int>(x), static_cast<int>(y),
              static_cast<int>(w), static_cast<int>(h));
#ifdef TESTING
        ++floor_layer_fallback_count_;
#endif
        return false;
    }
    // A below-camera floor draws a pad-widened window, so (x+w, y+h) can
    // exceed the render size: grow the layer on demand (never shrink — other
    // viewports may still need the full render extent this frame). The pad is
    // bounded by the caller's scale clamp (kMinBelowFloorScale), so the layer
    // tops out at ~2x the render dimensions.
    const int need_w = std::max(E_Screen->render->w,
                                static_cast<int>(extent_w));
    const int need_h = std::max(E_Screen->render->h,
                                static_cast<int>(extent_h));
    const Sint64 need_pixels = static_cast<Sint64>(need_w) * need_h;
    const auto report_fallback = [&](bool budget) {
        const char* const reason = budget ? "budget" : "allocation";
        TRACE("render",
              "floor_layer_fallback reason=%s source=%dx%d pixels=%lld budget=%lld",
              reason, need_w, need_h, static_cast<long long>(need_pixels),
              static_cast<long long>(kFloorLayerSourcePixelBudget));
#ifdef TESTING
        ++floor_layer_fallback_count_;
#endif
        if (floor_layer_reported_fallback_w_ != need_w ||
            floor_layer_reported_fallback_h_ != need_h ||
            floor_layer_reported_budget_fallback_ != budget)
        {
            LogWarn("Multifloor compositor {} fallback for {}x{} source "
                    "({} pixels; budget {}); drawing the faded floor "
                    "directly without parallax scaling/depth FX.\n",
                    reason, need_w, need_h, need_pixels,
                    kFloorLayerSourcePixelBudget);
            floor_layer_reported_fallback_w_ = need_w;
            floor_layer_reported_fallback_h_ = need_h;
            floor_layer_reported_budget_fallback_ = budget;
        }
        return false;
    };
    if (need_pixels > kFloorLayerSourcePixelBudget)
        return report_fallback(true);

    if (!floor_layer_ || floor_layer_->w < need_w || floor_layer_->h < need_h)
    {
        // Allocate transactionally. A transient failed growth must not throw
        // away a smaller cached layer that another viewport can still use.
        SDL_Surface* next = nullptr;
#ifdef TESTING
        if (fail_next_floor_layer_allocation_)
            fail_next_floor_layer_allocation_ = false;
        else
#endif
            next = SDL_CreateSurface(need_w, need_h, SDL_PIXELFORMAT_ARGB8888);
        if (!next)
            return report_fallback(false);
        SDL_SetSurfaceBlendMode(next, SDL_BLENDMODE_BLEND);
        SDL_DestroySurface(floor_layer_);
        floor_layer_ = next;
    }

    // Clear just this viewport's region to fully transparent (ARGB = 0). Opaque
    // tile/sprite blits below go through SDL_MapRGB on this alpha-capable format,
    // which yields A=0xFF, so drawn pixels become opaque coverage while un-drawn
    // cells remain transparent.
    SDL_Rect r{ x, y, w, h };
    if (!SDL_FillSurfaceRect(floor_layer_, &r, 0x00000000u))
        return report_fallback(false);

    // Redirect every tile/sprite blit (they hardcode E_Screen->render) to the
    // layer; floor_layer_end restores the saved surface.
    floor_layer_saved_render_ = E_Screen->render;
    E_Screen->render = floor_layer_;
    return true;
}

void sdl_video::floor_layer_end(Sint32 x, Sint32 y, Sint32 w, Sint32 h,
                                float scale, Sint32 cx, Sint32 cy,
                                unsigned char alpha,
                                DepthFxParams fx,
                                Sint32 pad_x, Sint32 pad_y)
{
    if (!E_Screen)
        return;
    // Restore the real render target (mirror floor_layer_begin's redirect).
    if (floor_layer_saved_render_)
    {
        E_Screen->render = floor_layer_saved_render_;
        floor_layer_saved_render_ = nullptr;
    }
    if (!floor_layer_ || !E_Screen->render || scale <= 0.0f)
        return;

    SDL_Rect out;
    SDL_Rect src;
    if (pad_x > 0 || pad_y > 0)
    {
        // Padded below-floor composite (scale<1): the caller drew a
        // (w+2*pad_x) x (h+2*pad_y) world window at (x,y) on the layer;
        // squeeze that whole window down onto the FULL (x,y,w,h) viewport.
        // The old centred-shrink dst left a black ring around the composite
        // — with the expanded source window that ring is real drawn content.
        out.x = x;
        out.y = y;
        out.w = w;
        out.h = h;
        src.x = x;
        src.y = y;
        src.w = w + 2 * pad_x;
        src.h = h + 2 * pad_y;
        if (out.w <= 0 || out.h <= 0)
            return;
    }
    else
    {
    // Centred scale: source pixel p maps to dst = centre + (p - centre)*scale.
    // Clip the visible output to the viewport so a zoomed (scale>1) upper floor
    // cannot bleed into adjacent split-screen panes and the sampled source stays
    // within the layer bounds.
    const float fcx = static_cast<float>(cx);
    const float fcy = static_cast<float>(cy);
    const float fdx = fcx + (static_cast<float>(x) - fcx) * scale;
    const float fdy = fcy + (static_cast<float>(y) - fcy) * scale;
    const float fdw = static_cast<float>(w) * scale;
    const float fdh = static_cast<float>(h) * scale;

    const float ox0 = std::max(static_cast<float>(x), fdx);
    const float oy0 = std::max(static_cast<float>(y), fdy);
    const float ox1 = std::min(static_cast<float>(x + w), fdx + fdw);
    const float oy1 = std::min(static_cast<float>(y + h), fdy + fdh);
    if (ox1 <= ox0 || oy1 <= oy0)
        return;

    out.x = static_cast<int>(std::lround(ox0));
    out.y = static_cast<int>(std::lround(oy0));
    out.w = static_cast<int>(std::lround(ox1 - ox0));
    out.h = static_cast<int>(std::lround(oy1 - oy0));
    if (out.w <= 0 || out.h <= 0)
        return;

    // Inverse-map the (viewport-clipped) output rect back to the source region.
    const float inv = 1.0f / scale;
    src.x = static_cast<int>(std::lround(fcx + (static_cast<float>(out.x) - fcx) * inv));
    src.y = static_cast<int>(std::lround(fcy + (static_cast<float>(out.y) - fcy) * inv));
    src.w = static_cast<int>(std::lround(static_cast<float>(out.w) * inv));
    src.h = static_cast<int>(std::lround(static_cast<float>(out.h) * inv));
    }
    // Clamp the source to the layer bounds (defensive against rounding).
    if (src.x < 0) { src.w += src.x; src.x = 0; }
    if (src.y < 0) { src.h += src.y; src.y = 0; }
    if (src.x + src.w > floor_layer_->w) src.w = floor_layer_->w - src.x;
    if (src.y + src.h > floor_layer_->h) src.h = floor_layer_->h - src.y;
    if (src.w <= 0 || src.h <= 0)
        return;

    // Smooth path: bilinear-stretch the selected source rect into a LOCAL
    // output-sized scratch, then alpha-blend that scratch over the real render
    // surface. The padded source can be almost 2x the viewport in each axis;
    // mirroring those dimensions in this second surface used to double the
    // compositor's peak/retained memory for pixels that can never be output.
    SDL_Rect scaled_out{0, 0, out.w, out.h};
    if (floor_layer_scaled_ && (floor_layer_scaled_->w < scaled_out.w ||
                                floor_layer_scaled_->h < scaled_out.h))
    {
        // Grow to the largest actual output encountered; never to the padded
        // source extent. Reuse avoids per-floor allocation churn in split view.
        SDL_DestroySurface(floor_layer_scaled_);
        floor_layer_scaled_ = nullptr;
    }
    if (!floor_layer_scaled_)
    {
        floor_layer_scaled_ = SDL_CreateSurface(
            scaled_out.w, scaled_out.h, SDL_PIXELFORMAT_ARGB8888);
    }
    bool smooth_ok = false;
    if (floor_layer_scaled_)
    {
        SDL_FillSurfaceRect(floor_layer_scaled_, &scaled_out, 0x00000000u);
        smooth_ok = SDL_StretchSurface(floor_layer_, &src,
                                       floor_layer_scaled_, &scaled_out,
                                       SDL_SCALEMODE_LINEAR);
    }
    SDL_Surface* const composited = smooth_ok ? floor_layer_scaled_ : floor_layer_;
    // Depth-effect treatment (cfg effects/depth_fx): mutate every drawn
    // (coverage alpha > 0) layer pixel BEFORE compositing, on the already-
    // scaled surface — so tiles, decor and entities of the below floor are
    // treated together, screen-space patterns (the mist dither, the fog
    // noise) stay period-correct after the parallax scale, and un-drawn air
    // holes stay untouched. Blending toward a reference color (rather than a
    // multiplicative mod) shifts hue on anything — pure-green grass visibly
    // cools/pales. The layer is repainted from scratch every floor pass, so
    // these mutations cannot leak into later composites. Mode Off touches
    // nothing: bit-identical to the plain faded composite.
    if (fx.mode != DepthFxMode::Off && fx.stories > 0)
    {
        // Legacy cold blue-grey (Tint) — strengths 52/96 per depth, pinned
        // byte-identical to the retired boolean effects/depth_tint.
        constexpr Uint8 kTintR = 58, kTintG = 74, kTintB = 140;
        const int tint_t = fx.stories >= 2 ? 96 : 52;
        // Pale steel (Haze / the Mist dither color / Fog's base wash):
        // aerial perspective — contrast lifts toward it, ~30% per story
        // (tuned up from the spec's 20% starting point: the unconditional
        // depth fade composites the treated layer down over black, which
        // eats a weaker wash).
        constexpr Uint8 kHazeR = 150, kHazeG = 160, kHazeB = 175;
        const int haze_t = fx.stories >= 3 ? 210 : fx.stories * 77;
        // Fog patch color: a lighter fog-white so the drifting banks read
        // over the haze wash beneath them.
        constexpr Uint8 kFogR = 204, kFogG = 211, kFogB = 222;
        // Hoisted out of the hot loop: SDL3 replaced surface->format (struct)
        // with a lookup by SDL_PixelFormat enum.
        const SDL_PixelFormatDetails* det =
            SDL_GetPixelFormatDetails(composited->format);
        SDL_LockSurface(composited);
        const SDL_Rect& region = smooth_ok ? scaled_out : src;
        for (int py = region.y; py < region.y + region.h; py++)
        {
            Uint32* row = reinterpret_cast<Uint32*>(
                static_cast<Uint8*>(composited->pixels) +
                py * composited->pitch);
            for (int px = region.x; px < region.x + region.w; px++)
            {
                // The scratch is output-local; depth patterns remain pinned
                // to screen space exactly as before the memory optimization.
                const int effect_x = smooth_ok ? out.x + px : px;
                const int effect_y = smooth_ok ? out.y + py : py;
                const Uint32 c = row[px];
                Uint8 pr, pg, pb, pa;
                SDL_GetRGBA(c, det, nullptr, &pr, &pg, &pb, &pa);
                if (pa == 0)
                    continue;
                switch (fx.mode)
                {
                case DepthFxMode::Tint:
                    pr = static_cast<Uint8>(pr + ((kTintR - pr) * tint_t) / 255);
                    pg = static_cast<Uint8>(pg + ((kTintG - pg) * tint_t) / 255);
                    pb = static_cast<Uint8>(pb + ((kTintB - pb) * tint_t) / 255);
                    break;
                case DepthFxMode::Haze:
                    pr = static_cast<Uint8>(pr + ((kHazeR - pr) * haze_t) / 255);
                    pg = static_cast<Uint8>(pg + ((kHazeG - pg) * haze_t) / 255);
                    pb = static_cast<Uint8>(pb + ((kHazeB - pb) * haze_t) / 255);
                    break;
                case DepthFxMode::Mist:
                {
                    // Hash stipple, NO alpha blending: every output pixel is
                    // either the original or exactly the mist (haze) color —
                    // zero requantization. An ordered (px+py) lattice reads
                    // as diagonal stripes at these densities (user report),
                    // so the mask is a cheap integer hash of the screen cell:
                    // even random-looking grain, fully deterministic, static
                    // across frames (mist ignores the tick; Fog is the
                    // animated mode). Density: 1 story ~25%, 2+ ~50%.
                    Uint32 m = (static_cast<Uint32>(effect_x) * 0x9E3779B1u) ^
                               (static_cast<Uint32>(effect_y) * 0x85EBCA77u);
                    m ^= m >> 15;
                    m *= 0x2C1B3C6Du;
                    m ^= m >> 12;
                    if ((m & 3u) < (fx.stories >= 2 ? 2u : 1u))
                    {
                        pr = kHazeR;
                        pg = kHazeG;
                        pb = kHazeB;
                    }
                    break;
                }
                case DepthFxMode::Fog:
                {
                    // Haze wash + drifting fog patches from the dedicated
                    // fixed-seed noise field (screen-space: fog hangs
                    // between the camera and the floor, so it must not ride
                    // the parallax-sliding floor beneath it).
                    pr = static_cast<Uint8>(pr + ((kHazeR - pr) * haze_t) / 255);
                    pg = static_cast<Uint8>(pg + ((kHazeG - pg) * haze_t) / 255);
                    pb = static_cast<Uint8>(pb + ((kHazeB - pb) * haze_t) / 255);
                    const int a =
                        depth_fog_alpha_at(effect_x, effect_y,
                                           fx.frame, fx.stories);
                    if (a > 0)
                    {
                        pr = static_cast<Uint8>(pr + ((kFogR - pr) * a) / 255);
                        pg = static_cast<Uint8>(pg + ((kFogG - pg) * a) / 255);
                        pb = static_cast<Uint8>(pb + ((kFogB - pb) * a) / 255);
                    }
                    break;
                }
                case DepthFxMode::Off:
                    break; // unreachable: gated above
                }
                row[px] = SDL_MapRGBA(det, nullptr, pr, pg, pb, pa);
            }
        }
        SDL_UnlockSurface(composited);
    }
    SDL_SetSurfaceAlphaMod(composited, alpha);
    SDL_SetSurfaceBlendMode(composited, SDL_BLENDMODE_BLEND);
    if (smooth_ok)
    {
        SDL_Rect dstpos = out;
        SDL_BlitSurface(composited, &scaled_out, E_Screen->render, &dstpos);
    }
    else
    {
        // Fallback (SDL_StretchSurface failed): nearest-neighbour scaled
        // alpha blit straight from the layer. Still seam-free (one bitmap).
        SDL_Rect dst = out;
        SDL_BlitSurfaceScaled(composited, &src, E_Screen->render, &dst,
                              SDL_SCALEMODE_NEAREST);
    }
    SDL_SetSurfaceAlphaMod(composited, 255);
}

void sdl_video::fail_next_floor_layer_allocation_for_testing()
{
    // Tests call this between passes. Restore defensively so an assertion or
    // early return can never leave public drawing aliases on freed storage.
    if (floor_layer_saved_render_ && E_Screen)
        E_Screen->render = floor_layer_saved_render_;
    floor_layer_saved_render_ = nullptr;
    SDL_DestroySurface(floor_layer_scaled_);
    SDL_DestroySurface(floor_layer_);
    floor_layer_scaled_ = nullptr;
    floor_layer_ = nullptr;
#ifdef TESTING
    fail_next_floor_layer_allocation_ = true;
#endif
}

int sdl_video::floor_layer_fallback_count_for_testing() const
{
#ifdef TESTING
    return floor_layer_fallback_count_;
#else
    return 0;
#endif
}

std::int64_t sdl_video::floor_layer_source_pixels_for_testing() const
{
    return floor_layer_ ? static_cast<Sint64>(floor_layer_->w) * floor_layer_->h
                        : 0;
}

std::int64_t sdl_video::floor_layer_scaled_pixels_for_testing() const
{
    return floor_layer_scaled_
        ? static_cast<Sint64>(floor_layer_scaled_->w) * floor_layer_scaled_->h
        : 0;
}

bool sdl_video::floor_layer_redirect_active_for_testing() const
{
    return floor_layer_saved_render_ != nullptr;
}


// walkputbuffer draws active guys to the screen (basically all non-tiles
// c-only since it isn't used that often (despite what you might think)
// walkerstartx,walkerstarty are the screen position we will try to draw to
// walkerwidth,walkerheight define the object's size
// portstartx,portstarty,portendx,portendy define a clipping rectangle
// sourceptr holds the walker data
// teamcolor is used for recoloring the guys to the appropriate team
void sdl_video::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr, unsigned char teamcolor)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	Sint32 walkoff=0,walkshift=0;
	Sint32 totrows,rowsize;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return; //walker is below or to the right of the viewport

	if (walkerstartx < portstartx) //clip the left edge of the view
	{
		xmin = portstartx-walkerstartx;  //start drawing walker at xmin
		walkerstartx = portstartx;
	}

	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
		xmax = portendx - walkerstartx; //stop drawing walker at xmax

	if (walkerstarty < portstarty) // clip the top edge
	{
		ymin = portstarty-walkerstarty; //start drawing walker at ymin
		walkerstarty = portstarty;
	}

	else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
		ymax = portendy - walkerstarty; //stop drawing walker at ymax

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//note!! the clipper makes the assumption that no object is larger than
	// the view it will be clipped to in either dimension!!!

	walkshift = walkerwidth - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;

	SDL_Surface* const target = E_Screen->render;
	if (cached_format_details(target->format)->bytes_per_pixel == 4 &&
	    walkerstartx >= 0 && walkerstarty >= 0 &&
	    walkerstartx + rowsize <= target->w &&
	    walkerstarty + totrows <= target->h)
	{
		// Normal gameplay canvases are 32bpp. Resolve the palette once for the
		// whole sprite and write its non-transparent pixels straight into each
		// destination row. The old pointb path repeated palette lookup, format
		// mapping, bounds checks and a bytes-per-pixel switch for every pixel;
		// zoomed-out views make enough additional actors/decor visible for that
		// dispatch overhead to become measurable. Keep the generic path below
		// for unusual formats and malformed/out-of-surface clipping ports.
		const Uint32* const lut = palette_color_lut(target);
		for (cury = 0; cury < totrows; ++cury)
		{
			Uint32* const row = reinterpret_cast<Uint32*>(
				static_cast<Uint8*>(target->pixels) +
				static_cast<std::size_t>(walkerstarty + cury) *
					static_cast<std::size_t>(target->pitch)) + walkerstartx;
			for (curx = 0; curx < rowsize; ++curx)
			{
				curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
				if (!curcolor)
					continue;
				if (curcolor > static_cast<unsigned char>(247))
					curcolor = static_cast<unsigned char>(
						teamcolor + (255 - curcolor));
				row[curx] = lut[curcolor];
			}
			walkoff += walkshift;
		}
		return;
	}

	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
			if (!curcolor)
				continue;
			if (curcolor > static_cast<unsigned char>(247))
				curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
			//buffers: PORT: videobuffer[buffoff++] = curcolor;
			pointb(walkerstartx+curx,walkerstarty+cury,curcolor);
		}
		walkoff += walkshift;
	}
}

// Full-color team-recolored blit with a global alpha (faded/ghosted floors).
// Mirrors the simple walkputbuffer clip/recolor loop but blends each pixel via
// the alpha pointb instead of an opaque write.
void sdl_video::walkputbuffer_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr,
                          unsigned char teamcolor, Uint8 alpha)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	Sint32 walkoff=0,walkshift=0;
	Sint32 totrows,rowsize;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return;
	if (walkerstartx < portstartx)
	{
		xmin = portstartx-walkerstartx;
		walkerstartx = portstartx;
	}
	else if (walkerstartx + walkerwidth > portendx)
		xmax = portendx - walkerstartx;
	if (walkerstarty < portstarty)
	{
		ymin = portstarty-walkerstarty;
		walkerstarty = portstarty;
	}
	else if (walkerstarty + walkerheight > portendy)
		ymax = portendy - walkerstarty;

	totrows = (ymax-ymin);
	rowsize = (xmax-xmin);
	if (totrows <= 0 || rowsize <= 0)
		return;

	walkshift = walkerwidth - rowsize;
	walkoff   = (ymin * walkerwidth) + xmin;

	SDL_Surface* const target = E_Screen->render;
	if (walkerstartx >= 0 && walkerstarty >= 0 &&
	    walkerstartx + rowsize <= target->w &&
	    walkerstarty + totrows <= target->h)
	{
		// Preserve blend_pixel's format-specific and transparent-HUD handling,
		// but avoid converting the same palette entries again for every sprite
		// pixel. Clipping above guarantees blend_pixel receives in-bounds points.
		const Uint32* const lut = palette_color_lut(target);
		for (cury = 0; cury < totrows; ++cury)
		{
			for (curx = 0; curx < rowsize; ++curx)
			{
				curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
				if (!curcolor)
					continue;
				if (curcolor > static_cast<unsigned char>(247))
					curcolor = static_cast<unsigned char>(
						teamcolor + (255 - curcolor));
				blend_pixel(target, walkerstartx + curx,
				            walkerstarty + cury, lut[curcolor], alpha);
			}
			walkoff += walkshift;
		}
		return;
	}

	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
			if (!curcolor)
				continue;
			if (curcolor > static_cast<unsigned char>(247))
				curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
			pointb(walkerstartx+curx,walkerstarty+cury,curcolor,alpha);
		}
		walkoff += walkshift;
	}
}

// Ground-shadow blit: a vertically squashed black silhouette, bottom row one
// pixel below the sprite's feet. Iterates TARGET rows (each samples every
// height_divisor'th source row bottom-up) so each destination pixel blends
// exactly once despite several source rows collapsing onto one. The classic
// unit shadow uses height_divisor 2 (half height) and inset 0 — for those
// arguments this is byte-identical to the pre-parameterized blit; the
// upper-floor blob shadows squash harder (3-4) and trim `inset` columns off
// each side so distance reads as a smaller, flatter blob.
void sdl_video::walkputbuffer_shadow(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr, Uint8 alpha,
                          Sint32 height_divisor, Sint32 inset)
{
	Sint32 curx, t;
	Sint32 xmin = 0, xmax = walkerwidth;
	if (height_divisor < 1)
		height_divisor = 1;
	Sint32 shadowrows = (walkerheight + height_divisor - 1) / height_divisor;

	if (walkerstartx >= portendx || walkerstartx + walkerwidth <= portstartx)
		return;
	if (walkerstartx < portstartx) //clip the left edge of the view
		xmin = portstartx - walkerstartx;
	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
		xmax = portendx - walkerstartx;
	if (inset > 0) // trim columns off both sides (smaller blob)
	{
		if (xmin < inset)
			xmin = inset;
		if (xmax > walkerwidth - inset)
			xmax = walkerwidth - inset;
	}
	if (xmax <= xmin || walkerheight <= 0)
		return;

	for (t = 0; t < shadowrows; t++)
	{
		// t=0 is the feet row, landing one pixel below the sprite's bottom.
		Sint32 desty = walkerstarty + walkerheight - t;
		if (desty < portstarty || desty >= portendy)
			continue;
		Sint32 walkoff = (walkerheight-1 - t*height_divisor) * walkerwidth;
		for (curx = xmin; curx < xmax; curx++)
		{
			if (!sourceptr[static_cast<std::size_t>(walkoff + curx)])
				continue;
			pointb(walkerstartx+curx, desty, PURE_BLACK, alpha);
		}
	}
}

// Reflection blit: the sprite vertically flipped (top-left at walkerstartx,
// walkerstarty), team-recolored and alpha-blended, but a pixel is plotted
// only where the underlying grid tile's id is marked in reflect_mask (glass
// + pure water in production, per reflective_tiles()). world_offset_x/y
// convert screen px to world px (topx - xloc, topy - yloc) for the grid
// lookup.
void sdl_video::walkputbuffer_reflect(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr,
                          unsigned char teamcolor, Uint8 alpha,
                          std::span<const unsigned char> grid,
                          Sint32 gridw, Sint32 gridh,
                          Sint32 world_offset_x, Sint32 world_offset_y,
                          std::span<const bool, 256> reflect_mask)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	Sint32 totrows,rowsize;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return;
	if (walkerstartx < portstartx)
	{
		xmin = portstartx-walkerstartx;
		walkerstartx = portstartx;
	}
	else if (walkerstartx + walkerwidth > portendx)
		xmax = portendx - walkerstartx;
	if (walkerstarty < portstarty)
	{
		ymin = portstarty-walkerstarty;
		walkerstarty = portstarty;
	}
	else if (walkerstarty + walkerheight > portendy)
		ymax = portendy - walkerstarty;

	totrows = (ymax-ymin);
	rowsize = (xmax-xmin);
	if (totrows <= 0 || rowsize <= 0)
		return;

	for(cury = 0; cury < totrows;cury++)
	{
		// Vertical flip: the first target row samples the sprite's LAST row.
		Sint32 walkoff = (walkerheight-1 - (ymin+cury)) * walkerwidth + xmin;
		Sint32 desty = walkerstarty + cury;
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[static_cast<std::size_t>(walkoff + curx)];
			if (!curcolor)
				continue;
			Sint32 destx = walkerstartx + curx;
			Sint32 gx = (destx + world_offset_x) / GRID_SIZE;
			Sint32 gy = (desty + world_offset_y) / GRID_SIZE;
			if (gx < 0 || gx >= gridw || gy < 0 || gy >= gridh)
				continue;
			if (!reflect_mask[grid[static_cast<std::size_t>(gx + gridw*gy)]])
				continue;
			if (curcolor > static_cast<unsigned char>(247))
				curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
			pointb(destx, desty, curcolor, alpha);
		}
	}
}

void sdl_video::walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr, unsigned char teamcolor)
{
	Sint32 curx, cury;
	unsigned char curcolor;
	Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	Sint32 walkoff=0,walkshift=0;
	Sint32 totrows,rowsize;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return; //walker is below or to the right of the viewport

	if (walkerstartx < portstartx) //clip the left edge of the view
	{
		xmin = portstartx-walkerstartx;  //start drawing walker at xmin
		walkerstartx = portstartx;
	}

	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
		xmax = portendx - walkerstartx; //stop drawing walker at xmax

	if (walkerstarty < portstarty) // clip the top edge
	{
		ymin = portstarty-walkerstarty; //start drawing walker at ymin
		walkerstarty = portstarty;
	}

	else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
		ymax = portendy - walkerstarty; //stop drawing walker at ymax

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//note!! the clipper makes the assumption that no object is larger than
	// the view it will be clipped to in either dimension!!!

	walkshift = walkerwidth - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;


	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
			if (!curcolor)
				continue;

			if (curcolor > static_cast<unsigned char>(247))
				curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
			
			int r,g,b;
            query_palette_reg(curcolor,&r,&g,&b);
            r *= 4;
            g *= 4;
            b *= 4;
            
            if(r > 155)
                r = 255;
            else
                r += 100;
            
            if(g > 155)
                g = 255;
            else
                g += 100;
            
            if(b > 155)
                b = 255;
            else
                b += 100;
            
            
			//buffers: PORT: videobuffer[buffoff++] = curcolor;
			pointb(walkerstartx+curx,walkerstarty+cury,r, g, b);
		}
		walkoff += walkshift;
	}
}

void sdl_video::walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr, unsigned char teamcolor)
{
        Sint32 curx, cury;
        unsigned char curcolor;
        Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
        Sint32 walkoff=0,walkshift=0;
        Sint32 totrows,rowsize;
	int color;
	SDL_Rect rect;

        if (walkerstartx >= portendx || walkerstarty >= portendy)
                return; //walker is below or to the right of the viewport

        if (walkerstartx < portstartx) //clip the left edge of the view
        {
                xmin = portstartx-walkerstartx;  //start drawing walker at xmin
                walkerstartx = portstartx;
        }
	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
                xmax = portendx - walkerstartx; //stop drawing walker at xmax

        if (walkerstarty < portstarty) // clip the top edge
        {
                ymin = portstarty-walkerstarty; //start drawing walker at ymin
                walkerstarty = portstarty;
        }

        else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
                ymax = portendy - walkerstarty; //stop drawing walker at ymax

        totrows = (ymax-ymin); //how many rows to copy
        rowsize = (xmax-xmin); //how many bytes to copy
        if (totrows <= 0 || rowsize <= 0)
                return; //this happens on bad args

        //note!! the clipper makes the assumption that no object is larger than
        // the view it will be clipped to in either dimension!!!

        walkshift = walkerwidth - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;

        for(cury = 0; cury < totrows;cury++)
        {
                for(curx=0;curx<rowsize;curx++)
                {
                        curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
                        if (!curcolor)
                                continue;
		        if (curcolor > static_cast<unsigned char>(247))
		        {
		                curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
		        }
                        color = static_cast<int>(palette_color_lut(E_Screen->render)[curcolor]);

                        rect.x = (curx + walkerstartx);
                        rect.y = (cury + walkerstarty);
                        rect.w = 1;
                        rect.h = 1;
                        SDL_FillSurfaceRect(E_Screen->render,&rect,static_cast<Uint32>(color));
                }
                walkoff += walkshift;
        }
}

void sdl_video::walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr, unsigned char teamcolor, Uint8 alpha)
{
        Sint32 curx, cury;
        unsigned char curcolor;
        Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
        Sint32 walkoff=0,walkshift=0;
        Sint32 totrows,rowsize;

        if (walkerstartx >= portendx || walkerstarty >= portendy)
                return; //walker is below or to the right of the viewport

        if (walkerstartx < portstartx) //clip the left edge of the view
        {
                xmin = portstartx-walkerstartx;  //start drawing walker at xmin
                walkerstartx = portstartx;
        }
	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
                xmax = portendx - walkerstartx; //stop drawing walker at xmax

        if (walkerstarty < portstarty) // clip the top edge
        {
                ymin = portstarty-walkerstarty; //start drawing walker at ymin
                walkerstarty = portstarty;
        }

        else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
                ymax = portendy - walkerstarty; //stop drawing walker at ymax

        totrows = (ymax-ymin); //how many rows to copy
        rowsize = (xmax-xmin); //how many bytes to copy
        if (totrows <= 0 || rowsize <= 0)
                return; //this happens on bad args

        //note!! the clipper makes the assumption that no object is larger than
        // the view it will be clipped to in either dimension!!!

        walkshift = walkerwidth - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;

        for(cury = 0; cury < totrows;cury++)
        {
                for(curx=0;curx<rowsize;curx++)
                {
                        curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
                        if (!curcolor)
                                continue;
                        if (curcolor > static_cast<unsigned char>(247))
                                curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
                        
                        pointb(curx + walkerstartx, cury + walkerstarty, teamcolor, alpha);
                }
                walkoff += walkshift;
        }
}


void sdl_video::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                          Sint32 walkerwidth, Sint32 walkerheight,
                          Sint32 portstartx, Sint32 portstarty,
                          Sint32 portendx, Sint32 portendy,
                          std::span<const unsigned char> sourceptr, unsigned char teamcolor,
                          unsigned char mode, Sint32 invisibility,
                          unsigned char outline, unsigned char shifttype)
{
	Sint32 curx, cury;
	unsigned char curcolor, bufcolor;
	Sint32 xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	Sint32 walkoff=0,buffoff=0,walkshift=0,buffshift=0;
	Sint32 totrows,rowsize;
	signed char shift;
	int yval, xval;
	Uint8 r,g,b;
	int tx,ty,tempbuf;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return; //walker is below or to the right of the viewport

	if (walkerstartx < portstartx) //clip the left edge of the view
	{
		xmin = portstartx-walkerstartx;  //start drawing walker at xmin
		walkerstartx = portstartx;
	}

	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
		xmax = portendx - walkerstartx; //stop drawing walker at xmax

	if (walkerstarty < portstarty) // clip the top edge
	{
		ymin = portstarty-walkerstarty; //start drawing walker at ymin
		walkerstarty = portstarty;
	}

	else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
		ymax = portendy - walkerstarty; //stop drawing walker at ymax

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//note!! the clipper makes the assumption that no object is larger than
	// the view it will be clipped to in either dimension!!!

	walkshift = walkerwidth - rowsize;
	buffshift = active_canvas_w() - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*active_canvas_w()) + walkerstartx;
	xval = walkerstartx;
	yval = walkerstarty;

	// Zardus: FIX: and now we simply replace all the videobuffer stuff with pointb.
	switch (mode)
	{
		case INVISIBLE_MODE:

			for(cury = 0; cury < totrows;cury++)
			{
				for(curx=0;curx<rowsize;curx++)
				{
					curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
					if (!curcolor)
					{
						if (outline)
						{
							if (curx>0)
							{
								if (sourceptr[static_cast<std::size_t>(walkoff-2)])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (curx<(rowsize-1))
							{
								if (sourceptr[static_cast<std::size_t>(walkoff)])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (cury>0)
							{
								if (sourceptr[static_cast<std::size_t>(walkoff-1-walkerwidth)])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (cury<(totrows-1))
							{
								if (sourceptr[static_cast<std::size_t>(walkoff-1+walkerwidth)])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}
						} // end of outline check

						xval++;
						continue;
					} //end of transparency check

					if (curcolor > static_cast<unsigned char>(247))
						curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));

					if (outline)
					{
						if (curx==0 || cury==0 || curx==(walkerwidth-1) || cury==(totrows-1))
						{
							pointb(xval++, yval, outline);
							continue;
						}
					} // end outline

					if (rng(static_cast<Uint32>(invisibility)) > 8)
					{
						xval++;
						//videobuffer[buffoff++] = teamcolor+rng(7);
						continue;
					}
					pointb(xval++, yval, curcolor);
				} //end of each row

				walkoff += walkshift;
				yval++;
				xval = walkerstartx;
			} // end of all rows

			break; // end INVISIBLE

		case OUTLINE_MODE:

			for(cury = 0; cury < totrows;cury++)
			{
				for(curx=0;curx<rowsize;curx++)
				{
					curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
					if (!curcolor)
					{
						if (curx>0)
						{
							if (sourceptr[static_cast<std::size_t>(walkoff-2)])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (curx<(rowsize-1))
						{
							if (sourceptr[static_cast<std::size_t>(walkoff)])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (cury>0)
						{
							if (sourceptr[static_cast<std::size_t>(walkoff-1-walkerwidth)])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (cury<(totrows-1))
						{
							if (sourceptr[static_cast<std::size_t>(walkoff-1+walkerwidth)])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						xval++;
						continue;
					} //end of transparency check

					if (curcolor > static_cast<unsigned char>(247))
						curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));

					if (curx==0 || cury==0 || curx==(walkerwidth-1) || cury==(totrows-1))
					{
						pointb(xval++, yval, outline);
						continue;
					}

					pointb(xval++, yval, curcolor);
				} //end of each row

				walkoff += walkshift;
				xval = walkerstartx;
				yval++;
			} // end of all rows

			break; // end OUTLINE

			//buffers: PORT: ported the below block of code
		case PHANTOM_MODE:
			switch (shifttype)
			{
				case SHIFT_LEFT:
					shift = -1;
					break;

				case SHIFT_RIGHT:
					shift = 1;
					break;

				case SHIFT_RIGHT_RANDOM:
					shift = static_cast<signed char>(rng(2));
					break;

				default:
					shift = 0;
					break;
			} //end switch (shifttype)

			for(cury = 0; cury < totrows;cury++)
			{
				for(curx=0;curx<rowsize;curx++)
				{
					curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
					if (!curcolor)
					{
						buffoff++;
						continue;
					}

					//buffers: this is a messy optimization. sorry.
					if (shifttype == SHIFT_RANDOM)
					{
						//pointb(buffoff++,get_pixel(buffoff+rng(2)));
						tempbuf = static_cast<int>(static_cast<Uint32>(buffoff)+rng(2));
						ty = tempbuf/active_canvas_w();
						tx = tempbuf-ty*active_canvas_w();
						get_pixel(tx,ty,&r,&g,&b);

						ty = buffoff/active_canvas_w();
						tx = buffoff-ty*active_canvas_w();
						;
						pointb(tx,ty,static_cast<int>(r),static_cast<int>(g),static_cast<int>(b));
						buffoff++;
					}

					else if (shifttype == SHIFT_LIGHTER)
					{
						//buffers: bufcolor = videobuffer[buffoff];
						bufcolor = static_cast<unsigned char>(get_pixel(buffoff));
						if ((bufcolor%8)!=0 && bufcolor !=0)
							bufcolor--;
						//buffers: videobuffer[buffoff++] = bufcolor;
						pointb(buffoff,bufcolor);
						buffoff++;
					}

					else if (shifttype == SHIFT_DARKER)
					{
						//buffers: bufcolor = videobuffer[buffoff];
						bufcolor = static_cast<unsigned char>(get_pixel(buffoff));
						if ((bufcolor%7)!=0 && bufcolor<255)
							bufcolor++;
						//videobuffer[buffoff++] = bufcolor;
						pointb(buffoff++,bufcolor);
					}

					else if (shifttype == SHIFT_BLOCKY)
					{
							if (cury%2) //buffers:videobuffer[buffoff++] = videobuffer[buffoff-VIDEO_BUFFER_WIDTH];
								pointb(buffoff, static_cast<unsigned char>(get_pixel(buffoff-active_canvas_w())));
							else if (curx%2) //videobuffer[buffoff++] = videobuffer[buffoff-1];
								pointb(buffoff, static_cast<unsigned char>(get_pixel(buffoff-2)));
                        buffoff++;

					}

					else
					{
						//buffers: videobuffer[buffoff++] = videobuffer[buffoff+shift];
							pointb(buffoff, static_cast<unsigned char>(get_pixel(buffoff+shift)));
						buffoff++;
					}
				} //end each row

				walkoff += walkshift;
				buffoff += buffshift;
			} //end all rows

			break; //end case PHANTOM

		default: // NORMAL walkputbuffer
			{
				for(cury = 0; cury < totrows;cury++)
				{
					for(curx=0;curx<rowsize;curx++)
					{
						curcolor = sourceptr[static_cast<std::size_t>(walkoff++)];
						if (!curcolor)
						{
							buffoff++;
							continue;
						}
						if (curcolor > static_cast<unsigned char>(247))
							curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
						pointb(buffoff++, curcolor);
					} //end each row

					walkoff += walkshift;
					buffoff += buffshift;
				} //end all rows

			} //end default

	} //end switch of mode
}

// sdl_video::buffer_to_screen
// copies all or a portion of the video buffer to the screen
// viewstartx,viewstarty,viewwidth,viewheight define a rectangle which
//     can be used to draw only a portion of the buffer to screen,
//     and is used to draw viewscreens when we don't need a full update
// NOTE!! this function requires that you pass it a rectangle which is
// a multiple of four WIDE, or it will NOT draw correctly
// This is designed this way with the assumption that screen draws are
// the slowest thing we can possible do.
void sdl_video::buffer_to_screen(Sint32 viewstartx,Sint32 viewstarty,
                             Sint32 viewwidth, Sint32 viewheight)
{
	E_Screen->swap(viewstartx,viewstarty,viewwidth,viewheight);
}

//buffers: like buffer_to_screen but automaticaly swaps the entire screen
void sdl_video::swap(void)
{
	buffer_to_screen(0,0,active_canvas_w(),active_canvas_h());
}

int sdl_video::canvas_w() const
{
	return active_canvas_w();
}

int sdl_video::canvas_h() const
{
	return active_canvas_h();
}

int sdl_video::world_canvas_w() const
{
	return E_Screen ? E_Screen->world_w() : kUiCanvasW;
}

int sdl_video::world_canvas_h() const
{
	return E_Screen ? E_Screen->world_h() : kUiCanvasH;
}

int sdl_video::gameplay_ui_canvas_w() const
{
	return E_Screen ? E_Screen->gameplay_ui_w() : kUiCanvasW;
}

int sdl_video::gameplay_ui_canvas_h() const
{
	return E_Screen ? E_Screen->gameplay_ui_h() : kUiCanvasH;
}

bool sdl_video::gameplay_ui_canvas_available() const
{
	return E_Screen ? E_Screen->gameplay_ui_canvas_available() : true;
}

void sdl_video::set_active_canvas(CanvasTarget target)
{
	if (E_Screen)
	{
		const bool floor_redirect_active = floor_layer_saved_render_ != nullptr;
		E_Screen->set_active_canvas(target);
		// Fixed damage numbers/mini-bars may briefly select GameplayUI while a
		// faded floor is being assembled. Restoring World must resume drawing
		// into that off-screen floor layer, not bypass it for the rest of the
		// entity list. floor_layer_end will restore floor_layer_saved_render_.
		if (floor_redirect_active && target == CanvasTarget::World && floor_layer_)
			E_Screen->render = floor_layer_;
	}
}

CanvasTarget sdl_video::active_canvas() const
{
	return E_Screen ? E_Screen->active_canvas() : CanvasTarget::UI;
}

CanvasTarget sdl_video::last_presented_canvas() const
{
	return E_Screen ? E_Screen->last_presented_canvas() : CanvasTarget::UI;
}

void sdl_video::begin_gameplay_frame()
{
	if (E_Screen)
		E_Screen->begin_gameplay_frame();
}

void sdl_video::prepare_ui_canvas_from_world()
{
	if (E_Screen)
		E_Screen->prepare_ui_canvas_from_world();
}

void sdl_video::set_world_canvas_pinned_classic(bool pinned)
{
	if (E_Screen)
		E_Screen->set_world_canvas_pinned_classic(pinned);
}

void sdl_video::reapply_world_scale()
{
	// DISPLAY zoom/smoothing, window resize, and RESTORE SETTINGS live-apply
	// seam. The legacy method name remains part of the video interface.
	apply_world_scale_from_cfg();
}

int sdl_video::minimum_world_zoom_steps() const
{
	return E_Screen ? E_Screen->minimum_world_zoom_steps()
	                : og::kZoomStepsMax;
}

bool sdl_video::world_smoothing_supported() const
{
	return E_Screen && E_Screen->world_smoothing_supported();
}

// The display every display-settings decision should target: the one the
// window actually sits on (the user may have dragged it to a secondary
// monitor); primary only as the fallback headless drivers need.
[[maybe_unused]] static SDL_DisplayID display_for_window()
{
	const SDL_DisplayID display = SDL_GetDisplayForWindow(E_Screen->window);
	return display != 0 ? display : SDL_GetPrimaryDisplay();
}

// SDL's X11/XRandR backend temporarily disables a CRTC while changing a real
// video mode. With several displays on the same X screen, shrinking the root
// can make another output fall outside its bounds; the target CRTC may then
// never be re-enabled. Preflight the topology because SDL reports success too
// early for an application rollback to be reliable (SDL issue #9560).
[[maybe_unused]] static bool current_topology_allows_exclusive_mode_switch()
{
	const char* driver = SDL_GetCurrentVideoDriver();
	if (driver == nullptr || std::string_view(driver) != "x11")
		return true;

	int count = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&count);
	if (displays == nullptr)
		return false;
	SDL_free(displays);
	return og::platform::exclusive_mode_switch_is_safe(driver, count);
}

// Attach the closest real video mode by PHYSICAL pixel size. SDL3's mode w/h
// fields are logical coordinates, so SDL_GetClosestFullscreenDisplayMode()
// cannot directly consume the values shown by this menu on Retina/HiDPI
// displays. Select one of SDL's owned mode pointers while the list is alive,
// then release the list after SDL has accepted it. Equal physical sizes favor
// the desktop's exact density/layout for the desktop request, otherwise a
// density nearest 1.0 and a refresh nearest the desktop.
[[maybe_unused]] static bool apply_exclusive_mode(SDL_DisplayID display, int w, int h)
{
	const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
	const std::pair<int, int> desktop_pixels = desktop != nullptr
		? og::platform::display_mode_pixel_size(*desktop)
		: std::pair<int, int>{0, 0};
	const bool requesting_desktop =
		std::pair<int, int>{w, h} == desktop_pixels;

	int count = 0;
	SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
	const SDL_DisplayMode* best = nullptr;
	using Rank = std::tuple<unsigned long long, int, double, double, int>;
	Rank best_rank{std::numeric_limits<unsigned long long>::max(), 1,
	               std::numeric_limits<double>::infinity(),
	               std::numeric_limits<double>::infinity(),
	               std::numeric_limits<int>::max()};
	for (int i = 0; modes != nullptr && i < count; ++i)
	{
		const SDL_DisplayMode& candidate = *modes[i];
		const auto pixels = og::platform::display_mode_pixel_size(candidate);
		if (pixels.first < w || pixels.second < h)
			continue;
		const auto dw = static_cast<unsigned long long>(pixels.first - w);
		const auto dh = static_cast<unsigned long long>(pixels.second - h);
		const unsigned long long error = dw * dw + dh * dh;
		const double candidate_density =
			std::isfinite(candidate.pixel_density) && candidate.pixel_density > 0.0f
				? static_cast<double>(candidate.pixel_density)
				: 1.0;
		const double desktop_density = desktop != nullptr &&
			std::isfinite(desktop->pixel_density) && desktop->pixel_density > 0.0f
				? static_cast<double>(desktop->pixel_density)
				: 1.0;
		const bool desktop_layout = requesting_desktop && desktop != nullptr &&
			candidate.w == desktop->w && candidate.h == desktop->h &&
			std::abs(candidate_density - desktop_density) < 0.0001;
		const double density_distance = std::abs(candidate_density - 1.0);
		const double refresh_distance = desktop != nullptr &&
			candidate.refresh_rate > 0.0f && desktop->refresh_rate > 0.0f
				? std::abs(static_cast<double>(candidate.refresh_rate -
			                                          desktop->refresh_rate))
				: 0.0;
		const Rank rank{error, desktop_layout ? 0 : 1, density_distance,
		                refresh_distance, i};
		if (rank < best_rank)
		{
			best = &candidate;
			best_rank = rank;
		}
	}

	const bool applied = best != nullptr &&
		SDL_SetWindowFullscreenMode(E_Screen->window, best);
	SDL_free(modes);
	return applied;
}

// Persist only the separately confirmed snapshot. SDL_GetWindowFlags() folds
// in pending flags and SDL_GetWindowFullscreenMode() eagerly copies a mode
// request, so neither getter is evidence of truth after SDL_SyncWindow times
// out. Exclusive dimensions are physical pixels; Windowed/Borderless values
// are logical coordinates.
void sdl_video::persist_confirmed_display_state()
{
	const og::platform::DisplayStateSnapshot& state = display_state_.confirmed();
	switch (state.mode)
	{
	case og::platform::DisplayStateMode::Windowed:
		cfg.apply_setting("graphics", "fullscreen", "off");
		cfg.apply_setting("graphics", "width", std::to_string(state.width));
		cfg.apply_setting("graphics", "height", std::to_string(state.height));
		cfg.apply_setting("graphics", "windowed_width",
		                  std::to_string(state.windowed_width));
		cfg.apply_setting("graphics", "windowed_height",
		                  std::to_string(state.windowed_height));
		break;
	case og::platform::DisplayStateMode::Borderless:
		cfg.apply_setting("graphics", "fullscreen", "borderless");
		cfg.apply_setting("graphics", "width", std::to_string(state.width));
		cfg.apply_setting("graphics", "height", std::to_string(state.height));
		cfg.apply_setting("graphics", "windowed_width",
		                  std::to_string(state.windowed_width));
		cfg.apply_setting("graphics", "windowed_height",
		                  std::to_string(state.windowed_height));
		break;
	case og::platform::DisplayStateMode::Exclusive:
		cfg.apply_setting("graphics", "fullscreen", "exclusive");
		cfg.apply_setting("graphics", "width", std::to_string(state.width));
		cfg.apply_setting("graphics", "height", std::to_string(state.height));
		cfg.apply_setting("graphics", "windowed_width",
		                  std::to_string(state.windowed_width));
		cfg.apply_setting("graphics", "windowed_height",
		                  std::to_string(state.windowed_height));
		break;
	}
}

og::platform::DisplayStateSnapshot sdl_video::confirmed_snapshot_from_window(
	DisplayStateConfirmation confirmation) const
{
	using og::platform::DisplayStateMode;
	using og::platform::DisplayStateSnapshot;
	const DisplayStateSnapshot previous = display_state_.confirmed();

	if (confirmation == DisplayStateConfirmation::LeaveFullscreen)
	{
		int w = previous.width;
		int h = previous.height;
		SDL_GetWindowSize(E_Screen->window, &w, &h);
		return {DisplayStateMode::Windowed, w, h, w, h};
	}

	if (confirmation == DisplayStateConfirmation::Resized &&
	    previous.mode == DisplayStateMode::Windowed)
	{
		int w = previous.width;
		int h = previous.height;
		SDL_GetWindowSize(E_Screen->window, &w, &h);
		return {DisplayStateMode::Windowed, w, h, w, h};
	}

	if (confirmation == DisplayStateConfirmation::PixelSizeChanged &&
	    previous.mode == DisplayStateMode::Windowed)
	{
		// Pixel-size events carry physical backing dimensions, not logical
		// window coordinates. Query the completed logical size instead; this
		// also settles a Windowed resize on backends that emit PIXEL only.
		int w = previous.width;
		int h = previous.height;
		SDL_GetWindowSize(E_Screen->window, &w, &h);
		return {DisplayStateMode::Windowed, w, h, w, h};
	}

	bool fullscreen_now = true;
	if (confirmation == DisplayStateConfirmation::Synchronized)
	{
		fullscreen_now =
			(SDL_GetWindowFlags(E_Screen->window) & SDL_WINDOW_FULLSCREEN) != 0;
	}
	else if (confirmation == DisplayStateConfirmation::EnterFullscreen)
	{
		fullscreen_now = true;
	}

	if (!fullscreen_now)
	{
		int w = previous.width;
		int h = previous.height;
		SDL_GetWindowSize(E_Screen->window, &w, &h);
		return {DisplayStateMode::Windowed, w, h, w, h};
	}

	const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode(E_Screen->window);
	if (mode != nullptr)
	{
		const auto [w, h] = og::platform::display_mode_pixel_size(*mode);
		return {DisplayStateMode::Exclusive, w, h,
		        remembered_window_w_, remembered_window_h_};
	}
	return {DisplayStateMode::Borderless,
	        remembered_window_w_, remembered_window_h_,
	        remembered_window_w_, remembered_window_h_};
}

[[maybe_unused]] static std::pair<int, int> update_runtime_window_metrics()
{
	int actual_w = 0;
	int actual_h = 0;
	if (E_Screen && E_Screen->window)
		SDL_GetWindowSize(E_Screen->window, &actual_w, &actual_h);
	if (og::runtime::current_session != nullptr)
	{
		og::runtime::current_session->window_w_ = static_cast<float>(actual_w);
		og::runtime::current_session->window_h_ = static_cast<float>(actual_h);
		update_overscan_setting();
	}
	return {actual_w, actual_h};
}

void sdl_video::reflect_display_settings_from_window(
	[[maybe_unused]] DisplayStateConfirmation confirmation, [[maybe_unused]] std::uint64_t event_timestamp_ns)
{
#ifndef __EMSCRIPTEN__
	if (!E_Screen || !E_Screen->window)
		return;

	const bool event_confirmation =
		confirmation != DisplayStateConfirmation::Synchronized;
	if (event_confirmation &&
	    !display_state_.event_is_current(event_timestamp_ns))
	{
		// A completion event queued before a serialized newer request cannot
		// validate SDL's getter for that newer request.
		persist_confirmed_display_state();
		update_runtime_window_metrics();
		return;
	}

	const og::platform::DisplayStateSnapshot previous =
		display_state_.confirmed();
	const bool had_pending_request = display_state_.request_pending();
	const og::platform::DisplayStateMode request_target = display_state_.target();
	og::platform::DisplayStateSnapshot observed =
		confirmed_snapshot_from_window(confirmation);
	bool completes_request = true;
	if (had_pending_request)
	{
		if (request_target == og::platform::DisplayStateMode::Windowed)
		{
			// RESIZED/PIXEL can confirm the intermediate Exclusive ->
			// Borderless mode clear, but only LEAVE or a successful Sync proves
			// the final Windowed state.
			completes_request =
				confirmation == DisplayStateConfirmation::Synchronized ||
				confirmation == DisplayStateConfirmation::LeaveFullscreen;
		}
		else if (previous.mode == og::platform::DisplayStateMode::Windowed)
		{
			// A resize can precede ENTER on some backends. It is a truthful
			// Windowed observation, but does not complete a fullscreen entry.
			completes_request =
				confirmation == DisplayStateConfirmation::Synchronized ||
				confirmation == DisplayStateConfirmation::EnterFullscreen;
		}
	}
	display_state_.confirm(observed, completes_request);

	if (observed.mode == og::platform::DisplayStateMode::Windowed)
	{
		remembered_window_w_ = observed.width;
		remembered_window_h_ = observed.height;
	}

	if (pending_windowed_w_ > 0 && pending_windowed_h_ > 0 &&
	    observed.mode == og::platform::DisplayStateMode::Windowed)
	{
		// LEAVE first confirms the restored logical size. Only then request
		// the user's Windowed target; a timeout preserves this just-confirmed
		// snapshot and keeps the target internal for a late RESIZED event.
		display_state_.confirm(observed, false);

		const std::pair<int, int> current{observed.width, observed.height};
		if (current != std::pair<int, int>{pending_windowed_w_, pending_windowed_h_})
		{
			const bool size_accepted = SDL_SetWindowSize(
				E_Screen->window, pending_windowed_w_, pending_windowed_h_);
			const SDL_DisplayID display = pending_windowed_display_ != 0
				? pending_windowed_display_
				: display_for_window();
			const int centered = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display));
			SDL_SetWindowPosition(E_Screen->window, centered, centered);
			if (size_accepted && !SDL_SyncWindow(E_Screen->window))
			{
				LogWarn("Timed out waiting for the requested Windowed size\n");
				persist_confirmed_display_state();
				update_runtime_window_metrics();
				return;
			}
			if (!size_accepted)
			{
				display_state_.cancel_request();
				pending_windowed_w_ = 0;
				pending_windowed_h_ = 0;
				pending_windowed_display_ = 0;
				persist_confirmed_display_state();
				update_runtime_window_metrics();
				return;
			}
		}
		pending_windowed_w_ = 0;
		pending_windowed_h_ = 0;
		pending_windowed_display_ = 0;
		observed = confirmed_snapshot_from_window(
			DisplayStateConfirmation::Synchronized);
		display_state_.confirm(observed);
		if (observed.mode == og::platform::DisplayStateMode::Windowed)
		{
			remembered_window_w_ = observed.width;
			remembered_window_h_ = observed.height;
		}
	}
	else if (had_pending_request && completes_request &&
	         request_target == og::platform::DisplayStateMode::Windowed)
	{
		// A successful synchronization settled on a non-Windowed state, so
		// the leave was denied. Discard its delayed logical resize.
		pending_windowed_w_ = 0;
		pending_windowed_h_ = 0;
		pending_windowed_display_ = 0;
	}

	persist_confirmed_display_state();
	update_runtime_window_metrics();
#endif
}

void sdl_video::apply_display_settings_from_cfg()
{
#ifndef __EMSCRIPTEN__
	// The DISPLAY screen's live-apply path (and RESTORE SETTINGS): read the
	// mode + resolution out of cfg, drive the real window, update the overscan
	// viewport, then reapply the aspect-relative world zoom/smoothing settings.
	// The completed resize event refreshes that zoom baseline as well.
	og::ui::DisplayMode mode =
	    og::ui::parse_display_mode(cfg.get_setting("graphics", "fullscreen"));
	const bool exclusive_downgraded =
		mode == og::ui::DisplayMode::Exclusive &&
		!current_topology_allows_exclusive_mode_switch();
	if (exclusive_downgraded)
	{
		// Borderless retains fullscreen presentation without asking XRandR to
		// change hardware modes. Normalize cfg before beginning the tracked
		// request so boot and live apply both describe the safe real state.
		LogWarn("Exclusive fullscreen is disabled on multi-display X11; "
		        "using Borderless to protect the display topology\n");
		mode = og::ui::DisplayMode::Borderless;
		cfg.apply_setting("graphics", "fullscreen", "borderless");
	}
	const std::pair<int, int> res = og::ui::parse_resolution(
	    cfg.get_setting("graphics", "width"), cfg.get_setting("graphics", "height"));
	const int w = std::clamp(res.first, 320, kMaxConfiguredDisplayDimension);
	const int h = std::clamp(res.second, 200, kMaxConfiguredDisplayDimension);
	using og::platform::DisplayStateMode;
	const DisplayStateMode requested_mode = mode == og::ui::DisplayMode::Windowed
		? DisplayStateMode::Windowed
		: mode == og::ui::DisplayMode::Borderless
			? DisplayStateMode::Borderless
			: DisplayStateMode::Exclusive;

	// Never supersede an asynchronous request whose completion events may
	// still be queued: SDL's eager getters cannot associate those events with
	// the newer request. Try one finite barrier; on another timeout, retain the
	// confirmed snapshot and let the eventual event complete the old request.
	if (display_state_.request_pending())
	{
		if (!SDL_SyncWindow(E_Screen->window))
		{
			LogWarn("Display request still pending; preserving confirmed state\n");
			persist_confirmed_display_state();
			update_runtime_window_metrics();
			apply_world_scale_from_cfg();
			return;
		}
		reflect_display_settings_from_window(
			DisplayStateConfirmation::Synchronized);
		if (display_state_.request_pending())
		{
			// The fullscreen transition settled, but its delayed Windowed size
			// hit a second finite timeout inside the reflection hook.
			apply_world_scale_from_cfg();
			return;
		}
	}

	// Keep Windowed restores and resize recentering on the monitor the user
	// selected by placing the window there. Plain SDL_WINDOWPOS_CENTERED means
	// the primary display and would make subsequent mode enumeration jump too.
	const SDL_DisplayID target_display = display_for_window();
	const og::platform::DisplayStateSnapshot previous =
		display_state_.confirmed();
	const bool was_fullscreen = previous.mode != DisplayStateMode::Windowed;
	const bool was_exclusive = previous.mode == DisplayStateMode::Exclusive;
	const std::pair<int, int> previous_exclusive_pixels = was_exclusive
		? std::pair<int, int>{previous.width, previous.height}
		: std::pair<int, int>{0, 0};
	remembered_window_w_ = previous.windowed_width;
	remembered_window_h_ = previous.windowed_height;
	if (mode != og::ui::DisplayMode::Windowed)
	{
		// A newer fullscreen request supersedes any delayed Windowed restore.
		pending_windowed_w_ = 0;
		pending_windowed_h_ = 0;
		pending_windowed_display_ = 0;
	}

	display_state_.begin_request(requested_mode, SDL_GetTicksNS());
	bool request_accepted = true;
	switch (mode)
	{
	case og::ui::DisplayMode::Windowed:
	{
		int target_w = w;
		int target_h = h;
		// A normal Exclusive -> Windowed selector step still carries the
		// exclusive physical mode in cfg. Restore the logical size captured
		// before entering fullscreen; an explicitly different cfg size (for
		// example RESTORE SETTINGS) remains authoritative.
		if (was_fullscreen && was_exclusive &&
		    std::pair<int, int>{w, h} == previous_exclusive_pixels)
		{
			target_w = remembered_window_w_;
			target_h = remembered_window_h_;
		}
		// SDL_SetWindowSize has no effect while the leave request is still
		// pending. reflect_display_settings_from_window() applies this target
		// only after SDL reports that the window is no longer fullscreen.
		pending_windowed_w_ = target_w;
		pending_windowed_h_ = target_h;
		pending_windowed_display_ = target_display;
		request_accepted = SDL_SetWindowFullscreenMode(E_Screen->window, nullptr);
		request_accepted = SDL_SetWindowFullscreen(E_Screen->window, false) &&
			request_accepted;
		break;
	}
	case og::ui::DisplayMode::Borderless:
		if (!exclusive_downgraded &&
		    (!was_exclusive ||
		     std::pair<int, int>{w, h} != previous_exclusive_pixels))
		{
			// Usually this equals the real Windowed size captured above. A
			// different value is an explicit cfg operation such as RESTORE
			// DEFAULTS, and must become the size restored after fullscreen.
			remembered_window_w_ = w;
			remembered_window_h_ = h;
		}
		// NULL fullscreen mode = borderless desktop (the SDL2
		// FULLSCREEN_DESKTOP behavior). The chosen resolution still sizes
		// the window whenever the user drops back to Windowed.
		request_accepted = SDL_SetWindowFullscreenMode(E_Screen->window, nullptr);
		request_accepted = request_accepted &&
			SDL_SetWindowFullscreen(E_Screen->window, true);
		break;
	case og::ui::DisplayMode::Exclusive:
		request_accepted = apply_exclusive_mode(target_display, w, h);
		request_accepted = request_accepted &&
			SDL_SetWindowFullscreen(E_Screen->window, true);
		break;
	}

	if (!request_accepted)
	{
		display_state_.cancel_request();
		remembered_window_w_ = previous.windowed_width;
		remembered_window_h_ = previous.windowed_height;
		pending_windowed_w_ = 0;
		pending_windowed_h_ = 0;
		pending_windowed_display_ = 0;
		persist_confirmed_display_state();
		update_runtime_window_metrics();
	}
	else if (!SDL_SyncWindow(E_Screen->window))
	{
		// No SDL state getter is trustworthy here: flags include pending flags
		// and the fullscreen-mode getter eagerly exposes the request. Preserve
		// the last completed snapshot until a documented event confirms it.
		LogWarn("Timed out waiting for the requested display state\n");
		persist_confirmed_display_state();
		update_runtime_window_metrics();
	}
	else
	{
		reflect_display_settings_from_window(
			DisplayStateConfirmation::Synchronized);
	}
#else
	// The browser page/CSS owns the canvas box, and the backing is pinned to
	// the logical size (see Screen()); there is no window mode to apply on web.
#endif
	// Zoom and smoothing are world-canvas settings on every target, including
	// Emscripten. Keep this outside the native window-management branch so
	// RESTORE SETTINGS live-applies them in the browser too.
	apply_world_scale_from_cfg();
}

std::pair<int, int> sdl_video::desktop_resolution()
{
#ifndef __EMSCRIPTEN__
	const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display_for_window());
	if (desktop != nullptr)
		return og::platform::display_mode_pixel_size(*desktop);
#endif
	return {0, 0};
}

std::pair<int, int> sdl_video::windowed_desktop_resolution()
{
#ifndef __EMSCRIPTEN__
	SDL_Rect usable{};
	if (SDL_GetDisplayUsableBounds(display_for_window(), &usable) &&
	    usable.w > 0 && usable.h > 0)
	{
		return {usable.w, usable.h};
	}
#endif
	return {0, 0};
}

std::vector<std::pair<int, int>> sdl_video::display_resolutions()
{
	std::vector<std::pair<int, int>> out;
#ifndef __EMSCRIPTEN__
	// Returning no modes makes the DISPLAY selector skip Exclusive entirely.
	// The apply path repeats this guard so a hand-edited/saved cfg is safe too.
	if (!current_topology_allows_exclusive_mode_switch())
		return out;
	// Exclusive choices are strictly modes SDL says this display accepts.
	// Borderless reports desktop_resolution() separately; injecting that size
	// here can make a non-enumerated desktop request hide valid lower modes.
	int count = 0;
	SDL_DisplayMode** modes =
	    SDL_GetFullscreenDisplayModes(display_for_window(), &count);
	if (modes != nullptr)
	{
		for (int i = 0; i < count; ++i)
		{
			// The menu names physical monitor pixels. Logical dimensions that
			// differ only by density can therefore expose distinct choices
			// (1920x1080@2 becomes 3840x2160, not another 1920x1080).
			const std::pair<int, int> wh =
				og::platform::display_mode_pixel_size(*modes[i]);
			if (wh.first < 640 || wh.second < 400)
				continue; // smaller than the classic 2x window: not useful
			if (std::find(out.begin(), out.end(), wh) == out.end())
				out.push_back(wh);
		}
		SDL_free(modes);
	}
	std::sort(out.begin(), out.end(), std::greater<>());
#endif
	return out;
}

//buffers: get pixel's RGB values if you have XY
void sdl_video::get_pixel(int x, int y, Uint8 *r, Uint8 *g, Uint8 *b)
{
	Uint32 col = 0;
	Uint8 q=0,w=0,e=0;

	//buffers: bound checking to prevent out-of-bounds reads (mirrors pointb)
	if (x < 0 || x >= E_Screen->render->w || y < 0 || y >= E_Screen->render->h)
	{
		*r = 0;
		*g = 0;
		*b = 0;
		return;
	}

	const SDL_PixelFormatDetails* det = cached_format_details(E_Screen->render->format);
	char *p = reinterpret_cast<char*>(E_Screen->render->pixels);
	p += E_Screen->render->pitch*y;
	p += det->bytes_per_pixel*x;

	memcpy(&col,p,det->bytes_per_pixel);

	SDL_GetRGB(col,det,SDL_GetSurfacePalette(E_Screen->render),&q,&w,&e);
	*r=q;
	*g=w;
	*b=e;
}

//buffers: get pixel index if you have XY.
int sdl_video::get_pixel(int x, int y, int *index)
{
	Uint8 r,g,b;
	int tr,tg,tb;
	int i;

	get_pixel(x,y,&r,&g,&b);
	r /= 4;
	g /= 4;
	b /= 4;

		for(i=0;i<256;i++)
		{
			query_palette_reg(static_cast<unsigned char>(i),&tr,&tg,&tb);
			if(r==tr && g==tg && b==tb)
			{
				*index = i;
				return i;
		}
	}

	Log("DEBUG: could not find color: {} {} {}\n", static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
	return 0;
}

//buffers: get pixel index if you have an buffer offset
int sdl_video::get_pixel(int offset)
{
	int x,y,t;

	//buffers: reject out-of-range offsets before converting (mirrors pointb bounds)
	if (offset < 0 || offset >= E_Screen->render->w * E_Screen->render->h)
		return 0;

	const int cw = active_canvas_w();
	y = offset/cw;
	x = offset-y*cw;

	return get_pixel(x,y,&t);
}

#ifndef USE_BMP_SCREENSHOT
#include "../util/savepng.h"
#endif

bool sdl_video::save_screenshot()
{
	SDL_Surface* surf = E_Screen->render;
	SDL_Surface* composed_frame = nullptr;
	// Match Screen::swap(): smoothing is selected per active canvas. Display
	// creation keeps the legacy Engine slot on nearest while the WORLD canvas
	// carries its live SAI/Eagle choice in world_engine(). The scaler scratch
	// contains the last presented filtered frame; if it has not been produced
	// for the current canvas size yet, safely fall back to the logical canvas.
	const RenderEngine screenshot_engine =
	    (E_Screen->active_canvas() == CanvasTarget::World &&
	     E_Screen->world_scale().mode != og::WorldScaleMode::Legacy)
	        ? E_Screen->world_engine()
	        : E_Screen->Engine;
	switch(screenshot_engine)
	{
		case RenderEngine::SAI:
		case RenderEngine::Eagle:
			if (E_Screen->last_world_present_used_smart_surface() &&
			    E_Screen->render2 != nullptr &&
			    E_Screen->render2->w == E_Screen->render->w * 2 &&
			    E_Screen->render2->h == E_Screen->render->h * 2)
				surf = E_Screen->render2;
		    break;
		default:
			break;
	}

	// A scaled gameplay frame can be presented as two textures: zoomed/filtered
	// scenery followed by the nearest gameplay-UI overlay. Reproduce that
	// composition in the saved image instead of silently omitting HUD/radar/
	// messages. Scaling the overlay to render2's dimensions is explicitly
	// nearest, matching Screen::swap's texture scale mode.
	if (E_Screen->active_canvas() == CanvasTarget::World)
	{
		SDL_Surface* const overlay = E_Screen->gameplay_ui_overlay_surface();
		if (overlay != nullptr)
		{
			composed_frame = E_Screen->compose_gameplay_ui_for_capture(surf);
			if (composed_frame == nullptr)
			{
				return false;
			}
			surf = composed_frame;
		}
	}
	
	static int i = 1;
    #ifndef USE_BMP_SCREENSHOT
	std::string buf = std::format("screenshot{}.png", i);
	#else
	std::string buf = std::format("screenshot{}.bmp", i);
	#endif
	i++;

	SDL_IOStream* rwops = open_write_file(buf.c_str());
	if(rwops == nullptr)
    {
        LogError("Failed to open file for screenshot: {}\n", buf);
		SDL_DestroySurface(composed_frame);
        return false;
    }
    
    Log("Saving screenshot: {}\n", buf);
    
    #ifndef USE_BMP_SCREENSHOT
    // Make it safe to save (convert alpha channel)
    SDL_Surface* const png_surface = SDL_PNGFormatAlpha(surf);
	SDL_DestroySurface(composed_frame);
	if (png_surface == nullptr)
	{
		LogError("Failed to convert screenshot pixels: {}\n", SDL_GetError());
		SDL_CloseIO(rwops);
		return false;
	}
    
    // Save it
    bool result = (SDL_SavePNG_RW(png_surface, rwops, 1) >= 0);
    SDL_DestroySurface(png_surface);
    #else
    bool result = SDL_SaveBMP_IO(surf, rwops, true);
	SDL_DestroySurface(composed_frame);

    #endif
    
    return result;
}


// ***************************************************************************
// Fading routines! Thanks, Erik!
// ****************************************************************************
void sdl_video::FadeBetween24(
//Show transition between two screens at 'amount' between them.
//
//'pSurface' is the surface you want to apply the fade to,
//'fadeFrom' is a copy of what the old screen looks like, and
//'fadeTo' is a copy of what the normal screen looks like,
// neither faded in or out, but just normal.
//NOTE: fadeFrom, fadeTo, and pSurface must be the same size and dimensions.
//
//Params:
	SDL_Surface* pSurface, const Uint8* fadeFromRGB, const Uint8* fadeToRGB,
	const int amount)	//(in) mixing ratio (in increments of 'fadeDuration')
{
	Uint8 *pw = static_cast<Uint8*>(pSurface->pixels);
	Uint32 size = static_cast<Uint32>(pSurface->pitch * pSurface->h);

	const int nOldAmt = fadeDuration-amount;

	const Uint8 *pFrom = fadeFromRGB;
	const Uint8 *pTo = fadeToRGB;
	
	//Mix pixels in "from" and "to" images by 'amount'
	Uint8 *pStop = pw + size;
	while (pw != pStop)
	{
		*(pw++) = static_cast<Uint8>((nOldAmt * *(pFrom++) + amount * *(pTo++)) / fadeDuration);
		*(pw++) = static_cast<Uint8>((nOldAmt * *(pFrom++) + amount * *(pTo++)) / fadeDuration);
		*(pw++) = static_cast<Uint8>((nOldAmt * *(pFrom++) + amount * *(pTo++)) / fadeDuration);
		pw++; pFrom++; pTo++;
	}
    
	// FIXME!  Need to pass in the Screen structure.
	//SDL_UpdateRect (pSurface, 0, 0, 0, 0);
}

//*****************************************************************************
int sdl_video::FadeBetween(
//Fade between two screens.
//Time effect to be independent of machine speed.
	SDL_Surface* pOldSurface,	//(in)	Surface that contains starting image.
	SDL_Surface* pNewSurface,	//(in)	Image that destination surface will change to.
	SDL_Surface* DestSurface)	//	surface which is the destination
{
	// A fade owns every World swap until it completes. Drop any prepared or
	// just-presented gameplay overlay so stale full-bright HUD pixels cannot
	// be replayed over the interpolated scenery or the terminal black frame.
	if (E_Screen)
		E_Screen->discard_gameplay_ui_frame();
	bool bOldNull = false, bNewNull = false;
	int i = 1;

	//Set nullptr pointers to temporary black screens
	//(for simple fade-in/out effects).
	if (!pOldSurface && !pNewSurface)
		return 0; //nothing to do; avoid allocating two unused temporaries
	if (!pOldSurface)
	{
		bOldNull = true;
		pOldSurface = SDL_CreateSurface(
			active_canvas_w(), active_canvas_h(), SDL_PIXELFORMAT_RGB24);
		if (!pOldSurface) return 0;  // OOM: nothing safely lockable below
		SDL_FillSurfaceRect(pOldSurface,nullptr,0);
	}
	if (!pNewSurface)
	{
		bNewNull = true;
		pNewSurface = SDL_CreateSurface(
			active_canvas_w(), active_canvas_h(), SDL_PIXELFORMAT_RGB24);
		if (!pNewSurface) { if (bOldNull) SDL_DestroySurface(pOldSurface); return 0; }  // OOM: free the temp we just made
		SDL_FillSurfaceRect(pNewSurface,nullptr,0);
	}
	/* Lock the screen for direct access to the pixels */
    bool old_locked = false;
	if ( SDL_MUSTLOCK(pOldSurface) ) {
		if ( !SDL_LockSurface(pOldSurface) ) {
			return 0;
		}
        old_locked = true;
	}

    auto fail = [&](const char* reason) -> int
    {
        LogError("FadeBetween precondition failed: {}\n", reason);
        if(old_locked)
            SDL_UnlockSurface(pOldSurface);
        if(bOldNull)
            SDL_DestroySurface(pOldSurface);
        if(bNewNull)
            SDL_DestroySurface(pNewSurface);
        return 0;
    };
	
	//The new surface shouldn't need a lock unless it is somehow a screen surface.
	if(SDL_MUSTLOCK(pNewSurface))
        return fail("pNewSurface requires lock");
	// FadeBetween24 writes directly into the destination's pixel storage.
	// Reject surfaces that require locking instead of writing through an
	// unlocked RLE buffer.
	if(!DestSurface)
        return fail("dest size mismatch");
	if(SDL_MUSTLOCK(DestSurface))
        return fail("DestSurface requires lock");

	//The dimensions and format of the old and new surface must match exactly.
	if(pOldSurface->pitch != pNewSurface->pitch)
        return fail("pitch mismatch");
	if(pOldSurface->w != pNewSurface->w)
        return fail("width mismatch");
	if(pOldSurface->h != pNewSurface->h)
        return fail("height mismatch");
	// DestSurface drives the FadeBetween24 write/read loop; colorsf/colorst are
	// sized to pOldSurface, so a larger dest would read past them. Require an
	// exact dimension match (and non-null dest) to bound the loop.
	if(DestSurface->pitch != pOldSurface->pitch
	   || DestSurface->w != pOldSurface->w || DestSurface->h != pOldSurface->h)
        return fail("dest size mismatch");
	// SDL3: SDL_Surface::format is an SDL_PixelFormat enum — a single equality
	// check subsumes every legacy mask/shift/loss/BytesPerPixel comparison.
	if(pOldSurface->format != pNewSurface->format)
        return fail("pixel format mismatch");
	if(DestSurface->format != pOldSurface->format)
        return fail("dest pixel format mismatch");

	//Extract RGB pixel values from each image.
	const int bpp = SDL_GetPixelFormatDetails(pNewSurface->format)->bytes_per_pixel;
	if(bpp != 4)	//24-bit color only supported
        return fail("unsupported BytesPerPixel (expected 4)");

	Uint32 size = static_cast<Uint32>(pOldSurface->pitch * pOldSurface->h);
	std::vector<Uint8> colorsf(size);
	std::vector<Uint8> colorst(size);

	Uint8 *prf = static_cast<Uint8*>(pOldSurface->pixels), *prt = static_cast<Uint8*>(pNewSurface->pixels);
	memcpy(colorsf.data(), prf, size);
	memcpy(colorst.data(), prt, size);
	if(old_locked)
	{
		SDL_UnlockSurface(pOldSurface);
		old_locked = false;
	}

	//Fade from old to new surface.  Effect takes constant time.
#ifdef TESTING
	// In test mode, just do a direct blit instead of animated fade
	if(pNewSurface)
		SDL_BlitSurface(pNewSurface, nullptr, DestSurface, nullptr);
	TRACE("video", "FadeBetween: skipping animation (test mode)");
#else
	Uint64
		dwFirstPaint = SDL_GetTicks(),
		dwNow = dwFirstPaint;
	do {
		FadeBetween24(DestSurface,colorsf.data(),colorst.data(),
				static_cast<int>(dwNow - dwFirstPaint) + 50);	//allow first frame to show some change
		E_Screen->swap(0,0,active_canvas_w(),active_canvas_h());
		dwNow = SDL_GetTicks();

		get_input_events(POLL);
		if (query_key_press_event())
		{
			i = -1;
			break;
		}
	} while (dwNow - dwFirstPaint + 50 < static_cast<Uint64>(fadeDuration));	// constant-time effect
#endif

	//Show new screen entirely.
	// The destination may alias pNewSurface (fadeblack(true) does exactly
	// that), so the animation may have already
	// overwritten pNewSurface with its final partial blend. Restore the
	// snapshot captured before the animation in that case.
	if (pNewSurface == DestSurface)
		memcpy(DestSurface->pixels, colorst.data(), size);
	else
		SDL_BlitSurface(pNewSurface, nullptr, DestSurface, nullptr);
	// Preserve the historical contract that the old surface advances to the
	// new frame too, unless it is already the destination restored above.
	if (pOldSurface != DestSurface)
		SDL_BlitSurface(pNewSurface, nullptr, pOldSurface, nullptr);
	// Screen::Swap() does the work
	E_Screen->swap(0,0,active_canvas_w(),active_canvas_h());
	
	//Clean up.
	if (bOldNull)
		SDL_DestroySurface(pOldSurface);
	if (bNewNull)
		SDL_DestroySurface(pNewSurface);

	return i;
}

void sdl_video::fade_between24(void* surface, const Uint8* from, const Uint8* to,
                               int amount)
{
    FadeBetween24(static_cast<SDL_Surface*>(surface), from, to, amount);
}

int sdl_video::fade_between(void* old_surface, void* new_surface,
                            void* dest_surface)
{
    return FadeBetween(static_cast<SDL_Surface*>(old_surface),
                       static_cast<SDL_Surface*>(new_surface),
                       static_cast<SDL_Surface*>(dest_surface));
}

int sdl_video::fadeblack(bool fade_in)
{
	// Sized to the active canvas: FadeBetween requires exact dim matches
	// with E_Screen->render.
	SDL_Surface* black = SDL_CreateSurface(active_canvas_w(), active_canvas_h(), SDL_PIXELFORMAT_XRGB8888);
    if (!black)
        return -1;
    SDL_FillSurfaceRect(black, nullptr, map_surface_rgb_fast(black, 0, 0, 0));
	int i;

	if(fade_in)
        i = FadeBetween(black, E_Screen->render, E_Screen->render); // fade from black
	else
        i = FadeBetween(E_Screen->render, black, E_Screen->render); // fade to black

	SDL_DestroySurface(black);
	return i;
}
