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

#include "graph.h"
#include "render/sai2x.h"
#include "test_trace.h"
#include <format>
#include <cstring>
#include "runtime/game_context.h"
#include <memory>
#include <span>
#include <vector>

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

static cfg_store& active_config()
{
    if (ctx().config)
        return *ctx().config;
    return cfg;
}

#define VIDEO_BUFFER_WIDTH 320
#define VIDEO_WIDTH 320
#define VIDEO_SIZE 64000
#define CX_SCREEN 320
#define CY_SCREEN 200


unsigned char * videoptr = reinterpret_cast<unsigned char*>(VIDEO_LINEAR);

std::unique_ptr<Screen> E_Screen;

video::video()
    : text_normal(TEXT_1), text_big(TEXT_BIG)
{
	Sint32 i;
	RenderEngine render;
	fullscreen = 0;
    render = RenderEngine::NoZoom;

	if(active_config().is_on("graphics","fullscreen"))
		fullscreen = 1;
	else
		fullscreen = 0;

	
	std::string qresult = active_config().get_setting("graphics", "render");
	if(qresult == "normal")
		render = RenderEngine::NoZoom;
	else if(qresult == "sai")
		render = RenderEngine::SAI;
	else if(qresult == "eagle")
		render = RenderEngine::Eagle;
	else if(qresult == "double")
		render = RenderEngine::Double;
	
	fadeDuration = 500;

	// Load our palettes ..
	load_and_set_palette("our.pal", ourpalette);
	load_palette("our.pal", redpalette);

	// Create the red-shifted palette
	for (i=32; i < 256; i++)
	{
		redpalette[i*3+1] /= 2;
		redpalette[i*3+2] /= 2;
	}

	load_palette("our.pal", bluepalette);

	// Create the blue-shifted palette
	//for (i=32; i < 256; i++)
	//{
	//	bluepalette[i*3+0] /= 2;
	//	bluepalette[i*3+1] /= 2;
	//}

	int w = 640;
	int h = 400;

#ifdef __EMSCRIPTEN__
	// For web builds, use the native game resolution to match the canvas
	// CSS scaling handles display size
	w = 320;
	h = 200;
#else
	qresult = active_config().get_setting("graphics", "width");
	if(qresult.size() > 0)
	    w = stoi(qresult);

	qresult = active_config().get_setting("graphics", "height");
	if(qresult.size() > 0)
	    h = stoi(qresult);
#endif
        Log("Creating screen {}x{}\n", w, h);
	E_Screen = std::make_unique<Screen>(render, w, h, fullscreen);
	TRACE("init", "video initialized: %dx%d", w, h);
}

video::~video()
{
	E_Screen.reset();
	SDL_Quit();
}

void video::set_fullscreen(bool enable_fullscreen)
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
    window_w = w;
    window_h = h;
    update_overscan_setting();*/
}

unsigned char * video::getbuffer()
{
	return videobuffer.data();
}

void video::clearbuffer()
{
    E_Screen->clear();
}

void video::clearbuffer(int x, int y, int w, int h)
{
    E_Screen->clear(x, y, w, h);
}

void video::clear_window()
{
    E_Screen->clear_window();
}

void video::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled)
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

void video::draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled, Sint32 tobuffer)
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

void video::draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h, unsigned char color, Uint8 alpha)
{
    for (Uint32 i = 0; i < h; i++)
        hor_line_alpha(x, y+i, w, color, alpha);
}


void video::draw_button(const SDL_Rect& rect, Sint32 border)
{
    draw_button(rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1, border);
}

void video::draw_button_inverted(const SDL_Rect& rect)
{
    draw_text_bar(rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1);
}

void video::draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    draw_text_bar(x, y, x + w - 1, y + h - 1);
}


void video::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border)
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

void video::draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer)
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

void video::draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, bool use_border, int base_color, int high_color, int shadow_color)
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
Sint32 video::draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
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

void video::draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2)
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

void video::darken_screen()
{
    for(int i = 0; i < 320; i++)
    {
        for(int j = 0; j < 200; j++)
        {
            pointb(i, j, PURE_BLACK, 100);
        }
    }
}



void video::putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize)
{
	Sint32 curx, cury;
	Sint32 curpoint;

	for(cury = starty;cury < starty +ysize;cury++)
	{
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curpoint = (curx + (cury*VIDEO_WIDTH));
			if (curpoint > 0 && curpoint < VIDEO_SIZE)
				videoptr[curpoint] = 0;
		}
	}
}

// This version of fastbox writes directly to screen memory;
// The following version, with an extra parameter, writes to
// the buffer instead.  Note that it does NOT update (to screen)
// the area which it changes..
void video::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color)
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
void video::fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color, unsigned char flag)
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

void video::fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color)
{
    draw_box(startx, starty, startx + xsize, starty + ysize, color, 0);
}

// Place a point on the screen
//buffers: PORT: this point func is equivalent to drawing directly to screen
void video::point(Sint32 x, Sint32 y, unsigned char color)
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
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = static_cast<Uint8>(pixel);
        break;

    case 2:
        *(Uint16 *)p = static_cast<Uint16>(pixel);
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
        *(Uint32 *)p = pixel;
        break;
    }
}

//buffers: PORT: this draws a point in the offscreen buffer
//buffers: PORT: used for all the funcs that draw stuff in the offscreen buf
void video::pointb(Sint32 x, Sint32 y, unsigned char color)
{
	int r,g,b;
	int c;

	//buffers: this does bound checking (just to be safe)
	if(x<0 || x>319 || y<0 || y>199)
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
            
                Uint8 *pixel8 = (Uint8 *)surface->pixels + y*surface->pitch + x;
                
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
            
                Uint16 *pixel16 = (Uint16 *)surface->pixels + y*surface->pitch/2 + x;
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
            Uint8 *pix = (Uint8 *)surface->pixels + y * surface->pitch + x*3;
            Uint8 rshift8=surface->format->Rshift/8;
            Uint8 gshift8=surface->format->Gshift/8;
            Uint8 bshift8=surface->format->Bshift/8;
            Uint8 ashift8=surface->format->Ashift/8;
            
            
            
                Uint8 dR, dG, dB, dA=0;
                Uint8 sR, sG, sB, sA=0;
                
                pix = (Uint8 *)surface->pixels + y * surface->pitch + x*3;
                
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
            pixel = (Uint32*)surface->pixels + y*surface->pitch/4 + x;
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

void video::pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha)
{
	int r,g,b;
	int c;

	//buffers: this does bound checking (just to be safe)
	if(x<0 || x>319 || y<0 || y>199)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = SDL_MapRGB(E_Screen->render->format,
	               static_cast<Uint8>(r * 4),
	               static_cast<Uint8>(g * 4),
	               static_cast<Uint8>(b * 4));
	
    blend_pixel(E_Screen->render, x, y, c, alpha);
}

//buffers: this sets the color using raw RGB values. no *4...
void video::pointb(Sint32 x, Sint32 y, int r, int g, int b)
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
void video::pointb(int offset, unsigned char color)
{
	int x, y;

	y = offset/320;
	x = offset - y*320;

	pointb(x,y,color);
}

// Place a horizontal line on the screen.
//buffers: this func originally drew directly to the screen
void video::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
	hor_line(x,y,length,color,1);
}

void video::hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer)
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

void video::hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Uint8 alpha)
{
	Sint32 i;

	for (i = 0; i < length; i++)
		pointb(x+i,y,color, alpha);
}


// Place a vertical line on the screen.
// buffers: this func originally drew directly to the screen
void video::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color)
{
	//buffers: we always want to draw to the back buffer now
	ver_line(x,y,length,color,1);
}

void video::ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer)
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
void video::draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color)
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
//video::do_cycle
//cycle the palette for flame and water motion
// query and set functions are located in pal32.cpp
//buffers: PORT: added & to the last 3 args of the query_palette_reg funcs
void video::do_cycle(Sint32 curmode, Sint32 maxmode)
{
	Sint32 i;
	//buffers: PORT: changed these two arrays to ints
	int tempcol[3];
	int curcol[3];

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

//video::putdata
//draws objects to screen, respecting transparency
//used by text
void video::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata)
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
void video::putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata, unsigned char alpha)
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


void video::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata)
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

//video::putdata
//draws objects to screen, respecting transparency
//used by text
void video::putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata, unsigned char color)
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

void video::putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, std::span<const unsigned char> sourcedata, unsigned char color)
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

// video::putbuffer
// used to put tiles into the buffer as we compose the screen
// tilestartx,tilestarty are the ul corner of the tiles position on
//    screen, which may be negative since we have tiles offscreen
// tilewidth,tileheight are the tile size, which will usually be GRID_SIZE
//    but this leaves things open
// portstartx portstarty portendx porthendy allow us to clip to
//    a rectangular window on screen, ie a viewscreen
// sourceptr is a pointer to the video data to be copied into the buffer
void video::putbuffer(Sint32 tilestartx, Sint32 tilestarty,
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

void video::putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
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
void video::putbuffer(Sint32 tilestartx, Sint32 tilestarty,
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


// walkputbuffer draws active guys to the screen (basically all non-tiles
// c-only since it isn't used that often (despite what you might think)
// walkerstartx,walkerstarty are the screen position we will try to draw to
// walkerwidth,walkerheight define the object's size
// portstartx,portstarty,portendx,portendy define a clipping rectangle
// sourceptr holds the walker data
// teamcolor is used for recoloring the guys to the appropriate team
void video::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
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
	buffshift = VIDEO_BUFFER_WIDTH - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;


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

void video::walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
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
	buffshift = VIDEO_BUFFER_WIDTH - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;


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

void video::walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
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
        buffshift = VIDEO_BUFFER_WIDTH - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;
        buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;

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

void video::walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
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
        buffshift = VIDEO_BUFFER_WIDTH - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;
        buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;

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


void video::walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
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
	buffshift = VIDEO_BUFFER_WIDTH - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;
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
						ty = tempbuf/320;
						tx = tempbuf-ty*320;
						get_pixel(tx,ty,&r,&g,&b);

						ty = buffoff/320;
						tx = buffoff-ty*320;
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
								pointb(buffoff, static_cast<unsigned char>(get_pixel(buffoff-320)));
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
						videobuffer[buffoff++] = curcolor;
					} //end each row

					walkoff += walkshift;
					buffoff += buffshift;
				} //end all rows

			} //end default

	} //end switch of mode
}

// video::buffer_to_screen
// copies all or a portion of the video buffer to the screen
// viewstartx,viewstarty,viewwidth,viewheight define a rectangle which
//     can be used to draw only a portion of the buffer to screen,
//     and is used to draw viewscreens when we don't need a full update
// NOTE!! this function requires that you pass it a rectangle which is
// a multiple of four WIDE, or it will NOT draw correctly
// This is designed this way with the assumption that screen draws are
// the slowest thing we can possible do.
void video::buffer_to_screen(Sint32 viewstartx,Sint32 viewstarty,
                             Sint32 viewwidth, Sint32 viewheight)
{
	E_Screen->swap(viewstartx,viewstarty,viewwidth,viewheight);
}

//buffers: like buffer_to_screen but automaticaly swaps the entire screen
void video::swap(void)
{
	buffer_to_screen(0,0,320,200);
}

//buffers: get pixel's RGB values if you have XY
void video::get_pixel(int x, int y, Uint8 *r, Uint8 *g, Uint8 *b)
{
	Uint32 col = 0;
	Uint8 q=0,w=0,e=0;

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
int video::get_pixel(int x, int y, int *index)
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
int video::get_pixel(int offset)
{
	int x,y,t;

	y = offset/320;
	x = offset-y*320;

	return get_pixel(x,y,&t);
}

#ifndef USE_BMP_SCREENSHOT
#include "../util/savepng.h"
#endif

bool video::save_screenshot()
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
void video::FadeBetween24(
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
	Uint8 *pw = (Uint8 *)pSurface->pixels;
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
int video::FadeBetween(
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
			CX_SCREEN, CY_SCREEN, 24, 0, 0, 0, 0);
		SDL_FillRect(pOldSurface,nullptr,0);
	}
	if (!pNewSurface)
	{
		bNewNull = true;
		pNewSurface = SDL_CreateRGBSurface(SDL_SWSURFACE,
			CX_SCREEN, CY_SCREEN, 24, 0, 0, 0, 0);
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

	Uint8 *prf = (Uint8 *)pOldSurface->pixels, *prt = (Uint8 *)pNewSurface->pixels;
	memcpy(colorsf.data(), prf, size);
	memcpy(colorst.data(), prt, size);

	//Fade from old to new surface.  Effect takes constant time.
#ifdef TESTING
	// In test mode, just do a direct blit instead of animated fade
	if(pNewSurface)
		SDL_BlitSurface(pNewSurface, NULL, DestSurface, NULL);
	TRACE("video", "FadeBetween: skipping animation (test mode)");
#else
	Uint32
		dwFirstPaint = SDL_GetTicks(),
		dwNow = dwFirstPaint;
	do {
		FadeBetween24(DestSurface,colorsf.data(),colorst.data(),
				dwNow - dwFirstPaint + 50);	//allow first frame to show some change
		E_Screen->swap(0,0,320,200);
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
	E_Screen->swap(0,0,320,200);
	
	//Clean up.
	if (bOldNull)
		SDL_FreeSurface(pOldSurface);
	if (bNewNull)
		SDL_FreeSurface(pNewSurface);

	return i;
}

int video::fadeblack(bool fade_in)
{
	SDL_Surface* black = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 200, 32, 0, 0, 0, 0);
	int i;

	if(fade_in)
        i = FadeBetween(black, E_Screen->render, E_Screen->render); // fade from black
	else
        i = FadeBetween(E_Screen->render, black, E_Screen->render); // fade to black

	SDL_FreeSurface(black);
	return i;
}
