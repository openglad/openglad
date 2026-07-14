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
#include <openglad/interface/render/depth_fx.h>

#include <utility>
#include <array>
#include <span>
#include <vector>

class text;

// ---- Logical canvas targets -------------------------------------------------
//
// The renderer owns TWO logical canvases:
//  * the WORLD canvas — gameplay (viewscreens, HUD panels, level editor map
//    view). Its dimensions are variable in principle (default kUiCanvasW x
//    kUiCanvasH); nothing in the deterministic sim may ever read them.
//  * the UI canvas — menus, picker, help, intro, dialogs. PINNED at
//    kUiCanvasW x kUiCanvasH (320x200) forever, so every classic menu layout,
//    pixel pin and capture stays valid regardless of the world canvas size.
//
// Draw primitives route to the ACTIVE canvas; each canvas is presented with
// its own stretch to the window viewport at swap time. While the world canvas
// is at the default 320x200 the two canvases share ONE surface, which keeps
// swap (and every cross-mode flow: fades, in-game dialogs drawn over gameplay
// pixels, the demo compositor) byte-identical to the single-canvas renderer.
enum class CanvasTarget
{
    World,
    UI
};

// The fixed UI canvas dimensions — also the default world canvas dimensions.
inline constexpr int kUiCanvasW = 320;
inline constexpr int kUiCanvasH = 200;

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

    // Off-screen floor-layer compositing for the multi-floor vertical parallax.
    // floor_layer_begin redirects subsequent tile/sprite blits (which target the
    // backend's render surface) to a transparent off-screen layer covering
    // (x,y,w,h) — the viewport, or a pad-widened window for a below-camera
    // floor — returning true iff the redirect is installed (so the caller may
    // only widen its clip when the layer really absorbs the extra pixels).
    // floor_layer_end restores the real target and composites that layer back
    // onto the (x,y,w,h) viewport, smoothly (bilinear) scaled with a global
    // `alpha` (the floor's depth fade/ghost) and the depth-effect treatment
    // picked by `fx` (a default-constructed params object — mode Off —
    // composites bit-identically; floors below the camera pass the cfg'd mode
    // + their depth in stories). With pad_x/pad_y == 0 (the default; ghost
    // floors above, scale>=1) the layer holds the viewport 1:1 and is scaled
    // by `scale` about (cx,cy). With pads > 0 (a below floor, scale<1) the
    // layer holds a (w+2*pad_x)x(h+2*pad_y) window at (x,y) that is squeezed
    // onto the FULL viewport, so no black ring remains. Default no-ops for
    // backends without an off-screen surface. The caller gates these on
    // floor_count()>1, so single-floor / camera-floor rendering never enters
    // this path.
    virtual bool floor_layer_begin(Sint32 /*x*/, Sint32 /*y*/, Sint32 /*w*/, Sint32 /*h*/)
    {
        return false;
    }
    virtual void floor_layer_end(Sint32 /*x*/, Sint32 /*y*/, Sint32 /*w*/, Sint32 /*h*/,
                                 float /*scale*/, Sint32 /*cx*/, Sint32 /*cy*/,
                                 unsigned char /*alpha*/,
                                 DepthFxParams /*fx*/ = {},
                                 Sint32 /*pad_x*/ = 0, Sint32 /*pad_y*/ = 0) {}
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

    // Full-color, team-recolored sprite blit with a global alpha (for faded
    // lower floors / ghosted upper floors). Unlike walkputbuffertext_alpha
    // (single-color), this preserves the sprite's real colors.
    virtual void walkputbuffer_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                     Sint32 walkerwidth, Sint32 walkerheight,
                                     Sint32 portstartx, Sint32 portstarty,
                                     Sint32 portendx, Sint32 portendy,
                                     std::span<const unsigned char> sourceptr, unsigned char teamcolor, Uint8 alpha) = 0;

    // Ground-shadow blit: a vertically squashed black silhouette of the
    // sprite, bottom row one pixel below the sprite's feet.
    // (walkerstartx, walkerstarty) is the SPRITE's screen anchor — the shadow
    // rows run upward from walkerstarty + walkerheight. Source color 0 is
    // transparent; each covered target pixel is blended exactly once.
    // height_divisor squashes the silhouette to ~1/height_divisor of the
    // sprite height (2 = the classic half-height unit shadow; larger values
    // flatten it into the smaller "blob" cast by upper-floor entities), and
    // inset trims that many columns off each side (smaller-with-distance).
    virtual void walkputbuffer_shadow(Sint32 walkerstartx, Sint32 walkerstarty,
                                      Sint32 walkerwidth, Sint32 walkerheight,
                                      Sint32 portstartx, Sint32 portstarty,
                                      Sint32 portendx, Sint32 portendy,
                                      std::span<const unsigned char> sourceptr, Uint8 alpha,
                                      Sint32 height_divisor, Sint32 inset) = 0;

    // Reflection blit: the sprite vertically flipped (top-left at
    // walkerstartx, walkerstarty), team-recolored (>247 rule) and blended at
    // `alpha`, but a pixel is only plotted where the underlying grid tile's
    // id is marked in reflect_mask (per-tile-id lookup; production passes
    // reflective_tiles(): PIX_GLASS + pure water + lava/marsh).
    // world_offset_x/y convert
    // screen px to world px for the grid lookup (topx - xloc, topy - yloc);
    // grid is gridw x gridh tile ids.
    virtual void walkputbuffer_reflect(Sint32 walkerstartx, Sint32 walkerstarty,
                                       Sint32 walkerwidth, Sint32 walkerheight,
                                       Sint32 portstartx, Sint32 portstarty,
                                       Sint32 portendx, Sint32 portendy,
                                       std::span<const unsigned char> sourceptr,
                                       unsigned char teamcolor, Uint8 alpha,
                                       std::span<const unsigned char> grid,
                                       Sint32 gridw, Sint32 gridh,
                                       Sint32 world_offset_x, Sint32 world_offset_y,
                                       std::span<const bool, 256> reflect_mask) = 0;

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

    // Canvas routing (see the CanvasTarget block above).
    // canvas_w/canvas_h are the ACTIVE canvas dimensions: all offset-based
    // plot arithmetic (offset = x + y*canvas_w) and full-frame present rects
    // derive from them. world_canvas_w/world_canvas_h are the WORLD canvas
    // dimensions regardless of the active target (viewscreen layout sizing).
    // Defaults are the classic 320x200 in every implementation today.
    virtual int canvas_w() const = 0;
    virtual int canvas_h() const = 0;
    virtual int world_canvas_w() const = 0;
    virtual int world_canvas_h() const = 0;
    virtual void set_active_canvas(CanvasTarget target) = 0;
    virtual CanvasTarget active_canvas() const = 0;
    // Pin (true) the world canvas to the classic 320x200 dims, or restore
    // (false) the cfg graphics/scale-derived dims. The level editor pins for
    // its whole session: its panel chrome and mouse mapping still assume the
    // classic coordinate space. No-op for backends without a resizable world
    // canvas (and while pinned dims == current dims, i.e. every default run).
    virtual void set_world_canvas_pinned_classic(bool /*pinned*/) {}
    // Re-reads cfg graphics/scale and re-derives the world canvas from the
    // current window (the OPTIONS Scale button's live-apply seam; the caller
    // relayouts viewscreens when the dims moved). No-op for backends without
    // a scalable world canvas — and a routing no-op whenever the re-derived
    // dims match the current ones, i.e. every default run.
    virtual void reapply_world_scale() {}
    // DISPLAY settings live-apply: cfg graphics/fullscreen (windowed /
    // borderless / exclusive) + graphics/width/height, then re-derive the
    // overscan viewport and world canvas. No-op off the SDL display target.
    virtual void apply_display_settings_from_cfg() {}
    // Distinct WxH video modes of the primary display, largest first; empty
    // when the platform can't enumerate (headless drivers, web).
    virtual std::vector<std::pair<int, int>> display_resolutions() { return {}; }

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
    // Legacy scratch buffer sized to the canvas area (kUiCanvasW*kUiCanvasH
    // by default) — a vector so a future world-canvas resize can re-size it.
    virtual std::vector<unsigned char>& videobuffer_ref() = 0;
    virtual short& cyclemode_ref() = 0;
    virtual text& text_normal_ref() = 0;
    virtual text& text_big_ref() = 0;

protected:
    video() = default;
};
