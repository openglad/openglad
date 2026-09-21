/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The SETUP wizard in real SDL (docs/match-setup-design.md §2): the door
// twin on the Base Camp strip, the five steps, the tab jumps, the
// forward-only cycler wheels, GO's gated face, and the #305 census a
// fresh ball arena deals with no knob touched.
//
// Every flow here drives picker_main from an injector thread through the
// ACKNOWLEDGED ladders (tests/test_click_ladder.h) against named edges,
// and every give-up — and every happy path — leaves through the shared
// escape tail (tests/test_escape_tail.h) with the WIZARD's own door table
// bound: inside the nested wizard the Base Camp's `go` is not live, and
// the default table would spin against the group's ctest cap instead of
// pressing the one door that is up. This file is on tier 2 of
// scripts/check_injector_settles.sh from birth: no flat settle, ever.

#include <gtest/gtest.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/match_setup_session.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_camp_save_fixture.h"
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_launched_census.h"
#include "test_interact.h"
#include "test_menu_highlight.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

void picker_main(Sint32 argc, char** argv);
extern bool g_start_game_requested;
// picker_team_build.cpp's answer to "did the remote start pick START GAME":
// the same declaration menu_screen_specs.cpp makes for the fold.
bool team_build_start_selected();
Sint32 create_team_menu(Sint32 arg1);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>

namespace
{

inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

// The wizard's door table, in PRECEDENCE order: its own BACK first (the
// tab strip's presence is the oracle that it is up), then the nested
// LINEUP and VIEW LEVEL screens' BACKs, then Base Camp's, then the
// picker's own way forward out of the main menu.
constexpr EscapeDoor kSetupEscapeDoors[] = {
    {"setup_back", "setup_back"},
    {"lineup_unite", "back"},
    {"back", "back"},
    {"go", "back"},
    {"continue_game", "continue_game"},
};

// The camp flows' one starting company lives in
// tests/test_camp_save_fixture.h (write_save0_with_two_soldiers): two
// deployed soldiers on team 0, every match knob at a defined resting state
// and no arena deal memo. A local twin of it drifted the day one copy
// gained a field, so this file spells the company name and nothing else.
void write_versus_save(const std::string& campaign, short scen_num)
{
    write_save0_with_two_soldiers(campaign, scen_num, {}, "IRON KETTLE");
}

void restore_gladiator_mount()
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SaveData& save = test_screen()->save_data;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
}

const char* uxshots_dir()
{
    return std::getenv("UXSHOTS_DIR");
}

// One flow's observations, recorded on the injector thread and asserted on
// the main thread (a gtest failure raised from a thread that then dies
// mid-flow takes its message with it).
struct SetupFlowState
{
    std::atomic<bool> test_finished{false};
    bool camp_seen = false;
    bool door_seen = false;
    bool wizard_opened = false;
    bool finished = false;
    // Per-flow readings.
    std::string game_row;
    std::string arena_row;
    std::string teams_fill_row;
    std::string rules_row_0;
    std::string rules_row_1;
    std::string go_row;
    std::string team_line_census;
    bool reached_arena = false;
    bool reached_teams = false;
    bool reached_rules = false;
    bool reached_match = false;
    bool walked_back = false;
    bool arena_on_current_window = false;
    std::string arena_row_1;
    bool rules_row_2_absent = false;
    bool closed_from_every_step = true;
    // The Base Camp reading enter_wizard takes on its way past: the one
    // door's face, and which strip doors are up behind it.
    std::string door_label;
    bool difficulty_visible = false;
    bool strip_setup_visible = true;
    int captures = 0;
    // The keyboard landing per step (P1): where the highlight IS when the
    // step is entered, read off the runner's own mirror.
    Landing game_landing, arena_landing, teams_landing, rules_landing,
        match_landing;
    std::string match_go_label, match_view_label;
    bool nav_woke = true;
};

// The shared opening: main menu -> Base Camp -> the docket's SETUP row.
// Returns 0 on success, or the leg the escape tail gave up on.
int enter_wizard(SetupFlowState* state, int leg_base,
                 const std::function<int(int, const char*)>& escape)
{
    state->camp_seen = wait_for_interactable("continue_game", 10000);
    if (!state->camp_seen)
        return escape(leg_base, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");

    state->door_seen = wait_for_interactable("zone_action_0", 15000);
    if (!state->door_seen)
        return escape(leg_base + 1,
                      "the versus docket never showed its SETUP row");
    state->door_label = interactable_label("zone_action_0");
    state->strip_setup_visible = has_interactable("setup");
    state->difficulty_visible = has_interactable("difficulty");

    state->wizard_opened = open_setup_step(0, "GAME", 15000);
    if (!state->wizard_opened)
        return escape(leg_base + 2, "the SETUP row never opened the wizard");
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The ONE door, and what is still on the strip behind it.
//    R2-5 collapsed the versus docket to a single row that STATES the
//    match — "SETUP - <TITLE>  >" — and that row is the wizard's only door
//    on this client. R2-3 put DIFFICULTY back on the strip on every
//    campaign, so there is no SETUP button up there any more.

namespace
{
int door_twin_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<SetupFlowState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };
    const int failed = enter_wizard(state, 1, escape);
    if (failed != 0)
        return failed;

    (void)wait_for_menu_frames(2);
    state->game_landing = read_landing();
    state->nav_woke = wake_nav_highlight(KEY_LEFT);
    capture_presented_frame("setup_step_game", uxshots_dir());
    ++state->captures;
    state->game_row = interactable_label("setup_row_0");

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(4, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, docket_row_is_the_one_door_and_difficulty_stays)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    SetupFlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(door_twin_injector, "setup_door", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    EXPECT_TRUE(state.difficulty_visible)
        << "R2-3: DIFFICULTY is the strip's second door on EVERY campaign";
    EXPECT_FALSE(state.strip_setup_visible)
        << "and there is no SETUP twin up there: R2-5 gave the wizard ONE "
           "door, the docket row inside the panel";
    EXPECT_TRUE(state.door_label.starts_with("SETUP - "))
        << "the door STATES the match it opens: '" << state.door_label
        << "'";
    EXPECT_TRUE(state.wizard_opened);
    EXPECT_NE(std::string::npos, state.game_row.find("TEAM DEATHMATCH"))
        << "the GAME step is the campaign's own book root: '"
        << state.game_row << "'";
    // P1: entering GAME puts the keyboard on the CURRENT game's row — the
    // one match_knobs.arena_page names — and not on row 0, and never on
    // the inert current tab.
    EXPECT_TRUE(state.game_landing.id.starts_with("setup_row_"))
        << "the landing is a row: '" << state.game_landing.id << "'";
    EXPECT_NE(std::string::npos, state.game_landing.label.find("SOCCER"))
        << "the save's cursor is on 820, so GAME lands on SOCCER: '"
        << state.game_landing.label << "' (" << state.game_landing.id << ")";
    EXPECT_TRUE(state.nav_woke)
        << "the capture wakes the highlight ring with one no-op nav step";
    verify_captured_frames("setup_door", 1);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 2. The walk: GAME -> ARENA -> TEAMS -> RULES -> MATCH through the tab
//    strip, capturing each step. The ARENA tab lands on the page that
//    lists the CURSOR's arena (D27), not on the root and not on the last
//    page browsed.

namespace
{
int step_walk_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<SetupFlowState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };
    const int failed = enter_wizard(state, 1, escape);
    if (failed != 0)
        return failed;

    // ARENA: the tab descends into the page match_knobs named, opened on
    // the window that holds [CURRENT].
    state->reached_arena = open_setup_step(1, "ARENA", 15000);
    if (!state->reached_arena)
        return escape(4, "the ARENA tab never came up");
    (void)wait_for_menu_frames(2);
    state->arena_row = interactable_label("setup_row_0");
    state->arena_row_1 = interactable_label("setup_row_1");
    state->arena_on_current_window =
        state->arena_row.find("[CURRENT]") != std::string::npos;
    state->arena_landing = read_landing();
    state->nav_woke = state->nav_woke && wake_nav_highlight(KEY_LEFT);
    capture_presented_frame("setup_step_arena", uxshots_dir());
    ++state->captures;

    // TEAMS: the swatched team lines, the campaign's own line, the FILL
    // wheel, and the LINEUP door.
    state->reached_teams = open_setup_step(2, "TEAMS", 15000);
    if (!state->reached_teams)
        return escape(5, "the TEAMS tab never came up");
    (void)wait_for_menu_frames(2);
    state->teams_fill_row = interactable_label("setup_row_0");
    state->teams_landing = read_landing();
    state->nav_woke = state->nav_woke && wake_nav_highlight(KEY_LEFT);
    capture_presented_frame("setup_step_teams", uxshots_dir());
    ++state->captures;

    // RULES: the TWO match knobs (R2-3). The other seven rules went home
    // to the Base Camp DIFFICULTY screen, and the step's one line points
    // there.
    state->reached_rules = open_setup_step(3, "RULES", 15000);
    if (!state->reached_rules)
        return escape(6, "the RULES tab never came up");
    (void)wait_for_menu_frames(2);
    state->rules_row_0 = interactable_label("setup_row_0");
    state->rules_row_1 = interactable_label("setup_row_1");
    state->rules_row_2_absent = !has_interactable("setup_row_2");
    state->rules_landing = read_landing();
    state->nav_woke = state->nav_woke && wake_nav_highlight(KEY_LEFT);
    capture_presented_frame("setup_step_rules", uxshots_dir());
    ++state->captures;

    // MATCH: the staged census, the rules recap, VIEW LEVEL over GO.
    state->reached_match = open_setup_step(4, "MATCH", 15000);
    if (!state->reached_match)
        return escape(7, "the MATCH tab never came up");
    (void)wait_for_menu_frames(2);
    state->go_row = interactable_label("setup_row_1");
    state->match_landing = read_landing();
    state->match_go_label = state->go_row;
    state->match_view_label = interactable_label("setup_row_0");
    state->nav_woke = state->nav_woke && wake_nav_highlight(KEY_LEFT);
    capture_presented_frame("setup_step_match", uxshots_dir());
    ++state->captures;

    // ...and back down the strip by TAB, which is the jump. (The footer's
    // own PREV/NEXT walk is warm_next_prev_through_every_step's — this
    // one used to claim PREV while clicking the RULES tab.)
    state->walked_back =
        click_until_label("setup_tab_3", "[RULES]", 3, 10000, "step", "setup");

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(8, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, tab_walk_through_every_step)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE MUDBOWL (821) is PLAYED on this company. R2-4: the wizard shows
    // no sign of it — the arena rows carry [CURRENT] and nothing else.
    write_save0_with_two_soldiers("modes", 820, {821}, "IRON KETTLE");

    SetupFlowState state;
    SDL_Thread* thread =
        SDL_CreateThread(step_walk_injector, "setup_walk", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    EXPECT_TRUE(state.reached_arena);
    EXPECT_TRUE(state.arena_on_current_window)
        << "D27: the ARENA tab opens the page that lists the cursor's "
           "arena, on the window holding [CURRENT] — row 0 read '"
        << state.arena_row << "'";
    EXPECT_NE(std::string::npos, state.arena_row.find("THE PITCH"))
        << "the cursor is on 820: '" << state.arena_row << "'";
    EXPECT_NE(std::string::npos, state.arena_row.find("[CURRENT]"))
        << "[CURRENT] is not progress vocabulary and stays: '"
        << state.arena_row << "'";
    EXPECT_NE(std::string::npos, state.arena_row_1.find("THE MUDBOWL"))
        << "row 1 is 821, the arena this company has played: '"
        << state.arena_row_1 << "'";
    EXPECT_EQ(std::string::npos, state.arena_row_1.find("[CLEARED]"))
        << "R2-4: a played arena wears no mark — '" << state.arena_row_1
        << "'";
    EXPECT_TRUE(state.reached_teams);
    EXPECT_NE(std::string::npos, state.teams_fill_row.find("FILL:"))
        << "the TEAMS step leads with the FILL wheel on a two-side arena "
           "(SIDES is hidden — one legal value is not a wheel): '"
        << state.teams_fill_row << "'";
    EXPECT_TRUE(state.reached_rules);
    EXPECT_NE(std::string::npos, state.rules_row_0.find("SCORE:"))
        << "'" << state.rules_row_0 << "'";
    EXPECT_NE(std::string::npos, state.rules_row_1.find("TIME LIMIT:"))
        << "the clock's one home is the RULES step (D7): '"
        << state.rules_row_1 << "'";
    EXPECT_TRUE(state.rules_row_2_absent)
        << "R2-3: RULES is SCORE and TIME LIMIT and nothing else — the "
           "seven rules it had swallowed are back on the Base Camp "
           "DIFFICULTY screen, which the step's one line points at";
    EXPECT_TRUE(state.reached_match);
    EXPECT_NE(std::string::npos, state.go_row.find("GO"))
        << "the MATCH step ends on GO: '" << state.go_row << "'";
    EXPECT_TRUE(state.walked_back)
        << "the tabs jump back down the strip";

    // P1, the whole walk: every step entered leaves the keyboard on a LIVE
    // row of that step — never on the tab the click landed on, which the
    // engine dims and eats (D36), and never nowhere.
    EXPECT_TRUE(state.arena_landing.id.starts_with("setup_row_"))
        << "ARENA landing: '" << state.arena_landing.id << "'";
    EXPECT_NE(std::string::npos, state.arena_landing.label.find("[CURRENT]"))
        << "ARENA lands on the arena the cursor is on: '"
        << state.arena_landing.label << "'";
    EXPECT_EQ("setup_row_0", state.teams_landing.id)
        << "TEAMS lands on its first row (the FILL wheel here): '"
        << state.teams_landing.label << "'";
    EXPECT_EQ("setup_row_0", state.rules_landing.id)
        << "RULES lands on its first cycler: '" << state.rules_landing.label
        << "'";
    if (state.match_go_label == "GO") {
        EXPECT_EQ("setup_row_1", state.match_landing.id)
            << "a LIVE GO is what the MATCH step is for";
    } else {
        EXPECT_EQ("setup_row_0", state.match_landing.id)
            << "a gated GO ('" << state.match_go_label
            << "') hands MATCH to VIEW LEVEL — the row that still does "
               "something: '" << state.match_view_label << "'";
    }
    EXPECT_TRUE(state.nav_woke)
        << "every capture in this walk wakes the ring with a no-op nav step";
    verify_captured_frames("setup_walk", 4);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 3. The wheels turn FORWARD ONLY, like every other cycler in the picker
//    (the DIFFICULTY rows, the LINEUP wheels). An overshoot costs a lap —
//    the longest wheel on this screen is five stops.
//
//    The right button holds no secret reverse either. The wizard does not
//    set `right_click_enabled`, and on such a screen the engine's rule is
//    the legacy `if (leftmouse(buttons))` one it has always had: ANY
//    nonzero click activates leftclick. So a right-click on a cycler row
//    does exactly what a left-click does — it steps the wheel FORWARD —
//    which is what every other MenuSpecRow screen already does with it.

namespace
{
struct ForwardState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool stepped_forward = false;
    std::string face_after_right_click;
    bool right_click_stepped_forward = false;
    bool lapped_home = false;
    bool finished = false;
};

int forward_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<ForwardState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(3, "RULES", 15000);
    if (!state->opened)
        return escape(3, "the RULES step never came up");
    (void)wait_for_interactable_label_matching(
        "setup_row_0",
        [](const std::string& label) {
            return label.find("SCORE: MAP") != std::string::npos;
        },
        10000);

    // Forward one stop: MAP -> 1.
    state->stepped_forward = click_until_label_containing(
        "setup_row_0", "SCORE: 1", 3, 10000, "turned", "setup");

    // A RIGHT-click on the same row: one press, and the oracle is the
    // face it writes. It steps the wheel FORWARD, 1 -> 3, exactly as the
    // left click did — never back.
    (void)interact_right("setup_row_0");
    state->right_click_stepped_forward = wait_for_interactable_label_matching(
        "setup_row_0",
        [](const std::string& label) {
            return label.find("SCORE: 3") != std::string::npos;
        },
        10000);
    (void)wait_for_menu_frames(2);
    state->face_after_right_click = interactable_label("setup_row_0");

    // And the overshoot is paid forward: SCORE is {MAP,1,3,5,10}, so the
    // rest of the lap is three more presses. Each step is proven by the
    // face the row wrote, which is exactly what a lap has to cost to be
    // worth stating.
    if (state->right_click_stepped_forward) {
        static const char* const kFaces[] = {"SCORE: 5", "SCORE: 10",
                                             "SCORE: MAP"};
        state->lapped_home = true;
        for (const char* face : kFaces) {
            state->lapped_home = state->lapped_home &&
                click_until_label_containing("setup_row_0", face, 3, 10000,
                                             "turned", "setup");
        }
    }

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(4, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, a_rules_cycler_laps_forward_and_ignores_a_right_click)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 300);

    ForwardState state;
    SDL_Thread* thread =
        SDL_CreateThread(forward_injector, "setup_forward", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_TRUE(state.stepped_forward)
        << "one click steps the SCORE wheel MAP -> 1";
    EXPECT_TRUE(state.right_click_stepped_forward)
        << "a right-click on a cycler row is just a click: the wizard sets "
           "no right_click_enabled, so the engine's legacy rule applies and "
           "it steps the wheel FORWARD, 1 -> 3. A reverse hidden on one "
           "mouse button is a rule no other picker screen teaches; the "
           "face read '" << state.face_after_right_click << "'";
    EXPECT_TRUE(state.lapped_home)
        << "and an overshoot is paid forward: the rest of the lap walks "
           "the five-stop SCORE wheel home to MAP";
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 4. #305 through the SDL wizard: a FRESH solo save on a ball arena deals
//    STRONG, and STRONG buys a BODY — so the TEAMS line censuses two bots
//    opposite the player with no knob touched, and the MATCH step's own
//    report line says the same thing in VIEW LEVEL's words.

namespace
{
struct BodiesState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool reached_teams = false;
    bool reached_match = false;
    std::string fill_row;
    bool finished = false;
    int captures = 0;
};

int bodies_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<BodiesState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(2, "TEAMS", 15000);
    if (!state->opened)
        return escape(3, "the TEAMS step never came up");
    (void)wait_for_menu_frames(2);
    state->reached_teams = true;
    state->fill_row = interactable_label("setup_row_0");
    capture_presented_frame("setup_step_teams_ball", uxshots_dir());
    ++state->captures;

    state->reached_match = open_setup_step(4, "MATCH", 15000);
    if (state->reached_match) {
        (void)wait_for_menu_frames(2);
        capture_presented_frame("setup_match_ball", uxshots_dir());
        ++state->captures;
    }

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(4, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, fresh_ball_arena_deals_strong_on_the_teams_step)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // THE PITCH (scen 820) is a soccer arena: mode_shape says it buys
    // bodies, so the campaign's match_knobs deals STRONG (#305, D13).
    write_versus_save("modes", 820);

    BodiesState state;
    SDL_Thread* thread =
        SDL_CreateThread(bodies_injector, "setup_bodies", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.reached_teams);
    EXPECT_NE(std::string::npos, state.fill_row.find("FILL: STRONG"))
        << "#305: a fresh ball arena is DEALT STRONG, so the word is on "
           "the row the moment the step opens, with no knob touched: '"
        << state.fill_row << "'";
    EXPECT_TRUE(state.reached_match);
    verify_captured_frames("setup_bodies", 2);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 5. GO says WHY it will not launch (D33). The MATCH step's GO is gated by
//    the strip GO's OWN predicate — one home for the M4 refusal — and the
//    face IS the reason, because a GO that ejects the player onto Base Camp
//    to read a modal the wizard could have shown breaks the step's one
//    promise.

namespace
{
struct GoGateState
{
    std::atomic<bool> test_finished{false};
    bool reached_match = false;
    std::string sides_row;
    std::string go_face;
    bool finished = false;
    int captures = 0;
};

int go_gate_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<GoGateState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    // FOUR authored sides: the SIDES wheel is a wheel here (D30), and the
    // step prints one swatched line per authored team.
    if (!open_setup_step(2, "TEAMS", 15000))
        return escape(3, "the TEAMS step never came up");
    (void)wait_for_menu_frames(2);
    state->sides_row = interactable_label("setup_row_0");
    capture_presented_frame("setup_step_teams_four_sides", uxshots_dir());
    ++state->captures;

    state->reached_match = open_setup_step(4, "MATCH", 15000);
    if (!state->reached_match)
        return escape(4, "the MATCH step never came up");
    (void)wait_for_menu_frames(2);
    state->go_face = interactable_label("setup_row_1");
    capture_presented_frame("setup_go_gated", uxshots_dir());
    ++state->captures;

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(5, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, go_says_why_it_will_not_launch)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 822);
    // TWO seats, and only the first one's team has a deployed fighter: the
    // second player has nobody to play. That is exactly the case the strip
    // GO pops kDeployForEveryPlayerTitle for, and the wizard's GO wears it.
    {
        SaveData& save = test_screen()->save_data;
        save.numplayers = 2;
        save.team_list[1]->teamnum = 1;
        save.team_list[1]->deployed = false;
        ASSERT_TRUE(save.save("save0"));
    }

    GoGateState state;
    SDL_Thread* thread =
        SDL_CreateThread(go_gate_injector, "setup_go_gate", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.reached_match);
    EXPECT_NE(std::string::npos, state.sides_row.find("SIDES:"))
        << "D30: a FOUR-side arena has a real sides wheel, and it leads "
           "the step: '" << state.sides_row << "'";
    EXPECT_EQ(std::string(og::ui::kSetupGoDeployFace), state.go_face)
        << "the GO row's FACE is the refusal, not a modal one screen away: '"
        << state.go_face << "'";
    verify_captured_frames("setup_go_gate", 2);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 6. The whole story, cold, on the arena the player picks: SETUP -> ARENA ->
//    a level row -> TEAMS -> RULES -> MATCH -> GO, and then the world the
//    launch ADOPTED, censused at its opening tick. This is the wizard's one
//    promise end to end — everything the five steps said is what fields —
//    and it is the only flow that executes the D20 deferred GO: the wizard
//    exits, Base Camp presses its OWN strip GO on the ordinal that owns it,
//    and the intercept selects the start.
//
//    It starts on FOURSQUARE (822, four authored sides) and sets THE PITCH
//    (820, two), because the step that composes next must describe the NEW
//    arena on its very first frame: a TEAMS step still holding the previous
//    arena's authored mask leads with a SIDES wheel over four team lines on
//    a two-side pitch, and heals only when the next restage happens to
//    bump the stage generation a quarter-second later.

namespace
{
struct ColdStoryState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool level_set = false;
    bool landed_on_teams = false;
    std::string teams_row_0;
    bool time_limit_set = false;
    bool reached_match = false;
    std::string go_face;
    bool launched = false;
    LaunchedCensus adopted;
    bool finished = false;
};

int cold_story_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<ColdStoryState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(1, "ARENA", 15000);
    if (!state->opened)
        return escape(3, "the ARENA step never came up");
    // Row 0 of the soccer page is THE PITCH; the cursor is on FOURSQUARE,
    // so this is a real set and the step ADVANCES (a refusal never does).
    state->level_set = click_until_edge(
        "setup_row_0",
        [](int wait_ms) {
            return wait_for_interactable_label_matching(
                "setup_tab_2",
                [](const std::string& label) {
                    return label == "[TEAMS]";
                },
                wait_ms);
        },
        "level_set", 3, 15000, "zone");
    if (!state->level_set)
        return escape(4, "the level row never set the arena");

    // The FIRST completed frame of the step the set advanced into. No
    // settle beyond it: the point is that the answer is right immediately,
    // not a restage later.
    (void)wait_for_menu_frames(1);
    state->landed_on_teams = true;
    state->teams_row_0 = interactable_label("setup_row_0");

    // RULES: the clock's one home. 5 MIN is the first stop off MAP.
    if (!open_setup_step(3, "RULES", 15000))
        return escape(5, "the RULES step never came up");
    state->time_limit_set = click_until_label_containing(
        "setup_row_1", "TIME LIMIT: 5 MIN", 3, 10000, "turned", "setup");

    if (!open_setup_step(4, "MATCH", 15000))
        return escape(6, "the MATCH step never came up");
    (void)wait_for_menu_frames(2);
    state->reached_match = true;
    state->go_face = interactable_label("setup_row_1");

    // D20: the wizard's GO. The census rides the launched world's first
    // frame and then quits the mission, which drops back onto Base Camp.
    state->launched = go_and_census(state->adopted, "setup_row_1");
    if (!wait_for_interactable("go", 20000))
        return escape(7, "the camp never came back after the mission");

    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, cold_two_v_two_soccer_story)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // FOURSQUARE: four authored sides, so TEAMS leads with a SIDES wheel
    // here and must NOT once the pitch is set.
    write_versus_save("modes", 822);

    ColdStoryState state;
    SDL_Thread* thread =
        SDL_CreateThread(cold_story_injector, "setup_cold_story", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    ASSERT_TRUE(state.level_set)
        << "a level row that CHANGES the cursor advances the step (§2.3)";
    EXPECT_TRUE(trace_contains("zone", "level_set 820"))
        << "the shared answer switch set the cursor";
    EXPECT_TRUE(trace_contains("setup", "toast Level set to"))
        << "and the wizard says so on line B, in the camp's own voice "
           "(campaign_level_set_message, the same sentence the zone "
           "submenu and the docket use)";
    ASSERT_TRUE(state.landed_on_teams);
    EXPECT_NE(std::string::npos, state.teams_row_0.find("FILL:"))
        << "the step the set advanced into describes the NEW arena on its "
           "first frame: THE PITCH has two authored sides, so there is no "
           "SIDES wheel — a stale authored mask reads '"
        << state.teams_row_0 << "'";
    EXPECT_EQ(std::string::npos, state.teams_row_0.find("SIDES:"))
        << "'" << state.teams_row_0 << "'";
    EXPECT_TRUE(state.time_limit_set) << "the RULES clock steps MAP -> 5 MIN";
    ASSERT_TRUE(state.reached_match);
    EXPECT_EQ("GO", state.go_face)
        << "every player is deployed, so GO is GO: '" << state.go_face << "'";

    ASSERT_TRUE(state.launched)
        << "the wizard's GO must launch a world that outlives its first "
           "frame (D20: Base Camp's own strip GO is what it presses)";
    const LaunchedTeamCensus& red = state.adopted.teams[0];
    const LaunchedTeamCensus& green = state.adopted.teams[1];
    EXPECT_EQ(2, red.guys) << "the company's two soldiers stand on RED";
    EXPECT_EQ(0, red.bots) << "STRONG on the company's own band fields none";
    EXPECT_GT(green.bots, red.guys)
        << "#305: a ball arena deals STRONG, and STRONG buys a BODY above "
           "FAIR — the opponent outnumbers the company by one";
    EXPECT_EQ(green.bots, green.livings)
        << "nobody's company stands on GREEN";
    EXPECT_EQ(0, state.adopted.teams[2].livings)
        << "a two-side pitch fields nothing on BLUE";
    EXPECT_EQ(0, state.adopted.teams[3].livings);

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    EXPECT_EQ(820, static_cast<int>(save.scen_num))
        << "the arena the wizard set is the one the launch adopted";
    EXPECT_EQ(3600, static_cast<int>(save.time_limit))
        << "and the clock the RULES step turned";

    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 7. The FOOTER's own walk. The tabs jump; PREV and NEXT step, and they are
//    what makes the strip a wizard. NEXT on GAME means "keep the arena that
//    is set" and SKIPS the ARENA step — forcing a level choice on a player
//    who only wanted to check the rules is the thing a wizard must not do —
//    while PREV walks every stop back, ARENA included. NEXT is hidden on
//    the last step and PREV on the first, so the ladder proves each step by
//    its tab and the two footer buttons by their presence.

namespace
{
struct NextPrevState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool prev_hidden_on_first = false;
    std::array<bool, 3> forward{};
    std::array<bool, 4> backward{};
    bool next_hidden_on_last = false;
    bool finished = false;
};

int next_prev_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NextPrevState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(0, "GAME", 15000);
    if (!state->opened)
        return escape(3, "the wizard never opened on GAME");
    (void)wait_for_menu_frames(2);
    // Nothing to step back to from the first step.
    state->prev_hidden_on_first = !has_interactable("setup_prev");

    // Press one id, watch ANOTHER: NEXT's own face never changes, so the
    // edge is the tab that wears the step it landed on.
    const auto tab_reads = [](int slot, const std::string& want,
                              int wait_ms) {
        return wait_for_interactable_label_matching(
            "setup_tab_" + std::to_string(slot),
            [&want](const std::string& label) { return label == want; },
            wait_ms);
    };
    // NEXT on GAME means "keep the arena that is set": it SKIPS ARENA
    // rather than forcing a level choice the player did not ask to make,
    // so the forward walk is three stops, not four.
    static const int kForwardTab[] = {2, 3, 4};
    static const char* const kWords[] = {"TEAMS", "RULES", "MATCH"};
    for (int k = 0; k < 3; ++k) {
        const std::string want = std::string("[") + kWords[k] + "]";
        const int tab = kForwardTab[k];
        state->forward[static_cast<std::size_t>(k)] = click_until_edge(
            "setup_next",
            [&tab_reads, tab, &want](int wait_ms) {
                return tab_reads(tab, want, wait_ms);
            },
            "step", 3, 10000, "setup");
        if (!state->forward[static_cast<std::size_t>(k)])
            return escape(4 + k, "NEXT did not reach the next step");
    }
    (void)wait_for_menu_frames(2);
    state->next_hidden_on_last = !has_interactable("setup_next");

    // PREV walks every stop, ARENA included: coming back DOWN the strip is
    // how a player reaches the arena list without knowing the tabs.
    static const int kBackTab[] = {3, 2, 1, 0};
    static const char* const kBack[] = {"RULES", "TEAMS", "ARENA", "GAME"};
    for (int k = 0; k < 4; ++k) {
        const std::string want = std::string("[") + kBack[k] + "]";
        const int tab = kBackTab[k];
        state->backward[static_cast<std::size_t>(k)] = click_until_edge(
            "setup_prev",
            [&tab_reads, tab, &want](int wait_ms) {
                return tab_reads(tab, want, wait_ms);
            },
            "step", 3, 10000, "setup");
        if (!state->backward[static_cast<std::size_t>(k)])
            return escape(8 + k, "PREV did not reach the previous step");
    }

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(12, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, warm_next_prev_through_every_step)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    NextPrevState state;
    SDL_Thread* thread =
        SDL_CreateThread(next_prev_injector, "setup_next_prev", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_TRUE(state.prev_hidden_on_first)
        << "the first step has nothing behind it, so PREV is not there";
    static const char* const kWords[] = {"TEAMS", "RULES", "MATCH"};
    for (int k = 0; k < 3; ++k) {
        EXPECT_TRUE(state.forward[static_cast<std::size_t>(k)])
            << "NEXT steps forward onto " << kWords[k];
    }
    EXPECT_TRUE(state.next_hidden_on_last)
        << "the last step is where GO lives, so NEXT is not there";
    static const char* const kBack[] = {"RULES", "TEAMS", "ARENA", "GAME"};
    for (int k = 0; k < 4; ++k) {
        EXPECT_TRUE(state.backward[static_cast<std::size_t>(k)])
            << "PREV steps back onto " << kBack[k];
    }
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 8. The two doors. TEAMS carries the LINEUP door and MATCH the VIEW
//    LEVEL door; both open a NESTED engine screen over the wizard and both
//    come back to the step that opened them.

namespace
{
struct DoorsState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool lineup_opened = false;
    bool back_on_teams = false;
    bool view_level_opened = false;
    bool back_on_match = false;
    bool finished = false;
};

int doors_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DoorsState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(2, "TEAMS", 15000);
    if (!state->opened)
        return escape(3, "the TEAMS step never came up");
    (void)wait_for_menu_frames(2);

    // The door's oracle is the nested screen's own id.
    state->lineup_opened = click_until_edge("setup_row_1", [](int wait_ms) {
        return wait_for_interactable("lineup_unite", wait_ms);
    }, nullptr, 3, 15000);
    if (!state->lineup_opened)
        return escape(4, "the LINEUP door did not open");
    (void)wait_for_menu_frames(2);
    state->back_on_teams = click_until_edge("back", [](int wait_ms) {
        return wait_for_interactable_label_matching(
            "setup_tab_2",
            [](const std::string& label) { return label == "[TEAMS]"; },
            wait_ms);
    });
    if (!state->back_on_teams)
        return escape(5, "LINEUP's BACK did not return to TEAMS");

    if (!open_setup_step(4, "MATCH", 15000))
        return escape(6, "the MATCH step never came up");
    (void)wait_for_menu_frames(2);
    // The viewer's own trace is the oracle: its BACK shares the id with
    // three other screens', and a geometry wait would be this file's
    // fourth copy of the disambiguator.
    state->view_level_opened = click_and_acknowledge_trace(
        "setup_row_0", "picker", "view_scenario lines=",
        /*waits_for_autosave=*/false, 15000);
    if (!state->view_level_opened)
        return escape(7, "the VIEW LEVEL door did not open");
    (void)wait_for_menu_frames(2);
    state->back_on_match = click_until_edge("back", [](int wait_ms) {
        return wait_for_interactable_label_matching(
            "setup_tab_4",
            [](const std::string& label) { return label == "[MATCH]"; },
            wait_ms);
    });
    if (!state->back_on_match)
        return escape(8, "VIEW LEVEL's BACK did not return to MATCH");

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(9, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, lineup_and_view_level_doors_round_trip)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    DoorsState state;
    SDL_Thread* thread =
        SDL_CreateThread(doors_injector, "setup_doors", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_TRUE(state.lineup_opened)
        << "the TEAMS step's second row is the LINEUP door";
    EXPECT_TRUE(state.back_on_teams)
        << "the nested screen comes back to the step that opened it";
    EXPECT_TRUE(state.view_level_opened);
    EXPECT_TRUE(state.back_on_match);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 9. A refusal NEVER advances (§2.3). Clicking the arena the cursor is
//    already on is the refusal every host can reach without a lobby: the
//    step says so on line B and stays exactly where it was.

namespace
{
struct RefusalState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool current_row_seen = false;
    bool still_on_arena = false;
    bool finished = false;
};

int refusal_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<RefusalState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(1, "ARENA", 15000);
    if (!state->opened)
        return escape(3, "the ARENA step never came up");
    (void)wait_for_menu_frames(2);
    state->current_row_seen =
        interactable_label("setup_row_0").find("[CURRENT]") !=
        std::string::npos;

    // The cursor's own row. Acknowledged by the refusal's OWN trace, so a
    // press that landed on a slow frame is never re-sent into a set.
    // A refusal writes nothing, so it never autosaves.
    (void)click_and_acknowledge_trace("setup_row_0", "zone",
                                      "level_unchanged",
                                      /*waits_for_autosave=*/false, 10000);
    (void)wait_for_menu_frames(2);
    state->still_on_arena =
        interactable_label("setup_tab_1") == "[ARENA]";

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(4, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, refusals_never_advance)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    RefusalState state;
    SDL_Thread* thread =
        SDL_CreateThread(refusal_injector, "setup_refusal", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_TRUE(state.current_row_seen)
        << "the ARENA step opens on the window holding the cursor's arena";
    EXPECT_TRUE(trace_contains("zone", "level_unchanged"))
        << "the shared answer switch refuses an unchanged cursor";
    EXPECT_TRUE(trace_contains(
        "setup", std::string(og::ui::kCampaignLevelUnchangedMessage).c_str()))
        << "and the wizard says it on line B, in the same voice the zone "
           "submenu and the Base Camp docket use";
    EXPECT_TRUE(state.still_on_arena)
        << "a refusal NEVER advances the step (§2.3)";
    EXPECT_EQ(820, static_cast<int>(
                       og::runtime::current_session->myscreen_
                           ->save_data.scen_num))
        << "and it moves no cursor";
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 10. The JOINER's wizard. Everything the host turns is read-only here, the
//     level rows refuse in words rather than disappearing, and the GO slot
//     holds the pointer row instead — READY is the strip's, not the step's.
//     The whole point of showing a joiner the five steps is that they can
//     read what they are about to be dropped into.

namespace
{
// A networked lobby this machine is a GUEST in: two seats, ours is the
// second. lobby_players() rebuilds per call, so the menu thread and the
// injector never share mutable vector storage.
class JoinerLobbyClient : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return std::nullopt;
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer>
    lobby_players() const override
    {
        og::sim::LobbyPlayer host;
        host.player_index = 0;
        host.name = "net-0000000000000000";
        host.company = "RED LANTERN";
        host.team = 0;
        host.is_host = true;
        og::sim::LobbyPlayer guest;
        guest.player_index = 1;
        guest.name = "net-0000000000000001";
        guest.company = "IRON KETTLE";
        guest.team = 1;
        return {host, guest};
    }
    [[nodiscard]] std::vector<std::uint8_t>
    local_player_indices() const override
    {
        return {1};
    }
};

struct ActiveLobbyGuard
{
    og::ui::IPickerLobbyClient* saved = nullptr;
    explicit ActiveLobbyGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }
    ~ActiveLobbyGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

struct JoinerState
{
    std::atomic<bool> test_finished{false};
    bool opened_rules = false;
    bool arena_refused = false;
    bool still_on_arena = false;
    bool rules_has_no_rows = false;
    bool game_has_no_random_row = false;
    std::string game_last_row;
    std::string match_last_row;
    bool finished = false;
    int captures = 0;
    Landing rules_landing, arena_landing, match_landing;
    bool nav_woke = true;
};

int joiner_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<JoinerState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(1, "the versus docket never showed its SETUP row");

    state->opened_rules = open_setup_step(3, "RULES", 15000);
    if (!state->opened_rules)
        return escape(2, "the RULES step never came up");
    (void)wait_for_menu_frames(2);
    // R2-3: a joiner's RULES is the caption and the ONE packed line. No
    // rows at all — not even a read-only one.
    state->rules_has_no_rows = !has_interactable("setup_row_0");
    // P1 on a step with no live row at all: every rule is a LINE here and
    // the single CROSS CONTROL row is read-only, so the keyboard takes the
    // footer's way on rather than the inert tab. NEXT's RIGHT is the end
    // of the footer chain, so that is this shot's no-op wake.
    state->rules_landing = read_landing();
    state->nav_woke = wake_nav_highlight(KEY_RIGHT);
    capture_presented_frame("setup_joiner_rules", uxshots_dir());
    ++state->captures;

    if (!open_setup_step(1, "ARENA", 15000))
        return escape(3, "the ARENA step never came up");
    (void)wait_for_menu_frames(2);
    // The row is NOT hidden for a joiner — it refuses in words.
    // A refusal writes nothing, so it never autosaves.
    (void)click_and_acknowledge_trace("setup_row_0", "zone",
                                      "level_denied_nonhost",
                                      /*waits_for_autosave=*/false, 10000);
    (void)wait_for_menu_frames(2);
    state->arena_refused = trace_contains("zone", "level_denied_nonhost");
    state->still_on_arena = interactable_label("setup_tab_1") == "[ARENA]";
    state->arena_landing = read_landing();

    if (!open_setup_step(4, "MATCH", 15000))
        return escape(4, "the MATCH step never came up");
    (void)wait_for_menu_frames(2);
    state->match_last_row = interactable_label("setup_row_1");
    state->match_landing = read_landing();

    // R2-5: the RANDOM rows are host-gated in the book's own fetch, so a
    // joiner's GAME step ends on the last game and carries no roll.
    if (open_setup_step(0, "GAME", 15000)) {
        (void)wait_for_menu_frames(2);
        state->game_has_no_random_row = !has_interactable("setup_row_7");
        state->game_last_row = interactable_label("setup_row_6");
    }

    // A joiner's strip wears READY where the host's wears GO, so the
    // camp's own door is the edge that says the wizard closed.
    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("zone_action_0", wait_ms);
        }))
    {
        return escape(5, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, joiner_read_only_walk_and_ready_row)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    JoinerLobbyClient lobby;
    ActiveLobbyGuard lobby_guard(&lobby);

    JoinerState state;
    SDL_Thread* thread =
        SDL_CreateThread(joiner_injector, "setup_joiner", &state);
    ASSERT_NE(nullptr, thread);
    picker_load_menu_backdrops();
    create_team_menu(0);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened_rules)
        << "a joiner opens the wizard too: reading the match is the point";
    EXPECT_TRUE(state.rules_has_no_rows)
        << "R2-3: the joiner's RULES step is the caption and ONE packed "
           "line — no rows, read-only or otherwise";
    EXPECT_TRUE(state.game_has_no_random_row)
        << "R2-5: RANDOM is host-gated in the book's fetch, so a joiner's "
           "GAME step has no eighth row to press";
    EXPECT_NE(std::string::npos, state.game_last_row.find("FREE FOR ALL"))
        << "and its last row is the last GAME, not a dead roll: '"
        << state.game_last_row << "'";
    EXPECT_TRUE(state.arena_refused)
        << "a joiner's level row refuses in WORDS — it is never hidden, "
           "because a joiner has to be able to read which arenas exist";
    EXPECT_TRUE(trace_contains(
        "setup", std::string(og::ui::kCampaignPickerHostGuardMessage).c_str()))
        << "and the refusal is the engine's one sentence for it";
    EXPECT_TRUE(state.still_on_arena) << "a refusal never advances";
    // P1 for the read-only viewer, step by step.
    EXPECT_EQ("setup_next", state.rules_landing.id)
        << "the joiner's RULES step carries ONE read-only row, so the "
           "keyboard takes the footer's way on, not the pressed-in tab — "
           "landed on '" << state.rules_landing.id << "'";
    EXPECT_TRUE(state.nav_woke)
        << "the joiner capture wakes the ring with a no-op nav step";
    EXPECT_NE(std::string::npos, state.arena_landing.label.find("[CURRENT]"))
        << "a joiner's ARENA lands on the arena the host has set: '"
        << state.arena_landing.label << "'";
    EXPECT_EQ("setup_row_0", state.match_landing.id)
        << "the joiner's GO slot is the Disabled READY pointer, so MATCH "
           "lands on VIEW LEVEL — the one row a joiner can press";
    EXPECT_EQ(std::string(og::ui::kSetupJoinerReadyRow),
              state.match_last_row)
        << "the joiner's GO slot holds the pointer row: READY is the Base "
           "Camp strip's, and the wizard says where — '"
        << state.match_last_row << "'";
    verify_captured_frames("setup_joiner", 1);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 11. A paged ARENA window. CTF ships ten arenas plus the RANDOM ARENA row
//     and the step fits eight, so the pagers un-park beside the first row
//     and the window can be stepped. The step opens on the window holding
//     [CURRENT] (D27) — a host who sees rows none of which is theirs
//     cannot tell what GO would launch. The cursor is 508, the FIRST row
//     of window 2: on a window-1 cursor "opens on the window holding
//     [CURRENT]" passes without the step having chosen anything.

namespace
{
struct PagerState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool pagers_shown = false;
    bool current_on_entry = false;
    bool random_row_on_last_window = false;
    std::string first_window_row_0;
    bool stepped = false;
    bool stepped_back = false;
    bool finished = false;
    int captures = 0;
};

int pager_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<PagerState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(1, "ARENA", 15000);
    if (!state->opened)
        return escape(3, "the ARENA step never came up");
    (void)wait_for_menu_frames(2);
    state->pagers_shown = has_interactable("setup_page_next") &&
        has_interactable("setup_page_prev");
    // The one shot that shows the pager pair and the "p/N" under it
    // (SPEC §2.3): no unpaged step can.
    capture_presented_frame("setup_arena_paged", uxshots_dir());
    ++state->captures;
    // The window the step opened on holds the cursor's arena — window 2,
    // whose first row IS the cursor (508). The RANDOM ARENA row is
    // appended LAST of all, so it rides this window too and no arena
    // ordinal moved to make room for it (D5).
    for (int r = 0; r < og::ui::kSetupRowsMax; ++r) {
        const std::string label =
            interactable_label("setup_row_" + std::to_string(r));
        if (label.find("[CURRENT]") != std::string::npos)
            state->current_on_entry = true;
        if (label.find("RANDOM ARENA") != std::string::npos)
            state->random_row_on_last_window = true;
    }

    // A page step moves a window, not a setting: nothing autosaves. The
    // step opened on 2/2, so PREV is the first move and NEXT comes back.
    state->stepped_back = click_and_acknowledge_trace(
        "setup_page_prev", "setup", "page 1/2",
        /*waits_for_autosave=*/false, 10000);
    state->first_window_row_0 = interactable_label("setup_row_0");
    state->stepped = click_and_acknowledge_trace(
        "setup_page_next", "setup", "page 2/2",
        /*waits_for_autosave=*/false, 10000);

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(4, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, paged_arena_window_steps_and_opens_on_the_cursor)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // CTF: ten arenas and the RANDOM ARENA row over an eight-row floor.
    // 508 is index 8 — the first row of window 2.
    write_versus_save("modes", 508);

    PagerState state;
    SDL_Thread* thread =
        SDL_CreateThread(pager_injector, "setup_pager", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_TRUE(state.pagers_shown)
        << "eleven rows over an eight-row floor: the pagers un-park";
    EXPECT_TRUE(state.current_on_entry)
        << "D27: the step opens on the window that holds [CURRENT], and "
           "508 is the FIRST row of window 2 — a window-1 cursor would "
           "pass this without the step choosing anything";
    EXPECT_TRUE(state.random_row_on_last_window)
        << "D5: RANDOM ARENA is appended LAST, so it rides the last "
           "window and no arena ordinal moved to make room for it";
    EXPECT_TRUE(state.stepped_back) << "'<' steps the window to 1/2";
    EXPECT_TRUE(state.stepped) << "and '>' steps it back to 2/2";
    EXPECT_EQ(std::string::npos,
              state.first_window_row_0.find("RANDOM"))
        << "window 1 is arenas only (500 up): '"
        << state.first_window_row_0 << "'";
    verify_captured_frames("setup_pager", 1);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// (Section 12, the wizard's own DIFFICULTY row, is retired with R2-3: the
//  seven rules the RULES step had swallowed went back to the Base Camp
//  DIFFICULTY screen, whose value-taking tail has its own ladder in
//  tests/integration/test_difficulty.cpp.)

// ---------------------------------------------------------------------------
// 13. BACK closes the wizard from EVERY step (SPEC §4.1). The footer's BACK
//     carries Escape's hotkey (pinned statically in test_menu_layout), and
//     it is the ONE way out of all five steps: a wizard a player can walk
//     into and not out of is the hang this flow exists to forbid.

namespace
{
struct EscapeEveryStepState
{
    std::atomic<bool> test_finished{false};
    bool camp_seen = false;
    bool door_seen = false;
    int steps_opened = 0;
    int steps_closed = 0;
    std::string stuck_on;
    bool finished = false;
};

int escape_every_step_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<EscapeEveryStepState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    state->camp_seen = wait_for_interactable("continue_game", 10000);
    if (!state->camp_seen)
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    state->door_seen = wait_for_interactable("zone_action_0", 15000);
    if (!state->door_seen)
        return escape(2, "the versus docket never showed its SETUP row");

    static constexpr const char* kWords[] = {"GAME", "ARENA", "TEAMS",
                                             "RULES", "MATCH"};
    for (int step = 0; step < 5; ++step) {
        // open_setup_step re-opens the wizard through the strip door when
        // the tabs are gone, so each lap is a fresh entry and the close is
        // the thing under test.
        if (!open_setup_step(step, kWords[step], 15000)) {
            state->stuck_on = kWords[step];
            return escape(3 + step, "a step never came up");
        }
        ++state->steps_opened;
        (void)wait_for_menu_frames(2);
        // The camp's own GO is the edge that says the wizard is gone: it
        // is not live anywhere inside it.
        if (!click_until_edge("setup_back", [](int wait_ms) {
                return wait_for_interactable("go", wait_ms);
            }))
        {
            state->stuck_on = kWords[step];
            return escape(8 + step, "BACK did not close the wizard");
        }
        ++state->steps_closed;
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, escape_closes_from_every_step)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    EscapeEveryStepState state;
    SDL_Thread* thread =
        SDL_CreateThread(escape_every_step_injector, "setup_escape", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result << " (on "
        << (state.stuck_on.empty() ? std::string("<no step>")
                                   : state.stuck_on)
        << ")";
    EXPECT_EQ(5, state.steps_opened) << "every step must be reachable";
    EXPECT_EQ(5, state.steps_closed)
        << "and BACK closes the wizard from each of them";
    EXPECT_TRUE(state.finished);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 14. A host SET LEVEL while a joiner is parked in the wizard (SPEC §1.3's
//     last paragraph): the level-reload guard in the frame tick refetches
//     the step the joiner is standing on, so the arena rows re-decorate
//     under them instead of describing a match nobody is in any more.

namespace
{
struct ParkedJoinerState
{
    std::atomic<bool> test_finished{false};
    bool opened_arena = false;
    std::string before_row;
    std::string after_row;
    bool refetched = false;
    bool level_set = false;
    bool finished = false;
};

int parked_joiner_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<ParkedJoinerState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(1, "the versus docket never showed its SETUP row");
    state->opened_arena = open_setup_step(1, "ARENA", 15000);
    if (!state->opened_arena)
        return escape(2, "the ARENA step never came up");
    (void)wait_for_menu_frames(2);
    // Row 0 is THE PITCH (820) and wears [CURRENT] while the host's cursor
    // is there.
    state->before_row = interactable_label("setup_row_0");

    // The host sets 821. On a joiner that arrives as a save write under
    // the open screen (the lobby's settings sync), which is exactly the
    // cursor the frame tick's level-reload guard watches.
    if (!run_on_main_thread([] {
            og::runtime::current_session->myscreen_->save_data.scen_num = 821;
        }))
    {
        return escape(3, "the level change never reached the menu thread");
    }
    state->level_set = true;

    // The oracle is the DECORATION moving to the new arena's row — proof
    // the parked step recomposed, not just that the save changed.
    state->refetched = wait_for_interactable_label_matching(
        "setup_row_1",
        [](const std::string& label) {
            return label.find("[CURRENT]") != std::string::npos;
        },
        15000);
    state->after_row = interactable_label("setup_row_1");

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("zone_action_0", wait_ms);
        }))
    {
        return escape(4, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, host_level_change_refetches_a_parked_joiner)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    JoinerLobbyClient lobby;
    ActiveLobbyGuard lobby_guard(&lobby);

    ParkedJoinerState state;
    SDL_Thread* thread =
        SDL_CreateThread(parked_joiner_injector, "setup_parked", &state);
    ASSERT_NE(nullptr, thread);
    picker_load_menu_backdrops();
    create_team_menu(0);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened_arena);
    ASSERT_TRUE(state.level_set);
    EXPECT_NE(std::string::npos, state.before_row.find("[CURRENT]"))
        << "the joiner arrives on the host's arena: '" << state.before_row
        << "'";
    EXPECT_TRUE(state.refetched)
        << "a host level change must refetch the step the joiner is parked "
           "on — row 1 still reads '" << state.after_row << "'";
    EXPECT_TRUE(trace_contains("setup", "level_reload"))
        << "and it goes through the frame tick's level-reload guard";
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 15. The host's GO reaches a joiner parked ANYWHERE in the wizard (SPEC
//     §1.3: `remote_start = RemoteStartScope::TeamBuildScope`, like every
//     Base Camp child). The runner preempts the screen, the blocking
//     wrapper maps that MENU_EXIT to RemoteStart, and Base Camp folds it
//     into its own exit so the launch runs.

namespace
{
// The smallest honest stub: a joiner whose lobby HAS an accepted start
// config, which is the one thing team_build_remote_start_requested asks
// about beyond the request flag.
class RemoteStartLobbyClient final : public JoinerLobbyClient
{
public:
    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return true;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        og::ui::PickerLobbyGameStartConfig config;
        config.is_networked = true;
        config.local_player_index = 1;
        config.local_player_indices = {1};
        config.local_seat_teams = {1};
        return config;
    }
};

struct RemoteStartState
{
    std::atomic<bool> test_finished{false};
    int step = 0;
    // The leg that enters through Base Camp's own SETUP door instead of
    // opening the screen directly (the fold leg).
    bool from_camp = false;
    bool reached_step = false;
    bool armed = false;
};

int remote_start_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<RemoteStartState*>(data);
    // Once the start is armed there is nothing for the tail to CLOSE: the
    // remote start is what ends the screen, and a BACK press racing it
    // would close the wizard on its own and prove nothing.
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(
            state->test_finished, leg, why, kSetupEscapeDoors,
            [state] { return state->armed; });
    };

    static constexpr const char* kWords[] = {"GAME", "ARENA", "TEAMS",
                                             "RULES", "MATCH"};
    if (!wait_for_interactable(
            state->from_camp ? "zone_action_0" : "setup_tab_0", 15000))
    {
        return escape(1, "the wizard's way in never came up");
    }
    state->reached_step = open_setup_step(state->step, kWords[state->step],
                                          15000);
    if (!state->reached_step)
        return escape(2, "the step never came up");
    (void)wait_for_menu_frames(2);

    if (!run_on_main_thread([] { g_start_game_requested = true; }))
        return escape(3, "the start request never reached the menu thread");
    state->armed = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, remote_start_from_every_step)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    RemoteStartLobbyClient lobby;
    ActiveLobbyGuard lobby_guard(&lobby);
    picker_load_menu_backdrops();

    static constexpr const char* kWords[] = {"GAME", "ARENA", "TEAMS",
                                             "RULES", "MATCH"};
    for (int step = 0; step < 5; ++step) {
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;

        RemoteStartState state;
        state.step = step;
        SDL_Thread* thread =
            SDL_CreateThread(remote_start_injector, "setup_remote", &state);
        ASSERT_NE(nullptr, thread);
        const og::ui::MatchSetupExit exit =
            og::ui::run_match_setup_screen(std::string_view());
        state.test_finished.store(true);
        int thread_result = 0;
        SDL_WaitThread(thread, &thread_result);
        escape_tail_join_hygiene();

        EXPECT_EQ(0, thread_result)
            << kWords[step] << ": the injector gave up at leg "
            << thread_result;
        EXPECT_TRUE(state.reached_step) << kWords[step];
        EXPECT_EQ(og::ui::MatchSetupExit::RemoteStart, exit)
            << kWords[step]
            << ": a host GO reaching a joiner parked on this step is a "
               "REMOTE START, never this screen's own close";
        EXPECT_TRUE(team_build_start_selected())
            << kWords[step] << ": and the unwind target is START GAME";
    }

    // And Base Camp folds that answer into its own exit, which is what
    // actually launches the joiner (base_camp_open_match_setup).
    {
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;
        RemoteStartState state;
        state.step = 4;
        // The camp's own door, not the wizard's: this leg enters through
        // Base Camp so the fold is the thing under test.
        state.from_camp = true;
        SDL_Thread* thread =
            SDL_CreateThread(remote_start_injector, "setup_remote_fold",
                             &state);
        ASSERT_NE(nullptr, thread);
        create_team_menu(0);
        state.test_finished.store(true);
        int thread_result = 0;
        SDL_WaitThread(thread, &thread_result);
        escape_tail_join_hygiene();
        EXPECT_EQ(0, thread_result)
            << "the fold leg gave up at leg " << thread_result;
        EXPECT_TRUE(team_build_start_selected())
            << "Base Camp exits with START GAME selected, so the launch "
               "runs for the parked joiner";
    }

    g_start_game_requested = false;
    pks().selected_menu_item = nullptr;
    cleanup_picker_state();
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 17. The GAME step's RANDOM row (R2-5: "Add a RANDOM button to the wizard
//     for game type and map"). It is an ACTION row that answers a LEVEL, so
//     it rides the engine's own gated set tail — host gate, load rollback,
//     the one "Level set to <arena>." line — and lands the wizard on TEAMS
//     exactly as a level row does. Appended LAST (D5), so it is row 7 under
//     the seven games and no game's ordinal moved to make room for it.

namespace
{
struct RandomRowState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    std::string random_row;
    bool rolled = false;
    bool landed_on_teams = false;
    std::string arena_current_row;
    int scen_after = 0;
    bool finished = false;
};

int random_row_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<RandomRowState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(0, "GAME", 15000);
    if (!state->opened)
        return escape(3, "the GAME step never came up");
    (void)wait_for_menu_frames(2);
    state->random_row = interactable_label("setup_row_7");

    // The roll's landing is the step it advances into: a refusal never
    // advances, and the toast is the witness that keeps a press that DID
    // land from being re-sent onto a second roll.
    state->rolled = click_until_edge(
        "setup_row_7",
        [](int wait_ms) {
            return wait_for_interactable_label_matching(
                "setup_tab_2",
                [](const std::string& label) { return label == "[TEAMS]"; },
                wait_ms);
        },
        "toast Level set to", 3, 15000, "setup");
    if (!state->rolled)
        return escape(4, "the RANDOM row never set an arena");
    state->landed_on_teams = true;

    // What it set, read where the engine keeps it.
    (void)run_on_main_thread([state] {
        state->scen_after =
            og::runtime::current_session->myscreen_->save_data.scen_num;
    });

    // ...and the ARENA step follows the new cursor: its [CURRENT] row is
    // the arena the roll chose, which is not the one the save came in on.
    if (open_setup_step(1, "ARENA", 15000)) {
        (void)wait_for_menu_frames(2);
        for (int r = 0; r < og::ui::kSetupRowsMax; ++r) {
            const std::string label =
                interactable_label("setup_row_" + std::to_string(r));
            if (label.find("[CURRENT]") != std::string::npos)
                state->arena_current_row = label;
        }
    }

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(5, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, random_row_sets_an_arena_and_lands_on_teams)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);

    RandomRowState state;
    SDL_Thread* thread =
        SDL_CreateThread(random_row_injector, "setup_random", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_EQ("RANDOM - any game, any arena", state.random_row)
        << "the roll is the GAME step's LAST row: '" << state.random_row
        << "'";
    EXPECT_TRUE(state.landed_on_teams)
        << "an Acted row that answers a level runs the level tail and "
           "advances, exactly as a level row does";
    EXPECT_GE(state.scen_after, 300);
    EXPECT_LE(state.scen_after, 899);
    EXPECT_NE(820, state.scen_after)
        << "a roll that lands on the arena the campaign is already set to "
           "steps one row on: a button that changes nothing is not a roll";
    EXPECT_FALSE(state.arena_current_row.empty())
        << "the ARENA step follows the new cursor";
    EXPECT_EQ(std::string::npos, state.arena_current_row.find("THE PITCH"))
        << "and [CURRENT] is on the arena the roll chose, not the one the "
           "save came in on: '" << state.arena_current_row << "'";
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 18. R2-2, end to end on a FOUR-side arena: "the FILL: selector from the
//     wizard only changes teams 1 and 2, even for 4-player maps".
//
//     Two frames prove the fix, and the second one is the trap the first
//     could not see. (a) From the arena's own STRONG deal, ONE FILL turn
//     moves EVERY authored band together — BRUTAL on teams 1..4, SIDES
//     still 4. (b) With every band wheeled to NONE on the LINEUP page —
//     the state the shipped bug collapsed companies into, and the state a
//     v19 save heals to — a FILL turn lights every authored opponent
//     again, not just the lowest: SIDES back to 4 and WEAK on all four.
//
//     The oracle is the save's own fill array, read on the menu thread,
//     plus the two captures R2-7's media needs.

namespace
{
struct FourSideFillState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    std::string rest_sides_row;
    std::string rest_fill_row;
    bool turned_to_brutal = false;
    bool restaged_after_turn = false;
    bool restaged_after_empty = false;
    bool restaged_after_heal = false;
    std::array<int, 4> fill_after_turn{};
    bool lineup_opened = false;
    bool bands_emptied = false;
    bool back_on_teams = false;
    std::string empty_sides_row;
    std::string empty_fill_row;
    bool healed = false;
    std::string healed_sides_row;
    std::string healed_fill_row;
    std::array<int, 4> fill_after_heal{};
    bool finished = false;
    int captures = 0;
};

void read_fill(std::array<int, 4>& out)
{
    (void)run_on_main_thread([&out] {
        const SaveData& save = test_screen()->save_data;
        for (std::size_t t = 0; t < out.size(); ++t)
            out[t] = static_cast<int>(save.fill[t]);
    });
}

int four_side_fill_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<FourSideFillState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("zone_action_0", 15000))
        return escape(2, "the versus docket never showed its SETUP row");

    state->opened = open_setup_step(2, "TEAMS", 15000);
    if (!state->opened)
        return escape(3, "the TEAMS step never came up");
    (void)wait_for_menu_frames(2);
    state->rest_sides_row = interactable_label("setup_row_0");
    state->rest_fill_row = interactable_label("setup_row_1");
    // The BEFORE half of R2-7's four-side pair, taken from THIS flow so the
    // only difference between it and the _brutal frame is the one click.
    // (The older `setup_step_teams_four_sides` shot is the GO-gate test's
    // under-deployed company -- a different roster, so it cannot serve as a
    // one-click before.)
    capture_presented_frame("setup_step_teams_four_sides_rest",
                            uxshots_dir());
    ++state->captures;

    // (a) ONE turn off the deal. STRONG -> BRUTAL is the frame that shows
    // every band moving UP together; the next turn would wrap to WEAK and
    // read as FILL going down, so it is not the shot.
    //
    // The team LINES do not answer on the click: their numbers come from a
    // restage the turn only QUEUES (a trailing-edge debounce), so a frame
    // taken right after the wheel moved still carries the previous
    // state's census — a shot of FILL: WEAK over the BRUTAL body count.
    // Clear the buffer, turn, then wait for the stage to announce itself.
    trace_clear();
    state->turned_to_brutal = click_until_label_containing(
        "setup_row_1", "FILL: BRUTAL", 3, 10000, "turned", "setup");
    if (!state->turned_to_brutal)
        return escape(4, "the FILL wheel never reached BRUTAL");
    state->restaged_after_turn = wait_for_trace("setup", "staged", 15000);
    (void)wait_for_menu_frames(2);
    read_fill(state->fill_after_turn);
    capture_presented_frame("setup_step_teams_four_sides_brutal",
                            uxshots_dir());
    ++state->captures;

    // (b) empty every band from the LINEUP page (row 2 is the door on a
    // four-side arena: SIDES, FILL, LINEUP).
    trace_clear();
    state->lineup_opened = click_until_edge("setup_row_2", [](int wait_ms) {
        return wait_for_interactable("lineup_unite", wait_ms);
    });
    if (!state->lineup_opened)
        return escape(5, "the LINEUP door never opened");
    (void)wait_for_menu_frames(2);
    state->bands_emptied = true;
    for (int t = 0; t < 4; ++t) {
        // The band wheel keeps NONE (kLineupFillNote, "none to brutal"),
        // and BRUTAL wraps straight onto it.
        state->bands_emptied =
            click_until_label("lineup_fill_" + std::to_string(t),
                              "FILL: NONE", 3, 10000, "fill team=",
                              "lineup") &&
            state->bands_emptied;
    }
    state->back_on_teams = click_until_edge("back", [](int wait_ms) {
        return wait_for_interactable_label_matching(
            "setup_tab_2",
            [](const std::string& label) { return label == "[TEAMS]"; },
            wait_ms);
    });
    if (!state->back_on_teams)
        return escape(6, "LINEUP's BACK did not return to TEAMS");
    (void)wait_for_menu_frames(2);
    state->empty_sides_row = interactable_label("setup_row_0");
    state->empty_fill_row = interactable_label("setup_row_1");
    // The BEFORE half of the trap pair: the collapsed state itself, the one
    // the shipped bug left companies in. The team LINES lag the rows here
    // for the same reason the turn above does -- emptying the bands only
    // QUEUES the restage that recounts them -- so wait for the stage to
    // announce itself or the frame carries the previous census.
    state->restaged_after_empty = wait_for_trace("setup", "staged", 15000);
    (void)wait_for_menu_frames(2);
    capture_presented_frame("setup_step_teams_four_sides_empty",
                            uxshots_dir());
    ++state->captures;

    // ...and ONE turn out of the collapse, settled on the same oracle.
    trace_clear();
    state->healed = click_until_label_containing(
        "setup_row_1", "FILL: WEAK", 3, 10000, "turned", "setup");
    if (!state->healed)
        return escape(7, "the FILL wheel never re-lit the bands");
    state->restaged_after_heal = wait_for_trace("setup", "staged", 15000);
    (void)wait_for_menu_frames(2);
    state->healed_sides_row = interactable_label("setup_row_0");
    state->healed_fill_row = interactable_label("setup_row_1");
    read_fill(state->fill_after_heal);
    capture_presented_frame("setup_step_teams_four_sides_healed",
                            uxshots_dir());
    ++state->captures;

    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        }))
    {
        return escape(8, "BACK did not close the wizard");
    }
    state->finished = true;
    return escape(0, "");
}
} // namespace

TEST(MatchSetupUi, fill_turn_moves_every_side_on_a_four_side_arena)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // FOURSQUARE (822): four authored sides, a ball arena, so the deal is
    // STRONG on every band before a knob is touched.
    write_versus_save("modes", 822);

    FourSideFillState state;
    SDL_Thread* thread =
        SDL_CreateThread(four_side_fill_injector, "setup_four_fill", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.opened);
    EXPECT_NE(std::string::npos, state.rest_sides_row.find("SIDES: 4"))
        << "the arena authors four sides: '" << state.rest_sides_row << "'";
    EXPECT_NE(std::string::npos, state.rest_fill_row.find("FILL: STRONG"))
        << "a fresh ball arena is dealt STRONG: '" << state.rest_fill_row
        << "'";
    EXPECT_NE(std::string::npos, state.rest_fill_row.find("weak to brutal"))
        << "R2-2: NONE left the wizard's wheel — in the wizard it only "
           "ever emptied a versus arena: '" << state.rest_fill_row << "'";
    ASSERT_TRUE(state.turned_to_brutal);
    EXPECT_TRUE(state.restaged_after_turn)
        << "the team lines' numbers come from a restage the turn queues; "
           "a frame taken before it lands shows the previous census";
    const std::array<int, 4> kAllBrutal{
        og::sim::kFillBrutal, og::sim::kFillBrutal, og::sim::kFillBrutal,
        og::sim::kFillBrutal};
    EXPECT_EQ(kAllBrutal, state.fill_after_turn)
        << "one FILL turn moves EVERY authored band, not teams 1 and 2";

    ASSERT_TRUE(state.lineup_opened);
    EXPECT_TRUE(state.bands_emptied)
        << "LINEUP keeps per-team NONE, which is how a company collapses "
           "into the reported state in the first place";
    ASSERT_TRUE(state.back_on_teams);
    EXPECT_TRUE(state.restaged_after_empty)
        << "the emptied bands recount on a queued restage too";
    EXPECT_NE(std::string::npos, state.empty_sides_row.find("SIDES: 1"))
        << "with no band on, the step honestly says one side: '"
        << state.empty_sides_row << "'";
    ASSERT_TRUE(state.healed);
    EXPECT_TRUE(state.restaged_after_heal)
        << "same for the healed frame";
    EXPECT_NE(std::string::npos, state.healed_sides_row.find("SIDES: 4"))
        << "and ONE turn lights every authored opponent again: '"
        << state.healed_sides_row << "'";
    EXPECT_NE(std::string::npos, state.healed_fill_row.find("FILL: WEAK"))
        << "'" << state.healed_fill_row << "'";
    const std::array<int, 4> kAllWeak{
        og::sim::kFillWeak, og::sim::kFillWeak, og::sim::kFillWeak,
        og::sim::kFillWeak};
    EXPECT_EQ(kAllWeak, state.fill_after_heal)
        << "every authored side, not just the lowest (R2-2 fix B)";
    verify_captured_frames("setup_four_fill", 4);
    restore_gladiator_mount();
}
