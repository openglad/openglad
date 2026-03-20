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

#include <openglad/core/version.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/util.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/interface/render/walker_draw.h>

#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/interface/ui/picker_common.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "picker_sdl_defs.h"

#define DOWN(x) (72 + static_cast<Sint32>((x) * 15))
#define VIEW_DOWN(x) (10 + static_cast<Sint32>((x) * 20))


static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

struct UiRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Sync current_guy from the active session's state.
static void sync_current_guy_from_hire()
{
    if (pks().hire_session && pks().hire_session->current_recruit())
        og::runtime::current_session->current_guy_ = std::make_unique<guy>(*pks().hire_session->current_recruit());
}

static void sync_current_guy_from_train()
{
    if (pks().train_session && !pks().train_session->empty()) {
        og::runtime::current_session->current_guy_ = std::make_unique<guy>(pks().train_session->working_copy());
        pks().old_guy = &const_cast<guy&>(pks().train_session->original());
        og::runtime::current_session->editguy_ = pks().train_session->current_slot();
    }
}
#ifdef __EMSCRIPTEN__
void picker_request_start_game();
#endif
#ifdef TESTING
void picker_testing_mark_game_start();
void picker_testing_mark_game_end();
#endif


bool prompt_for_string(const std::string& message, std::string& result);
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void timed_dialog(const char* message, float delay_seconds = 3.0f);
void show_guy(Sint32 frames, Sint32 who, Sint32 centerx = 80, Sint32 centery = 45);
void view_team(short left, short top, short right, short bottom);
void draw_backdrop();
Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
void draw_highlight_interior(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons, Sint32& retvalue);
const char* family_name_copy(short family);
const char* get_family_string(Sint32 family);
std::string get_saved_name(const char* filename);
Sint32 create_view_menu(Sint32 arg1);
Sint32 create_hire_menu(Sint32 arg1);
Sint32 cycle_guy(Sint32 whichway);
Sint32 cycle_team_guy(Sint32 whichway);
Sint32 set_difficulty();
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
Sint32 create_detail_menu(guy *arg1);
void glad_main(Sint32 playermode);
void statscopy(guy *dest, guy *source);
void picker_lobby_initialize_from_save();
void picker_reinitialize_lobby_after_game();
void picker_lobby_sync_roster_from_save();
void picker_lobby_sync_settings_from_save();
void picker_lobby_poll();
bool picker_lobby_request_start();
bool picker_lobby_start_request_pending();
extern bool g_start_game_requested;

static void show_need_team_to_train_popup()
{
    popup_dialog("NEED A TEAM!", "You need to\nhire a team\nto train");
}

static bool save_has_trainable_team_member(const SaveData& save)
{
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[i] && picker_lobby_save_slot_editable(i))
            return true;
    }
    return false;
}

void picker_prepare_async_team_build_start_request()
{
    picker_lobby_sync_settings_from_save();
    picker_lobby_sync_roster_from_save();

    const bool start_already_requested =
        g_start_game_requested && picker_lobby_has_game_start_config();
    if (start_already_requested)
        return;

    g_start_game_requested = false;
#ifdef __EMSCRIPTEN__
    picker_request_start_game();
#else
    (void)picker_lobby_request_start();
#endif
}

static bool team_build_remote_start_requested(Sint32& retvalue)
{
    if (!g_start_game_requested || !picker_lobby_has_game_start_config())
        return false;

    pks().selected_menu_item = og::ui::find_picker_menu_item(
        og::ui::PickerMenuId::TeamBuild,
        og::ui::PickerMenuCommand::StartGame);
    retvalue = MENU_EXIT;
    return true;
}

static bool team_build_start_selected()
{
    return pks().selected_menu_item != nullptr
        && pks().selected_menu_item->command ==
            og::ui::PickerMenuCommand::StartGame;
}

constexpr int kTeamBuildGoButtonIndex = 5;
constexpr int kTeamBuildSetLevelButtonIndex = 8;
constexpr int kTeamBuildSetCampaignButtonIndex = 9;
constexpr int kViewTeamGoButtonIndex = 0;

void sync_button_hidden_state(const button* buttons, int button_index)
{
    if (buttons == nullptr || button_index < 0)
        return;
    if (og::runtime::current_session->allbuttons_[button_index] == nullptr)
        return;

    og::runtime::current_session->allbuttons_[button_index]->hidden =
        buttons[button_index].hidden;
}

void ensure_highlighted_button_visible(const button* buttons,
                                       int num_buttons,
                                       int& highlighted_button)
{
    if (buttons == nullptr || num_buttons <= 0)
        return;

    if (highlighted_button >= 0 && highlighted_button < num_buttons &&
        !buttons[highlighted_button].hidden)
    {
        return;
    }

    for (int index = 0; index < num_buttons; ++index)
    {
        if (!buttons[index].hidden)
        {
            highlighted_button = index;
            return;
        }
    }

    highlighted_button = 0;
}

void sync_team_build_host_control_visibility(button* buttons,
                                             int num_buttons,
                                             int& highlighted_button)
{
    if (buttons == nullptr || num_buttons <= kTeamBuildSetCampaignButtonIndex)
        return;

    const bool host_controls_visible = picker_lobby_host_controls_visible();
    buttons[kTeamBuildGoButtonIndex].hidden = !host_controls_visible;
    buttons[kTeamBuildSetLevelButtonIndex].hidden = !host_controls_visible;
    buttons[kTeamBuildSetCampaignButtonIndex].hidden = !host_controls_visible;

    sync_button_hidden_state(buttons, kTeamBuildGoButtonIndex);
    sync_button_hidden_state(buttons, kTeamBuildSetLevelButtonIndex);
    sync_button_hidden_state(buttons, kTeamBuildSetCampaignButtonIndex);
    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);
}

void sync_view_team_host_control_visibility(button* buttons,
                                            int num_buttons,
                                            int& highlighted_button)
{
    if (buttons == nullptr || num_buttons <= kViewTeamGoButtonIndex)
        return;

    buttons[kViewTeamGoButtonIndex].hidden =
        !picker_lobby_host_controls_visible();
    sync_button_hidden_state(buttons, kViewTeamGoButtonIndex);
    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);
}

// Per-session picker message buffer: access via current_session->message_.

#define STAT_NUM_OFFSET 42
#define STAT_COLOR   DARK_BLUE // color for normal stat text
#define STAT_CHANGED RED       // color for changed stat text
#define STAT_LEVELED LIGHT_BLUE   // color for leveled up stat text
#define STAT_DISABLED BLACK   // color for disabled stat text
#define STAT_DERIVED DARK_BLUE + 3

// Compute derived stats for a guy using the current screen's loader data.
static og::ui::DerivedStats compute_guy_derived_stats(const guy& g)
{
    auto pix = PIX(Order::Living, g.family);
	return og::ui::compute_derived_stats(g,
	    og::runtime::current_session->myscreen_->myloader->hitpoints[pix],
	    og::runtime::current_session->myscreen_->myloader->damage[pix],
	    og::runtime::current_session->myscreen_->myloader->stepsizes[pix],
	    og::runtime::current_session->myscreen_->myloader->fire_frequency[pix]);
}

// Draw the HP/MP/ATK/DEF/SPD/ATK_SPD derived stats block.
// y_fn(line) returns the y coordinate for the given line number.
template <typename YFn>
static void draw_derived_stats_block(text& mytext, const og::ui::DerivedStats& ds,
    int x, int derived_offset, unsigned char value_color,
    YFn y_fn, int& line)
{
    mytext.write_xy(x, y_fn(line), "HP:", STAT_DERIVED, 1);
    mytext.write_xy(x + derived_offset - 9, y_fn(line), HIGH_HP_COLOR, "%.0f", ds.hp);
    mytext.write_xy(x + derived_offset + 18, y_fn(line), "MP:", STAT_DERIVED, 1);
    mytext.write_xy(x + 2*derived_offset + 18 - 9, y_fn(line), MAX_MP_COLOR, "%.0f", ds.mp);

    line++;
    mytext.write_xy(x, y_fn(line), "ATK:", STAT_DERIVED, 1);
    mytext.write_xy(x + derived_offset - 3, y_fn(line), value_color, "%.0f", ds.atk);
    mytext.write_xy(x + derived_offset + 18, y_fn(line), "DEF:", STAT_DERIVED, 1);
    mytext.write_xy(x + 2*derived_offset + 18 - 3, y_fn(line), value_color, "%.0f", ds.def);

    line++;
    mytext.write_xy(x, y_fn(line), "SPD:", STAT_DERIVED, 1);
    mytext.write_xy(x + derived_offset, y_fn(line), value_color, "%.1f", ds.spd);

    line++;
    mytext.write_xy(x, y_fn(line), "ATK SPD:", STAT_DERIVED, 1);
    mytext.write_xy(x + derived_offset + 21, y_fn(line), value_color, "%.1f", ds.atk_spd);
}

Sint32 create_team_menu(Sint32 arg1)
{
	Sint32 retvalue=0;

	if (arg1 == 1)
    {
        // Go straight to the hiring screen if we just started a new game.
        retvalue = create_hire_menu(arg1);
    }

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

	og::runtime::current_session->myscreen_->fadeblack(0);
	
	text& mytext = og::runtime::current_session->myscreen_->text_normal;
	
	button* buttons = picker_createmenu_buttons();
	int num_buttons = picker_createmenu_button_count();
	int highlighted_button = 1;
    sync_team_build_host_control_visibility(
        buttons, num_buttons, highlighted_button);
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
	draw_backdrop();
	draw_buttons(buttons, num_buttons);
	
	int last_level_id = -1;
	
	og::runtime::current_session->myscreen_->fadeblack(1);
	
	while ( !(retvalue & MENU_EXIT) )
	{
        picker_lobby_poll();
        sync_team_build_host_control_visibility(
            buttons, num_buttons, highlighted_button);
        if (team_build_remote_start_requested(retvalue))
            break;
	    // Input
		if(leftmouse(buttons))
			retvalue = og::runtime::current_session->localbuttons_->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        
        // Reset buttons
        bool buttons_were_reset = reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);

        // Nested menus can replace the global vbutton array with a different
        // layout before returning MENU_EXIT. Avoid drawing with mismatched arrays.
        if (retvalue & MENU_EXIT)
            break;
		
        if(last_level_id != og::runtime::current_session->myscreen_->save_data.scen_num || buttons_were_reset)
        {
            retvalue = 0;
            last_level_id = og::runtime::current_session->myscreen_->save_data.scen_num;
            og::runtime::current_session->myscreen_->world().id = last_level_id;
            og::runtime::current_session->myscreen_->load_level();
        }
        
		// Draw
		og::runtime::current_session->myscreen_->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);

        const std::vector<std::string> lobby_status = picker_lobby_status_lines();
        for (std::size_t line_index = 0;
             line_index < lobby_status.size() && line_index < 2;
             ++line_index)
        {
            const std::string& line = lobby_status[line_index];
            if (line.empty())
                continue;

            const int status_y = 8 + static_cast<int>(line_index) * 10;
            const int status_w = static_cast<int>(line.size()) * 6;
            og::runtime::current_session->myscreen_->draw_rect_filled(
                10,
                status_y - 1,
                status_w + 4,
                8,
                PURE_BLACK,
                150);
            mytext.write_xy(12, status_y, WHITE, "%s", line.c_str());
        }
        
        // Level name
        int len = static_cast<int>(og::runtime::current_session->myscreen_->world().title.size());
        og::runtime::current_session->myscreen_->draw_rect_filled(buttons[7].x + buttons[7].sizex - 6*len - 2, buttons[7].y - 8 - 1, 6*len + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(buttons[7].x + buttons[7].sizex - 6*len, buttons[7].y - 8, WHITE, "%s", og::runtime::current_session->myscreen_->world().title.c_str());
        // Campaign name
        len = static_cast<int>(og::runtime::current_session->myscreen_->save_data.current_campaign.size());
        og::runtime::current_session->myscreen_->draw_rect_filled(buttons[8].x + buttons[8].sizex - 6*len - 2, buttons[8].y - 8 - 1, 6*len + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(buttons[8].x + buttons[8].sizex - 6*static_cast<int>(og::runtime::current_session->myscreen_->save_data.current_campaign.size()), buttons[8].y - 8, WHITE, "%s", og::runtime::current_session->myscreen_->save_data.current_campaign.c_str());
        
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        og::input_native::sleep_ms(10);
	}

	// Propagate MENU_EXIT if that's why we left the loop
	if (retvalue & MENU_EXIT)
		return retvalue;

	return MENU_REDRAW;
}

Sint32 create_view_menu(Sint32 arg1)
{
	Sint32 retvalue = 0;

	if (arg1)
		arg1 = 1;

	og::runtime::current_session->myscreen_->clearbuffer();

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

	button* buttons = picker_viewteam_buttons();
	int num_buttons = picker_viewteam_button_count();
	int highlighted_button = 1;
    sync_view_team_host_control_visibility(
        buttons, num_buttons, highlighted_button);
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

	while ( !(retvalue & MENU_EXIT) )
	{
        picker_lobby_poll();
        sync_view_team_host_control_visibility(
            buttons, num_buttons, highlighted_button);
        if (team_build_remote_start_requested(retvalue))
            break;
	    // Input
		if(leftmouse(buttons))
			retvalue = og::runtime::current_session->localbuttons_->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // BACK returns MENU_REDRAW to signal "go back to team menu".
        // Check before reset_buttons can clear it.
        if (retvalue & MENU_REDRAW)
            break;

        // Reset buttons (relevant after go_menu returns from game)
        reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);

		// Draw
		og::runtime::current_session->myscreen_->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        view_team(5,5,314, 160);
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        og::input_native::sleep_ms(10);
	}
	og::runtime::current_session->myscreen_->clearbuffer();

	// Propagate MENU_EXIT so TeamBuild interception can map GO -> StartGame.
	// BACK returns MENU_REDRAW to keep parent create_team_menu running.
	if (retvalue & MENU_EXIT)
		return retvalue;

	return MENU_REDRAW;
}

// Helper struct for progress menu
struct LevelProgress {
    int id;
    std::string title;
    int num_enemies;
    bool is_cleared;
    bool is_current;
};

std::vector<int> get_accessible_levels();

Sint32 create_progress_menu(Sint32 arg1)
{
    Sint32 retvalue = 0;
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    if (arg1)
        arg1 = 1;

    og::runtime::current_session->myscreen_->clearbuffer();

	    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

    // Get accessible levels
    std::vector<int> level_ids = get_accessible_levels();
    std::vector<LevelProgress> levels;

    // Count cleared levels
    int num_cleared = 0;

    // Load level info for each accessible level
    for (int level_id : level_ids) {
        LevelProgress lp;
        lp.id = level_id;
        lp.is_cleared = og::runtime::current_session->myscreen_->save_data.is_level_completed(level_id);
        lp.is_current = (level_id == og::runtime::current_session->myscreen_->save_data.scen_num);

        if (lp.is_cleared)
            num_cleared++;

        // Load level to get title and enemy count
        LevelRuntimeData ld(level_id);
        if (ld.load()) {
            if (ld.world().title.size() > 20) {
                lp.title = ld.world().title.substr(0, 17) + "...";
            } else {
                lp.title = ld.world().title;
            }

            // Count enemies
            int num_enemies = 0;
            std::list<int> unused_exits;
            getLevelStats(ld, nullptr, nullptr, &num_enemies, nullptr, unused_exits);
            lp.num_enemies = lp.is_cleared ? 0 : num_enemies;
        } else {
            lp.title = std::format("Level {}", level_id);
            lp.num_enemies = 0;
        }

        levels.push_back(lp);
    }

    // Scrolling state
    int scroll_offset = 0;
    int visible_rows = 10;

    // Buttons
    UiRect prev_btn = {30, 170, 40, 20};
    UiRect next_btn = {80, 170, 40, 20};
    UiRect back_btn = {260, 170, 50, 20};

    button buttons[] = {
        button("prev", "PREV", KEYSTATE_UNKNOWN, prev_btn.x, prev_btn.y, prev_btn.w, prev_btn.h, 0, -1, MenuNav{.right=1}),
        button("next", "NEXT", KEYSTATE_UNKNOWN, next_btn.x, next_btn.y, next_btn.w, next_btn.h, 0, -1, MenuNav{.left=0, .right=2}),
        button("back", "BACK", KEYSTATE_ESCAPE, back_btn.x, back_btn.y, back_btn.w, back_btn.h, button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{.left=1}),
    };
    int num_buttons = 3;
    int highlighted_button = 2;
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

    while (!(retvalue & MENU_EXIT))
    {
        picker_lobby_poll();
        if (team_build_remote_start_requested(retvalue))
            break;
        // Input
        if (leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Handle scroll buttons
        MouseState& mymouse = query_mouse();
        bool clicked = mymouse.left;
        const int mx = static_cast<int>(mymouse.x);
        const int my = static_cast<int>(mymouse.y);
        if (clicked) {
            while (mymouse.left) {
                picker_lobby_poll();
                if (team_build_remote_start_requested(retvalue))
                    return retvalue;
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
        }

        bool prev_enabled = (scroll_offset > 0);
        bool next_enabled = (scroll_offset + visible_rows < static_cast<int>(levels.size()));

        bool do_prev = prev_enabled && ((clicked && prev_btn.x <= mx && mx <= prev_btn.x + prev_btn.w
                       && prev_btn.y <= my && my <= prev_btn.y + prev_btn.h)
                       || (retvalue == MENU_OK && highlighted_button == 0));
        bool do_next = next_enabled && ((clicked && next_btn.x <= mx && mx <= next_btn.x + next_btn.w
                       && next_btn.y <= my && my <= next_btn.y + next_btn.h)
                       || (retvalue == MENU_OK && highlighted_button == 1));

        if (do_prev) {
            scroll_offset--;
            retvalue = 0;
        }
        if (do_next) {
            scroll_offset++;
            retvalue = 0;
        }

        // Check for GO button clicks on level rows
        if (clicked) {
            int row_y = 36;
            int row_height = 13;
            int go_btn_x = 295;
            int go_btn_w = 20;
            for (int i = scroll_offset; i < static_cast<int>(levels.size()) && i < scroll_offset + visible_rows; i++) {
                LevelProgress& lp = levels[i];
                if (!lp.is_cleared) {
                    // Check if click is on this row's GO button
                    if (mx >= go_btn_x && mx <= go_btn_x + go_btn_w &&
                        my >= row_y && my <= row_y + row_height) {
                        // Set current level and exit
                        og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(lp.id);
                        picker_lobby_sync_settings_from_save();
                        og::runtime::current_session->myscreen_->clearbuffer();
                        return MENU_REDRAW;
                    }
                }
                row_y += row_height;
            }
        }

        // Reset
        if (retvalue == MENU_OK && highlighted_button != 2)
            retvalue = 0;

        // Draw
        og::runtime::current_session->myscreen_->clearbuffer();

        // Header
        std::string header = std::format("Level Progress: {} cleared of {} discovered",
                 num_cleared, static_cast<int>(levels.size()));
        mytext.write_xy(160 - static_cast<int>(header.size()) * 3, 8, header.c_str(), DARK_GREEN, 1);

        // Column headers
        og::runtime::current_session->myscreen_->draw_text_bar(10, 22, 310, 32);
        mytext.write_xy(12, 24, "ID", DARK_BLUE, 1);
        mytext.write_xy(36, 24, "Status", DARK_BLUE, 1);
        mytext.write_xy(100, 24, "Title", DARK_BLUE, 1);
        mytext.write_xy(250, 24, "Foes", DARK_BLUE, 1);

        // Level rows
        int y = 36;
        int row_height = 13;
        for (int i = scroll_offset; i < static_cast<int>(levels.size()) && i < scroll_offset + visible_rows; i++) {
            LevelProgress& lp = levels[i];

            // ID
            std::string buf = std::format("{}", lp.id);
            mytext.write_xy(12, y + 2, buf.c_str(), WHITE, 1);

            // Status
            unsigned char status_color;
            const char* status_text;
            if (lp.is_cleared) {
                status_text = "CLEARED";
                status_color = DARK_GREEN;
            } else if (lp.is_current) {
                status_text = "CURRENT";
                status_color = YELLOW;
            } else {
                status_text = "-------";
                status_color = WHITE;
            }
            mytext.write_xy(36, y + 2, status_text, status_color, 1);

            // Title
            mytext.write_xy(100, y + 2, lp.title.c_str(), WHITE, 1);

            // Enemy count
            if (lp.is_cleared) {
                mytext.write_xy(258, y + 2, "0", DARK_GREEN, 1);
            } else {
                buf = std::format("{}", lp.num_enemies);
                mytext.write_xy(258, y + 2, buf.c_str(), WHITE, 1);

                // GO button for non-cleared levels (right edge aligns with BACK button at x=310)
                og::runtime::current_session->myscreen_->draw_button(292, y + 1, 310, y + 10, 1, 1);
                mytext.write_xy(296, y + 2, "GO", DARK_BLUE, 1);
            }

            y += row_height;
        }

        // Scroll indicator
        if (levels.size() > (size_t)visible_rows) {
            std::string scroll_info = std::format("{}-{} of {}",
                     scroll_offset + 1,
                     std::min(scroll_offset + visible_rows, static_cast<int>(levels.size())),
                     static_cast<int>(levels.size()));
            mytext.write_xy(140, 172, scroll_info.c_str(), WHITE, 1);
        }

        // Buttons
        draw_buttons(buttons, num_buttons);
        draw_highlight(buttons[highlighted_button]);

        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
    }

    og::runtime::current_session->myscreen_->clearbuffer();
    return MENU_REDRAW;
}

std::string get_class_description(unsigned char family)
{
    const auto* fd = get_family_descriptor(family);
    if (fd && fd->description)
        return fd->description;
    return {};
}

// stat: str 0, dex 1, con 2, int 3, armor 4
	const char* get_training_cost_rating(unsigned char family, int stat)
	{
	    const auto* fd = get_family_descriptor(family);
	    if (!fd) return "";
	    int value = 55/(fd->stat_costs[stat]);
	    int rating = (value * 5) / 11;
	    switch(rating)
	    {
    case 0:
        return "";
    case 1:
        return "*";
    case 2:
        return "**";
    case 3:
        return "***";
    case 4:
        return "****";
    case 5:
        return "*****";
    default:
        return "";
    }
}

Sint32 create_hire_menu(Sint32 arg1)
{
	Sint32 linesdown, retvalue = 0;
	Sint32 start_time = query_timer();
	unsigned char showcolor; // normally STAT_COLOR or STAT_CHANGED
	Uint32 current_cost;
	Sint32 clickvalue;

    
    UiRect stat_box = {196, 50 - 6 - 32, 104, 82 + 32};
    UiRect stat_box_inner = {stat_box.x + 4, stat_box.y + 4 + 6, stat_box.w - 8, stat_box.h - 8 - 6};
    UiRect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};
    
    UiRect cost_box = {196, 130, 104, 31};
    UiRect cost_box_inner = {cost_box.x + 4, cost_box.y + 4, cost_box.w - 8, cost_box.h - 8};
    UiRect cost_box_content = {cost_box_inner.x + 4, cost_box_inner.y + 4, cost_box_inner.w - 8, cost_box_inner.h - 8};
    
    UiRect description_box = {11, 71, 180, 90};
    UiRect description_box_inner = {description_box.x + 4, description_box.y + 4, description_box.w - 8, description_box.h - 8};
    UiRect description_box_content = {description_box_inner.x + 4, description_box_inner.y + 4, description_box_inner.w - 8, description_box_inner.h - 8};
    
    UiRect name_box = {description_box.x + description_box.w/2 - (126-34)/2, description_box.y - 71 + 8, 126 - 34, 24 - 8};
    UiRect name_box_inner = {name_box.x + 2, name_box.y + 2, name_box.w - 4, name_box.h - 4};
    
    button* buttons = picker_hiremenu_buttons();
    const int num_buttons = picker_hiremenu_button_count();

    buttons[0].x = description_box.x + description_box.w/2 - buttons[0].sizex - 4 - 30;
    buttons[0].y = name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2 - buttons[0].sizey/2;
    
    buttons[1].x = description_box.x + description_box.w/2 + 4 + 30;
    buttons[1].y = name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2 - buttons[1].sizey/2;
    
    buttons[2].hidden = (og::runtime::current_session->myscreen_->save_data.numplayers == 1);
    
	og::runtime::current_session->myscreen_->clearbuffer();

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	#ifdef DISABLE_MULTIPLAYER
	buttons[2].hidden = true;
	#endif

	int highlighted_button = 1;
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

    og::ui::HireSession hire_session(og::runtime::current_session->myscreen_->save_data, og::runtime::current_session->current_team_num_);
    pks().hire_session = &hire_session;
    sync_current_guy_from_hire();
    change_hire_teamnum(0);
    
    
    unsigned char last_family = og::runtime::current_session->current_guy_->family;
    std::string description = get_class_description(last_family);
    std::list<std::string> desc = explode(description);
    const char* family_name = get_family_string(last_family);
	
	grab_mouse();

	while ( !(retvalue & MENU_EXIT) )
	{
        picker_lobby_poll();
        if (team_build_remote_start_requested(retvalue))
            break;
	    // Input
		clickvalue = leftmouse(buttons);
		if (clickvalue == 1)
			retvalue = og::runtime::current_session->localbuttons_->leftclick();
		else if (clickvalue == 2)
			retvalue = og::runtime::current_session->localbuttons_->rightclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        if(retvalue == MENU_OK || retvalue == MENU_REDRAW)
        {
	            // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	            og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

            // Update our team-number display ..
            change_hire_teamnum(0);
            
            retvalue = 0;
        }
		
		// Draw
		og::runtime::current_session->myscreen_->clearbuffer();
		
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        if (!og::runtime::current_session->current_guy_)
            sync_current_guy_from_hire();
        
        // Name box
        og::runtime::current_session->myscreen_->draw_button(
            name_box.x, name_box.y, name_box.x + name_box.w - 1,
            name_box.y + name_box.h - 1, 1);
        og::runtime::current_session->myscreen_->draw_button_inverted(
            name_box_inner.x, name_box_inner.y, name_box_inner.w,
            name_box_inner.h);
        
        text& mytext = og::runtime::current_session->myscreen_->text_normal;
        mytext.write_xy(name_box.x + name_box.w/2 - 3*static_cast<Sint32>(strlen(family_name)), name_box.y + 6, family_name, static_cast<unsigned char>(DARK_BLUE), 1);
        
		show_guy(query_timer()-start_time, 0, description_box.x + description_box.w/2, name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2); // 0 means current_guy
        change_hire_teamnum(0);
        
        
        // Description box
        og::runtime::current_session->myscreen_->draw_button(
            description_box.x, description_box.y,
            description_box.x + description_box.w - 1,
            description_box.y + description_box.h - 1, 1);
        og::runtime::current_session->myscreen_->draw_button_inverted(
            description_box_inner.x, description_box_inner.y,
            description_box_inner.w, description_box_inner.h);
        
        if(og::runtime::current_session->current_guy_->family != last_family)
        {
            // Update description
            last_family = og::runtime::current_session->current_guy_->family;
            description = get_class_description(last_family);
            desc = explode(description);
            
            family_name = get_family_string(last_family);
        }
        
        int i = 0;
        for(auto& line : desc)
        {
            mytext.write_xy(description_box_content.x, description_box_content.y + i*10, DARK_BLUE, "%s", line.c_str());
            i++;
        }
        
        // Cost box
        og::runtime::current_session->myscreen_->draw_button(
            cost_box.x, cost_box.y, cost_box.x + cost_box.w - 1,
            cost_box.y + cost_box.h - 1, 1);
        og::runtime::current_session->myscreen_->draw_button_inverted(
            cost_box_inner.x, cost_box_inner.y, cost_box_inner.w,
            cost_box_inner.h);
        
        og::runtime::current_session->message_ = std::format("CASH: {}", og::runtime::current_session->myscreen_->save_data.m_totalcash[og::runtime::current_session->current_team_num_]);
        mytext.write_xy(cost_box_content.x, cost_box_content.y, og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        current_cost = pks().hire_session ? pks().hire_session->current_cost() : 0;
        mytext.write_xy(cost_box_content.x, cost_box_content.y + 10, "COST: ", DARK_BLUE, 1);
        og::runtime::current_session->message_ = std::format("      {}", current_cost );
        if (current_cost > og::runtime::current_session->myscreen_->save_data.m_totalcash[og::runtime::current_session->current_team_num_])
            mytext.write_xy(cost_box_content.x + 10, cost_box_content.y + 10, og::runtime::current_session->message_.c_str(), STAT_CHANGED, 1);
        else
            mytext.write_xy(cost_box_content.x + 10, cost_box_content.y + 10, og::runtime::current_session->message_.c_str(), STAT_COLOR, 1);

        // Stat box
        og::runtime::current_session->myscreen_->draw_button(
            stat_box.x, stat_box.y, stat_box.x + stat_box.w - 1,
            stat_box.y + stat_box.h - 1, 1);
        mytext.write_xy(stat_box.x + 65, stat_box.y + 2, DARK_BLUE, "Train");
        og::runtime::current_session->myscreen_->draw_button_inverted(
            stat_box_inner.x, stat_box_inner.y, stat_box_inner.w,
            stat_box_inner.h);

        // Stat box content
        linesdown = 0;
        int line_height = 10;

        showcolor = STAT_COLOR;

        struct { const char* label; short value; } hire_stats[] = {
            {"STR:",  og::runtime::current_session->current_guy_->strength},
            {"DEX:",  og::runtime::current_session->current_guy_->dexterity},
            {"CON:",  og::runtime::current_session->current_guy_->constitution},
            {"INT:",  og::runtime::current_session->current_guy_->intelligence},
            {"ARMOR:", og::runtime::current_session->current_guy_->armor},
        };
        for (int si = 0; si < 5; si++) {
            int y = stat_box_content.y + linesdown * line_height;
            og::runtime::current_session->message_ = std::format("{}", hire_stats[si].value);
            mytext.write_xy(stat_box_content.x, y, hire_stats[si].label,
                             static_cast<unsigned char>(STAT_COLOR), 1);
            mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, y, og::runtime::current_session->message_.c_str(), showcolor, 1);
            if (si < 4) // cost rating for STR/DEX/CON/INT only
                mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, y,
                                get_training_cost_rating(last_family, si), showcolor, 1);
            if (si < 4)
                linesdown++;
        }
		
		// Separator bar
		UiRect r = {stat_box_content.x + 10, stat_box_content.y + (linesdown+1)*line_height - 2, stat_box_content.w - 20, 2};
		og::runtime::current_session->myscreen_->draw_button_inverted(
			r.x, r.y, r.w, r.h);
		
		int derived_offset = 3*STAT_NUM_OFFSET/4;
		auto ds = compute_guy_derived_stats(*og::runtime::current_session->current_guy_);

        linesdown++;
        int hire_line = linesdown;
        draw_derived_stats_block(mytext, ds, stat_box_content.x, derived_offset, showcolor,
            [&](int l) { return stat_box_content.y + l*line_height + 4; }, hire_line);
        linesdown = hire_line;


        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        og::input_native::sleep_ms(10);
        
        if(arg1 == 1)
        {
            // Show popup on new game
            arg1 = -1;
            popup_dialog("HIRE TROOPS", "Get your team started here\nby hiring some fresh recruits.");
            
	            // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	            og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
        }
	}
	
	pks().hire_session = nullptr;
	og::runtime::current_session->myscreen_->clearbuffer();
	//myscreen->clearscreen();
	return MENU_REDRAW;
}

Sint32 create_train_menu(Sint32 arg1)
{
	float linesdown = 0.0f;
	Sint32 i, retvalue=0;
	unsigned char showcolor;
	Sint32 start_time = query_timer();
	Uint32 current_cost;
	Sint32 clickvalue;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
	
    UiRect stat_box = {38, 66, 82, 94};
    UiRect stat_box_inner = {stat_box.x + 4, stat_box.y + 4, stat_box.w - 8, stat_box.h - 8};
    UiRect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};
    
    UiRect info_box_inner = {176, 34, 304-176, 112+22-34};
    UiRect info_box_content = {info_box_inner.x + 4, info_box_inner.y + 4, info_box_inner.w - 8, info_box_inner.h - 8};
    
	if (arg1)
		arg1 = 1;

	// Make sure we have a local team member we can train.
	if (save.team_size < 1 || !save_has_trainable_team_member(save))
	{
        show_need_team_to_train_popup();
		
		return MENU_OK;
	}

	og::runtime::current_session->myscreen_->clearbuffer();

		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	
	#ifdef DISABLE_MULTIPLAYER
	button* buttons = picker_trainmenu_buttons();
	const int num_buttons = picker_trainmenu_button_count();
	buttons[18].hidden = true;
	#else
	button* buttons = picker_trainmenu_buttons();
	const int num_buttons = picker_trainmenu_button_count();
	#endif

	int highlighted_button = 1;
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
	
	for (i=2; i < 14; i++)
	{
		if (!(i%2)) // 2, 4, ..., 12
			og::runtime::current_session->allbuttons_[i]->set_graphic(FAMILY_MINUS);
		else
			og::runtime::current_session->allbuttons_[i]->set_graphic(FAMILY_PLUS);
	}

    og::ui::TrainSession train_session(save);
    pks().train_session = &train_session;
    sync_current_guy_from_train();
    if (pks().train_session->empty()) {
        pks().train_session = nullptr;
        show_need_team_to_train_popup();
        return MENU_OK;
    }
    current_cost = pks().train_session->current_cost();

	grab_mouse();
	
    clear_keyboard();
    
    clear_key_press_event();

	while ( !(retvalue & MENU_EXIT) )
	{
        picker_lobby_poll();
        if (team_build_remote_start_requested(retvalue))
            break;
	    // Input
		clickvalue = leftmouse(buttons);
		if (clickvalue == 1)
			retvalue = og::runtime::current_session->localbuttons_->leftclick();
		else if (clickvalue == 2)
			retvalue = og::runtime::current_session->localbuttons_->rightclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Nested menus can replace the global button array before returning.
        // Once a submenu requests EXIT, stop drawing this menu immediately.
        if (retvalue & MENU_EXIT)
            break;
        
        // Reset buttons
        if(og::runtime::current_session->localbuttons_ && (retvalue == MENU_OK || retvalue == MENU_REDRAW))
        {
            if(retvalue == MENU_REDRAW)
            {
					og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

				for (i=2; i < 14; i++)
				{
					if (!(i%2)) // 2, 4, ..., 12
						og::runtime::current_session->allbuttons_[i]->set_graphic(FAMILY_MINUS);
					else
						og::runtime::current_session->allbuttons_[i]->set_graphic(FAMILY_PLUS);
				}
				sync_current_guy_from_train();
            }

            if (!og::runtime::current_session->current_guy_)
                sync_current_guy_from_train();
            current_cost = pks().train_session->current_cost();
            retvalue = 0;
        }
		
        //current_cost = calculate_train_cost(here);
        
		// Draw
		og::runtime::current_session->myscreen_->clearbuffer();
		
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
		show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
		

        linesdown = 0;

        og::runtime::current_session->myscreen_->draw_button(34,  8, 126, 24, 1, 1);  // name box
        og::runtime::current_session->myscreen_->draw_text_bar(36, 10, 124, 22);
        
        text& mytext = og::runtime::current_session->myscreen_->text_normal;
        mytext.write_xy(80 - mytext.query_width(og::runtime::current_session->current_guy_->name.c_str())/2, 14,
                         og::runtime::current_session->current_guy_->name.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        og::runtime::current_session->myscreen_->draw_button(38, 66, 120, 160, 1, 1); // stats box
        og::runtime::current_session->myscreen_->draw_text_bar(42, 70, 116, 156);

        
        bool level_increased = pks().train_session->level_increased();
        bool stat_increased = pks().train_session->stats_increased();
        const guy& original_guy = pks().train_session->original();

        struct { const char* label; short cur_val; short old_val; } train_stats[] = {
            {"  STR:", og::runtime::current_session->current_guy_->strength,     original_guy.strength},
            {"  DEX:", og::runtime::current_session->current_guy_->dexterity,    original_guy.dexterity},
            {"  CON:", og::runtime::current_session->current_guy_->constitution, original_guy.constitution},
            {"  INT:", og::runtime::current_session->current_guy_->intelligence, original_guy.intelligence},
            {"ARMOR:", og::runtime::current_session->current_guy_->armor,        original_guy.armor},
        };
        for (auto& s : train_stats) {
            og::runtime::current_session->message_ = std::format("{}", s.cur_val);
            mytext.write_xy(stat_box_content.x, DOWN(linesdown), s.label,
                             static_cast<unsigned char>(STAT_COLOR), 1);
            if (level_increased)
                showcolor = STAT_LEVELED;
            else if (s.old_val < s.cur_val)
                showcolor = STAT_CHANGED;
            else
                showcolor = STAT_COLOR;
            mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), og::runtime::current_session->message_.c_str(), showcolor, 1);
        }

        // Level (different color logic: no STAT_LEVELED, uses STAT_DISABLED)
        og::runtime::current_session->message_ = std::format("{}", og::runtime::current_session->current_guy_->level);
        mytext.write_xy(stat_box_content.x, DOWN(linesdown), "LEVEL:",
                         static_cast<unsigned char>(STAT_COLOR), 1);
        if (level_increased)
            showcolor = STAT_CHANGED;
        else if(stat_increased)
            showcolor = STAT_DISABLED;
        else
            showcolor = STAT_COLOR;
        mytext.write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), og::runtime::current_session->message_.c_str(), showcolor, 1);


        // Info box
        og::runtime::current_session->myscreen_->draw_button(174, 32, 306, 114+22, 1, 1); // info box
        og::runtime::current_session->myscreen_->draw_text_bar(176, 34, 304, 112+22); // main text box
        
	        showcolor = DARK_BLUE;
	        linesdown = 0.0f;
	        int line_height = 10;
	        auto info_y = [&](float line) -> Sint32 {
	            return info_box_content.y + static_cast<Sint32>(line * static_cast<float>(line_height));
	        };
		
		int derived_offset = 3*STAT_NUM_OFFSET/4;
		
        og::runtime::current_session->message_ = std::format("Total Kills: {}", og::runtime::current_session->current_guy_->kills);
	        mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(), DARK_BLUE, 1);

        linesdown++;
        if (og::runtime::current_session->current_guy_->total_hits && og::runtime::current_session->current_guy_->total_shots) // have we at least hit something? :)
        {
            og::runtime::current_session->message_ = std::format("   Accuracy: {}% ",
                    (og::runtime::current_session->current_guy_->total_hits*100)/og::runtime::current_session->current_guy_->total_shots);
	            mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(), DARK_BLUE, 1);
        }
        else // haven't ever hit anyone
        {
	            mytext.write_xy(180, info_y(linesdown), "   Accuracy: N/A ", DARK_BLUE, 1);
        }

        linesdown++;
        og::runtime::current_session->message_ = std::format(" EXPERIENCE: {}", og::runtime::current_session->current_guy_->exp);
	        mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        
        
        linesdown++;
		// Separator bar
			UiRect r = {info_box_content.x + 10, info_y(linesdown) - 2, info_box_content.w - 20, 2};
			og::runtime::current_session->myscreen_->draw_button_inverted(
				r.x, r.y, r.w, r.h);
        
        linesdown += 0.4f;

        {
            auto ds = compute_guy_derived_stats(*og::runtime::current_session->current_guy_);
            float base_line = linesdown;
            int train_line = 0;
            draw_derived_stats_block(mytext, ds, info_box_content.x, derived_offset, showcolor,
                [&](int l) { return info_y(base_line + static_cast<float>(l)); }, train_line);
            linesdown = base_line + static_cast<float>(train_line);
        }
        
        
        linesdown++;
		// Separator bar
			UiRect r2 = {info_box_content.x + 10, info_y(linesdown) - 2, info_box_content.w - 20, 2};
			og::runtime::current_session->myscreen_->draw_button_inverted(
				r2.x, r2.y, r2.w, r2.h);
        
        linesdown += 0.4f;
        og::runtime::current_session->message_ = std::format("CASH: {}", og::runtime::current_session->myscreen_->save_data.m_totalcash[og::runtime::current_session->current_guy_->teamnum]);
	        mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);

        linesdown++;
	        mytext.write_xy(180, info_y(linesdown), "COST: ", DARK_BLUE, 1);
        og::runtime::current_session->message_ = std::format("      {}", current_cost );
        if (current_cost > og::runtime::current_session->myscreen_->save_data.m_totalcash[og::runtime::current_session->current_guy_->teamnum])
	            mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(), STAT_CHANGED, 1);
	        else
	            mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(), STAT_COLOR, 1);

        // Display our team setting ..
        og::runtime::current_session->message_ = std::format("Playing on Team {}", og::runtime::current_session->current_guy_->teamnum+1);
        og::runtime::current_session->allbuttons_[18]->label = og::runtime::current_session->message_;
        og::runtime::current_session->allbuttons_[18]->vdisplay();

        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        og::input_native::sleep_ms(10);
	}
	pks().train_session = nullptr;
	og::runtime::current_session->myscreen_->clearbuffer();
	//myscreen->clearscreen();
    if ((retvalue & MENU_EXIT) && team_build_start_selected())
    {
        return MENU_EXIT;
    }
	return MENU_REDRAW;
}

static Sint32 create_slot_menu(button* buttons, int num_buttons, const char* title)
{
	Sint32 retvalue=0;
	text& menutext = og::runtime::current_session->myscreen_->text_normal;
    auto return_to_parent = []() {
        clear_allbuttons();
        return MENU_REDRAW;
    };

	// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
	int highlighted_button = 10;
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

	while ( !(retvalue & MENU_EXIT) )
	{
        picker_lobby_poll();
        if (team_build_remote_start_requested(retvalue))
            break;
	    // Input
		if(leftmouse(buttons))
        {
			retvalue = og::runtime::current_session->localbuttons_->leftclick();
			if(retvalue == MENU_REDRAW)
            {
                return return_to_parent();
            }
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);
        if(retvalue == MENU_REDRAW)
        {
            return return_to_parent();
        }

        // Reset buttons
        reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);

		// Draw
		og::runtime::current_session->myscreen_->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);

        og::runtime::current_session->myscreen_->draw_button(15,  9, 255, 199, 1, 1);
        og::runtime::current_session->myscreen_->draw_text_bar(19, 13, 251, 21);
        int title_len = static_cast<int>(std::strlen(title));
        menutext.write_xy(135-(title_len*3), 15, title, RED, 1);
        for (Sint32 i=0; i < 10; i++)
        {
            std::string temp_filename = std::format("save{}", i+1);
            og::runtime::current_session->allbuttons_[i]->label = get_saved_name(temp_filename.c_str());
            og::runtime::current_session->myscreen_->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
            og::runtime::current_session->allbuttons_[i]->vdisplay();
            og::runtime::current_session->myscreen_->draw_box(og::runtime::current_session->allbuttons_[i]->xloc-1,
                               og::runtime::current_session->allbuttons_[i]->yloc-1,
                               og::runtime::current_session->allbuttons_[i]->xend,
                               og::runtime::current_session->allbuttons_[i]->yend, 0, 0, 1);
        }
        og::runtime::current_session->myscreen_->draw_text_bar(23, og::runtime::current_session->allbuttons_[10]->yloc-2, 66, og::runtime::current_session->allbuttons_[10]->yend+1);
        og::runtime::current_session->allbuttons_[10]->vdisplay();
        og::runtime::current_session->myscreen_->draw_box(og::runtime::current_session->allbuttons_[10]->xloc-1,
                           og::runtime::current_session->allbuttons_[10]->yloc-1,
                           og::runtime::current_session->allbuttons_[10]->xend,
                           og::runtime::current_session->allbuttons_[10]->yend, 0, 0, 1);

        draw_highlight(buttons[highlighted_button]);
		og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        og::input_native::sleep_ms(10);
	}

	return return_to_parent();
}

Sint32 create_load_menu(Sint32 /*arg1*/)
{
	button* buttons = picker_loadteam_buttons();
	return create_slot_menu(buttons, picker_loadteam_button_count(), "Gladiator: Load Game");
}

Sint32 create_save_menu(Sint32 /*arg1*/)
{
	button* buttons = picker_saveteam_buttons();
	return create_slot_menu(buttons, picker_saveteam_button_count(), "Gladiator: Save Game");
}

// --- Session-based thin wrappers for button callbacks ---

static og::ui::TrainSession::Stat but_to_stat(Sint32 whatstat)
{
    switch (whatstat) {
    case BUT_STR:   return og::ui::TrainSession::Stat::Strength;
    case BUT_DEX:   return og::ui::TrainSession::Stat::Dexterity;
    case BUT_CON:   return og::ui::TrainSession::Stat::Constitution;
    case BUT_INT:   return og::ui::TrainSession::Stat::Intelligence;
    case BUT_ARMOR: return og::ui::TrainSession::Stat::Armor;
    case BUT_LEVEL: return og::ui::TrainSession::Stat::Level;
    default:        return og::ui::TrainSession::Stat::Strength;
    }
}

Sint32 increase_stat(Sint32 whatstat, Sint32 howmuch)
{
    if (!pks().train_session)
        return MENU_OK;
    pks().train_session->increase_stat(but_to_stat(whatstat), howmuch);
    sync_current_guy_from_train();
    return MENU_OK;
}

Sint32 decrease_stat(Sint32 whatstat, Sint32 howmuch)
{
    if (!pks().train_session)
        return MENU_OK;
    pks().train_session->decrease_stat(but_to_stat(whatstat), howmuch);
    sync_current_guy_from_train();
    return MENU_OK;
}

Sint32 cycle_guy(Sint32 whichway)
{
    if (!pks().hire_session) {
        // Fallback: create recruit directly (for any code calling this outside a session)
        constexpr auto& guys = og::ui::kAllowableGuys;
        og::runtime::current_session->current_type_ = (og::runtime::current_session->current_type_ + whichway + static_cast<Sint32>(guys.size())) % static_cast<Sint32>(guys.size());
        if (og::runtime::current_session->current_type_ < 0)
            og::runtime::current_session->current_type_ = static_cast<Sint32>(guys.size()) - 1;
        og::runtime::current_session->current_guy_ = og::ui::create_recruit(guys[og::runtime::current_session->current_type_], og::runtime::current_session->current_team_num_, og::runtime::current_session->myscreen_->save_data);
        show_guy(0, 0);
        grab_mouse();
        return MENU_OK;
    }

    if (whichway > 0) pks().hire_session->next_family();
    else if (whichway < 0) pks().hire_session->prev_family();
    // whichway == 0: session constructor already initialized

    og::runtime::current_session->current_type_ = pks().hire_session->family_index();
    sync_current_guy_from_hire();
    show_guy(0, 0);
    grab_mouse();
    return MENU_OK;
}

	void show_guy(Sint32 frames, Sint32 who, Sint32 centerx, Sint32 centery) // shows the current guy ..
	{
		std::unique_ptr<walker> mywalker;
		Sint32 i;
		char newfamily;

	if (!og::runtime::current_session->current_guy_)
		return;

	frames = abs(frames);

		(void)who; // always show current_guy
		newfamily = og::runtime::current_session->current_guy_->family;

		mywalker = og::runtime::current_session->myscreen_->myloader->create_walker_owned(Order::Living, newfamily);
		mywalker->stats()->set_bit_flags(0);
		mywalker->set_curdir(static_cast<signed char>(((frames/192) + FACE_DOWN)%8));
		mywalker->set_ani_type(ANI_WALK);
		for (i=0; i <= (frames/12)%4; i++)
			mywalker->animate();
		//mywalker->set_team_num(ourteam[editguy]->teamnum);
		mywalker->set_team_num(static_cast<unsigned char>(og::runtime::current_session->current_guy_->teamnum));

	mywalker->setxy(centerx - (mywalker->sizex()/2), centery - (mywalker->sizey()/2));
	og::runtime::current_session->myscreen_->draw_button(centerx - 80 + 54, centery - 45 + 26, centerx - 80 + 106, centery - 45 + 64, 1, 1);
	og::runtime::current_session->myscreen_->draw_text_bar(centerx - 80 + 56, centery - 45 + 28, centerx - 80 + 104, centery - 45 + 62);
	draw_walker(*mywalker, og::runtime::current_session->myscreen_->viewob[0].get());
}
Sint32 cycle_team_guy(Sint32 whichway)
{
	if (!pks().train_session || pks().train_session->empty())
		return -1;

	if (whichway > 0) pks().train_session->next_member();
	else if (whichway < 0) pks().train_session->prev_member();

	sync_current_guy_from_train();
	show_guy(0, 0);

	og::runtime::current_session->current_team_num_ = og::runtime::current_session->current_guy_->teamnum;

	// Set our team button back to normal color
	if (og::runtime::current_session->allbuttons_[18])
		og::runtime::current_session->allbuttons_[18]->do_outline = 0;

	return MENU_OK;
}

Sint32 add_guy(guy *newguy)
{
	short team_num = newguy->teamnum;
	std::unique_ptr<guy> owned(newguy);
	int slot = og::ui::add_recruit_to_team(og::runtime::current_session->myscreen_->save_data, std::move(owned), team_num);
	return static_cast<Sint32>(slot);
}

Sint32 name_guy(Sint32 arg)  // 0 == current_guy, 1 == ourteam[editguy]
{
	text& nametext = og::runtime::current_session->myscreen_->text_normal;
	guy *someguy;

	if (arg && !picker_lobby_save_slot_editable(og::runtime::current_session->editguy_))
		return MENU_REDRAW;

	if (arg)
		someguy = og::runtime::current_session->myscreen_->save_data.team_list[og::runtime::current_session->editguy_].get();
	else
		someguy = og::runtime::current_session->current_guy_.get();

	if (!someguy)
		return MENU_REDRAW;

	release_mouse();
	
	og::runtime::current_session->myscreen_->draw_button(174,  8, 306, 30, 1, 1); // text box
	nametext.write_xy(176, 12, "NAME THIS CHARACTER:", DARK_BLUE, 1);
	og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
	
	clear_keyboard();
    std::optional<std::string> new_text = nametext.input_string_value(176, 20, 11, someguy->name.c_str());
	if(new_text.has_value())
		someguy->name = *new_text;
	og::runtime::current_session->myscreen_->draw_button(174,  8, 306, 30, 1, 1); // text box

	og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	return MENU_REDRAW;
}

Sint32 add_guy([[maybe_unused]] Sint32 ignoreme)
{
	if (!pks().hire_session)
		return -1;

	int slot = pks().hire_session->hire();
	if (slot < 0)
		return (og::runtime::current_session->myscreen_->save_data.team_size >= MAX_TEAM_SIZE) ? -1 : MENU_OK;

	// SDL-specific: prompt for name
	release_mouse();
	auto& hired = og::runtime::current_session->myscreen_->save_data.team_list[slot];
	std::string name = hired->name;
	if (prompt_for_string("NAME THIS CHARACTER", name))
		pks().hire_session->rename_hired(slot, name);
	grab_mouse();

	// Sync current_guy from the session's next recruit
	sync_current_guy_from_hire();
    picker_lobby_sync_roster_from_save();

	return MENU_OK;
}

// Accept changes ..
Sint32 edit_guy([[maybe_unused]] Sint32 arg1)
{
	if (!pks().train_session || pks().train_session->empty())
		return -1;
	if (!picker_lobby_save_slot_editable(pks().train_session->current_slot()))
		return MENU_OK;

	// SDL-specific: cheat mode (hold right mouse → free changes)
	bool force = false;
	if (CHEAT_MODE) {
		MouseState& cheatmouse = query_mouse();
		force = cheatmouse.right;
	}

	if (!pks().train_session->accept(force))
		return MENU_OK;  // can't afford

	// Sync working copy back after accept
	sync_current_guy_from_train();
    picker_lobby_sync_roster_from_save();
    sync_current_guy_from_train();

	// Color our team button normally
	og::runtime::current_session->allbuttons_[18]->do_outline = 0;

	return MENU_OK;
}

Sint32 how_many(Sint32 whatfamily)    // how many guys of family X on the team?
{
	return static_cast<Sint32>(og::ui::count_family_members(whatfamily, og::runtime::current_session->myscreen_->save_data));
}

Sint32 do_save(Sint32 arg1)
{
	release_mouse();
	clear_keyboard();
	
	std::string name = og::runtime::current_session->allbuttons_[arg1-1]->label;
	if(prompt_for_string("NAME YOUR SAVED GAME", name))
    {
        og::runtime::current_session->myscreen_->save_data.save_name = name;
        
        std::string newname = std::format("save{}", arg1);
        if(og::runtime::current_session->myscreen_->save_data.save(newname))
            timed_dialog("GAME SAVED");
        else
            timed_dialog("SAVE FAILED");
    }
    else
    {
        timed_dialog("SAVE CANCELED");
    }

    grab_mouse();

	return MENU_REDRAW;
}

Sint32 do_load(Sint32 arg1)
{
	std::string newname = std::format("save{}", arg1);

	if(og::runtime::current_session->myscreen_->save_data.load(newname))
    {
        timed_dialog("GAME LOADED");
        picker_lobby_sync_from_save();
    }
    else
    {
        timed_dialog("LOAD FAILED");
    }

    return MENU_REDRAW;
}

std::string get_saved_name(const char * filename)
{
	std::string temp_filename;

	char temptext[4] = {};
    std::array<char, 40> savedgame{};
	char temp_version = 1;
	short temp_registered = 0;

	// This only uses the first segment of the save format.
	// See load_team_list() for full format
	
	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number
	// 2-bytes registered mark, version 7+ only
	// 40-byte saved-game name (version 2 and up only!)
	//   .
	//   .

	temp_filename = std::format("{}.gtl", filename); // gladiator team list

	auto infile = og::io::og_open_read("save/", temp_filename.c_str());
	if (!infile) // open for read
	{
		return std::string("EMPTY SLOT");
	}

	// Read id header
	if (!og::io::og_read_exact(*infile, temptext, 1, 3) || std::string(temptext, 3) != "GTL")
	{
		return std::string("EMPTY SLOT");
	}

	// Read version number
	if (!og::io::og_read_exact(*infile, &temp_version, 1, 1))
        return std::string("EMPTY SLOT");

	if (temp_version != 1)
	{
		if (temp_version >= 2)
		{
			if (temp_version >= 7)
            {
                if (!og::io::og_read_exact(*infile, &temp_registered, 2, 1))
                    return std::string("SAVED GAME");
            }
			if (!og::io::og_read_exact(*infile, savedgame.data(), 40, 1))
                return std::string("SAVED GAME");
		}
		else
		{
			return std::string("SAVED GAME");
		}
	}
	else
		return std::string("SAVED GAME");

    const size_t name_len = strnlen(savedgame.data(), savedgame.size());
    return std::string(savedgame.data(), name_len);
}

Sint32 delete_all()
{
	Sint32 counter = og::runtime::current_session->myscreen_->save_data.team_size;

	for (int i = 0; i < og::runtime::current_session->myscreen_->save_data.team_size; i++)
    {
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset();
    }
    
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    picker_lobby_sync_roster_from_save();

	return counter;
}

Sint32 go_menu(Sint32 arg1)
{
	// Save the current team in memory to save0.gtl, and
	// run gladiator.

	if (arg1)
		arg1 = 1;

    // Make sure we have a valid team
    if (og::runtime::current_session->myscreen_->save_data.team_size < 1)
    {
        popup_dialog("NEED A TEAM!", "Please hire a\nteam before\nstarting the level");

        return MENU_REDRAW;
    }

#ifdef __EMSCRIPTEN__
    picker_prepare_async_team_build_start_request();
    og::runtime::current_session->myscreen_->save_data.save("save0");
    og::runtime::current_session->current_guy_.reset();
    Log("go_menu: Lobby start requested, returning MENU_EXIT\n");
    return MENU_EXIT;  // This will unwind all menu loops back to picker_main/picker_frame
#else
    picker_lobby_sync_settings_from_save();
    picker_lobby_sync_roster_from_save();
    const bool start_already_requested =
        g_start_game_requested && picker_lobby_has_game_start_config();
    if (!start_already_requested)
        g_start_game_requested = false;
    if (!start_already_requested && !picker_lobby_request_start())
    {
        while (!g_start_game_requested && picker_lobby_start_request_pending())
        {
            picker_lobby_poll();
            og::input_native::sleep_ms(10);
        }
    }

    if (!g_start_game_requested)
        return MENU_REDRAW;

    g_start_game_requested = false;

    // Native build: use blocking loop
    do
    {
        og::runtime::current_session->myscreen_->save_data.save("save0");
        release_mouse();

        //*******************************
        // Fade out from MENU loop
        //*******************************
        // Zardus: PORT: fade out from menu code now in glad.cpp
        //clear_keyboard();
        //myscreen->fadeblack(0);

        og::runtime::current_session->current_guy_.reset();

        // Reset viewscreen prefs
        {
            short numviews = (og::runtime::current_session->myscreen_->save_data.numplayers == 0) ? 1 : og::runtime::current_session->myscreen_->save_data.numplayers;
            og::runtime::current_session->myscreen_->ready_for_battle(numviews);
        }

#ifdef TESTING
        picker_testing_mark_game_start();
#endif
        glad_main(og::runtime::current_session->myscreen_->save_data.numplayers);
#ifdef TESTING
        picker_testing_mark_game_end();
#endif

        Log("Returned from glad_main, retry={}\n", og::runtime::current_session->myscreen_->world().retry);

        //*******************************
        // Fade out from ACTION loop
        //*******************************
        // Zardus: PORT: new fade code
        og::runtime::current_session->myscreen_->fadeblack(0);

        // Zardus: PORT: doesn't seem to be neccessary
        og::runtime::current_session->myscreen_->clearbuffer();

        // Zardus: PORT: they had this in just so that the pallettes got reset to
        // normal. It actually faded in a black screen, since fading in the menu
        // would mean messing with a bunch of things. Maybe we'll do the fade in
        // menu later, but for now we'll keep it like they had
        //*******************************
        // Fade in to MENU loop
        //*******************************
        // Zardus: PORT: new fade code
        //myscreen->fadeblack(1);

        grab_mouse();

        og::runtime::current_session->myscreen_->reset(1);
        og::runtime::current_session->myscreen_->viewob[0]->resize(PREF_VIEW_FULL);

        auto loadgame = og::io::og_open_read("save/", "save0.gtl");
        if (loadgame)
        {
            og::runtime::current_session->myscreen_->save_data.load("save0");
        }
    }
    while(og::runtime::current_session->myscreen_->world().retry);

    picker_reinitialize_lobby_after_game();

	return button_action_id(ButtonAction::CreateTeamMenu);
#endif
}

void statscopy(guy *dest, guy *source)
{
	og::ui::statscopy(dest, source);
}
