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
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/io.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <cstring>
#include <openglad/platform/game_context.h>
#include <memory>
#include <span>
#include <vector>

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

// Dimensions of the ACTIVE canvas (world or UI; see CanvasTarget in
// video.h). Every offset conversion (offset = x + y*width), full-frame
// present rect and fade-surface size derives from these — byte-identical to
// the retired VIDEO_WIDTH/VIDEO_SIZE/CX_SCREEN/CY_SCREEN 320x200 constants
// while the canvases are at their default dims. The kUiCanvas fallbacks only
// matter for display-less (headless test) sessions, which never plot.
static inline int active_canvas_w() { return E_Screen ? E_Screen->canvas_w() : kUiCanvasW; }
static inline int active_canvas_h() { return E_Screen ? E_Screen->canvas_h() : kUiCanvasH; }


// videoptr lives in GameSession — access via current_session->videoptr_.

std::unique_ptr<Screen> E_Screen;

static void video_init_palettes(sdl_video& v)
{
	load_and_set_palette("our.pal", v.ourpalette);
	load_palette("our.pal", v.redpalette);

	for (Sint32 i = 32; i < 256; i++)
	{
		v.redpalette[i*3+1] /= 2;
		v.redpalette[i*3+2] /= 2;
	}

	load_palette("our.pal", v.bluepalette);
}

static void video_create_display()
{
	RenderEngine render = RenderEngine::NoZoom;
	int fullscreen_flag = 0;

	if(cfg.is_on("graphics","fullscreen"))
		fullscreen_flag = 1;

#ifdef __EMSCRIPTEN__
	// Never honor the fullscreen cfg in the browser: the shipped default
	// (graphics/fullscreen: on) made SDL request browser fullscreen at
	// boot via SDL_WINDOW_FULLSCREEN_DESKTOP — obnoxious, and the
	// Fullscreen API is meant to be a user gesture. The page/CSS owns
	// how big the canvas appears; users can fullscreen the tab themselves.
	fullscreen_flag = 0;
#endif

	std::string qresult = cfg.get_setting("graphics", "render");
	if(qresult == "normal")
		render = RenderEngine::NoZoom;
	else if(qresult == "sai")
		render = RenderEngine::SAI;
	else if(qresult == "eagle")
		render = RenderEngine::Eagle;
	else if(qresult == "double")
		render = RenderEngine::Double;

	int w = 640;
	int h = 400;

#ifdef __EMSCRIPTEN__
	w = 320;
	h = 200;
#else
	qresult = cfg.get_setting("graphics", "width");
	if(qresult.size() > 0)
	    w = stoi(qresult);

	qresult = cfg.get_setting("graphics", "height");
	if(qresult.size() > 0)
	    h = stoi(qresult);
#endif
	Log("Creating screen {}x{}\n", w, h);
	E_Screen = std::make_unique<Screen>(render, w, h, fullscreen_flag);
	TRACE("init", "video initialized: %dx%d", w, h);
	apply_world_scale_from_cfg();
}

void apply_world_scale_from_cfg()
{
#ifndef __EMSCRIPTEN__
	if (!E_Screen)
		return;
	const std::string value = cfg.get_setting("graphics", "scale");
	const og::WorldScaleSetting setting = og::parse_world_scale_setting(value);
	// A present-but-unrecognized value falls back to Legacy (the classic
	// byte-identical canvas) — say so, since the user asked for SOMETHING.
	// The empty string (absent key) and the documented "off" stay silent.
	if (setting.mode == og::WorldScaleMode::Legacy && !value.empty() &&
	    value != "off")
	{
		LogWarn("Unrecognized graphics/scale value \"{}\"; using classic "
		        "scaling (off). Accepted: off, 1, 2, 3, 4, 8, sai, eagle.\n",
		        value);
		TRACE("canvas", "world_scale unrecognized value=%s", value.c_str());
	}
	int win_w = 0;
	int win_h = 0;
	SDL_GetWindowSize(E_Screen->window, &win_w, &win_h);
	E_Screen->set_world_scale(setting, win_w, win_h);
	if (setting.mode != og::WorldScaleMode::Legacy)
		Log("World canvas {}x{} (graphics/scale={})\n",
		    E_Screen->world_w(), E_Screen->world_h(), value);
	TRACE("canvas", "world_scale mode=%d canvas=%dx%d",
	      static_cast<int>(setting.mode), E_Screen->world_w(), E_Screen->world_h());
#endif
}

sdl_video::sdl_video()
    : text_normal(TEXT_1), text_big(TEXT_BIG)
{
	fullscreen = 0;
	fadeDuration = 500;
	owns_display_ = true;

	video_init_palettes(*this);
	video_create_display();
}

sdl_video::sdl_video(bool create_display)
    : text_normal(TEXT_1), text_big(TEXT_BIG)
{
	fullscreen = 0;
	fadeDuration = 500;
	owns_display_ = create_display;

	video_init_palettes(*this);
	if (create_display) {
		video_create_display();
	}
}

sdl_video::~sdl_video()
{
	// Free the multi-floor compositing scratch surfaces (independent of the
	// display) before any SDL_Quit below.
	if (floor_layer_) { SDL_FreeSurface(floor_layer_); floor_layer_ = nullptr; }
	if (floor_layer_scaled_) { SDL_FreeSurface(floor_layer_scaled_); floor_layer_scaled_ = nullptr; }

	// Only the display-owning video instance tears down SDL.
	// IMPORTANT: All non-owning video instances (sub-sessions with
	// owns_display_=false) must be destroyed BEFORE the owning instance,
	// because SDL_Quit() shuts down all subsystems globally.  The demo
	// enforces this by destroying sub-sessions before the host session.
	if (owns_display_) {
		E_Screen.reset();
		SDL_Quit();
	}
}

void sdl_video::set_fullscreen(bool enable_fullscreen)
{
    (void)enable_fullscreen;
    // FIXME: A bug in my copy of SDL is making FULLSCREEN -> WINDOWED -> FULLSCREEN take up a partial portion of the screen and ruin the game.
    /*if(fullscreen)
    {
        SDL_SetWindowFullscreen(E_Screen->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else
    {
        SDL_SetWindowFullscreen(E_Screen->window, 0);
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
        hor_line_alpha(x, y+i, w, color, alpha);
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
    draw_text_bar(x, y, x + w - 1, y + h - 1);
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

Uint32 get_Uint32_color(unsigned char color)
{
    int r,g,b;
	query_palette_reg(color,&r,&g,&b);
		
	return SDL_MapRGB(E_Screen->render->format,
	                  static_cast<Uint8>(r * 4),
	                  static_cast<Uint8>(g * 4),
	                  static_cast<Uint8>(b * 4));
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

	//buffers: create the rect to fill with SDL_FillRect
	rect.x = startx;
	rect.y = starty;
	rect.w = xsize;
	rect.h = ysize;

	query_palette_reg(color,&r,&g,&b);
	SDL_FillRect(E_Screen->render, &rect, SDL_MapRGB(E_Screen->render->format,
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
    
    int bpp = surface->format->BytesPerPixel;
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

	//buffers: bound check against the CURRENT render target (mirrors
	// get_pixel). During a padded floor-layer redirect the target is the
	// grown off-screen layer, which extends past the legacy 320x200 logical
	// screen; a hardcoded 319/199 clip would truncate the padded window.
	if (x < 0 || y < 0 || x >= E_Screen->render->w || y >= E_Screen->render->h)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = SDL_MapRGB(E_Screen->render->format,
	               static_cast<Uint8>(r * 4),
	               static_cast<Uint8>(g * 4),
	               static_cast<Uint8>(b * 4));

    putpixel(E_Screen->render, x, y, c);
}

void blend_pixel(SDL_Surface* surface, int x, int y, Uint32 color, Uint8 alpha)
{
    Uint32 Rmask = surface->format->Rmask, Gmask = surface->format->Gmask, Bmask = surface->format->Bmask, Amask = surface->format->Amask;
    Uint32 R,G,B,A=0;//SDL_ALPHA_OPAQUE;
    Uint32* pixel;
    switch (surface->format->BytesPerPixel)
    {
        case 1: { /* Assuming 8-bpp */
            
                Uint8 *pixel8 = static_cast<Uint8*>(surface->pixels) + y*surface->pitch + x;
                
                Uint8 dR = surface->format->palette->colors[*pixel8].r;
                Uint8 dG = surface->format->palette->colors[*pixel8].g;
                Uint8 dB = surface->format->palette->colors[*pixel8].b;
                Uint8 sR = surface->format->palette->colors[color].r;
                Uint8 sG = surface->format->palette->colors[color].g;
                Uint8 sB = surface->format->palette->colors[color].b;
                
                dR = static_cast<Uint8>(dR + (((sR - dR) * alpha) >> 8));
                dG = static_cast<Uint8>(dG + (((sG - dG) * alpha) >> 8));
                dB = static_cast<Uint8>(dB + (((sB - dB) * alpha) >> 8));
            
                *pixel8 = static_cast<Uint8>(SDL_MapRGB(surface->format, dR, dG, dB));
                
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
            Uint8 rshift8=surface->format->Rshift/8;
            Uint8 gshift8=surface->format->Gshift/8;
            Uint8 bshift8=surface->format->Bshift/8;
            Uint8 ashift8=surface->format->Ashift/8;
            
            
            
                Uint8 dR, dG, dB, dA=0;
                Uint8 sR, sG, sB, sA=0;
                
                pix = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x*3;
                
                dR = *((pix)+rshift8); 
                dG = *((pix)+gshift8);
                dB = *((pix)+bshift8);
                dA = *((pix)+ashift8);
                
                sR = (color>>surface->format->Rshift)&0xff;
                sG = (color>>surface->format->Gshift)&0xff;
                sB = (color>>surface->format->Bshift)&0xff;
                sA = (color>>surface->format->Ashift)&0xff;
                
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

	//buffers: bound check against the CURRENT render target (mirrors
	// get_pixel). During a padded floor-layer redirect the target is the
	// grown off-screen layer, which extends past the legacy 320x200 logical
	// screen; a hardcoded 319/199 clip would truncate the padded window.
	if (x < 0 || y < 0 || x >= E_Screen->render->w || y >= E_Screen->render->h)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = SDL_MapRGB(E_Screen->render->format,
	               static_cast<Uint8>(r * 4),
	               static_cast<Uint8>(g * 4),
	               static_cast<Uint8>(b * 4));
	
    blend_pixel(E_Screen->render, x, y, c, alpha);
}

//buffers: this sets the color using raw RGB values. no *4...
void sdl_video::pointb(Sint32 x, Sint32 y, int r, int g, int b)
{
	SDL_Rect  rect;
	int c;
	c = SDL_MapRGB(E_Screen->render->format,
	               static_cast<Uint8>(r),
	               static_cast<Uint8>(g),
	               static_cast<Uint8>(b));

	rect.x = x;
	rect.y = y;
	rect.w = 1;
	rect.h = 1;
	SDL_FillRect(E_Screen->render,&rect,c);
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
			query_palette_reg(static_cast<char>(i-1), &curcol[0], &curcol[1], &curcol[2]);
			set_palette_reg(static_cast<char>(i), static_cast<char>(curcol[0]),static_cast<char>(curcol[1]), static_cast<char>(curcol[2]));
		}
		set_palette_reg(ORANGE_START, tempcol[0],
		                tempcol[1], tempcol[2]);        // reassign last to first

		// For blue:
		query_palette_reg(WATER_END, &tempcol[0],
		                  &tempcol[1], &tempcol[2]);        // get first color
		for (i=WATER_END; i > WATER_START; i--)
		{
			query_palette_reg(static_cast<char>(i-1), &curcol[0], &curcol[1], &curcol[2]);
			set_palette_reg(static_cast<char>(i), curcol[0], curcol[1], curcol[2]);
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
	int r,g,b,color;
	SDL_Rect rect;

	for(cury = starty;cury < starty +ysize;cury++)
 	{
		for (curx = startx; curx < startx +xsize; curx++)
	        {
			curcolor = sourcedata[num++];
			if (!curcolor)
		        	continue;
			//point(curx,cury,curcolor);//buffers: PORT: draw the poin
			query_palette_reg(curcolor,&r,&g,&b);
			color = SDL_MapRGB(E_Screen->render->format,
			                   static_cast<Uint8>(r * 4),
			                   static_cast<Uint8>(g * 4),
			                   static_cast<Uint8>(b * 4));

			rect.x = curx;
			rect.y = cury;
			rect.w = 1;
			rect.h = 1;
			Log("test\n");
			SDL_FillRect(E_Screen->render,&rect,color);
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
	int r,g,b,scolor;
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
			query_palette_reg(curcolor,&r,&g,&b);
			scolor = SDL_MapRGB(E_Screen->render->format,
			                    static_cast<Uint8>(r * 4),
			                    static_cast<Uint8>(g * 4),
			                    static_cast<Uint8>(b * 4));

            rect.x = curx;
            rect.y = cury;
			rect.w = 1;	
			rect.h = 1;
			SDL_FillRect(E_Screen->render,&rect,scolor);
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

    SDL_Surface* surface = SDL_CreateRGBSurface(
        SDL_SWSURFACE, width, height, 32, 0, 0, 0, 0);
    if (!surface) {
        LogError("sdl_video::create_accel_surface: SDL_CreateRGBSurface failed: {}\n",
                 SDL_GetError());
        return nullptr;
    }

    if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
        LogError("sdl_video::create_accel_surface: SDL_LockSurface failed: {}\n",
                 SDL_GetError());
        SDL_FreeSurface(surface);
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
            int r, g, b;
            query_palette_reg(indexed_pixels[src_index], &r, &g, &b);
            pixels[static_cast<std::size_t>(y) * pitch_pixels +
                   static_cast<std::size_t>(x)] =
                SDL_MapRGB(surface->format,
                           static_cast<Uint8>(r * 4),
                           static_cast<Uint8>(g * 4),
                           static_cast<Uint8>(b * 4));
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
    SDL_FreeSurface(static_cast<SDL_Surface*>(surface));
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
    // A below-camera floor draws a pad-widened window, so (x+w, y+h) can
    // exceed the render size: grow the layer on demand (never shrink — other
    // viewports may still need the full render extent this frame). The pad is
    // bounded by the caller's scale clamp (kMinBelowFloorScale), so the layer
    // tops out at ~2x the render dimensions.
    const int need_w = std::max(E_Screen->render->w, static_cast<int>(x + w));
    const int need_h = std::max(E_Screen->render->h, static_cast<int>(y + h));
    if (floor_layer_ && (floor_layer_->w < need_w || floor_layer_->h < need_h))
    {
        SDL_FreeSurface(floor_layer_);
        floor_layer_ = nullptr;
    }
    if (!floor_layer_)
    {
        floor_layer_ = SDL_CreateRGBSurfaceWithFormat(
            0, need_w, need_h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (floor_layer_)
            SDL_SetSurfaceBlendMode(floor_layer_, SDL_BLENDMODE_BLEND);
    }
    if (!floor_layer_)
        return false; // allocation failed: leave E_Screen->render untouched (no redirect)

    // Clear just this viewport's region to fully transparent (ARGB = 0). Opaque
    // tile/sprite blits below go through SDL_MapRGB on this alpha-capable format,
    // which yields A=0xFF, so drawn pixels become opaque coverage while un-drawn
    // cells remain transparent.
    SDL_Rect r{ x, y, w, h };
    SDL_FillRect(floor_layer_, &r, 0x00000000u);

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

    // Smooth path: bilinear-stretch the layer into a scratch surface, then
    // alpha-blend composite that scratch (at the floor's depth alpha) over the
    // real render surface. SDL_SoftStretchLinear ignores blend/alpha, so the
    // fade is applied on the second (blend) blit.
    if (floor_layer_scaled_ && (floor_layer_scaled_->w < floor_layer_->w ||
                                floor_layer_scaled_->h < floor_layer_->h))
    {
        // The layer grew (padded below-floor window): match the scratch.
        SDL_FreeSurface(floor_layer_scaled_);
        floor_layer_scaled_ = nullptr;
    }
    if (!floor_layer_scaled_)
    {
        floor_layer_scaled_ = SDL_CreateRGBSurfaceWithFormat(
            0, floor_layer_->w, floor_layer_->h, 32, SDL_PIXELFORMAT_ARGB8888);
    }
    bool smooth_ok = false;
    if (floor_layer_scaled_)
    {
        SDL_FillRect(floor_layer_scaled_, &out, 0x00000000u);
        smooth_ok = SDL_SoftStretchLinear(floor_layer_, &src,
                                          floor_layer_scaled_, &out) == 0;
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
        SDL_LockSurface(composited);
        const SDL_Rect& region = smooth_ok ? out : src;
        for (int py = region.y; py < region.y + region.h; py++)
        {
            Uint32* row = reinterpret_cast<Uint32*>(
                static_cast<Uint8*>(composited->pixels) +
                py * composited->pitch);
            for (int px = region.x; px < region.x + region.w; px++)
            {
                const Uint32 c = row[px];
                Uint8 pr, pg, pb, pa;
                SDL_GetRGBA(c, composited->format, &pr, &pg, &pb, &pa);
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
                    Uint32 m = (static_cast<Uint32>(px) * 0x9E3779B1u) ^
                               (static_cast<Uint32>(py) * 0x85EBCA77u);
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
                        depth_fog_alpha_at(px, py, fx.frame, fx.stories);
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
                row[px] = SDL_MapRGBA(composited->format, pr, pg, pb, pa);
            }
        }
        SDL_UnlockSurface(composited);
    }
    SDL_SetSurfaceAlphaMod(composited, alpha);
    SDL_SetSurfaceBlendMode(composited, SDL_BLENDMODE_BLEND);
    if (smooth_ok)
    {
        SDL_Rect dstpos = out;
        SDL_BlitSurface(composited, &out, E_Screen->render, &dstpos);
    }
    else
    {
        // Fallback (SoftStretchLinear unsupported): nearest-neighbour scaled
        // alpha blit straight from the layer. Still seam-free (one bitmap).
        SDL_Rect dst = out;
        SDL_BlitScaled(composited, &src, E_Screen->render, &dst);
    }
    SDL_SetSurfaceAlphaMod(composited, 255);
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
	Sint32 walkoff=0,buffoff=0,walkshift=0,buffshift=0;
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
	buffshift = active_canvas_w() - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*active_canvas_w()) + walkerstartx;


	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[walkoff++];
			if (!curcolor)
			{
				buffoff++;
				continue;
			}
			if (curcolor > static_cast<unsigned char>(247))
				curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
			//buffers: PORT: videobuffer[buffoff++] = curcolor;
			pointb(walkerstartx+curx,walkerstarty+cury,curcolor);
		}
		walkoff += walkshift;
		buffoff += buffshift;
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

	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[walkoff++];
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
			if (!sourceptr[walkoff + curx])
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
			curcolor = sourceptr[walkoff + curx];
			if (!curcolor)
				continue;
			Sint32 destx = walkerstartx + curx;
			Sint32 gx = (destx + world_offset_x) / GRID_SIZE;
			Sint32 gy = (desty + world_offset_y) / GRID_SIZE;
			if (gx < 0 || gx >= gridw || gy < 0 || gy >= gridh)
				continue;
			if (!reflect_mask[grid[gx + gridw*gy]])
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
	Sint32 walkoff=0,buffoff=0,walkshift=0,buffshift=0;
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
	buffshift = active_canvas_w() - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*active_canvas_w()) + walkerstartx;


	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[walkoff++];
			if (!curcolor)
			{
				buffoff++;
				continue;
			}
			
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
		buffoff += buffshift;
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
        Sint32 walkoff=0,buffoff=0,walkshift=0,buffshift=0;
        Sint32 totrows,rowsize;
	int r,g,b,color;
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
        buffshift = active_canvas_w() - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;
        buffoff   = (walkerstarty*active_canvas_w()) + walkerstartx;

        for(cury = 0; cury < totrows;cury++)
        {
                for(curx=0;curx<rowsize;curx++)
                {
                        curcolor = sourceptr[walkoff++];
                        if (!curcolor)
                        {
                                buffoff++;
                                continue;
                        }
		        if (curcolor > static_cast<unsigned char>(247))
		        {
		                curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
		        }
				query_palette_reg(curcolor,&r,&g,&b);
                        color = SDL_MapRGB(E_Screen->render->format,
                                           static_cast<Uint8>(r * 4),
                                           static_cast<Uint8>(g * 4),
                                           static_cast<Uint8>(b * 4));

                        rect.x = (curx + walkerstartx);
                        rect.y = (cury + walkerstarty);
                        rect.w = 1;
                        rect.h = 1;
                        SDL_FillRect(E_Screen->render,&rect,color);
                }
                walkoff += walkshift;
                buffoff += buffshift;
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
        Sint32 walkoff=0,buffoff=0,walkshift=0,buffshift=0;
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
        buffshift = active_canvas_w() - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;
        buffoff   = (walkerstarty*active_canvas_w()) + walkerstartx;

        for(cury = 0; cury < totrows;cury++)
        {
                for(curx=0;curx<rowsize;curx++)
                {
                        curcolor = sourceptr[walkoff++];
                        if (!curcolor)
                        {
                                buffoff++;
                                continue;
                        }
                        if (curcolor > static_cast<unsigned char>(247))
                                curcolor = static_cast<unsigned char>(teamcolor+(255-curcolor));
                        
                        pointb(curx + walkerstartx, cury + walkerstarty, teamcolor, alpha);
                }
                walkoff += walkshift;
                buffoff += buffshift;
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
					curcolor = sourceptr[walkoff++];
					if (!curcolor)
					{
						if (outline)
						{
							if (curx>0)
							{
								if (sourceptr[walkoff-2])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (curx<(rowsize-1))
							{
								if (sourceptr[walkoff])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (cury>0)
							{
								if (sourceptr[walkoff-1-walkerwidth])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (cury<(totrows-1))
							{
								if (sourceptr[walkoff-1+walkerwidth])
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

					if (rng(invisibility) > 8)
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
					curcolor = sourceptr[walkoff++];
					if (!curcolor)
					{
						if (curx>0)
						{
							if (sourceptr[walkoff-2])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (curx<(rowsize-1))
						{
							if (sourceptr[walkoff])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (cury>0)
						{
							if (sourceptr[walkoff-1-walkerwidth])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (cury<(totrows-1))
						{
							if (sourceptr[walkoff-1+walkerwidth])
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
					curcolor = sourceptr[walkoff++];
					if (!curcolor)
					{
						buffoff++;
						continue;
					}

					//buffers: this is a messy optimization. sorry.
					if (shifttype == SHIFT_RANDOM)
					{
						//pointb(buffoff++,get_pixel(buffoff+rng(2)));
						tempbuf = buffoff+rng(2);
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
						curcolor = sourceptr[walkoff++];
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

void sdl_video::set_active_canvas(CanvasTarget target)
{
	if (E_Screen)
		E_Screen->set_active_canvas(target);
}

CanvasTarget sdl_video::active_canvas() const
{
	return E_Screen ? E_Screen->active_canvas() : CanvasTarget::UI;
}

void sdl_video::set_world_canvas_pinned_classic(bool pinned)
{
	if (E_Screen)
		E_Screen->set_world_canvas_pinned_classic(pinned);
}

void sdl_video::reapply_world_scale()
{
	// The OPTIONS Scale button / RESTORE DEFAULTS live-apply path: re-parse
	// cfg graphics/scale and re-derive the world canvas from the current
	// window (a routing no-op when nothing changed — every default run).
	apply_world_scale_from_cfg();
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

	char *p = reinterpret_cast<char*>(E_Screen->render->pixels);
	p += E_Screen->render->pitch*y;
	p += E_Screen->render->format->BytesPerPixel*x;

	memcpy(&col,p,E_Screen->render->format->BytesPerPixel);

	SDL_GetRGB(col,E_Screen->render->format,&q,&w,&e);
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
    SDL_Surface* surf;
    
	switch(E_Screen->Engine)
	{
		case RenderEngine::SAI:
		case RenderEngine::Eagle:
            surf = E_Screen->render2;
		    break;
        default:
            surf = E_Screen->render;
            break;
	}
	
	static int i = 1;
    #ifndef USE_BMP_SCREENSHOT
	std::string buf = std::format("screenshot{}.png", i);
	#else
	std::string buf = std::format("screenshot{}.bmp", i);
	#endif
	i++;

	SDL_RWops* rwops = open_write_file(buf.c_str());
	if(rwops == nullptr)
    {
        LogError("Failed to open file for screenshot: {}\n", buf);
        return false;
    }
    
    Log("Saving screenshot: {}\n", buf);
    
    #ifndef USE_BMP_SCREENSHOT
    // Make it safe to save (convert alpha channel)
    surf = SDL_PNGFormatAlpha(surf);
    
    // Save it
    bool result = (SDL_SavePNG_RW(surf, rwops, 1) >= 0);
    SDL_FreeSurface(surf);
    #else
    bool result = (SDL_SaveBMP_RW(surf, rwops, 1) >= 0);
    
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
	Uint32 size = pSurface->pitch * pSurface->h;

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
	bool bOldNull = false, bNewNull = false;
	int i = 1;

	//Set nullptr pointers to temporary black screens
	//(for simple fade-in/out effects).
	if (!pOldSurface)
	{
		bOldNull = true;
		pOldSurface = SDL_CreateRGBSurface(SDL_SWSURFACE,
			active_canvas_w(), active_canvas_h(), 24, 0, 0, 0, 0);
		if (!pOldSurface) return 0;  // OOM: nothing safely lockable below
		SDL_FillRect(pOldSurface,nullptr,0);
	}
	if (!pNewSurface)
	{
		bNewNull = true;
		pNewSurface = SDL_CreateRGBSurface(SDL_SWSURFACE,
			active_canvas_w(), active_canvas_h(), 24, 0, 0, 0, 0);
		if (!pNewSurface) { if (bOldNull) SDL_FreeSurface(pOldSurface); return 0; }  // OOM: free the temp we just made
		SDL_FillRect(pNewSurface,nullptr,0);
	}
	if (bOldNull && bNewNull) return 0;	//nothing to do

	/* Lock the screen for direct access to the pixels */
    bool old_locked = false;
	if ( SDL_MUSTLOCK(pOldSurface) ) {
		if ( SDL_LockSurface(pOldSurface) < 0 ) {
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
            SDL_FreeSurface(pOldSurface);
        if(bNewNull)
            SDL_FreeSurface(pNewSurface);
        return 0;
    };
	
	//The new surface shouldn't need a lock unless it is somehow a screen surface.
	if(SDL_MUSTLOCK(pNewSurface))
        return fail("pNewSurface requires lock");

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
	if(!DestSurface || DestSurface->pitch != pOldSurface->pitch
	   || DestSurface->w != pOldSurface->w || DestSurface->h != pOldSurface->h)
        return fail("dest size mismatch");
	if(pOldSurface->format->Rmask != pNewSurface->format->Rmask)
        return fail("Rmask mismatch");
	if(pOldSurface->format->Rshift != pNewSurface->format->Rshift)
        return fail("Rshift mismatch");
	if(pOldSurface->format->Rloss != pNewSurface->format->Rloss)
        return fail("Rloss mismatch");
	if(pOldSurface->format->Gmask != pNewSurface->format->Gmask)
        return fail("Gmask mismatch");
	if(pOldSurface->format->Gshift != pNewSurface->format->Gshift)
        return fail("Gshift mismatch");
	if(pOldSurface->format->Gloss != pNewSurface->format->Gloss)
        return fail("Gloss mismatch");
	if(pOldSurface->format->Bmask != pNewSurface->format->Bmask)
        return fail("Bmask mismatch");
	if(pOldSurface->format->Bshift != pNewSurface->format->Bshift)
        return fail("Bshift mismatch");
	if(pOldSurface->format->Bloss != pNewSurface->format->Bloss)
        return fail("Bloss mismatch");
	if(pOldSurface->format->Rshift != pNewSurface->format->Rshift)
        return fail("Rshift mismatch (duplicate check)");
	if(pOldSurface->format->BytesPerPixel != pNewSurface->format->BytesPerPixel)
        return fail("BytesPerPixel mismatch");

	//Extract RGB pixel values from each image.
	const int bpp = pNewSurface->format->BytesPerPixel;
	if(bpp != 4)	//24-bit color only supported
        return fail("unsupported BytesPerPixel (expected 4)");

	Uint32 size = pOldSurface->pitch * pOldSurface->h;
	std::vector<Uint8> colorsf(size);
	std::vector<Uint8> colorst(size);

	Uint8 *prf = static_cast<Uint8*>(pOldSurface->pixels), *prt = static_cast<Uint8*>(pNewSurface->pixels);
	memcpy(colorsf.data(), prf, size);
	memcpy(colorst.data(), prt, size);

	//Fade from old to new surface.  Effect takes constant time.
#ifdef TESTING
	// In test mode, just do a direct blit instead of animated fade
	if(pNewSurface)
		SDL_BlitSurface(pNewSurface, nullptr, DestSurface, nullptr);
	TRACE("video", "FadeBetween: skipping animation (test mode)");
#else
	Uint32
		dwFirstPaint = SDL_GetTicks(),
		dwNow = dwFirstPaint;
	do {
		FadeBetween24(DestSurface,colorsf.data(),colorst.data(),
				dwNow - dwFirstPaint + 50);	//allow first frame to show some change
		E_Screen->swap(0,0,active_canvas_w(),active_canvas_h());
		dwNow = SDL_GetTicks();

		get_input_events(POLL);
		if (query_key_press_event())
		{
			i = -1;
			break;
		}
	} while (Sint32(dwNow) - Sint32(dwFirstPaint) + 50 < fadeDuration);	// constant-time effect
#endif

	if ( SDL_MUSTLOCK(pNewSurface) ) {
		SDL_UnlockSurface(pNewSurface);
	}

	//Show new screen entirely.
	SDL_BlitSurface(pNewSurface, nullptr, pOldSurface, nullptr);
	// Screen::Swap() does the work
	E_Screen->swap(0,0,active_canvas_w(),active_canvas_h());
	
	//Clean up.
	if (bOldNull)
		SDL_FreeSurface(pOldSurface);
	if (bNewNull)
		SDL_FreeSurface(pNewSurface);

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
	SDL_Surface* black = SDL_CreateRGBSurface(SDL_SWSURFACE, active_canvas_w(), active_canvas_h(), 32, 0, 0, 0, 0);
    if (!black)
        return -1;
    SDL_FillRect(black, nullptr, SDL_MapRGB(black->format, 0, 0, 0));
	int i;

	if(fade_in)
        i = FadeBetween(black, E_Screen->render, E_Screen->render); // fade from black
	else
        i = FadeBetween(E_Screen->render, black, E_Screen->render); // fade to black

	SDL_FreeSurface(black);
	return i;
}
