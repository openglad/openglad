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

#include <openglad/core/text_wrap.h>
#include <openglad/core/version.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/core/util.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/interface/render/walker_draw.h>

#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/resources/gparser.h>
#include <openglad/interface/render/video.h>
#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/game_mode.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_selection.h>
#include <openglad/core/test_trace.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
void draw_backdrop();
Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
void draw_highlight_interior(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons, Sint32& retvalue);
const char* family_name_copy(short family);
const char* get_family_string(Sint32 family);
std::string get_saved_name(const char* filename);
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
        if (save.team_list[static_cast<std::size_t>(i)] && picker_lobby_save_slot_editable(i))
            return true;
    }
    return false;
}

// §2.5 per-row TRAIN seed: the base-camp dispatch stashes the clicked
// roster slot here; create_train_menu consumes it (one-shot) so the train
// screen opens directly on that character instead of the first editable one.
static int g_train_seed_slot = -1;

void picker_set_train_seed_slot(int slot)
{
    g_train_seed_slot = slot;
}

// The §3.8 base-camp mutation tail (design G12, live via the WP2 choke
// point): re-sync the lobby roster (a networked content change clears that
// machine's ready server-side — §4.3), optimistically drop the local ready
// flag (a no-op for solo/local lobby clients), and autosave the company
// (networked lobbies take the [SAVE-F1] owner-preserving merge write).
// Called from every roster-mutation site: deploy toggle, hire, train accept,
// TRAIN/Base Camp team change, and rename.
void picker_base_camp_after_roster_mutation(int additional_owned_team)
{
    picker_lobby_sync_roster_from_save();
    (void)picker_lobby_set_ready(false);
    // Failures log inside company_autosave; the base camp never blocks on a
    // failed autosave (§3.8 — callers surface but don't crash).
    (void)og::ui::company_autosave_after_mutation(
        og::runtime::current_session->myscreen_->save_data,
        picker_lobby_is_networked(), additional_owned_team);
}

void picker_base_camp_after_roster_mutation()
{
    picker_base_camp_after_roster_mutation(-1);
}

// §2.6: gather the lobby/save inputs for the pure GO/READY state table.
// Global deployed: networked = every machine's replicated deploy flags (the
// server's rule-4 count); solo = the private roster. Own deployed reads the
// PRIVATE save (the wire mirrors it). Spectator = this machine contributes
// no character slots (numplayers==0 or an empty roster). That formatter shape
// has no deploy minimum [NET-R9]; Base Camp gives a true zero-seat client no
// READY action and exempts it from the server gate.
og::ui::ReadyGoPresentation picker_compute_ready_go_presentation()
{
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const bool networked = picker_lobby_is_networked();
    const int own_deployed = og::ui::count_deployed_members(save);
    int global_deployed = own_deployed;
    bool all_ready = true;
    if (networked)
    {
        const std::vector<og::sim::LobbyPlayer> players =
            picker_lobby_players();
        global_deployed = 0;
        for (const og::sim::LobbyPlayer& player : players)
        {
            for (const og::sim::LobbyCharacterSlot& slot :
                 player.character_slots)
            {
                if (slot.deployed)
                    ++global_deployed;
            }
        }
        const og::ui::BaseCampReadyCounts ready =
            og::ui::count_base_camp_ready_machines(players);
        all_ready = ready.ready == ready.machines;
    }
    const bool spectator = save.numplayers == 0 || save.team_size == 0;
    return og::ui::format_ready_go_button(
        networked, picker_lobby_host_controls_visible(),
        picker_lobby_local_ready(), all_ready, global_deployed, own_deployed,
        save.cross_control != 0, spectator);
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

// Non-static: the menu engine's RemoteStartScope::TeamBuildScope check
// (run_menu_screen) calls this too; declared in picker_sdl_defs.h.
bool team_build_remote_start_requested(Sint32& retvalue)
{
    if (!g_start_game_requested || !picker_lobby_has_game_start_config())
        return false;

    pks().selected_menu_item = og::ui::find_picker_menu_item(
        og::ui::PickerMenuId::TeamBuild,
        og::ui::PickerMenuCommand::StartGame);
    retvalue = MENU_EXIT;
    return true;
}

// Non-static: the engine wrappers for the team-build subscreens (SCENARIO in
// menu_screen_specs.cpp) fold their exits through this too.
bool team_build_start_selected()
{
    return pks().selected_menu_item != nullptr
        && pks().selected_menu_item->command ==
            og::ui::PickerMenuCommand::StartGame;
}

void sync_button_hidden_state(const button* buttons, int button_index)
{
    if (buttons == nullptr || button_index < 0)
        return;
    if (og::runtime::current_session->allbuttons_[static_cast<std::size_t>(button_index)] == nullptr)
        return;

    og::runtime::current_session->allbuttons_[static_cast<std::size_t>(button_index)]->hidden =
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

// The §2.5 base camp rewires its full roster graph per frame (pattern b) —
// the rewire lives on the spec in menu_screen_specs.cpp.

// Full-graph rewire for the SCENARIO subscreen (pattern b): two visibility
// axes — SET CAMPAIGN / SET LEVEL / TROOPS gate on the host,
// TEAMS / LIMIT (#218, re-homed from MATCHUP) gate on the versus campaign
// and stay visible to joiners as read-only labels. Every link is written on
// every call so no variant inherits a stale one; the parked spare (the
// retired MATCHUP door's ordinal) never participates.
void picker_wire_scenario_menu_nav(button* buttons,
                                   int count,
                                   bool host_controls_visible,
                                   bool match_settings_visible)
{
    if (buttons == nullptr || count < kScenarioMenuButtonCount)
        return;

    const bool host = host_controls_visible;
    const bool match = match_settings_visible;

    // Host column: SET CAMPAIGN over SET LEVEL over VIEW LEVEL.
    buttons[kScenarioMenuSetCampaignIndex].nav =
        {.down = kScenarioMenuSetLevelIndex};
    buttons[kScenarioMenuSetLevelIndex].nav =
        {.up = kScenarioMenuSetCampaignIndex,
         .down = kScenarioMenuViewScenarioIndex};

    // y=100 row: VIEW LEVEL <-> PROGRESS; up-links close for joiners.
    const int row_up = host ? kScenarioMenuSetLevelIndex : -1;
    buttons[kScenarioMenuViewScenarioIndex].nav =
        {.up = row_up,
         .down = match ? kScenarioMenuCtfTeamsIndex : kScenarioMenuBackIndex,
         .right = kScenarioMenuProgressIndex};
    buttons[kScenarioMenuProgressIndex].nav =
        {.up = row_up,
         .down = host ? kScenarioMenuTroopsIndex
                      : (match ? kScenarioMenuCtfCapsIndex
                               : kScenarioMenuBackIndex),
         .left = kScenarioMenuViewScenarioIndex};

    // y=140 match-settings band: TEAMS (30) | TROOPS (120) | LIMIT (210).
    // TROOPS is host-gated, TEAMS/LIMIT versus-gated, so the horizontal
    // chain skips whichever member is hidden this frame.
    buttons[kScenarioMenuCtfTeamsIndex].nav =
        {.up = kScenarioMenuViewScenarioIndex,
         .down = kScenarioMenuBackIndex,
         .right = host ? kScenarioMenuTroopsIndex
                       : kScenarioMenuCtfCapsIndex};
    buttons[kScenarioMenuTroopsIndex].nav =
        {.up = kScenarioMenuProgressIndex,
         .down = kScenarioMenuBackIndex,
         .left = match ? kScenarioMenuCtfTeamsIndex : -1,
         .right = match ? kScenarioMenuCtfCapsIndex : -1};
    buttons[kScenarioMenuCtfCapsIndex].nav =
        {.up = kScenarioMenuProgressIndex,
         .down = kScenarioMenuBackIndex,
         .left = host ? kScenarioMenuTroopsIndex
                      : kScenarioMenuCtfTeamsIndex};

    // BACK climbs into the nearest visible x=30 column member.
    buttons[kScenarioMenuBackIndex].nav =
        {.up = match ? kScenarioMenuCtfTeamsIndex
                     : kScenarioMenuViewScenarioIndex};

    // The parked spare: no links in, no links out (#236 precedent).
    buttons[kScenarioMenuSpareIndex].nav = {};
}

void sync_scenario_menu_host_control_visibility(button* buttons,
                                                int num_buttons,
                                                int& highlighted_button)
{
    if (buttons == nullptr || num_buttons < kScenarioMenuButtonCount)
        return;

    // SET CAMPAIGN / SET LEVEL keep their host-only visibility inside the
    // subscreen; VIEW LEVEL / PROGRESS stay visible for everyone.
    const bool host_controls_visible = picker_lobby_host_controls_visible();
    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    buttons[kScenarioMenuSetCampaignIndex].hidden = !host_controls_visible;
    buttons[kScenarioMenuSetLevelIndex].hidden = !host_controls_visible;
    buttons[kScenarioMenuTroopsIndex].hidden = !host_controls_visible;
    sync_button_hidden_state(buttons, kScenarioMenuSetCampaignIndex);
    sync_button_hidden_state(buttons, kScenarioMenuSetLevelIndex);
    // Re-derive the label from the save every frame: a host cycling TROOPS
    // reaches a joiner through the lobby settings, which land in the save
    // under the open menu (both label surfaces, per the menu skill).
    buttons[kScenarioMenuTroopsIndex].label =
        og::ui::format_ctf_troops_label(save);
    sync_button_hidden_state(buttons, kScenarioMenuTroopsIndex);
    if (og::runtime::current_session->allbuttons_[kScenarioMenuTroopsIndex] !=
        nullptr)
    {
        og::runtime::current_session->allbuttons_[kScenarioMenuTroopsIndex]
            ->label = buttons[kScenarioMenuTroopsIndex].label;
    }
    // TEAMS / LIMIT (#218, re-homed from MATCHUP): versus campaigns only,
    // and — unlike TROOPS — visible to JOINERS as read-only labels (the
    // host's turns land in the lobby-synced save and the same re-derive
    // shows them; change_ctf_teams/change_ctf_caps popup for a non-host).
    const bool match_settings_visible = og::ui::is_versus_campaign(save);
    for (const int index :
         {kScenarioMenuCtfTeamsIndex, kScenarioMenuCtfCapsIndex})
    {
        buttons[index].hidden = !match_settings_visible;
        buttons[index].label = index == kScenarioMenuCtfTeamsIndex
            ? og::ui::format_ctf_teams_label(save)
            : og::ui::format_ctf_caps_label(save);
        sync_button_hidden_state(buttons, index);
        if (og::runtime::current_session
                ->allbuttons_[static_cast<std::size_t>(index)] != nullptr)
        {
            og::runtime::current_session
                ->allbuttons_[static_cast<std::size_t>(index)]
                ->label = buttons[index].label;
        }
    }
    // The retired MATCHUP door's ordinal stays parked: hidden, zero-size,
    // no nav (the #236 seat_rail_spare precedent) — re-asserted per frame
    // because the engine's gate pass marks ungated rows visible.
    buttons[kScenarioMenuSpareIndex].hidden = true;
    sync_button_hidden_state(buttons, kScenarioMenuSpareIndex);
    picker_wire_scenario_menu_nav(buttons, num_buttons,
                                  host_controls_visible,
                                  match_settings_visible);

    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);
}

void sync_difficulty_menu_visibility(button* buttons,
                                     int num_buttons,
                                     int& highlighted_button)
{
    if (buttons == nullptr || num_buttons < kDifficultyMenuButtonCount)
        return;

    // The six settings rows are LobbySettings-backed (difficulty included):
    // a joiner's click would be rejected by the server and the per-frame
    // label re-derive would immediately restore the host's value. Hide them
    // for non-hosts (the GO / SET LEVEL precedent). The CTRL row (#218,
    // §2.7) instead gates on the NETWORK axis: visible to every peer of a
    // networked lobby — a joiner keeps sight of the mode that changes their
    // rights (change_cross_control popups for them) — and hidden outright
    // in local sessions, where cross-control decides nothing.
    const bool host_controls_visible = picker_lobby_host_controls_visible();
    const bool networked = picker_lobby_is_networked();
    for (int index = kDifficultyMenuDifficultyIndex;
         index < kDifficultyMenuCrossControlIndex; ++index)
    {
        buttons[index].hidden = !host_controls_visible;
        sync_button_hidden_state(buttons, index);
    }
    buttons[kDifficultyMenuCrossControlIndex].hidden = !networked;
    sync_button_hidden_state(buttons, kDifficultyMenuCrossControlIndex);

    // Vertical cycle around whichever tail rows are visible this frame.
    buttons[kDifficultyMenuInfiniteGoldIndex].nav.down =
        networked ? kDifficultyMenuCrossControlIndex
                  : kDifficultyMenuBackIndex;
    buttons[kDifficultyMenuCrossControlIndex].nav.up =
        host_controls_visible ? kDifficultyMenuInfiniteGoldIndex
                              : kDifficultyMenuBackIndex;
    buttons[kDifficultyMenuCrossControlIndex].nav.down =
        kDifficultyMenuBackIndex;
    buttons[kDifficultyMenuBackIndex].nav.up = networked
        ? kDifficultyMenuCrossControlIndex
        : (host_controls_visible ? kDifficultyMenuInfiniteGoldIndex : -1);
    buttons[kDifficultyMenuBackIndex].nav.down = host_controls_visible
        ? kDifficultyMenuDifficultyIndex
        : (networked ? kDifficultyMenuCrossControlIndex : -1);

    ensure_highlighted_button_visible(buttons, num_buttons, highlighted_button);
}

// ... and the word that goes where those rows were. Hiding all six leaves a
// joiner looking at a heading over an empty panel with no clue why, and the
// door is one click off the Base Camp strip now — the screen a networked
// joiner lives on. Every sibling surface says whose call it is (the modes
// book prints "The host calls the rules."), so this one does too.
std::string difficulty_panel_caption()
{
    if (picker_lobby_host_controls_visible())
        return std::string();
    return std::string("The host sets these for everyone.");
}

// Per-session picker message buffer: access via current_session->message_.

#define STAT_NUM_OFFSET 42
#define STAT_COLOR   DARK_BLUE // color for normal stat text
#define STAT_CHANGED RED       // color for changed stat text
#define STAT_LEVELED LIGHT_BLUE   // color for leveled up stat text
#define STAT_DISABLED BLACK   // color for disabled stat text
#define STAT_DERIVED DARK_BLUE + 3

// Compute derived stats for a guy using the current screen's loader data.
// Non-static: the base-camp roster's HP column (menu_screen_specs.cpp)
// computes per-row derived hitpoints through this too.
og::ui::DerivedStats picker_compute_guy_derived_stats(const guy& g)
{
    // guy::family comes verbatim from the .gtl save file with no range check,
    // so an out-of-capacity value must not index the loader stat arrays.
    // loader::slot_for answers -1 for those; anything it accepts — including a
    // class-pack family, whose id is >= NUM_FAMILIES and which PIX() cannot
    // address at all — reads its OWN row, so a pack class's panel now shows its
    // declared stats instead of the soldier's.
    int pix = loader::slot_for(Order::Living, g.family);
    if (pix < 0)
    {
        pix = loader::slot_for(Order::Living, FAMILY_SOLDIER);
    }
    const auto* l = og::runtime::current_session->myscreen_->myloader;
    return og::ui::compute_derived_stats(g,
        l->hitpoints[static_cast<std::size_t>(pix)],
        l->damage[static_cast<std::size_t>(pix)],
        l->stepsizes[static_cast<std::size_t>(pix)],
        l->fire_frequency[static_cast<std::size_t>(pix)]);
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

// create_team_menu (TEAM BUILD -> BASE CAMP, §2.5): engine-hosted — the
// roster spec, its content/rewire/frame hooks, and the entry wrapper live in
// menu_screen_specs.cpp (docs/menu-engine.md). The VIEW TEAM screen retired
// into the roster view.

// ---------------------------------------------------------------------------
// VIEW LEVEL: read-only roster report of the current scenario, rendered from
// a scratch headless level load (never touches the live picker world).
// ---------------------------------------------------------------------------

// PREV/NEXT handler: records the requested page step and returns MENU_OK so
// both vbutton::leftclick and handle_menu_nav fire it; the engine screen's
// consume_click hook turns the step into a clamped page flip.
Sint32 view_scenario_page_flip(Sint32 step)
{
    pks().view_scenario_page_step = (step < 0) ? -1 : 1;
    return MENU_OK;
}

// VIEW LEVEL: engine-hosted (§1.8 step 6; the spec lives in
// menu_screen_specs.cpp, docs/menu-engine.md). The open screen's report and
// PageModel live in the wrapper; the hooks read them through a file-static
// pointer (the rewire and state-override signatures carry no screen_state).
// Null state = no open screen (the G5 sweep and the
// gate-lattice sweep drive the spec bare) and presents the single-page
// shape: pagers hidden, BACK's right-link closed.
// The change key of everything the VIEW LEVEL report reads that a lobby can
// move under a parked viewer: the level, the five match-request knobs, the
// campaign identity (save side + actual mount) and the STAGE GENERATION —
// the heavy world rebuild lives in MatchStage behind its 250 ms debounce
// (roster edits and knob turns move the owner's change key, restage, and
// bump the generation), so the frame tick's job is only the cheap
// deserialize + apply + line rebuild. A host cycling TEAMS / TROOPS /
// SET LEVEL while a joiner sits in the viewer changes this key and the
// frame tick rebuilds the report (the pre-#218 screen never refreshed).
struct ViewScenarioKey
{
    int level_id = -1;
    short team_count = 0;
    short strip_troops = 0;
    short capture_limit = 0;
    short respawn_ticks = 0;
    // #241: the staged preview bakes the resolved match clock, so a TIME
    // LIMIT change must invalidate the cached staged world like any other
    // knob — without it the viewer keeps promising the old deadline.
    short time_limit = 0;
    std::uint32_t stage_generation = 0;
    // Seat block (#218): digest of the displayed seat facts. REQUIRED as a
    // key member because ready flips deliberately never restage
    // (MatchStageInputs excludes ready bits), so without it a parked viewer
    // would show stale [RDY]/team seat lines.
    std::uint64_t seat_digest = 0;
    std::string save_campaign;
    std::string mounted_campaign;

    bool operator==(const ViewScenarioKey&) const = default;
};

struct ViewScenarioEngineState
{
    og::ui::PageModel pager;
    std::vector<std::string> lines;
    std::string title;
    // The SDL-hooked scratch level: the RENDER COPY the staged preview band
    // heals from the broadcast pair bytes (the mandatory local load — a
    // snapshot cannot rebuild a level), and the fallback census world when
    // nothing is staged. Reloaded when the key's level/campaign moves.
    std::unique_ptr<LevelRuntimeData> scenario;
    // Render-copy heal bookkeeping: the generation whose pair the copy
    // holds, and whether the copy currently presents a healed staged world
    // (false = degradation text in the band).
    std::uint32_t healed_generation = 0;
    bool render_healed = false;
    // The heal's own GameplayContext (the obmap/current_game discipline the
    // mirror uses): apply_snapshot requires a bound context whose world IS
    // the target, so the heal swaps this in around the applies and restores
    // the picker's context after.
    GameplayContext heal_ctx;
    SaveData heal_save;
    og::sim::SimEventLog heal_events;
    IRandom* heal_rng_ptr = nullptr;
    bool heal_active = false;
    ViewScenarioKey key;
};

static ViewScenarioEngineState* g_view_scenario_engine_state = nullptr;

// The seat context every VIEW LEVEL key read and rebuild consumes: the
// replicated lobby seats (falling back to the shared local synthesis when
// no lobby list exists — solo shows its own seats), plus this machine's
// seat indices. Non-networked sessions mark every seat local: every seat
// reads YOU.
static og::ui::ScenarioSeatContext view_scenario_seat_context(
    const SaveData& save)
{
    og::ui::ScenarioSeatContext context;
    context.players = picker_lobby_players();
    if (context.players.empty())
        context.players = og::ui::synthesize_local_lobby_players(save);
    context.local_player_indices = picker_lobby_local_player_indices();
    if (!picker_lobby_is_networked())
    {
        context.local_player_indices.clear();
        for (const og::sim::LobbyPlayer& player : context.players)
            context.local_player_indices.push_back(player.player_index);
    }
    return context;
}

// FNV-1a over exactly the seat facts the report displays (player_index,
// team, ready, company, is_local) — the ViewScenarioKey member that makes
// restage-less seat changes (a ready flip) refresh a parked viewer.
static std::uint64_t view_scenario_seat_digest(
    const og::ui::ScenarioSeatContext& context)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    for (const og::sim::LobbyPlayer& player : context.players)
    {
        mix(player.player_index);
        mix(static_cast<std::uint16_t>(player.team));
        mix(player.ready ? 1u : 0u);
        const bool local =
            std::find(context.local_player_indices.begin(),
                      context.local_player_indices.end(),
                      player.player_index) !=
            context.local_player_indices.end();
        mix(local ? 1u : 0u);
        for (const char ch : player.company)
            mix(static_cast<std::uint8_t>(ch));
        mix(0xffu);  // field terminator: company bytes never bleed across
    }
    return hash;
}

// One key read per frame; the same values feed both the comparison and any
// rebuild, so a mid-poll lobby update can never split the key from the
// report it stamps.
static ViewScenarioKey view_scenario_current_key(const SaveData& save)
{
    ViewScenarioKey key;
    key.level_id = save.scen_num;
    key.team_count = save.ctf_team_count;
    key.strip_troops = save.ctf_strip_scenario_troops;
    key.capture_limit = save.ctf_capture_limit;
    key.respawn_ticks = save.ctf_respawn_ticks;
    key.time_limit = save.time_limit;
    key.save_campaign = save.current_campaign;
    key.mounted_campaign = get_mounted_campaign();
    og::ui::IPickerLobbyClient* const lobby =
        og::ui::active_picker_lobby_client();
    key.stage_generation = lobby != nullptr ? lobby->stage_generation() : 0;
    key.seat_digest =
        view_scenario_seat_digest(view_scenario_seat_context(save));
    return key;
}

// Heal the RENDER COPY from the lobby client's serialized staged pair: the
// host pane and every joiner pane deserialize the SAME bytes the wire
// carries (networked exactness is byte-level, not re-derived). apply runs
// only when the generation moved; failure is honest (the band renders the
// degradation text, never a stale half-applied world claiming to be the
// stage). The pair's level must match the loaded scratch level — a restage
// to a different level heals after the lobby-synced save reloads the copy.
static void view_scenario_heal_render_copy(ViewScenarioEngineState& state)
{
    og::ui::IPickerLobbyClient* const lobby =
        og::ui::active_picker_lobby_client();
    std::uint32_t generation = 0;
    const std::vector<std::uint8_t>* setup_bytes = nullptr;
    const std::vector<std::uint8_t>* keyframe_bytes = nullptr;
    if (lobby == nullptr || state.scenario == nullptr ||
        !lobby->staged_keyframe_bytes(generation, setup_bytes,
                                      keyframe_bytes))
    {
        state.render_healed = false;
        state.healed_generation = 0;
        return;
    }
    if (state.render_healed && state.healed_generation == generation)
        return;
    state.render_healed = false;
    state.healed_generation = generation;
    const std::optional<og::sim::InitialSetupMessage> setup =
        og::sim::deserialize_initial_setup_message(*setup_bytes);
    if (!setup.has_value() ||
        setup->level_id != state.scenario->world().id)
        return;
    og::sim::WorldSnapshot snapshot;
    try
    {
        snapshot = og::sim::deserialize_snapshot(*keyframe_bytes);
    }
    catch (const std::exception&)
    {
        return;
    }
    // The apply discipline the preview mirror uses: apply_snapshot requires
    // a bound GameplayContext whose world IS the target, so the heal swaps
    // its own context in and restores the picker's after. The apply runs
    // under its own event suppression guard — the pane can never
    // manufacture announcements, and the tick-0 apply's on_load re-arm is
    // inert (the pane never ticks). Stripped troops reconcile away;
    // snapshot-created bots get sprites through the same
    // create_missing_entities + SDL loader wiring a mid-game joiner uses.
    GameWorld& render_world = state.scenario->world();
    state.heal_ctx.world = &render_world;
    state.heal_ctx.save = &state.heal_save;
    state.heal_ctx.sim_events = &state.heal_events;
    state.heal_ctx.config = &cfg;
    state.heal_rng_ptr = &render_world.rng_;
    state.heal_ctx.session_rng_ref = &state.heal_rng_ptr;
    state.heal_ctx.gameplay_active_ref = &state.heal_active;
    GameplayContext* const previous_context = current_game;
    current_game = &state.heal_ctx;
    og::sim::apply_initial_setup_to_world(render_world, *setup);
    const bool applied = og::sim::apply_snapshot(render_world, snapshot);
    current_game = previous_context;
    if (!applied)
        return;
    state.render_healed = true;
    TRACE("picker", "view_scenario pane gen=%u",
          static_cast<unsigned>(generation));
}

// (Re)build the report + lines + pager + title: the STAGED reader (host
// stage / joiner mirror through the lobby client), with the scratch level
// as the count-only fallback world when nothing is staged.
static void view_scenario_rebuild(ViewScenarioEngineState& state,
                                  const SaveData& save)
{
    og::ui::IPickerLobbyClient* const lobby =
        og::ui::active_picker_lobby_client();
    const GameWorld* staged =
        lobby != nullptr ? lobby->staged_world() : nullptr;
    og::ui::StagePreviewStatus status = og::ui::StagePreviewStatus::None;
    if (lobby != nullptr &&
        lobby->staged_preview_health() ==
            og::ui::IPickerLobbyClient::StagedPreviewHealth::Failed)
        status = og::ui::StagePreviewStatus::Failed;
    else if (staged != nullptr)
        status = og::ui::StagePreviewStatus::Staged;
    // A staged pair for a DIFFERENT level than the save's (a restage racing
    // the lobby save sync) must not caption this level's screen.
    if (staged != nullptr && staged->id != save.scen_num)
        staged = nullptr;
    const og::ui::ScenarioSeatContext seat_context =
        view_scenario_seat_context(save);
    const og::ui::ScenarioRosterReport report =
        og::ui::build_scenario_roster_report(
            staged, status, save,
            state.scenario != nullptr ? &state.scenario->world() : nullptr,
            &seat_context);
    state.lines = og::ui::format_scenario_report_lines(report);
    state.pager = og::ui::PageModel::make(
        static_cast<int>(state.lines.size()), kViewScenarioRowsPerPage);
    state.title =
        std::format("SCEN {}: {}", save.scen_num,
                    state.scenario->world().title);
    if (state.title.size() > 48)
        state.title.resize(48);
    // Test seam (no-op in production): the match block — the lines above
    // the first blank separator — so injector flows assert exact staged
    // census lines without racing the menu thread.
    for (const std::string& line : state.lines)
    {
        if (line.empty())
            break;
        TRACE("picker", "view_scenario line %s", line.c_str());
    }
}

// Per-frame refresh guard: rebuild the cached report when the change key
// moved (host SET LEVEL / TEAMS / TROOPS under a parked joiner — the
// blocking-subscreen level-reload discipline; a stage-generation move —
// the owner's debounced restage or a joiner mirror refresh — re-heals the
// render copy first). Never exits the loop.
bool picker_view_scenario_engine_frame_tick(void* /*screen_state*/,
                                            int /*frame*/)
{
    ViewScenarioEngineState* const state = g_view_scenario_engine_state;
    if (state == nullptr || state->scenario == nullptr)
        return true;
    const SaveData& save =
        og::runtime::current_session->myscreen_->save_data;
    ViewScenarioKey key = view_scenario_current_key(save);
    if (key == state->key)
        return true;
    const bool level_moved =
        key.level_id != state->key.level_id ||
        key.save_campaign != state->key.save_campaign ||
        key.mounted_campaign != state->key.mounted_campaign;
    state->key = std::move(key);
    if (level_moved)
    {
        // Mirror the entry guards without popups (mid-frame): an unmounted
        // campaign or a failed load renders its refusal as the report.
        if (get_mounted_campaign() != save.current_campaign)
        {
            state->lines = {"CAMPAIGN NOT MOUNTED"};
            state->pager = og::ui::PageModel::make(
                static_cast<int>(state->lines.size()),
                kViewScenarioRowsPerPage);
            state->title = std::format("SCEN {}:", save.scen_num);
            state->render_healed = false;
            return true;
        }
        auto fresh = std::make_unique<LevelRuntimeData>(
            save.scen_num, false, &sdl_level_data_hooks());
        if (!fresh->load())
        {
            state->lines = {"COULD NOT LOAD LEVEL"};
            state->pager = og::ui::PageModel::make(
                static_cast<int>(state->lines.size()),
                kViewScenarioRowsPerPage);
            state->title = std::format("SCEN {}:", save.scen_num);
            state->render_healed = false;
            return true;
        }
        state->scenario = std::move(fresh);
        state->render_healed = false;
        state->healed_generation = 0;
    }
    view_scenario_heal_render_copy(*state);
    view_scenario_rebuild(*state, save);
    TRACE("picker", "view_scenario refresh lines=%d page=%d",
          static_cast<int>(state->lines.size()), state->pager.page);
    return true;
}

// PREV/NEXT visibility: the legacy entry-time `hidden = !multi_page()`,
// re-derived per frame (page count never changes while the screen is open).
og::ui::RowState picker_view_scenario_engine_pager_row_state(
    const og::ui::MenuLabelContext& /*context*/)
{
    const ViewScenarioEngineState* const state = g_view_scenario_engine_state;
    return (state != nullptr && state->pager.multi_page())
        ? og::ui::RowState::Visible
        : og::ui::RowState::Hidden;
}

// The legacy entry-time nav closure (`back.nav.right = -1` when one page),
// re-asserted per frame over the static base link.
void picker_view_scenario_engine_rewire(button* buttons, int num_buttons,
                                        int& /*highlighted_button*/)
{
    if (num_buttons <= kViewScenarioBackIndex)
        return;
    const ViewScenarioEngineState* const state = g_view_scenario_engine_state;
    const bool multi_page = state != nullptr && state->pager.multi_page();
    buttons[kViewScenarioBackIndex].nav.right =
        multi_page ? kViewScenarioPrevIndex : -1;
}

// The legacy page-step consumption, verbatim at the legacy frame point:
// PREV/NEXT carry ButtonAction::ViewScenarioPageFlip — both mouse leftclick
// and keyboard FIRE route through do_call, which records the requested step
// and returns MENU_OK. Consume the step here (retvalue-zero + clamped flip
// + the flip trace).
Sint32 picker_view_scenario_engine_consume_click(Sint32 retvalue,
                                                 void* /*screen_state*/)
{
    ViewScenarioEngineState* const state = g_view_scenario_engine_state;
    if (state == nullptr)
        return retvalue;
    const int step = pks().view_scenario_page_step;
    pks().view_scenario_page_step = 0;
    if (step != 0)
    {
        retvalue = 0;
        if (state->pager.step(step))
        {
            TRACE("picker", "view_scenario lines=%d page=%d",
                  static_cast<int>(state->lines.size()), state->pager.page);
        }
    }
    return retvalue;
}

// The per-frame content pass (runs after draw_buttons — the report frame at
// (5,5,314,160) never covers the y>=170 buttons). The census rows sit BELOW
// the staged preview band the background pass painted; the band region
// itself is never overdrawn here.
void picker_view_scenario_engine_draw_content(void* /*screen_state*/)
{
    const ViewScenarioEngineState* const state = g_view_scenario_engine_state;
    if (state == nullptr)
        return;
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    mytext.write_xy(10, 8, state->title.c_str(), static_cast<unsigned char>(BLACK), 1);
    const int first_line = state->pager.first_index();
    for (int line_index = first_line; line_index < state->pager.end_index();
         ++line_index)
    {
        mytext.write_xy(10,
                        kViewScenarioCensusTopY +
                            (line_index - first_line) * kViewScenarioRowPitch,
                        state->lines[static_cast<std::size_t>(line_index)].c_str(),
                        static_cast<unsigned char>(BLACK), 1);
    }
    if (state->pager.multi_page())
    {
        mytext.write_xy(140, 176, state->pager.indicator().c_str(), WHITE, 1);
    }
}

// Deterministic render-only ping-pong in [0, span]: a triangle wave over the
// classic UI clock (query_timer, ~13.6 ms grain). Wall-clock cosmetic —
// zero sim contact, zero RNG; tests assert band content, never pan phase.
static Sint32 preview_pan_offset(Sint32 span, Sint32 ticks_per_px)
{
    if (span <= 0 || ticks_per_px <= 0)
        return 0;
    const Sint32 phase = (query_timer() / ticks_per_px) % (span * 2);
    return phase < span ? phase : span * 2 - phase;
}

// THE INVARIANT (#251): no menu draw may observe the preview camera. Every
// menu pixie — the backdrop quadrants, the graphic buttons, the main-menu
// logo, the HIRE/TRAIN portrait walker — is blitted as
// `xpos - view->topx + view->xloc` through viewob[0], so any camera or
// geometry this preview leaves on the view displaces the whole menu until
// level start (the HIRE "empty box" report). Two halves keep it true:
// draw_backdrop() below must stay ABOVE the borrow, and this guard restores
// every field the borrow writes.
//
// It writes more than it looks: nulling control makes viewscreen::redraw take
// its control-less branch, which copies the staged level's camera onto the
// view (render/view.cpp: topx = data->level_visuals().topx), and the band
// resize rewrites the whole render rect. topx/topy are otherwise assigned
// exactly once, at construction — viewscreen::resize never touches them — so
// no relayout can undo them; restoring the rect here as well is both exact
// (screen::relayout_views re-derives it from prefs[PREF_VIEW] instead of the
// PREF_VIEW_FULL the picker forces at entry) and free of relayout's side
// effects. Destructor-based because the healed branch below early-returns; a
// restore written at the end of the function never runs.
namespace
{
class ScopedBorrowedView
{
public:
    explicit ScopedBorrowedView(viewscreen& view)
        : view_(view), topx_(view.topx), topy_(view.topy),
          xloc_(view.xloc), yloc_(view.yloc), xview_(view.xview),
          yview_(view.yview), endx_(view.endx), endy_(view.endy),
          slot_x_(view.slot_x_), slot_y_(view.slot_y_), slot_w_(view.slot_w_),
          slot_h_(view.slot_h_), control_(view.control),
          following_(view.following_)
    {
    }

    ~ScopedBorrowedView()
    {
        view_.topx = topx_;
        view_.topy = topy_;
        view_.xloc = xloc_;
        view_.yloc = yloc_;
        view_.xview = xview_;
        view_.yview = yview_;
        view_.endx = endx_;
        view_.endy = endy_;
        view_.slot_x_ = slot_x_;
        view_.slot_y_ = slot_y_;
        view_.slot_w_ = slot_w_;
        view_.slot_h_ = slot_h_;
        view_.control = control_;
        view_.following_ = following_;
    }

    ScopedBorrowedView(const ScopedBorrowedView&) = delete;
    ScopedBorrowedView& operator=(const ScopedBorrowedView&) = delete;

private:
    viewscreen& view_;
    Sint32 topx_;
    Sint32 topy_;
    Sint32 xloc_;
    Sint32 yloc_;
    Sint32 xview_;
    Sint32 yview_;
    Sint32 endx_;
    Sint32 endy_;
    Sint32 slot_x_;
    Sint32 slot_y_;
    Sint32 slot_w_;
    Sint32 slot_h_;
    walker* control_;
    bool following_;
};
}  // namespace

// The staged-preview background pass (#218): the classic backdrop, then the
// STAGED world rendered into the preview band through the borrowed viewob[0]
// (the demo Center-camera shape: direct-geometry resize, control-less free
// camera from the level's own LevelVisuals — TRAP B dangling-control never
// applies because the pane never points control at a staged walker). Slow
// horizontal pan surveys pitches wider than the band (direct-geometry views
// have no zoom); ScopedBorrowedView restores the camera and the geometry.
// Degradation states render text into the band — never a crash, never a
// stale world presented as the stage; the census fallback lines still render
// below through the content pass.
void picker_view_scenario_staged_draw_background(void* screen_state)
{
    (void)screen_state;
    screen* const scr = og::runtime::current_session->myscreen_;
    // The classic picker frame first (the shared backdrop pass's shape).
    scr->clearbuffer();
    draw_backdrop();
    text& mytext = scr->text_normal;
    scr->draw_button(kViewScenarioFrameX, kViewScenarioFrameY,
                     kViewScenarioFrameW, kViewScenarioFrameH, 2, 1);
    const ViewScenarioEngineState* const state = g_view_scenario_engine_state;
    if (state != nullptr && state->render_healed && state->scenario != nullptr &&
        state->scenario->level_visuals().renderer_ != nullptr &&
        scr->viewob[0] != nullptr)
    {
        viewscreen* const view = scr->viewob[0].get();
        const ScopedBorrowedView borrowed(*view);
        view->control = nullptr;
        view->following_ = false;
        // TRAP C: redraw(data,...) draws the view's message strip
        // unconditionally; staging never writes it, and this clears any
        // gameplay residue.
        view->clear_text();
        GameWorld& staged_world = state->scenario->world();
        LevelVisuals& visuals = state->scenario->level_visuals();
        const Sint32 span_x = std::max(
            0, static_cast<int>(staged_world.pixmaxx) -
                   kViewScenarioPreviewBandW);
        const Sint32 span_y = std::max(
            0, static_cast<int>(staged_world.pixmaxy) -
                   kViewScenarioPreviewBandH);
        // ~4 timer ticks (~55 ms) per pixel horizontally; the vertical
        // wave is 4x slower and centered, so the pan surveys the whole
        // staged world without racing it.
        visuals.topx = preview_pan_offset(span_x, 4);
        visuals.topy = span_y / 2 +
            (span_y > 0 ? preview_pan_offset(span_y, 16) - span_y / 2 : 0);
        {
            ScopedUiCanvas ui_canvas(*scr);
            view->resize(kViewScenarioPreviewBandX, kViewScenarioPreviewBandY,
                         kViewScenarioPreviewBandW, kViewScenarioPreviewBandH);
            (void)view->redraw(state->scenario.get(), /*draw_radar=*/false);
        }
        return;
    }

    // Degradation text centered in the band (6 px font, 6 px per char).
    const char* band_text = "PREVIEW UNAVAILABLE";
    if (state != nullptr)
    {
        og::ui::IPickerLobbyClient* const lobby =
            og::ui::active_picker_lobby_client();
        using Health = og::ui::IPickerLobbyClient::StagedPreviewHealth;
        const Health health = lobby != nullptr
            ? lobby->staged_preview_health()
            : Health::None;
        if (health == Health::Failed)
            band_text = "STAGING FAILED";
        else if (health == Health::None &&
                 picker_lobby_is_networked() &&
                 !picker_lobby_host_controls_visible())
            band_text = "WAITING FOR HOST PREVIEW";
        else if (health == Health::Unavailable || health == Health::None)
            band_text = "STAGING PREVIEW UNAVAILABLE";
    }
    const int text_w = static_cast<int>(std::strlen(band_text)) * 6;
    mytext.write_xy(
        kViewScenarioPreviewBandX +
            std::max(0, (kViewScenarioPreviewBandW - text_w) / 2),
        kViewScenarioPreviewBandY + kViewScenarioPreviewBandH / 2 - 3,
        band_text, static_cast<unsigned char>(BLACK), 1);
}

// VIEW LEVEL, engine-hosted (the legacy loop is gone). The pre-loop guards,
// the scratch level load, and the exit shape are the legacy code, verbatim:
// BACK returns MENU_REDRAW; a remote start propagates its MENU_EXIT.
Sint32 create_view_scenario_menu(Sint32 arg1)
{
    (void)arg1;
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
    ViewScenarioEngineState state;
    state.scenario = std::make_unique<LevelRuntimeData>(
        save.scen_num, false, &sdl_level_data_hooks());
    if (!state.scenario->load())
    {
        popup_dialog("VIEW LEVEL", "COULD NOT\nLOAD LEVEL");
        return MENU_REDRAW;
    }

    // The staged reader needs no roster marshaling: the staged world (host
    // stage / joiner mirror) already CONTAINS the combined lobby rosters as
    // spawned walkers, and the render copy heals from the same broadcast
    // bytes. Read the key once here; the frame tick re-reads per frame.
    state.key = view_scenario_current_key(save);
    view_scenario_heal_render_copy(state);
    // The pinned VIEW LEVEL pager runs on the engine PageModel (G6): page
    // count, clamped flips, hidden-when-one-page, and the "p/N" indicator
    // all come from the model; the oracle test in tests/unit/test_menu_spec
    // pins its equivalence to the legacy arithmetic this replaced.
    view_scenario_rebuild(state, save);

    TRACE("picker", "view_scenario lines=%d page=%d",
          static_cast<int>(state.lines.size()), state.pager.page);

    og::runtime::current_session->myscreen_->clearbuffer();

    pks().view_scenario_page_step = 0;
    g_view_scenario_engine_state = &state;
    const Sint32 retvalue = og::ui::run_menu_screen(
        og::ui::view_scenario_menu_screen_spec(), &state);
    g_view_scenario_engine_state = nullptr;

    og::runtime::current_session->myscreen_->clearbuffer();

    if (retvalue & MENU_EXIT)
        return retvalue;

    return MENU_REDRAW;
}

// ---------------------------------------------------------------------------
// SCENARIO subscreen: SET CAMPAIGN / SET LEVEL (host-gated) with the
// campaign-name / level-title strips alongside, plus the always-visible
// VIEW LEVEL | PROGRESS row. Blocking-subscreen pattern: per-frame
// picker_lobby_poll, joiner remote-start honored (propagates MENU_EXIT with
// the StartGame item), BACK returns MENU_REDRAW to the team-build screen.
// ---------------------------------------------------------------------------

// create_scenario_menu (the SCENARIO subscreen): engine-hosted — the spec,
// the title-strip content pass, the level-reload guard, and the entry
// wrapper live in menu_screen_specs.cpp (docs/menu-engine.md).

// Helper struct for progress menu
struct LevelProgress {
    int id;
    std::string title;
    int num_enemies;
    bool is_cleared;
    bool is_current;
};

std::vector<int> get_accessible_levels();

// ---------------------------------------------------------------------------
// PROGRESS: engine-hosted (§1.8 step 6; the spec lives in
// menu_screen_specs.cpp, docs/menu-engine.md). PREV/NEXT are KEYBOARD-DEAD
// by design (myfun = 0 — the shipped screen; fixing that is a visible
// change deferred past Layer E): the raw mouse-rect dispatch, the per-row
// GO shortcut scan, and the held-click spin-wait live in the frame_tick
// hook, verbatim. The screen draws over a plain cleared buffer (no
// backdrop). NOTE the draw-order normalization: legacy painted the report
// BEFORE its three buttons; the runner paints buttons first — no content
// pixel reaches y >= 170, so the output is identical.
// ---------------------------------------------------------------------------

// Per-open screen state (the legacy loop's locals), owned by the wrapper.
struct ProgressEngineState
{
    std::vector<LevelProgress> levels;
    int num_cleared = 0;
    int scroll_offset = 0;
    int visible_rows = 10;
};

// The legacy button rects, needed by the raw mouse-rect dispatch (the
// engine spec transcribes the same values).
constexpr UiRect kProgressPrevBtn = {30, 170, 40, 20};
constexpr UiRect kProgressNextBtn = {80, 170, 40, 20};

// #207: the per-row action hit-test, shared by the frame_tick dispatch and
// the coverage harness. Uncleared rows keep the legacy GO rect; cleared
// rows carry TWO buttons — VISIT (the classic plain cursor write: the
// purged empty walk-through, kept deliberately for cleared-road traversal
// and re-branching) and REPLAY (the arm: the level loads with its authored
// census restored and a win returns the cursor home). REPLAY stays
// right-aligned with BACK at x=310; VISIT sits left of it on the same row
// grid. The Foes column moved left (206/210, was 250/258) and the title
// budget dropped to 17 chars to clear the two-button column — cleared rows
// now show the AUTHORED foe count (the honest REPLAY number; the old
// hardcoded 0 was only accidentally true while the purge emptied every
// replay, and SET LEVEL's browser already advertises the authored count).
constexpr int kProgressRowY = 36;
constexpr int kProgressRowHeight = 13;
constexpr int kProgressTitleChars = 17;   // 14 + "..." — ends x=202
constexpr int kProgressFoesHeaderX = 206;
constexpr int kProgressFoesNumberX = 210;
constexpr int kProgressGoHitX = 295, kProgressGoHitW = 20;
constexpr int kProgressVisitHitX = 230, kProgressVisitHitW = 36;
constexpr int kProgressReplayHitX = 270, kProgressReplayHitW = 40;

// What a row click means (#207): GO on the frontier, VISIT (plain write)
// or REPLAY (arm) on a cleared row.
enum class ProgressRowAction
{
    None,
    Go,
    Visit,
    Replay,
};

struct ProgressRowHit
{
    int id = -1;
    ProgressRowAction action = ProgressRowAction::None;
};

// Whether this report's rows carry a GO/REPLAY affordance at all. They are
// the SET LEVEL write in another shape, so a joiner does not get one: the
// report stays open to read (that is the screen's whole job), but no row
// invites a click whose only possible answer is a refusal. The refusal is a
// popup, and popup_dialog runs its own event loop with no lobby poll in it
// — a joiner behind that OK button stops applying lobby messages and cannot
// follow the host's GO until they dismiss it. The rows say "(HOST)" where
// the button was, the same word the Base Camp level rows wear.
bool progress_rows_actionable()
{
    return picker_lobby_host_controls_visible();
}

// The click-time answer behind GO, VISIT and REPLAY, shared with the tests:
// the SET LEVEL host gate (this screen had none — a networked joiner could
// write scen_num here; the rows no longer offer a joiner the click, and this
// stays as the write's own guard) and the earned-roads predicate, each
// refusing with the popup (trace-only under TESTING) before any cursor
// write. `replay_arm` (#207) swaps the plain cursor write for the replay
// arm — same gates, and arm_replay moves scen_num itself so the lobby
// publish and go_menu behave identically either way. True = the write
// landed and the report exits.
bool progress_row_click_applies(int hit_id, bool replay_arm)
{
    if (!picker_lobby_host_controls_visible())
    {
        TRACE("picker", "progress_row_denied_nonhost %d", hit_id);
        popup_dialog(
            "PROGRESS",
            std::string(og::ui::kCampaignPickerHostGuardMessage).c_str());
        return false;
    }
    if (!og::data::level_selection_allowed(
            og::runtime::current_session->myscreen_->save_data, hit_id))
    {
        TRACE("picker", "progress_row_denied_gate %d", hit_id);
        popup_dialog(
            "PROGRESS",
            std::string(og::ui::kCampaignLevelClosedMessage).c_str());
        return false;
    }
    if (replay_arm)
    {
        // REPLAY: arm the excursion (this also sets scen_num) — the level
        // loads restored and the win fold returns the cursor to the
        // position the player left.
        og::runtime::current_session->myscreen_->save_data.arm_replay(
            static_cast<short>(hit_id));
        TRACE("picker", "progress_row_replay_armed %d", hit_id);
    }
    else
    {
        // A plain cursor write (VISIT, or GO on an uncleared row) abandons
        // any excursion in flight: the stale arm must not hijack this
        // level's purge or restore a cursor the player just re-pointed.
        og::runtime::current_session->myscreen_->save_data.clear_replay_arm();
        // Set current level and exit
        og::runtime::current_session->myscreen_->save_data.scen_num =
            static_cast<short>(hit_id);
    }
    picker_lobby_sync_settings_from_save();
    return true;
}

ProgressRowHit progress_row_action_hit(const ProgressEngineState& state,
                                       int mx, int my)
{
    if (!progress_rows_actionable())
        return {};  // no affordance drawn, no hit to answer
    int row_y = kProgressRowY;
    for (int i = state.scroll_offset;
         i < static_cast<int>(state.levels.size()) &&
         i < state.scroll_offset + state.visible_rows;
         i++) {
        const LevelProgress& lp = state.levels[static_cast<std::size_t>(i)];
        if (my >= row_y && my <= row_y + kProgressRowHeight) {
            if (lp.is_cleared) {
                if (mx >= kProgressVisitHitX &&
                    mx <= kProgressVisitHitX + kProgressVisitHitW) {
                    return {lp.id, ProgressRowAction::Visit};
                }
                if (mx >= kProgressReplayHitX &&
                    mx <= kProgressReplayHitX + kProgressReplayHitW) {
                    return {lp.id, ProgressRowAction::Replay};
                }
            } else if (mx >= kProgressGoHitX &&
                       mx <= kProgressGoHitX + kProgressGoHitW) {
                return {lp.id, ProgressRowAction::Go};
            }
        }
        row_y += kProgressRowHeight;
    }
    return {};
}

void picker_progress_menu_engine_draw_background(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->clearbuffer();
}

// The legacy custom input block, verbatim: held-click spin-wait (lobby kept
// alive; a remote start ends the wait and fires the loop-top check next
// frame — team_build_remote_start_requested is idempotent), raw-rect
// PREV/NEXT scrolling, and the per-row GO shortcut (false => the runner
// exits with MENU_REDRAW, the legacy return). The legacy block also read
// retvalue == MENU_OK for keyboard PREV/NEXT — dead code (their myfun is 0,
// so neither leftclick nor handle_menu_nav ever produced MENU_OK here) —
// and that dead branch does not survive.
bool picker_progress_menu_engine_frame_tick(void* screen_state, int /*frame*/)
{
    auto* const state = static_cast<ProgressEngineState*>(screen_state);
    if (state == nullptr)
        return true;

    MouseState& mymouse = query_mouse();
    bool clicked = mymouse.left;
    const int mx = static_cast<int>(mymouse.x);
    const int my = static_cast<int>(mymouse.y);
    if (clicked) {
        while (mymouse.left) {
            picker_lobby_poll();
            Sint32 remote_retvalue = 0;
            if (team_build_remote_start_requested(remote_retvalue))
                return true;
            og::input_native::sleep_ms(1);
            get_input_events(POLL);
        }
    }

    bool prev_enabled = (state->scroll_offset > 0);
    bool next_enabled = (state->scroll_offset + state->visible_rows < static_cast<int>(state->levels.size()));

    bool do_prev = prev_enabled && clicked && kProgressPrevBtn.x <= mx && mx <= kProgressPrevBtn.x + kProgressPrevBtn.w
                   && kProgressPrevBtn.y <= my && my <= kProgressPrevBtn.y + kProgressPrevBtn.h;
    bool do_next = next_enabled && clicked && kProgressNextBtn.x <= mx && mx <= kProgressNextBtn.x + kProgressNextBtn.w
                   && kProgressNextBtn.y <= my && my <= kProgressNextBtn.y + kProgressNextBtn.h;

    if (do_prev) {
        state->scroll_offset--;
    }
    if (do_next) {
        state->scroll_offset++;
    }

    // Check for GO/VISIT/REPLAY button clicks on level rows (#207: cleared
    // rows are selectable two ways — VISIT is the classic plain cursor
    // write onto the purged walk-through, REPLAY arms the excursion so the
    // level loads restored and a win returns the cursor home; completion
    // marking stays idempotent and only the first win pays a time bonus).
    if (clicked) {
        const ProgressRowHit hit = progress_row_action_hit(*state, mx, my);
        if (hit.id >= 0) {
            if (!progress_row_click_applies(
                    hit.id, hit.action == ProgressRowAction::Replay))
                return true;  // refused: stay on the report
            og::runtime::current_session->myscreen_->clearbuffer();
            TRACE("picker", "progress_row_go level=%d", hit.id);
            return false;
        }
    }

    return true;
}

// The legacy report pass, verbatim: header, column bar, level rows with GO
// shortcuts, scroll indicator. No pixel reaches the y >= 170 button strip,
// so painting after draw_buttons (the runner order) is output-identical.
void picker_progress_menu_engine_draw_content(void* screen_state)
{
    auto* const state = static_cast<ProgressEngineState*>(screen_state);
    if (state == nullptr)
        return;
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    // Header
    std::string header = std::format("Level Progress: {} cleared of {} discovered",
             state->num_cleared, static_cast<int>(state->levels.size()));
    mytext.write_xy(160 - static_cast<int>(header.size()) * 3, 8, header.c_str(), DARK_GREEN, 1);

    // Column headers (#207: Foes moved left of the two-button column)
    og::runtime::current_session->myscreen_->draw_text_bar(10, 22, 310, 32);
    mytext.write_xy(12, 24, "ID", DARK_BLUE, 1);
    mytext.write_xy(36, 24, "Status", DARK_BLUE, 1);
    mytext.write_xy(100, 24, "Title", DARK_BLUE, 1);
    mytext.write_xy(kProgressFoesHeaderX, 24, "Foes", DARK_BLUE, 1);

    // Level rows
    const bool rows_actionable = progress_rows_actionable();
    int y = 36;
    int row_height = 13;
    for (int i = state->scroll_offset; i < static_cast<int>(state->levels.size()) && i < state->scroll_offset + state->visible_rows; i++) {
        LevelProgress& lp = state->levels[static_cast<std::size_t>(i)];

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

        // Enemy count — the authored number on every row (#207: REPLAY
        // restores the census, so the hardcoded 0 on cleared rows became a
        // lie; VISIT still walks the purged level, but the authored count
        // is what SET LEVEL's browser already advertises).
        buf = std::format("{}", lp.num_enemies);
        mytext.write_xy(kProgressFoesNumberX, y + 2, buf.c_str(),
                        lp.is_cleared ? DARK_GREEN : WHITE, 1);

        // The row's action. A joiner reads the report but does not set the
        // level, so the button is replaced by the word that says whose call
        // it is — no button, no click, no refusal popup.
        if (!rows_actionable) {
            mytext.write_xy(274, y + 2, "(HOST)", GREY, 1);
        } else if (lp.is_cleared) {
            // #207: a cleared row is selectable two ways — VISIT is the
            // classic plain cursor write (the purged walk-through, for
            // traversal and re-branching), REPLAY arms the excursion
            // (restored census, cursor returns home on the win). REPLAY
            // keeps its right edge aligned with BACK at x=310.
            og::runtime::current_session->myscreen_->draw_button(
                kProgressVisitHitX, y + 1,
                kProgressVisitHitX + kProgressVisitHitW, y + 10, 1, 1);
            mytext.write_xy(kProgressVisitHitX + 3, y + 2, "VISIT",
                            DARK_BLUE, 1);
            og::runtime::current_session->myscreen_->draw_button(
                kProgressReplayHitX, y + 1,
                kProgressReplayHitX + kProgressReplayHitW, y + 10, 1, 1);
            mytext.write_xy(kProgressReplayHitX + 2, y + 2, "REPLAY",
                            DARK_BLUE, 1);
        } else {
            // GO button for non-cleared levels (right edge aligns with BACK button at x=310)
            og::runtime::current_session->myscreen_->draw_button(292, y + 1, 310, y + 10, 1, 1);
            mytext.write_xy(296, y + 2, "GO", DARK_BLUE, 1);
        }

        y += row_height;
    }

    // Scroll indicator
    if (state->levels.size() > static_cast<size_t>(state->visible_rows)) {
        std::string scroll_info = std::format("{}-{} of {}",
                 state->scroll_offset + 1,
                 std::min(state->scroll_offset + state->visible_rows, static_cast<int>(state->levels.size())),
                 static_cast<int>(state->levels.size()));
        mytext.write_xy(140, 172, scroll_info.c_str(), WHITE, 1);
    }
}

// PROGRESS, engine-hosted (the legacy loop is gone). The level-report build
// and the exit shape are the legacy code, verbatim: every local exit (BACK,
// a GO shortcut) returns MENU_REDRAW; a remote start propagates its
// MENU_EXIT (legacy split both ways across its two check sites — normalized
// to the propagating shape).
Sint32 create_progress_menu(Sint32 arg1)
{
    // arg1 is part of the button.h signature but was never read by the
    // legacy loop (it only normalized it and moved on).
    (void)arg1;

    og::runtime::current_session->myscreen_->clearbuffer();

    // Get accessible levels
    std::vector<int> level_ids = get_accessible_levels();
    ProgressEngineState state;

    // Load level info for each accessible level
    for (int level_id : level_ids) {
        LevelProgress lp;
        lp.id = level_id;
        lp.is_cleared = og::runtime::current_session->myscreen_->save_data.is_level_completed(level_id);
        lp.is_current = (level_id == og::runtime::current_session->myscreen_->save_data.scen_num);

        if (lp.is_cleared)
            state.num_cleared++;

        // Load level to get title and enemy count
        LevelRuntimeData ld(level_id);
        if (ld.load()) {
            // #207: the title budget dropped 20 -> 17 chars so the Foes
            // column clears the VISIT/REPLAY button pair.
            if (ld.world().title.size() > kProgressTitleChars) {
                lp.title =
                    ld.world().title.substr(0, kProgressTitleChars - 3) +
                    "...";
            } else {
                lp.title = ld.world().title;
            }

            // Count enemies — the authored number on every row (#207).
            int num_enemies = 0;
            std::list<int> unused_exits;
            getLevelStats(ld, nullptr, nullptr, &num_enemies, nullptr, unused_exits);
            lp.num_enemies = num_enemies;
        } else {
            lp.title = std::format("Level {}", level_id);
            lp.num_enemies = 0;
        }

        state.levels.push_back(lp);
    }

    const Sint32 retvalue = og::ui::run_menu_screen(
        og::ui::progress_menu_screen_spec(), &state);

    og::runtime::current_session->myscreen_->clearbuffer();
    if (retvalue & MENU_EXIT)
        return retvalue;
    return MENU_REDRAW;
}

std::string get_class_description(unsigned char family)
{
    const auto* fd = get_family_descriptor(family);
    if (fd && fd->description)
        return fd->description;
    return {};
}

// stat is a StatAxis index (the caller walks the priced axes at runtime,
// which is why stat_costs stays an array).
	const char* get_training_cost_rating(unsigned char family, int stat)
	{
	    const auto* fd = get_family_descriptor(family);
	    if (!fd) return "";
	    if (stat < 0 || stat >= StatAxis::Count || fd->stat_costs[stat] == 0) return "";
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

// ---------------------------------------------------------------------------
// HIRE: engine-hosted (§1.8 step 6; the spec lives in menu_screen_specs.cpp,
// docs/menu-engine.md). The hooks below carry everything the deleted legacy
// loop did around the shared frame skeleton: the entry-time PREV/NEXT
// repositioning, the solo-hidden team cycler's nav closure, the reset tail,
// the new-game popup, and the stat/cost/description content pass — all
// verbatim, beside the file-local helpers they use.
// ---------------------------------------------------------------------------

// The legacy box geometry, verbatim (pure constants; shared by the
// prepare-buttons hook and the content pass).
struct HireMenuLayout
{
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
};

// Per-open screen state (the legacy loop's locals), owned by the wrapper.
struct HireEngineState
{
    Sint32 start_time = 0;
    unsigned char last_family = 0;
    std::string description;
    std::vector<std::string> desc;
    const char* family_name = "";
    // arg1 == 1: show the new-game intro popup after the first presented
    // frame (no production caller passes 1 today — team build passes -1 —
    // but the signature contract is preserved).
    bool pending_new_game_popup = false;
};

// Entry-time repositioning of PREV/NEXT around the portrait, verbatim from
// the deleted loop (runs once, before init_buttons; the accessor output —
// what the G2 pins transcribe — keeps the table shape).
void picker_hire_menu_engine_prepare_buttons(button* buttons, int num_buttons,
                                             void* /*screen_state*/)
{
    if (num_buttons < 2)
        return;
    const HireMenuLayout l;
    buttons[0].x = l.description_box.x + l.description_box.w/2 - buttons[0].sizex - 4 - 30;
    buttons[0].y = l.name_box.y + l.name_box.h + (l.description_box.y - (l.name_box.y + l.name_box.h))/2 - buttons[0].sizey/2;

    buttons[1].x = l.description_box.x + l.description_box.w/2 + 4 + 30;
    buttons[1].y = l.name_box.y + l.name_box.h + (l.description_box.y - (l.name_box.y + l.name_box.h))/2 - buttons[1].sizey/2;
}

// Nav closure over the solo-hidden team cycler: legacy left HIRE ME's
// right-link pointing at the hidden row — a no-op in handle_menu_nav (it
// refuses hidden targets) — so the explicit no-op (-1) is
// behavior-identical and satisfies the engine's §1.5 nav invariant.
void picker_hire_menu_engine_rewire(button* buttons, int num_buttons,
                                    int& /*highlighted_button*/)
{
    if (num_buttons > 3)
        buttons[3].nav.right = buttons[2].hidden ? -1 : 2;
}

// The legacy reset tail: after any consumed MENU_OK/MENU_REDRAW re-init,
// re-derive the hire-team label onto the fresh live surface.
void picker_hire_menu_engine_on_reset(void* /*screen_state*/)
{
    change_hire_teamnum(0);
}

// The legacy loop-bottom arg1 == 1 branch: one intro popup after the first
// presented frame (frame 1 draws at the END of iteration 1; the tick for
// iteration 2 is the first point after that present), then a re-init
// because the production popup swaps allbuttons_ under the screen.
bool picker_hire_menu_engine_frame_tick(void* screen_state, int frame)
{
    auto* const state = static_cast<HireEngineState*>(screen_state);
    if (state == nullptr)
        return true;
    if (state->pending_new_game_popup && frame >= 2)
    {
        state->pending_new_game_popup = false;
        popup_dialog("HIRE TROOPS", "Get your team started here\nby hiring some fresh recruits.");
        // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
        og::runtime::current_session->localbuttons_ =
            init_buttons(pks().hiremenu_buttons.data(),
                         static_cast<int>(pks().hiremenu_buttons.size()));
    }
    return true;
}

// The legacy per-frame content pass, verbatim (runs after draw_buttons):
// name box, portrait, description, cost, and the stat panel. Note the
// shipped one-frame quirk preserved on purpose: the name-box label is drawn
// from last frame's family_name, and the family-change re-derive happens
// mid-pass (between the portrait and the description lines).
void picker_hire_menu_engine_draw_content(void* screen_state)
{
    auto* const state = static_cast<HireEngineState*>(screen_state);
    if (state == nullptr)
        return;
    const HireMenuLayout l;

    if (!og::runtime::current_session->current_guy_)
        sync_current_guy_from_hire();

    // Name box
    og::runtime::current_session->myscreen_->draw_button(
        l.name_box.x, l.name_box.y, l.name_box.x + l.name_box.w - 1,
        l.name_box.y + l.name_box.h - 1, 1);
    og::runtime::current_session->myscreen_->draw_button_inverted(
        l.name_box_inner.x, l.name_box_inner.y, static_cast<Uint32>(l.name_box_inner.w),
        static_cast<Uint32>(l.name_box_inner.h));

    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    mytext.write_xy(l.name_box.x + l.name_box.w/2 - 3*static_cast<Sint32>(strlen(state->family_name)), l.name_box.y + 6, state->family_name, static_cast<unsigned char>(DARK_BLUE), 1);

    show_guy(query_timer()-state->start_time, 0, l.description_box.x + l.description_box.w/2, l.name_box.y + l.name_box.h + (l.description_box.y - (l.name_box.y + l.name_box.h))/2); // 0 means current_guy
    change_hire_teamnum(0);


    // Description box
    og::runtime::current_session->myscreen_->draw_button(
        l.description_box.x, l.description_box.y,
        l.description_box.x + l.description_box.w - 1,
        l.description_box.y + l.description_box.h - 1, 1);
    og::runtime::current_session->myscreen_->draw_button_inverted(
        l.description_box_inner.x, l.description_box_inner.y,
        static_cast<Uint32>(l.description_box_inner.w), static_cast<Uint32>(l.description_box_inner.h));

    if(og::runtime::current_session->current_guy_->family != state->last_family)
    {
        // Update description
        state->last_family = static_cast<unsigned char>(og::runtime::current_session->current_guy_->family);
        state->description = get_class_description(state->last_family);
        state->desc = og::core::wrap_text(state->description,
                                          l.description_box_content.w / 6,
                                          og::core::WrapMode::Paragraphs);

        state->family_name = get_family_string(state->last_family);
    }

    // Fit the flowed description inside the box (issue #152): the shipped
    // 10px pitch for up to 8 lines, an 8px pitch for longer descriptions
    // (pack authors are no longer hand-wrapping to the box). Never draw
    // past the inner bevel — a runaway pack description stays inside.
    {
        const int desc_lines = static_cast<int>(state->desc.size());
        const int pitch = (desc_lines <= 8) ? 10 : 8;
        const int max_desc_lines =
            (l.description_box_inner.y + l.description_box_inner.h -
             l.description_box_content.y - 6) / pitch + 1;
        int i = 0;
        for(auto& line : state->desc)
        {
            if (i >= max_desc_lines)
                break;
            mytext.write_xy(l.description_box_content.x, l.description_box_content.y + i*pitch, DARK_BLUE, "%s", line.c_str());
            i++;
        }
    }

    // Cost box
    og::runtime::current_session->myscreen_->draw_button(
        l.cost_box.x, l.cost_box.y, l.cost_box.x + l.cost_box.w - 1,
        l.cost_box.y + l.cost_box.h - 1, 1);
    og::runtime::current_session->myscreen_->draw_button_inverted(
        l.cost_box_inner.x, l.cost_box_inner.y, static_cast<Uint32>(l.cost_box_inner.w),
        static_cast<Uint32>(l.cost_box_inner.h));

    // current_team_num_ is derived from a save-loaded guy::teamnum (which
    // is unvalidated); clamp before indexing the MAX_PLAYERS-sized array.
    const int hire_cash_team =
        (og::runtime::current_session->current_team_num_ < 0 ||
         og::runtime::current_session->current_team_num_ >= MAX_PLAYERS)
            ? 0
            : static_cast<int>(og::runtime::current_session->current_team_num_);
    og::runtime::current_session->message_ = std::format(
        "CASH: {}",
        og::ui::format_wallet_amount(
            og::runtime::current_session->myscreen_->save_data, hire_cash_team));
    mytext.write_xy(l.cost_box_content.x, l.cost_box_content.y, og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
    const Uint32 current_cost = pks().hire_session ? pks().hire_session->current_cost() : 0;
    mytext.write_xy(l.cost_box_content.x, l.cost_box_content.y + 10, "COST: ", DARK_BLUE, 1);
    og::runtime::current_session->message_ = std::format("      {}", current_cost );
    if (!og::ui::can_afford(og::runtime::current_session->myscreen_->save_data,
                            hire_cash_team, current_cost))
        mytext.write_xy(l.cost_box_content.x + 10, l.cost_box_content.y + 10, og::runtime::current_session->message_.c_str(), STAT_CHANGED, 1);
    else
        mytext.write_xy(l.cost_box_content.x + 10, l.cost_box_content.y + 10, og::runtime::current_session->message_.c_str(), STAT_COLOR, 1);

    // Stat box
    og::runtime::current_session->myscreen_->draw_button(
        l.stat_box.x, l.stat_box.y, l.stat_box.x + l.stat_box.w - 1,
        l.stat_box.y + l.stat_box.h - 1, 1);
    mytext.write_xy(l.stat_box.x + 65, l.stat_box.y + 2, DARK_BLUE, "Train");
    og::runtime::current_session->myscreen_->draw_button_inverted(
        l.stat_box_inner.x, l.stat_box_inner.y, static_cast<Uint32>(l.stat_box_inner.w),
        static_cast<Uint32>(l.stat_box_inner.h));

    // Stat box content
    Sint32 linesdown = 0;
    int line_height = 10;

    unsigned char showcolor = STAT_COLOR; // normally STAT_COLOR or STAT_CHANGED

    struct { const char* label; short value; } hire_stats[] = {
        {"STR:",  og::runtime::current_session->current_guy_->strength},
        {"DEX:",  og::runtime::current_session->current_guy_->dexterity},
        {"CON:",  og::runtime::current_session->current_guy_->constitution},
        {"INT:",  og::runtime::current_session->current_guy_->intelligence},
        {"ARMOR:", og::runtime::current_session->current_guy_->armor},
    };
    for (int si = 0; si < 5; si++) {
        int y = l.stat_box_content.y + linesdown * line_height;
        og::runtime::current_session->message_ = std::format("{}", hire_stats[si].value);
        mytext.write_xy(l.stat_box_content.x, y, hire_stats[si].label,
                         static_cast<unsigned char>(STAT_COLOR), 1);
        mytext.write_xy(l.stat_box_content.x + STAT_NUM_OFFSET, y, og::runtime::current_session->message_.c_str(), showcolor, 1);
        if (si < 4) // cost rating for STR/DEX/CON/INT only
            mytext.write_xy(l.stat_box_content.x + STAT_NUM_OFFSET + 18, y,
                            get_training_cost_rating(state->last_family, si), showcolor, 1);
        if (si < 4)
            linesdown++;
    }

    // Separator bar
    UiRect r = {l.stat_box_content.x + 10, l.stat_box_content.y + (linesdown+1)*line_height - 2, l.stat_box_content.w - 20, 2};
    og::runtime::current_session->myscreen_->draw_button_inverted(
        r.x, r.y, static_cast<Uint32>(r.w), static_cast<Uint32>(r.h));

    int derived_offset = 3*STAT_NUM_OFFSET/4;
    auto ds = picker_compute_guy_derived_stats(*og::runtime::current_session->current_guy_);

    linesdown++;
    int hire_line = linesdown;
    draw_derived_stats_block(mytext, ds, l.stat_box_content.x, derived_offset, showcolor,
        [&](int lline) { return l.stat_box_content.y + lline*line_height + 4; }, hire_line);
}

// HIRE, engine-hosted (the legacy loop is gone). Entry setup, session
// lifetime, and the exit shape are the legacy code, verbatim: BACK folds to
// MENU_REDRAW (the spec's exit_value); a remote start propagates its
// MENU_EXIT directly (the slot-menu normalization — the parent breaks with
// StartGame selected instead of re-detecting the start one loop later).
Sint32 create_hire_menu(Sint32 arg1)
{
	og::runtime::current_session->myscreen_->clearbuffer();

    og::ui::HireSession hire_session(og::runtime::current_session->myscreen_->save_data, og::runtime::current_session->current_team_num_);
    pks().hire_session = &hire_session;
    sync_current_guy_from_hire();
    // Legacy entry side effects (team stamp on the recruit); the label write
    // inside is null-guarded and re-derived on the first frame regardless.
    change_hire_teamnum(0);

    HireEngineState state;
    state.start_time = query_timer();
    state.last_family = static_cast<unsigned char>(og::runtime::current_session->current_guy_->family);
    state.description = get_class_description(state.last_family);
    state.desc = og::core::wrap_text(state.description,
                                     HireMenuLayout{}.description_box_content.w / 6,
                                     og::core::WrapMode::Paragraphs);
    state.family_name = get_family_string(state.last_family);
    state.pending_new_game_popup = (arg1 == 1);

	grab_mouse();

    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::hire_menu_screen_spec(), &state);

	pks().hire_session = nullptr;
	og::runtime::current_session->myscreen_->clearbuffer();
	//myscreen->clearscreen();
	return retvalue;
}

// ---------------------------------------------------------------------------
// TRAIN: engine-hosted (§1.8 step 6; the spec lives in menu_screen_specs.cpp,
// docs/menu-engine.md). The +/- pixie faces are the spec's art_family
// bindings (re-applied by the runner after init_buttons and after every
// reset — legacy re-applied only on MENU_REDRAW re-inits, but under the
// engine EVERY consumed reset re-inits, so the re-apply must ride along);
// the reset tail is the on_reset hook; the stat/info panels and the live
// allbuttons_[18] label write are the content pass, verbatim.
// ---------------------------------------------------------------------------

// Per-open screen state (the legacy loop's locals), owned by the wrapper.
struct TrainEngineState
{
    Sint32 start_time = 0;
};

// DISABLE_MULTIPLAYER hides the team cycler (the spec's state_override);
// the legacy static links into it were refused by handle_menu_nav, so the
// explicit no-ops keep behavior identical under the §1.5 nav invariant.
// With multiplayer compiled in this is a no-op (row 18 is always visible).
void picker_train_menu_engine_rewire(button* buttons, int num_buttons,
                                     int& /*highlighted_button*/)
{
#ifdef DISABLE_MULTIPLAYER
    if (num_buttons > kTrainMenuChangeTeamIndex &&
        buttons[kTrainMenuChangeTeamIndex].hidden)
    {
        buttons[13].nav.right = -1;  // inc_level
        buttons[15].nav.down = -1;   // rename
        buttons[16].nav.down = -1;   // details
        if (num_buttons > kTrainMenuSellIndex)
            buttons[kTrainMenuSellIndex].nav.up = 16; // sell -> details
    }
#else
    (void)buttons;
    (void)num_buttons;
#endif
}

Sint32 picker_train_menu_engine_on_spec_row(int row, void* /*screen_state*/)
{
    if (row != kTrainMenuSellIndex || !pks().train_session ||
        pks().train_session->empty())
    {
        return MENU_OK;
    }
    if (!picker_lobby_save_slot_editable(
            pks().train_session->current_slot()))
    {
        return MENU_OK;
    }

    const guy& member = pks().train_session->original();
    const int sold_team = std::clamp(
        static_cast<int>(member.teamnum), 0,
        static_cast<int>(SCORE_TEAM_COUNT) - 1);
    const std::uint32_t payout =
        pks().train_session->current_sell_value();
    const std::string message = std::format(
        "SELL {} FOR {} GOLD?", member.name, payout);
    if (!yes_or_no_prompt("SELL CHARACTER?", message.c_str(), false))
        return MENU_REDRAW;

    const og::ui::TrainSession::SellResult result =
        pks().train_session->sell_current([] {
            return og::data::backup_company_now(
                og::data::active_company_slot());
        });
    if (result == og::ui::TrainSession::SellResult::CheckpointFailed)
    {
        popup_dialog("SELL CHARACTER", "BACKUP FAILED\nCHARACTER NOT SOLD");
        return MENU_REDRAW;
    }
    if (result != og::ui::TrainSession::SellResult::Sold)
        return MENU_OK;

    picker_base_camp_after_roster_mutation(sold_team);
    if (pks().train_session->empty())
    {
        og::runtime::current_session->current_guy_.reset();
        pks().old_guy = nullptr;
        og::runtime::current_session->editguy_ = -1;
        return MENU_EXIT;
    }

    sync_current_guy_from_train();
    return MENU_REDRAW;
}

// The legacy reset tail. On MENU_REDRAW resets: the DETAILS submenu can
// promote (family-change) the REAL team member in place — re-snapshot the
// working copy first or the stale copy hides the promotion on screen and a
// later ACCEPT statscopy()s the old family back (bug A9). The runner fires
// this on MENU_OK resets too, where both calls are idempotent no-ops (the
// stat callbacks already synced; no promotion is pending).
void picker_train_menu_engine_on_reset(void* /*screen_state*/)
{
    if (pks().train_session)
        pks().train_session->resync_if_promoted();
    sync_current_guy_from_train();
}

// The legacy per-frame content pass, verbatim (runs after draw_buttons):
// portrait, name box, stat box with change-coloring against the original,
// the info box (kills/accuracy/exp, derived stats, cash/cost), and the live
// allbuttons_[18] "Playing on Team N" write + vdisplay (G8: swept only at
// Layer F). current_cost re-derives from the session each frame — it only
// changes through clicks, which is when the legacy loop re-read it.
void picker_train_menu_engine_draw_content(void* screen_state)
{
    auto* const state = static_cast<TrainEngineState*>(screen_state);
    if (state == nullptr)
        return;
    float linesdown = 0.0f;
    unsigned char showcolor;

    UiRect stat_box = {38, 66, 82, 94};
    UiRect stat_box_inner = {stat_box.x + 4, stat_box.y + 4, stat_box.w - 8, stat_box.h - 8};
    UiRect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};

    UiRect info_box_inner = {176, 34, 304-176, 112+22-34};
    UiRect info_box_content = {info_box_inner.x + 4, info_box_inner.y + 4, info_box_inner.w - 8, info_box_inner.h - 8};

    const Uint32 current_cost =
        pks().train_session ? pks().train_session->current_cost() : 0;
    // The 2013 loop left this breadcrumb in its draw pass after moving the
    // live calculation elsewhere; TrainSession now performs that calculation:
    //current_cost = calculate_train_cost(here);

		show_guy(query_timer()-state->start_time, 1); // 1 means ourteam[editguy]


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
				r.x, r.y, static_cast<Uint32>(r.w), static_cast<Uint32>(r.h));
        
        linesdown += 0.4f;

        {
            auto ds = picker_compute_guy_derived_stats(*og::runtime::current_session->current_guy_);
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
				r2.x, r2.y, static_cast<Uint32>(r2.w), static_cast<Uint32>(r2.h));
        
        linesdown += 0.4f;
        // teamnum is loaded verbatim from the .gtl save file with no range
        // check; m_totalcash has only MAX_PLAYERS slots, so a malicious value
        // would index out of bounds. Clamp to a valid team before indexing.
        const int train_cash_team =
            (og::runtime::current_session->current_guy_->teamnum < 0 ||
             og::runtime::current_session->current_guy_->teamnum >= MAX_PLAYERS)
                ? 0
                : static_cast<int>(og::runtime::current_session->current_guy_->teamnum);
        og::runtime::current_session->message_ = std::format(
            "CASH: {}",
            og::ui::format_wallet_amount(
                og::runtime::current_session->myscreen_->save_data,
                train_cash_team));
	        mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);

        linesdown++;
	        mytext.write_xy(180, info_y(linesdown), "COST: ", DARK_BLUE, 1);
        og::runtime::current_session->message_ = std::format("      {}", current_cost );
        if (!og::ui::can_afford(og::runtime::current_session->myscreen_->save_data,
                                train_cash_team, current_cost))
	            mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(), STAT_CHANGED, 1);
	        else
	            mytext.write_xy(180, info_y(linesdown), og::runtime::current_session->message_.c_str(), STAT_COLOR, 1);

        // Update our team-number display ..
        og::runtime::current_session->message_ = std::format("Playing on Team {}", og::runtime::current_session->current_guy_->teamnum+1);
        og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->label = og::runtime::current_session->message_;
        og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->vdisplay();
}

// TRAIN, engine-hosted (the legacy loop is gone). The entry guards, session
// lifetime, and the exit fold are the legacy code, verbatim: nested
// submenus' MENU_REDRAWs are consumed by reset_buttons; every exit-bearing
// path carries MENU_EXIT and folds to MENU_REDRAW unless a start (VIEW TEAM
// GO or a remote host GO) was selected.
Sint32 create_train_menu(Sint32 arg1)
{
    // arg1 is part of the button.h signature but was never read by the
    // legacy loop (it only normalized it and moved on).
    (void)arg1;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

	// Make sure we have a local team member we can train.
	if (save.team_size < 1 || !save_has_trainable_team_member(save))
	{
        g_train_seed_slot = -1;
        show_need_team_to_train_popup();

		return MENU_OK;
	}

	og::runtime::current_session->myscreen_->clearbuffer();

    og::ui::TrainSession train_session(save);
    // §2.5 per-row TRAIN: a stashed base-camp seed slot opens the session
    // directly on that character (one-shot; empty/foreign slots fall back to
    // the legacy first-editable seat).
    if (g_train_seed_slot >= 0) {
        (void)train_session.seek_slot(g_train_seed_slot);
        g_train_seed_slot = -1;
    }
    pks().train_session = &train_session;
    sync_current_guy_from_train();
    if (pks().train_session->empty()) {
        pks().train_session = nullptr;
        show_need_team_to_train_popup();
        return MENU_OK;
    }

    TrainEngineState state;
    state.start_time = query_timer();

	grab_mouse();

    clear_keyboard();

    clear_key_press_event();

    const Sint32 retvalue =
        og::ui::run_menu_screen(og::ui::train_menu_screen_spec(), &state);

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

// The SAVE/LOAD slot menus are retired (§3.8): saving is automatic.

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
        og::runtime::current_session->current_guy_ = og::ui::create_recruit(guys[static_cast<std::size_t>(og::runtime::current_session->current_type_)], og::runtime::current_session->current_team_num_, og::runtime::current_session->myscreen_->save_data);
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
	// Zardus: FIX: added a check for null pointers
	if (og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex])
		og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->do_outline = 0;

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
		someguy = team_list[static_cast<std::size_t>(slot)].get();
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
	{
		someguy->name = *new_text;
		// §3.8: renaming a ROSTER member (arg != 0) is a base-camp mutation.
		// The arg==0 path names the hire screen's not-yet-hired recruit —
		// nothing on the roster changed, so nothing to save.
		if (arg)
			picker_base_camp_after_roster_mutation();
	}
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
	auto& hired = og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(slot)];
	std::string name = hired->name;
	if (prompt_for_string("NAME THIS CHARACTER", name))
		pks().hire_session->rename_hired(slot, name);
	grab_mouse();

	// Sync current_guy from the session's next recruit
	sync_current_guy_from_hire();
    // §3.8: a hire is a base-camp mutation — autosave + ready-clear ride the
    // shared tail (which also re-syncs the lobby roster).
    picker_base_camp_after_roster_mutation();

	return MENU_OK;
}

// Accept changes ..
Sint32 edit_guy([[maybe_unused]] Sint32 arg1)
{
	if (!pks().train_session || pks().train_session->empty())
		return -1;
	if (!picker_lobby_save_slot_editable(pks().train_session->current_slot()))
		return MENU_OK;

	// This is for cheating! Only CHEAT :)
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
    // §3.8: an accepted training is a base-camp mutation — autosave +
    // ready-clear ride the shared tail (which re-syncs the lobby roster).
    picker_base_camp_after_roster_mutation();
    sync_current_guy_from_train();

	// Color our team button normally
	og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->do_outline = 0;

	return MENU_OK;
}

Sint32 how_many(Sint32 whatfamily)    // how many guys of family X on the team?
{
	return static_cast<Sint32>(og::ui::count_family_members(whatfamily, og::runtime::current_session->myscreen_->save_data));
}

// do_save/do_load (the slot-menu actions) are RETIRED (§3.8): saving is
// automatic on every base-camp mutation + level win; loading goes through
// the §2.3 Company List. get_saved_name stays (header-only slot peek used
// by tests; §3.5 records the scanner consolidation as debt).

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
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset();
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

    // Make sure the launched match has a valid team. Networked picker saves
    // remain private, so a spectator/empty-local peer must consult the
    // authoritative lobby roster rather than only its own save_data.
    //
    // §4.3 rule 4 / [NET-R9]: the guard requires >= 1 DEPLOYED character
    // GLOBALLY — never a per-machine minimum (an all-spectator or 0-deploy
    // host machine starts fine when any other machine deploys; the server's
    // start_allowed is the authoritative backstop). Hired-but-benched rosters
    // get the deploy popup instead of the hire popup.
    bool has_any_member =
        og::runtime::current_session->myscreen_->save_data.team_size > 0;
    bool has_gameplay_roster = std::any_of(
        og::runtime::current_session->myscreen_->save_data.team_list.begin(),
        og::runtime::current_session->myscreen_->save_data.team_list.end(),
        [](const std::unique_ptr<guy>& member) {
            return member != nullptr && member->deployed;
        });
    if (picker_lobby_is_networked())
    {
        const std::vector<og::sim::LobbyPlayer> players =
            picker_lobby_players();
        has_any_member = std::any_of(
            players.begin(), players.end(),
            [](const og::sim::LobbyPlayer& player) {
                return !player.character_slots.empty();
            });
        has_gameplay_roster = std::any_of(
            players.begin(), players.end(),
            [](const og::sim::LobbyPlayer& player) {
                return std::any_of(
                    player.character_slots.begin(),
                    player.character_slots.end(),
                    [](const og::sim::LobbyCharacterSlot& slot) {
                        return slot.deployed;
                    });
            });
    }
    // §2.6 state 3 (networked host, machines not ready): popup the blocking
    // companies and send NOTHING — the server gate stays the authoritative
    // backstop. Rule 3 outranks rule 4 (the server's start_allowed order),
    // so the unready popup fires before the global-deploy popup below.
    if (picker_lobby_is_networked() && picker_lobby_host_controls_visible())
    {
        const std::vector<og::sim::LobbyPlayer> players =
            picker_lobby_players();
        const og::ui::BaseCampReadyCounts ready =
            og::ui::count_base_camp_ready_machines(players);
        if (ready.ready < ready.machines)
        {
            TRACE("basecamp", "go_gated ready=%d/%d", ready.ready,
                  ready.machines);
            popup_dialog("WAITING FOR:",
                         og::ui::format_go_blockers(players).c_str());
            return MENU_REDRAW;
        }
    }
    if (!has_gameplay_roster)
    {
        if (!has_any_member)
            popup_dialog("NEED A TEAM!", "Please hire a\nteam before\nstarting the level");
        else if (picker_lobby_is_networked())
            popup_dialog("NO ONE IS DEPLOYED", "Deploy at least\none character\nbefore starting");
        else
            popup_dialog("DEPLOY AT LEAST ONE", "Deploy at least\none character\nbefore starting");

        return MENU_REDRAW;
    }

    if (!picker_lobby_is_networked() &&
        og::runtime::current_session->myscreen_->save_data.numplayers > 0)
    {
        const SaveData& save =
            og::runtime::current_session->myscreen_->save_data;
        std::vector<og::sim::LobbyPlayer> lobby_players =
            picker_lobby_players();
        std::sort(lobby_players.begin(), lobby_players.end(),
                  [](const og::sim::LobbyPlayer& lhs,
                     const og::sim::LobbyPlayer& rhs) {
                      return lhs.player_index < rhs.player_index;
                  });
        std::vector<short> seat_teams;
        seat_teams.reserve(lobby_players.size());
        for (const og::sim::LobbyPlayer& player : lobby_players)
            seat_teams.push_back(player.team);
        if (seat_teams.size() != save.numplayers)
        {
            // A not-yet-initialized lobby has no explicit state. Legacy save
            // fields are only the seed for that narrow fallback; once the
            // local lobby exists, its per-seat choices are authoritative.
            seat_teams = og::ui::derive_local_gameplay_seat_teams(save);
        }
        if (!og::ui::local_seat_teams_have_controls(save, seat_teams))
        {
            popup_dialog("DEPLOY FOR EVERY PLAYER",
                         "Each player needs\na deployed hero on\ntheir playing team");
            return MENU_REDRAW;
        }
    }

    // Tier-B progression hook (tower-triple §5.9): the mounted mode
    // provisions its content BEFORE the pre-launch save0 writes below, so a
    // freshly drawn tower run seed rides that write (D8: floor prefetch at
    // GO time, the safest generation window). Classic returns true
    // untouched. A veto (the tower in a networked session — the backstop
    // behind the shelf filter) aborts the GO with the shared reason string.
    if (!og::mode::current_progression().prepare_launch(
            og::runtime::current_session->myscreen_->save_data,
            picker_lobby_is_networked()))
    {
        // Two veto shapes share the return: the networked-session gate and
        // a local provisioning failure (the mode could not write its
        // generated content — e.g. a tower floor). Local sessions can only
        // hit the latter, so key the message on the session, not the mode.
        if (picker_lobby_is_networked())
            popup_dialog("TOWER CLIMB", "TOWER CLIMB is local-only");
        else
            popup_dialog("TOWER CLIMB",
                         "Could not prepare\nthe tower's floors.\nGO aborted.");
        return MENU_REDRAW;
    }

#ifdef __EMSCRIPTEN__
    picker_prepare_async_team_build_start_request();
    og::runtime::current_session->myscreen_->save_data.save(
        og::data::active_company_slot());
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
    {
        // §2.6: a server denial that beat the local pre-check (a joiner
        // flipping unready mid-flight / the async echo race) still renders
        // its reason — best-effort from the cached [NET-R4] echo (a repeated
        // identical denial does not re-broadcast, and an accepted retry can
        // blip a stale reason; both disclosed WP5 contracts).
        if (picker_lobby_is_networked())
        {
            switch (picker_lobby_last_start_denial())
            {
            case og::sim::StartDenialReason::MachinesNotReady:
            {
                std::string blockers =
                    og::ui::format_go_blockers(picker_lobby_players());
                if (blockers.empty())
                    blockers = "Waiting for other\nmachines to ready";
                popup_dialog("WAITING FOR:", blockers.c_str());
                break;
            }
            case og::sim::StartDenialReason::NoDeployedCharacters:
                popup_dialog("NO ONE IS DEPLOYED",
                             "Deploy at least\none character\nbefore starting");
                break;
            default:
                break;
            }
        }
        return MENU_REDRAW;
    }

    g_start_game_requested = false;

    // Native build: use blocking loop
    bool retry_level = false;
    do
    {
        og::runtime::current_session->myscreen_->save_data.save(
            og::data::active_company_slot());
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

        // Latch the retry exits (pause-menu RESTART's
        // local_transport_shadow_restart_level stamp, the defeat results
        // screen's RETRY) HERE, right after glad_main returns: the loop-tail
        // reset(1) below runs level_runtime_data_.clear() -> GameWorld::clear(),
        // which wipes world().retry before the while() would read it — reading
        // the flag in the loop condition sent RESTART MISSION back to Base
        // Camp exactly like QUIT.
        retry_level = og::runtime::current_session->myscreen_->world().retry;
        Log("Returned from glad_main, retry={}\n", retry_level);

        //*******************************
        // Fade out from ACTION loop
        //*******************************
        // Zardus: PORT: new fade code
        og::runtime::current_session->myscreen_->set_active_canvas(
            og::runtime::current_session->myscreen_->last_presented_canvas());
        og::runtime::current_session->myscreen_->fadeblack(0);
        // #200: this IS the fade back to the menu. Base Camp's entry must not
        // play a second one over the stale pause-menu image.
        og::ui::note_menu_faded_to_black();

        // Zardus: PORT: doesn't seem to be neccessary
        og::runtime::current_session->myscreen_->clearbuffer();
        og::runtime::current_session->myscreen_->set_active_canvas(CanvasTarget::UI);
        // The fade above only blackened whichever canvas was last presented;
        // clear the other one too so no path re-enters a menu over stale ink.
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

        const std::string slot_file = og::data::active_company_slot() + ".gtl";
        auto loadgame = og::io::og_open_read("save/", slot_file.c_str());
        if (loadgame)
        {
            // #207: SaveData::load clears the transient replay arm; carry a
            // live one across the reload when the disk cursor still points
            // at the armed level — a RETRY of a lost replay must relaunch
            // ARMED (the retried level loads restored), and the loss/quit
            // restore below still needs the origin. A won replay already
            // cleared the arm in the fold, so nothing carries.
            const short armed_level = og::runtime::current_session
                ->myscreen_->save_data.replay_level;
            const short armed_origin = og::runtime::current_session
                ->myscreen_->save_data.replay_origin;
            og::runtime::current_session->myscreen_->save_data.load(
                og::data::active_company_slot());
            if (armed_level != 0 &&
                og::runtime::current_session->myscreen_->save_data.scen_num ==
                    armed_level)
            {
                og::runtime::current_session->myscreen_->save_data
                    .replay_level = armed_level;
                og::runtime::current_session->myscreen_->save_data
                    .replay_origin = armed_origin;
            }
        }
    }
    while(retry_level);

    // #207 design point 5: an armed replay that ended without a win (loss,
    // quit-to-menu) restores the campaign cursor here, on picker re-entry;
    // the next disk write persists it.
    (void)og::ui::replay_reentry_restore(
        og::runtime::current_session->myscreen_->save_data);

    picker_reinitialize_lobby_after_game();

	return button_action_id(ButtonAction::CreateTeamMenu);
#endif
}

#ifdef TESTING
#include "../../../tests/coverage_internal/picker_team_build_internal.inc"
#endif

void statscopy(guy *dest, guy *source)
{
	og::ui::statscopy(dest, source);
}
