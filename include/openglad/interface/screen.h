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

#include <openglad/legacy/base.h> // NUM_FAMILIES/NUM_SPECIALS + legacy globals (transitional)
#include <openglad/platform/sound.h>
#include <openglad/interface/render/video.h>
#include <openglad/interface/render/text.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/runtime/level_runtime_data.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/resources/save_data.h>

#include <array>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

struct InputState;

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

    void get_pixel(int x, int y, Uint8* r, Uint8* g, Uint8* b) override;
    int get_pixel(int x, int y, int* index) override;
    int get_pixel(int offset) override;

    bool save_screenshot() override;

    void fade_between24(void* surface, const Uint8* from, const Uint8* to, int amount) override;
    int fade_between(void* old_surface, void* new_surface, void* dest_surface) override;
    int fadeblack(bool fade_in) override;
    std::array<unsigned char, 768>& ourpalette_ref() override { return ourpalette; }
    std::array<unsigned char, 768>& redpalette_ref() override { return redpalette; }
    std::array<unsigned char, 768>& bluepalette_ref() override { return bluepalette; }
    std::array<unsigned char, 768>& dospalette_ref() override { return dospalette; }
    std::array<unsigned char, 64000>& videobuffer_ref() override { return videobuffer; }
    short& cyclemode_ref() override { return cyclemode; }
    text& text_normal_ref() override { return text_normal; }
    text& text_big_ref() override { return text_big; }

    void initialize_views();
    void cleanup(short);
    void clear();
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    // Overloads to avoid implicit int->float conversions at call sites.
    bool query_passable(Sint32 x, Sint32 y, walker* ob) { return query_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool query_object_passable(Sint32 x, Sint32 y, walker* ob) { return query_object_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool query_grid_passable(Sint32 x, Sint32 y, walker* ob) { return query_grid_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool redraw();
    void refresh();
    walker* first_of(Order whatorder, unsigned char whatfamily, int team_num = -1);
    short input(const void* native_event);
    short continuous_input();
    void process_input(const InputState& input_state);
    bool act();

    short endgame(short ending);
    short endgame(short ending, short nextlevel); // what level next?
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    void draw_panels(short howmany);
    walker* find_nearest_blood(walker* who);
    walker* find_nearest_player(walker* ob);
    std::list<walker*> find_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    std::list<walker*> find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    std::list<walker*> find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    std::list<walker*> find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
    char damage_tile(short xloc, short yloc); // damage the specified tile
    void do_notify(std::string_view message, walker* who); // printing text
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
    auto& oblist() { return world_.oblist; }
    const auto& oblist() const { return world_.oblist; }
    auto& fxlist() { return world_.fxlist; }
    const auto& fxlist() const { return world_.fxlist; }
    auto& weaplist() { return world_.weaplist; }
    const auto& weaplist() const { return world_.weaplist; }
    auto& dead_list() { return world_.dead_list; }
    const auto& dead_list() const { return world_.dead_list; }
    int& living_count() { return world_.living_count; }
    const int& living_count() const { return world_.living_count; }

    // Delegated render data (preserves legacy field-style access).
    std::array<unsigned char, 768>& ourpalette;
    std::array<unsigned char, 768>& redpalette;
    std::array<unsigned char, 768>& bluepalette;
    std::array<unsigned char, 768>& dospalette;
    std::array<unsigned char, 64000>& videobuffer;
    short& cyclemode;
    text& text_normal;
    text& text_big;

    // General drawing data
    std::array<unsigned char, 768> newpalette{};
    short palmode;

    // Level data
    GameWorld& world_;
    // Forwarders to gameplay-owned game state.
    float& control_hp;
    char& end;
    signed char& timer_wait;
    short& level_done;
    bool& retry;
    Sint32& enemy_freeze;
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
    std::unique_ptr<viewscreen> viewob[5];
    short numviews;
    Uint32 timerstart;
    Uint32 framecount;

private:
    void init_common(short howmany, bool has_display);
};
