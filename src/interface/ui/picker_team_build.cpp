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
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/core/test_trace.h>
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

constexpr int kTeamBuildGoButtonIndex = kCreateMenuGoIndex;
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

// Nav must never link to a hidden button: route hire_troops/save_team/
// networking around GO when it is hidden (and back through it when not).
void picker_wire_team_build_nav(button* buttons,
                                int count,
                                bool host_controls_visible)
{
    if (buttons == nullptr || count < kCreateMenuButtonCount)
        return;

    if (host_controls_visible)
    {
        buttons[kCreateMenuHireIndex].nav.down = kCreateMenuGoIndex;
        buttons[kCreateMenuSaveIndex].nav.right = kCreateMenuGoIndex;
        buttons[kCreateMenuNetworkingIndex].nav.up = kCreateMenuGoIndex;
    }
    else
    {
        buttons[kCreateMenuHireIndex].nav.down = kCreateMenuNetworkingIndex;
        buttons[kCreateMenuSaveIndex].nav.right = -1;
        buttons[kCreateMenuNetworkingIndex].nav.up = kCreateMenuHireIndex;
    }
}

// Rewire the always-visible VIEW LEVEL | TEAMS | PROGRESS row's up-links
// around the host-gated SET CAMPAIGN / SET LEVEL column.
void picker_wire_scenario_menu_nav(button* buttons,
                                   int count,
                                   bool host_controls_visible)
{
    if (buttons == nullptr || count < kScenarioMenuButtonCount)
        return;

    const int row_up =
        host_controls_visible ? kScenarioMenuSetLevelIndex : -1;
    buttons[kScenarioMenuViewScenarioIndex].nav.up = row_up;
    buttons[kScenarioMenuTeamsIndex].nav.up = row_up;
    buttons[kScenarioMenuProgressIndex].nav.up = row_up;
}

void sync_team_build_host_control_visibility(button* buttons,
                                             int num_buttons,
                                             int& highlighted_button)
{
    if (buttons == nullptr || num_buttons < kCreateMenuButtonCount)
        return;

    // GO is the only host-gated team-build button now: SET CAMPAIGN and
    // SET LEVEL moved into the SCENARIO subscreen (with their own gating),
    // and the CTF match settings live inside the TEAMS subscreen.
    const bool host_controls_visible = picker_lobby_host_controls_visible();
    buttons[kTeamBuildGoButtonIndex].hidden = !host_controls_visible;
    sync_button_hidden_state(buttons, kTeamBuildGoButtonIndex);
    picker_wire_team_build_nav(buttons, num_buttons, host_controls_visible);

    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);
}

void sync_scenario_menu_host_control_visibility(button* buttons,
                                                int num_buttons,
                                                int& highlighted_button)
{
    if (buttons == nullptr || num_buttons < kScenarioMenuButtonCount)
        return;

    // SET CAMPAIGN / SET LEVEL keep their host-only visibility inside the
    // subscreen; VIEW LEVEL / TEAMS / PROGRESS stay visible for everyone.
    const bool host_controls_visible = picker_lobby_host_controls_visible();
    buttons[kScenarioMenuSetCampaignIndex].hidden = !host_controls_visible;
    buttons[kScenarioMenuSetLevelIndex].hidden = !host_controls_visible;
    sync_button_hidden_state(buttons, kScenarioMenuSetCampaignIndex);
    sync_button_hidden_state(buttons, kScenarioMenuSetLevelIndex);
    picker_wire_scenario_menu_nav(buttons, num_buttons, host_controls_visible);

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

void sync_difficulty_menu_visibility(button* buttons,
                                     int num_buttons,
                                     int& highlighted_button)
{
    if (buttons == nullptr || num_buttons < kDifficultyMenuButtonCount)
        return;

    // Every settings row on this screen is LobbySettings-backed (difficulty
    // included): a joiner's click would be rejected by the server and the
    // per-frame label re-derive would immediately restore the host's value.
    // Hide the rows for non-hosts (the GO / SET LEVEL precedent) and rewire
    // BACK's vertical cycle so nav never lands on a hidden button.
    const bool host_controls_visible = picker_lobby_host_controls_visible();
    for (int index = kDifficultyMenuDifficultyIndex;
         index < kDifficultyMenuButtonCount; ++index)
    {
        buttons[index].hidden = !host_controls_visible;
        sync_button_hidden_state(buttons, index);
    }
    buttons[kDifficultyMenuBackIndex].nav.up =
        host_controls_visible ? kDifficultyMenuGeneratorRateIndex : -1;
    buttons[kDifficultyMenuBackIndex].nav.down =
        host_controls_visible ? kDifficultyMenuDifficultyIndex : -1;

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
    // guy::family comes verbatim from the .gtl save file with no range check;
    // a malicious/negative value would index the loader stat arrays out of
    // bounds. Clamp to a valid living family (matching the soldier fallback
    // used by create_walker_owned / set_derived_stats) before indexing.
    const int fam = (g.family < 0 || g.family >= NUM_FAMILIES)
        ? FAMILY_SOLDIER
        : static_cast<int>(g.family);
    auto pix = PIX(Order::Living, fam);
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
    (void)arg1;
	Sint32 retvalue=0;

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
        
        // Compact current-scenario hint in the empty y=40 band. The full
        // level-title and campaign-name strips moved into the SCENARIO
        // subscreen, next to the SET LEVEL / SET CAMPAIGN buttons.
        {
            std::string hint = std::format(
                "SCEN {}: {}",
                og::runtime::current_session->myscreen_->save_data.scen_num,
                og::runtime::current_session->myscreen_->world().title);
            if (hint.size() > 34)
                hint.resize(34);
            const int hint_w = static_cast<int>(hint.size()) * 6;
            og::runtime::current_session->myscreen_->draw_rect_filled(
                10, 43, hint_w + 4, 8, PURE_BLACK, 150);
            mytext.write_xy(12, 44, WHITE, "%s", hint.c_str());
        }

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

// ---------------------------------------------------------------------------
// TEAMS subscreen: per-team JOIN (local my_team / networked TeamChange), the
// host-gated CTF match settings, local per-character team cycling, and the
// networked READY toggle.
// ---------------------------------------------------------------------------

void picker_wire_teams_menu_nav(button* buttons, int count,
                                const TeamsMenuWiring& wiring)
{
    if (buttons == nullptr || count < kTeamsMenuButtonCount)
        return;

    // One vertical chain of "row anchors" through the team rows: the row's
    // JOIN when visible, else its pager. A pager that shares a row with a
    // visible JOIN hangs off horizontally and copies the JOIN's vertical
    // links, so every visible pager stays reachable in every variant.
    std::vector<int> mids;
    for (int t = 0; t < 4; ++t)
    {
        if (wiring.join_visible[t])
            mids.push_back(kTeamsMenuJoinFirstIndex + t);
        else if (wiring.pager_visible[t])
            mids.push_back(kTeamsMenuPageFirstIndex + t);
    }
    const int first_mid = mids.empty() ? -1 : mids.front();
    const int last_mid = mids.empty() ? -1 : mids.back();
    const int bottom_mid = wiring.networked ? kTeamsMenuReadyIndex : -1;
    const int bottom_right = wiring.show_ctf ? kTeamsMenuCtfTroopsIndex : -1;

    for (int index = 0; index < kTeamsMenuButtonCount; ++index)
        buttons[index].nav = MenuNav{};

    // Bottom row: BACK | READY (networked) | TROOPS (CTF host).
    buttons[kTeamsMenuBackIndex].nav.right =
        bottom_mid >= 0 ? bottom_mid : bottom_right;
    if (bottom_mid >= 0)
    {
        buttons[bottom_mid].nav.left = kTeamsMenuBackIndex;
        buttons[bottom_mid].nav.right = bottom_right;
    }
    if (bottom_right >= 0)
    {
        buttons[bottom_right].nav.left =
            bottom_mid >= 0 ? bottom_mid : kTeamsMenuBackIndex;
    }

    // Row-anchor chain (visible rows only, top to bottom).
    const int below_mids = wiring.guy_row
        ? kTeamsMenuGuyTeamIndex
        : (bottom_right >= 0
               ? bottom_right
               : (bottom_mid >= 0 ? bottom_mid : kTeamsMenuBackIndex));
    for (std::size_t mid_order = 0; mid_order < mids.size(); ++mid_order)
    {
        buttons[mids[mid_order]].nav.up = mid_order == 0
            ? (wiring.show_ctf ? kTeamsMenuCtfTeamsIndex : -1)
            : mids[mid_order - 1];
        buttons[mids[mid_order]].nav.down = mid_order + 1 < mids.size()
            ? mids[mid_order + 1]
            : below_mids;
    }

    // Pagers beside a visible JOIN: horizontal link plus the JOIN's
    // vertical neighbors (both already point at visible buttons).
    for (int t = 0; t < 4; ++t)
    {
        if (!wiring.pager_visible[t] || !wiring.join_visible[t])
            continue;
        const int pager = kTeamsMenuPageFirstIndex + t;
        const int join = kTeamsMenuJoinFirstIndex + t;
        buttons[pager].nav.right = join;
        buttons[join].nav.left = pager;
        buttons[pager].nav.up = buttons[join].nav.up;
        buttons[pager].nav.down = buttons[join].nav.down;
    }

    // CTF settings row.
    if (wiring.show_ctf)
    {
        buttons[kTeamsMenuCtfTeamsIndex].nav.right = kTeamsMenuCtfCapsIndex;
        buttons[kTeamsMenuCtfCapsIndex].nav.left = kTeamsMenuCtfTeamsIndex;
        buttons[kTeamsMenuCtfTeamsIndex].nav.down = first_mid >= 0
            ? first_mid
            : (wiring.guy_row ? kTeamsMenuGuyPrevIndex : kTeamsMenuBackIndex);
        buttons[kTeamsMenuCtfCapsIndex].nav.down = first_mid >= 0
            ? first_mid
            : (wiring.guy_row ? kTeamsMenuGuyTeamIndex : bottom_right);
    }

    // Local guy-cycling row.
    if (wiring.guy_row)
    {
        buttons[kTeamsMenuGuyPrevIndex].nav.right = kTeamsMenuGuyNextIndex;
        buttons[kTeamsMenuGuyNextIndex].nav.left = kTeamsMenuGuyPrevIndex;
        buttons[kTeamsMenuGuyNextIndex].nav.right = kTeamsMenuGuyTeamIndex;
        buttons[kTeamsMenuGuyTeamIndex].nav.left = kTeamsMenuGuyNextIndex;
        const int above_guys = first_mid >= 0
            ? first_mid
            : (wiring.show_ctf ? kTeamsMenuCtfTeamsIndex : -1);
        buttons[kTeamsMenuGuyPrevIndex].nav.up = above_guys;
        buttons[kTeamsMenuGuyNextIndex].nav.up = above_guys;
        buttons[kTeamsMenuGuyTeamIndex].nav.up = last_mid >= 0
            ? last_mid
            : (wiring.show_ctf ? kTeamsMenuCtfCapsIndex : -1);
        buttons[kTeamsMenuGuyPrevIndex].nav.down = kTeamsMenuBackIndex;
        buttons[kTeamsMenuGuyNextIndex].nav.down = kTeamsMenuBackIndex;
        buttons[kTeamsMenuGuyTeamIndex].nav.down =
            bottom_right >= 0 ? bottom_right : kTeamsMenuBackIndex;
    }

    // Bottom-row up links.
    buttons[kTeamsMenuBackIndex].nav.up = wiring.guy_row
        ? kTeamsMenuGuyPrevIndex
        : (first_mid >= 0
               ? first_mid
               : (wiring.show_ctf ? kTeamsMenuCtfTeamsIndex : -1));
    if (bottom_mid >= 0)
    {
        buttons[bottom_mid].nav.up = last_mid >= 0
            ? last_mid
            : (wiring.show_ctf ? kTeamsMenuCtfTeamsIndex : -1);
    }
    if (bottom_right >= 0)
    {
        buttons[bottom_right].nav.up = wiring.guy_row
            ? kTeamsMenuGuyTeamIndex
            : (last_mid >= 0 ? last_mid : kTeamsMenuCtfCapsIndex);
    }
}

namespace
{

// The TEAMS screen's detail-line budgets (6px/char from x=24): a single
// slice may run to the readability bar's edge; a paged slice stops short of
// the page indicator + '>' pager at the row's right edge.
constexpr int kTeamsDetailCharsUnpaged = 34;
constexpr int kTeamsDetailCharsPaged = 26;

// One frame's full TEAMS-subscreen state: the nav wiring inputs plus the CTF
// map context (authored flag teams from the LIVE picker world, scanned before
// any CTF init could mutate flags — the picker never runs ctf init).
struct TeamsMenuFrameState
{
    bool is_ctf = false;   // CTF campaign selected
    bool ctf_map = false;  // loaded picker world is a TYPE_CTF level
    bool campaign_mounted = true; // mounted campaign matches the save's
    bool allied = false;
    bool authored[4] = {};
    // Per-team member/player detail line, paginated; detail_page is the
    // normalized current slice (the raw counter lives in PickerState).
    std::array<std::vector<std::string>, 4> detail_pages;
    std::array<int, 4> detail_page = {};
    TeamsMenuWiring wiring;
};

TeamsMenuFrameState compute_teams_menu_state()
{
    TeamsMenuFrameState state;
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    state.is_ctf = og::ui::is_ctf_campaign(save);
    state.allied = save.allied_mode != 0;
    state.wiring.show_ctf =
        state.is_ctf && picker_lobby_host_controls_visible();
    state.wiring.networked = picker_lobby_is_networked();
    state.wiring.guy_row = !state.wiring.networked && save.team_size > 0;

    // Per-team detail items: lobby players (networked) or roster members.
    const std::vector<og::sim::LobbyPlayer> players =
        state.wiring.networked ? picker_lobby_players()
                               : std::vector<og::sim::LobbyPlayer>{};
    for (int t = 0; t < 4; ++t)
    {
        std::vector<std::string> items;
        if (state.wiring.networked)
        {
            for (const og::sim::LobbyPlayer& player : players)
            {
                if (player.team != t)
                    continue;
                items.push_back(player.ready ? player.name + " [RDY]"
                                             : player.name);
            }
        }
        else
        {
            for (const auto& member : save.team_list)
            {
                if (member && member->teamnum == t)
                    items.push_back(member->name);
            }
        }

        // Fits one slice -> no pager; otherwise repack with room for the
        // "p/N" indicator and the '>' button at the row's right edge.
        std::vector<std::string> pages =
            og::ui::paginate_team_detail_pages(items, kTeamsDetailCharsUnpaged);
        if (pages.size() > 1)
        {
            pages = og::ui::paginate_team_detail_pages(
                items, kTeamsDetailCharsPaged);
        }
        state.wiring.pager_visible[t] = pages.size() > 1;

        // Normalize the session's raw flip counter onto this page count and
        // write it back, so a shrinking roster can't strand the page.
        const int page_count = static_cast<int>(pages.size());
        int& raw_page = pks().teams_menu_team_page[static_cast<std::size_t>(t)];
        raw_page = ((raw_page % page_count) + page_count) % page_count;
        state.detail_page[static_cast<std::size_t>(t)] = raw_page;
        state.detail_pages[static_cast<std::size_t>(t)] = std::move(pages);
    }

    // A joiner without the host's campaign mounted has some OTHER level
    // loaded: never let authored-flag gating act on a wrong world.
    state.campaign_mounted =
        get_mounted_campaign() == save.current_campaign;

    const GameWorld& world = og::runtime::current_session->myscreen_->world();
    state.ctf_map = state.campaign_mounted &&
        (world.type & GameWorld::TYPE_CTF) != 0;
    if (state.ctf_map)
        og::sim::ctf_authored_flag_teams(world, state.authored);

    int team_limit = MAX_PLAYERS;
    if (state.is_ctf && save.ctf_team_count > 0)
        team_limit = std::min<int>(MAX_PLAYERS, save.ctf_team_count);

    const bool spectator = save.numplayers == 0;
    for (int t = 0; t < 4; ++t)
    {
        // A JOIN is offered only where joining is meaningful: never in allied
        // mode (everyone fights together) or spectator mode, never beyond the
        // explicit CTF team-count clamp, and never onto a team a CTF map does
        // not author a flag for (alive there means no flag and no respawn).
        state.wiring.join_visible[t] = !state.allied && !spectator &&
            t < team_limit &&
            (!state.is_ctf || !state.ctf_map || state.authored[t]);
    }
    return state;
}

void sync_teams_menu_visibility(button* buttons,
                                int num_buttons,
                                int& highlighted_button,
                                const TeamsMenuFrameState& state)
{
    if (buttons == nullptr || num_buttons < kTeamsMenuButtonCount)
        return;

    const SaveData& save = og::runtime::current_session->myscreen_->save_data;

    buttons[kTeamsMenuCtfTeamsIndex].hidden = !state.wiring.show_ctf;
    buttons[kTeamsMenuCtfCapsIndex].hidden = !state.wiring.show_ctf;
    buttons[kTeamsMenuCtfTroopsIndex].hidden = !state.wiring.show_ctf;
    if (state.wiring.show_ctf)
    {
        buttons[kTeamsMenuCtfTeamsIndex].label =
            og::ui::format_ctf_teams_label(save);
        buttons[kTeamsMenuCtfCapsIndex].label =
            og::ui::format_ctf_caps_label(save);
        buttons[kTeamsMenuCtfTroopsIndex].label =
            og::ui::format_ctf_troops_label(save);
    }

    for (int t = 0; t < 4; ++t)
    {
        button& join = buttons[kTeamsMenuJoinFirstIndex + t];
        join.hidden = !state.wiring.join_visible[t];
        join.label = save.my_team == t ? "YOU" : "JOIN";
    }

    buttons[kTeamsMenuGuyPrevIndex].hidden = !state.wiring.guy_row;
    buttons[kTeamsMenuGuyNextIndex].hidden = !state.wiring.guy_row;
    buttons[kTeamsMenuGuyTeamIndex].hidden = !state.wiring.guy_row;

    for (int t = 0; t < 4; ++t)
    {
        buttons[kTeamsMenuPageFirstIndex + t].hidden =
            !state.wiring.pager_visible[t];
    }

    buttons[kTeamsMenuReadyIndex].hidden = !state.wiring.networked;
    if (state.wiring.networked)
    {
        buttons[kTeamsMenuReadyIndex].label =
            picker_lobby_local_ready() ? "UNREADY" : "READY";
    }

    picker_wire_teams_menu_nav(buttons, num_buttons, state.wiring);

    auto& allbuttons = og::runtime::current_session->allbuttons_;
    for (int index = 0; index < kTeamsMenuButtonCount; ++index)
    {
        sync_button_hidden_state(buttons, index);
        if (allbuttons[index] != nullptr)
            allbuttons[index]->label = buttons[index].label;
    }
    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);
}

// Clip a drawn line to the 6px/char budget of the given pixel width.
std::string clip_to_width(std::string line, int max_chars)
{
    if (static_cast<int>(line.size()) > max_chars)
        line.resize(static_cast<std::size_t>(max_chars));
    return line;
}

// Pre-pass: the translucent row readability bars. These must render BENEATH
// the buttons — the per-team '>' pager (219, 39+30t) is the only button whose
// face sits inside a bar, and a bar painted after draw_buttons would dim it
// to ~41% brightness unlike every other button. The frame loop draws this
// before draw_buttons; all text/content stays in draw_teams_menu_content
// (drawn after the buttons) so it still reads on top of the bars.
void draw_teams_menu_row_bars()
{
    screen* const myscreen = og::runtime::current_session->myscreen_;
    for (int t = 0; t < 4; ++t)
    {
        const int row_y = 32 + 30 * t;
        myscreen->draw_rect_filled(8, row_y - 2, 226, 22, PURE_BLACK, 150);
    }
}

void draw_teams_menu_content(const TeamsMenuFrameState& state, text& mytext)
{
    screen* const myscreen = og::runtime::current_session->myscreen_;
    const SaveData& save = myscreen->save_data;

    mytext.write_xy(10, 8, "TEAMS", WHITE, 1);

    const std::vector<short> seats = og::ui::derive_local_seat_teams(save);
    const std::vector<og::sim::LobbyPlayer> players = picker_lobby_players();

    for (int t = 0; t < 4; ++t)
    {
        const int row_y = 32 + 30 * t;

        // The team's palette swatch (the row's readability bar is drawn by
        // draw_teams_menu_row_bars before the buttons).
        myscreen->draw_rect_filled(
            10, row_y, 10, 10, static_cast<unsigned char>(t * 16 + 40), 255);

        int hero_count = 0;
        for (std::size_t slot = 0; slot < save.team_list.size(); ++slot)
        {
            const auto& member = save.team_list[slot];
            if (member && member->teamnum == t)
                ++hero_count;
        }

        std::string seat_tag;
        bool has_humans = false;
        if (state.wiring.networked)
        {
            for (const og::sim::LobbyPlayer& player : players)
            {
                if (player.team == t)
                    has_humans = true;
            }
            if (save.my_team == t)
                seat_tag = "(YOU)";
        }
        else
        {
            // Only seats below numplayers materialize in-game (game.cpp
            // truncates the derivation at numviews); never tag the rest.
            const std::size_t seat_limit = std::min<std::size_t>(
                seats.size(), static_cast<std::size_t>(save.numplayers));
            for (std::size_t seat = 0; seat < seat_limit; ++seat)
            {
                if (seats[seat] == t)
                    seat_tag = std::format("(P{})", seat + 1);
            }
        }

        // The pager column ('>' at x=219 and the p/N indicator ending at
        // x=217) narrows both the label row and the detail line.
        const bool paged = state.wiring.pager_visible[t];
        const std::string row_label = og::ui::format_team_row_label(
            static_cast<short>(t),
            hero_count,
            state.is_ctf && state.ctf_map,
            state.authored[t],
            has_humans,
            seat_tag);
        mytext.write_xy(24, row_y,
                        clip_to_width(row_label, paged ? 32 : 34).c_str(),
                        WHITE, 1);

        // Member-detail line: the current pre-clipped slice from the frame
        // state; the indicator makes any truncation visible.
        const std::vector<std::string>& pages =
            state.detail_pages[static_cast<std::size_t>(t)];
        const int page = state.detail_page[static_cast<std::size_t>(t)];
        const std::string& detail = pages[static_cast<std::size_t>(page)];
        if (!detail.empty())
            mytext.write_xy(24, row_y + 9, detail.c_str(), DARK_BLUE, 1);
        if (paged)
        {
            const std::string indicator = std::format(
                "{}/{}", page + 1, static_cast<int>(pages.size()));
            mytext.write_xy(217 - 6 * static_cast<int>(indicator.size()),
                            row_y + 9, indicator.c_str(), WHITE, 1);
        }
    }

    // Banner band above the team rows: the y=8 buttons fill rows 8..22
    // (height 15) and the rows' readability bars start at y=30 (row_y-2
    // with row_y=32), so the 23..29 band is free of buttons, row bars,
    // and detail lines (draw_rect_filled fills [y, y+h)).
    if (state.allied)
    {
        myscreen->draw_rect_filled(8, 23, 226, 7, PURE_BLACK, 150);
        mytext.write_xy(10, 24, "ALLIED: ALL PLAYERS FIGHT TOGETHER",
                        YELLOW, 1);
    }
    else if (!state.campaign_mounted)
    {
        // The loaded picker world is some other map: JOIN gating could not
        // be verified against the real level (authored-flag clamp skipped).
        myscreen->draw_rect_filled(8, 23, 226, 7, PURE_BLACK, 150);
        mytext.write_xy(10, 24, "TEAM LIST UNVERIFIED", YELLOW, 1);
    }

    if (state.wiring.guy_row)
    {
        const int slot = teams_menu_selected_guy_slot();
        if (slot >= 0 && save.team_list[slot])
        {
            const std::string guy_line = std::format(
                "{} {}",
                save.team_list[slot]->name,
                og::sim::ctf_team_color_name(save.team_list[slot]->teamnum));
            mytext.write_xy(30, 148, clip_to_width(guy_line, 14).c_str(),
                            WHITE, 1);
        }
    }
}

} // namespace

Sint32 create_teams_menu(Sint32 arg1)
{
    (void)arg1;
    Sint32 retvalue = 0;
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    og::runtime::current_session->myscreen_->clearbuffer();

	    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

    button* buttons = picker_teamsmenu_buttons();
    int num_buttons = picker_teamsmenu_button_count();
    int highlighted_button = kTeamsMenuBackIndex;
    // Per-team pager pages are session state and reset every open.
    pks().teams_menu_team_page.fill(0);
    TeamsMenuFrameState state = compute_teams_menu_state();
    og::runtime::current_session->localbuttons_ =
        init_buttons(buttons, num_buttons);
    sync_teams_menu_visibility(buttons, num_buttons, highlighted_button, state);
    short last_level_id =
        og::runtime::current_session->myscreen_->save_data.scen_num;
    // Trace each paged team's slice on entry and on every flip (tests pin
    // the indicator and the rotating member names through these).
    std::array<int, 4> traced_page = {-1, -1, -1, -1};
    const auto trace_paged_rows = [&traced_page](
                                      const TeamsMenuFrameState& frame) {
        for (int t = 0; t < 4; ++t)
        {
            const auto& pages =
                frame.detail_pages[static_cast<std::size_t>(t)];
            if (pages.size() <= 1)
                continue;
            const int page = frame.detail_page[static_cast<std::size_t>(t)];
            if (traced_page[static_cast<std::size_t>(t)] == page)
                continue;
            traced_page[static_cast<std::size_t>(t)] = page;
            TRACE("picker", "teams_detail t=%d page=%d/%d %s", t, page + 1,
                  static_cast<int>(pages.size()),
                  pages[static_cast<std::size_t>(page)].c_str());
        }
    };
    trace_paged_rows(state);

    while (!(retvalue & MENU_EXIT))
    {
        picker_lobby_poll();
        // Mirror the parent team-build loop's level-reload guard: a host
        // SET LEVEL synced into save.scen_num while parked here must reload
        // the picker world so JOIN gating reflects the real map.
        if (last_level_id !=
            og::runtime::current_session->myscreen_->save_data.scen_num)
        {
            last_level_id =
                og::runtime::current_session->myscreen_->save_data.scen_num;
            og::runtime::current_session->myscreen_->world().id = last_level_id;
            og::runtime::current_session->myscreen_->load_level();
        }
        state = compute_teams_menu_state();
        sync_teams_menu_visibility(
            buttons, num_buttons, highlighted_button, state);
        trace_paged_rows(state);
        // A joiner parked here still follows the host's GO.
        if (team_build_remote_start_requested(retvalue))
            break;

        // Input
        if (leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // BACK returns MENU_REDRAW to signal "go back to team menu".
        // Check before reset_buttons can clear it.
        if (retvalue & MENU_REDRAW)
            break;

        reset_buttons(og::runtime::current_session->localbuttons_,
                      buttons, num_buttons, retvalue);

        // Draw: row bars go beneath the buttons (the in-row pager must keep
        // the standard button face); text content goes on top of both.
        og::runtime::current_session->myscreen_->clearbuffer();
        draw_backdrop();
        draw_teams_menu_row_bars();
        draw_buttons(buttons, num_buttons);
        draw_teams_menu_content(state, mytext);
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
    }
    og::runtime::current_session->myscreen_->clearbuffer();

    // Propagate MENU_EXIT (remote start / GO) so TeamBuild interception works;
    // BACK returns MENU_REDRAW to keep the parent create_team_menu running.
    if (retvalue & MENU_EXIT)
        return retvalue;

    return MENU_REDRAW;
}

#ifdef TESTING
// Test hook: set up and render exactly one TEAMS-subscreen frame (the real
// draw order: backdrop -> row bars -> buttons -> content) and present it, so
// pixel tests can probe button faces against the translucent row bars
// without racing the blocking frame loop.
void picker_test_render_teams_menu_frame()
{
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    button* buttons = picker_teamsmenu_buttons();
    int num_buttons = picker_teamsmenu_button_count();
    int highlighted_button = kTeamsMenuBackIndex;
    pks().teams_menu_team_page.fill(0);
    TeamsMenuFrameState state = compute_teams_menu_state();
    og::runtime::current_session->localbuttons_ =
        init_buttons(buttons, num_buttons);
    sync_teams_menu_visibility(buttons, num_buttons, highlighted_button, state);

    og::runtime::current_session->myscreen_->clearbuffer();
    draw_backdrop();
    draw_teams_menu_row_bars();
    draw_buttons(buttons, num_buttons);
    draw_teams_menu_content(state, mytext);
    draw_highlight(buttons[highlighted_button]);
    og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
}
#endif

// ---------------------------------------------------------------------------
// VIEW LEVEL: read-only roster report of the current scenario, rendered from
// a scratch headless level load (never touches the live picker world).
// ---------------------------------------------------------------------------

// PREV/NEXT handler: records the requested page step for the frame loop and
// returns MENU_OK so both vbutton::leftclick and handle_menu_nav fire it.
Sint32 view_scenario_page_flip(Sint32 step)
{
    pks().view_scenario_page_step = (step < 0) ? -1 : 1;
    return MENU_OK;
}

Sint32 create_view_scenario_menu(Sint32 arg1)
{
    (void)arg1;
    Sint32 retvalue = 0;
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // The viewer is read-only and never mounts: a joiner without the host's
    // campaign mounted gets a popup (the accepted joiner-quirk family).
    if (get_mounted_campaign() != save.current_campaign)
    {
        popup_dialog("VIEW LEVEL", "CAMPAIGN NOT\nMOUNTED");
        return MENU_REDRAW;
    }

    // Scratch load with the SDL hooks: the hook-less default loader lacks the
    // CTF treasure families (FAMILY_FLAG / FAMILY_CTF_POINT).
    LevelRuntimeData scenario(save.scen_num, false, &sdl_level_data_hooks());
    if (!scenario.load())
    {
        popup_dialog("VIEW LEVEL", "COULD NOT\nLOAD LEVEL");
        return MENU_REDRAW;
    }

    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(scenario.world(), save);
    const std::vector<std::string> lines =
        og::ui::format_scenario_report_lines(report);
    const int total_pages = std::max(
        1,
        (static_cast<int>(lines.size()) + kViewScenarioRowsPerPage - 1) /
            kViewScenarioRowsPerPage);
    int page = 0;

    std::string title =
        std::format("SCEN {}: {}", save.scen_num, scenario.world().title);
    if (title.size() > 48)
        title.resize(48);

    TRACE("picker", "view_scenario lines=%d page=%d",
          static_cast<int>(lines.size()), page);

    og::runtime::current_session->myscreen_->clearbuffer();

	    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

    button* buttons = picker_viewscenario_buttons();
    int num_buttons = picker_viewscenario_button_count();
    int highlighted_button = kViewScenarioBackIndex;
    buttons[kViewScenarioPrevIndex].hidden = total_pages <= 1;
    buttons[kViewScenarioNextIndex].hidden = total_pages <= 1;
    if (total_pages <= 1)
        buttons[kViewScenarioBackIndex].nav.right = -1;
    og::runtime::current_session->localbuttons_ =
        init_buttons(buttons, num_buttons);

    pks().view_scenario_page_step = 0;

    while (!(retvalue & MENU_EXIT))
    {
        picker_lobby_poll();
        if (team_build_remote_start_requested(retvalue))
            break;

        // Input: the leftmouse() result is the consumed press transition; a
        // fast press+release must still dispatch the click.
        if (leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // BACK returns MENU_REDRAW to signal "go back to team menu".
        if (retvalue & MENU_REDRAW)
            break;

        // PREV/NEXT carry ButtonAction::ViewScenarioPageFlip: both mouse
        // leftclick and keyboard FIRE route through do_call, which records
        // the requested step and returns MENU_OK. Consume the step here.
        const int step = pks().view_scenario_page_step;
        pks().view_scenario_page_step = 0;
        if (step != 0)
        {
            const int next_page =
                std::clamp(page + step, 0, total_pages - 1);
            retvalue = 0;
            if (next_page != page)
            {
                page = next_page;
                TRACE("picker", "view_scenario lines=%d page=%d",
                      static_cast<int>(lines.size()), page);
            }
        }

        reset_buttons(og::runtime::current_session->localbuttons_,
                      buttons, num_buttons, retvalue);

        // Draw
        og::runtime::current_session->myscreen_->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        og::runtime::current_session->myscreen_->draw_button(5, 5, 314, 160, 2, 1);
        mytext.write_xy(10, 8, title.c_str(), static_cast<unsigned char>(BLACK), 1);
        const int first_line =
            page * kViewScenarioRowsPerPage;
        for (int row = 0; row < kViewScenarioRowsPerPage; ++row)
        {
            const int line_index = first_line + row;
            if (line_index >= static_cast<int>(lines.size()))
                break;
            mytext.write_xy(10, 18 + row * 6, lines[line_index].c_str(),
                            static_cast<unsigned char>(BLACK), 1);
        }
        if (total_pages > 1)
        {
            const std::string page_info =
                std::format("{}/{}", page + 1, total_pages);
            mytext.write_xy(140, 176, page_info.c_str(), WHITE, 1);
        }
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
    }
    og::runtime::current_session->myscreen_->clearbuffer();

    if (retvalue & MENU_EXIT)
        return retvalue;

    return MENU_REDRAW;
}

// ---------------------------------------------------------------------------
// SCENARIO subscreen: SET CAMPAIGN / SET LEVEL (host-gated) with the
// campaign-name / level-title strips alongside, plus the always-visible
// VIEW LEVEL | TEAMS | PROGRESS row. Blocking-subscreen pattern: per-frame
// picker_lobby_poll, joiner remote-start honored (propagates MENU_EXIT with
// the StartGame item), BACK returns MENU_REDRAW to the team-build screen.
// ---------------------------------------------------------------------------

Sint32 create_scenario_menu(Sint32 arg1)
{
    (void)arg1;
    Sint32 retvalue = 0;
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    og::runtime::current_session->myscreen_->clearbuffer();

	    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.

    button* buttons = picker_scenariomenu_buttons();
    int num_buttons = picker_scenariomenu_button_count();
    int highlighted_button = kScenarioMenuBackIndex;
    sync_scenario_menu_host_control_visibility(
        buttons, num_buttons, highlighted_button);
    og::runtime::current_session->localbuttons_ =
        init_buttons(buttons, num_buttons);

    short last_level_id =
        og::runtime::current_session->myscreen_->save_data.scen_num;

    while (!(retvalue & MENU_EXIT))
    {
        picker_lobby_poll();
        sync_scenario_menu_host_control_visibility(
            buttons, num_buttons, highlighted_button);
        // A joiner parked here still follows the host's GO.
        if (team_build_remote_start_requested(retvalue))
            break;

        // Input
        if (leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Nested screens (TEAMS / VIEW LEVEL / PROGRESS / the campaign and
        // level pickers) return MENU_REDRAW; reset_buttons clears it and
        // reinits this layout. BACK carries MENU_EXIT; a remote start that
        // fired inside a nested screen propagates MENU_EXIT + StartGame.
        const bool buttons_were_reset = reset_buttons(
            og::runtime::current_session->localbuttons_, buttons, num_buttons,
            retvalue);
        if (retvalue & MENU_EXIT)
            break;

        // The parent loop's level-reload guard: a SET LEVEL pick (or a host
        // sync while parked here) must refresh the title strip's world.
        if (last_level_id !=
                og::runtime::current_session->myscreen_->save_data.scen_num ||
            buttons_were_reset)
        {
            retvalue = 0;
            last_level_id =
                og::runtime::current_session->myscreen_->save_data.scen_num;
            og::runtime::current_session->myscreen_->world().id = last_level_id;
            og::runtime::current_session->myscreen_->load_level();
        }

        // Draw
        og::runtime::current_session->myscreen_->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        mytext.write_xy(10, 8, "SCENARIO", WHITE, 1);

        // The campaign-name / level-title strips sit beside the buttons that
        // change them (always drawn — joiners see the host's choices even
        // while SET CAMPAIGN / SET LEVEL are hidden).
        const auto draw_strip = [&mytext](int y, const std::string& value) {
            const std::string clipped = clip_to_width(value, 32);
            const int strip_w = static_cast<int>(clipped.size()) * 6;
            og::runtime::current_session->myscreen_->draw_rect_filled(
                114, y - 1, strip_w + 4, 8, PURE_BLACK, 150);
            mytext.write_xy(116, y, WHITE, "%s", clipped.c_str());
        };
        draw_strip(
            buttons[kScenarioMenuSetCampaignIndex].y + 4,
            og::data::campaign_display_title(
                og::runtime::current_session->myscreen_->save_data
                    .current_campaign));
        draw_strip(
            buttons[kScenarioMenuSetLevelIndex].y + 4,
            std::format(
                "SCEN {}: {}",
                og::runtime::current_session->myscreen_->save_data.scen_num,
                og::runtime::current_session->myscreen_->world().title));

        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
    }
    og::runtime::current_session->myscreen_->clearbuffer();

    // Propagate a remote start (MENU_EXIT + the StartGame item) so the
    // parent team-build screen exits into GO; BACK's own MENU_EXIT folds
    // into MENU_REDRAW to keep the parent running.
    if ((retvalue & MENU_EXIT) && team_build_start_selected())
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
        if (levels.size() > static_cast<size_t>(visible_rows)) {
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
	    if (stat < 0 || stat >= 6 || fd->stat_costs[stat] == 0) return "";
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
        
        // current_team_num_ is derived from a save-loaded guy::teamnum (which
        // is unvalidated); clamp before indexing the MAX_PLAYERS-sized array.
        const int hire_cash_team =
            (og::runtime::current_session->current_team_num_ < 0 ||
             og::runtime::current_session->current_team_num_ >= MAX_PLAYERS)
                ? 0
                : static_cast<int>(og::runtime::current_session->current_team_num_);
        og::runtime::current_session->message_ = std::format("CASH: {}", og::runtime::current_session->myscreen_->save_data.m_totalcash[hire_cash_team]);
        mytext.write_xy(cost_box_content.x, cost_box_content.y, og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
        current_cost = pks().hire_session ? pks().hire_session->current_cost() : 0;
        mytext.write_xy(cost_box_content.x, cost_box_content.y + 10, "COST: ", DARK_BLUE, 1);
        og::runtime::current_session->message_ = std::format("      {}", current_cost );
        if (current_cost > og::runtime::current_session->myscreen_->save_data.m_totalcash[hire_cash_team])
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
				// The DETAILS submenu can promote (family-change) the REAL
				// team member in place. Re-snapshot the working copy first
				// or the stale copy hides the promotion on screen and a
				// later ACCEPT statscopy()s the old family back (bug A9).
				if (pks().train_session)
					pks().train_session->resync_if_promoted();
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
        // teamnum is loaded verbatim from the .gtl save file with no range
        // check; m_totalcash has only MAX_PLAYERS slots, so a malicious value
        // would index out of bounds. Clamp to a valid team before indexing.
        const int train_cash_team =
            (og::runtime::current_session->current_guy_->teamnum < 0 ||
             og::runtime::current_session->current_guy_->teamnum >= MAX_PLAYERS)
                ? 0
                : static_cast<int>(og::runtime::current_session->current_guy_->teamnum);
        og::runtime::current_session->message_ = std::format("CASH: {}", og::runtime::current_session->myscreen_->save_data.m_totalcash[train_cash_team]);
	        mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);

        linesdown++;
	        mytext.write_xy(180, info_y(linesdown), "COST: ", DARK_BLUE, 1);
        og::runtime::current_session->message_ = std::format("      {}", current_cost );
        if (current_cost > og::runtime::current_session->myscreen_->save_data.m_totalcash[train_cash_team])
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
	pks().old_guy = nullptr;
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
	{
		auto& team_list = og::runtime::current_session->myscreen_->save_data.team_list;
		const int slot = og::runtime::current_session->editguy_;
		if (slot < 0 || slot >= static_cast<int>(team_list.size()))
			return MENU_REDRAW;
		someguy = team_list[slot].get();
	}
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

	std::array<char, 4> temptext{};
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
	if (!og::io::og_read_exact(*infile, temptext.data(), 1, 3) || std::string(temptext.data(), 3) != "GTL")
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

#ifdef TESTING
// GCOVR_EXCL_START -- test-only coverage harness in src/, not shipped code.
int picker_team_build_testing_exercise_internal_paths()
{
    struct TestLobbyClient final : og::ui::IPickerLobbyClient
    {
        bool host_visible = false;
        bool has_start_config = true;
        bool editable = true;
        bool requested = false;

        void initialize_from_save() override {}
        void shutdown() override {}
        void sync_from_save() override {}
        void sync_roster_from_save() override {}
        void sync_settings_from_save() override {}
        void poll_and_apply() override {}
        void set_player_mode(int) override {}
        bool request_start_game() override
        {
            requested = true;
            return true;
        }
        std::optional<og::ui::PickerLobbyGameStartConfig>
        build_game_start_config() const override
        {
            return has_start_config
                ? std::optional<og::ui::PickerLobbyGameStartConfig>{og::ui::PickerLobbyGameStartConfig{}}
                : std::nullopt;
        }
        std::optional<og::ui::PickerLobbyGameStartConfig>
        consume_game_start_config() override
        {
            return build_game_start_config();
        }
        bool start_request_pending() const noexcept override
        {
            return false;
        }
        bool has_game_start_config() const noexcept override
        {
            return has_start_config;
        }
        bool host_controls_visible() const noexcept override
        {
            return host_visible;
        }
        bool is_save_slot_editable(std::size_t) const noexcept override
        {
            return editable;
        }
    };

    struct LobbyGuard
    {
        og::ui::IPickerLobbyClient* saved = og::ui::active_picker_lobby_client();
        ~LobbyGuard()
        {
            og::ui::install_active_picker_lobby_client(saved);
        }
    } lobby_guard;

    int check_index = 0;
    int failed_check = 0;
    const auto check = [&check_index, &failed_check](bool condition) {
        ++check_index;
        if (!condition && failed_check == 0)
            failed_check = check_index;
    };
    TestLobbyClient client;
    og::ui::install_active_picker_lobby_client(&client);
    client.initialize_from_save();
    client.sync_from_save();
    client.sync_roster_from_save();
    client.sync_settings_from_save();
    client.poll_and_apply();
    client.set_player_mode(2);
    check(client.request_start_game() && client.requested);
    check(client.build_game_start_config().has_value());
    check(client.consume_game_start_config().has_value());
    check(!client.start_request_pending());
    client.shutdown();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const unsigned char old_team_size = save.team_size;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[i] = std::move(save.team_list[i]);
    auto restore_team = [&] {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[i] = std::move(saved_team[i]);
        save.team_size = old_team_size;
    };

    check(!save_has_trainable_team_member(save));
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_size = 1;
    client.editable = false;
    check(!save_has_trainable_team_member(save));
    client.editable = true;
    check(save_has_trainable_team_member(save));

    button buttons[] = {
        button("b0", "0", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b1", "1", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b2", "2", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b3", "3", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b4", "4", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b5", "5", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b6", "6", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b7", "7", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b8", "8", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b9", "9", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
        button("b10", "10", KEYSTATE_UNKNOWN, 0, 0, 10, 10, 0, 0, MenuNav{}),
    };
    int highlighted = 5;
    sync_button_hidden_state(nullptr, 0);
    sync_button_hidden_state(buttons, -1);
    ensure_highlighted_button_visible(nullptr, 0, highlighted);
    buttons[0].hidden = true;
    buttons[1].hidden = false;
    highlighted = 0;
    ensure_highlighted_button_visible(buttons, 2, highlighted);
    check(highlighted == 1);
    buttons[1].hidden = true;
    ensure_highlighted_button_visible(buttons, 2, highlighted);
    check(highlighted == 0);

    highlighted = 5;
    client.host_visible = false;
    sync_team_build_host_control_visibility(buttons, 11, highlighted);
    check(buttons[kTeamBuildGoButtonIndex].hidden &&
        buttons[kCreateMenuHireIndex].nav.down == kCreateMenuNetworkingIndex &&
        buttons[kCreateMenuSaveIndex].nav.right == -1 &&
        buttons[kCreateMenuNetworkingIndex].nav.up == kCreateMenuHireIndex);
    client.host_visible = true;
    sync_team_build_host_control_visibility(buttons, 11, highlighted);
    check(!buttons[kTeamBuildGoButtonIndex].hidden &&
        buttons[kCreateMenuHireIndex].nav.down == kTeamBuildGoButtonIndex &&
        buttons[kCreateMenuSaveIndex].nav.right == kTeamBuildGoButtonIndex &&
        buttons[kCreateMenuNetworkingIndex].nav.up == kTeamBuildGoButtonIndex);
    client.host_visible = false;
    sync_scenario_menu_host_control_visibility(buttons, 11, highlighted);
    check(buttons[kScenarioMenuSetCampaignIndex].hidden &&
        buttons[kScenarioMenuSetLevelIndex].hidden &&
        buttons[kScenarioMenuViewScenarioIndex].nav.up == -1 &&
        buttons[kScenarioMenuTeamsIndex].nav.up == -1 &&
        buttons[kScenarioMenuProgressIndex].nav.up == -1);
    client.host_visible = true;
    sync_scenario_menu_host_control_visibility(buttons, 11, highlighted);
    check(!buttons[kScenarioMenuSetCampaignIndex].hidden &&
        !buttons[kScenarioMenuSetLevelIndex].hidden &&
        buttons[kScenarioMenuViewScenarioIndex].nav.up ==
            kScenarioMenuSetLevelIndex);
    sync_view_team_host_control_visibility(buttons, 1, highlighted);
    check(!buttons[kViewTeamGoButtonIndex].hidden);

    // DIFFICULTY subscreen gating: every settings row hides for a non-host;
    // BACK's vertical cycle is rewired and the highlight is pulled onto BACK.
    for (int i = 0; i < 11; ++i)
        buttons[i].hidden = false;
    buttons[kDifficultyMenuBackIndex].nav.up = kDifficultyMenuGeneratorRateIndex;
    buttons[kDifficultyMenuBackIndex].nav.down = kDifficultyMenuDifficultyIndex;
    int diff_highlight = kDifficultyMenuGeneratorRateIndex;
    client.host_visible = false;
    sync_difficulty_menu_visibility(buttons, kDifficultyMenuButtonCount,
                                    diff_highlight);
    check(buttons[kDifficultyMenuDifficultyIndex].hidden &&
        buttons[kDifficultyMenuRespawnModeIndex].hidden &&
        buttons[kDifficultyMenuRespawnDelayIndex].hidden &&
        buttons[kDifficultyMenuPermadeathIndex].hidden &&
        buttons[kDifficultyMenuGeneratorRateIndex].hidden &&
        buttons[kDifficultyMenuBackIndex].nav.up == -1 &&
        buttons[kDifficultyMenuBackIndex].nav.down == -1 &&
        diff_highlight == kDifficultyMenuBackIndex);
    client.host_visible = true;
    sync_difficulty_menu_visibility(buttons, kDifficultyMenuButtonCount,
                                    diff_highlight);
    check(!buttons[kDifficultyMenuDifficultyIndex].hidden &&
        !buttons[kDifficultyMenuGeneratorRateIndex].hidden &&
        buttons[kDifficultyMenuBackIndex].nav.up ==
            kDifficultyMenuGeneratorRateIndex &&
        buttons[kDifficultyMenuBackIndex].nav.down ==
            kDifficultyMenuDifficultyIndex);

    g_start_game_requested = false;
    Sint32 retvalue = 0;
    check(!team_build_remote_start_requested(retvalue));
    g_start_game_requested = true;
    client.has_start_config = false;
    check(!team_build_remote_start_requested(retvalue));
    client.has_start_config = true;
    check(team_build_remote_start_requested(retvalue) && (retvalue & MENU_EXIT));
    check(team_build_start_selected());
    pks().selected_menu_item = nullptr;
    check(!team_build_start_selected());
    g_start_game_requested = false;

    // Main-menu-scope remote start (mainmenu + the DIFFICULTY subscreen):
    // exits with the Main CONTINUE item selected so the state machine
    // re-enters team build, which consumes the start.
    retvalue = 0;
    check(!picker_main_scope_remote_start_requested(retvalue));
    g_start_game_requested = true;
    client.has_start_config = false;
    check(!picker_main_scope_remote_start_requested(retvalue));
    client.has_start_config = true;
    check(picker_main_scope_remote_start_requested(retvalue) &&
        (retvalue & MENU_EXIT));
    check(pks().selected_menu_item != nullptr &&
        pks().selected_menu_item->command ==
            og::ui::PickerMenuCommand::ContinueGame);
    pks().selected_menu_item = nullptr;
    g_start_game_requested = false;
    picker_prepare_async_team_build_start_request();
    check(client.requested);
#ifdef __EMSCRIPTEN__
    check(g_start_game_requested);
#else
    check(!g_start_game_requested);
#endif

    check(!get_class_description(FAMILY_SOLDIER).empty());
    check(get_class_description(250).empty());
    for (int family : og::ui::kAllowableGuys)
    {
        for (int stat = 0; stat < 5; ++stat)
            (void)get_training_cost_rating(static_cast<unsigned char>(family), stat);
    }
    check(std::strlen(get_training_cost_rating(250, 0)) == 0);

    og::ui::TrainSession train_session(save);
    pks().train_session = nullptr;
    check(increase_stat(BUT_STR, 1) == MENU_OK &&
        decrease_stat(BUT_STR, 1) == MENU_OK &&
        cycle_team_guy(1) == -1 &&
        edit_guy(0) == -1);

    pks().train_session = &train_session;
    for (Sint32 stat : {BUT_STR, BUT_DEX, BUT_CON, BUT_INT, BUT_ARMOR, BUT_LEVEL, Sint32{-999}})
    {
        (void)increase_stat(stat, 1);
        (void)decrease_stat(stat, 1);
    }
    (void)cycle_team_guy(1);
    (void)cycle_team_guy(-1);
    sync_current_guy_from_train();
    check(og::runtime::current_session->current_guy_ != nullptr);
    pks().train_session = nullptr;

    og::ui::HireSession hire_session(save, 0);
    pks().hire_session = nullptr;
    check(add_guy(0) == -1);
    (void)cycle_guy(0);
    pks().hire_session = &hire_session;
    (void)cycle_guy(1);
    (void)cycle_guy(-1);
    sync_current_guy_from_hire();
    check(og::runtime::current_session->current_guy_ != nullptr);
    pks().hire_session = nullptr;

    og::runtime::current_session->current_guy_.reset();
    check(name_guy(0) == MENU_REDRAW);
    client.editable = false;
    og::runtime::current_session->editguy_ = 0;
    check(name_guy(1) == MENU_REDRAW);
    client.editable = true;

    (void)how_many(FAMILY_SOLDIER);
    check(delete_all() >= 0);
    check(go_menu(0) == MENU_REDRAW);
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;
    client.requested = false;
    g_start_game_requested = false;
    check(go_menu(0) == MENU_REDRAW && client.requested);

    guy source(FAMILY_ELF);
    guy destination(FAMILY_SOLDIER);
    source.name = "Copied";
    source.level = 3;
    source.exp = calculate_exp(source.level);
    source.kills = 4;
    statscopy(&destination, &source);
    check(destination.family == source.family &&
        destination.name == source.name &&
        destination.exp == source.exp);

    g_start_game_requested = false;
    pks().selected_menu_item = nullptr;
    pks().train_session = nullptr;
    pks().hire_session = nullptr;
    restore_team();
    return failed_check == 0 ? 0 : -failed_check;
}
// GCOVR_EXCL_STOP
#endif

void statscopy(guy *dest, guy *source)
{
	og::ui::statscopy(dest, source);
}
