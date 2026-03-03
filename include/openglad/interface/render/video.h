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
#pragma once

#include <openglad/interface/base.h>

#include <array>
#include <span>

class text;

// Abstract interface-layer rendering surface.
// Platform backends implement this contract (SDL in Phase 10).
class video
{
public:
    virtual ~video() = default;

    video(const video&) = delete;
    video& operator=(const video&) = delete;
    video(video&&) = delete;
    video& operator=(video&&) = delete;

    virtual void set_fullscreen(bool fullscreen) = 0;

    virtual void clearbuffer() = 0;
    virtual void clearbuffer(int x, int y, int w, int h) = 0;
    virtual void clear_window() = 0;

    virtual std::span<unsigned char> getbuffer() = 0;
    virtual void putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize) = 0;
    virtual void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color) = 0;
    virtual void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color, unsigned char flag) = 0;
    virtual void fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color) = 0;
    virtual void point(Sint32 x, Sint32 y, unsigned char color) = 0;
    //buffers: PORT: added below prototype
    virtual void pointb(Sint32 x, Sint32 y, unsigned char color) = 0;
    virtual void pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha) = 0;
    virtual void pointb(int offset, unsigned char color) = 0;
    virtual void pointb(Sint32 x, Sint32 y, int r, int g, int b) = 0;
    virtual void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color) = 0;
    virtual void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color) = 0;
    virtual void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer) = 0;
    virtual void hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Uint8 alpha) = 0;
    virtual void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer) = 0;
    virtual void draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color) = 0;
    virtual void do_cycle(Sint32 curmode, Sint32 maxmode) = 0;
    virtual void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                         std::span<const unsigned char> sourcedata) = 0;
    virtual void putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                               std::span<const unsigned char> sourcedata, unsigned char alpha) = 0;
    virtual void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                             std::span<const unsigned char> sourcedata) = 0;
    virtual void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                         std::span<const unsigned char> sourcedata, unsigned char color) = 0;
    virtual void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                             std::span<const unsigned char> sourcedata, unsigned char color) = 0;

    virtual void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                           Sint32 tilewidth, Sint32 tileheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           std::span<const unsigned char> sourceptr) = 0;
    virtual void putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
                                 Sint32 tilewidth, Sint32 tileheight,
                                 Sint32 portstartx, Sint32 portstarty,
                                 Sint32 portendx, Sint32 portendy,
                                 std::span<const unsigned char> sourceptr, unsigned char alpha) = 0;
    virtual void putbuffer_surface(Sint32 tilestartx, Sint32 tilestarty,
                                   Sint32 tilewidth, Sint32 tileheight,
                                   Sint32 portstartx, Sint32 portstarty,
                                   Sint32 portendx, Sint32 portendy,
                                   void* sourceptr) = 0;
    virtual void* create_accel_surface(std::span<const unsigned char> indexed_pixels,
                                       Sint32 width, Sint32 height) = 0;
    virtual void destroy_accel_surface(void* surface) = 0;
    virtual void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                               Sint32 walkerwidth, Sint32 walkerheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               std::span<const unsigned char> sourceptr, unsigned char teamcolor) = 0;
    virtual void walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
                                     Sint32 walkerwidth, Sint32 walkerheight,
                                     Sint32 portstartx, Sint32 portstarty,
                                     Sint32 portendx, Sint32 portendy,
                                     std::span<const unsigned char> sourceptr, unsigned char teamcolor) = 0;
    virtual void walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
                                   Sint32 walkerwidth, Sint32 walkerheight,
                                   Sint32 portstartx, Sint32 portstarty,
                                   Sint32 portendx, Sint32 portendy,
                                   std::span<const unsigned char> sourceptr, unsigned char teamcolor) = 0;
    virtual void walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                         Sint32 walkerwidth, Sint32 walkerheight,
                                         Sint32 portstartx, Sint32 portstarty,
                                         Sint32 portendx, Sint32 portendy,
                                         std::span<const unsigned char> sourceptr, unsigned char teamcolor, Uint8 alpha) = 0;

    virtual void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                               Sint32 walkerwidth, Sint32 walkerheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               std::span<const unsigned char> sourceptr, unsigned char teamcolor,
                               unsigned char mode, Sint32 invisibility,
                               unsigned char outline, unsigned char shifttype) = 0;
    virtual void buffer_to_screen(Sint32 viewstartx, Sint32 viewstarty,
                                  Sint32 viewwidth, Sint32 viewheight) = 0;

    virtual void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled) = 0;
    virtual void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled, Sint32 tobuffer) = 0;
    virtual void draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h, unsigned char color, Uint8 alpha) = 0;
    virtual void draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h) = 0;
    virtual void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border) = 0;
    virtual void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer) = 0;
    virtual void draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                                     bool use_border, int base_color, int high_color = 15, int shadow_color = 11) = 0;
    virtual Sint32 draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, const char* header) = 0;
    virtual void draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2) = 0;

    virtual void darken_screen() = 0;

    virtual void swap() = 0;

    virtual void get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b) = 0;
    virtual int get_pixel(int x, int y, int* index) = 0;
    virtual int get_pixel(int offset) = 0;

    virtual bool save_screenshot() = 0;

    virtual void fade_between24(void* surface, const Uint8* from, const Uint8* to, int amount) = 0;
    virtual int fade_between(void* old_surface, void* new_surface, void* dest_surface) = 0;
    virtual int fadeblack(bool fade_in) = 0;

    virtual std::array<unsigned char, 768>& ourpalette_ref() = 0;
    virtual std::array<unsigned char, 768>& redpalette_ref() = 0;
    virtual std::array<unsigned char, 768>& bluepalette_ref() = 0;
    virtual std::array<unsigned char, 768>& dospalette_ref() = 0;
    virtual std::array<unsigned char, 64000>& videobuffer_ref() = 0;
    virtual short& cyclemode_ref() = 0;
    virtual text& text_normal_ref() = 0;
    virtual text& text_big_ref() = 0;

protected:
    video() = default;
};
