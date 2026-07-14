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

#include <openglad/interface/render/video.h>
#include <openglad/interface/render/text.h>

#include <SDL3/SDL.h>

#include <array>
#include <span>
#include <vector>

class sdl_video final : public video
{
public:
    sdl_video();
    explicit sdl_video(bool create_display);
    ~sdl_video() override;

    void set_fullscreen(bool fullscreen) override;

    void clearbuffer() override;
    void clearbuffer(int x, int y, int w, int h) override;
    void clear_window() override;

    std::span<unsigned char> getbuffer() override;
    void putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize) override;
    void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color) override;
    void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color, unsigned char flag) override;
    void fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color) override;
    void point(Sint32 x, Sint32 y, unsigned char color) override;
    void pointb(Sint32 x, Sint32 y, unsigned char color) override;
    void pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha) override;
    void pointb(int offset, unsigned char color) override;
    void pointb(Sint32 x, Sint32 y, int r, int g, int b) override;
    void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color) override;
    void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color) override;
    void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer) override;
    void hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Uint8 alpha) override;
    void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer) override;
    void draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color) override;
    void do_cycle(Sint32 curmode, Sint32 maxmode) override;
    void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                 std::span<const unsigned char> sourcedata) override;
    void putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                       std::span<const unsigned char> sourcedata, unsigned char alpha) override;
    void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     std::span<const unsigned char> sourcedata) override;
    void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                 std::span<const unsigned char> sourcedata, unsigned char color) override;
    void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
                     std::span<const unsigned char> sourcedata, unsigned char color) override;

    void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                   Sint32 tilewidth, Sint32 tileheight,
                   Sint32 portstartx, Sint32 portstarty,
                   Sint32 portendx, Sint32 portendy,
                   std::span<const unsigned char> sourceptr) override;
    void putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
                         Sint32 tilewidth, Sint32 tileheight,
                         Sint32 portstartx, Sint32 portstarty,
                         Sint32 portendx, Sint32 portendy,
                         std::span<const unsigned char> sourceptr, unsigned char alpha) override;
    void putbuffer_surface(Sint32 tilestartx, Sint32 tilestarty,
                           Sint32 tilewidth, Sint32 tileheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           void* sourceptr) override;
    void* create_accel_surface(std::span<const unsigned char> indexed_pixels,
                               Sint32 width, Sint32 height) override;
    void destroy_accel_surface(void* surface) override;
    bool floor_layer_begin(Sint32 x, Sint32 y, Sint32 w, Sint32 h) override;
    void floor_layer_end(Sint32 x, Sint32 y, Sint32 w, Sint32 h,
                         float scale, Sint32 cx, Sint32 cy,
                         unsigned char alpha,
                         DepthFxParams fx = {},
                         Sint32 pad_x = 0, Sint32 pad_y = 0) override;
    void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
                   Sint32 tilewidth, Sint32 tileheight,
                   Sint32 portstartx, Sint32 portstarty,
                   Sint32 portendx, Sint32 portendy,
                   SDL_Surface* sourceptr);
    void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                       Sint32 walkerwidth, Sint32 walkerheight,
                       Sint32 portstartx, Sint32 portstarty,
                       Sint32 portendx, Sint32 portendy,
                       std::span<const unsigned char> sourceptr, unsigned char teamcolor) override;
    void walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
                             Sint32 walkerwidth, Sint32 walkerheight,
                             Sint32 portstartx, Sint32 portstarty,
                             Sint32 portendx, Sint32 portendy,
                             std::span<const unsigned char> sourceptr, unsigned char teamcolor) override;
    void walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
                           Sint32 walkerwidth, Sint32 walkerheight,
                           Sint32 portstartx, Sint32 portstarty,
                           Sint32 portendx, Sint32 portendy,
                           std::span<const unsigned char> sourceptr, unsigned char teamcolor) override;
    void walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                 Sint32 walkerwidth, Sint32 walkerheight,
                                 Sint32 portstartx, Sint32 portstarty,
                                 Sint32 portendx, Sint32 portendy,
                                 std::span<const unsigned char> sourceptr, unsigned char teamcolor, Uint8 alpha) override;
    void walkputbuffer_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                             Sint32 walkerwidth, Sint32 walkerheight,
                             Sint32 portstartx, Sint32 portstarty,
                             Sint32 portendx, Sint32 portendy,
                             std::span<const unsigned char> sourceptr, unsigned char teamcolor, Uint8 alpha) override;
    void walkputbuffer_shadow(Sint32 walkerstartx, Sint32 walkerstarty,
                              Sint32 walkerwidth, Sint32 walkerheight,
                              Sint32 portstartx, Sint32 portstarty,
                              Sint32 portendx, Sint32 portendy,
                              std::span<const unsigned char> sourceptr, Uint8 alpha,
                              Sint32 height_divisor, Sint32 inset) override;
    void walkputbuffer_reflect(Sint32 walkerstartx, Sint32 walkerstarty,
                               Sint32 walkerwidth, Sint32 walkerheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               std::span<const unsigned char> sourceptr,
                               unsigned char teamcolor, Uint8 alpha,
                               std::span<const unsigned char> grid,
                               Sint32 gridw, Sint32 gridh,
                               Sint32 world_offset_x, Sint32 world_offset_y,
                               std::span<const bool, 256> reflect_mask) override;

    void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
                       Sint32 walkerwidth, Sint32 walkerheight,
                       Sint32 portstartx, Sint32 portstarty,
                       Sint32 portendx, Sint32 portendy,
                       std::span<const unsigned char> sourceptr, unsigned char teamcolor,
                       unsigned char mode, Sint32 invisibility,
                       unsigned char outline, unsigned char shifttype) override;
    void buffer_to_screen(Sint32 viewstartx, Sint32 viewstarty,
                          Sint32 viewwidth, Sint32 viewheight) override;

    void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled) override;
    void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled, Sint32 tobuffer) override;
    void draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h, unsigned char color, Uint8 alpha) override;
    void draw_button(const SDL_Rect& rect, Sint32 border);
    void draw_button_inverted(const SDL_Rect& rect);
    void draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h) override;
    void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border) override;
    void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer) override;
    void draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                             bool use_border, int base_color, int high_color = 15, int shadow_color = 11) override;
    Sint32 draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, const char* header) override;
    void draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2) override;

    void darken_screen() override;

    void swap() override;

    // Canvas routing: delegated to the Screen (E_Screen) two-canvas split.
    int canvas_w() const override;
    int canvas_h() const override;
    int world_canvas_w() const override;
    int world_canvas_h() const override;
    void set_active_canvas(CanvasTarget target) override;
    CanvasTarget active_canvas() const override;
    void set_world_canvas_pinned_classic(bool pinned) override;
    void reapply_world_scale() override;
    void apply_display_settings_from_cfg() override;
    std::vector<std::pair<int, int>> display_resolutions() override;

    void get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b) override;
    int get_pixel(int x, int y, int* index) override;
    int get_pixel(int offset) override;

    bool save_screenshot() override;

    void FadeBetween24(SDL_Surface* surface, const Uint8* from, const Uint8* to, int amount);
    int FadeBetween(SDL_Surface* old_surface, SDL_Surface* new_surface, SDL_Surface* dest_surface);
    void fade_between24(void* surface, const Uint8* from, const Uint8* to, int amount) override;
    int fade_between(void* old_surface, void* new_surface, void* dest_surface) override;
    int fadeblack(bool fade_in) override;

    std::array<unsigned char, 768>& ourpalette_ref() override { return ourpalette; }
    std::array<unsigned char, 768>& redpalette_ref() override { return redpalette; }
    std::array<unsigned char, 768>& bluepalette_ref() override { return bluepalette; }
    std::array<unsigned char, 768>& dospalette_ref() override { return dospalette; }
    std::vector<unsigned char>& videobuffer_ref() override { return videobuffer; }
    short& cyclemode_ref() override { return cyclemode; }
    text& text_normal_ref() override { return text_normal; }
    text& text_big_ref() override { return text_big; }

    int fadeDuration;

    std::array<unsigned char, 768> ourpalette{};
    std::array<unsigned char, 768> redpalette{};
    std::array<unsigned char, 768> bluepalette{};
    std::array<unsigned char, 768> dospalette{};

    // Legacy scratch sized to the canvas area (kUiCanvasW*kUiCanvasH).
    std::vector<unsigned char> videobuffer =
        std::vector<unsigned char>(static_cast<std::size_t>(kUiCanvasW) *
                                       static_cast<std::size_t>(kUiCanvasH),
                                   0);
    short cyclemode = 0;

    //buffers: screen vars
    SDL_Surface* window = nullptr;
    int screen_width = 0, screen_height = 0, fullscreen = 0;
    int pdouble = 0;

    text text_normal;
    text text_big;

private:
    bool owns_display_ = true;

    // Off-screen compositing scratch for the multi-floor vertical parallax
    // (floor_layer_begin/floor_layer_end). Lazily created at the render size,
    // reused across frames, freed in the destructor. ARGB8888 (alpha-capable)
    // so un-drawn tile cells / air holes stay transparent and reveal the floors
    // below in the scaled composite.
    SDL_Surface* floor_layer_ = nullptr;         // transparent 1:1 draw target
    SDL_Surface* floor_layer_scaled_ = nullptr;  // bilinear-stretched scratch
    SDL_Surface* floor_layer_saved_render_ = nullptr; // E_Screen->render while redirected
};

// Installs the cfg graphics/scale world-canvas mode on the live display
// (E_Screen): parses the key, picks the world present engine, and sizes the
// world canvas from the current window. Called once by display creation;
// split out so tests can re-apply a changed setting. On Emscripten this is a
// no-op — the wasm build keeps the classic single-canvas path (its window is
// forced to 320x200; a variable world canvas there is a follow-up).
void apply_world_scale_from_cfg();
