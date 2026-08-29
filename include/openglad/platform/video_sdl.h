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
#include <openglad/platform/display_state.h>

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef TESTING
namespace og::video_testing
{
// Fade-ownership invariants. Two are checked by sdl_video::fadeblack at the
// fade itself: a fade-in requires a black window ("fade-in without a
// fade-out"), and a fade-out requires the render buffer to equal the frame
// the window last showed ("fade-out from a frame that was never presented" —
// a clear or a stale redraw between the last present and the fade, or a draw
// no present's rect ever covered). The third is reported by the menu runner
// at a fading ENTRY that found the window not black ("entry found an unfaded
// window: the previous surface exited without its fade-out") — the entry
// still fades out, so production never hard-cuts, but under TESTING the
// missing exit fade is a failure. A violation traces ("video", "FADE
// VIOLATION: ..."), logs, and lands here. trace_clear() never touches this:
// the integration listener resets it at test start and fails the test at its
// end for each message recorded, so every flow test in the tree is an oracle
// for the class.
extern std::atomic<int> g_fade_violations;
std::vector<std::string> fade_violation_messages();
void reset_fade_violations();
void report_fade_violation(const char* what);
} // namespace og::video_testing
#endif

namespace og::platform
{
// SDL3 display-mode dimensions are logical coordinates. Convert them to the
// physical pixel pair users expect in a monitor-resolution selector.
std::pair<int, int> display_mode_pixel_size(const SDL_DisplayMode& mode);

// XRandR exclusive mode changes are unsafe when one X11 screen spans several
// displays: SDL can leave the target CRTC disabled if the resized root no
// longer contains the other outputs. Other backends do not use that path.
inline bool exclusive_mode_switch_is_safe(std::string_view video_driver,
                                          int display_count)
{
    return video_driver != "x11" || display_count <= 1;
}
}

class sdl_video final : public video
{
public:
    // Bounds each multifloor source surface to 16.4 MiB at ARGB8888. Larger
    // window/zoom combinations draw the faded floor directly instead of
    // retaining an unbounded compositor scratch surface.
    static constexpr std::int64_t kFloorLayerSourcePixelBudget = 4'096'000;

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
    void fail_next_floor_layer_allocation_for_testing() override;
    [[nodiscard]] int floor_layer_fallback_count_for_testing() const override;
    [[nodiscard]] std::int64_t floor_layer_source_pixels_for_testing() const override;
    [[nodiscard]] std::int64_t floor_layer_scaled_pixels_for_testing() const override;
    [[nodiscard]] bool floor_layer_redirect_active_for_testing() const override;
    bool camera_scale_begin(Sint32 w, Sint32 h) override;
    void camera_scale_end(Sint32 x, Sint32 y, Sint32 w, Sint32 h,
                          Sint32 denominator) override;
    void fail_next_camera_scale_allocation_for_testing() override;
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
    int gameplay_ui_canvas_w() const override;
    int gameplay_ui_canvas_h() const override;
    bool gameplay_ui_canvas_available() const override;
    void set_active_canvas(CanvasTarget target) override;
    CanvasTarget active_canvas() const override;
	CanvasTarget last_presented_canvas() const override;
    void begin_gameplay_frame() override;
    void prepare_ui_canvas_from_world() override;
    void set_world_canvas_pinned_classic(bool pinned) override;
    void reapply_world_scale() override;
    int minimum_world_zoom_steps() const override;
    bool world_smoothing_supported() const override;
    // §7.1 per-view zoom composition + partitioned presentation.
    void set_world_view_scale(int min_scale_num) override;
    bool world_zoom_composition_fits(int min_scale_num) const override;
    bool world_present_partition_supported() const override;
    bool world_canvas_pinned_classic() const override;
    void set_world_present_slices(
        std::span<const WorldPresentSlice> slices) override;
    void apply_display_settings_from_cfg() override;
    void reflect_display_settings_from_window(
        DisplayStateConfirmation confirmation = DisplayStateConfirmation::Synchronized,
        std::uint64_t event_timestamp_ns = 0) override;
    std::vector<std::pair<int, int>> display_resolutions() override;
    std::pair<int, int> desktop_resolution() override;
    std::pair<int, int> windowed_desktop_resolution() override;

    void get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b) override;
    int get_pixel(int x, int y, int* index) override;
    int get_pixel(int offset) override;

    bool save_screenshot() override;

    // Fading code: (thanks Erik!)
    void FadeBetween24(SDL_Surface* surface, const Uint8* from, const Uint8* to, int amount);
    int FadeBetween(SDL_Surface* old_surface, SDL_Surface* new_surface, SDL_Surface* dest_surface);
    void fade_between24(void* surface, const Uint8* from, const Uint8* to, int amount) override;
    int fade_between(void* old_surface, void* new_surface, void* dest_surface) override;
    int fadeblack(bool fade_in) override;
    bool window_is_black() override;
#ifdef TESTING
    void testing_reset_window_state() override;
    void testing_report_fade_violation(const char* what) override;
#endif

    std::array<unsigned char, 768>& ourpalette_ref() override { return ourpalette; }
    std::array<unsigned char, 768>& redpalette_ref() override { return redpalette; }
    std::array<unsigned char, 768>& bluepalette_ref() override { return bluepalette; }
    std::array<unsigned char, 768>& dospalette_ref() override { return dospalette; }
    std::vector<unsigned char>& videobuffer_ref() override { return videobuffer; }
    short& cyclemode_ref() override { return cyclemode; }
    text& text_normal_ref() override { return text_normal; }
    text& text_big_ref() override { return text_big; }

    int fadeDuration;

    // our standard glad palette
    std::array<unsigned char, 768> ourpalette{};
    std::array<unsigned char, 768> redpalette{};
    // for special effects like time-freeze
    std::array<unsigned char, 768> bluepalette{};
    std::array<unsigned char, 768> dospalette{};

    // Legacy scratch sized to the canvas area (kUiCanvasW*kUiCanvasH).
    std::vector<unsigned char> videobuffer =
        std::vector<unsigned char>(static_cast<std::size_t>(kUiCanvasW) *
                                       static_cast<std::size_t>(kUiCanvasH),
                                   0);
    // color cycling on or off
    short cyclemode = 0;

    //buffers: screen vars
    SDL_Surface* window = nullptr;
    int screen_width = 0, screen_height = 0, fullscreen = 0;
    int pdouble = 0;

    text text_normal;
    text text_big;

private:
    void persist_confirmed_display_state();
    og::platform::DisplayStateSnapshot confirmed_snapshot_from_window(
        DisplayStateConfirmation confirmation) const;

    bool owns_display_ = true;
    // Fullscreen mode dimensions are physical pixels, while SDL window sizes
    // are logical coordinates. Preserve the last real Windowed size across
    // Borderless/Exclusive transitions instead of reusing fullscreen cfg
    // pixels as an enormous logical window on HiDPI displays.
    int remembered_window_w_ = 640;
    int remembered_window_h_ = 400;
	og::platform::DisplayStateTracker display_state_;
	// Leaving fullscreen is asynchronous on some window systems, and SDL
	// ignores SDL_SetWindowSize while the window is still fullscreen. Retain
	// the requested logical restore until the completed LEAVE/RESIZED event.
	[[maybe_unused]] int pending_windowed_w_ = 0;
	[[maybe_unused]] int pending_windowed_h_ = 0;
	[[maybe_unused]] SDL_DisplayID pending_windowed_display_ = 0;

    // Off-screen compositing scratch for the multi-floor vertical parallax
    // (floor_layer_begin/floor_layer_end). Lazily created at the render size,
    // reused across frames, freed in the destructor. ARGB8888 (alpha-capable)
    // so un-drawn tile cells / air holes stay transparent and reveal the floors
    // below in the scaled composite.
    SDL_Surface* floor_layer_ = nullptr;         // transparent 1:1 draw target
    SDL_Surface* floor_layer_scaled_ = nullptr;  // bilinear-stretched scratch
    SDL_Surface* floor_layer_saved_render_ = nullptr; // E_Screen->render while redirected
    int floor_layer_reported_fallback_w_ = 0;
    int floor_layer_reported_fallback_h_ = 0;
    bool floor_layer_reported_budget_fallback_ = false;
    // Fault-injection/telemetry for the compositor tests. Only read and written
    // under TESTING, but declared unconditionally so the class layout is the
    // same in every build.
    [[maybe_unused]] bool fail_next_floor_layer_allocation_ = false;
    [[maybe_unused]] int floor_layer_fallback_count_ = 0;

    // Off-screen camera-pane downscale layer (camera_scale_begin/end): the
    // one-seat second-minimap 0.5-zoom draw target. Its OWN surface + saved-
    // render slot — never the floor layer's — so a multi-floor camera redraw
    // can still begin/end floor layers inside this redirect. Created in the
    // render surface's format (whole-pixel nearest sampling needs no
    // conversion), grow-only like the floor layer, freed in the destructor.
    SDL_Surface* camera_scale_layer_ = nullptr;
    SDL_Surface* camera_scale_saved_render_ = nullptr;
    [[maybe_unused]] bool fail_next_camera_scale_allocation_ = false;
};

// Installs cfg graphics/zoom + graphics/smoothing on the live display. The
// world canvas derives from a classic-density, aspect-matched baseline and
// zoom; smoothing selects its world-only present engine. Called at display
// creation and exposed so settings/tests can live-apply changes. This path
// also runs on Emscripten.
void apply_world_scale_from_cfg();

// SDL3's Emscripten resize callback can replace the browser-selected logical
// backing while a fullscreen exit is still in flight. Reassert the fitted CSS
// logical size (and its HiDPI physical backing) after browser events. Native
// builds retain this compatibility entry point as a strict no-op.
void restore_web_canvas_backing_size(int logical_w, int logical_h);

// Flag that the rendering device was lost and restored (the browser
// 'webglcontextrestored' event) and the renderer plus all GPU textures must
// be rebuilt. Only sets a pending flag: browser callbacks can run while the
// ASYNCIFY'd C stack is suspended inside a blocking menu loop, so no GL work
// may happen at the notification site. Screen::swap() consumes the flag and
// performs the recreate at the next present.
void request_render_backend_recreate();

// True while a requested render-backend recreate has not completed yet.
bool render_backend_recreate_pending();
