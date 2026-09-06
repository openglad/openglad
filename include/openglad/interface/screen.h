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

// Definition of SCREEN class

#include <openglad/interface/base.h> // shared interface-era type aliases/constants
#include <openglad/interface/render/video.h>
#include <openglad/interface/render/text.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/win_shares.h>

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct InputState;
class soundob;
class DamageNumberRenderContext;
namespace og::sim { class GameClient; }

// Maximum number of split-screen viewscreens (one per local player).
inline constexpr int MAX_VIEWS = 5;

// One inset camera pane rect on the GameplayUI canvas (docs/camera-views-
// design.md §6). The camera viewscreen stays singular; at two seats the SAME
// camera draws into two of these — one block above each seat's own radar —
// so the multiplicity lives purely in draw geometry.
struct CameraPaneRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const CameraPaneRect&) const = default;
};

class screen : public video
{
private:
    std::unique_ptr<video> video_impl_;

public:
    enum class ScenarioTitleError
    {
        None = 0,
        OpenReadFailed,
        InvalidHeader,
        UnsupportedVersion,
        ReadFailed
    };

    screen(GameWorld& world, std::unique_ptr<video> video_impl, short howmany, bool has_display);

    void reset(short howmany);
    void ready_for_battle(short howmany);
    ~screen() override;
    screen(const screen&) = delete;
    screen& operator=(const screen&) = delete;
    screen(screen&&) = delete;
    screen& operator=(screen&&) = delete;

    // Abstract video API delegation (interface base -> platform implementation).
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
    NativeWorldViewSource begin_native_world_view(
        std::span<const NativeWorldViewDestination> destinations) override;
    bool end_native_world_view() override;
    void cancel_native_world_view() override;
    bool native_world_view_active() const override
    {
        return video_impl_->native_world_view_active();
    }
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
    // Defaults preserve the classic unit ground shadow (half height, full
    // width); the upper-floor blob shadows pass a larger divisor + inset.
    void walkputbuffer_shadow(Sint32 walkerstartx, Sint32 walkerstarty,
                              Sint32 walkerwidth, Sint32 walkerheight,
                              Sint32 portstartx, Sint32 portstarty,
                              Sint32 portendx, Sint32 portendy,
                              std::span<const unsigned char> sourceptr, Uint8 alpha,
                              Sint32 height_divisor = 2, Sint32 inset = 0) override;
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
    // Default-mask convenience: reflects over the production reflective
    // tiles (reflective_tiles(): PIX_GLASS + pure water + lava/marsh).
    void walkputbuffer_reflect(Sint32 walkerstartx, Sint32 walkerstarty,
                               Sint32 walkerwidth, Sint32 walkerheight,
                               Sint32 portstartx, Sint32 portstarty,
                               Sint32 portendx, Sint32 portendy,
                               std::span<const unsigned char> sourceptr,
                               unsigned char teamcolor, Uint8 alpha,
                               std::span<const unsigned char> grid,
                               Sint32 gridw, Sint32 gridh,
                               Sint32 world_offset_x, Sint32 world_offset_y);

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
    void draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h) override;
    void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border) override;
    void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer) override;
    void draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2,
                             bool use_border, int base_color, int high_color = 15, int shadow_color = 11) override;
    Sint32 draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, const char* header) override;
    void draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2) override;

    void darken_screen() override;

    void swap() override;

    // Re-derives every live viewscreen's geometry from the current world
    // canvas dims (after a canvas resize) and flags a redraw.
    void relayout_views();

    // Canvas routing (world/UI split) — forwarded to the platform video.
    int canvas_w() const override { return video_impl_->canvas_w(); }
    // Pins the world canvas to the classic 320x200 dims (true) or restores
    // the cfg graphics/zoom-derived dims (false) — used by the level editor,
    // whose panel chrome still assumes classic coordinates.
    void set_world_canvas_pinned_classic(bool pinned) override { video_impl_->set_world_canvas_pinned_classic(pinned); }
    int canvas_h() const override { return video_impl_->canvas_h(); }
    int world_canvas_w() const override { return video_impl_->world_canvas_w(); }
    int world_canvas_h() const override { return video_impl_->world_canvas_h(); }
    int gameplay_ui_canvas_w() const override { return video_impl_->gameplay_ui_canvas_w(); }
    int gameplay_ui_canvas_h() const override { return video_impl_->gameplay_ui_canvas_h(); }
    bool gameplay_ui_canvas_available() const override
    {
        return video_impl_->gameplay_ui_canvas_available();
    }
    void set_active_canvas(CanvasTarget target) override { video_impl_->set_active_canvas(target); }
    CanvasTarget active_canvas() const override { return video_impl_->active_canvas(); }
	CanvasTarget last_presented_canvas() const override { return video_impl_->last_presented_canvas(); }
    void begin_gameplay_frame() override { video_impl_->begin_gameplay_frame(); }
    void prepare_ui_canvas_from_world() override { video_impl_->prepare_ui_canvas_from_world(); }
    // Re-reads cfg graphics/zoom + graphics/smoothing into the live world
    // canvas (the DISPLAY selectors / RESTORE SETTINGS live-apply path).
    void reapply_world_scale() override { video_impl_->reapply_world_scale(); }
    int minimum_world_zoom_steps() const override
    {
        return video_impl_->minimum_world_zoom_steps();
    }
    // §7.1 per-view zoom composition + partitioned presentation: forwarded
    // to the platform video (defaults are inert for headless backends).
    void set_world_view_scale(int min_scale_num) override
    {
        video_impl_->set_world_view_scale(min_scale_num);
    }
    bool world_zoom_composition_fits(int min_scale_num) const override
    {
        return video_impl_->world_zoom_composition_fits(min_scale_num);
    }
    bool world_present_partition_supported() const override
    {
        return video_impl_->world_present_partition_supported();
    }
    bool world_canvas_pinned_classic() const override
    {
        return video_impl_->world_canvas_pinned_classic();
    }
    void set_world_present_slices(
        std::span<const WorldPresentSlice> slices) override
    {
        video_impl_->set_world_present_slices(slices);
    }
    // Deepest per-view zoom scale over the live views (tenths; 10 when no
    // view carries an override) — the value relayout_views composes into the
    // canvas derivation.
    int min_view_zoom_scale_num() const;
    bool world_smoothing_supported() const override
    {
        return video_impl_->world_smoothing_supported();
    }
    void apply_display_settings_from_cfg() override { video_impl_->apply_display_settings_from_cfg(); }
    void reflect_display_settings_from_window(
        DisplayStateConfirmation confirmation = DisplayStateConfirmation::Synchronized,
        std::uint64_t event_timestamp_ns = 0) override
    {
        video_impl_->reflect_display_settings_from_window(
            confirmation, event_timestamp_ns);
    }
    std::vector<std::pair<int, int>> display_resolutions() override { return video_impl_->display_resolutions(); }
    std::pair<int, int> desktop_resolution() override { return video_impl_->desktop_resolution(); }
    std::pair<int, int> windowed_desktop_resolution() override { return video_impl_->windowed_desktop_resolution(); }

    void get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b) override;
    int get_pixel(int x, int y, int* index) override;
    int get_pixel(int offset) override;

    bool save_screenshot() override;

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

    void initialize_views();
    void cleanup(short);
    void clear();
    bool redraw();
    void refresh();
    walker* first_of(Order whatorder, unsigned char whatfamily, int team_num = -1);
    short input(const void* native_event);
    short continuous_input();
    void process_input(const InputState& input_state);
    using TickWorldBatches =
        std::pair<og::sim::SimEventBatch, og::sim::GameFlowEventBatch>;
    [[nodiscard]] TickWorldBatches tick_world();
    void dispatch_cosmetic_events(const og::sim::SimEventBatch& batch);
    bool dispatch_game_flow_events(const og::sim::GameFlowEventBatch& batch);
    bool dispatch_sim_event_batch(const og::sim::SimEventBatch& batch);

    short endgame(short ending);
    short endgame(short ending, short nextlevel); // what level next?
    void draw_panels(short howmany);
    void draw_panel_chrome(short howmany);
    char damage_tile(short xloc, short yloc); // damage the specified tile
    void do_notify(std::string_view message, walker* who); // printing text
    // Wipe every live view's message feed. Called by each (re)launcher of a
    // level so no line survives into a world whose tick clock just restarted
    // at 0 (#246).
    void clear_all_view_text();
    void report_mem();
    walker* set_walker(walker* ob, Order order, Sint32 family);
    ScenarioTitleError get_scen_title_with_error(const char* filename, std::string& out_title);
    const char* get_scen_title(const char* filename, screen* master);
    void sync_world_from_save_data();
    void sync_save_data_from_world();
    bool is_level_completed(int level_index) const;
    int get_num_levels_completed(const std::string& campaign) const;
    void add_level_completed(const std::string& campaign, int level_index);
    bool load_level();
    bool save_level();
    LevelRuntimeData::IoError load_level_with_error();
    LevelRuntimeData::IoError save_level_with_error();
    LevelRuntimeData::IoError level_io_error() const { return level_runtime_data_.last_io_error(); }
    std::string& level_grid_file() { return level_runtime_data_.grid_file; }
    const std::string& level_grid_file() const { return level_runtime_data_.grid_file; }
    std::list<std::string>& level_description() { return level_runtime_data_.description; }
    const std::list<std::string>& level_description() const { return level_runtime_data_.description; }
    std::string get_level_description_line(int i) const { return level_runtime_data_.get_description_line(i); }
    void set_level_draw_pos(std::int32_t new_topx, std::int32_t new_topy) { level_runtime_data_.set_draw_pos(new_topx, new_topy); }
    void add_level_draw_pos(std::int32_t dx, std::int32_t dy) { level_runtime_data_.add_draw_pos(dx, dy); }
    void draw_level(screen* scr = nullptr) { level_runtime_data_.draw(scr ? scr : this); }
    LevelRuntimeData& level_runtime_data() { return level_runtime_data_; }
    const LevelRuntimeData& level_runtime_data() const { return level_runtime_data_; }
    GameWorld& world() { return world_; }
    const GameWorld& world() const { return world_; }
    LevelVisuals& level_visuals() { return level_visuals_; }
    const LevelVisuals& level_visuals() const { return level_visuals_; }
    void set_render_interpolation_client(
        const og::sim::GameClient* client) noexcept;
    [[nodiscard]] const og::sim::GameClient* render_interpolation_client()
        const noexcept
    {
        return render_interpolation_client_;
    }
    void set_render_interpolation_speed_factor(float speed_factor) noexcept;
    [[nodiscard]] float render_interpolation_speed_factor() const noexcept
    {
        return render_interpolation_speed_factor_;
    }
    DamageNumberRenderContext& damage_number_render_context() noexcept;
    const DamageNumberRenderContext& damage_number_render_context() const
        noexcept;

    // Delegated render data (preserves legacy field-style access).
    std::array<unsigned char, 768>& ourpalette;
    std::array<unsigned char, 768>& redpalette;
    std::array<unsigned char, 768>& bluepalette;
    std::array<unsigned char, 768>& dospalette;
    std::vector<unsigned char>& videobuffer;
    short& cyclemode;
    text& text_normal;
    text& text_big;

    // General drawing data
    std::array<unsigned char, 768> newpalette{};
    short palmode;

    // Level data
    GameWorld& world_;
    // Platform-owned entity loader (wired at setup time).
    loader* myloader;
    LevelVisuals level_visuals_;
    LevelRuntimeData level_runtime_data_;

    // Save data
    SaveData save_data;

    std::string special_name[NUM_FAMILIES][NUM_SPECIALS];
    std::string alternate_name[NUM_FAMILIES][NUM_SPECIALS];
    std::unique_ptr<soundob> soundp;
    short redrawme;
    std::unique_ptr<viewscreen> viewob[MAX_VIEWS];
    short numviews;
    // Camera view (docs/camera-views-design.md §4): engine-owned, display-side
    // only, NOT a seat. Lives outside viewob[]/numviews by design — no seat
    // loop (input dispatch, HUD density, seat claiming/surgery, results
    // classification, pause seats) can reach it.
    std::unique_ptr<viewscreen> camera_view_;
    std::int32_t camera_entity_id_ = 0;   // last-synced declaration
    std::uint8_t camera_style_ = 0;       // kCameraStyleAuto / kCameraStyleInset
    bool camera_docked_ = false;          // per-machine resolution (§6), off-wire
    // The inset pane rects (§6), resolved with the geometry in
    // relayout_camera_view: one centered rect at 4 seats, one second-minimap
    // block at 1 seat, TWO blocks at 2 seats (one above each seat's radar).
    // Empty while docked or with no camera. Outside a draw the camera
    // viewscreen sits on the first rect; draw_camera_view_ui walks the list,
    // the border draws per rect, and every structural transition scrubs every
    // previous rect.
    std::vector<CameraPaneRect> camera_pane_rects_;
    // Camera-view lifecycle (§5): one idempotent, diff-based pass, run as the
    // first statement of redraw() — construct on change, retarget every
    // frame, destroy on a cleared slot. Display screens only (identity belt).
    void sync_camera_views();
    // Derived camera geometry: the docked quadrant through the one
    // compute_view_layout pipeline, the centered GameplayUI inset rect at 2
    // and 4 seats, or the second minimap above the radar block at 1 seat.
    // (Amended by the two-seat ruling: 2 seats now take a second-minimap
    // block per seat; only 4 seats keep the centered rect. One function, one
    // switch over the local seat count — the whole inset style rule.)
    void relayout_camera_view();
    // The near-minimap block for ONE seat: the radar's compact footprint,
    // mirrored above it through the shared radar placement rule (§6).
    // Used for seat 0 at 1 seat and both seats at 2 seats.
    CameraPaneRect camera_minimap_block_for_seat(int seat, int ui_w,
                                                 int ui_h) const;
    // Docked pane world pixels + its own bevel on GameplayUI; no-op unless a
    // docked camera is live. draw_panel_chrome stays untouched (constraint 7).
    void draw_camera_view_world();
    // Inset camera draw at the game_loop seams (§6): world content via the
    // staged-preview draw mechanism + 1px border on the GameplayUI canvas.
    void draw_camera_view_ui();
    // Stale-pixel rule (§6): scrub an abandoned inset rect (+ border ring)
    // on the GameplayUI canvas — it persists across frames on the classic
    // alias path — and set redrawme.
    void clear_camera_inset_rect(int x, int y, int w, int h);
    // The same scrub over a whole rect list (every block at 2 seats).
    void clear_camera_pane_rects(const std::vector<CameraPaneRect>& rects);
    // Pane count the LAYOUT consumes: the human seats plus the docked camera.
    // With no docked camera this is numviews exactly — the byte-identical
    // OFF state at every call site (constraint 7).
    int layout_pane_count() const { return numviews + (camera_docked_ ? 1 : 0); }
    Uint32 timerstart;
    Uint32 framecount;
    const og::sim::GameClient* render_interpolation_client_ = nullptr;
    float render_interpolation_speed_factor_ = 1.0f;
    std::unique_ptr<DamageNumberRenderContext> damage_number_render_context_;

    // One-shot "the way is clear" announcement latch (bug B4). Render-only
    // state fed by redraw() from the world's level_done (which the sim
    // recomputes every tick and snapshots sync to network mirrors): when it
    // transitions 0 -> 1 (live hostiles cleared, a live exit present), each
    // view gets a one-time on-screen notice. Never written back to the sim.
    int way_clear_level_id_ = -1;
    short way_clear_last_level_done_ = -1;
    bool way_clear_announced_ = false;
    void announce_way_clear_if_needed();

    // §4.6 networked money split: screen::endgame folds the display screen's
    // save inside event dispatch and latches the win capture (pre-fold deploy
    // roster + the fold's applied per-team deltas) here; the client's
    // out-of-dispatch persist reads it to size this machine's share. Re-armed
    // (overwritten) on every networked win; unused on local/solo play.
    std::optional<og::progression::NetWinFoldCapture> pending_net_win_capture_;

private:
    void init_common(short howmany, bool has_display);
};
