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
#include <cstdint>
#include <span>
#include <vector>

class text;

// ---- Logical canvas targets -------------------------------------------------
//
// The renderer owns three logical targets:
//  * the WORLD canvas — gameplay scenery (map, tiles, sprites, effects, level
//    editor map view). At zoom 1.0 its dimensions follow the logical window;
//    lower zoom values enlarge it. The deterministic sim never reads them.
//  * the UI canvas — menus, picker, help, intro, dialogs. PINNED at
//    kUiCanvasW x kUiCanvasH (320x200) forever, so every classic menu layout,
//    pixel pin and capture stays valid regardless of the world canvas size.
//  * the GAMEPLAY UI canvas — a transparent overlay pinned to the zoom-1.0
//    gameplay geometry. Radar, messages and HUD paint here at a stable size,
//    then the backend composites it nearest-neighbour over the zoomed scenery.
//    Anything drawn here that must line up with a world sprite has to project
//    BOTH its anchor and its extents (mini health bars do; see
//    draw_small_health_bar) — a raw world-canvas length used on this overlay
//    comes out 1/zoom times too large.
//
// Draw primitives route to the ACTIVE canvas; each canvas is aspect-fitted in
// the window viewport at swap time. While the world canvas is 320x200 the two
// canvases share ONE surface, which keeps
// swap (and every cross-mode flow: fades, in-game dialogs drawn over gameplay
// pixels, the demo compositor) byte-identical to the single-canvas renderer.
enum class CanvasTarget
{
    World,
    UI,
    GameplayUI
};

// Why a native display-state snapshot is safe to confirm. SDL3's ordinary
// fullscreen getters include pending requests, so only a successful explicit
// synchronization or the corresponding completion event may advance cfg.
enum class DisplayStateConfirmation
{
    Synchronized,
    EnterFullscreen,
    LeaveFullscreen,
    Resized,
    PixelSizeChanged
};

// The fixed UI canvas dimensions and minimum world-canvas dimensions.
inline constexpr int kUiCanvasW = 320;
inline constexpr int kUiCanvasH = 200;

// One per-view presentation slice (§7.1 per-view zoom): the view's 1:1
// world-canvas WINDOW presented onto its proportional canvas SLOT. Both
// rects are in world-canvas coordinates; the backend maps dst through the
// same aspect-fitted viewport as the whole-canvas present.
struct WorldPresentSlice
{
    int src_x = 0;
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;
    int dst_x = 0;
    int dst_y = 0;
    int dst_w = 0;
    int dst_h = 0;
};

// A live world image is never rasterized into UI or GameplayUI. Instead it
// renders at its own world-pixel resolution and is composited into one or more
// logical canvas rectangles at the physical-display present seam. The canvas
// selects only the destination coordinate space; it never limits the source
// raster's resolution.
struct NativeWorldViewDestination
{
    CanvasTarget canvas = CanvasTarget::UI;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct NativeWorldViewSource
{
    int w = 0;
    int h = 0;

    constexpr explicit operator bool() const { return w > 0 && h > 0; }
};

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

    // Resolution-independent live-world plane. The backend derives the source
    // raster from the destinations' actual physical-output pixel dimensions,
    // then redirects all ordinary world draw primitives into it. Callers use
    // the returned dimensions as their camera window; they cannot choose a
    // DOS-sized or wastefully oversampled intermediate. end restores the prior
    // canvas and queues that raster for the next physical present.
    // Unsupported/allocation-failed backends return an empty source and must
    // not fall back to drawing live world pixels into a UI canvas.
    virtual NativeWorldViewSource begin_native_world_view(
        std::span<const NativeWorldViewDestination> /*destinations*/)
    {
        return {};
    }
    virtual bool end_native_world_view() { return false; }
    virtual void cancel_native_world_view() {}
    // True only while draw primitives are redirected to a native world
    // plane. World renderers use this with active_canvas() to reject any
    // attempt to rasterize a live scene into UI or GameplayUI.
    virtual bool native_world_view_active() const { return false; }

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
    virtual void fail_next_floor_layer_allocation_for_testing() {}
    [[nodiscard]] virtual int floor_layer_fallback_count_for_testing() const
    {
        return 0;
    }
    [[nodiscard]] virtual std::int64_t floor_layer_source_pixels_for_testing() const
    {
        return 0;
    }
    [[nodiscard]] virtual std::int64_t floor_layer_scaled_pixels_for_testing() const
    {
        return 0;
    }
    [[nodiscard]] virtual bool floor_layer_redirect_active_for_testing() const
    {
        return false;
    }
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
    // gameplay_ui_canvas_w/h expose the stable zoom-1.0 gameplay geometry;
    // fixed/headless backends may keep it equal to their world dimensions.
    // Display backends use an aspect-relative, zoomable world canvas;
    // fixed/headless backends may retain the minimum 320x200 dimensions.
    virtual int canvas_w() const = 0;
    virtual int canvas_h() const = 0;
    virtual int world_canvas_w() const = 0;
    virtual int world_canvas_h() const = 0;
    virtual int gameplay_ui_canvas_w() const { return world_canvas_w(); }
    virtual int gameplay_ui_canvas_h() const { return world_canvas_h(); }
    // True when GameplayUI drawing can use the fixed gameplay-UI dimensions.
    // On an allocation fallback it aliases World instead; touch hit-testing
    // must use the same effective geometry as the controls that were drawn.
    virtual bool gameplay_ui_canvas_available() const
    {
        return gameplay_ui_canvas_w() == world_canvas_w() &&
               gameplay_ui_canvas_h() == world_canvas_h();
    }
    virtual void set_active_canvas(CanvasTarget target) = 0;
    virtual CanvasTarget active_canvas() const = 0;
	// Canvas that produced the most recent physical present. Scoped UI draws
	// may restore another active target afterwards; transition fades use this
	// value so they fade what the player can actually see.
	virtual CanvasTarget last_presented_canvas() const { return active_canvas(); }
    // Start a gameplay render frame. Scalable backends may allocate and clear
    // a transparent fixed-size gameplay-UI overlay here. At zoom 1.0 with
    // nearest rendering the historical single-surface path can still be used.
    virtual void begin_gameplay_frame() {}
    // Seed the fixed UI canvas from the current world frame using nearest
    // scaling. Modal UI then overlays a crisp 320x200 background even when
    // graphics/zoom has split the world onto a larger surface.
    virtual void prepare_ui_canvas_from_world() {}
    // Pin (true) the world canvas to the classic 320x200 dims, or restore
    // (false) the cfg graphics/zoom-derived dims. The level editor pins for
    // its whole session: its panel chrome and mouse mapping still assume the
    // classic coordinate space. No-op for backends without a resizable world
    // canvas, or when the current world canvas is already 320x200.
    virtual void set_world_canvas_pinned_classic(bool /*pinned*/) {}
    // Re-reads cfg graphics/zoom and graphics/smoothing. The canvas derives
    // from the display aspect and zoom; the caller relayouts viewscreens only
    // when those logical dimensions change. No-op for backends without a
    // scalable world canvas.
    virtual void reapply_world_scale() {}
    // Deepest selectable zoom step supported by the current logical window
    // and renderer. The default exposes the complete 0.1..1.0 range.
    virtual int minimum_world_zoom_steps() const { return 1; }

    // --- Per-view zoom composition + partitioned presentation (§7.1) -------
    // Per-view zoom rides the SAME single-resample pipeline as graphics/zoom:
    // the world canvas derives from the minimum effective zoom (global steps
    // composed with the deepest per-view scale), every view renders its
    // window 1:1, and presentation performs the one and only resample.
    //
    // set_world_view_scale composes `min_scale_num` (tenths, 10 = no
    // override .. 5 = 0.5x; see og::clamp_view_scale_num) into the canvas
    // derivation. 10 is the untouched global-only path — zoom OFF for every
    // view stays byte-identical by construction.
    virtual void set_world_view_scale(int /*min_scale_num*/) {}
    // Whether the composed canvas for `min_scale_num` at the CURRENT global
    // zoom fits the budget/texture limits (the per-view cycler's clamp).
    virtual bool world_zoom_composition_fits(int /*min_scale_num*/) const
    {
        return true;
    }
    // True when the backend can present per-view slices; the per-view ZOOM
    // rows are disabled (never wrong) without it.
    virtual bool world_present_partition_supported() const { return false; }
    // Live query for the pin state (per-view zoom is forced inert while the
    // level editor pins the classic canvas).
    virtual bool world_canvas_pinned_classic() const { return false; }
    // Present-time partition: when any view's 1:1 window differs from its
    // proportional canvas slot, the backend overlays one nearest-scaled
    // texture blit per slice (src = window, dst = slot, both in world-canvas
    // coordinates) after the ordinary whole-canvas present. An empty span
    // clears the partition (the byte-identical single-blit path).
    virtual void set_world_present_slices(
        std::span<const WorldPresentSlice> /*slices*/) {}
    // Whether the current world canvas can use the bounded 2x smart-scaler
    // scratch. False lets UI describe a retained SAI/Eagle preference as N/A.
    virtual bool world_smoothing_supported() const { return true; }
    // DISPLAY settings live-apply: cfg graphics/fullscreen (windowed /
    // borderless / exclusive) + graphics/width/height, then update the
    // overscan viewport, then recompute the aspect-relative zoom canvas.
    // No-op off the SDL display target.
    virtual void apply_display_settings_from_cfg() {}
    // Re-read the native window's completed fullscreen state into cfg and
    // viewport bookkeeping. SDL can finish fullscreen transitions after the
    // initiating call returns, so its enter/leave events use this hook to
    // reconcile the eventual state. No-op for non-windowed backends.
    virtual void reflect_display_settings_from_window(
        DisplayStateConfirmation /*confirmation*/ = DisplayStateConfirmation::Synchronized,
        std::uint64_t /*event_timestamp_ns*/ = 0) {}
    // Distinct physical-pixel WxH fullscreen modes of the window's display,
    // largest first; empty when the platform can't enumerate (headless
    // drivers, web).
    virtual std::vector<std::pair<int, int>> display_resolutions() { return {}; }
    // The window display's current physical-pixel desktop resolution; {0,0}
    // when unknown. On HiDPI displays this can differ from SDL window units.
    virtual std::pair<int, int> desktop_resolution() { return {0, 0}; }
    // Maximum useful Windowed size in SDL logical window coordinates. This
    // deliberately excludes taskbars/docks and is not multiplied by display
    // density. {0,0} when unknown.
    virtual std::pair<int, int> windowed_desktop_resolution() { return {0, 0}; }

    virtual void get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b) = 0;
    virtual int get_pixel(int x, int y, int* index) = 0;
    virtual int get_pixel(int offset) = 0;

    virtual bool save_screenshot() = 0;

    virtual void fade_between24(void* surface, const Uint8* from, const Uint8* to, int amount) = 0;
    virtual int fade_between(void* old_surface, void* new_surface, void* dest_surface) = 0;
    // Fade ownership (docs/menu-engine.md, "Drawing and transitions"):
    // fadeblack(false) blends the ACTIVE render buffer to black and is a
    // no-op (returns 0, no FadeBetween) while the window already shows
    // black; fadeblack(true) blends the composed buffer in from black.
    // Whoever fades a screen in fades it out, at its own exit, while its
    // last presented frame is still the buffer.
    virtual int fadeblack(bool fade_in) = 0;
    // True while the window shows black: never presented, or a completed
    // fade-out with no present since. The single source of truth.
    virtual bool window_is_black() = 0;
#ifdef TESTING
    // Test boundary reset: the window is black and every canvas counts as
    // presented as it stands. See Screen::testing_reset_window_state.
    virtual void testing_reset_window_state() = 0;
    // Records a fade-ownership violation the video layer cannot see at the
    // fade itself (an ENTRY that found an unfaded window — the outgoing
    // screen skipped its exit fade-out). Same sink as the two fade-site
    // invariants: traced, logged, and failed by the integration listener.
    virtual void testing_report_fade_violation(const char* what) = 0;
#endif

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

// Temporarily route drawing and input coordinates to one logical canvas.
// Modal UI can return through several branches, so restoring the caller's
// target in a scope guard keeps gameplay on the world canvas after it closes.
class ScopedCanvasTarget final
{
public:
    ScopedCanvasTarget(video& output, CanvasTarget target)
        : output_(output), previous_(output.active_canvas())
    {
        output_.set_active_canvas(target);
    }

    ~ScopedCanvasTarget()
    {
        output_.set_active_canvas(previous_);
    }

    ScopedCanvasTarget(const ScopedCanvasTarget&) = delete;
    ScopedCanvasTarget& operator=(const ScopedCanvasTarget&) = delete;
    ScopedCanvasTarget(ScopedCanvasTarget&&) = delete;
    ScopedCanvasTarget& operator=(ScopedCanvasTarget&&) = delete;

    CanvasTarget previous() const { return previous_; }

private:
    video& output_;
    CanvasTarget previous_;
};

// Fixed-coordinate UI entry point. When entered from gameplay, seed the UI
// backing from the complete current frame before drawing menus or dialogs.
// This is also required at classic dimensions when smart smoothing keeps the
// HUD in a separate overlay; nested UI scopes do not copy again.
class ScopedUiCanvas final
{
public:
    explicit ScopedUiCanvas(video& output)
        : output_(output), target_(output, CanvasTarget::UI),
          entered_from_world_(target_.previous() == CanvasTarget::World),
          entered_from_split_world_(
              entered_from_world_ &&
              (output.world_canvas_w() != kUiCanvasW ||
               output.world_canvas_h() != kUiCanvasH))
    {
        if (entered_from_world_)
            output_.prepare_ui_canvas_from_world();
    }

    ScopedUiCanvas(const ScopedUiCanvas&) = delete;
    ScopedUiCanvas& operator=(const ScopedUiCanvas&) = delete;
    ScopedUiCanvas(ScopedUiCanvas&&) = delete;
    ScopedUiCanvas& operator=(ScopedUiCanvas&&) = delete;

    bool entered_from_world() const { return entered_from_world_; }
    bool entered_from_split_world() const { return entered_from_split_world_; }

private:
    video& output_;
    ScopedCanvasTarget target_;
    bool entered_from_world_;
    bool entered_from_split_world_;
};

// Gameplay HUD entry point. When the backend prepared its fixed-size overlay,
// this selects it for compositing after the zoomed/filtered scenery. Otherwise
// GameplayUI aliases World, preserving classic and allocation-fallback paths.
class ScopedGameplayUiCanvas final
{
public:
    explicit ScopedGameplayUiCanvas(video& output)
        : target_(output, CanvasTarget::GameplayUI)
    {
    }

    ScopedGameplayUiCanvas(const ScopedGameplayUiCanvas&) = delete;
    ScopedGameplayUiCanvas& operator=(const ScopedGameplayUiCanvas&) = delete;
    ScopedGameplayUiCanvas(ScopedGameplayUiCanvas&&) = delete;
    ScopedGameplayUiCanvas& operator=(ScopedGameplayUiCanvas&&) = delete;

private:
    ScopedCanvasTarget target_;
};
